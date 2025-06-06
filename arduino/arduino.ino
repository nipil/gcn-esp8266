// GPIO CHANGE NOTIFIER (C) NIPIL 2025+

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

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

// Functional parameters
#define GCN_STATUS_LED_PIN LED_BUILTIN
#define GCN_STATUS_LED_INVERT true
#define GCN_MONITORED_DIGITAL_PINS \
  { D1, }

// General behaviour
#define GCN_SERIAL_BAUD 74880  // ESP8266 default baud rate on boot
#define GCN_PERIODIC_REBOOT_AFTER_MS (24 * 60 * 60 * 1000)
#define GCN_WIFI_WAITING_TIMEOUT_MS (10 * 1000)
#define GCN_MQTT_WAITING_TIMEOUT_MS (10 * 1000)
#define GCN_MQTT_CONNECT_RETRY_WAIT_MS (3 * 1000)
#define GCN_ERROR_WAITING_BEFORE_REBOOT_MS (10 * 1000)
#define GCN_LOOP_MIN_DELAY_MS 10
#define GCN_LIGHT_SHORT_DURATION_MS 100
#define GCN_LIGHT_LONG_DURATION_MS 1000
#define GCN_MQTT_STATUS_INTERVAL_MS (60 * 1000)
#define GCN_WIFI_STATUS_INTERVAL_MS (60 * 1000)

// MQTT data
#define GCN_MQTT_BROKER_APP_TOPIC "gcn"
#define GCN_MQTT_BROKER_WILL_SUBTOPIC "out/status"
#define GCN_MQTT_BROKER_WILL_QOS 0
#define GCN_MQTT_BROKER_WILL_RETAIN true
#define GCN_MQTT_BROKER_WILL_MESSAGE "offline"
#define GCN_MQTT_BROKER_BORN_MESSAGE "online"
#define GCN_MQTT_BROKER_CLEAN_SESSION true

// Optional behaviours
// #define GCN_DEBUG_LOOP_TIMING_WARN_MS 1000
// #define GCN_DEBUG_ARDUINO_OUTPUT_PIN
// #define GCN_DEBUG_LIGHT_STATE_MACHINE
// #define GCN_DEBUG_MAIN_STATE_MACHINE
// #define GCN_DEBUG_WIFI_STATUS_CHANGES
// #define GCN_DEBUG_MQTT_STATUS_CHANGES
// #define GCN_DEBUG_MQTT_PUBLISH
#define GCN_DEBUG_MQTT_SUBSCRIBE

// Use dedicated header to re-#define after #undef
#if __has_include("override_defines.h")
#include "override_defines.h"
#endif

typedef struct {
  const char *const ssid;
  const char *const password;
} WifiCredential;

// Update with your Wifi credential pairs (can use more than one pair, one for each SSID)
const WifiCredential WIFI_CREDENTIALS[] = {
#if defined(GCN_WIFI1_SSID) && defined(GCN_WIFI1_PASS)
  { GCN_WIFI1_SSID, GCN_WIFI1_PASS },
#endif
#if defined(GCN_WIFI2_SSID) && defined(GCN_WIFI2_PASS)
  { GCN_WIFI2_SSID, GCN_WIFI2_PASS },
#endif
#if defined(GCN_WIFI3_SSID) && defined(GCN_WIFI3_PASS)
  { GCN_WIFI3_SSID, GCN_WIFI3_PASS },
#endif
};
const int WIFI_CREDENTIALS_COUNT = sizeof(WIFI_CREDENTIALS) / sizeof(WifiCredential);

// The pins you want to monitor
const int MONITOR_DIGITAL_PINS[] = GCN_MONITORED_DIGITAL_PINS;
const int MONITOR_DIGITAL_PINS_COUNT = sizeof(MONITOR_DIGITAL_PINS) / sizeof(int);

// global functions
void setup();
void loop();
void main_state_machine_mqtt_callback(char *topic, byte *payload, unsigned int length);

class ArduinoOutputPin {
public:
  ArduinoOutputPin(const int pin, const bool invert_output);
  void setup();
  void on();
  void off();
private:
  const int pin_number;
  const bool invert;
};

