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
  Serial.print(" and password ");
  Serial.println(credential->password);
#ifdef GCN_DEBUG_WIFI_STATUS_CHANGES
  last_wifi_status = WiFi.status();
  print_millis();
  Serial.print("Initial wifi status ");
  print_wifi_status_value_name(last_wifi_status);
  Serial.println();
#endif  // GCN_DEBUG_WIFI_STATUS_CHANGES
  last_wifi_begin_ms = millis();
  WiFi.begin(credential->ssid, credential->password);
}

void MainStateMachine::state_wifi_try_config_task() {
  unsigned long elapsed = millis() - last_wifi_begin_ms;
  if (is_wifi_connected()) {
    print_millis();
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
  last_sntp_begin_ms = millis();
  print_millis();
  Serial.println("Configuring time synchronization via SNTP");
  configTime(
    GCN_SNTP_TIMEZONE,
#ifdef GCN_SNTP_SERVER1
    GCN_SNTP_SERVER1,
#else
    nullptr,
#endif  // GCN_SNTP_SERVER1
#ifdef GCN_SNTP_SERVER2
    GCN_SNTP_SERVER2,
#else
    nullptr,
#endif  // GCN_SNTP_SERVER2
#ifdef GCN_SNTP_SERVER3
    GCN_SNTP_SERVER3
#else
    nullptr
#endif  // GCN_SNTP_SERVER3
  );
}

void MainStateMachine::state_wifi_connected_task() {
  unsigned long elapsed = millis() - last_sntp_begin_ms;

  if (elapsed > GCN_SNTP_WAITING_TIMEOUT_MS) {
    print_millis();
    Serial.print("Failed to synchronize to SNTP after ");
    Serial.print(elapsed);
    Serial.println("ms");
    set_state(MAIN_STATE_ERROR);
    return;
  }

  if (!is_sntp_connected()) {
    delay(1000);
    return;
  }

  char fmt_buf[sizeof("2025-06-08T09:29:58+0200")];  // use an example to ensure actual size including null terminating byte
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  strftime(fmt_buf, sizeof(fmt_buf), "%FT%T%z", &timeinfo);
  print_millis();
  Serial.print("Synchronized with SNTP unix timestamp=");
  Serial.print(now);
  Serial.print(" local_time_iso=");
  Serial.println(fmt_buf);
  set_state(MAIN_STATE_SNTP_CONNECTED);
}

void setup_wifi() {
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
  WiFi.persistent(false);  // do not store wifi credentials in flash
  WiFi.mode(WIFI_STA);
}
