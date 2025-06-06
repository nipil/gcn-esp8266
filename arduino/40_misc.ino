// GPIO CHANGE NOTIFIER (C) NIPIL 2025+

void setup() {
  Serial.begin(GCN_SERIAL_BAUD);
  Serial.print("MQTT client MAC address is ");
  Serial.println(WiFi.macAddress());
  if (MONITOR_DIGITAL_PINS_COUNT > 0) {
    Serial.print("Will monitor digital pins");
    for (int i = 0; i < MONITOR_DIGITAL_PINS_COUNT; i++) {
      Serial.print(" ");
      Serial.print(MONITOR_DIGITAL_PINS[i]);
    }
    Serial.println();
  } else {
    Serial.println("No digital pins monitored");
  }
  Serial.print("Will use MQTT server ");
  Serial.print(GCN_MQTT_BROKER_DNS_NAME);
  Serial.print(" port ");
  Serial.println(GCN_MQTT_BROKER_TCP_PORT);
  light_state_machine.setup();
  WiFi.mode(WIFI_STA);
}

void loop() {
  delay(GCN_LOOP_MIN_DELAY_MS);
#ifdef GCN_DEBUG_LOOP_TIMING_WARN_MS
  static unsigned long max_loop_millis = 0;
  unsigned long start_loop_millis = millis();
#endif  // GCN_DEBUG_LOOP_TIMING
  main_state_machine.update();
#ifdef GCN_DEBUG_LOOP_TIMING_WARN_MS
  unsigned long loop_millis = millis() - start_loop_millis;
  if (loop_millis > GCN_DEBUG_LOOP_TIMING_WARN_MS) {
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
