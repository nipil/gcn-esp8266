import asyncio
import logging

from dataclasses import dataclass


@dataclass
class BufferTotalDroppedItem:
    value: int


class NotifyApp:

    def __init__(self, args, in_queue: asyncio.Queue):
        self._in_queue = in_queue

    async def loop(self) -> None:
        notification = await self._in_queue.get()
        logging.warning(f"Notification needs sending : {notification}")

# TODO: turn from a single app dequeuing serially, into a parallel notification task creator
async def run_notify_app(app: NotifyApp) -> None:
    while True:
        await app.loop()
