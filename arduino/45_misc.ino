// GPIO CHANGE NOTIFIER (C) NIPIL 2025+

void print_millis() {
  Serial.print("GCN(");
  Serial.print(millis());
  Serial.print("ms): ");
}

void setup() {
  Serial.begin(GCN_SERIAL_BAUD);
  setup_wifi();
  setup_sntp();
#if GCN_MQTT_BROKER_IS_SECURE
  setup_tls();
#endif
  setup_mqtt();
  setup_monitors();
  main_state_machine.setup();
}

void loop() {
  delay(GCN_LOOP_MIN_DELAY_MS);
  main_state_machine.update();
}
