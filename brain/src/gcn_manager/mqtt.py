import asyncio
import logging
import random
import socket
import ssl
from typing import Any

import paho
from paho.mqtt.client import Client, ConnectFlags, DisconnectFlags, ReasonCodes, Properties, MQTTMessage, \
    MQTTMessageInfo, MQTT_LOG_INFO, MQTT_LOG_NOTICE, MQTT_LOG_WARNING, MQTT_LOG_ERR, MQTT_LOG_DEBUG, MQTT_ERR_NO_CONN, \
    MQTT_ERR_SUCCESS, PayloadType, topic_matches_sub

from gcn_manager import AppError, AppErrorCanRetry, AppMqttMessage, get_env
from gcn_manager.backoff import BackOff
from gcn_manager.constants import *


# paho mqtt client library
#   https://eclipse.dev/paho/files/paho.mqtt.python/html/client.html
#   https://eclipse.dev/paho/files/paho.mqtt.python/html/types.

# async code from example
#   https://github.com/eclipse-paho/paho.mqtt.python/blob/master/examples/loop_asyncio.py


class MqttApp:

    def __init__(self, args, loop: asyncio.AbstractEventLoop, receive_queue: asyncio.Queue) -> None:
        self._receive_queue = receive_queue
        self._dropped_messages_because_receive_queue_was_full = 0
        self._loop = loop
        self._idle_loop_sleep = args.idle_loop_sleep
        self._misc_loop_task: asyncio.Task | None = None
        self._finished: asyncio.Future | None = None  # to provide wait and status at the end
        self._mqtt_socket_send_buffer_size = args.mqtt_socket_send_buffer_size
        self._mqtt_client_id = f"{MQTT_APP}_manager_{random.randbytes(args.mqtt_client_id_random_bytes).hex().lower()}"
        self._mqtt_host = args.mqtt_host
        self._mqtt_port = args.mqtt_port
        self._mqtt_keep_alive = args.mqtt_keep_alive
        self._paho_mqtt_client = Client(callback_api_version=paho.mqtt.client.CallbackAPIVersion.VERSION2,
                                        clean_session=True, client_id=self._mqtt_client_id,
                                        protocol=paho.mqtt.client.MQTTv311, transport=args.mqtt_transport,
                                        reconnect_on_failure=args.mqtt_reconnect)
        context = ssl.create_default_context(purpose=ssl.Purpose.SERVER_AUTH)
        if args.mqtt_tls_min_version is not None:
            context.minimum_version = args.mqtt_tls_min_version
        if args.mqtt_tls_max_version is not None:
            context.maximum_version = args.mqtt_tls_max_version
        if args.mqtt_tls_ciphers is not None:
            try:
                context.set_ciphers(args.mqtt_tls_ciphers)
            except ssl.SSLError as e:
                raise AppError(f"Could not set MQTT TLS ciphers string {args.mqtt_tls_ciphers} : {e}")
        self._paho_mqtt_client.tls_set_context(context)
        self._paho_mqtt_client.connect_timeout = args.mqtt_connect_timeout
        self._paho_mqtt_client.username_pw_set(username=get_env(ENV_MQTT_USER_NAME),
                                               password=get_env(ENV_MQTT_USER_PASSWORD))
        self._paho_mqtt_client.on_connect = self._on_connect
        self._paho_mqtt_client.on_connect_fail = self._on_connect_fail
        self._paho_mqtt_client.on_disconnect = self._on_disconnect
        self._paho_mqtt_client.on_log = self._on_log
        self._paho_mqtt_client.on_message = self._on_message
        self._paho_mqtt_client.on_pre_connect = self._on_pre_connect
        self._paho_mqtt_client.on_publish = self._on_publish
        self._paho_mqtt_client.on_socket_close = self._on_socket_close
        self._paho_mqtt_client.on_socket_open = self._on_socket_open
        self._paho_mqtt_client.on_socket_register_write = self._on_socket_register_write
        self._paho_mqtt_client.on_socket_unregister_write = self._on_socket_unregister_write
        self._paho_mqtt_client.on_subscribe = self._on_subscribe
        self._paho_mqtt_client.on_unsubscribe = self._on_unsubscribe
        self._paho_mqtt_client.will_set(topic=MQTT_APP_MANAGER_STATUS, payload=MQTT_APP_MANAGER_STATUS_OFFLINE, qos=1,
                                        retain=True)

    def connect(self) -> None:
        logging.info(f"Connecting to MQTT broker '{self._mqtt_host}:{self._mqtt_port}'...")
        try:
            self._paho_mqtt_client.connect(self._mqtt_host, self._mqtt_port, self._mqtt_keep_alive)
        except socket.gaierror as e:
            raise AppErrorCanRetry(f"Could not resolve MQTT broker address '{self._mqtt_host}' : {e}")
        except TimeoutError as e:
            raise AppErrorCanRetry(f"Timed out while connecting to '{self._mqtt_host}' : {e}")
        except ConnectionRefusedError as e:
            raise AppErrorCanRetry(f"Impossible to connect to port {self._mqtt_port} on '{self._mqtt_host}' : {e}")
        except ssl.SSLError as e:
            # TLS errors are red flag for bad configuration or misbehaving actors : do not retry
            raise AppError(f"TLS error while communicating with {self._mqtt_port} of '{self._mqtt_host}' : {e}")
        self._finished = self._loop.create_future()

    @property
    def finished(self):
        return self._finished

    def publish(self, topic: str, payload: PayloadType = None, qos: int = 0, retain: bool = False,
                properties: Properties | None = None) -> MQTTMessageInfo:
        return self._paho_mqtt_client.publish(topic, payload, qos, retain, properties)

    def clear_topic(self, topic: str, qos: int = 0) -> None:
        # to remove a retained message, the retained flag must be present
        self.publish(topic, b'', qos=qos, retain=True)

    @staticmethod
    def topic_matches_subscription(topic: str, subscription: str) -> bool:
        """Wrapper so that we do not have to change API if we need to reimplement when changing low-level MQTT client"""
        return topic_matches_sub(subscription, topic)

    def subscribe(self, topic: str, qos: int) -> None:
        error, mid = self._paho_mqtt_client.subscribe(topic, qos)
        if error == MQTT_ERR_NO_CONN:
            raise AppError(f"Could not subscribe to topic {topic}: not connected")
        elif error == MQTT_ERR_SUCCESS:
            return
        else:
            raise AppError(f"Unknown MQTTErrorCode during subscribe to {topic}: {error}")

    def disconnect(self):
        logging.debug("Update status on broker before disconnecting.")
        info = self._set_status(online=False)
        info.wait_for_publish()
        logging.debug("Disconnecting cleanly from broker.")
        self._paho_mqtt_client.disconnect()

    @property
    def dropped_messages_because_receive_queue_was_full(self):
        return self._dropped_messages_because_receive_queue_was_full

    def _set_status(self, *, online: bool) -> MQTTMessageInfo:
        topic = f"{MQTT_APP_MANAGER_STATUS_TOPIC}/{self._mqtt_client_id}"
        message = MQTT_APP_MANAGER_STATUS_ONLINE if online else MQTT_APP_MANAGER_STATUS_OFFLINE
        return self.publish(topic=topic, payload=message, qos=1, retain=True)

    def _on_connect(self, _client: Client, _user_data: Any, connect_flags: ConnectFlags, reason_code: ReasonCodes,
                    properties: Properties) -> None:
        reason_text = reason_code.getName()
        logging.info(f"Connected, "
                     f"reason code '{reason_code.value}/{reason_text}', "
                     f"session_present {connect_flags.session_present}, "
                     f"properties {properties}")

        if reason_code.is_failure:
            logging.error(f"Failure upon connecting to '{self._mqtt_host}:{self._mqtt_port}': {reason_text}")
            self._finished.set_result(reason_code)  # resolve the future early, so that the reason_code is pertinent
            self._paho_mqtt_client.disconnect()

        self.subscribe(MQTT_APP_MANAGER_STATUS_SUBSCRIPTION, 1)
        self._set_status(online=True)
        self.subscribe(MQTT_APP_CLIENT_SUBSCRIPTION, 1)

    def _on_connect_fail(self, _client: Client, _user_data: Any) -> None:
        """
        As per client library source code
        - this function is only called from client._handle_on_connect_fail()
        - _handle_on_connect_fail is only called from client.loop_forever()
        So when running in an async loop, this function will never not be called
        """
        raise AppError("MqttClient._on_connect_fail has ben called : "
                       "this should never happen if NOT using loop_forever() !")

    def _on_disconnect(self, _client: Client, _user_data: Any, disconnect_flags: DisconnectFlags,
                       reason_code: ReasonCodes, properties: Properties) -> None:
        logging.info(f"Disconnected, "
                     f"reason code '{reason_code.value}/{reason_code.getName()}', "
                     f"session_present {disconnect_flags.is_disconnect_packet_from_server}, "
                     f"properties {properties}")

        # resolve the Future only if it has not already been resolved
        # for example, in on_connect on reason_code=135/Not authorized
        if not self._finished.done():
            self._finished.set_result(reason_code)

    @staticmethod
    def _on_log(_client: Client, _user_data: Any, level: int, buf: str) -> None:
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

    def _on_message(self, _client: Client, _user_data: Any, msg: MQTTMessage) -> None:
        logging.debug(f"Received message: "
                      f"timestamp {msg.timestamp}, "
                      f"state {msg.state}, "
                      f"dup {msg.dup}, "
                      f"mid {msg.mid}, "
                      f"qos {msg.qos}, "
                      f"retain {msg.retain}, "
                      f"info {msg.info}, "  # used with MQTTMessageInfo.wait_for_publish(timeout_sec)
                      f"properties {msg.properties}, "
                      f"topic {msg.topic}, "
                      f"payload {msg.payload}")

        # noinspection PyTypeChecker
        message = AppMqttMessage(msg.topic, msg.payload)
        try:
            self._receive_queue.put_nowait(message)
        except asyncio.QueueFull:
            logging.warning(f"Receive message queue is full, dropping message {message}")
            self._dropped_messages_because_receive_queue_was_full += 1

    @staticmethod
    def _on_pre_connect(_client: Client, _user_data: Any) -> None:
        logging.debug(f"Starting connection.")

    @staticmethod
    def _on_publish(_client: Client, _user_data: Any, mid: int, reason_code: ReasonCodes,
                    properties: Properties) -> None:
        logging.debug(f"Sent message: "
                      f"mid {mid}, "
                      f"reason code {reason_code.getName()}, "
                      f"properties {properties}")

    def _on_socket_close(self, _client: Client, _user_data: Any, sock) -> None:
        logging.debug(f"Socket {sock} is about to close, remove socket from loop readers")
        self._loop.remove_reader(sock)
        self._misc_loop_task.cancel()  # never none, as socket close cannot happen before open succeeded

    def _loop_read(self):
        logging.debug("Socket is readable, calling loop_read")
        self._paho_mqtt_client.loop_read()

    def _on_socket_open(self, _client: Client, _user_data: Any, sock) -> None:
        logging.debug(f"Socket {sock} opened, add socket to loop readers, set sock opt, create misc loop async task")
        # noinspection PyUnresolvedReferences
        self._paho_mqtt_client.socket().setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF,
                                                   self._mqtt_socket_send_buffer_size)
        self._loop.add_reader(sock, self._loop_read)
        self._misc_loop_task = self._loop.create_task(self._loop_misc())

    def _loop_write(self):
        logging.debug("Socket is writable, calling loop_write")
        self._paho_mqtt_client.loop_write()

    def _on_socket_register_write(self, _client: Client, _user_data: Any, sock) -> None:
        logging.debug(f"Client data needs writing to {sock}, add sock to loop writers")
        self._loop.add_writer(sock, self._loop_write)

    def _on_socket_unregister_write(self, _client: Client, _user_data: Any, sock) -> None:
        logging.debug(f"No more client data to write into socket {sock}, remove sock from loop writers")
        self._loop.remove_writer(sock)

    @staticmethod
    def _on_subscribe(_client: Client, _user_data: Any, mid: int, reason_code_list: list[ReasonCodes],
                      properties: Properties) -> None:
        logging.info(f"Broker responded to subscribe "
                     f"mid {mid}, "
                     f"reason_codes {reason_code_list}, "
                     f"properties {properties}")

    @staticmethod
    def _on_unsubscribe(_client: Client, _user_data: Any, mid: int, reason_code_list: list[ReasonCodes],
                        properties: Properties) -> None:
        logging.info(f"Broker responded to unsubscribe "
                     f"mid {mid}, "
                     f"reason_codes {reason_code_list}, "
                     f"properties {properties}")

    async def _loop_misc(self):
        logging.debug("misc_loop started")
        while self._paho_mqtt_client.loop_misc() == MQTT_ERR_SUCCESS:
            try:
                await asyncio.sleep(self._idle_loop_sleep)
            except asyncio.CancelledError:
                logging.debug("misc_loop cancelled")
                break
        logging.debug("misc_loop finished")


async def run_mqtt_app(mqtt: MqttApp, back_off: BackOff) -> None:
    while True:
        try:
            # FIXME: blocking operation (see CLI timeout option)
            # info : running connect in a ThreadPoolExecutor triggers a RuntimeError:
            #        Non-thread-safe operation invoked on an event loop other than the current one
            mqtt.connect()
            # waits for disconnection or error
            reason_code = await mqtt.finished
            if reason_code.is_failure:
                raise AppError(f"Error while communicating with MQTT broker : {reason_code}")
            # reinitialize back off on successful connection
            back_off.reset()
        except AppErrorCanRetry as e:
            duration = back_off.next()
            logging.warning(f"Possible transient error encountered, will retry in {duration} seconds : {e}")
            await asyncio.sleep(duration)
