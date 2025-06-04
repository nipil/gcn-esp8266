#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// ESP8266 default baud rate on boot
#define SERIAL_BAUD 74880

// Change according to your output led
#define STATUS_LED_PIN LED_BUILTIN
#define STATUS_LED_INVERT true

// General behaviour
#define PERIODIC_REBOOT_AFTER_MS (24 * 60 * 60 * 1000)
#define WIFI_WAITING_TIMEOUT_MS (10 * 1000)
#define MQTT_WAITING_TIMEOUT_MS (10 * 1000)
#define ERROR_WAITING_BEFORE_REBOOT_MS (10 * 1000)
#define LIGHT_SHORT_DURATION_MS 100
#define LIGHT_LONG_DURATION_MS 1000

// MQTT
// https://test.mosquitto.org for the various available setups
#define MQTT_BROKER_DNS_NAME "test.mosquitto.org"
#define MQTT_BROKER_TCP_PORT 1883
#define MQTT_BROKER_USERNAME NULL
#define MQTT_BROKER_PASSWORD NULL
#define MQTT_BROKER_APP_TOPIC "gcn"
#define MQTT_BROKER_WILL_SUBTOPIC "status"
#define MQTT_BROKER_WILL_QOS 1
#define MQTT_BROKER_WILL_RETAIN true
#define MQTT_BROKER_WILL_MESSAGE "offline"
#define MQTT_BROKER_CLEAN_SESSION true

// Serial debugging
// #define DEBUG_ARDUINO_OUTPUT_PIN
// #define DEBUG_LIGHT_STATE_MACHINE
// #define DEBUG_MAIN_STATE_MACHINE

typedef struct
{
  const char *const ssid;
  const char *const password;
} WifiCredential;

