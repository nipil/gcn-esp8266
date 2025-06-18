import logging
import queue
import random
import ssl
import threading
from typing import Any

import paho
from paho.mqtt.client import Client, ConnectFlags, DisconnectFlags, ReasonCodes, Properties, MQTTMessage, \
    MQTTMessageInfo, MQTT_LOG_INFO, MQTT_LOG_NOTICE, MQTT_LOG_WARNING, MQTT_LOG_ERR, MQTT_LOG_DEBUG, MQTT_ERR_NO_CONN, \
    MQTT_ERR_SUCCESS, PayloadType, topic_matches_sub

from gcn_manager import AppError, AppMqttMessage
from gcn_manager.constants import *


def setup_threading_exception_queue(exceptions: queue.Queue):
    def queue_thread_exceptions(exc_args):
        thread_name = threading.current_thread().name
        logging.warning(f"Exception in thread {thread_name}: {repr(exc_args.exc_value)}")
        item = (exc_args.exc_value, thread_name)
        exceptions.put(item)

    threading.excepthook = queue_thread_exceptions


# https://eclipse.dev/paho/files/paho.mqtt.python/html/client.html
# https://eclipse.dev/paho/files/paho.mqtt.python/html/types.html

def get_client_id(random_bytes_count: int) -> str:
    return f"{MQTT_APP}_manager_{random.randbytes(random_bytes_count).hex().lower()}"


def create_paho_client(client_id: str, *, user_name: str, user_password: str, transport: str,
                       reconnect_on_failure: bool, connect_timeout: int, tls_ciphers: str) -> paho.mqtt.client.Client:
    paho_mqtt_client = paho.mqtt.client.Client(callback_api_version=paho.mqtt.client.CallbackAPIVersion.VERSION2,
                                               clean_session=True, client_id=client_id,
                                               protocol=paho.mqtt.client.MQTTv311, transport=transport,
                                               reconnect_on_failure=reconnect_on_failure)
    paho_mqtt_client.connect_timeout = connect_timeout
    paho_mqtt_client.tls_set(tls_version=ssl.PROTOCOL_TLSv1_2, ciphers=tls_ciphers)
    paho_mqtt_client.username_pw_set(username=user_name, password=user_password)
    return paho_mqtt_client


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
        self._subscribe(MQTT_APP_MANAGER_STATUS_SUBSCRIPTION, 1)
        self._set_status(online=True)
        self._subscribe(MQTT_APP_CLIENT_SUBSCRIPTION, 1)

    def _on_connect_fail(self, client: Client, user_data: Any) -> None:
        logging.debug(f"Failed to connect")

    def _on_disconnect(self, client: Client, user_data: Any, disconnect_flags: DisconnectFlags,
                       reason_code: ReasonCodes, properties: Properties) -> None:
        logging.info(f"Disconnected, "
                     f"reason code {reason_code.getName()}, "
                     f"session_present {disconnect_flags.is_disconnect_packet_from_server}, "
                     f"properties {properties}")

        # TODO: raise exception so as to terminate/restart the MqttApp

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
                      f"payload {msg.payload}")

        # put message in queue, blocking indefinitely if full
        self._in_queue.put(AppMqttMessage(msg.topic, msg.payload))

    def _on_pre_connect(self, client: Client, user_data: Any) -> None:
        logging.debug(f"Starting connection.")

    def _on_publish(self, client: Client, user_data: Any, mid: int, reason_code: ReasonCodes,
                    properties: Properties) -> None:
        logging.debug(f"Sent message: "
                      f"mid {mid}, "
                      f"reason code {reason_code.getName()}, "
                      f"properties {properties}")

    def _on_socket_close(self, client: Client, user_data: Any, socket) -> None:
        logging.debug(f"Socket {socket} is about to close")

    def _on_socket_open(self, client: Client, user_data: Any, socket) -> None:
        logging.debug(f"Socket {socket} has just been open")

    def _on_socket_register_write(self, client: Client, user_data: Any, socket) -> None:
        logging.debug(f"Client data needs writing into socket {socket}")

    def _on_socket_unregister_write(self, client: Client, user_data: Any, socket) -> None:
        logging.debug(f"No more client data to write into socket {socket}")

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
        message = MQTT_APP_MANAGER_STATUS_ONLINE if online else MQTT_APP_MANAGER_STATUS_OFFLINE
        return self.publish(topic=topic, payload=message, qos=1, retain=True)

    def _subscribe(self, topic: str, qos: int) -> None:
        error, mid = self._paho_client.subscribe(topic, qos)
        if error == MQTT_ERR_NO_CONN:
            raise AppError(f"Could not subscribe to topic {topic}: not connected")
        elif error == MQTT_ERR_SUCCESS:
            return
        else:
            raise AppError(f"Unknown MQTTErrorCode during subscribe to {topic}: {error}")

    def publish(self, topic: str, payload: PayloadType = None, qos: int = 0, retain: bool = False,
                properties: Properties | None = None) -> MQTTMessageInfo:
        return self._paho_client.publish(topic, payload, qos, retain, properties)

    def clear_topic(self, topic: str, qos: int = 0) -> None:
        # to remove a retained message, the retained flag must be present
        self.publish(topic, b'', qos=qos, retain=True)

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

    def topic_matches_subscription(self, topic: str, subscription: str) -> bool:
        """Wrapper so that we do not have to change API if we need to reimplement when changing low-level MQTT client"""
        return topic_matches_sub(subscription, topic)
