#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// ESP8266 default baud rate on boot
#define GCN_SERIAL_BAUD 74880

// Change according to your output led
#define GCN_STATUS_LED_PIN LED_BUILTIN
#define GCN_STATUS_LED_INVERT true

// General behaviour
#define GCN_WIFI_WAITING_TIMEOUT_MS (10 * 1000)
#define GCN_MQTT_WAITING_TIMEOUT_MS (10 * 1000)
#define GCN_ERROR_WAITING_BEFORE_REBOOT_MS (10 * 1000)
#define GCN_LIGHT_SHORT_DURATION_MS 100
#define GCN_LIGHT_LONG_DURATION_MS 1000
#define GCN_MQTT_STATUS_INTERVAL_MS (60 * 1000)
#define GCN_WIFI_STATUS_INTERVAL_MS (60 * 1000)

// Optional behaviours
#define GCN_DEBUG_LOOP_TIMING
// #define GCN_DEBUG_ARDUINO_OUTPUT_PIN
// #define GCN_DEBUG_LIGHT_STATE_MACHINE
// #define GCN_DEBUG_MAIN_STATE_MACHINE
// #define GCN_DEBUG_WIFI_STATUS
// #define GCN_DEBUG_MQTT_PUBLISH
// #define GCN_DEBUG_MQTT_SUBSCRIBE
#define GCN_PERIODIC_REBOOT_AFTER_MS (24 * 60 * 60 * 1000)

// Optional parameters
#ifdef GCN_DEBUG_LOOP_TIMING
#define GCN_SLOW_LOOP_WARN_MS 1000
#endif  // GCN_DEBUG_LOOP_TIMING

// MQTT
// https://test.mosquitto.org for the various available setups
#define GCN_MQTT_BROKER_DNS_NAME "test.mosquitto.org"
#define GCN_MQTT_BROKER_TCP_PORT 1883
#define GCN_MQTT_BROKER_USER_NAME NULL
#define GCN_MQTT_BROKER_PASSWORD NULL
#define GCN_MQTT_BROKER_APP_TOPIC "gcn"
#define GCN_MQTT_BROKER_WILL_SUBTOPIC "out/status"
#define GCN_MQTT_BROKER_WILL_QOS 0
#define GCN_MQTT_BROKER_WILL_RETAIN true
#define GCN_MQTT_BROKER_WILL_MESSAGE "offline"
#define GCN_MQTT_BROKER_BORN_MESSAGE "online"
#define GCN_MQTT_BROKER_CLEAN_SESSION true


typedef struct
{
  const char *const ssid;
  const char *const password;
} WifiCredential;

// Update with your Wifi credential pairs (can use more than one pair, one for each SSID)
const WifiCredential wifi_credentials[] = {
  { "your_ssid", "your_password" },
};

// The pins you want to monitor
const int monitor_digital_pins[] = {
  D1,
};

void (*resetFunc)(void) = 0;

void reboot() {
  Serial.print("Triggering reboot");
  resetFunc();
}

class ArduinoOutputPin {
private:
  const int pin_number;
  const bool invert;

public:
  ArduinoOutputPin(const int pin, const bool invert_output)
    : pin_number(pin), invert(invert_output) {
  }

  void setup() {
    pinMode(pin_number, OUTPUT);
  }

  void on() {
#ifdef GCN_DEBUG_ARDUINO_OUTPUT_PIN
    Serial.print("Turning on ");
    if (invert) {
      Serial.print("inverted ");
    }
    Serial.println(pin_number);
#endif  // GCN_DEBUG_ARDUINO_OUTPUT_PIN

    if (invert) {
      digitalWrite(pin_number, LOW);
    } else {
      digitalWrite(pin_number, HIGH);
    }
  }

  void off() {
#ifdef GCN_DEBUG_ARDUINO_OUTPUT_PIN
    Serial.print("Turning off ");
    if (invert) {
      Serial.print("inverted ");
    }
    Serial.println(pin_number);
#endif  // GCN_DEBUG_ARDUINO_OUTPUT_PIN

    if (invert) {
      digitalWrite(pin_number, HIGH);
    } else {
      digitalWrite(pin_number, LOW);
    }
  }
};

class LightStateMachine {
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

  void set_state(const LightState new_state) {
    light_state = new_state;
    record_time();
#ifdef DEBUG_LIGHT_STATE_MACHINE
    Serial.print("Light new state ");
    Serial.print(new_state);
    Serial.print(" at ");
    Serial.print(last_event_millis);
    Serial.println(" ms");
#endif  // DEBUG_LIGHT_STATE_MACHINE
  }

