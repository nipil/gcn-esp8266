import asyncio
import enum
import inspect
import logging
import os
from datetime import datetime
from typing import Callable, Any

from pydantic import BaseModel, Field


class AppError(Exception):
    pass


class MessageError(Exception):
    pass


class MqttMessage(BaseModel):
    topic: str
    payload: bytes

    def decode_payload(self) -> str:
        try:
            return self.payload.decode("utf-8")
        except UnicodeDecodeError:
            raise MessageError(f"Could not decode payload {self.payload} for topic {self.topic}")

    def decode_payload_as(self, target_type: type) -> Any:
        decode = self.decode_payload()
        try:
            return target_type(decode)
        except ValueError as e:
            raise MessageError(f"Invalid payload {self.payload} for type "
                               f"{target_type.__name__} for topic {self.topic} : {e}")


class ClientStatus(enum.StrEnum):
    ONLINE = "online"
    OFFLINE = "offline"


def get_env(key: str) -> str:
    try:
        return os.environ[key]
    except KeyError:
        raise AppError(f'Environment variable {key} not set')


def datetime_system_tz() -> datetime:
    return datetime.now().astimezone()


def datetime_tz(timestamp: float) -> datetime:
    return datetime.fromtimestamp(timestamp).astimezone()


class MqttPublisher:

    def __init__(self, client_id: str) -> None:
        self._client_id = client_id

    @property
    def client_id(self):
        return self._client_id

    def subscribe(self, topic: str, qos: int) -> None:
        raise NotImplementedError()

    def unsubscribe(self, topic: str) -> None:
        raise NotImplementedError()

    def publish(self, topic: str, payload: bytes | bytearray = None, qos: int = 0, retain: bool = False) -> None:
        raise NotImplementedError()

    def clear_topic(self, topic: str, qos: int = 0) -> None:
        # need retain to remove a retained message
        self.publish(topic, b'', qos=qos, retain=True)


class MessageProcessor:

    async def process(self, message: MqttMessage, *, publisher: MqttPublisher) -> None:
        raise NotImplementedError()

    async def shutdown(self) -> None:
        raise NotImplementedError()


class DelayedAction:
    def __init__(self, seconds: float, fn: Callable, *args, loop: asyncio.AbstractEventLoop, name: str) -> None:
        self._seconds = seconds
        self._name = name
        self._event_loop = loop
        self._fn = fn
        self._args = args
        self._task: asyncio.Task | None = None

    def __str__(self) -> str:
        return f"{self.__class__.__name__}({self._seconds}, {self._fn.__name__})"

    @property
    def seconds(self) -> float:
        return self._seconds

    async def create(self) -> None:
        if self._task is not None:
            return
        logging.debug(f"Creating {self._name} task")
        self._task = self._event_loop.create_task(self._run())

    async def cancel(self) -> None:
        if self._task is None:
            return
        if not self._task.done():
            logging.debug(f"Canceling {self._name} task")
            self._task.cancel()
        try:
            result = await self._task
            logging.debug(f"Task {self._name} returned {result}")
        except asyncio.CancelledError:
            logging.debug(f"Task {self._name} has been cancelled")
        self._task = None

    async def _run(self) -> None:
        logging.debug(f"Start of {self._name} task")
        try:
            await asyncio.sleep(self._seconds)
        except asyncio.CancelledError:
            logging.debug(f"Wait in {self._name} task has been cancelled")
            return
        logging.debug(f"Triggering action in {self._name} task")
        result = self._fn(*self._args)
        if inspect.iscoroutinefunction(self._fn):
            await result
        logging.debug(f"End of {self._name} task")

    async def restart(self) -> None:
        await self.cancel()
        await self.create()


class GpioSample(BaseModel, extra="forbid"):
    state: int
    timestamp: int


class ClientInfo(BaseModel):
    id: str
    status: ClientStatus | None = None
    monitored_gpio: tuple[str, ...] | None = None
    heartbeat: int | None = None
    heartbeat_watchdog: DelayedAction | None = None
    hardware: str | None = None
    buffer_total_dropped_item: int = 0
    gpio: dict[str, GpioSample] = Field(default_factory=dict)

    class Config:
        arbitrary_types_allowed = True
