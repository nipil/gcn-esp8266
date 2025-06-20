import asyncio
import logging
import time
from argparse import Namespace
from collections import deque

from pydantic import BaseModel, ValidationError

from gcn_manager import ClientInfo, ClientStatus, DelayedAction, MessageProcessor, MqttPublisher, MessageError, \
    MqttMessage
from gcn_manager.constants import *
from gcn_manager.notifiers import GcnHeartbeatSkewed
from notifiers import GcnStatusChange, GcnDroppedItems, GcnGpioChanged, GcnHeartbeatMissed


class GpioSample(BaseModel, extra="forbid"):
    state: int
    timestamp: int


class Brain(MessageProcessor):
    def __init__(self, args: Namespace):
        self._args = args
        self._client_infos: dict[str, ClientInfo] = dict()

    def _ensure_client(self, client_id: str) -> ClientInfo:
        try:
            return self._client_infos[client_id]
        except KeyError:
            pass
        logging.info(f"First time seeing client {client_id}")
        client = ClientInfo(id=client_id)
        notification = GcnHeartbeatMissed(client=client, elapsed_seconds=self._args.heartbeat_watchdog)
        client.heartbeat_watchdog = DelayedAction(self._args.heartbeat_watchdog, notification.send,
                                                  loop=asyncio.get_running_loop(),
                                                  name=f"heartbeat_watchdog_{client_id}")
        self._client_infos[client_id] = client
        return client

    async def process(self, message: MqttMessage, *, publisher: MqttPublisher) -> None:
        logging.debug(f"Processing {message.topic} with payload {message.payload}")
        parts = deque(message.topic.split("/"))
        try:
            app = parts.popleft()

            if app == MQTT_APP_CLIENT:
                id_ = parts.popleft()
                if MQTT_APP_CLIENT_ID.fullmatch(id_) is None:
                    raise MessageError(f"Invalid client id {id_}")
                client = self._ensure_client(id_)
                direction = parts.popleft()

                if direction == MQTT_APP_CLIENT_OUT:
                    category = parts.popleft()

                    if category == MQTT_APP_CLIENT_STATUS:
                        status = message.decode_payload_as(ClientStatus)
                        if client.status is not None and client.status == status:
                            return
                        logging.info(f"Client {client.id} {category} changed : "
                                     f"{client.status} -> {status}")
                        client.status = status
                        await GcnStatusChange(client=client).send()

                    elif category == MQTT_APP_CLIENT_HEARTBEAT:
                        heartbeat = message.decode_payload_as(int)
                        logging.debug(f"Got heartbeat {heartbeat} for {client.id}")
                        client.heartbeat = heartbeat
                        await client.heartbeat_watchdog.restart()
                        skew = abs(heartbeat - time.time())
                        if skew < self._args.heartbeat_max_skew:
                            return
                        await GcnHeartbeatSkewed(client=client, skew=skew,
                                                 max_skew=self._args.heartbeat_max_skew).send()

                    elif category == MQTT_APP_CLIENT_BUFFER_TOTAL_DROPPED_ITEM:
                        dropped = message.decode_payload_as(int)
                        if client.buffer_total_dropped_item is not None and client.buffer_total_dropped_item == dropped:
                            return
                        logging.info(f"Client {client.id} {category} changed : "
                                     f"{client.buffer_total_dropped_item} -> {dropped}")
                        client.buffer_total_dropped_item = dropped
                        await GcnDroppedItems(client=client).send()

                    elif category == MQTT_APP_CLIENT_MONITORED_GPIO:
                        monitored_gpio = message.decode_payload()
                        monitored_gpio = tuple(sorted(set(pin.strip() for pin in monitored_gpio.split(","))))
                        if monitored_gpio != client.monitored_gpio:
                            logging.info(f"Client {client.id} monitored GPIO has changed : "
                                         f"{client.monitored_gpio}-> {monitored_gpio}")
                        client.monitored_gpio = monitored_gpio
                        # notify non-monitored gpio for which data has been seen
                        if client.gpio is None:
                            return
                        unmonitored_gpio = tuple(gpio for gpio in client.gpio if gpio not in monitored_gpio)
                        if len(unmonitored_gpio) == 0:
                            return
                        logging.warning(f"Client {client.id} monitors gpio {client.monitored_gpio} "
                                        f"but also publishes for gpio {unmonitored_gpio}")

                    elif category == MQTT_APP_CLIENT_GPIO:
                        gpio = parts.popleft()
                        # notify non-monitored gpio for which data is seen
                        if client.monitored_gpio is not None and gpio not in client.monitored_gpio:
                            logging.warning(f"Ignoring gpio {gpio} for client {client.id} which does not monitor it")
                            return
                        # validate payload
                        sample = message.decode_payload().split()
                        try:
                            if len(sample) != 2:
                                raise ValidationError("Sample must have 2 elements")
                            sample = GpioSample(**{"state": sample[0], "timestamp": sample[1]})
                        except ValidationError:
                            raise MessageError(f"Client {client.id} gpio {gpio} : invalid payload {message.payload}")
                        # check if previous
                        try:
                            previous = client.gpio[gpio]
                        except KeyError:
                            logging.info(f"Client {client.id} gpio {gpio} first sample has state {sample.state} "
                                         f"with age of {abs(sample.timestamp - time.time())} seconds")
                            client.gpio[gpio] = sample
                            return
                        client.gpio[gpio] = sample
                        # check if changed
                        if previous.state == sample.state:
                            logging.debug(f"Client {client.id} gpio {gpio} has not changed from timestamps "
                                          f"{previous.timestamp} to {sample.timestamp} : {sample.state}")
                            return
                        # notify of change
                        await GcnGpioChanged(client=client, gpio_name=gpio, gpio_is_set=bool(sample.state))

                    else:
                        logging.debug(f"Ignoring unknown client topic category {category}")

                elif direction == MQTT_APP_CLIENT_IN:
                    raise NotImplementedError()

                else:
                    raise MessageError(f"Unknown direction {direction}")

            elif app == MQTT_APP_MANAGER:
                category = parts.popleft()

                if category == MQTT_APP_MANAGER_STATUS:
                    id_ = parts.popleft()
                    if MQTT_APP_MANAGER_ID.fullmatch(id_) is None:
                        raise MessageError(f"Invalid manager id {id_}")
                    if len(message.payload) == 0:
                        logging.debug(f"Empty manager status message for {id_}, ignoring cleanup")
                        return
                    status = message.decode_payload()
                    if status == MQTT_APP_MANAGER_STATUS_ONLINE:
                        logging.debug(f"Manager {id_} detected online")
                        return
                    elif status == MQTT_APP_MANAGER_STATUS_OFFLINE:
                        logging.debug(f"Manager {id_} detected offline, clearing its status")
                        publisher.clear_topic(message.topic, qos=1)
                    else:
                        raise MessageError(f"Unknown manager status {status}")

                else:
                    raise MessageError(f"Unknown manager category {category}")

            else:
                raise MessageError(f"Unknown app {app}")

        except IndexError:
            raise MessageError(f"Malformed topic {message.topic}")

    async def shutdown(self) -> None:
        logging.info(f"Brain shutting down {len(self._client_infos)} known GCN client(s)")
        while self._client_infos:
            client_id, client_info = self._client_infos.popitem()
            if client_info.heartbeat_watchdog is None:
                continue
            await client_info.heartbeat_watchdog.cancel()
        logging.info(f"Brain shutdown finished")
