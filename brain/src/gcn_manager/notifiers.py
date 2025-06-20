import logging
from datetime import datetime, timedelta

from pydantic import BaseModel

from gcn_manager import ClientInfo, datetime_tz


class Recipient(BaseModel):
    pass


class Notification(BaseModel):

    def to_json(self) -> str:
        return self.model_dump_json(exclude_none=True)

    def to_raw_text(self) -> str:
        raise NotImplementedError()

    async def send(self) -> None:
        logging.info(f"Sending notification: {self.to_raw_text()}")


class ManagerStartingNotification(Notification):
    id: str
    started_at: datetime

    def to_raw_text(self) -> str:
        return f"Manager {self.id} starting on {self.started_at}"


class ManagerExitingNotification(Notification):
    id: str
    run_duration: timedelta

    def to_raw_text(self) -> str:
        return f"Manager {self.id} exiting after running for {self.run_duration}"


class MqttStillConnectingNotification(Notification):
    id: str
    server: str
    elapsed_seconds: float

    def to_raw_text(self) -> str:
        return f"Manager {self.id} still connecting to MQTT {self.server} after {self.elapsed_seconds} seconds"


class MqttConnectedNotification(Notification):
    id: str
    server: str

    def to_raw_text(self) -> str:
        return f"Manager {self.id} connected to MQTT server {self.server}"


class MqttDisconnectedNotification(Notification):
    id: str
    server: str

    def to_raw_text(self) -> str:
        return f"Manager {self.id} disconnected from MQTT server {self.server}"


class GcnHeartbeatSkewed(Notification):
    client: ClientInfo
    skew: float
    max_skew: float

    def to_raw_text(self) -> str:
        return (f"GCN client {self.client.id} ({self.client.status}) received heartbeat {self.client.heartbeat} "
                f"skew {timedelta(seconds=self.skew)} from manager exceeds {self.max_skew} seconds")


class GcnHeartbeatMissed(Notification):
    client: ClientInfo
    elapsed_seconds: float

    def to_raw_text(self) -> str:
        return (f"GCN client {self.client.id} ({self.client.status}) not received in the last {self.elapsed_seconds} "
                f"seconds, latest heartbeat {self.client.heartbeat} is {datetime_tz(self.client.heartbeat)}")


class GcnDroppedItems(Notification):
    client: ClientInfo

    def to_raw_text(self) -> str:
        return f"GCN client {self.client.id} dropped item reached {self.client.buffer_total_dropped_item}"


class GcnStatusChange(Notification):
    client: ClientInfo

    def to_raw_text(self) -> str:
        return f"GCN client {self.client.id} status is {self.client.status}"


class GcnGpioChanged(Notification):
    client: ClientInfo
    gpio_name: str
    gpio_is_set: bool

    def to_raw_text(self) -> str:
        return f"GCN client {self.client.id} gpio {self.gpio_name} went {'up' if self.gpio_is_set else 'down'}"

# class SmtpConfig(BaseModel):
#     host: str
#     sender: str
#     port: int
#     username: str | None = None
#     password: str | None = None
#     starttls: bool = False
