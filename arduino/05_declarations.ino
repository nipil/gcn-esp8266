// GPIO CHANGE NOTIFIER (C) NIPIL 2025+

// WIFI

typedef struct {
  const char *const ssid;
  const char *const password;
} WifiCredential;

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

// Global functions

void setup();
void loop();
void print_millis();

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

typedef void (*InterruptServiceRoutine)();

class PinDebouncer {
public:
  virtual bool update(bool &new_value) = 0;
  virtual void setup(InterruptServiceRoutine callback) = 0;
  bool get_last_stable_value();
protected:
  bool last_stable_value;
};

class SinglePinTimeDebouncer : public PinDebouncer {
public:
  const char *gpio_name;
  const uint8_t gpio_number;
  SinglePinTimeDebouncer(const char *gpio_name, const uint8_t gpio_number);
  bool update(bool &new_value);
  void setup(InterruptServiceRoutine callback);
private:
  uint32_t last_change_ms;
};

class DualPinComplementDebouncer : public PinDebouncer {
public:
  const char *gpio_name;
  const uint8_t gpio_number;
  const char *gpio_name_inverted;
  const uint8_t gpio_number_inverted;
  DualPinComplementDebouncer(const char *gpio_name, const uint8_t gpio_number, const char *gpio_name_inverted, uint8_t gpio_number_inverted);
  bool update(bool &new_value);
  void setup(InterruptServiceRoutine callback);
};

// Used to monitor GPIO changes and buffer them (as possible !) until they are sent to MQTT
class GpioChangeBuffer {
public:
  GpioChangeBuffer();
  void push_front(const uint32_t timestamp, const uint8_t bit);
  bool pop_back(uint32_t &timestamp, uint8_t &bit);
  void print();
  static uint32_t get_total_dropped_item_count();

private:
  // MUST be a power of 2, so that the ring can be optimized for and'ing
  static const unsigned int mask = (1 << GCN_CHANGE_QUEUE_SIZE_TWO_POW) - 1;

  // i optimize the entry size by storing the GPIO value (1 bit) in MSB
  // which would leave max 'storable' timestamp to 2147483647, ie Tue Jan 19 2038 03:14:07 GMT+0000
  // so i subtract an 'offset' from the 'real' unix timestamp' in order to widen the scheme longevity
  // this way the max 'storable' timestamp becomes 3847483647, ie Mon Dec 03 2091 01:27:27 GMT+0000
  const uint32_t timestamp_offset = 1700000000;  // Tue Nov 14 2023 22:13:20 GMT+0000

  uint32_t ring[mask + 1];
  unsigned int head;
  unsigned int tail;

  static uint32_t total_dropped_item_count;
};

class MainStateMachine {

public:
  MainStateMachine(PubSubClient &mqtt_client, LightStateMachine &light_state_machine);
  void setup();
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
    MAIN_STATE_SNTP_CONNECTED = 20,
    MAIN_STATE_MQTT_CONNECTED = 30,
  } MainState;

  int wifi_credentials_index;
  PubSubClient &mqtt_client;
  LightStateMachine &light_state_machine;
  MainState main_state;

  IPAddress last_wifi_address;
  unsigned long last_wifi_begin_ms;
  unsigned long last_sntp_begin_ms;
  unsigned long last_mqtt_begin_ms;
  unsigned long next_mqtt_retry_ms;
  unsigned long last_heartbeat_ms;

  unsigned long mqtt_count_sent_ok;
  unsigned long mqtt_count_sent_error;
  unsigned long mqtt_count_connect_ok;
  unsigned long mqtt_count_connect_error;
  unsigned long mqtt_count_subscribe_ok;
  unsigned long mqtt_count_subscribe_error;
  unsigned long mqtt_count_received;

#if defined(GCN_MQTT_BROKER_PERIODIC_UPDATE_INTERVAL_MINUTE) || defined(GCN_COMMAND_SEND_METRICS)
  unsigned long last_metrics_begin_ms;
  bool send_metrics();
#endif  // defined(GCN_MQTT_BROKER_PERIODIC_UPDATE_INTERVAL_MINUTE) || defined(GCN_COMMAND_SEND_METRICS)

#ifdef GCN_DEBUG_WIFI_STATUS_CHANGES
  wl_status_t last_wifi_status = WL_NO_SHIELD;
  void print_wifi_status_value_name(wl_status_t status);
#endif  // GCN_DEBUG_WIFI_STATUS_CHANGES

#ifdef GCN_DEBUG_MQTT_STATUS_CHANGES
  int last_mqtt_state = -1;
  bool last_mqtt_connected = false;
#endif  // GCN_DEBUG_MQTT_STATUS_CHANGES

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
  void state_sntp_connected_enter();
  void state_sntp_connected_task();
  void state_mqtt_connected_enter();
  void state_mqtt_connected_task();
  void state_default_enter();
  void state_default_task();
  bool is_wifi_connected();
  bool is_sntp_connected();
  bool is_mqtt_connected();
  void sntp_synchronize();
  void flush_monitors();
  bool mqtt_publish_topic_string(const char *topic_utf8, const char *message, bool retain);
  bool mqtt_subscribe_topic(const char *topic_utf8, int qos);
  bool mqtt_publish_hardware_constants();
  void mqtt_queue_initial_values();
  void mqtt_flush_buffer_once(const char *gpio_name, GpioChangeBuffer &buffer);
  void mqtt_disconnect_properly();
  String mqtt_get_client_id_utf8();
  String mqtt_get_will_topic_utf8();
  String mqtt_get_in_topic_utf8(int n, ... /* const char * */);
  String mqtt_get_out_topic_utf8(int n, ... /* const char * */);
};
