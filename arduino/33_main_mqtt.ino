// GPIO CHANGE NOTIFIER (C) NIPIL 2025+

void main_state_machine_mqtt_callback(char *topic_utf8, byte *payload, unsigned int length) {
  main_state_machine.mqtt_callback(topic_utf8, payload, length);
}

void debug_payload(byte *payload, unsigned int length) {
  for (unsigned int i = 0; i < length; i++) {
    Serial.print(payload[i], HEX);
  }
  Serial.println();
  for (unsigned int i = 0; i < length; i++) {
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

void MainStateMachine::mqtt_callback(char *topic_utf8, byte *payload, unsigned int length) {
  mqtt_count_received++;
#ifdef GCN_DEBUG_MQTT_RECEIVED
  print_millis();
  Serial.print("MQTT recv ");
  Serial.print(length);
  Serial.print(" bytes topic ");
  Serial.println(topic_utf8);
  if (length > 0) {
    debug_payload(payload, length);
  }
#endif  // GCN_DEBUG_MQTT_RECEIVED

  String topic(topic_utf8);

#ifdef GCN_COMMAND_SYNCHRONIZE_SNTP
  if (topic == mqtt_get_in_topic_utf8(1, GCN_COMMAND_SYNCHRONIZE_SNTP) && length > 0) {
    print_millis();
    Serial.println("Received 'synchronize SNTP' command");
    sntp_synchronize();
    return;
  }
#endif  // GCN_COMMAND_SYNCHRONIZE_SNTP

#ifdef GCN_COMMAND_DISCONNECT_MQTT
  if (topic == mqtt_get_in_topic_utf8(1, GCN_COMMAND_DISCONNECT_MQTT) && length > 0) {
    print_millis();
    Serial.println("Received 'disconnect MQTT' command");
    mqtt_disconnect_properly();
    set_state(MAIN_STATE_SNTP_CONNECTED);
    return;
  }
#endif  // GCN_COMMAND_DISCONNECT_MQTT

#ifdef GCN_COMMAND_DISCONNECT_WIFI
  if (topic == mqtt_get_in_topic_utf8(1, GCN_COMMAND_DISCONNECT_WIFI) && length > 0) {
    print_millis();
    Serial.println("Received 'disconnect wifi' command");
    mqtt_disconnect_properly();
    WiFi.disconnect();
    set_state(MAIN_STATE_WIFI_NOT_CONNECTED);
    return;
  }
#endif  // GCN_COMMAND_DISCONNECT_WIFI

#ifdef GCN_COMMAND_REBOOT
  if (topic == mqtt_get_in_topic_utf8(1, GCN_COMMAND_REBOOT) && length > 0) {
    print_millis();
    Serial.println("Received 'reboot' command");
    mqtt_disconnect_properly();
    WiFi.disconnect();
    set_state(MAIN_STATE_REBOOT);
    return;
  }
#endif  // GCN_COMMAND_REBOOT

#ifdef GCN_COMMAND_SEND_METRICS
  if (topic == mqtt_get_in_topic_utf8(1, GCN_COMMAND_SEND_METRICS) && length > 0) {
    print_millis();
    Serial.println("Received 'send metrics' command");
    send_metrics();
    return;
  }
#endif  // GCN_COMMAND_SEND_METRICS

  print_millis();
  Serial.print("Ignoring unknown input : ");
  Serial.print(" length ");
  Serial.print(length);
  if (length > 0) {
    Serial.print("1st_hex_byte ");
    Serial.print(payload[0], HEX);
  }
  Serial.print(" topic ");
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
    mqtt_count_sent_error++;
    mqtt_publish_log_serial("[ERROR] Could not publish ", topic_utf8, message, retain);
  } else {
    mqtt_count_sent_ok++;
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
    mqtt_count_subscribe_error++;
    mqtt_subscribe_log_serial("[ERROR] Could not subscribe to ", topic_utf8, qos);
  } else {
    mqtt_count_subscribe_ok++;
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

#ifdef GCN_COMMAND_SEND_METRICS
  mqtt_subscribe_topic(mqtt_get_in_topic_utf8(1, GCN_COMMAND_SEND_METRICS).c_str(), 1);
#endif  // GCN_COMMAND_SEND_METRICS

  // publish hardware metadata as persistent
  mqtt_publish_hardware_constants();
}

bool MainStateMachine::mqtt_publish_hardware_constants() {
  bool success = true;
  print_millis();
  Serial.println("Displaying and sending " GCN_MQTT_BROKER_HW_TOPIC "=" GCN_MQTT_BROKER_HW_VALUE " specific information");

  success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(1, GCN_MQTT_BROKER_HW_TOPIC).c_str(), GCN_MQTT_BROKER_HW_VALUE, true);

#ifdef GCN_MQTT_BROKER_ESP8266_TOPIC
  if (strcmp(GCN_MQTT_BROKER_HW_VALUE, GCN_MQTT_BROKER_ESP8266_TOPIC) == 0) {
    char buf[sizeof("4294967295")];  // uint32_t

    String tmp_s = ESP.getResetReason();
    Serial.print("\tLast reset reason : ");
    Serial.println(tmp_s.c_str());
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_ESP8266_TOPIC, GCN_MQTT_BROKER_ESP8266_TOPIC_RESET_REASON).c_str(), tmp_s.c_str(), true);

    uint32_t tmp_lu = ESP.getChipId();
    snprintf(buf, sizeof(buf), "%u", tmp_lu);
    Serial.print("\tChip ID : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_ESP8266_TOPIC, GCN_MQTT_BROKER_ESP8266_TOPIC_CHIP_ID).c_str(), buf, true);

    tmp_s = ESP.getCoreVersion();
    Serial.print("\tCore version : ");
    Serial.println(tmp_s.c_str());
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_ESP8266_TOPIC, GCN_MQTT_BROKER_ESP8266_TOPIC_CORE_VERSION).c_str(), tmp_s.c_str(), true);

    const char *tmp_c = ESP.getSdkVersion();
    Serial.print("\tSDK version : ");
    Serial.println(tmp_c);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_ESP8266_TOPIC, GCN_MQTT_BROKER_ESP8266_TOPIC_SDK_VERSION).c_str(), tmp_c, true);

    uint8_t tmp_byte = ESP.getCpuFreqMHz();
    snprintf(buf, sizeof(buf), "%u", tmp_byte);
    Serial.print("\tCPU Frequency MHz : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_ESP8266_TOPIC, GCN_MQTT_BROKER_ESP8266_TOPIC_CPU_FREQ_MHZ).c_str(), buf, true);

    tmp_lu = ESP.getSketchSize();
    snprintf(buf, sizeof(buf), "%u", tmp_lu);
    Serial.print("\tSketch size : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_ESP8266_TOPIC, GCN_MQTT_BROKER_ESP8266_TOPIC_SKETCH_SIZE).c_str(), buf, true);

    tmp_lu = ESP.getFreeSketchSpace();
    snprintf(buf, sizeof(buf), "%u", tmp_lu);
    Serial.print("\tFree sketch size : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_ESP8266_TOPIC, GCN_MQTT_BROKER_ESP8266_TOPIC_FREE_SKETCH_SIZE).c_str(), buf, true);

    tmp_s = ESP.getSketchMD5();
    Serial.print("\tSketch MD5 : ");
    Serial.println(tmp_s.c_str());
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_ESP8266_TOPIC, GCN_MQTT_BROKER_ESP8266_TOPIC_SKETCH_MD5).c_str(), tmp_s.c_str(), true);

    tmp_lu = ESP.getFlashChipId();
    snprintf(buf, sizeof(buf), "%u", tmp_lu);
    Serial.print("\tFlash chip ID : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_ESP8266_TOPIC, GCN_MQTT_BROKER_ESP8266_TOPIC_FLASH_CHIP_ID).c_str(), buf, true);

    tmp_lu = ESP.getFlashChipSize();
    snprintf(buf, sizeof(buf), "%u", tmp_lu);
    Serial.print("\tFlash chip size : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_ESP8266_TOPIC, GCN_MQTT_BROKER_ESP8266_TOPIC_FLASH_CHIP_SIZE).c_str(), buf, true);

    tmp_lu = ESP.getFlashChipRealSize();
    snprintf(buf, sizeof(buf), "%u", tmp_lu);
    Serial.print("\tFlash chip real size : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_ESP8266_TOPIC, GCN_MQTT_BROKER_ESP8266_TOPIC_FLASH_CHIP_REAL_SIZE).c_str(), buf, true);

    tmp_lu = ESP.getFlashChipSpeed();
    snprintf(buf, sizeof(buf), "%u", tmp_lu);
    Serial.print("\tFlash chip speed Hz : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_ESP8266_TOPIC, GCN_MQTT_BROKER_ESP8266_TOPIC_FLASH_CHIP_SPEED_HZ).c_str(), buf, true);

    bool tmp_bool = ESP.checkFlashCRC();  // takes ~350 ms
    snprintf(buf, sizeof(buf), "%i", tmp_bool ? 1 : 0);
    Serial.print("\tCheck flash CRC : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_ESP8266_TOPIC, GCN_MQTT_BROKER_ESP8266_TOPIC_CHECK_FLASH_CRC).c_str(), buf, true);
  }
#endif  // GCN_MQTT_BROKER_ESP8266_TOPIC

  return success;
}

#if defined(GCN_MQTT_BROKER_PERIODIC_UPDATE_INTERVAL_MINUTE) || defined(GCN_COMMAND_SEND_METRICS)
bool MainStateMachine::send_metrics() {
  last_metrics_begin_ms = millis();
  bool success = true;
  print_millis();
  Serial.println("Sending metrics");

  char buf[sizeof("4294967295")];  // uint32_t
#ifdef GCN_MQTT_BROKER_UPTIME_TOPIC
  do {  // uptime
    uint32_t tmp_ul = millis();
    snprintf(buf, sizeof(buf), "%u", tmp_ul);
    Serial.print("\tSystem uptime ms : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_UPTIME_TOPIC, GCN_MQTT_BROKER_UPTIME_SYSTEM).c_str(), buf, true);

    tmp_ul = millis() - last_wifi_begin_ms;
    snprintf(buf, sizeof(buf), "%u", tmp_ul);
    Serial.print("\tWifi uptime ms : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_UPTIME_TOPIC, GCN_MQTT_BROKER_UPTIME_WIFI).c_str(), buf, true);

    tmp_ul = millis() - last_sntp_begin_ms;
    snprintf(buf, sizeof(buf), "%u", tmp_ul);
    Serial.print("\tSNTP uptime ms : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_UPTIME_TOPIC, GCN_MQTT_BROKER_UPTIME_SNTP).c_str(), buf, true);

    tmp_ul = millis() - last_mqtt_begin_ms;
    snprintf(buf, sizeof(buf), "%u", tmp_ul);
    Serial.print("\tMQTT connection uptime : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_UPTIME_TOPIC, GCN_MQTT_BROKER_UPTIME_MQTT).c_str(), buf, true);

    tmp_ul = time(nullptr);
    snprintf(buf, sizeof(buf), "%u", tmp_ul);
    Serial.print("\tLocal unix timestamp : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_UPTIME_TOPIC, GCN_MQTT_BROKER_UPTIME_TIMESTAMP).c_str(), buf, true);
  } while (false);
#endif  // GCN_MQTT_BROKER_UPTIME_TOPIC

#ifdef GCN_MQTT_BROKER_NETWORK_TOPIC
  do {  // WIFI
    String tmp_s = WiFi.localIP().toString();
    Serial.print("\tLocal IP : ");
    Serial.println(tmp_s.c_str());
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_NETWORK_TOPIC, GCN_MQTT_BROKER_NETWORK_LOCAL_IP).c_str(), tmp_s.c_str(), true);

    tmp_s = WiFi.subnetMask().toString();
    Serial.print("\tNetwork mask : ");
    Serial.println(tmp_s.c_str());
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_NETWORK_TOPIC, GCN_MQTT_BROKER_NETWORK_NETMASK).c_str(), tmp_s.c_str(), true);

    tmp_s = WiFi.gatewayIP().toString();
    Serial.print("\tGateway IP : ");
    Serial.println(tmp_s.c_str());
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_NETWORK_TOPIC, GCN_MQTT_BROKER_NETWORK_GATEWAY_IP).c_str(), tmp_s.c_str(), true);

    for (int i = 0; i < GCN_MQTT_BROKER_NETWORK_DNS_MAX; i++) {
      IPAddress ip = WiFi.dnsIP(i);
      if (ip.v4() == 0) {
        continue;
      }
      Serial.print("\tDNS IP ");
      Serial.print(i);
      Serial.print(" : ");
      tmp_s = ip.toString();
      Serial.println(tmp_s.c_str());
      snprintf(buf, sizeof(buf), "%u", i);
      success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(3, GCN_MQTT_BROKER_NETWORK_TOPIC, GCN_MQTT_BROKER_NETWORK_DNS, buf).c_str(), tmp_s.c_str(), true);
    }

    tmp_s = WiFi.SSID();
    Serial.print("\tWifi SSID : ");
    Serial.println(tmp_s.c_str());
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_NETWORK_TOPIC, GCN_MQTT_BROKER_NETWORK_SSID).c_str(), tmp_s.c_str(), true);

    /* IMPORTANT : do absolutely NOT implement sending WiFi.psk() as this is sensitive and private user information which we do not want to be responsible for */

    tmp_s = WiFi.BSSIDstr();
    Serial.print("\tWifi BSSID : ");
    Serial.println(tmp_s.c_str());
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_NETWORK_TOPIC, GCN_MQTT_BROKER_NETWORK_BSSID).c_str(), tmp_s.c_str(), true);

    int8_t tmp_i = WiFi.RSSI();
    snprintf(buf, sizeof(buf), "%i", tmp_i);
    Serial.print("\tWifi RSSI : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_NETWORK_TOPIC, GCN_MQTT_BROKER_NETWORK_RSSI).c_str(), buf, true);
  } while (false);
