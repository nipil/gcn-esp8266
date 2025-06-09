// GPIO CHANGE NOTIFIER (C) NIPIL 2025+

void main_state_machine_mqtt_callback(char *topic_utf8, byte *payload, unsigned int length) {
  main_state_machine.mqtt_callback(topic_utf8, payload, length);
}

void MainStateMachine::mqtt_callback(char *topic_utf8, byte *payload, unsigned int length) {
#ifdef GCN_DEBUG_MQTT_RECEIVED
  print_millis();
  Serial.print("MQTT recv ");
  Serial.print(length);
  Serial.print(" bytes topic ");
  Serial.println(topic_utf8);
  if (length > 0) {
    for (int i = 0; i < length; i++) {
      Serial.print(payload[i], HEX);
    }
    Serial.println();
    for (int i = 0; i < length; i++) {
      char value = (char)payload[i];
      Serial.print(' ');
      if (value >= 0x20 && value <= 0x7F) {  // ASCII range
        Serial.print(value);
      } else {
        Serial.print(' ');  // replacement character
      }
    }
    Serial.println();
  }
#endif  // GCN_DEBUG_MQTT_RECEIVED

  String topic(topic_utf8);

#ifdef GCN_COMMAND_SYNCHRONIZE_SNTP
  if (topic == mqtt_get_in_topic_utf8(1, GCN_COMMAND_SYNCHRONIZE_SNTP)) {
    print_millis();
    Serial.println("Received 'synchronize SNTP' command");
    sntp_synchronize();
    return;
  }
#endif  // GCN_COMMAND_SYNCHRONIZE_SNTP

#ifdef GCN_COMMAND_DISCONNECT_MQTT
  if (topic == mqtt_get_in_topic_utf8(1, GCN_COMMAND_DISCONNECT_MQTT)) {
    print_millis();
    Serial.println("Received 'disconnect MQTT' command");
    mqtt_client.disconnect();
    set_state(MAIN_STATE_SNTP_CONNECTED);
    return;
  }
#endif  // GCN_COMMAND_DISCONNECT_MQTT

#ifdef GCN_COMMAND_DISCONNECT_WIFI
  if (topic == mqtt_get_in_topic_utf8(1, GCN_COMMAND_DISCONNECT_WIFI)) {
    print_millis();
    Serial.println("Received 'disconnect wifi' command");
    mqtt_client.disconnect();
    WiFi.disconnect();
    set_state(MAIN_STATE_WIFI_NOT_CONNECTED);
    return;
  }
#endif  // GCN_COMMAND_DISCONNECT_WIFI

#ifdef GCN_COMMAND_REBOOT
  if (topic == mqtt_get_in_topic_utf8(1, GCN_COMMAND_REBOOT)) {
    print_millis();
    Serial.println("Received 'reboot' command");
    mqtt_client.disconnect();
    WiFi.disconnect();
    set_state(MAIN_STATE_REBOOT);
    return;
  }
#endif  // GCN_COMMAND_REBOOT

  print_millis();
  Serial.print("Ignoring unknown MQTT input topic");
  Serial.println(topic_utf8);
}

String mqtt_get_topic_utf8(const char *topic_direction, int n, va_list args /* const char* */) {
  String tmp(GCN_MQTT_BROKER_APP_TOPIC);
  tmp += "/";
  tmp += String(WiFi.macAddress());
  tmp += "/";
  tmp += topic_direction;
  for (int i = 0; i < n; i++) {
    tmp += "/";
    tmp += va_arg(args, const char *);
  }
  return tmp;
}

String MainStateMachine::mqtt_get_in_topic_utf8(int n, ... /* const char* */) {
  va_list args;
  va_start(args, n);
  String result = mqtt_get_topic_utf8(GCN_MQTT_BROKER_IN_TOPIC, n, args);
  va_end(args);
  return result;
}

String MainStateMachine::mqtt_get_out_topic_utf8(int n, ... /* const char* */) {
  va_list args;
  va_start(args, n);
  String result = mqtt_get_topic_utf8(GCN_MQTT_BROKER_OUT_TOPIC, n, args);
  va_end(args);
  return result;
}

String MainStateMachine::mqtt_get_will_topic_utf8() {
  return mqtt_get_out_topic_utf8(1, GCN_MQTT_BROKER_WILL_TOPIC);
}

String MainStateMachine::mqtt_get_client_id_utf8() {
  String mac = String(WiFi.macAddress());
  std::replace(mac.begin(), mac.end(), ':', '_');
  return String(GCN_MQTT_BROKER_APP_TOPIC) + String("-") + mac;
}

bool MainStateMachine::is_mqtt_connected() {
  // http://pubsubclient.knolleary.net/api#state @ 2025-06-06
  // -4 : MQTT_CONNECTION_TIMEOUT - the server didn't respond within the keepalive time
  // -3 : MQTT_CONNECTION_LOST - the network connection was broken
  // -2 : MQTT_CONNECT_FAILED - the network connection failed
  // -1 : MQTT_DISCONNECTED - the client is disconnected cleanly
  // 0 : MQTT_CONNECTED - the client is connected
  // 1 : MQTT_CONNECT_BAD_PROTOCOL - the server doesn't support the requested version of MQTT
  // 2 : MQTT_CONNECT_BAD_CLIENT_ID - the server rejected the client identifier
  // 3 : MQTT_CONNECT_UNAVAILABLE - the server was unable to accept the connection
  // 4 : MQTT_CONNECT_BAD_CREDENTIALS - the username/password were rejected
  // 5 : MQTT_CONNECT_UNAUTHORIZED - the client was not authorized to connect
  bool connected = mqtt_client.connected();
#ifdef GCN_DEBUG_MQTT_STATUS_CHANGES
  int state = mqtt_client.state();
  if (last_mqtt_connected != connected || last_mqtt_state != state) {
    last_mqtt_connected = connected;
    last_mqtt_state = state;
    print_millis();
    Serial.print("MQTT change : connected ");
    Serial.print(connected ? "YES" : "NO");
    Serial.print(" state ");
    Serial.print(state);
    Serial.println();
  }
#endif  // GCN_DEBUG_MQTT_STATUS_CHANGES
  return connected;
}

