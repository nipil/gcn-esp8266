import enum
import os
from dataclasses import dataclass, field


class AppError(Exception):
    pass


class AppErrorCanRetry(Exception):
    pass


# TODO: switch to pydantic
@dataclass(frozen=True)
class AppMqttMessage:
    topic: str
    payload: bytes | bytearray


class ClientStatus(enum.StrEnum):
    ONLINE = 'online'
    OFFLINE = 'offline'


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
