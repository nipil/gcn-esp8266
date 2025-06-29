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
    MQTT_ERR_SUCCESS, PayloadType

from gcn_manager import AppError, get_env, MqttPublisher, MessageProcessor, MessageError
from gcn_manager.constants import *


class MqttMessageIdTracker:
    def __init__(self):
        self._topics: dict[int, str] = dict()

    def track(self, mid: int, topic: str) -> None:
        logging.debug(f"Tracking {mid=} {topic=}")
        if mid in self._topics:
            raise AppError(f"Wants to track mid {mid} but already tracked")
        self._topics[mid] = topic

    def untrack(self, mid: int) -> str:
        try:
            topic = self._topics.pop(mid)
        except KeyError:
            raise AppError(f"Wants to untrack mid {mid} but not tracked")
        logging.debug(f"Untracking {mid=} {topic=}")
        return topic


class MqttAgent(MqttPublisher):

    def __init__(self, args, client_id: str, *, processor: MessageProcessor, shutdown_requested: asyncio.Event) -> None:
        super().__init__(client_id)
        # message processing
        self._processor = processor
        self._shutdown_requested = shutdown_requested
        self._remaining_tasks: set[asyncio.Task] = set()
        self._subscribed_topics: set[str] = set()
        # agent lifecycle
        self._mid = MqttMessageIdTracker()
        self._idle_loop_sleep = args.idle_loop_sleep
        self._no_writer_left: asyncio.Event | None = None
        self._misc_loop_task: asyncio.Task | None = None
        self._connect_result: asyncio.Future | None = None
        self._disconnect_result: asyncio.Future | None = None
        # PAHO client configuration
        self._mqtt_socket_send_buffer_size = args.mqtt_socket_send_buffer_size
        self._mqtt_host = args.mqtt_host
        self._mqtt_port = args.mqtt_port
        self._mqtt_keep_alive = args.mqtt_keep_alive
        self._mqtt_retry = args.mqtt_reconnect
        self._paho_mqtt_client = Client(callback_api_version=paho.mqtt.client.CallbackAPIVersion.VERSION2,
                                        clean_session=True, client_id=self.client_id,
                                        protocol=paho.mqtt.client.MQTTv311, transport=args.mqtt_transport,
                                        reconnect_on_failure=self._mqtt_retry)
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
        return f"{MQTT_APP_MANAGER_STATUS_TOPIC}/{self.client_id}"

    @backoff.on_exception(partial(backoff.expo, base=1.5, max_value=3), jitter=backoff.random_jitter,
                          exception=(socket.gaierror, TimeoutError, ConnectionRefusedError))
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
        if self._shutdown_requested.is_set():
            raise AppError("Still trying to connect while shutting down")
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

    def publish(self, topic: str, payload: PayloadType = None, qos: int = 0, retain: bool = False,
                properties: Properties | None = None) -> MQTTMessageInfo:
        info = self._paho_mqtt_client.publish(topic, payload, qos, retain, properties)
        logging.debug(f"Published message {info}")
        self._mid.track(info.mid, topic)  # for _on_publish()
        return info

    def subscribe(self, topic: str, qos: int) -> None:
        error, mid = self._paho_mqtt_client.subscribe(topic, qos)
        if error == MQTT_ERR_NO_CONN:
            raise AppError(f"Could not subscribe to topic {topic}: not connected")
        elif error == MQTT_ERR_SUCCESS:
            logging.debug(f"Subscribing to topic {topic} {qos=}")
            self._mid.track(mid, topic)  # for _on_subscribe()
            self._subscribed_topics.add(topic)
        else:
            raise AppError(f"Unknown MQTTErrorCode during subscribe to {topic}: {error}")

    def unsubscribe(self, topic: str | list[str], properties: Properties | None = None) -> None:
        if topic not in self._subscribed_topics:
            raise AppError(f"Topic {topic} was not subscribed and cannot be unsubscribed")
        error, mid = self._paho_mqtt_client.unsubscribe(topic, properties)
        if error == MQTT_ERR_NO_CONN:
            raise AppError(f"Could not unsubscribe to topic {topic}: not connected")
        elif error == MQTT_ERR_SUCCESS:
            logging.debug(f"Unsubscribing from topic {topic}")
            self._mid.track(mid, topic)  # for _on_unsubscribe()
            self._subscribed_topics.remove(topic)
        else:
            raise AppError(f"Unknown MQTTErrorCode during subscribe to {topic}: {error}")

    def unsubscribe_all(self) -> None:
        topics = self._subscribed_topics.copy()  # build a shallow copy to iterate over
        logging.info(f"Unsubscribing to all ({len(topics)}) subscribed topics")
        for topic in topics:
            self.unsubscribe(topic)  # as this modifies self._subscribed_topics in-place

    def _disconnect(self) -> None:
        logging.debug("Disconnecting from broker")
        self._paho_mqtt_client.disconnect()

    def _set_status(self, *, online: bool) -> MQTTMessageInfo:
        topic = self._get_manager_status_topic()
        message = MQTT_APP_MANAGER_STATUS_ONLINE if online else MQTT_APP_MANAGER_STATUS_OFFLINE
        logging.debug(f"Setting {self.client_id} status to {message}")
        return self.publish(topic=topic, payload=message, qos=1, retain=True)

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

        # TODO: check tracked mid --> WHAT for  ?

        # noinspection PyTypeChecker
        future = self._processor.process(msg.topic, msg.payload, publisher=self)
        # create a task per message
        task = asyncio.get_running_loop().create_task(future)
        # store a strong reference to the created task, to prevent garbage collection in asyncio
        self._remaining_tasks.add(task)

    @staticmethod
    def _on_pre_connect(_client: Client, _user_data: Any) -> None:
        logging.debug(f"Starting connection.")

    def _on_publish(self, _client: Client, _user_data: Any, mid: int, reason_code: ReasonCodes,
                    properties: Properties) -> None:
        logging.debug(f"Broker responded to publish {mid=} {reason_code=} {str(properties)=}")
        topic = self._mid.untrack(mid)  # from publish()
        if reason_code.is_failure:
            raise AppError(f"Encountered {reason_code.value}/{reason_code} while publishing to {topic}")

    def _on_socket_close(self, _client: Client, _user_data: Any, sock) -> None:
        """
        cancel task for asynchronous reading of packets, no more incoming messages can arrive
        cancel task for queuing keepalive packets
        """
        logging.debug(f"Socket {sock} is about to close, remove socket from loop readers")
        asyncio.get_running_loop().remove_reader(sock)
        if self._misc_loop_task is not None:
            self._misc_loop_task.cancel()

    def _loop_read(self) -> None:
        """task for asynchronous reading of incoming socket data, then calling the appropriate callbacks"""
        logging.debug("Socket is readable, calling loop_read")
        self._paho_mqtt_client.loop_read()

    def _on_socket_open(self, _client: Client, _user_data: Any, sock) -> None:
        """
        set socket write buffer size
        add task for asynchronous reading of packets for incoming messages
        add task for queuing keepalive packets as needed
        """
        logging.debug(f"Socket {sock} opened, add socket to loop readers, set sock opt, create misc loop async task")
        # noinspection PyUnresolvedReferences
        _set_sock_opt = self._paho_mqtt_client.socket().setsockopt
        _set_sock_opt(socket.SOL_SOCKET, socket.SO_SNDBUF, self._mqtt_socket_send_buffer_size)
        # noinspection PyTypeChecker
        asyncio.get_running_loop().add_reader(sock, self._loop_read)
        self._misc_loop_task = asyncio.get_running_loop().create_task(self._loop_misc())

    def _loop_write(self) -> None:
        """task for asynchronous writing of buffered packets to socket, as long as there are any to send"""
        logging.debug("Socket is writable, calling loop_write")
        self._paho_mqtt_client.loop_write()

    def _on_socket_register_write(self, _client: Client, _user_data: Any, sock) -> None:
        logging.debug(f"Client data needs writing to {sock}, add sock to loop writers")
        # noinspection PyTypeChecker
        asyncio.get_running_loop().add_writer(sock, self._loop_write)
        self._no_writer_left = asyncio.Event()

    def _on_socket_unregister_write(self, _client: Client, _user_data: Any, sock) -> None:
        logging.debug(f"No more client data to write into socket {sock}, remove sock from loop writers")
        asyncio.get_running_loop().remove_writer(sock)
        self._no_writer_left.set()

    def _on_subscribe(self, _client: Client, _user_data: Any, mid: int, reason_code_list: list[ReasonCodes],
                      properties: Properties) -> None:
        logging.debug(f"Broker responded to subscribe {mid=} {reason_code_list=} {str(properties)=}")
        topic = self._mid.untrack(mid)  # from subscribe()
        for reason_code in reason_code_list:
            if reason_code.is_failure:
                raise AppError(f"Encountered {reason_code.value}/{reason_code} while subscribing to {topic}")

    def _on_unsubscribe(self, _client: Client, _user_data: Any, mid: int, reason_code_list: list[ReasonCodes],
                        properties: Properties) -> None:
        logging.debug(f"Broker responded to unsubscribe {mid=} {reason_code_list=} {str(properties)=}")
        topic = self._mid.untrack(mid)  # from unsubscribe()
        for reason_code in reason_code_list:
            if reason_code.is_failure:
                raise AppError(f"Encountered {reason_code.value}/{reason_code} while unsubscribing to {topic}")

    async def _loop_misc(self) -> None:
        """asynchronous task for queuing keepalive packets as needed regarding message activity"""
        logging.debug("MQTT misc_loop started")
        while self._paho_mqtt_client.loop_misc() == MQTT_ERR_SUCCESS:
            try:
                await asyncio.sleep(self._idle_loop_sleep)
            except asyncio.CancelledError:
                logging.debug("MQTT misc_loop cancelled")
                break
        logging.debug("MQTT misc_loop finished")

    async def _cleanup_finished_handling_tasks(self, *, timeout: float | None):
        if len(self._remaining_tasks) == 0:  # asyncio.wait() raises ValueError on empty set
            return
        finished_tasks, self._remaining_tasks = await asyncio.wait(self._remaining_tasks, timeout=timeout,
                                                                   return_when=asyncio.FIRST_COMPLETED)
        if len(finished_tasks) > 0:
            logging.debug(f"Found {len(self._remaining_tasks)} remaining tasks and {len(finished_tasks)} finished")
        while finished_tasks:
            task = finished_tasks.pop()
            try:
                result = task.result()
                logging.debug(f"Task result : {result}")
            except MessageError as e:
                logging.warning(f"Message processing error, ignoring message : {e}")

                # TODO: find a way to attach message info to task, so we can provide it back

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

        # periodically cleanup finished tasks until shutdown or spurious disconnection
        while not self._shutdown_requested.is_set():
            if not self._paho_mqtt_client.is_connected():
                logging.warning("Detected unexpected disconnection.")
                break
            try:
                await self._cleanup_finished_handling_tasks(timeout=self._idle_loop_sleep)
                await asyncio.sleep(self._idle_loop_sleep)
            except asyncio.CancelledError:
                logging.error("MQTT Agent app task was cancelled before shutdown was requested")
                return

        # cleanup subscription and status if still connected
        if self._paho_mqtt_client.is_connected():
            logging.info(f"MQTT task detected shutdown request")
            # unsubscribing to all tracked topics to quench the flow
            self.unsubscribe_all()
            # update our online status
            self._set_status(online=False)
            # waiting for writer tasks to finish
            await self._no_writer_left.wait()
            logging.info(f"No more writers, disconnecting from server")

        # connected or not, wait for all tasks to finish
        while self._remaining_tasks:
            logging.info(f"Waiting for {len(self._remaining_tasks)} handling tasks to finish...")
            try:
                await self._cleanup_finished_handling_tasks(timeout=None)  # minimum display during cleanup
            except asyncio.CancelledError:
                logging.error("MQTT Agent app task was cancelled before handling tasks could finish")
                return

        if self._paho_mqtt_client.is_connected():
            # disconnect cleanly
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
        await self._loop()
        while self._mqtt_retry and not self._shutdown_requested.is_set():
            await self._loop()
        logging.info("Exiting mqtt lifecycle loop")
