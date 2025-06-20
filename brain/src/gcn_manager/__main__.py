#!/usr/bin/env python3

import asyncio
import logging
import os
import random
import signal
import ssl
import sys
from argparse import ArgumentParser, Namespace
from asyncio import AbstractEventLoop

from gcn_manager import AppError, get_env, datetime_system_tz
from gcn_manager.brain import Brain
from gcn_manager.constants import *
from gcn_manager.mqtt import MqttAgent
from gcn_manager.notifiers import ManagerStartingNotification, ManagerExitingNotification


class App:
    def __init__(self, args: Namespace, loop: AbstractEventLoop) -> None:
        self._args = args
        self._loop = loop
        self._shutdown_requested = asyncio.Event()
        self._brain = Brain(args)
        self._mqtt_client_id = f"{MQTT_APP_MANAGER}_{random.randbytes(args.mqtt_client_id_random_bytes).hex().lower()}"
        self._mqtt_agent = MqttAgent(args, self._mqtt_client_id, processor=self._brain,
                                     shutdown_requested=self._shutdown_requested, event_loop=self._loop)

    @property
    def mqtt_client_id(self) -> str:
        return self._mqtt_client_id

    async def _run_loop(self) -> None:
        while True:
            await self._mqtt_agent.run_once()
            await self._brain.shutdown()
            if not self._args.mqtt_reconnect or self._shutdown_requested.is_set():
                break

    def _graceful_shutdown(self, sig: signal.Signals) -> None:
        if sig == signal.SIGINT:
            sig = "SIGINT"
        elif sig == signal.SIGTERM:
            sig = "SIGTERM"
        logging.info(f"Received signal {sig}, requesting graceful shutdown")
        self._shutdown_requested.set()

    async def run_with_graceful_shutdown(self) -> None:
        for sig in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM):
            self._loop.add_signal_handler(sig, self._graceful_shutdown, sig)
        try:
            await self._run_loop()
        except asyncio.CancelledError:
            if not self._shutdown_requested.is_set():
                logging.error("Main task was cancelled but no shutdown had been requested beforehand")
            else:
                logging.warning("Main task was cancelled while a shut down was requested")

    @staticmethod
    async def run(args: Namespace) -> None:
        random.seed()
        start = datetime_system_tz()
        logging.debug(f"Running on system timezone : {start.tzinfo}")
        loop = asyncio.get_running_loop()
        app = App(args, loop)
        await ManagerStartingNotification(id=app.mqtt_client_id, started_at=start).send()
        try:
            await app.run_with_graceful_shutdown()
        except KeyboardInterrupt:
            logging.info("Shutting down due to user interruption")
        finally:
            await ManagerExitingNotification(id=app.mqtt_client_id, run_duration=datetime_system_tz() - start).send()
            logging.info("Exiting application loop")


def _tls_available_versions():
    return (v for v in vars(ssl.TLSVersion) if not v.startswith("_"))


def _to_tls_version(tls_version: str):
    if tls_version is None:
        return None
    if not hasattr(ssl.TLSVersion, tls_version):
        versions = " ".join(_tls_available_versions())
        raise AppError(f"Unknown TLS version '{tls_version}', available versions: {versions}")
    return getattr(ssl.TLSVersion, tls_version)


