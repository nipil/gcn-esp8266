#!/usr/bin/env python3

import argparse
import logging
import os
import queue
import random
import ssl
import sys
import time

import paho.mqtt.client

from gcn_manager import AppError, get_env
from gcn_manager.constants import *
from gcn_manager.mqtt import MqttApp


# The callback for when the client receives a CONNACK response from the server.
def on_connect(client, userdata, flags, reason_code, properties):
    print(
        f"Connected with result code {reason_code}")  # Subscribing in on_connect() means that if we lose the connection and  # reconnect then subscriptions will be renewed.  # client.subscribe("$SYS/#")


# The callback for when a PUBLISH message is received from the server.
def on_message(client, userdata, msg):
    print(msg.topic + " " + str(msg.payload))


def run(args: argparse.Namespace):
    random.seed()
    in_queue = queue.Queue(maxsize=args.mqtt_in_queue_max_size)
    mqtt_client_id = f"{MQTT_APP}_manager_{random.randbytes(args.mqtt_client_id_random_bytes).hex().lower()}"
    paho_mqtt_client = paho.mqtt.client.Client(callback_api_version=paho.mqtt.client.CallbackAPIVersion.VERSION2,
                                               clean_session=True, client_id=mqtt_client_id,
                                               protocol=paho.mqtt.client.MQTTv311, transport=args.mqtt_transport,
                                               reconnect_on_failure=args.mqtt_reconnect)
    paho_mqtt_client.connect_timeout = args.mqtt_connect_timeout
    paho_mqtt_client.tls_set(tls_version=ssl.PROTOCOL_TLSv1_2, ciphers=args.mqtt_tls_ciphers)

    # paho_mqtt_client.enable_logger()  # this is redundant with MqttApp._on_log()

    # FIXME: make auth method modular ? (client certificate ?, tls pre-shared-key ?)
    paho_mqtt_client.username_pw_set(username=get_env(ENV_MQTT_USER_NAME), password=get_env(ENV_MQTT_USER_PASSWORD))

    # TODO: implement proxying ? https://eclipse.dev/paho/files/paho.mqtt.python/html/client.html#paho.mqtt.client.Client.proxy_set

    mqtt_app = MqttApp(paho_mqtt_client, in_queue, mqtt_client_id=mqtt_client_id)
    mqtt_app.start(args.mqtt_host, args.mqtt_port, args.mqtt_keep_alive)

    while True:
        try:
            time.sleep(0.1)
        except KeyboardInterrupt:
            break

    mqtt_app.stop()

    logging.info("Shutting down MQTT client.")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(ARG_TRACE, action="store_true")
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
    logging.basicConfig(format="%(levelname)s %(message)s", level=getattr(logging, args.log_level.upper()))
    logging.debug(f"Args: {args}")
    run(args)


def try_main():
    try:
        main()
        logging.info("Application finished without error.")
    except AppError as e:
        logging.error(e)
        sys.exit(2)


if __name__ == '__main__':
    if ARG_TRACE in sys.argv[1:]:
        main()
    else:
        try_main()
