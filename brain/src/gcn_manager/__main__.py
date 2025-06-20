#!/usr/bin/env python3

import asyncio
import logging
import os
import random
import sys
from argparse import ArgumentParser
from queue import Queue

from gcn_manager import AppError, get_env
from gcn_manager.constants import *
from gcn_manager.mqtt import MqttClient

# Source: https://github.com/aio-libs/aiopg/issues/678#issuecomment-667908402
# because asyncio.loop.add/remove_reader/writer raise NotImplemented
# when using the default of WindowsProactorEventLoopPolicy()
if sys.version_info >= (3, 8) and os.name == "nt":
    asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())


async def run_mqtt_client(args, loop, out_queue: Queue) -> None:
    mqtt = MqttClient(args, loop, out_queue)
    mqtt.connect()
    await mqtt.finished
    logging.info("Task MQTT client has finished")  # FIXME: should it really be allowed to finish ?!

    # TODO: retry on recoverable errors


async def run_brain(args, in_queue: Queue) -> None:
    while True:
        await asyncio.sleep(1)

    # TODO: allow task to exit (currently, only KeyboardInterrupt is automatic), instead of canceling ?


async def _run_async(args) -> None:
    # initialize random number generator
    random.seed()
    # shared queue
    received_messages = Queue()
    # get current running async loop and create tasks
    loop = asyncio.get_event_loop()
    mqtt_client_task = loop.create_task(run_mqtt_client(args, loop, received_messages))
    brain_task = loop.create_task(run_brain(args, received_messages))
    # TODO: build list and wait on any, and loop gather list until list is empty
    # run up to completion or interruption
    await asyncio.gather(brain_task, mqtt_client_task)


def _run(args) -> None:
    try:
        asyncio.run(_run_async(args))
    except KeyboardInterrupt:
        pass


def _try_run(args) -> None:
    try:
        _run(args)
    except AppError as e:
        logging.critical(f"Application error: {e}")
        sys.exit(2)


def main() -> None:
    parser = ArgumentParser()
    parser.add_argument("--trace", action="store_true")
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
    parser.add_argument("--mqtt-host", default=get_env(ENV_MQTT_SERVER_HOST), metavar="HOST")
    parser.add_argument("--mqtt-port", type=int, default=get_env(ENV_MQTT_SERVER_PORT), metavar="PORT")
    parser.add_argument("--mqtt-tls-ciphers", default=os.environ.get(ENV_MQTT_TLS_CIPHERS, DEFAULT_MQTT_TLS_CIPHERS),
                        metavar="STR")
    parser.add_argument("--mqtt-socket-send-buffer-size", type=int,
                        default=os.environ.get(ENV_MQTT_SOCKET_SEND_BUFFER_SIZE, DEFAULT_MQTT_SOCKET_SEND_BUFFER_SIZE),
                        metavar="BYTES")
    parser.add_argument("--idle-loop-sleep",
                        default=os.environ.get(ENV_GCN_IDLE_LOOP_SLEEP_MS, DEFAULT_GCN_IDLE_LOOP_SLEEP_MS),
                        metavar="MS")

    args = parser.parse_args()
    log_level = getattr(logging, args.log_level.upper())
    logging.basicConfig(format="%(levelname)s %(message)s", level=log_level)
    logging.getLogger("asyncio").setLevel(log_level)
    logging.debug(f"Args: {args}")

    if args.trace:
        _run(args)
    else:
        _try_run(args)
    logging.info("Finished.")


if __name__ == '__main__':
    main()