void mqtt_publish_log_serial(const char *start, const char *topic_utf8, const char *message, bool retain) {
  print_millis();
  Serial.print(start);
  Serial.print(retain ? "retained " : "");
  Serial.print("message to topic ");
  Serial.print(topic_utf8);
  Serial.print(" with text message ");
  Serial.println(message);
}

bool MainStateMachine::mqtt_publish_topic_string(const char *topic_utf8, const char *message, bool retain) {
  bool success = mqtt_client.publish(topic_utf8, message, retain);
  if (!success) {
    mqtt_publish_log_serial("[ERROR] Could not publish ", topic_utf8, message, retain);
  } else {
#ifdef GCN_DEBUG_MQTT_PUBLISH
    mqtt_publish_log_serial("Published ", topic_utf8, message, retain);
#endif  // GCN_DEBUG_MQTT_PUBLISH
  }
  return success;
}

void mqtt_subscribe_log_serial(const char *start, const char *topic_utf8, int qos) {
  print_millis();
  Serial.print(start);
  Serial.print(qos);
  Serial.print(" topic ");
  Serial.println(topic_utf8);
}

bool MainStateMachine::mqtt_subscribe_topic(const char *topic_utf8, int qos) {
  bool success = mqtt_client.subscribe(topic_utf8, qos);
  if (!success) {
    mqtt_subscribe_log_serial("[ERROR] Could not subscribe to ", topic_utf8, qos);
  } else {
#ifdef GCN_DEBUG_MQTT_SUBSCRIBE
    mqtt_subscribe_log_serial("Subscribed to ", topic_utf8, qos);
#endif  // GCN_DEBUG_MQTT_SUBSCRIBE
  }
  return success;
}

void MainStateMachine::state_mqtt_connected_enter() {
  light_state_machine.permanent_on();
  print_millis();
  Serial.print("MQTT connection established in ");
  Serial.print(millis() - last_mqtt_begin_ms);
  Serial.println(" ms");

#ifdef GCN_COMMAND_SYNCHRONIZE_SNTP
  mqtt_subscribe_topic(mqtt_get_in_topic_utf8(1, GCN_COMMAND_SYNCHRONIZE_SNTP).c_str(), 1);
#endif  // GCN_COMMAND_SYNCHRONIZE_SNTP

#ifdef GCN_COMMAND_DISCONNECT_MQTT
  mqtt_subscribe_topic(mqtt_get_in_topic_utf8(1, GCN_COMMAND_DISCONNECT_MQTT).c_str(), 1);
#endif  // GCN_COMMAND_DISCONNECT_MQTT

#ifdef GCN_COMMAND_DISCONNECT_WIFI
  mqtt_subscribe_topic(mqtt_get_in_topic_utf8(1, GCN_COMMAND_DISCONNECT_WIFI).c_str(), 1);
#endif  // GCN_COMMAND_DISCONNECT_WIFI

#ifdef GCN_COMMAND_REBOOT
  mqtt_subscribe_topic(mqtt_get_in_topic_utf8(1, GCN_COMMAND_REBOOT).c_str(), 1);
#endif  // GCN_COMMAND_REBOOT

  // TODO: publish metadata (manufacturer, chip, max_ram, freq, etc) as persistent
}

void MainStateMachine::state_mqtt_connected_task() {
  // Flushing all monitors to MQTT once per loop (only if they have anything stored)
  // This means at most N messages (1 per monitored pin) are sent to the broker each time
  InterruptGpioMonitors::mqtt_flush_all_once(*this);

  // TODO: publish uptime, wifi uptime, mqtt uptime, sntp timestamp, ... periodically
}

void MainStateMachine::mqtt_flush_monitor_once(InterruptGpioMonitor &monitor) {
  // treat this as a critical section
  uint32_t timestamp;
  uint8_t bit;
  noInterrupts();
  bool found = monitor.pop_back(timestamp, bit);
  interrupts();

  // return to normal concurrency
  if (!found) {
    return;
  }

  // send sample to broker
  String pin_topic = mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_GPIO_MESSAGE, monitor.gpio_name.c_str());
  char buf[sizeof("0 4294967295")];  // bit is guaranteed to be 0 or 1
  snprintf(buf, sizeof(buf), "%i %lu", bit, timestamp);
  mqtt_publish_topic_string(pin_topic.c_str(), buf, true);
}

void setup_mqtt() {
  print_millis();
  Serial.print("Connection to MQTT server will be ");
  Serial.println(GCN_MQTT_BROKER_IS_SECURE ? "secured by SSL/TLS" : "in clear text (insecure)");
  print_millis();
  Serial.print("Will use MQTT server ");
  Serial.print(GCN_MQTT_BROKER_DNS_NAME);
  Serial.print(" port ");
  Serial.println(GCN_MQTT_BROKER_TCP_PORT);
}