class LightStateMachine {

public:
  LightStateMachine(ArduinoOutputPin &output_pin, const unsigned long short_ms, const unsigned long long_ms);
  void setup();
  void permanent_on();
  void stop();
  void start(const int count);
  void update();

private:
  typedef enum {
    LIGHT_STATE_IDLE = 0,
    LIGHT_STATE_ON,
    LIGHT_STATE_OFF,
    LIGHT_STATE_LONG_WAIT,
    LIGHT_STATE_PERMANENT,
  } LightState;

  ArduinoOutputPin &output_pin;

  LightState light_state;
  const unsigned long short_duration_ms;
  const unsigned long long_duration_ms;
  unsigned long last_event_millis;
  int blink_count;
  int blink_index;

  void set_state(const LightState new_state);
  void record_event_millis();
  unsigned long millis_since_last_event();
};

class MainStateMachine {

public:
  MainStateMachine(PubSubClient &mqtt_client, LightStateMachine &light_state_machine);
  void update();
  void mqtt_callback(char *topic, byte *payload, unsigned int length);

private:

  typedef enum {
    MAIN_STATE_ERROR = -2,
    MAIN_STATE_REBOOT = -1,
    MAIN_STATE_BOOT = 0,
    MAIN_STATE_WIFI_NOT_CONNECTED = 10,
    MAIN_STATE_WIFI_TRY_CONFIG = 11,
    MAIN_STATE_WIFI_CONNECTED = 12,
    MAIN_STATE_MQTT_CONNECTED = 20,
  } MainState;

  const String mqtt_will_topic_utf8 = get_will_topic_utf8();
  const String mqtt_client_id_utf8 = get_client_id_utf8();

  int wifi_credentials_index;
  WiFiClientSecure wifi_client_secure;
  PubSubClient &mqtt_client;
  LightStateMachine &light_state_machine;
  MainState main_state;

  unsigned long error_detected_millis;
  unsigned long wifi_connecting_start_millis;
  unsigned long wifi_last_status_display;
  unsigned long mqtt_connecting_start_millis;
  unsigned long mqtt_connecting_next_millis;
  unsigned long mqtt_last_status_display;

#ifdef GCN_DEBUG_WIFI_STATUS_CHANGES
  wl_status_t wifi_connected_last_status = WL_NO_SHIELD;
  void print_wifi_status_value_name(wl_status_t status);
#endif  // GCN_DEBUG_WIFI_STATUS_CHANGES

#ifdef GCN_DEBUG_MQTT_STATUS_CHANGES
  int mqtt_connected_last_state = -1;
  bool mqtt_connected_last_value = false;
#endif  //GCN_DEBUG_MQTT_STATUS_CHANGES

  void set_state(const MainState new_state);
  void run_enters();
  void run_tasks();
  void state_boot_task();
  void state_error_enter();
  void state_error_task();
  void state_reboot_enter();
  void state_reboot_task();
  void state_wifi_not_connected_enter();
  void state_wifi_not_connected_task();
  void state_wifi_try_config_enter();
  void state_wifi_try_config_task();
  void state_wifi_connected_enter();
  void state_wifi_connected_task();
  void state_mqtt_connected_enter();
  void state_mqtt_connected_task();
  void state_default_enter();
  void state_default_task();
  bool is_wifi_connected();
  bool is_mqtt_connected();
  bool publish_string(const String &topic, const String &message, bool retain);
  String get_will_topic_utf8();
  String get_client_id_utf8();
};

// Dependency injection

#if GCN_MQTT_BROKER_IS_SECURE
WiFiClient wifi_client;
#else
WiFiClientSecure wifi_client;
#endif
PubSubClient mqtt_client(GCN_MQTT_BROKER_DNS_NAME, GCN_MQTT_BROKER_TCP_PORT, main_state_machine_mqtt_callback, wifi_client);
ArduinoOutputPin status_pin(GCN_STATUS_LED_PIN, GCN_STATUS_LED_INVERT);
LightStateMachine light_state_machine(status_pin, GCN_LIGHT_SHORT_DURATION_MS, GCN_LIGHT_LONG_DURATION_MS);
MainStateMachine main_state_machine(mqtt_client, light_state_machine);
