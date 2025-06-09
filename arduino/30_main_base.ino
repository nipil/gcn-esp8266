// GPIO CHANGE NOTIFIER (C) NIPIL 2025+

MainStateMachine::MainStateMachine(PubSubClient &mqtt_client, LightStateMachine &light_state_machine)
  : mqtt_client(mqtt_client), light_state_machine(light_state_machine), main_state(MAIN_STATE_BOOT) {
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
    case MAIN_STATE_SNTP_CONNECTED:
      state_sntp_connected_enter();
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
    case MAIN_STATE_SNTP_CONNECTED:
      state_sntp_connected_task();
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
  print_millis();
  Serial.print("Switching main_state from ");
  Serial.print(main_state);
  Serial.print(" to ");
  Serial.println(new_state);
#endif  // GCN_DEBUG_MAIN_STATE_MACHINE
  main_state = new_state;
  run_enters();
}

void MainStateMachine::state_error_enter() {
  light_state_machine.start(1);
  print_millis();
  Serial.println("Unrecoverable error state reached");
}

void MainStateMachine::state_error_task() {
  set_state(MAIN_STATE_REBOOT);
}

void MainStateMachine::state_reboot_enter() {
  print_millis();
  Serial.println("Rebooting");
}

void MainStateMachine::state_reboot_task() {
  mqtt_client.disconnect();
  WiFi.disconnect();
  delay(1000);
  ESP.restart();
}

void MainStateMachine::state_boot_task() {
  set_state(MAIN_STATE_WIFI_NOT_CONNECTED);
}

void MainStateMachine::state_default_enter() {
  print_millis();
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

  // Periodic tasks
#ifdef GCN_SNTP_RESYNCHRONIZE_INTERVAL_MINUTE
  if (millis() - last_sntp_begin_ms > GCN_SNTP_RESYNCHRONIZE_INTERVAL_MINUTE * 60 * 1000 && main_state >= MAIN_STATE_WIFI_CONNECTED) {
    sntp_synchronize();
  }
#endif  // GCN_SNTP_RESYNCHRONIZE_INTERVAL_MINUTE

  // watchdogs
#ifdef GCN_PERIODIC_REBOOT_AFTER_MS
  if (millis() > GCN_PERIODIC_REBOOT_AFTER_MS) {
    print_millis();
    Serial.println("Maximum running time reached");
    set_state(MAIN_STATE_REBOOT);
    return;
  }
#endif  // GCN_PERIODIC_REBOOT_AFTER_MS
  if (!is_wifi_connected() && main_state >= MAIN_STATE_WIFI_CONNECTED) {
    print_millis();
    Serial.println("Detected WiFi disconnection");
    set_state(MAIN_STATE_WIFI_NOT_CONNECTED);
    return;
  }
  if (!is_mqtt_connected() && main_state >= MAIN_STATE_MQTT_CONNECTED) {
    print_millis();
    Serial.println("Detected MQTT disconnection");
    set_state(MAIN_STATE_WIFI_CONNECTED);
    return;
  }
}

void MainStateMachine::setup() {
  light_state_machine.setup();
  print_millis();
  Serial.print("MQTT client-id ");
  Serial.println(mqtt_get_client_id_utf8().c_str());
  print_millis();
  Serial.print("MQTT will topic ");
  Serial.print(mqtt_get_will_topic_utf8().c_str());
  Serial.print(" qos ");
  Serial.print(GCN_MQTT_BROKER_WILL_QOS);
  Serial.print(" retain ");
  Serial.print(GCN_MQTT_BROKER_WILL_RETAIN ? "yes" : "NO");
  Serial.print(" message=");
  Serial.println(GCN_MQTT_BROKER_WILL_MESSAGE);
}
