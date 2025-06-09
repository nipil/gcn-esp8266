// GPIO CHANGE NOTIFIER (C) NIPIL 2025+

#include <ESP8266WiFi.h>
#include "TZ.h"
#include "sntp.h"
#include <PubSubClient.h>

/**** MOST IMPORTANT PARAMETERS ******************************************************************************* */

// Change according to your various WiFi networks
#define GCN_WIFI1_SSID "your_wifi_name1"
#define GCN_WIFI1_PASS "your_wifi_password1"
#define GCN_WIFI2_SSID "your_wifi_name2"
#define GCN_WIFI2_PASS "your_wifi_password2"
#define GCN_WIFI3_SSID "your_wifi_name3"
#define GCN_WIFI3_PASS "your_wifi_password3"

// Change according to your MQTT connection
// See https://test.mosquitto.org for the various setups
#define GCN_MQTT_BROKER_DNS_NAME "test.mosquitto.org"
#define GCN_MQTT_BROKER_IS_SECURE false
#define GCN_MQTT_BROKER_TCP_PORT 1883
#define GCN_MQTT_BROKER_USER_NAME_UTF8 NULL
#define GCN_MQTT_BROKER_PASSWORD NULL

// GPIO pins to be monitored (IMPORTANT: BY NAME !!)
#define GCN_MONITORED_DIGITAL_PIN_A D1
// #define GCN_MONITORED_DIGITAL_PIN_B D...
// #define GCN_MONITORED_DIGITAL_PIN_C D...
// #define GCN_MONITORED_DIGITAL_PIN_D D...
// #define GCN_MONITORED_DIGITAL_PIN_E D...
// #define GCN_MONITORED_DIGITAL_PIN_F D...

/**** MINOR PARAMETERS ******************************************************************************* */

// Functional parameters
#define GCN_STATUS_LED_PIN LED_BUILTIN
#define GCN_STATUS_LED_INVERT true

// Define how much caching is done on GPIO changes until they can be sent to MQTT (round-robin database)
// each of these change will consume one byte of RAM, multiplied by the amount of monitored GPIO
#define GCN_MONITOR_CHANGE_QUEUE_DEFAULT_SIZE_TWO_POW 4
#define GCN_MONITOR_CHANGE_QUEUE_DEBOUNCE_MS 100

/**** MOST USED PARAMETERS ******************************************************************************* */

// General behaviour
#define GCN_SERIAL_BAUD 74880  // ESP8266 default baud rate on boot
#define GCN_PERIODIC_REBOOT_AFTER_MS (24 * 60 * 60 * 1000)
#define GCN_WIFI_WAITING_TIMEOUT_MS (10 * 1000)
#define GCN_SNTP_WAITING_TIMEOUT_MS (15 * 1000)
#define GCN_MQTT_BROKER_RETRY_MS (5 * 1000)
#define GCN_LOOP_MIN_DELAY_MS 10
#define GCN_LIGHT_SHORT_DURATION_MS 100
#define GCN_LIGHT_LONG_DURATION_MS 1000

// TLS timings for mqtt_client.connect()
// - each failed attempt blocks for about 16 seconds
// - requires about 3 seconds when succeeding
#define GCN_MQTT_WAITING_TIMEOUT_MS (60 * 1000)

// SNTP data
#define GCN_SNTP_TIMEZONE TZ_Europe_Paris  // from Arduino/cores/esp8266/TZ.h
#define GCN_SNTP_SERVER1 "fr.pool.ntp.org"
#define GCN_SNTP_SERVER2 "ntp.ripe.net"
#define GCN_SNTP_SERVER3 "time.apple.com"
#define GCN_SNTP_RESYNCHRONIZE_INTERVAL_MINUTE 30

// MQTT data
#define GCN_MQTT_BROKER_APP_TOPIC "gcn"
#define GCN_MQTT_BROKER_IN_TOPIC "in"
#define GCN_MQTT_BROKER_OUT_TOPIC "out"
#define GCN_MQTT_BROKER_WILL_TOPIC "status"
#define GCN_MQTT_BROKER_WILL_QOS 0
#define GCN_MQTT_BROKER_WILL_RETAIN true
#define GCN_MQTT_BROKER_WILL_MESSAGE "offline"
#define GCN_MQTT_BROKER_BORN_MESSAGE "online"
#define GCN_MQTT_BROKER_CLEAN_SESSION true

// TLS security
#define GCN_TLS_VERSION_MIN BR_TLS12  // Arduino/tools/sdk/include/bearssl/bearssl_ssl.h
#define GCN_TLS_VERSION_MAX BR_TLS12  // no support yet for TLS 1.3 in BearSSL
#define GCN_TLS_CIPHERS_HARDEN false  // WARNING: test thouroughly against your servers

// Optional behaviours
// #define GCN_DEBUG_WIFI_STATUS_CHANGES
// #define GCN_DEBUG_MQTT_STATUS_CHANGES
// #define GCN_DEBUG_MAIN_STATE_MACHINE
// #define GCN_DEBUG_LIGHT_STATE_MACHINE
// #define GCN_DEBUG_ARDUINO_OUTPUT_PIN
// #define GCN_DEBUG_MONITOR_RECORD
#define GCN_DEBUG_MONITOR_POP
// #define GCN_DEBUG_MQTT_PUBLISH
// #define GCN_DEBUG_MQTT_SUBSCRIBE

// Input commands
#define GCN_COMMAND_REBOOT "reboot"
#define GCN_COMMAND_DISCONNECT_WIFI "disconnect_wifi"
#define GCN_COMMAND_DISCONNECT_MQTT "disconnect_mqtt"
#define GCN_COMMAND_SYNCHRONIZE_SNTP "synchronize_sntp"

// Use dedicated header to customize setting without modifying main code
// You just have to #undef what you want to change, then #define it with the new value
#if __has_include("override_defines.h")
#include "override_defines.h"
#endif
