import asyncio
import logging
import socket
import ssl
from functools import partial
from typing import Any

import backoff
import paho
from paho.mqtt.client import Client, ConnectFlags, DisconnectFlags, ReasonCodes, Properties, MQTTMessage, \
    MQTTMessageInfo, MQTT_LOG_INFO, MQTT_LOG_NOTICE, MQTT_LOG_WARNING, MQTT_LOG_ERR, MQTT_LOG_DEBUG, MQTT_ERR_NO_CONN, \
    MQTT_ERR_SUCCESS, PayloadType, topic_matches_sub
from paho.mqtt.enums import MQTTErrorCode

from gcn_manager import AppError, get_env, MqttPublisher, MessageProcessor
from gcn_manager.constants import *


class MqttAgent(MqttPublisher):

    def __init__(self, args, client_id: str, *, processor: MessageProcessor, shutdown_requested: asyncio.Event) -> None:
        # message processing
        self._processor = processor
        self._shutdown_requested = shutdown_requested
        self._handling_tasks: set[asyncio.Task] = set()
        self._subscribed_topics: set[str] = set()
        # agent lifecycle
        self._idle_loop_sleep = args.idle_loop_sleep
        self._no_writer_left: asyncio.Event | None = None
        self._misc_loop_task: asyncio.Task | None = None
        self._connect_result: asyncio.Future | None = None
        self._disconnect_result: asyncio.Future | None = None
        # PAHO client configuration
        self._client_id = client_id
        self._mqtt_socket_send_buffer_size = args.mqtt_socket_send_buffer_size
        self._mqtt_host = args.mqtt_host
        self._mqtt_port = args.mqtt_port
        self._mqtt_keep_alive = args.mqtt_keep_alive
        self._paho_mqtt_client = Client(callback_api_version=paho.mqtt.client.CallbackAPIVersion.VERSION2,
                                        clean_session=True, client_id=self._client_id,
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
        self._paho_mqtt_client.will_set(topic=self._get_manager_status_topic(), payload=MQTT_APP_MANAGER_STATUS_OFFLINE,
                                        qos=1, retain=True)

    def _get_manager_status_topic(self) -> str:
        return f"{MQTT_APP_MANAGER_STATUS_TOPIC}/{self._client_id}"


    # FIXME: fail when self._shutdown_requested and still retrying
    @backoff.on_exception(partial(backoff.expo, base=3, max_value=10),
                          (socket.gaierror, TimeoutError, ConnectionRefusedError))
    async def _connect(self) -> asyncio.Future:
        """
        Transient exceptions are retried with back off
        :return: Future representing the result of the connection attempt
                 (either reason_code or non-retryable exception)
        """
        # Paho client is blocking
        #
        # Here we cannot use asyncio.to_thread() as on_connect callback creates at least 1 new task (loop_misc)
        # These tasks would then have to be moved from another thread to the main thread.
        #
        # # FIXME maybe we could submit the newly created tasks from the other thread to the current loop
        # By calling asyncio.run_coroutine_threadsafe() to get a Future which we would track as strong reference
        #
        # Anyway, make connect() async, so at least backoff can process other tasks while retrying on exceptions.
        logging.info(f"Connecting to MQTT broker '{self._mqtt_host}:{self._mqtt_port}'...")
        self._connect_result = asyncio.get_running_loop().create_future()
        self._disconnect_result = asyncio.get_running_loop().create_future()
        try:
            self._paho_mqtt_client.connect(self._mqtt_host, self._mqtt_port, self._mqtt_keep_alive)
        except ssl.SSLError as e:
            # TLS errors are red flag for bad configuration or misbehaving actors : do not retry
            exc = AppError(f"TLS error while communicating with {self._mqtt_port} of '{self._mqtt_host}' : {e}")
            self._connect_result.set_exception(exc)
            self._disconnect_result.set_exception(exc)
        return self._connect_result

    def publish(self, topic: str, payload: bytes | bytearray = None, qos: int = 0, retain: bool = False) -> None:
        # Make it a different signature to decouple from Paho client
        self._publish(topic, payload, qos, retain)

    def _publish(self, topic: str, payload: PayloadType = None, qos: int = 0, retain: bool = False,
                 properties: Properties | None = None) -> MQTTMessageInfo:
        return self._paho_mqtt_client.publish(topic, payload, qos, retain, properties)

    @staticmethod
    def topic_matches_subscription(topic: str, subscription: str) -> bool:
        # TODO: remove so we have no functional adherence to this mqtt client
        # and we do not actually parse topics multiple times
        """Wrapper so that we do not have to change API if we need to reimplement when changing low-level MQTT client"""
        return topic_matches_sub(subscription, topic)

    def subscribe(self, topic: str, qos: int) -> None:
        error, mid = self._paho_mqtt_client.subscribe(topic, qos)
        if error == MQTT_ERR_NO_CONN:
            raise AppError(f"Could not subscribe to topic {topic}: not connected")
        elif error == MQTT_ERR_SUCCESS:
            self._subscribed_topics.add(topic)
            return
        else:
            raise AppError(f"Unknown MQTTErrorCode during subscribe to {topic}: {error}")

    def unsubscribe(self, topic: str) -> None:
        if topic in self._subscribed_topics:
            self._unsubscribe(topic)
        else:
            raise AppError(f"Topic {topic} was not subscribed and cannot be unsubscribed")

    def _unsubscribe(self, topic: str | list[str], properties: Properties | None = None) -> tuple[
        MQTTErrorCode, int | None]:
        # TODO: check errors MQTT_ERR_SUCCESS / MQTT_ERR_NO_CONN
        logging.debug(f"Unsubscribing from topic {topic}")
        return self._paho_mqtt_client.unsubscribe(topic, properties)

    def _disconnect(self) -> None:
        logging.debug("Disconnecting from broker")
        self._paho_mqtt_client.disconnect()

    def _set_status(self, *, online: bool) -> MQTTMessageInfo:
        topic = self._get_manager_status_topic()
        message = MQTT_APP_MANAGER_STATUS_ONLINE if online else MQTT_APP_MANAGER_STATUS_OFFLINE
        logging.debug(f"Setting {self._client_id} status to {message}")
        return self._publish(topic=topic, payload=message, qos=1, retain=True)

    @property
    def connection_result(self) -> asyncio.Future:
        return self._connect_result

    def _on_connect(self, _client: Client, _user_data: Any, connect_flags: ConnectFlags, reason_code: ReasonCodes,
                    properties: Properties) -> None:
        reason_text = reason_code.getName()
        logging.debug(f"Connected, "
                      f"reason code '{reason_code.value}/{reason_text}', "
                      f"session_present {connect_flags.session_present}, "
                      f"properties {properties}")
        # resolve the future early, so that the reason_code is pertinent
        if reason_code.is_failure:
            exc = AppError(f"Failure upon connecting to '{self._mqtt_host}:{self._mqtt_port}': {reason_text}")
            self._connect_result.set_exception(exc)
            return
        self._connect_result.set_result(reason_code)

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
        logging.debug(f"Disconnected, "
                      f"reason code '{reason_code.value}/{reason_code.getName()}', "
                      f"session_present {disconnect_flags.is_disconnect_packet_from_server}, "
                      f"properties {properties}")
        # resolve the Future only if it has not already been resolved, for example, in on_connect on error
        if not self._disconnect_result.done():
            self._disconnect_result.set_result(reason_code)
        # handle spurious disconnections by shutting down and trying again
        if reason_code.is_failure:
            logging.warning(f"Detected error '{reason_code}' on disconnect, triggering shutting down...")
            self._shutdown_requested.set()

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
        future = self._processor.process(msg.topic, msg.payload, publisher=self)
        # create a task per message
        task = asyncio.get_running_loop().create_task(future)
        # store a strong reference to the created task, to prevent garbage collection in asyncio
        self._handling_tasks.add(task)

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
        asyncio.get_running_loop().remove_reader(sock)
        if self._misc_loop_task is not None:
            self._misc_loop_task.cancel()

    def _loop_read(self) -> None:
        logging.debug("Socket is readable, calling loop_read")
        self._paho_mqtt_client.loop_read()

    def _on_socket_open(self, _client: Client, _user_data: Any, sock) -> None:
        logging.debug(f"Socket {sock} opened, add socket to loop readers, set sock opt, create misc loop async task")
        # noinspection PyUnresolvedReferences
        _set_sock_opt = self._paho_mqtt_client.socket().setsockopt
        _set_sock_opt(socket.SOL_SOCKET, socket.SO_SNDBUF, self._mqtt_socket_send_buffer_size)
        # add task for asynchronous reading of packets + add task for sending keepalive as needed
        asyncio.get_running_loop().add_reader(sock, self._loop_read)
        self._misc_loop_task = asyncio.get_running_loop().create_task(self._loop_misc())

    def _loop_write(self) -> None:
        logging.debug("Socket is writable, calling loop_write")
        self._paho_mqtt_client.loop_write()

    def _on_socket_register_write(self, _client: Client, _user_data: Any, sock) -> None:
        logging.debug(f"Client data needs writing to {sock}, add sock to loop writers")
        asyncio.get_running_loop().add_writer(sock, self._loop_write)
        self._no_writer_left = asyncio.Event()

    def _on_socket_unregister_write(self, _client: Client, _user_data: Any, sock) -> None:
        logging.debug(f"No more client data to write into socket {sock}, remove sock from loop writers")
        asyncio.get_running_loop().remove_writer(sock)
        self._no_writer_left.set()

    @staticmethod
    def _on_subscribe(_client: Client, _user_data: Any, mid: int, reason_code_list: list[ReasonCodes],
                      properties: Properties) -> None:
        logging.debug(f"Broker responded to subscribe "
                      f"mid {mid}, "
                      f"reason_codes {reason_code_list}, "
                      f"properties {properties}")

        # TODO: handle subscription errors

    @staticmethod
    def _on_unsubscribe(_client: Client, _user_data: Any, mid: int, reason_code_list: list[ReasonCodes],
                        properties: Properties) -> None:
        logging.debug(f"Broker responded to unsubscribe "
                      f"mid {mid}, "
                      f"reason_codes {reason_code_list}, "
                      f"properties {properties}")

        # TODO: handle unsubscription errors

    async def _loop_misc(self) -> None:
        # mqtt keepalive task
        logging.debug("MQTT misc_loop started")
        while self._paho_mqtt_client.loop_misc() == MQTT_ERR_SUCCESS:
            try:
                await asyncio.sleep(self._idle_loop_sleep)
            except asyncio.CancelledError:
                logging.debug("MQTT misc_loop cancelled")
                break
        logging.debug("MQTT misc_loop finished")

    def _cleanup_finished_handling_tasks(self):
        before_size = len(self._handling_tasks)
        self._handling_tasks = set(task for task in self._handling_tasks if not task.done())
        # TODO: handle failed tasks
        after_size = len(self._handling_tasks)
        if before_size != after_size:
            logging.debug(f"Handling tasks count: {before_size} -> {after_size}")

    async def _loop(self):
        # try to connect
        try:
            connect_result_future = await self._connect()
            reason_code = await connect_result_future
        except asyncio.CancelledError:
            logging.debug("MQTT Agent app task was cancelled while waiting for connection")
            raise
        except AppError as e:
            logging.error(f"Connection failed irrevocably : {e}")
            raise

        # connection established successfully, publish our presence and subscribe to desired topics
        logging.info(f"Connected to MQTT broker : {reason_code.value}/{reason_code}")
        self._set_status(online=True)
        self.subscribe(MQTT_APP_MANAGER_STATUS_SUBSCRIPTION, 1)
        self.subscribe(MQTT_APP_CLIENT_SUBSCRIPTION, 1)

        # periodically cleanup spawned message processing tasks until shutdown, once they are finished
        while not self._shutdown_requested.is_set():
            self._cleanup_finished_handling_tasks()
            try:
                await asyncio.sleep(self._idle_loop_sleep)
            except asyncio.CancelledError:
                logging.error("MQTT Agent app task was cancelled before shutdown was requested")
                return
        logging.info(f"MQTT task detected shutdown request")

        # FIXME: what happens to _result upon unexpected disconnect ?

        # unsubscribing to all tracked topics and updating our online status, to quench the flow
        logging.info(f"Unsubscribing to all subscribed topics")
        for topic in self._subscribed_topics:
            self._unsubscribe(topic)
        self._set_status(online=False)
        # waiting for writer tasks to finish
        await self._no_writer_left.wait()
        logging.info(f"No more writers, disconnecting from server")

        # wait for all tasks to finish
        while self._handling_tasks:
            logging.info(f"Waiting for {len(self._handling_tasks)} handling tasks to finish...")
            try:
                finished, self._handling_tasks = await asyncio.wait(self._handling_tasks,
                                                                    return_when=asyncio.FIRST_COMPLETED,
                                                                    timeout=self._idle_loop_sleep)

                # TODO: handle failed tasks
            except asyncio.CancelledError:
                logging.error("MQTT Agent app task was cancelled before handling tasks could finish")
                return

        # disconnect finally
        self._disconnect()
        try:
            reason_code = await self._disconnect_result
            logging.info(f"Disconnected from MQTT broker {reason_code.value}/{reason_code}")
        except asyncio.CancelledError:
            logging.debug("MQTT Agent app task was cancelled while waiting for disconnection")
            return
        except AppError as e:
            logging.error(f"Disconnection failed irrevocably : {e}")
            return
        logging.info("MQTT app task finished")

    async def run(self) -> None:
        while not self._shutdown_requested.is_set():
            await self._loop()
