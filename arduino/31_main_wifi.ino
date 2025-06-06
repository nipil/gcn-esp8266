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
#ifdef GCN_DEBUG_WIFI_STATUS_CHANGES
  if (status != wifi_connected_last_status) {
    Serial.print("Wifi status changed from ");
    print_wifi_status_value_name(wifi_connected_last_status);
    Serial.print(" to ");
    print_wifi_status_value_name(status);
    wifi_connected_last_status = status;
  }
#endif  // GCN_DEBUG_WIFI_STATUS_CHANGES
  return status == WL_CONNECTED;
}

void MainStateMachine::state_wifi_not_connected_enter() {
  if (WIFI_CREDENTIALS_COUNT == 0) {
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
    Serial.print("Exhausted available wifi credentials, aborting");
    set_state(MAIN_STATE_ERROR);
    return;
  }
  const WifiCredential *credential = &WIFI_CREDENTIALS[wifi_credentials_index];
  Serial.print("Trying WiFi credential ");
  Serial.print(wifi_credentials_index);
  Serial.print(" with SSID ");
  Serial.print(credential->ssid);
  Serial.print(" and password");
  Serial.println(credential->password);
  wifi_connecting_start_millis = millis();
  WiFi.begin(credential->ssid, credential->password);
#ifdef GCN_DEBUG_WIFI_STATUS_CHANGES
  wifi_connected_last_status = WiFi.status();
  Serial.print("Initial wifi status ");
  print_wifi_status_value_name(wifi_connected_last_status);
#endif  // GCN_DEBUG_WIFI_STATUS_CHANGES
}

void MainStateMachine::state_wifi_try_config_task() {
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

void MainStateMachine::state_wifi_connected_enter() {
  // TODO: rework because it does not trigger once mqtt is connected... or suppress
  if (millis() - wifi_last_status_display > GCN_WIFI_STATUS_INTERVAL_MS) {
    wifi_last_status_display = millis();
    Serial.print("Wifi connection active for ");
    Serial.print((millis() - wifi_connecting_start_millis)) / 1000UL;
    Serial.print(" secs, local IP address is ");
    Serial.println(WiFi.localIP());
  }
  mqtt_connecting_start_millis = mqtt_connecting_next_millis = millis();
}

void MainStateMachine::state_wifi_connected_task() {
  unsigned long elapsed = millis() - mqtt_connecting_start_millis;
  if (elapsed > GCN_MQTT_WAITING_TIMEOUT_MS) {
    Serial.print("Exhausted available wait time to connect to MQTT, aborting");
    set_state(MAIN_STATE_ERROR);
    return;
  }

  if (millis() < mqtt_connecting_next_millis) {
    return;  // wait
  }

  bool success = mqtt_client.connect(
    mqtt_client_id_utf8.c_str(),
    GCN_MQTT_BROKER_USER_NAME_UTF8, GCN_MQTT_BROKER_PASSWORD,
    mqtt_will_topic_utf8.c_str(), GCN_MQTT_BROKER_WILL_QOS, GCN_MQTT_BROKER_WILL_RETAIN, GCN_MQTT_BROKER_WILL_MESSAGE,
    GCN_MQTT_BROKER_CLEAN_SESSION);

  Serial.print("MQTT connect ");
  Serial.print(success ? "OK" : "FAIL");
  Serial.print(" after ");
  Serial.print(millis() - mqtt_connecting_start_millis);
  Serial.print(" ms, state=");
  Serial.println(mqtt_client.state());

  if (!success) {
    mqtt_connecting_next_millis = millis() + GCN_MQTT_CONNECT_RETRY_WAIT_MS;
#if GCN_MQTT_BROKER_IS_SECURE
    Serial.println("Retrying with INSECURE client");
    wifi_client_secure.setInsecure();
    mqtt_client.setClient(wifi_client_secure);
    mqtt_client;
#endif
    return;
  }

  if (!publish_string(mqtt_will_topic_utf8.c_str(), GCN_MQTT_BROKER_BORN_MESSAGE, true)) {
    Serial.println("MQTT 'born' message could not be published to will topic");
  }

  set_state(MAIN_STATE_MQTT_CONNECTED);
}
