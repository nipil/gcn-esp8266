import logging

from gcn_manager import ClientStatus, ClientInfo, MessageProcessor, MqttPublisher, MessageError
from gcn_manager.constants import *


class MessageHandler:

    def __init__(self, topic: str, payload: bytes | bytearray, *, publisher: MqttPublisher) -> None:
        self._topic = topic
        self._payload = payload
        self._publisher = publisher

    @property
    def topic(self):
        return self._topic

    async def handle(self, topics: list[str]) -> None:
        raise NotImplementedError()

    def decoded_payload(self):
        try:
            return self._payload.decode("utf-8")
        except UnicodeDecodeError:
            raise MessageError(f"Could not decode payload {self._payload} for topic {self._topic}")

    def decoded_payload_as(self, target_type: type):
        decode = self.decoded_payload()
        try:
            return target_type(decode)
        except ValueError as e:
            raise MessageError(f"Invalid value for {self.topic} : {e}")


class ClientMessageHandler(MessageHandler):

    def __init__(self, topic: str, client: ClientInfo, payload: bytes | bytearray, *, publisher: MqttPublisher) -> None:
        super().__init__(topic, payload, publisher=publisher)
        self._client = client

    async def handle_status(self) -> None:
        status = self.decoded_payload_as(ClientStatus)
        if self._client.status is not None and self._client.status == status:
            return
        logging.info(f"Client {self._client.id} {MQTT_APP_CLIENT_STATUS} change : {self._client.status} -> {status}")
        self._client.status = status

    async def handle_heartbeat(self) -> None:
        heartbeat = self.decoded_payload_as(int)
        logging.debug(f"Got heartbeat {heartbeat} for {self._client.id}")
        self._client.heartbeat = heartbeat

        # TODO: compare to host, and warn if more than XXXX skew

        # TODO: handle offline for a long time, but needs status

    async def handle_buffer_dropped(self) -> None:
        dropped = self.decoded_payload_as(int)
        dropped += 1  # fixme: debug
        if self._client.buffer_total_dropped_item is not None and self._client.buffer_total_dropped_item == dropped:
            return
        logging.info(f"Client {self._client.id} {MQTT_APP_CLIENT_BUFFER_TOTAL_DROPPED_ITEM} change : "
                     f"{self._client.buffer_total_dropped_item} -> {dropped}")
        self._client.buffer_total_dropped_item = dropped  # TODO: notify

    async def handle_monitored_gpio(self) -> None:
        raise MessageError(f"NOT IMPLEMENTED {self.topic}")  # TODO: implement

    async def handle_in(self, topics: list[str]):
        if len(topics) == 0:
            raise MessageError(f"Client topic {self.topic} has no category")
        raise MessageError(f"NOT IMPLEMENTED {self.topic}")  # TODO: implement

    async def handle_out(self, topics: list[str]):
        if len(topics) == 0:
            raise MessageError(f"Client topic {self.topic} has no category")
        if topics[0] == MQTT_APP_CLIENT_STATUS:
            await self.handle_status()
        elif topics[0] == MQTT_APP_CLIENT_HEARTBEAT:
            await self.handle_heartbeat()
        elif topics[0] == MQTT_APP_CLIENT_BUFFER_TOTAL_DROPPED_ITEM:
            await self.handle_buffer_dropped()
        elif topics[0] == MQTT_APP_CLIENT_MONITORED_GPIO:
            await self.handle_monitored_gpio()
        else:
            raise MessageError(f"Unknown client category in {self.topic}")

    async def handle(self, topics: list[str]) -> None:
        if len(topics) == 0:
            raise MessageError(f"Client topic {self.topic} has no direction")
        if topics[0] == MQTT_APP_CLIENT_OUT:
            await self.handle_out(topics[1:])
        elif topics[0] == MQTT_APP_CLIENT_IN:
            await self.handle_in(topics[1:])
        else:
            raise MessageError(f"Unknown client direction in {self.topic}")


class ManagerMessageHandler(MessageHandler):

    def __init__(self, topic: str, payload: bytes | bytearray, publisher: MqttPublisher) -> None:
        super().__init__(topic, payload, publisher=publisher)

    async def handle_status(self, topics: list[str]) -> None:
        if len(topics) == 0:
            raise MessageError(f"Manager status topic {self.topic} has no id")
        manager_id = topics[0]
        if len(self._payload) == 0:
            logging.debug(f"Empty manager status message for {manager_id}, ignoring cleanup")
            return
        try:
            status = self._payload.decode("utf-8")
        except UnicodeDecodeError:
            raise MessageError(f"Could not decode manager status payload {self._payload} for topic {self.topic}")
        if status == MQTT_APP_MANAGER_STATUS_ONLINE:
            logging.info(f"Manager {manager_id} detected online")
            return
        elif status == MQTT_APP_MANAGER_STATUS_OFFLINE:
            logging.info(f"Manager {manager_id} detected offline, clearing its status")
            self._publisher.clear_topic(self.topic, qos=1)
        else:
            raise MessageError(f"Unknown manager status {status} for {self.topic}")

    async def handle(self, topics: list[str]) -> None:
        if len(topics) == 0:
            raise MessageError(f"Manager topic {self.topic} has no category")
        if topics[0] == MQTT_APP_MANAGER_STATUS:
            await self.handle_status(topics[1:])
        else:
            raise MessageError(f"Unknown manager category in {self.topic}")


class Brain(MessageProcessor):
    def __init__(self, _args, ):
        self._clients = dict()

    def _ensure_client(self, client_id: str) -> ClientInfo:
        try:
            return self._clients[client_id]
        except KeyError:
            logging.info(f"First time seeing client {client_id}")
            client = ClientInfo(id=client_id)
            self._clients[client_id] = client
            return client

    async def process(self, topic: str, payload: bytes | bytearray, *, publisher: MqttPublisher) -> None:
        topics = topic.split("/")
        if len(topics) == 0:
            raise MessageError("Received empty topic")
        elif topics[0] == MQTT_APP_CLIENT:
            if len(topics) < 2:
                raise MessageError(f"Client topic {topic} has no client id")
            client = self._ensure_client(topics[1])
            logging.debug(f"Handling client topic {topic} with payload {payload}")
            await ClientMessageHandler(topic, client, payload, publisher=publisher).handle(topics[2:])
        elif topics[0] == MQTT_APP_MANAGER:
            logging.debug(f"Handling manager topic {topic} with payload {payload}")
            await ManagerMessageHandler(topic, payload, publisher=publisher).handle(topics[1:])
        else:
            raise MessageError(f"Unknown app in {topic}")

        # TODO: return result ? what kind ?
