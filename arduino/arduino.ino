#include <ESP8266WiFi.h>

// ESP8266 default baud rate on boot
#define SERIAL_BAUD 74880

// Change according to your output led
#define STATUS_LED_PIN LED_BUILTIN
#define STATUS_LED_INVERT true

// General behaviour
#define WIFI_WAITING_TIMEOUT_MS 10000
#define ERROR_WAITING_BEFORE_REBOOT_MS 10000
#define LIGHT_SHORT_DURATION_MS 100
#define LIGHT_LONG_DURATION_MS 1000

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
  { "your_ssid", "your_password"},
};

// See https://test.mosquitto.org for the various available setups
#define MQTT_BROKER_DNS_NAME "test.mosquitto.org"
#define MQTT_BROKER_TCP_PORT 1883

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
    MAIN_STATE_ERROR = -1,
    MAIN_STATE_BOOT = 0,
    MAIN_STATE_WIFI_NOT_CONNECTED = 10,
    MAIN_STATE_WIFI_START = 11,
    MAIN_STATE_WIFI_WAITING = 12,
    MAIN_STATE_WIFI_CONNECTED = 13
  } MainState;

  const int WIFI_CREDENTIALS_COUNT = sizeof(wifi_credentials) / sizeof(WifiCredential);

  LightStateMachine &light_state_machine;
  MainState main_state;
  int wifi_credentials_index;
  unsigned long wifi_connecting_start_millis;
  unsigned long error_detected_millis;

  void run_enters() {
    switch (main_state) {
      case MAIN_STATE_ERROR:
        state_error_enter();
        break;
      case MAIN_STATE_BOOT:
        state_boot_enter();
        break;
      case MAIN_STATE_WIFI_NOT_CONNECTED:
        state_wifi_not_connected_enter();
        break;
      case MAIN_STATE_WIFI_START:
        state_wifi_start_enter();
        break;
      case MAIN_STATE_WIFI_WAITING:
        state_wifi_waiting_enter();
        break;
      case MAIN_STATE_WIFI_CONNECTED:
        state_wifi_connected_enter();
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
      case MAIN_STATE_BOOT:
        state_boot_task();
        break;
      case MAIN_STATE_WIFI_NOT_CONNECTED:
        state_wifi_not_connected_task();
        break;
      case MAIN_STATE_WIFI_START:
        state_wifi_start_task();
        break;
      case MAIN_STATE_WIFI_WAITING:
        state_wifi_waiting_task();
        break;
      case MAIN_STATE_WIFI_CONNECTED:
        state_wifi_connected_task();
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

  void state_wifi_not_connected_enter() {
    if (WIFI_CREDENTIALS_COUNT == 0) {
      Serial.println("No available wifi credentials to try");
      set_state(MAIN_STATE_ERROR);
      return;
    }
    light_state_machine.start(2);
    wifi_credentials_index = 0;
    set_state(MAIN_STATE_WIFI_START);
  }

  void state_wifi_not_connected_task() {
    // no continuous action
  }

  void state_wifi_start_enter() {
    wifi_connecting_start_millis = millis();

    const WifiCredential *credential = &wifi_credentials[wifi_credentials_index];

    Serial.print("Trying WiFi credential ");
    Serial.print(wifi_credentials_index);
    Serial.print(" SSID=");
    Serial.print(credential->ssid);
    Serial.print(" password=");
    Serial.println(credential->password);

    WiFi.begin(credential->ssid, credential->password);

    set_state(MAIN_STATE_WIFI_WAITING);
  }

  void state_wifi_start_task() {
    // no continuous action
  }

  void state_wifi_waiting_enter() {
    // no initial action
  }

  void state_wifi_waiting_task() {
    unsigned long wifi_elapsed_ms = millis() - wifi_connecting_start_millis;
    if (is_wifi_connected()) {
      Serial.print("Wifi connected after ");
      Serial.print(wifi_elapsed_ms);
      Serial.println(" ms");

      light_state_machine.permanent();
      set_state(MAIN_STATE_WIFI_CONNECTED);
      return;
    }

    if (wifi_elapsed_ms < WIFI_WAITING_TIMEOUT_MS) {
      return;  // wait more
    }

    Serial.print("Failed to connect to Wifi after ");
    Serial.print(WIFI_WAITING_TIMEOUT_MS);
    Serial.println(" ms");

    if (wifi_credentials_index >= WIFI_CREDENTIALS_COUNT) {
      Serial.print("Exhausted available wifi credentials, aborting");
      set_state(MAIN_STATE_ERROR);
      return;
    }

    wifi_credentials_index++;
    set_state(MAIN_STATE_WIFI_START);
  }

  void state_wifi_connected_enter() {
    // TODO: connect to MQTT
  }

  void state_wifi_connected_task() {
    // TODO: connect to MQTT
  }

  void state_default_enter() {
    state_default_task();
  }

  void state_default_task() {
    Serial.print("Invalid main state ");
    Serial.println(main_state);
    set_state(MAIN_STATE_ERROR);
  }

  void state_boot_enter() {
    Serial.println("Cannot re-enter initial boot state without an actual reboot");
    set_state(MAIN_STATE_ERROR);
  }

  void state_boot_task() {
    Serial.begin(SERIAL_BAUD);
    Serial.print("Wifi MAC address is ");
    Serial.println(WiFi.macAddress());
    light_state_machine.setup();
    WiFi.mode(WIFI_STA);
    set_state(MAIN_STATE_WIFI_NOT_CONNECTED);
  }

public:
  MainStateMachine(LightStateMachine &light_state_machine)
    : light_state_machine(light_state_machine), main_state(MAIN_STATE_BOOT) {
  }

  bool is_wifi_connected() const {
    return WiFi.status() == WL_CONNECTED;
  }

  void update() {
    // common watchdog for wifi disconnections
    if (!is_wifi_connected() && main_state >= MAIN_STATE_WIFI_CONNECTED) {
      set_state(MAIN_STATE_WIFI_NOT_CONNECTED);
    }

    run_tasks();

    light_state_machine.update();
  }
};

ArduinoOutputPin status_pin(STATUS_LED_PIN, false);
LightStateMachine light_state_machine(status_pin, LIGHT_SHORT_DURATION_MS, LIGHT_LONG_DURATION_MS);
MainStateMachine main_state_machine(light_state_machine);

void setup() {
}

void loop() {
  delay(10);
  main_state_machine.update();
}
