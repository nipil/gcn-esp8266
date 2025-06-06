// GPIO CHANGE NOTIFIER (C) NIPIL 2025+

void main_state_machine_mqtt_callback(char *topic_utf8, byte *payload, unsigned int length) {
#ifdef GCN_DEBUG_MQTT_SUBSCRIBE
  Serial.print("MQTT recv ");
  Serial.print(length);
  Serial.print(" bytes topic ");
  Serial.println(topic_utf8);
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
#endif  // GCN_DEBUG_MQTT_SUBSCRIBE
  main_state_machine.mqtt_callback(topic_utf8, payload, length);
}

void MainStateMachine::mqtt_callback(char *topic_utf8, byte *payload, unsigned int length) {
  // TODO
}

String MainStateMachine::get_will_topic_utf8() {
  return String(GCN_MQTT_BROKER_APP_TOPIC) + String("/") + String(WiFi.macAddress()) + String("/") + String(GCN_MQTT_BROKER_WILL_SUBTOPIC);
}

String MainStateMachine::get_client_id_utf8() {
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
  if (mqtt_connected_last_value != connected || mqtt_connected_last_state != state) {
    mqtt_connected_last_value = connected;
    mqtt_connected_last_state = state;
    Serial.print("MQTT change : connected ");
    Serial.print(connected ? "YES" : "NO");
    Serial.print(" state ");
    Serial.print(state);
    Serial.print(" at ");
    Serial.print(millis());
    Serial.println(" ms");
  }
#endif  // GCN_DEBUG_MQTT_STATUS_CHANGES
  return connected;
}

bool MainStateMachine::publish_string(const String &topic_utf8, const String &message, bool retain) {
  bool result = mqtt_client.publish(topic_utf8.c_str(), message.c_str(), retain);
#ifdef GCN_DEBUG_MQTT_PUBLISH
  Serial.print("MQTT publish ");
  Serial.print(result ? "OK" : "FAIL");
  Serial.print(" retain ");
  Serial.print(retain ? "YES" : "NO");
  Serial.print(" topic ");
  Serial.print(topic_utf8.c_str());
  Serial.print(" message ");
  Serial.println(message.c_str());
#endif  // GCN_DEBUG_MQTT_PUBLISH
  return result;
}

void MainStateMachine::state_mqtt_connected_enter() {
  light_state_machine.permanent_on();
  Serial.print("MQTT connection established in ");
  Serial.print(millis() - mqtt_connecting_start_millis);
  Serial.println(" ms");

  mqtt_last_status_display = millis();

  // TODO: subscribe to gcn/%u/in/command
}

void MainStateMachine::state_mqtt_connected_task() {
  if (millis() - mqtt_last_status_display > GCN_MQTT_STATUS_INTERVAL_MS) {
    mqtt_last_status_display = millis();
    Serial.print("MQTT connection active for ");
    Serial.print((millis() - mqtt_connecting_start_millis) / 1000UL);
    Serial.println(" secs");
  }

  // TODO: read initial input states
  // TODO: read current input stats
  // TODO: compute changes
  // TODO: store current as initial
  // TODO: publish changes in topics gcn/%u/out/pins/i with retain ?
}
