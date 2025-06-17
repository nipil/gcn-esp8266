// GPIO CHANGE NOTIFIER (C) NIPIL 2025+

bool MainStateMachine::is_sntp_connected() {
  uint32 now = sntp_get_current_timestamp();
  return now > 1700000000;  // nov 2023, but any recent epoch would work !
}

void MainStateMachine::sntp_synchronize() {
  last_sntp_begin_ms = millis();
  print_millis();
  Serial.println("Triggering SNTP synchronization");
  sntp_stop();
  sntp_init();
}

void MainStateMachine::state_sntp_connected_enter() {
  last_mqtt_begin_ms = next_mqtt_retry_ms = millis();
  light_state_machine.start(3);
}

void MainStateMachine::state_sntp_connected_task() {
  unsigned long elapsed = millis() - last_mqtt_begin_ms;

  if (elapsed > GCN_MQTT_WAITING_TIMEOUT_MS) {
    print_millis();
    Serial.print("Failed to connect to MQTT after ");
    Serial.print(elapsed);
    Serial.println("ms");
    set_state(MAIN_STATE_ERROR);
    return;
  }

  if (millis() < next_mqtt_retry_ms) {
    return;  // wait more
  };

  String mqtt_will_topic_utf8 = mqtt_get_will_topic_utf8();

  bool success = mqtt_client.connect(
    mqtt_get_client_id_utf8().c_str(),
    GCN_MQTT_BROKER_USER_NAME_UTF8, GCN_MQTT_BROKER_PASSWORD,
    mqtt_will_topic_utf8.c_str(), GCN_MQTT_BROKER_WILL_QOS, GCN_MQTT_BROKER_WILL_RETAIN, GCN_MQTT_BROKER_WILL_MESSAGE,
    GCN_MQTT_BROKER_CLEAN_SESSION);

  if (!success) {
    mqtt_count_connect_error++;
    next_mqtt_retry_ms = millis() + GCN_MQTT_BROKER_RETRY_MS;
    print_millis();
    Serial.print("[ERROR] Could not connect to MQTT broker (client state ");
    Serial.print(mqtt_client.state());
    Serial.print(") ... Retrying in ");
    Serial.print(GCN_MQTT_BROKER_RETRY_MS);
    Serial.println("ms");
    return;
  } else {
    mqtt_count_connect_ok++;
  }

  print_millis();
  Serial.println("Successfully connected to MQTT broker");
  // inform the managing app that we are actually online
  mqtt_publish_topic_string(mqtt_will_topic_utf8.c_str(), GCN_MQTT_BROKER_BORN_MESSAGE, true);

  print_millis();
  Serial.print("Provide monitored pins inventory for consumers: ");
  Serial.println(MONITORED_GPIO_MESSAGE);
  // publish the list of monitored pins, so that the managing app can cleanup obsolete persistant gpio messages
  mqtt_publish_topic_string(mqtt_get_out_topic_utf8(1, GCN_MQTT_BROKER_MONITORED_GPIO).c_str(), MONITORED_GPIO_MESSAGE, true);

  // do not wait for the first change, inform about our state right away
  mqtt_queue_initial_values();

  set_state(MAIN_STATE_MQTT_CONNECTED);
}

void setup_sntp() {
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
}
