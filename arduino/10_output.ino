// GPIO CHANGE NOTIFIER (C) NIPIL 2025+

ArduinoOutputPin::ArduinoOutputPin(const int pin, const bool invert_output)
  : pin_number(pin), invert(invert_output) {
}

void ArduinoOutputPin::setup() {
  print_millis();
  Serial.print("Using digital pin number=");
  Serial.print(pin_number);
  Serial.print(" to drive the ");
  Serial.print(invert ? "inverted" : "non-inverted");
  Serial.println(" led");
  pinMode(pin_number, OUTPUT);
}

void ArduinoOutputPin::on() {
#ifdef GCN_DEBUG_ARDUINO_OUTPUT_PIN
  print_millis();
  Serial.print("Turning on ");
  Serial.print(invert ? "inverted" : "non-inverted");
  Serial.println(pin_number);
#endif  // GCN_DEBUG_ARDUINO_OUTPUT_PIN
  if (invert) {
    digitalWrite(pin_number, LOW);
  } else {
    digitalWrite(pin_number, HIGH);
  }
}

void ArduinoOutputPin::off() {
#ifdef GCN_DEBUG_ARDUINO_OUTPUT_PIN
  print_millis();
  Serial.print("Turning off ");
  Serial.print(invert ? "inverted" : "non-inverted");
  Serial.println(pin_number);
#endif  // GCN_DEBUG_ARDUINO_OUTPUT_PIN
  if (invert) {
    digitalWrite(pin_number, HIGH);
  } else {
    digitalWrite(pin_number, LOW);
  }
}
