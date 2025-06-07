// GPIO CHANGE NOTIFIER (C) NIPIL 2025+

void main_state_machine_mqtt_callback(char *topic_utf8, byte *payload, unsigned int length) {
#ifdef GCN_DEBUG_MQTT_SUBSCRIBE
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
#endif  // GCN_DEBUG_MQTT_SUBSCRIBE
  main_state_machine.mqtt_callback(topic_utf8, payload, length);
}

void MainStateMachine::mqtt_callback(char *topic_utf8, byte *payload, unsigned int length) {
  String topic(topic_utf8);

#ifdef GCN_COMMAND_SYNCHRONIZE_SNTP
  if (topic == mqtt_get_sub_topic_utf8(GCN_MQTT_BROKER_IN_TOPIC, GCN_COMMAND_SYNCHRONIZE_SNTP)) {
    print_millis();
    Serial.println("Received 'synchronize SNTP' command");
    sntp_resynchronize();
    return;
  }
#endif  // GCN_COMMAND_SYNCHRONIZE_SNTP

#ifdef GCN_COMMAND_DISCONNECT_MQTT
  if (topic == mqtt_get_sub_topic_utf8(GCN_MQTT_BROKER_IN_TOPIC, GCN_COMMAND_DISCONNECT_MQTT)) {
    print_millis();
    Serial.println("Received 'disconnect MQTT' command");
    mqtt_client.disconnect();
    set_state(MAIN_STATE_SNTP_CONNECTED);
    return;
  }
#endif  // GCN_COMMAND_DISCONNECT_MQTT

#ifdef GCN_COMMAND_DISCONNECT_WIFI
  if (topic == mqtt_get_sub_topic_utf8(GCN_MQTT_BROKER_IN_TOPIC, GCN_COMMAND_DISCONNECT_WIFI)) {
    print_millis();
    Serial.println("Received 'disconnect wifi' command");
    mqtt_client.disconnect();
    WiFi.disconnect();
    set_state(MAIN_STATE_WIFI_NOT_CONNECTED);
    return;
  }
#endif  // GCN_COMMAND_DISCONNECT_WIFI

#ifdef GCN_COMMAND_REBOOT
  if (topic == mqtt_get_sub_topic_utf8(GCN_MQTT_BROKER_IN_TOPIC, GCN_COMMAND_REBOOT)) {
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

String mqtt_get_sub_topic_utf8(const char *topic_direction, const char *sub_topic) {
  return String(GCN_MQTT_BROKER_APP_TOPIC) + String("/") + String(WiFi.macAddress()) + String("/") + String(topic_direction) + String("/") + String(sub_topic);
}

String MainStateMachine::mqtt_get_will_topic_utf8() {
  return mqtt_get_sub_topic_utf8(GCN_MQTT_BROKER_OUT_TOPIC, GCN_MQTT_BROKER_WILL_TOPIC);
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

bool MainStateMachine::mqtt_publish_topic_string(const String &topic_utf8, const String &message, bool retain) {
  bool result = mqtt_client.publish(topic_utf8.c_str(), message.c_str(), retain);
#ifdef GCN_DEBUG_MQTT_PUBLISH
  print_millis();
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

bool MainStateMachine::mqtt_subscribe_topic(const String &topic_utf8, int qos) {
  const char *topic = topic_utf8.c_str();
  bool result = mqtt_client.subscribe(topic, qos);
  print_millis();
  Serial.print("MQTT subscribing to ");
  Serial.print(topic);
  Serial.print(" qos ");
  Serial.print(qos);
  Serial.print(" result ");
  Serial.println(result ? "OK" : "FAIL");
  return result;
}

void MainStateMachine::state_mqtt_connected_enter() {
  light_state_machine.permanent_on();
  print_millis();
  Serial.print("MQTT connection established in ");
  Serial.print(millis() - last_mqtt_begin_ms);
  Serial.println(" ms");
  mqtt_subscribe_topic(mqtt_get_sub_topic_utf8(GCN_MQTT_BROKER_IN_TOPIC, GCN_COMMAND_SYNCHRONIZE_SNTP), 1);
  mqtt_subscribe_topic(mqtt_get_sub_topic_utf8(GCN_MQTT_BROKER_IN_TOPIC, GCN_COMMAND_DISCONNECT_MQTT), 1);
  mqtt_subscribe_topic(mqtt_get_sub_topic_utf8(GCN_MQTT_BROKER_IN_TOPIC, GCN_COMMAND_DISCONNECT_WIFI), 1);
  mqtt_subscribe_topic(mqtt_get_sub_topic_utf8(GCN_MQTT_BROKER_IN_TOPIC, GCN_COMMAND_REBOOT), 1);
}

void MainStateMachine::state_mqtt_connected_task() {

  // TODO: read initial input states
  // TODO: read current input stats
  // TODO: compute changes
  // TODO: store current as initial
  // TODO: publish changes in topics gcn/%u/out/pins/i with retain ?
}
