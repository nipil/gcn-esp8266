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

// GPIO pins to be monitored :
// - a single pin for a time debouncer
// - two pins for a complement debouncer
#define GCN_MONITORED_DIGITAL_PIN_A D1
// #define GCN_MONITORED_DIGITAL_PIN_A_INVERTED D2

/**** MINOR PARAMETERS ******************************************************************************* */

// Functional parameters
#define GCN_STATUS_LED_PIN LED_BUILTIN
#define GCN_STATUS_LED_INVERT true

// Define how much caching is done on GPIO changes until they can be sent to MQTT (round-robin database)
// each of these change will consume one byte of RAM, multiplied by the amount of monitored GPIO
#define GCN_CHANGE_QUEUE_SIZE_TWO_POW 4
#define GCN_CHANGE_BUFFER_DEBOUNCE_MS 10

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
// - requires about 3 seconds the first time when succeeding
#define GCN_MQTT_WAITING_TIMEOUT_MS (60 * 1000)

// SNTP data
#define GCN_SNTP_TIMEZONE TZ_Europe_Paris  // from Arduino/cores/esp8266/TZ.h
#define GCN_SNTP_SERVER1 "fr.pool.ntp.org"
#define GCN_SNTP_SERVER2 "ntp.ripe.net"
#define GCN_SNTP_SERVER3 "time.apple.com"
#define GCN_SNTP_RESYNCHRONIZE_INTERVAL_MINUTE 30

// MQTT functional data
#define GCN_MQTT_BROKER_APP_TOPIC "gcn"
#define GCN_MQTT_BROKER_IN_TOPIC "in"
#define GCN_MQTT_BROKER_OUT_TOPIC "out"
#define GCN_MQTT_BROKER_WILL_TOPIC "status"
#define GCN_MQTT_BROKER_WILL_QOS 0
#define GCN_MQTT_BROKER_WILL_RETAIN true
#define GCN_MQTT_BROKER_WILL_MESSAGE "offline"
#define GCN_MQTT_BROKER_BORN_MESSAGE "online"
#define GCN_MQTT_BROKER_CLEAN_SESSION true
#define GCN_MQTT_BROKER_GPIO_TOPIC "gpio"
#define GCN_MQTT_BROKER_MONITORED_GPIO "monitored_gpio"
#define GCN_MQTT_BROKER_PERIODIC_UPDATE_INTERVAL_MINUTE 10
#define GCN_MQTT_BROKER_HEARTBEAT_TOPIC "heartbeat"
#define GCN_MQTT_BROKER_HEARTBEAT_UPDATE_INTERVAL_SECOND 30

// MQTT hardware data
#define GCN_MQTT_BROKER_HW_TOPIC "hardware"
#define GCN_MQTT_BROKER_ESP8266_TOPIC "esp8266"
#define GCN_MQTT_BROKER_HW_VALUE GCN_MQTT_BROKER_ESP8266_TOPIC
#define GCN_MQTT_BROKER_ESP8266_TOPIC_RESET_REASON "reset_reason"
#define GCN_MQTT_BROKER_ESP8266_TOPIC_CHIP_ID "chip_id"
#define GCN_MQTT_BROKER_ESP8266_TOPIC_CORE_VERSION "core_version"
#define GCN_MQTT_BROKER_ESP8266_TOPIC_SDK_VERSION "sdk_version"
#define GCN_MQTT_BROKER_ESP8266_TOPIC_CPU_FREQ_MHZ "cpu_freq_mhz"
#define GCN_MQTT_BROKER_ESP8266_TOPIC_SKETCH_SIZE "sketch_size"
#define GCN_MQTT_BROKER_ESP8266_TOPIC_FREE_SKETCH_SIZE "free_sketch_size"
#define GCN_MQTT_BROKER_ESP8266_TOPIC_SKETCH_MD5 "sketch_md5"
#define GCN_MQTT_BROKER_ESP8266_TOPIC_FLASH_CHIP_ID "flash_chip_id"
#define GCN_MQTT_BROKER_ESP8266_TOPIC_FLASH_CHIP_SIZE "flash_chip_size"
#define GCN_MQTT_BROKER_ESP8266_TOPIC_FLASH_CHIP_REAL_SIZE "flash_chip_real_size"
#define GCN_MQTT_BROKER_ESP8266_TOPIC_FLASH_CHIP_SPEED_HZ "flash_chip_speed_hz"
#define GCN_MQTT_BROKER_ESP8266_TOPIC_CHECK_FLASH_CRC "check_flash_crc"