  void record_time() {
    last_event_millis = millis();
  }

  unsigned long elapsed_time() {
    return millis() - last_event_millis;
  }

public:
  LightStateMachine(ArduinoOutputPin &output_pin, const unsigned long short_ms, const unsigned long long_ms)
    : output_pin(output_pin), short_duration_ms(short_ms), long_duration_ms(long_ms) {
    stop();
  }

  void setup() {
    output_pin.setup();
  }

  void permanent() {
    output_pin.on();
    set_state(LIGHT_STATE_PERMANENT);
#ifdef DEBUG_LIGHT_STATE_MACHINE
    Serial.print("Starting permanent light at ");
    Serial.println(last_event_millis);
#endif  // DEBUG_LIGHT_STATE_MACHINE
  }

  void stop() {
    output_pin.off();
    set_state(LIGHT_STATE_IDLE);
  }

  void start(const int count) {
    blink_count = count;
    blink_index = 0;
    if (blink_count > 0) {
      output_pin.on();
      set_state(LIGHT_STATE_ON);
#ifdef DEBUG_LIGHT_STATE_MACHINE
      Serial.print("Starting light cycle of ");
      Serial.print(count);
      Serial.print(" at ");
      Serial.println(last_event_millis);
#endif  // DEBUG_LIGHT_STATE_MACHINE
    } else {
      stop();
#ifdef DEBUG_LIGHT_STATE_MACHINE
      Serial.print("Stopping light cycle at ");
      Serial.println(last_event_millis);
#endif  // DEBUG_LIGHT_STATE_MACHINE
    }
  }

  void update() {
    switch (light_state) {
      case LIGHT_STATE_IDLE:
        return;
      case LIGHT_STATE_ON:
        if (elapsed_time() < short_duration_ms) {
          break;  // wait more
        }
        blink_index++;
#ifdef DEBUG_LIGHT_STATE_MACHINE
        Serial.print("Light blink ");
        Serial.print(blink_index);
        Serial.print("/");
        Serial.println(blink_count);
#endif  // DEBUG_LIGHT_STATE_MACHINE
        output_pin.off();
        set_state(LIGHT_STATE_OFF);
        break;
      case LIGHT_STATE_OFF:
        if (elapsed_time() < short_duration_ms) {
          break;  // wait more
        }
        if (blink_index < blink_count) {
          output_pin.on();
          set_state(LIGHT_STATE_ON);
          break;
        }
        set_state(LIGHT_STATE_LONG_WAIT);
        break;
      case LIGHT_STATE_LONG_WAIT:
        if (elapsed_time() < long_duration_ms) {
          break;  // wait more
        }
        start(blink_count);  // restart
        break;
      case LIGHT_STATE_PERMANENT:
        break;  // stay
      default:
        Serial.print("Invalid light state ");
        Serial.println(light_state);
    }
  }
};

class MainStateMachine {
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

  const int WIFI_CREDENTIALS_COUNT = sizeof(wifi_credentials) / sizeof(WifiCredential);
  const int MONITOR_DIGITAL_PIN_COUNT = sizeof(monitor_digital_pins) / sizeof(int);

  PubSubClient &mqtt_client;
  LightStateMachine &light_state_machine;
  MainState main_state;
  int wifi_credentials_index;
  unsigned long error_detected_millis;
  unsigned long wifi_connecting_start_millis;
  unsigned long wifi_last_status_display;
  unsigned long mqtt_connecting_start_millis;
  unsigned long mqtt_last_status_display;
  int last_wifi_status;

  void run_enters() {
    switch (main_state) {
      case MAIN_STATE_ERROR:
        state_error_enter();
        break;
      case MAIN_STATE_REBOOT:
        state_reboot_enter();
        break;
      case MAIN_STATE_BOOT:
        state_boot_enter();
        break;
      case MAIN_STATE_WIFI_NOT_CONNECTED:
        state_wifi_not_connected_enter();
        break;
      case MAIN_STATE_WIFI_TRY_CONFIG:
        state_wifi_try_config_enter();
        break;
      case MAIN_STATE_WIFI_CONNECTED:
        state_wifi_connected_enter();
        break;
      case MAIN_STATE_MQTT_CONNECTED:
        state_mqtt_connected_enter();
        break;
      default:
        state_default_enter();
        break;
    }
  }

