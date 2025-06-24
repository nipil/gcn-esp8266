import asyncio
import logging

from dataclasses import dataclass


@dataclass
class BufferTotalDroppedItem:
    value: int


class NotifyApp:

    def __init__(self, args, in_queue: asyncio.Queue):
        self._in_queue = in_queue

    @staticmethod
    async def send(notification) -> None:
        logging.warning(f"Notification needs sending : {notification}")

    async def loop(self) -> bool:
        try:
            notification = await self._in_queue.get()
        except asyncio.CancelledError:
            logging.debug("Notify app task is cancelled")
            return False
        await self.send(notification)
        return True

    async def run(self) -> None:
        # TODO: turn from a single app dequeue serially, into a parallel notification task creator
        while True:
            if not await self.loop():
                break
        logging.debug("Notify app task finished")
