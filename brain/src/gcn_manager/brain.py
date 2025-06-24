import asyncio
import logging
from pathlib import PurePath

from gcn_manager import AppMqttMessage, ClientInfo, ClientStatus, MessageProcessor, MqttPublisher
from gcn_manager.constants import *
from gcn_manager.notifier import BufferTotalDroppedItem


class Brain(MessageProcessor):
    def __init__(self, _args, ):
        self._clients = dict()

    async def handle_manager_status(self, message: AppMqttMessage) -> None:
        manager = PurePath(message.topic).name
        if len(message.payload) == 0:
            logging.debug(f"Empty status message for topic {message.topic}")
            return
        status = message.payload.decode("utf-8")
        if status == MQTT_APP_MANAGER_STATUS_ONLINE:
            return
        logging.info(f"Clearing status for manager {manager} : not {MQTT_APP_MANAGER_STATUS_ONLINE}.")
        self._mqtt_app.clear_topic(message.topic, qos=1)

    def ensure_client(self, client_id: str) -> ClientInfo:
        try:
            return self._clients[client_id]
        except KeyError:
            logging.info(f"First time seeing client {client_id}")
            client = ClientInfo(id=client_id)
            self._clients[client_id] = client
            return client

    @staticmethod
    async def handle_client_status(client: ClientInfo, payload: bytes | bytearray):
        try:
            status = ClientStatus(payload.decode())
        except ValueError as e:
            logging.warning(f"Invalid {MQTT_APP_CLIENT_STATUS} {payload} for {client.id} : {e}")
            return
        if client.status is not None and client.status == status:
            return
        logging.info(f"Client {client.id} {MQTT_APP_CLIENT_STATUS} change : {client.status} -> {status}")
        client.status = status

        # TODO: handle offline for a long time, needs heartbeat

    @staticmethod
    async def handle_client_heartbeat(client: ClientInfo, payload: bytes | bytearray):
        try:
            value = int(payload.decode())
        except ValueError as e:
            logging.warning(f"Invalid {MQTT_APP_CLIENT_HEARTBEAT} {payload} for {client.id} : {e}")
            return
        client.heartbeat = value

        # TODO: handle offline for a long time, needs status

    async def handle_client_buffer_dropped(self, client: ClientInfo, payload: bytes | bytearray):
        try:
            value = int(payload.decode())
        except ValueError as e:
            logging.warning(f"Invalid {MQTT_APP_CLIENT_BUFFER_TOTAL_DROPPED_ITEM} {payload} for {client.id} : {e}")
            return
        if client.buffer_total_dropped_item is not None and client.buffer_total_dropped_item == value:
            return
        logging.info(f"Client {client.id} {MQTT_APP_CLIENT_BUFFER_TOTAL_DROPPED_ITEM} change : "
                     f"{client.buffer_total_dropped_item} -> {value}")
        client.buffer_total_dropped_item = value
        try:
            self._notify_queue.put_nowait(BufferTotalDroppedItem(value))
        except asyncio.QueueFull:
            logging.warning(f"Dropping notification {MQTT_APP_CLIENT_BUFFER_TOTAL_DROPPED_ITEM} {value} : queue full")
            return

    async def handle_client_message(self, message: AppMqttMessage) -> None:
        logging.info(f"Handling client message : {message}")
        try:
            _, client_id, _, category = message.topic.split("/")
        except ValueError as e:
            logging.warning(f"Invalid topic {message.topic}: {e}")
            return
        logging.debug(f"Split topic into : {client_id=} {category=}")
        client = self.ensure_client(client_id)
        if category == MQTT_APP_CLIENT_STATUS:
            await self.handle_client_status(client, message.payload)
        elif category == MQTT_APP_CLIENT_HEARTBEAT:
            await self.handle_client_heartbeat(client, message.payload)
        elif category == MQTT_APP_CLIENT_BUFFER_TOTAL_DROPPED_ITEM:
            await self.handle_client_buffer_dropped(client, message.payload)
        elif category == MQTT_APP_CLIENT_MONITORED_GPIO:
            pass

    async def _loop(self) -> bool:
        try:
            message = await self._receive_queue.get()
        except asyncio.CancelledError as e:
            logging.debug("Brain app task is cancelled")
            return False
        # TODO: do not parse topics multiple times
        if self._mqtt_app.topic_matches_subscription(message.topic, MQTT_APP_MANAGER_STATUS_SUBSCRIPTION):
            await self.handle_manager_status(message)
        elif self._mqtt_app.topic_matches_subscription(message.topic, MQTT_APP_CLIENT_SUBSCRIPTION):
            await self.handle_client_message(message)
        else:
            logging.warning(f"Received message topic is not MQTT app: {message.topic}")
        return True

    async def run(self) -> None:
        while True:
            if not await self._loop():
                break
        logging.debug("Brain app task finished")

    async def process(self, topic: str, payload: bytes | bytearray, *, publisher: MqttPublisher) -> None:
        logging.critical(f"Processing {topic} with {payload}")
        await asyncio.sleep(1)