  void run_tasks() {
    switch (main_state) {
      case MAIN_STATE_ERROR:
        state_error_task();
        break;
      case MAIN_STATE_REBOOT:
        state_reboot_task();
        break;
      case MAIN_STATE_BOOT:
        state_boot_task();
        break;
      case MAIN_STATE_WIFI_NOT_CONNECTED:
        state_wifi_not_connected_task();
        break;
      case MAIN_STATE_WIFI_TRY_CONFIG:
        state_wifi_try_config_task();
        break;
      case MAIN_STATE_WIFI_CONNECTED:
        state_wifi_connected_task();
        break;
      case MAIN_STATE_MQTT_CONNECTED:
        state_mqtt_connected_task();
        break;
      default:
        state_default_task();
        break;
    }
  }

  void set_state(const MainState new_state) {
#ifdef GCN_DEBUG_MAIN_STATE_MACHINE
    Serial.print("Switching main_state to ");
    Serial.println(new_state);
#endif  // GCN_DEBUG_MAIN_STATE_MACHINE
    main_state = new_state;
    run_enters();
  }

  void state_error_enter() {
    error_detected_millis = millis();
    light_state_machine.start(1);
    Serial.print("Unrecoverable error state reached, rebooting in ");
    Serial.print(GCN_ERROR_WAITING_BEFORE_REBOOT_MS);
    Serial.println(" ms");
  }

  void state_error_task() {
    if (millis() - error_detected_millis > GCN_ERROR_WAITING_BEFORE_REBOOT_MS) {
      reboot();
    }
  }

  void state_reboot_enter() {
  }

  void state_reboot_task() {
    reboot();
  }

  void state_boot_enter() {
    Serial.println("Cannot re-enter initial boot state without an actual reboot");
    set_state(MAIN_STATE_ERROR);
  }

  void state_boot_task() {
    Serial.begin(GCN_SERIAL_BAUD);
    Serial.print("MQTT client MAC address is ");
    Serial.println(WiFi.macAddress());
    if (MONITOR_DIGITAL_PIN_COUNT > 0) {
      Serial.print("Monitored digital pins");
      for (int i = 0; i < MONITOR_DIGITAL_PIN_COUNT; i++) {
        Serial.print(" ");
        Serial.print(monitor_digital_pins[i]);
      }
      Serial.println();
    } else {
      Serial.println("No digital pins monitored");
    }
    light_state_machine.setup();
    WiFi.mode(WIFI_STA);
    set_state(MAIN_STATE_WIFI_NOT_CONNECTED);
  }

  void state_wifi_not_connected_enter() {
    if (WIFI_CREDENTIALS_COUNT == 0) {
      Serial.println("No available wifi credentials to try");
      set_state(MAIN_STATE_ERROR);
      return;
    }
    light_state_machine.start(2);
    wifi_credentials_index = 0;
    set_state(MAIN_STATE_WIFI_TRY_CONFIG);
  }

  void state_wifi_not_connected_task() {
    // no continuous action
  }

  void state_wifi_try_config_enter() {
    if (wifi_credentials_index >= WIFI_CREDENTIALS_COUNT) {
      Serial.print("Exhausted available wifi credentials, aborting");
      set_state(MAIN_STATE_ERROR);
      return;
    }

    const WifiCredential *credential = &wifi_credentials[wifi_credentials_index];
    Serial.print("Trying WiFi credential ");
    Serial.print(wifi_credentials_index);
    Serial.print(" SSID=");
    Serial.print(credential->ssid);
    Serial.print(" password=");
    Serial.println(credential->password);

    wifi_connecting_start_millis = millis();
    WiFi.begin(credential->ssid, credential->password);

    last_wifi_status = WiFi.status();
#ifdef GCN_DEBUG_WIFI_STATUS
    Serial.print("Initial wifi status ");
    Serial.println(last_wifi_status);
#endif  // GCN_DEBUG_WIFI_STATUS
  }

  void state_wifi_try_config_task() {
    if (is_wifi_connected()) {
      Serial.print("Wifi connection established in ");
      Serial.print(millis() - wifi_connecting_start_millis);
      Serial.println(" ms");
      set_state(MAIN_STATE_WIFI_CONNECTED);
      return;
    }

    if (millis() - wifi_connecting_start_millis < GCN_WIFI_WAITING_TIMEOUT_MS) {
      return;  // wait more
    }

    Serial.print("Failed to connect to Wifi after ");
    Serial.print(GCN_WIFI_WAITING_TIMEOUT_MS);
    Serial.println(" ms");

    wifi_credentials_index++;
    set_state(MAIN_STATE_WIFI_TRY_CONFIG);
  }

