import enum
import os
from dataclasses import dataclass, field


class AppError(Exception):
    pass


class MessageError(Exception):
    pass


# TODO: switch to pydantic
@dataclass(frozen=True)
class AppMqttMessage:
    topic: str
    payload: bytes | bytearray


class ClientStatus(enum.StrEnum):
    ONLINE = "online"
    OFFLINE = "offline"


@dataclass
class ClientInfo:
    id: str
    status: ClientStatus | None = None
    monitored_gpio: tuple[str] | None = None
    heartbeat: int | None = None
    hardware: str | None = None
    buffer_total_dropped_item: int = 0
    gpio: dict[str, int] = field(default_factory=dict)


def get_env(key: str) -> str:
    try:
        return os.environ[key]
    except KeyError:
        raise AppError(f'Environment variable {key} not set')


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
        # need retain to  remove a retained message
        self.publish(topic, b'', qos=qos, retain=True)


class MessageProcessor:

    async def process(self, topic: str, payload: bytes | bytearray, *, publisher: MqttPublisher) -> None:
        raise NotImplementedError()