const WifiCredential wifi_credentials[] = {
  { "your_ssid", "your_password" },
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
#ifdef DEBUG_ARDUINO_OUTPUT_PIN
    Serial.print("Turning on ");
    if (invert) {
      Serial.print("inverted ");
    }
    Serial.println(pin_number);
#endif  // DEBUG_ARDUINO_OUTPUT_PIN

    if (invert) {
      digitalWrite(pin_number, LOW);
    } else {
      digitalWrite(pin_number, HIGH);
    }
  }

  void off() {
#ifdef DEBUG_ARDUINO_OUTPUT_PIN
    Serial.print("Turning off ");
    if (invert) {
      Serial.print("inverted ");
    }
    Serial.println(pin_number);
#endif  // DEBUG_ARDUINO_OUTPUT_PIN

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

  PubSubClient &mqtt_client;
  LightStateMachine &light_state_machine;
  MainState main_state;
  int wifi_credentials_index;
  unsigned long error_detected_millis;
  unsigned long wifi_connecting_start_millis;
  unsigned long mqtt_connecting_start_millis;
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
#ifdef DEBUG_MAIN_STATE_MACHINE
    Serial.print("Switching main_state to ");
    Serial.println(new_state);
#endif  // DEBUG_MAIN_STATE_MACHINE
    main_state = new_state;
    run_enters();
  }

  void state_error_enter() {
    error_detected_millis = millis();
    light_state_machine.start(1);
    Serial.print("Unrecoverable error state reached, rebooting in ");
    Serial.print(ERROR_WAITING_BEFORE_REBOOT_MS);
    Serial.println(" ms");
  }

  void state_error_task() {
    if (millis() - error_detected_millis > ERROR_WAITING_BEFORE_REBOOT_MS) {
      reboot();
    }
  }

  void state_reboot_enter() {
    Serial.print("Maximum running time reached ");
    Serial.print(PERIODIC_REBOOT_AFTER_MS);
    Serial.println(" ms");
  }

  void state_reboot_task() {
    reboot();
  }

  void state_boot_enter() {
    Serial.println("Cannot re-enter initial boot state without an actual reboot");
    set_state(MAIN_STATE_ERROR);
  }

  void state_boot_task() {
    Serial.begin(SERIAL_BAUD);
    Serial.print("MQTT client MAC address is ");
    Serial.println(WiFi.macAddress());
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
    Serial.print("Initial wifi status ");
    Serial.println(last_wifi_status);
  }

  void state_wifi_try_config_task() {
    if (is_wifi_connected()) {
      Serial.println("is_wifi_connected");
      set_state(MAIN_STATE_WIFI_CONNECTED);
      return;
    }

    if (millis() - wifi_connecting_start_millis < WIFI_WAITING_TIMEOUT_MS) {
      return;  // wait more
    }

    Serial.print("Failed to connect to Wifi after ");
    Serial.print(WIFI_WAITING_TIMEOUT_MS);
    Serial.println(" ms");

    wifi_credentials_index++;
    set_state(MAIN_STATE_WIFI_TRY_CONFIG);
  }

  void state_wifi_connected_enter() {
    Serial.print("Wifi connected after ");
    Serial.print(millis() - wifi_connecting_start_millis);
    Serial.println(" ms");
    Serial.print("Local IP address is ");
    Serial.println(WiFi.localIP());

    mqtt_connecting_start_millis = millis();

    if (is_mqtt_connected()) {
      Serial.println("MQTT client already connected... WEIRD !");
      set_state(MAIN_STATE_MQTT_CONNECTED);
    }

    Serial.print("Will connect to MQTT server ");
    Serial.print(MQTT_BROKER_DNS_NAME);
    Serial.print(" port ");
    Serial.println(MQTT_BROKER_TCP_PORT);
    mqtt_client.setServer(MQTT_BROKER_DNS_NAME, MQTT_BROKER_TCP_PORT);

    // void mqttCallback(char *topic, byte *payload, unsigned int length);
    // mqtt_client.setCallback(mqttCallback); // TODO: callback for subscribes
  }

  void state_wifi_connected_task() {
    if (millis() - mqtt_connecting_start_millis > MQTT_WAITING_TIMEOUT_MS) {
      Serial.print("Exhausted available wait time to connect to MQTT, aborting");
      set_state(MAIN_STATE_ERROR);
      return;
    }

    String mac = String(WiFi.macAddress());
    String will_topic = String(MQTT_BROKER_APP_TOPIC) + String("/") + mac + String("/") + String(MQTT_BROKER_WILL_SUBTOPIC);
    std::replace(mac.begin(), mac.end(), ':', '_');
    String mqtt_client_id = String(MQTT_BROKER_APP_TOPIC) + String("-") + mac;

    Serial.print("MQTT-client-id ");
    Serial.print(mqtt_client_id.c_str());
    Serial.print("MQTT-will topic ");
    Serial.print(will_topic.c_str());
    Serial.print(" qos ");
    Serial.print(MQTT_BROKER_WILL_QOS);
    Serial.print(" retain ");
    Serial.print(MQTT_BROKER_WILL_RETAIN ? "yes" : "NO");
    Serial.print(" message ");
    Serial.println(MQTT_BROKER_WILL_MESSAGE);

    bool success = mqtt_client.connect(
      mqtt_client_id.c_str(),
      MQTT_BROKER_USERNAME, MQTT_BROKER_PASSWORD,
      will_topic.c_str(), MQTT_BROKER_WILL_QOS, MQTT_BROKER_WILL_RETAIN, MQTT_BROKER_WILL_MESSAGE,
      MQTT_BROKER_CLEAN_SESSION);
    Serial.print("MQTT-connect result ");
    Serial.print(success ? "connected" : "disconnected");
    Serial.print(" state ");
    Serial.println(mqtt_client.state());  // http://pubsubclient.knolleary.net/api#state

    if (success) {
      set_state(MAIN_STATE_MQTT_CONNECTED);
    }

    delay(10000);
  }

  void state_mqtt_connected_enter() {
    light_state_machine.permanent();
    Serial.print("MQTT connected after ");
    Serial.print(millis() - mqtt_connecting_start_millis);
    Serial.println(" ms");
    // TODO: subscribe
  }

  void state_mqtt_connected_task() {
    Serial.print("state_mqtt_connected_task ");
    Serial.println(millis());
    delay(1000);
    // TODO: publish
  }

  void state_default_enter() {
    state_default_task();
  }

  void state_default_task() {
    Serial.print("Invalid main state ");
    Serial.println(main_state);
    set_state(MAIN_STATE_ERROR);
  }

public:
  MainStateMachine(PubSubClient &mqtt_client, LightStateMachine &light_state_machine)
    : mqtt_client(mqtt_client), light_state_machine(light_state_machine), main_state(MAIN_STATE_BOOT) {
  }

  bool is_wifi_connected() {
    wl_status_t status = WiFi.status();
    if (status != last_wifi_status) {
      last_wifi_status = status;
      Serial.print("Wifi status changed to ");
      Serial.println(status);
    }
    return status == WL_CONNECTED;
  }

  bool is_mqtt_connected() const {
    return mqtt_client.connected();
  }

  void update() {
    run_tasks();
    light_state_machine.update();

    // keepalives
    if (is_mqtt_connected()) {
      mqtt_client.loop();
    }

    // watchdogs
    if (millis() > PERIODIC_REBOOT_AFTER_MS) {
      set_state(MAIN_STATE_REBOOT);
      return;
    }
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
ArduinoOutputPin status_pin(STATUS_LED_PIN, false);
LightStateMachine light_state_machine(status_pin, LIGHT_SHORT_DURATION_MS, LIGHT_LONG_DURATION_MS);
MainStateMachine main_state_machine(mqtt_client, light_state_machine);

void setup() {
}

void loop() {
  delay(10);
  main_state_machine.update();
}
