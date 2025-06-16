#!/usr/bin/env python3
import logging
import os
import random
import ssl
from argparse import ArgumentParser

import paho.mqtt.client as mqtt

from gcn_manager import *


# The callback for when the client receives a CONNACK response from the server.
def on_connect(client, userdata, flags, reason_code, properties):
    print(
        f"Connected with result code {reason_code}")  # Subscribing in on_connect() means that if we lose the connection and  # reconnect then subscriptions will be renewed.  # client.subscribe("$SYS/#")


# The callback for when a PUBLISH message is received from the server.
def on_message(client, userdata, msg):
    print(msg.topic + " " + str(msg.payload))


def main():
    random.seed()
    parser = ArgumentParser()
    parser.add_argument("-l", "--log-level", choices=("debug", "info", "warning", "error", "critical"), default="info")
    parser.add_argument("--mqtt-keep-alive", type=int, default=DEFAULT_MQTT_KEEPALIVE_SECOND)
    parser.add_argument("--mqtt-connect-timeout", type=int, default=DEFAULT_MQTT_CONNECT_TIMEOUT_SECOND)
    parser.add_argument("--mqtt-reconnect", action="store_true")
    parser.add_argument("--mqtt-transport", choices=("tcp", "websocket", "unix"), default=DEFAULT_MQTT_TRANSPORT)
    parser.add_argument("--mqtt-tls-ciphers", default=DEFAULT_MQTT_TLS_CIPHERS)
    args = parser.parse_args()
    logging.basicConfig(format="%(levelname)s %(message)s", level=getattr(logging, args.log_level.upper()))
    logging.debug(f"Args: {args}")

    # TODO: user_data provided to callbacks

    mqttc = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
                        client_id=f"{MQTT_APP}_manager_{random.randbytes(MQTT_CLIENT_RANDOM_BYTES).hex().lower()}",
                        clean_session=True, transport=args.mqtt_transport, reconnect_on_failure=args.mqtt_reconnect)
    mqttc.on_connect = on_connect
    mqttc.on_message = on_message

    mqttc.connect_timeout = args.mqtt_connect_timeout
    mqttc.username_pw_set(username=os.environ[ENV_MQTT_USER_NAME], password=os.environ[ENV_MQTT_USER_PASSWORD])
    mqttc.tls_set(tls_version=ssl.PROTOCOL_TLSv1_2, ciphers=args.mqtt_tls_ciphers)
    mqttc.enable_logger()

    mqttc.will_set(topic=MQTT_APP_MANAGER_STATUS, payload=MQTT_APP_MANAGER_STATUS_OFFLINE, qos=1, retain=True)
    mqttc.connect(os.environ[ENV_MQTT_SERVER_HOST], int(os.environ[ENV_MQTT_SERVER_PORT]), args.mqtt_keep_alive)
    mqttc.publish(topic=MQTT_APP_MANAGER_STATUS, payload=MQTT_APP_MANAGER_STATUS_ONLINE, qos=1, retain=True)
    mqttc.subscribe(MQTT_APP_MANAGER_SUBSCRIPTION, 1)

    mqttc.loop_forever()


if __name__ == '__main__':
    main()
