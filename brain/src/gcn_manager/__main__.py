#!/usr/bin/env python3

import argparse
import asyncio
import logging
import os
import sys

from gcn_manager import AppError, get_env
from gcn_manager.app import App
from gcn_manager.constants import *


def main() -> None:
    async def run_loop_forever(app: App) -> None:
        while True:
            await app.loop()
            await asyncio.sleep(args.idle_loop_sleep)

    def run(args_: argparse.Namespace) -> None:
        with App(args_) as app:  # automatic shutdown on clean exit
            try:
                asyncio.run(run_loop_forever(app))
            except KeyboardInterrupt:
                pass

    def try_run(args_: argparse.Namespace) -> None:
        try:
            run(args_)
        except AppError as e:
            logging.critical(f"Application error: {e}")
            sys.exit(2)

    parser = argparse.ArgumentParser()
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
    parser.add_argument("--idle-loop-sleep",
                        default=os.environ.get(ENV_GCN_IDLE_LOOP_SLEEP_MS, DEFAULT_GCN_IDLE_LOOP_SLEEP_MS),
                        metavar="MS")

    args = parser.parse_args()
    log_level = getattr(logging, args.log_level.upper())
    logging.basicConfig(format="%(levelname)s %(message)s", level=log_level)
    logging.getLogger("asyncio").setLevel(log_level)
    logging.debug(f"Args: {args}")

    if args.trace:
        run(args)
    else:
        try_run(args)
    logging.info("Finished.")


if __name__ == '__main__':
    main()
