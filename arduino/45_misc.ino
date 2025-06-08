// GPIO CHANGE NOTIFIER (C) NIPIL 2025+

void print_millis() {
  Serial.print("GCN(");
  Serial.print(millis());
  Serial.print("ms): ");
}

void setup() {
  Serial.begin(GCN_SERIAL_BAUD);
  print_millis();
  Serial.print("WiFi MAC address is ");
  Serial.println(WiFi.macAddress());
  for (int i = 0; i < WIFI_CREDENTIALS_COUNT; i++) {
    print_millis();
    Serial.print("WiFi credential ");
    Serial.print(i);
    Serial.print(" is for SSID ");
    Serial.println(WIFI_CREDENTIALS[i].ssid);
  }
  print_millis();
  Serial.print("Will use SNTP timezone definition ");
  Serial.println(GCN_SNTP_TIMEZONE);
#ifdef GCN_SNTP_SERVER1
  print_millis();
  Serial.print("Will use SNTP server ");
  Serial.println(GCN_SNTP_SERVER1);
#endif  // GCN_SNTP_SERVER1
#ifdef GCN_SNTP_SERVER2
  print_millis();
  Serial.print("Will use SNTP server ");
  Serial.println(GCN_SNTP_SERVER2);
#endif  // GCN_SNTP_SERVER2
#ifdef GCN_SNTP_SERVER3
  print_millis();
  Serial.print("Will use SNTP server ");
  Serial.println(GCN_SNTP_SERVER3);
#endif  // GCN_SNTP_SERVER3
#if GCN_MQTT_BROKER_IS_SECURE
  setup_ca_certificates();
#endif
  print_millis();
  Serial.print("Will use MQTT server ");
  Serial.print(GCN_MQTT_BROKER_DNS_NAME);
  Serial.print(" port ");
  Serial.println(GCN_MQTT_BROKER_TCP_PORT);
  InterruptGpioMonitors::setup();
  WiFi.persistent(false);  // do not store wifi credentials in flash
  WiFi.mode(WIFI_STA);
  light_state_machine.setup();
}

void loop() {
  delay(GCN_LOOP_MIN_DELAY_MS);
  main_state_machine.update();
}


// https://arduino-esp8266.readthedocs.io/en/stable/reference.html
