import argparse
import logging
import pathlib
import queue
import random

from gcn_manager import get_env, AppMqttMessage
from gcn_manager.constants import *
from gcn_manager.mqtt import MqttApp, setup_threading_exception_queue, get_client_id, create_paho_client


class App:
    def __init__(self, args: argparse.Namespace):
        # initialize random number generator (used only to randomize MQTT client ids)
        random.seed()
        # install handler for unhandled exceptions in threads
        self.thread_exceptions = queue.Queue(maxsize=1)
        setup_threading_exception_queue(self.thread_exceptions)
        # message queue for subscribed messages reception
        self.inbox_queue = queue.Queue(maxsize=args.mqtt_in_queue_max_size)
        # PAHO mqtt synchronous client
        mqtt_client_id = get_client_id(args.mqtt_client_id_random_bytes)
        paho_mqtt_client = create_paho_client(mqtt_client_id, user_name=get_env(ENV_MQTT_USER_NAME),
                                              user_password=get_env(ENV_MQTT_USER_PASSWORD),
                                              transport=args.mqtt_transport, reconnect_on_failure=args.mqtt_reconnect,
                                              connect_timeout=args.mqtt_connect_timeout,
                                              tls_ciphers=args.mqtt_tls_ciphers)
        # thin wrapper around paho client (for logging, exceptions and life-cycle)
        self.mqtt_app = MqttApp(paho_mqtt_client, self.inbox_queue, mqtt_client_id=mqtt_client_id)
        self.mqtt_app.start(args.mqtt_host, args.mqtt_port, args.mqtt_keep_alive)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, exc_traceback):
        # if exiting cleanly, shutting down paho mqtt client
        if exc_type is None:
            self.shutdown()
        # do not swallow the exception, if any had happened
        return False

    def shutdown(self):
        self.mqtt_app.stop()

    async def raise_thread_exception_if_any(self) -> None:
        try:
            exc, thread_name = self.thread_exceptions.get_nowait()
        except queue.Empty:
            return
        self.thread_exceptions.task_done()  # decrease queue task counter, in case anyone calls join on the queue
        logging.error(f"Thread {thread_name} threw exception, re-raising from main thread")
        raise exc  # in main thread this time

    async def handle_manager_status(self, msg: AppMqttMessage) -> None:
        manager = pathlib.PurePath(msg.topic).name
        if len(msg.payload) > 0 and msg.payload != MQTT_APP_MANAGER_STATUS_ONLINE.encode():
            logging.info(f"Clearing status for manager {manager} : not {MQTT_APP_MANAGER_STATUS_ONLINE}.")
            self.mqtt_app.clear_topic(msg.topic, qos=1)

    async def handle_message(self, msg: AppMqttMessage) -> None:
        if self.mqtt_app.topic_matches_subscription(msg.topic, MQTT_APP_MANAGER_STATUS_SUBSCRIPTION):
            await self.handle_manager_status(msg)

    async def handle_message_if_any(self) -> None:
        try:
            msg: AppMqttMessage = self.inbox_queue.get_nowait()
        except queue.Empty:
            return
        await self.handle_message(msg)

    async def loop(self) -> None:
        await self.raise_thread_exception_if_any()
        await self.handle_message_if_any()
