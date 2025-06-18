import os
from dataclasses import dataclass


class AppError(Exception):
    pass


def get_env(key: str) -> str:
    try:
        return os.environ[key]
    except KeyError:
        raise AppError(f'Environment variable {key} not set')


@dataclass
class AppMqttMessage:
    topic: str
    payload: bytes | bytearray
