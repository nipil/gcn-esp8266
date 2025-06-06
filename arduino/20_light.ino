// GPIO CHANGE NOTIFIER (C) NIPIL 2025+

LightStateMachine::LightStateMachine(ArduinoOutputPin &output_pin, const unsigned long short_ms, const unsigned long long_ms)
  : output_pin(output_pin), short_duration_ms(short_ms), long_duration_ms(long_ms) {
  stop();
}

void LightStateMachine::set_state(const LightState new_state) {
  light_state = new_state;
  record_event_millis();
#ifdef DEBUG_LIGHT_STATE_MACHINE
  Serial.print("Light new state ");
  Serial.print(new_state);
  Serial.print(" at ");
  Serial.print(last_event_millis);
  Serial.println(" ms");
#endif  // DEBUG_LIGHT_STATE_MACHINE
}

void LightStateMachine::record_event_millis() {
  last_event_millis = millis();
}

unsigned long LightStateMachine::millis_since_last_event() {
  return millis() - last_event_millis;
}

void LightStateMachine::setup() {
  output_pin.setup();
}

void LightStateMachine::permanent_on() {
  output_pin.on();
  set_state(LIGHT_STATE_PERMANENT);
#ifdef DEBUG_LIGHT_STATE_MACHINE
  Serial.print("Starting permanent light at ");
  Serial.println(last_event_millis);
#endif  // DEBUG_LIGHT_STATE_MACHINE
}

void LightStateMachine::stop() {
  output_pin.off();
  set_state(LIGHT_STATE_IDLE);
}

void LightStateMachine::start(const int count) {
  blink_count = count;
  blink_index = 0;
  if (blink_count > 0) {
    output_pin.on();
    set_state(LIGHT_STATE_ON);
#ifdef DEBUG_LIGHT_STATE_MACHINE
    Serial.print("Starting light cycle of ");
    Serial.print(count);
    Serial.print(" at ");
    Serial.println(last_event_millis);
#endif  // DEBUG_LIGHT_STATE_MACHINE
  } else {
    stop();
#ifdef DEBUG_LIGHT_STATE_MACHINE
    Serial.print("Stopping light cycle at ");
    Serial.println(last_event_millis);
#endif  // DEBUG_LIGHT_STATE_MACHINE
  }
}

void LightStateMachine::update() {
  switch (light_state) {
    case LIGHT_STATE_IDLE:
      return;
    case LIGHT_STATE_ON:
      if (millis_since_last_event() < short_duration_ms) {
        break;  // wait more
      }
      blink_index++;
#ifdef DEBUG_LIGHT_STATE_MACHINE
      Serial.print("Light blink ");
      Serial.print(blink_index);
      Serial.print("/");
      Serial.println(blink_count);
#endif  // DEBUG_LIGHT_STATE_MACHINE
      output_pin.off();
      set_state(LIGHT_STATE_OFF);
      break;
    case LIGHT_STATE_OFF:
      if (millis_since_last_event() < short_duration_ms) {
        break;  // wait more
      }
      if (blink_index < blink_count) {
        output_pin.on();
        set_state(LIGHT_STATE_ON);
        break;
      }
      set_state(LIGHT_STATE_LONG_WAIT);
      break;
    case LIGHT_STATE_LONG_WAIT:
      if (millis_since_last_event() < long_duration_ms) {
        break;  // wait more
      }
      start(blink_count);  // restart
      break;
    case LIGHT_STATE_PERMANENT:
      break;  // stay
    default:
      Serial.print("Invalid light state ");
      Serial.println(light_state);
  }
}