// MQTT periodic metrics
#ifdef GCN_MQTT_BROKER_PERIODIC_UPDATE_INTERVAL_MINUTE
#define GCN_MQTT_BROKER_BUFFER_TOTAL_DROPPED_ITEM "buffer_total_dropped_item"
#define GCN_MQTT_BROKER_MQTT_TOPIC "mqtt"
#define GCN_MQTT_BROKER_MQTT_SENT_OK "sent_ok"
#define GCN_MQTT_BROKER_MQTT_SENT_ERROR "sent_error"
#define GCN_MQTT_BROKER_MQTT_SUBSCRIBE_OK "subscribe_ok"
#define GCN_MQTT_BROKER_MQTT_SUBSCRIBE_ERROR "subscribe_error"
#define GCN_MQTT_BROKER_MQTT_CONNECT_OK "connect_ok"
#define GCN_MQTT_BROKER_MQTT_CONNECT_ERROR "connect_error"
#define GCN_MQTT_BROKER_MQTT_RECEIVED "received"
#define GCN_MQTT_BROKER_UPTIME_TOPIC "uptime"
#define GCN_MQTT_BROKER_UPTIME_SYSTEM "system_ms"
#define GCN_MQTT_BROKER_UPTIME_WIFI "wifi_ms"
#define GCN_MQTT_BROKER_UPTIME_SNTP "sntp_ms"
#define GCN_MQTT_BROKER_UPTIME_MQTT "mqtt_ms"
#define GCN_MQTT_BROKER_UPTIME_TIMESTAMP "unix_timestamp"
#define GCN_MQTT_BROKER_NETWORK_TOPIC "network"
#define GCN_MQTT_BROKER_NETWORK_LOCAL_IP "local_ip"
#define GCN_MQTT_BROKER_NETWORK_NETMASK "netmask"
#define GCN_MQTT_BROKER_NETWORK_GATEWAY_IP "gateway_ip"
#define GCN_MQTT_BROKER_NETWORK_DNS "dns"
#define GCN_MQTT_BROKER_NETWORK_DNS_MAX 4
#define GCN_MQTT_BROKER_NETWORK_SSID "ssid"
#define GCN_MQTT_BROKER_NETWORK_BSSID "bssid"
#define GCN_MQTT_BROKER_NETWORK_RSSI "rssi"
#define GCN_MQTT_BROKER_ESP8266_TOPIC_FREE_HEAP "free_heap"
#define GCN_MQTT_BROKER_ESP8266_TOPIC_HEAP_FRAGMENTATION_PERCENT "heap_fragmentation_percent"
#define GCN_MQTT_BROKER_ESP8266_TOPIC_MAX_FREE_BLOCK_SIZE "max_free_block_size"
#endif  // GCN_MQTT_BROKER_PERIODIC_UPDATE_INTERVAL_MINUTE

// TLS security
#define GCN_TLS_VERSION_MIN BR_TLS12  // Arduino/tools/sdk/include/bearssl/bearssl_ssl.h
#define GCN_TLS_VERSION_MAX BR_TLS12  // no support yet for TLS 1.3 in BearSSL
#define GCN_TLS_CIPHERS_HARDEN false  // WARNING: test thouroughly against your servers

// Optional Serial debugging
// #define GCN_DEBUG_WIFI_STATUS_CHANGES
// #define GCN_DEBUG_MQTT_STATUS_CHANGES
// #define GCN_DEBUG_MAIN_STATE_MACHINE
// #define GCN_DEBUG_LIGHT_STATE_MACHINE
// #define GCN_DEBUG_ARDUINO_OUTPUT_PIN
// #define GCN_DEBUG_DEBOUNCER_ISR
// #define GCN_DEBUG_DEBOUNCER_SINGLE
// #define GCN_DEBUG_DEBOUNCER_DUAL
// #define GCN_DEBUG_BUFFER_PUSH
// #define GCN_DEBUG_BUFFER_POP
// #define GCN_DEBUG_MQTT_PUBLISH
// #define GCN_DEBUG_MQTT_SUBSCRIBE
// #define GCN_DEBUG_MQTT_RECEIVED
// #define GCN_DEBUG_MQTT_HEARTBEAT

// Optional input commands (comment to disable)
#define GCN_COMMAND_REBOOT "reboot"
#define GCN_COMMAND_DISCONNECT_WIFI "disconnect_wifi"
#define GCN_COMMAND_DISCONNECT_MQTT "disconnect_mqtt"
#define GCN_COMMAND_SYNCHRONIZE_SNTP "synchronize_sntp"
#define GCN_COMMAND_SEND_METRICS "send_metrics"

// Use dedicated header to customize setting without modifying main code
// You just have to #undef what you want to change, then #define it with the new value
#if __has_include("override_defines.h")
#include "override_defines.h"
#endif
