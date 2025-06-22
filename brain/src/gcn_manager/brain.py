import asyncio
import logging
from pathlib import PurePath

from gcn_manager import AppMqttMessage
from gcn_manager.constants import *
from gcn_manager.mqtt import MqttApp


class BrainApp:
    def __init__(self, _args, receive_queue: asyncio.Queue, mqtt_app: MqttApp):
        self._receive_queue = receive_queue
        self._mqtt_app = mqtt_app

    async def handle_manager_status(self, message: AppMqttMessage) -> None:
        manager = PurePath(message.topic).name
        if len(message.payload) > 0 and message.payload != MQTT_APP_MANAGER_STATUS_ONLINE.encode():
            logging.info(f"Clearing status for manager {manager} : not {MQTT_APP_MANAGER_STATUS_ONLINE}.")
            self._mqtt_app.clear_topic(message.topic, qos=1)

    @staticmethod
    async def handle_client_message(message: AppMqttMessage) -> None:
        logging.info(f"Handling client message : {message}")

    async def loop(self) -> None:
        message = await self._receive_queue.get()
        if self._mqtt_app.topic_matches_subscription(message.topic, MQTT_APP_MANAGER_STATUS_SUBSCRIPTION):
            await self.handle_manager_status(message)
        elif self._mqtt_app.topic_matches_subscription(message.topic, MQTT_APP_CLIENT_SUBSCRIPTION):
            await self.handle_client_message(message)


async def run_brain_app(app: BrainApp) -> None:
    while True:
        await app.loop()