  void state_wifi_connected_enter() {
    if (millis() - wifi_last_status_display > GCN_WIFI_STATUS_INTERVAL_MS) {
      wifi_last_status_display = millis();
      Serial.print("Wifi connection active for ");
      Serial.print((millis() - wifi_connecting_start_millis)) / 1000UL;
      Serial.print(" secs, local IP address is ");
      Serial.println(WiFi.localIP());
    }

    mqtt_connecting_start_millis = millis();

    mqtt_client.disconnect();
    Serial.print("MQTT server ");
    Serial.print(GCN_MQTT_BROKER_DNS_NAME);
    Serial.print(" port ");
    Serial.println(GCN_MQTT_BROKER_TCP_PORT);
    mqtt_client.setServer(GCN_MQTT_BROKER_DNS_NAME, GCN_MQTT_BROKER_TCP_PORT);

    // TODO: subscribe and debug define
    // void mqttCallback(char *topic, byte *payload, unsigned int length);
    // mqtt_client.setCallback(mqttCallback); // TODO: callback for subscribes
  }

  void state_wifi_connected_task() {
    unsigned long elapsed = millis() - mqtt_connecting_start_millis;
    if (elapsed > GCN_MQTT_WAITING_TIMEOUT_MS) {
      Serial.print("Exhausted available wait time to connect to MQTT, aborting");
      set_state(MAIN_STATE_ERROR);
      return;
    }

    String mac = String(WiFi.macAddress());
    String will_topic = String(GCN_MQTT_BROKER_APP_TOPIC) + String("/") + mac + String("/") + String(GCN_MQTT_BROKER_WILL_SUBTOPIC);
    std::replace(mac.begin(), mac.end(), ':', '_');
    String mqtt_client_id = String(GCN_MQTT_BROKER_APP_TOPIC) + String("-") + mac;

    Serial.print("MQTT connect client-id ");
    Serial.print(mqtt_client_id.c_str());
    Serial.print("MQTT will topic ");
    Serial.print(will_topic.c_str());
    Serial.print(" qos ");
    Serial.print(GCN_MQTT_BROKER_WILL_QOS);
    Serial.print(" retain ");
    Serial.print(GCN_MQTT_BROKER_WILL_RETAIN ? "yes" : "NO");
    Serial.print(" message ");
    Serial.println(GCN_MQTT_BROKER_WILL_MESSAGE);

    bool success = mqtt_client.connect(
      mqtt_client_id.c_str(),
      GCN_MQTT_BROKER_USER_NAME, GCN_MQTT_BROKER_PASSWORD,
      will_topic.c_str(), GCN_MQTT_BROKER_WILL_QOS, GCN_MQTT_BROKER_WILL_RETAIN, GCN_MQTT_BROKER_WILL_MESSAGE,
      GCN_MQTT_BROKER_CLEAN_SESSION);

    Serial.print("MQTT connect ");
    Serial.print(success ? "OK" : "FAIL");
    Serial.print(" after ");
    Serial.print(elapsed);
    Serial.print(" ms, state=");
    Serial.println(mqtt_client.state());  // http://pubsubclient.knolleary.net/api#state

    if (!success) {
      return;
    }

    if (!publish(will_topic.c_str(), GCN_MQTT_BROKER_BORN_MESSAGE, true)) {
      Serial.println("MQTT 'born' message could not be published to will topic");
    }

    set_state(MAIN_STATE_MQTT_CONNECTED);
  }

  void state_mqtt_connected_enter() {
    light_state_machine.permanent();
    Serial.print("MQTT connection established in ");
    Serial.print(millis() - mqtt_connecting_start_millis);
    Serial.println(" ms");

    mqtt_last_status_display = millis();

    // TODO: subscribe to gcn/%u/in/command
  }

  void state_mqtt_connected_task() {
    if (millis() - mqtt_last_status_display > GCN_MQTT_STATUS_INTERVAL_MS) {
      mqtt_last_status_display = millis();
      Serial.print("MQTT connection active for ");
      Serial.print((millis() - mqtt_connecting_start_millis) / 1000UL);
      Serial.println(" secs");
    }

    // TODO: read initial input states
    // TODO: read current input stats
    // TODO: compute changes
    // TODO: store current as initial
    // TODO: publish changes in topics gcn/%u/out/pins/i with retain ?
  }

