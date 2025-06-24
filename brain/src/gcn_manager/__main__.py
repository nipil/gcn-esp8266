#!/usr/bin/env python3

import asyncio
import logging
import os
import random
import ssl
import sys
from argparse import ArgumentParser

from gcn_manager import AppError, get_env
from gcn_manager.backoff import ExponentialFullRandomBackOff
from gcn_manager.brain import BrainApp, run_brain_app
from gcn_manager.constants import *
from gcn_manager.mqtt import MqttApp, run_mqtt_app
from gcn_manager.notifier import NotifyApp, run_notify_app

# Source: https://github.com/aio-libs/aiopg/issues/678#issuecomment-667908402
# because asyncio.loop.add/remove_reader/writer raise NotImplemented
# when using the default of WindowsProactorEventLoopPolicy()
if sys.version_info >= (3, 8) and os.name == "nt":
    asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())


async def _run_async(args) -> None:
    # initialize random number generator
    random.seed()
    # dependencies
    loop = asyncio.get_running_loop()
    received_messages = asyncio.Queue(args.mqtt_in_queue_max_size)
    notify_queue = asyncio.Queue(args.notification_out_queue_max_size)
    mqtt_back_off = ExponentialFullRandomBackOff(3, 30)
    mqtt_app = MqttApp(args, loop, received_messages)
    brain_app = BrainApp(args, received_messages, mqtt_app, notify_queue)
    mqtt_app_task = loop.create_task(run_mqtt_app(mqtt_app, mqtt_back_off))
    brain_app_task = loop.create_task(run_brain_app(brain_app))
    notification_app = NotifyApp(args, notify_queue)
    notification_task = loop.create_task(run_notify_app(notification_app))
    await asyncio.gather(mqtt_app_task, brain_app_task, notification_task)

    # TODO: handle clean disconnect


def _tls_available_versions():
    return (v for v in vars(ssl.TLSVersion) if not v.startswith('_'))


def _tls_version(tls_version: str):
    if tls_version is None:
        return None
    if not hasattr(ssl.TLSVersion, tls_version):
        versions = " ".join(_tls_available_versions())
        raise AppError(f"Unknown TLS version '{tls_version}', available versions: {versions}")
    return getattr(ssl.TLSVersion, tls_version)


def main_trace() -> None:
    parser = ArgumentParser()
    parser.add_argument(CLI_OPT_TRACE, action="store_true")
    parser.add_argument("-l", "--log-level", choices=("debug", "info", "warning", "error", "critical"),
                        default=os.environ.get(ENV_MQTT_LOG_LEVEL, DEFAULT_MQTT_LOG_LEVEL), metavar="LVL")
    parser.add_argument("--mqtt-keep-alive", type=int, metavar="SEC",
                        default=os.environ.get(ENV_MQTT_KEEPALIVE_SECOND, DEFAULT_MQTT_KEEPALIVE_SECOND))
    parser.add_argument("--mqtt-connect-timeout", type=int, metavar="SEC",
                        default=os.environ.get(ENV_MQTT_CONNECT_TIMEOUT_SECOND, DEFAULT_MQTT_CONNECT_TIMEOUT_SECOND))
    parser.add_argument("--mqtt-reconnect", action="store_true",
                        default=os.environ.get(ENV_MQTT_RECONNECT, DEFAULT_ENV_MQTT_RECONNECT))
    parser.add_argument("--mqtt-transport", choices=("tcp", "websocket", "unix"), metavar="STR",
                        default=os.environ.get(ENV_MQTT_TRANSPORT, DEFAULT_MQTT_TRANSPORT))
    parser.add_argument("--mqtt-in-queue-max-size", type=int, metavar="N",
                        default=os.environ.get(ENV_MQTT_IN_QUEUE_MAX_SIZE, DEFAULT_MQTT_IN_QUEUE_MAX_SIZE))
    parser.add_argument("--mqtt-client-id-random-bytes", type=int, metavar="N",
                        default=os.environ.get(ENV_MQTT_CLIENT_ID_RANDOM_BYTES, DEFAULT_MQTT_CLIENT_ID_RANDOM_BYTES))
    parser.add_argument("--mqtt-host", metavar="HOST")
    parser.add_argument("--mqtt-port", type=int, metavar="PORT")
    parser.add_argument("--mqtt-tls-min-version", metavar="VER", choices=_tls_available_versions(),
                        default=_tls_version(os.environ.get(ENV_MQTT_TLS_MIN_VERSION, DEFAULT_MQTT_TLS_MIN_VERSION)))
    parser.add_argument("--mqtt-tls-max-version", metavar="VER", choices=_tls_available_versions(),
                        default=_tls_version(os.environ.get(ENV_MQTT_TLS_MAX_VERSION, DEFAULT_MQTT_TLS_MAX_VERSION)))
    parser.add_argument("--mqtt-tls-ciphers", default=os.environ.get(ENV_MQTT_TLS_CIPHERS, DEFAULT_MQTT_TLS_CIPHERS),
                        metavar="STR")
    parser.add_argument("--mqtt-socket-send-buffer-size", type=int,
                        default=os.environ.get(ENV_MQTT_SOCKET_SEND_BUFFER_SIZE, DEFAULT_MQTT_SOCKET_SEND_BUFFER_SIZE),
                        metavar="N")
    parser.add_argument("--idle-loop-sleep",
                        default=os.environ.get(ENV_GCN_IDLE_LOOP_SLEEP_MS, DEFAULT_GCN_IDLE_LOOP_SLEEP_MS),
                        metavar="MS")
    parser.add_argument("--notification-out-queue-max-size", type=int, metavar="N",
                        default=os.environ.get(ENV_GCN_NOTIFICATION_OUT_QUEUE_MAX_SIZE,
                                               DEFAULT_GCN_NOTIFICATION_OUT_QUEUE_MAX_SIZE))

    args = parser.parse_args()
    log_level = getattr(logging, args.log_level.upper())
    logging.basicConfig(format="%(levelname)s %(message)s", level=log_level)
    logging.getLogger("asyncio").setLevel(log_level)
    logging.debug(f"Args: {args}")

    if args.mqtt_host is None:
        args.mqtt_host = get_env(ENV_MQTT_SERVER_HOST)
    if args.mqtt_port is None:
        try:
            args.mqtt_port = int(get_env(ENV_MQTT_SERVER_PORT))
        except ValueError as e:
            raise AppError(f"MQTT server port must be an integer: {e}")

    try:
        asyncio.run(_run_async(args))
    except KeyboardInterrupt:
        logging.info("Shutting down due to user interruption")


def main() -> None:
    try:
        main_trace()
    except AppError as e:
        logging.critical(f"Application error: {e}")
        sys.exit(2)


if __name__ == '__main__':
    if CLI_OPT_TRACE in sys.argv[1:]:
        main_trace()
    else:
        main()
