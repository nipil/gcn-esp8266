import logging
import queue
from typing import Any

from paho.mqtt.client import Client, ConnectFlags, DisconnectFlags, ReasonCodes, Properties, MQTTMessage, \
    MQTTMessageInfo, MQTT_LOG_INFO, MQTT_LOG_NOTICE, MQTT_LOG_WARNING, MQTT_LOG_ERR, MQTT_LOG_DEBUG

from gcn_manager import AppError
from gcn_manager.constants import *


# https://eclipse.dev/paho/files/paho.mqtt.python/html/client.html
# https://eclipse.dev/paho/files/paho.mqtt.python/html/types.html

class MqttApp:

    def __init__(self, client: Client, in_queue: queue.Queue, *, mqtt_client_id: str) -> None:
        self._paho_client = client
        self._mqtt_client_id = mqtt_client_id
        self._in_queue = in_queue

    def _on_connect(self, client: Client, user_data: Any, connect_flags: ConnectFlags, reason_code: ReasonCodes,
                    properties: Properties) -> None:
        logging.info(f"Connected, "
                     f"reason code {reason_code.getName()}, "
                     f"session_present {connect_flags.session_present}, "
                     f"properties {properties}")
        self._set_status(online=True)
        self._paho_client.subscribe(MQTT_APP_MANAGER_SUBSCRIPTION, 1)

    def _on_connect_fail(self, client: Client, user_data: Any) -> None:
        logging.info(f"Communication not established, reconnecting...")

    def _on_disconnect(self, client: Client, user_data: Any, disconnect_flags: DisconnectFlags,
                       reason_code: ReasonCodes, properties: Properties) -> None:
        logging.info(f"Disconnected, "
                     f"reason code {reason_code.getName()}, "
                     f"session_present {disconnect_flags.is_disconnect_packet_from_server}, "
                     f"properties {properties}")

    def _on_log(self, client: Client, user_data: Any, level: int, buf: str) -> None:
        if level == MQTT_LOG_INFO:
            level = "INFO"
        elif level == MQTT_LOG_NOTICE:
            level = "NOTICE"
        elif level == MQTT_LOG_WARNING:
            level = "WARNING"
        elif level == MQTT_LOG_ERR:
            level = "ERROR"
        elif level == MQTT_LOG_DEBUG:
            level = "DEBUG"
        else:
            raise AppError(f"Unknown MQTT log level {level}")
        logging.debug(f"MQTT log {level} {buf}")

    def _on_message(self, client: Client, user_data: Any, msg: MQTTMessage) -> None:
        logging.debug(f"Received message: "
                      f"timestamp {msg.timestamp}, "
                      f"state {msg.state}, "
                      f"dup {msg.dup}, "
                      f"mid {msg.mid}, "
                      f"qos {msg.qos}, "
                      f"retain {msg.retain}, "
                      f"info {msg.info}, "  # MQTTMessageInfo.wait_for_publish(timeout_sec)
                      f"properties {msg.properties}, "
                      f"topic {msg.topic}, "
                      f"dup {msg.dup}, "
                      f"dup {msg.dup}, "
                      f"dup {msg.dup}, "
                      f"payload {msg.payload}")

    def _on_pre_connect(self, client: Client, user_data: Any) -> None:
        logging.debug(f"Failed to connect")

    def _on_publish(self, client: Client, user_data: Any, mid: int, reason_code: ReasonCodes,
                    properties: Properties) -> None:
        logging.info(f"Sent message: "
                     f"mid {mid}, "
                     f"reason code {reason_code.getName()}, "
                     f"properties {properties}")

    def _on_socket_close(self, client: Client, user_data: Any, socket) -> None:
        logging.debug(f"Socket {socket} is about to close")

    def _on_socket_open(self, client: Client, user_data: Any, socket) -> None:
        logging.debug(f"Socket {socket} has just been open")

    def _on_socket_register_write(self, client: Client, user_data: Any, socket) -> None:
        logging.debug(f"Socket {socket} needs writing but cannot")

    def _on_socket_unregister_write(self, client: Client, user_data: Any, socket) -> None:
        logging.debug(f"Socket {socket} does not needs writing anymore")

    def _on_subscribe(self, client: Client, user_data: Any, mid: int, reason_code_list: list[ReasonCodes],
                      properties: Properties) -> None:
        logging.info(f"Broker responded to subscribe "
                     f"mid {mid}, "
                     f"reason_codes {reason_code_list}, "
                     f"properties {properties}")

    def _on_unsubscribe(self, client: Client, user_data: Any, mid: int, reason_code_list: list[ReasonCodes],
                        properties: Properties) -> None:
        logging.info(f"Broker responded to unsubscribe "
                     f"mid {mid}, "
                     f"reason_codes {reason_code_list}, "
                     f"properties {properties}")

    def _setup_callbacks(self):
        for key in dir(self._paho_client):
            if not key.startswith("on_"):
                continue
            try:
                setattr(self._paho_client, key, getattr(self, "_" + key))
            except AttributeError as e:
                raise AppError(f"Missing attribute {key} in MQTT App")

    def _set_status(self, *, online: bool) -> MQTTMessageInfo:
        topic = f"{MQTT_APP_MANAGER_STATUS}/{self._mqtt_client_id}"
        if online:
            message = MQTT_APP_MANAGER_STATUS_ONLINE
        else:
            message = MQTT_APP_MANAGER_STATUS_OFFLINE
        return self._paho_client.publish(topic=topic, payload=message, qos=1, retain=True)

    def start(self, host: str, port: int, keep_alive: int) -> None:
        self._setup_callbacks()
        self._paho_client.will_set(topic=MQTT_APP_MANAGER_STATUS, payload=MQTT_APP_MANAGER_STATUS_OFFLINE, qos=1,
                                   retain=True)
        self._paho_client.connect(host, port, keep_alive)
        logging.info("Starting PAHO client loop")
        self._paho_client.loop_start()

    def stop(self):
        logging.debug("Update status on broker before disconnecting.")
        info = self._set_status(online=False)
        info.wait_for_publish()
        logging.debug("Disconnecting cleanly from broker.")
        self._paho_client.disconnect()
        logging.info("Waiting for PAHO client loop to stop...")
        self._paho_client.loop_stop()
        logging.info("PAHO client loop has stopped.")
