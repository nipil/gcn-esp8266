// GPIO CHANGE NOTIFIER (C) NIPIL 2025+

#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)

/** COMMON ************************************************************************************/

#if GCN_MQTT_BROKER_IS_SECURE
BearSSL::X509List ca_certs;
BearSSL::Session tls_session;
WiFiClientSecure wifi_client;
#else
WiFiClient wifi_client;
#endif
PubSubClient mqtt_client(GCN_MQTT_BROKER_DNS_NAME, GCN_MQTT_BROKER_TCP_PORT, main_state_machine_mqtt_callback, wifi_client);

ArduinoOutputPin status_pin(GCN_STATUS_LED_PIN, GCN_STATUS_LED_INVERT);
LightStateMachine light_state_machine(status_pin, GCN_LIGHT_SHORT_DURATION_MS, GCN_LIGHT_LONG_DURATION_MS);
MainStateMachine main_state_machine(mqtt_client, light_state_machine);

/** PER PIN : PIN "A" ************************************************************************************/

#ifdef GCN_MONITORED_DIGITAL_PIN_A

GpioChangeBuffer gpio_changed_buffer_pin_a;

#ifdef GCN_MONITORED_DIGITAL_PIN_A_INVERTED
DualPinComplementDebouncer input_debouncer_pin_a(TO_STRING(GCN_MONITORED_DIGITAL_PIN_A), GCN_MONITORED_DIGITAL_PIN_A, TO_STRING(GCN_MONITORED_DIGITAL_PIN_A_INVERTED), GCN_MONITORED_DIGITAL_PIN_A_INVERTED);
#else
SinglePinTimeDebouncer input_debouncer_pin_a(TO_STRING(GCN_MONITORED_DIGITAL_PIN_A), GCN_MONITORED_DIGITAL_PIN_A);
#endif  // GCN_MONITORED_DIGITAL_PIN_A_INVERTED

static void gpio_changed_pin_a() {

#ifdef GCN_DEBUG_DEBOUNCER_ISR
  Serial.print("gpio_changed_pin_a called ");
#endif  // GCN_DEBUG_DEBOUNCER_ISR

  bool new_bit;
  if (!input_debouncer_pin_a.update(new_bit)) {
#ifdef GCN_DEBUG_DEBOUNCER_ISR
    Serial.println("no_update");
#endif  // GCN_DEBUG_DEBOUNCER_ISR
    return;
  }

  uint32_t timestamp = time(nullptr);
#ifdef GCN_DEBUG_DEBOUNCER_ISR
  Serial.println(timestamp);
#endif  // GCN_DEBUG_DEBOUNCER_ISR

  // interrupts disabled inside
  gpio_changed_buffer_pin_a.push_front(timestamp, new_bit);
}

IRAM_ATTR static void gpio_changed_isr_pin_a() {
  gpio_changed_pin_a();
}

#endif  // GCN_MONITORED_DIGITAL_PIN_A

/** ALL PINS **/

void setup_monitors() {
#ifdef GCN_MONITORED_DIGITAL_PIN_A
#ifdef GCN_MONITORED_DIGITAL_PIN_A_INVERTED
  input_debouncer_pin_a.setup(gpio_changed_isr_pin_a);
#else
  input_debouncer_pin_a.setup(gpio_changed_isr_pin_a);
#endif  // GCN_MONITORED_DIGITAL_PIN_A_INVERTED
#endif  // GCN_MONITORED_DIGITAL_PIN_A
}

void MainStateMachine::mqtt_queue_initial_values() {
  bool value;
  uint32_t timestamp = time(nullptr);
#ifdef GCN_MONITORED_DIGITAL_PIN_A
  value = input_debouncer_pin_a.get_last_stable_value();
  print_millis();
  Serial.print("Queuing initial GPIO value ");
  Serial.print(value);
  Serial.println(" for " TO_STRING(GCN_MONITORED_DIGITAL_PIN_A));
  gpio_changed_buffer_pin_a.push_front(timestamp, value);
#endif  // GCN_MONITORED_DIGITAL_PIN_A
}

void MainStateMachine::flush_monitors() {
#ifdef GCN_MONITORED_DIGITAL_PIN_A
  mqtt_flush_buffer_once(input_debouncer_pin_a.gpio_name, gpio_changed_buffer_pin_a);
#endif  // GCN_MONITORED_DIGITAL_PIN_A
}

const char* MONITORED_GPIO_MESSAGE =
#ifdef GCN_MONITORED_DIGITAL_PIN_A
  "" TO_STRING(GCN_MONITORED_DIGITAL_PIN_A)
#endif  // GCN_MONITORED_DIGITAL_PIN_A
  ;
