// GPIO CHANGE NOTIFIER (C) NIPIL 2025+

#ifdef GCN_DEBUG_WIFI_STATUS_CHANGES
void MainStateMachine::print_wifi_status_value_name(wl_status_t status) {
  switch (status) {
    case WL_NO_SHIELD:
      Serial.print("WL_NO_SHIELD");
      break;
    case WL_IDLE_STATUS:
      Serial.print("WL_IDLE_STATUS");
      break;
    case WL_NO_SSID_AVAIL:
      Serial.print("WL_NO_SSID_AVAIL");
      break;
    case WL_SCAN_COMPLETED:
      Serial.print("WL_SCAN_COMPLETED");
      break;
    case WL_CONNECTED:
      Serial.print("WL_CONNECTED");
      break;
    case WL_CONNECT_FAILED:
      Serial.print("WL_CONNECT_FAILED");
      break;
    case WL_CONNECTION_LOST:
      Serial.print("WL_CONNECTION_LOST");
      break;
    case WL_WRONG_PASSWORD:
      Serial.print("WL_WRONG_PASSWORD");
      break;
    case WL_DISCONNECTED:
      Serial.print("WL_DISCONNECTED");
      break;
    default:
      Serial.print("UNDEFINED");
      break;
  }
}
#endif  // GCN_DEBUG_WIFI_STATUS_CHANGES

bool MainStateMachine::is_wifi_connected() {
  // https://github.com/esp8266/Arduino/blob/f5142b883dd84ed682a832ed6fa50ef7f21b655b/cores/esp8266/wl_definitions.h#L60
  // WL_NO_SHIELD        = 255,   // for compatibility with WiFi Shield library
  // WL_IDLE_STATUS      = 0,
  // WL_NO_SSID_AVAIL    = 1,
  // WL_SCAN_COMPLETED   = 2,
  // WL_CONNECTED        = 3,
  // WL_CONNECT_FAILED   = 4,
  // WL_CONNECTION_LOST  = 5,
  // WL_WRONG_PASSWORD   = 6,
  // WL_DISCONNECTED     = 7
  wl_status_t status = WiFi.status();
  IPAddress current_wifi_address = WiFi.localIP();
#ifdef GCN_DEBUG_WIFI_STATUS_CHANGES
  if (status != last_wifi_status) {
    print_millis();
    Serial.print("Wifi status changed from ");
    print_wifi_status_value_name(last_wifi_status);
    Serial.print(" to ");
    print_wifi_status_value_name(status);
    Serial.println();
    last_wifi_status = status;
  }
  if (current_wifi_address != last_wifi_address) {
    print_millis();
    Serial.print("Wifi local address changed from ");
    Serial.print(last_wifi_address);
    Serial.print(" to ");
    Serial.print(current_wifi_address);
    Serial.println();
    last_wifi_address = current_wifi_address;
  }
#endif  // GCN_DEBUG_WIFI_STATUS_CHANGES
  return status == WL_CONNECTED && current_wifi_address.v4() != 0;
}

void MainStateMachine::state_wifi_not_connected_enter() {
  if (WIFI_CREDENTIALS_COUNT == 0) {
    print_millis();
    Serial.println("No available wifi credentials to try");
    set_state(MAIN_STATE_ERROR);
    return;
  }
  light_state_machine.start(2);
  wifi_credentials_index = 0;
  set_state(MAIN_STATE_WIFI_TRY_CONFIG);
}

void MainStateMachine::state_wifi_not_connected_task() {
  // no continuous action
}

void MainStateMachine::state_wifi_try_config_enter() {
  if (wifi_credentials_index >= WIFI_CREDENTIALS_COUNT) {
    print_millis();
    Serial.println("Exhausted available wifi credentials");
    set_state(MAIN_STATE_ERROR);
    return;
  }
  WiFi.disconnect();
  const WifiCredential *credential = &WIFI_CREDENTIALS[wifi_credentials_index];
  print_millis();
  Serial.print("Trying WiFi credential ");
  Serial.print(wifi_credentials_index);
  Serial.print(" with SSID ");
  Serial.print(credential->ssid);
  Serial.print(" and password");
  Serial.println(credential->password);
#ifdef GCN_DEBUG_WIFI_STATUS_CHANGES
  last_wifi_status = WiFi.status();
  print_millis();
  Serial.print("Initial wifi status ");
  print_wifi_status_value_name(last_wifi_status);
  Serial.println();
#endif  // GCN_DEBUG_WIFI_STATUS_CHANGES
  last_wifi_begin = millis();
  WiFi.begin(credential->ssid, credential->password);
}

void MainStateMachine::state_wifi_try_config_task() {
  unsigned long elapsed = millis() - last_wifi_begin;
  if (is_wifi_connected()) {
    Serial.print("Wifi connection established in ");
    Serial.print(elapsed);
    Serial.println("ms");
    set_state(MAIN_STATE_WIFI_CONNECTED);
    return;
  }
  if (elapsed < GCN_WIFI_WAITING_TIMEOUT_MS) {
    return;  // wait more
  }
  print_millis();
  Serial.print("Failed to connect to Wifi after ");
  Serial.print(elapsed);
  Serial.println("ms");
  wifi_credentials_index++;
  set_state(MAIN_STATE_WIFI_TRY_CONFIG);
}

void MainStateMachine::state_wifi_connected_enter() {
  last_mqtt_begin = millis();
}

void MainStateMachine::state_wifi_connected_task() {
  unsigned long elapsed = millis() - last_mqtt_begin;

  if (elapsed > GCN_MQTT_WAITING_TIMEOUT_MS) {
    print_millis();
    Serial.print("Failed to connect to MQTT after ");
    Serial.print(elapsed);
    Serial.println("ms");
    set_state(MAIN_STATE_ERROR);
    return;
  }

#if GCN_MQTT_CLIENT_INSECURE
  Serial.println("Disabling TLS verification on MQTT client making it INSECURE");
  wifi_client_secure.setInsecure();
  mqtt_client.setClient(wifi_client_secure);
#endif

  bool success = mqtt_client.connect(
    mqtt_client_id_utf8.c_str(),
    GCN_MQTT_BROKER_USER_NAME_UTF8, GCN_MQTT_BROKER_PASSWORD,
    mqtt_will_topic_utf8.c_str(), GCN_MQTT_BROKER_WILL_QOS, GCN_MQTT_BROKER_WILL_RETAIN, GCN_MQTT_BROKER_WILL_MESSAGE,
    GCN_MQTT_BROKER_CLEAN_SESSION);

  print_millis();
  Serial.print("MQTT connect ");
  Serial.print(success ? "OK" : "FAIL");
  Serial.print(" state=");
  Serial.println(mqtt_client.state());

  if (!success) {
    delay(1000);
    return;
  }

  if (!mqtt_publish_topic_string(mqtt_will_topic_utf8.c_str(), GCN_MQTT_BROKER_BORN_MESSAGE, true)) {
    print_millis();
    Serial.println("MQTT 'born' message could not be published to will topic");
  }

  set_state(MAIN_STATE_MQTT_CONNECTED);
}