  void state_default_enter() {
    state_default_task();
  }

  void state_default_task() {
    Serial.print("Invalid main state ");
    Serial.println(main_state);
    set_state(MAIN_STATE_ERROR);
  }

  bool is_wifi_connected() {
    wl_status_t status = WiFi.status();
    if (status != last_wifi_status) {
      last_wifi_status = status;
#ifdef GCN_DEBUG_WIFI_STATUS
      Serial.print("Wifi status changed to ");
      Serial.println(status);
#endif  // GCN_DEBUG_WIFI_STATUS
    }
    return status == WL_CONNECTED;
  }

  bool is_mqtt_connected() const {
    static bool s_connected = false;
    static int s_state = -1;
    bool connected = mqtt_client.connected();
    int state = mqtt_client.state();
    if (s_connected != connected || s_state != state) {
      s_connected = connected;
      s_state = state;
      Serial.print("MQTT client change detected : connected ");
      Serial.print(connected ? "YES" : "NO");
      Serial.print(" state ");
      Serial.print(state);
      Serial.print(" at ");
      Serial.println(millis());
      Serial.println(" ms");
    }
    return connected;
  }

  bool publish(const String &topic, const String &message, bool retain) {
    bool result = mqtt_client.publish(topic.c_str(), message.c_str(), retain);
#ifdef GCN_DEBUG_MQTT_PUBLISH
    Serial.print("MQTT publish ");
    Serial.print(result ? "OK" : "FAIL");
    Serial.print(" retain ");
    Serial.print(retain ? "YES" : "NO");
    Serial.print(" topic ");
    Serial.print(topic.c_str());
    Serial.print(" message ");
    Serial.println(message.c_str());
#endif  // GCN_DEBUG_MQTT_PUBLISH
    return result;
  }

public:
  MainStateMachine(PubSubClient &mqtt_client, LightStateMachine &light_state_machine)
    : mqtt_client(mqtt_client), light_state_machine(light_state_machine), main_state(MAIN_STATE_BOOT) {
  }

  void update() {
    run_tasks();
    light_state_machine.update();

    // keepalives
    if (is_mqtt_connected()) {
      mqtt_client.loop();
    }

    // watchdogs
#ifdef GCN_PERIODIC_REBOOT_AFTER_MS
    if (millis() > GCN_PERIODIC_REBOOT_AFTER_MS) {
      Serial.print("Maximum running time reached ");
      Serial.print(GCN_PERIODIC_REBOOT_AFTER_MS);
      Serial.println(" ms");
      set_state(MAIN_STATE_REBOOT);
      return;
    }
#endif  // GCN_PERIODIC_REBOOT_AFTER_MS
    if (!is_wifi_connected() && main_state >= MAIN_STATE_WIFI_CONNECTED) {
      Serial.println("Detected WiFi disconnection");
      set_state(MAIN_STATE_WIFI_NOT_CONNECTED);
      return;
    }
    if (!is_mqtt_connected() && main_state >= MAIN_STATE_MQTT_CONNECTED) {
      Serial.println("Detected MQTT disconnection");
      set_state(MAIN_STATE_WIFI_CONNECTED);
      return;
    }
  }
};

WiFiClient espClient;
PubSubClient mqtt_client(espClient);
ArduinoOutputPin status_pin(GCN_STATUS_LED_PIN, false);
LightStateMachine light_state_machine(status_pin, GCN_LIGHT_SHORT_DURATION_MS, GCN_LIGHT_LONG_DURATION_MS);
MainStateMachine main_state_machine(mqtt_client, light_state_machine);

void setup() {
}

void loop() {
  delay(10);
#ifdef GCN_DEBUG_LOOP_TIMING
  static unsigned long max_loop_millis = 0;
  unsigned long start_loop_millis = millis();
#endif  // GCN_DEBUG_LOOP_TIMING
  main_state_machine.update();
#ifdef GCN_DEBUG_LOOP_TIMING
  unsigned long loop_millis = millis() - start_loop_millis;
  if (loop_millis > GCN_SLOW_LOOP_WARN_MS) {
    Serial.print("Slow loop ");
    Serial.print(loop_millis);
    Serial.println(" ms");
  }
  if (loop_millis > max_loop_millis) {
    max_loop_millis = loop_millis;
    Serial.print("Max loop duration reached ");
    Serial.print(loop_millis);
    Serial.println(" ms");
  }
#endif  // GCN_DEBUG_LOOP_TIMING
}
