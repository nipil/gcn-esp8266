// GPIO CHANGE NOTIFIER (C) NIPIL 2025+

MainStateMachine::MainStateMachine(PubSubClient &mqtt_client, LightStateMachine &light_state_machine)
  : mqtt_client(mqtt_client), light_state_machine(light_state_machine), main_state(MAIN_STATE_BOOT),
    mqtt_client_id_utf8(get_client_id_utf8()), mqtt_will_topic_utf8(get_will_topic_utf8()) {
}

void MainStateMachine::run_enters() {
  switch (main_state) {
    case MAIN_STATE_ERROR:
      state_error_enter();
      break;
    case MAIN_STATE_REBOOT:
      state_reboot_enter();
      break;
    case MAIN_STATE_BOOT:
      Serial.println("Invalid state boot enter");
      set_state(MAIN_STATE_ERROR);
      break;
    case MAIN_STATE_WIFI_NOT_CONNECTED:
      state_wifi_not_connected_enter();
      break;
    case MAIN_STATE_WIFI_TRY_CONFIG:
      state_wifi_try_config_enter();
      break;
    case MAIN_STATE_WIFI_CONNECTED:
      state_wifi_connected_enter();
      break;
    case MAIN_STATE_MQTT_CONNECTED:
      state_mqtt_connected_enter();
      break;
    default:
      state_default_enter();
      break;
  }
}

void MainStateMachine::run_tasks() {
  switch (main_state) {
    case MAIN_STATE_ERROR:
      state_error_task();
      break;
    case MAIN_STATE_REBOOT:
      state_reboot_task();
      break;
    case MAIN_STATE_BOOT:
      state_boot_task();
      break;
    case MAIN_STATE_WIFI_NOT_CONNECTED:
      state_wifi_not_connected_task();
      break;
    case MAIN_STATE_WIFI_TRY_CONFIG:
      state_wifi_try_config_task();
      break;
    case MAIN_STATE_WIFI_CONNECTED:
      state_wifi_connected_task();
      break;
    case MAIN_STATE_MQTT_CONNECTED:
      state_mqtt_connected_task();
      break;
    default:
      state_default_task();
      break;
  }
}

void MainStateMachine::set_state(const MainState new_state) {
#ifdef GCN_DEBUG_MAIN_STATE_MACHINE
  Serial.print("Switching main_state to ");
  Serial.println(new_state);
#endif  // GCN_DEBUG_MAIN_STATE_MACHINE
  main_state = new_state;
  run_enters();
}

void MainStateMachine::state_error_enter() {
  error_detected_millis = millis();
  light_state_machine.start(1);
  Serial.print("Unrecoverable error state reached, rebooting in ");
  Serial.print(GCN_ERROR_WAITING_BEFORE_REBOOT_MS);
  Serial.println(" ms");
}

void MainStateMachine::state_error_task() {
  if (millis() - error_detected_millis > GCN_ERROR_WAITING_BEFORE_REBOOT_MS) {
    set_state(MAIN_STATE_REBOOT);
  }
}

void MainStateMachine::state_reboot_enter() {
  Serial.print("Rebooting.");
}

void MainStateMachine::state_reboot_task() {
  ESP.restart();
}

void MainStateMachine::state_boot_task() {
  Serial.print("MQTT client-id ");
  Serial.print(mqtt_client_id_utf8.c_str());
  Serial.print("MQTT will topic ");
  Serial.print(mqtt_will_topic_utf8.c_str());
  Serial.print(" qos ");
  Serial.print(GCN_MQTT_BROKER_WILL_QOS);
  Serial.print(" retain ");
  Serial.print(GCN_MQTT_BROKER_WILL_RETAIN ? "yes" : "NO");
  Serial.print(" message ");
  Serial.println(GCN_MQTT_BROKER_WILL_MESSAGE);
  set_state(MAIN_STATE_WIFI_NOT_CONNECTED);
}

void MainStateMachine::state_default_enter() {
  Serial.print("Invalid main state ");
  Serial.println(main_state);
}

void MainStateMachine::state_default_task() {
  set_state(MAIN_STATE_ERROR);
}

void MainStateMachine::update() {
  run_tasks();
  light_state_machine.update();

  // keepalives
  if (is_mqtt_connected()) {
    mqtt_client.loop();
  }

  // watchdogs
#ifdef GCN_PERIODIC_REBOOT_AFTER_MS
  if (millis() > GCN_PERIODIC_REBOOT_AFTER_MS) {
    Serial.print("Maximum running time reached ");
    Serial.print(GCN_PERIODIC_REBOOT_AFTER_MS);
    Serial.println(" ms");
    set_state(MAIN_STATE_REBOOT);
    return;
  }
#endif  // GCN_PERIODIC_REBOOT_AFTER_MS
  if (!is_wifi_connected() && main_state >= MAIN_STATE_WIFI_CONNECTED) {
    Serial.println("Detected WiFi disconnection");
    set_state(MAIN_STATE_WIFI_NOT_CONNECTED);
    return;
  }
  if (!is_mqtt_connected() && main_state >= MAIN_STATE_MQTT_CONNECTED) {
    Serial.println("Detected MQTT disconnection");
    set_state(MAIN_STATE_WIFI_CONNECTED);
    return;
  }
}