#endif  // GCN_MQTT_BROKER_NETWORK_TOPIC

#ifdef GCN_MQTT_BROKER_MQTT_TOPIC
  do {  // MQTT
    uint32_t tmp_ul = mqtt_count_sent_ok;
    snprintf(buf, sizeof(buf), "%u", tmp_ul);
    Serial.print("\tMQTT publish OK : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_MQTT_TOPIC, GCN_MQTT_BROKER_MQTT_SENT_OK).c_str(), buf, true);

    tmp_ul = mqtt_count_sent_error;
    snprintf(buf, sizeof(buf), "%u", tmp_ul);
    Serial.print("\tMQTT publish FAIL : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_MQTT_TOPIC, GCN_MQTT_BROKER_MQTT_SENT_ERROR).c_str(), buf, true);

    tmp_ul = mqtt_count_subscribe_ok;
    snprintf(buf, sizeof(buf), "%u", tmp_ul);
    Serial.print("\tMQTT subscribe OK : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_MQTT_TOPIC, GCN_MQTT_BROKER_MQTT_SUBSCRIBE_OK).c_str(), buf, true);

    tmp_ul = mqtt_count_subscribe_error;
    snprintf(buf, sizeof(buf), "%u", tmp_ul);
    Serial.print("\tMQTT subscribe FAIL : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_MQTT_TOPIC, GCN_MQTT_BROKER_MQTT_SUBSCRIBE_ERROR).c_str(), buf, true);

    tmp_ul = mqtt_count_connect_ok;
    snprintf(buf, sizeof(buf), "%u", tmp_ul);
    Serial.print("\tMQTT connect OK : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_MQTT_TOPIC, GCN_MQTT_BROKER_MQTT_CONNECT_OK).c_str(), buf, true);

    tmp_ul = mqtt_count_connect_error;
    snprintf(buf, sizeof(buf), "%u", tmp_ul);
    Serial.print("\tMQTT connect FAIL : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_MQTT_TOPIC, GCN_MQTT_BROKER_MQTT_CONNECT_ERROR).c_str(), buf, true);

    tmp_ul = mqtt_count_received;
    snprintf(buf, sizeof(buf), "%u", tmp_ul);
    Serial.print("\tMQTT received : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_MQTT_TOPIC, GCN_MQTT_BROKER_MQTT_RECEIVED).c_str(), buf, true);
  } while (false);
#endif  // GCN_MQTT_BROKER_MQTT_TOPIC

#ifdef GCN_MQTT_BROKER_ESP8266_TOPIC
  do {  // esp8266
    uint32_t tmp_ul = ESP.getFreeHeap();
    snprintf(buf, sizeof(buf), "%u", tmp_ul);
    Serial.print("\tFree heap : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_ESP8266_TOPIC, GCN_MQTT_BROKER_ESP8266_TOPIC_FREE_HEAP).c_str(), buf, true);

    uint8_t tmp_byte = ESP.getHeapFragmentation();
    snprintf(buf, sizeof(buf), "%u", tmp_byte);
    Serial.print("\tHeap fragmentation : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_ESP8266_TOPIC, GCN_MQTT_BROKER_ESP8266_TOPIC_HEAP_FRAGMENTATION_PERCENT).c_str(), buf, true);

    tmp_ul = ESP.getMaxFreeBlockSize();
    snprintf(buf, sizeof(buf), "%u", tmp_ul);
    Serial.print("\tMax free block size on the heap : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_ESP8266_TOPIC, GCN_MQTT_BROKER_ESP8266_TOPIC_MAX_FREE_BLOCK_SIZE).c_str(), buf, true);
  } while (false);
#endif  // GCN_MQTT_BROKER_ESP8266_TOPIC

#ifdef GCN_MQTT_BROKER_BUFFER_TOTAL_DROPPED_ITEM
  do {
    uint32_t tmp_ul = GpioChangeBuffer::get_total_dropped_item_count();
    snprintf(buf, sizeof(buf), "%u", tmp_ul);
    Serial.print("\tBuffer total dropped items : ");
    Serial.println(buf);
    success &= mqtt_publish_topic_string(mqtt_get_out_topic_utf8(1, GCN_MQTT_BROKER_BUFFER_TOTAL_DROPPED_ITEM).c_str(), buf, true);
  } while (false);
#endif  // GCN_MQTT_BROKER_BUFFER_TOTAL_DROPPED_ITEM

  return success;
}
#endif  // defined(GCN_MQTT_BROKER_PERIODIC_UPDATE_INTERVAL_MINUTE) || defined(GCN_COMMAND_SEND_METRICS)

void MainStateMachine::state_mqtt_connected_task() {
  // Flushing all monitors to MQTT once per loop (only if they have anything stored)
  // This means at most N messages (1 per monitored pin) are sent to the broker each time
  flush_monitors();

  // Sending heartbeats, not for MQTT session keepalive, but for detecting age of last connection
  if (millis() - last_heartbeat_ms > GCN_MQTT_BROKER_HEARTBEAT_UPDATE_INTERVAL_SECOND * 1000 || last_heartbeat_ms == 0) {
    last_heartbeat_ms = millis();
    uint32_t tmp_ul = time(nullptr);
    char buf[sizeof("4294967295")];  // uint32_t
    snprintf(buf, sizeof(buf), "%u", tmp_ul);
#ifdef GCN_DEBUG_MQTT_HEARTBEAT
    print_millis();
    Serial.print("Sending heartbeat with timestamp ");
    Serial.println(buf);
#endif  // GCN_DEBUG_MQTT_HEARTBEAT
    mqtt_publish_topic_string(mqtt_get_out_topic_utf8(1, GCN_MQTT_BROKER_HEARTBEAT_TOPIC).c_str(), buf, true);
  }

#ifdef GCN_MQTT_BROKER_PERIODIC_UPDATE_INTERVAL_MINUTE
  // Periodic metrics update
  if (millis() - last_metrics_begin_ms > GCN_MQTT_BROKER_PERIODIC_UPDATE_INTERVAL_MINUTE * 60 * 1000 || last_metrics_begin_ms == 0) {
    send_metrics();
  }
#endif  // GCN_MQTT_BROKER_PERIODIC_UPDATE_INTERVAL_MINUTE
}

void MainStateMachine::mqtt_flush_buffer_once(const char *gpio_name, GpioChangeBuffer &buffer) {
  uint32_t timestamp;
  uint8_t bit;
  bool found = buffer.pop_back(timestamp, bit);  // interrupts disabled inside
  if (!found) {
    return;
  }
  // send sample to broker
  String pin_topic = mqtt_get_out_topic_utf8(2, GCN_MQTT_BROKER_GPIO_TOPIC, gpio_name);
  char buf[sizeof("0 4294967295")];  // bit is guaranteed to be 0 or 1, timestamp is an uint32_t
  snprintf(buf, sizeof(buf), "%i %u", bit, timestamp);
  mqtt_publish_topic_string(pin_topic.c_str(), buf, true);
}

void MainStateMachine::mqtt_disconnect_properly() {
  // disconnecting properly cancels the automatic use of will message
  // so that induces more work to setup the "offline" state than doing it "uncleanly"
  // howerver this is the "correct" way to behave, so do it proper.
  // SO, deliberately send the will message, to inform that we are actually going offline !
  mqtt_publish_topic_string(mqtt_get_will_topic_utf8().c_str(), GCN_MQTT_BROKER_WILL_MESSAGE, true);
  // as sending is synchronous with this library, no need to call for loop to flush it, and we can disconnect now
  mqtt_client.disconnect();
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