def main_trace() -> None:
    parser = ArgumentParser()
    parser.add_argument(CLI_OPT_TRACE, action="store_true")
    parser.add_argument("--log-level", choices=("debug", "info", "warning", "error", "critical"),
                        default=os.environ.get(ENV_GCN_MQTT_LOG_LEVEL, DEFAULT_GCN_MQTT_LOG_LEVEL), metavar="LVL")

    parser.add_argument("--mqtt-keep-alive", type=float, metavar="SEC",
                        default=os.environ.get(ENV_GCN_MQTT_KEEPALIVE_SECOND, DEFAULT_GCN_MQTT_KEEPALIVE_SECOND))
    parser.add_argument("--mqtt-connect-timeout", type=float, metavar="SEC",
                        default=os.environ.get(ENV_GCN_MQTT_CONNECT_TIMEOUT_SECOND,
                                               DEFAULT_GCN_MQTT_CONNECT_TIMEOUT_SECOND))
    parser.add_argument("--mqtt-reconnect", action="store_true",
                        default=os.environ.get(ENV_GCN_MQTT_RECONNECT, DEFAULT_ENV_GCN_MQTT_RECONNECT))
    parser.add_argument("--mqtt-still-connecting-alert", type=float, metavar="SEC",
                        default=os.environ.get(ENV_GCN_MQTT_STILL_CONNECTING_ALERT,
                                               DEFAULT_GCN_MQTT_STILL_CONNECTING_ALERT))
    parser.add_argument("--mqtt-transport", choices=("tcp", "websocket", "unix"), metavar="STR",
                        default=os.environ.get(ENV_GCN_MQTT_TRANSPORT, DEFAULT_GCN_MQTT_TRANSPORT))
    parser.add_argument("--mqtt-client-id-random-bytes", type=int, metavar="N",
                        default=os.environ.get(ENV_GCN_MQTT_CLIENT_ID_RANDOM_BYTES,
                                               DEFAULT_GCN_MQTT_CLIENT_ID_RANDOM_BYTES))
    parser.add_argument("--mqtt-host", metavar="HOST")
    parser.add_argument("--mqtt-port", type=int, metavar="PORT")
    parser.add_argument("--mqtt-tls-min-version", metavar="VER", choices=_tls_available_versions(),
                        default=_to_tls_version(
                            os.environ.get(ENV_GCN_MQTT_TLS_MIN_VERSION, DEFAULT_GCN_MQTT_TLS_MIN_VERSION)))
    parser.add_argument("--mqtt-tls-max-version", metavar="VER", choices=_tls_available_versions(),
                        default=_to_tls_version(
                            os.environ.get(ENV_GCN_MQTT_TLS_MAX_VERSION, DEFAULT_GCN_MQTT_TLS_MAX_VERSION)))
    parser.add_argument("--mqtt-tls-ciphers",
                        default=os.environ.get(ENV_GCN_MQTT_TLS_CIPHERS, DEFAULT_GCN_MQTT_TLS_CIPHERS), metavar="STR")
    parser.add_argument("--mqtt-socket-send-buffer-size", type=int,
                        default=os.environ.get(ENV_GCN_MQTT_SOCKET_SEND_BUFFER_SIZE,
                                               DEFAULT_GCN_MQTT_SOCKET_SEND_BUFFER_SIZE), metavar="N")

    parser.add_argument("--idle-loop-sleep", metavar="SEC",
                        default=os.environ.get(ENV_GCN_IDLE_LOOP_SLEEP_SEC, DEFAULT_GCN_IDLE_LOOP_SLEEP_SEC))
    parser.add_argument("--client-heartbeat-max-skew", type=float, metavar="SEC",
                        default=os.environ.get(ENV_GCN_HEARTBEAT_MAX_SKEW_SEC, DEFAULT_GCN_HEARTBEAT_MAX_SKEW_SEC))
    parser.add_argument("--client-heartbeat-watchdog", type=float, metavar="SEC",
                        default=os.environ.get(ENV_GCN_HEARTBEAT_WATCHDOG_SEC, DEFAULT_GCN_HEARTBEAT_WATCHDOG_SEC))

    parser.add_argument("--enable-email-notifications", action="store_true",
                        default=bool(os.environ.get(ENV_GCN_ENABLE_NOTIFY_EMAIL, 0)))
    parser.add_argument("--enable-sms-notifications", action="store_true",
                        default=bool(os.environ.get(ENV_GCN_ENABLE_NOTIFY_SMS, 0)))
    parser.add_argument("--enable-twitter-notifications", action="store_true",
                        default=bool(os.environ.get(ENV_GCN_ENABLE_NOTIFY_TWITTER, 0)))

    # parser.add_argument("--email-smtp-host", metavar="HOST",
    #                     default=os.environ.get(ENV_EMAIL_SMTP_HOST, None))
    # parser.add_argument("--email-smtp-port", type=int, metavar="PORT",
    #                     default=os.environ.get(ENV_EMAIL_SMTP_PORT, None))
    # parser.add_argument("--email-username", metavar="NAME", default=os.environ.get(ENV_EMAIL_USERNAME, None))
    # parser.add_argument("--email-password", metavar="PASS", default=os.environ.get(ENV_EMAIL_PASSWORD, None))
    # parser.add_argument("--email-smtp-starttls", type=int, metavar="0/1",
    #                     default=os.environ.get(ENV_EMAIL_SMTP_STARTTLS, 0))

    parser.add_argument("--notify-manager-starting-recipients", metavar="A,B,C",
                        default=os.environ.get(ENV_GCN_MANAGER_STARTING_RECIPIENTS, None))
    parser.add_argument("--notify-manager-starting-recipients", metavar="A,B,C",
                        default=os.environ.get(ENV_GCN_MANAGER_STARTING_RECIPIENTS, None))
    parser.add_argument("--notify-manager-starting-recipients", metavar="A,B,C",
                        default=os.environ.get(ENV_GCN_MANAGER_STARTING_RECIPIENTS, None))

    parser.add_argument("--notify-manager-starting-recipients", metavar="A,B,C",
                        default=os.environ.get(ENV_GCN_MANAGER_STARTING_RECIPIENTS, None))
    parser.add_argument("--notify-manager-still-connecting-recipients", metavar="A,B,C",
                        default=os.environ.get(ENV_GCN_MANAGER_STILL_CONNECTING_RECIPIENTS, None))
    parser.add_argument("--notify-manager-connected-recipients", metavar="A,B,C",
                        default=os.environ.get(ENV_GCN_MANAGER_CONNECTED_RECIPIENTS, None))
    parser.add_argument("--notify-manager-disconnected-recipients", metavar="A,B,C",
                        default=os.environ.get(ENV_GCN_MANAGER_DISCONNECTED_RECIPIENTS, None))
    parser.add_argument("--notify-manager-exiting-recipients", metavar="A,B,C",
                        default=os.environ.get(ENV_GCN_MANAGER_EXITING_RECIPIENTS, None))

    parser.add_argument("--notify-client-skewed-heartbeat-recipients", metavar="A,B,C",
                        default=os.environ.get(ENV_GCN_CLIENT_SKEWED_HEARTBEAT_RECIPIENTS, None))
    parser.add_argument("--notify-client-missed-heartbeat-recipients", metavar="A,B,C",
                        default=os.environ.get(ENV_GCN_CLIENT_MISSED_HEARTBEAT_RECIPIENTS, None))
    parser.add_argument("--notify-client-dropped-items-recipients", metavar="A,B,C",
                        default=os.environ.get(ENV_GCN_CLIENT_DROPPED_ITEMS_RECIPIENTS, None))
    parser.add_argument("--notify-client-status-change-recipients", metavar="A,B,C",
                        default=os.environ.get(ENV_GCN_CLIENT_STATUS_CHANGE_RECIPIENTS, None))
    parser.add_argument("--notify-client-gpio-change-recipients", metavar="A,B,C",
                        default=os.environ.get(ENV_GCN_CLIENT_GPIO_CHANGE_RECIPIENTS, None))

    args = parser.parse_args()
    log_level = getattr(logging, args.log_level.upper())
    logging.basicConfig(format="%(levelname)s %(message)s", level=log_level)
    logging.getLogger("asyncio").setLevel(log_level)
    logging.debug(f"Args: {args}")

    if args.mqtt_host is None:
        args.mqtt_host = get_env(ENV_GCN_MQTT_SERVER_HOST)

    if args.mqtt_port is None:
        try:
            args.mqtt_port = int(get_env(ENV_GCN_MQTT_SERVER_PORT))
        except ValueError as e:
            raise AppError(f"MQTT server port must be an integer: {e}")

    try:
        asyncio.run(App.run(args))
    except KeyboardInterrupt:
        logging.info("Shutting down due to user interruption")


def main() -> None:
    try:
        main_trace()
    except AppError as e:
        logging.critical(f"Application error: {e}")
        sys.exit(2)


if __name__ == "__main__":
    if CLI_OPT_TRACE in sys.argv[1:]:
        main_trace()
    else:
        main()
