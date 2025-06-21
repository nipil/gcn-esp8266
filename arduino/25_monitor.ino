// GPIO CHANGE NOTIFIER (C) NIPIL 2025+

bool PinDebouncer::get_last_stable_value() {
  return last_stable_value;
}

SinglePinTimeDebouncer::SinglePinTimeDebouncer(const char* gpio_name, const uint8_t gpio_number)
  : gpio_name(gpio_name), gpio_number(gpio_number) {
}

bool SinglePinTimeDebouncer::update(bool& new_value) {
  bool current_value = digitalRead(gpio_number);

#ifdef GCN_DEBUG_DEBOUNCER_SINGLE
  print_millis();
  Serial.print("SingleDebouncer ");
  Serial.print(current_value);
  Serial.print(" ");
#endif  // GCN_DEBUG_DEBOUNCER_SINGLE

  if (current_value == last_stable_value) {
#ifdef GCN_DEBUG_DEBOUNCER_SINGLE
    Serial.println("same");
#endif  // GCN_DEBUG_DEBOUNCER_SINGLE
    return false;
  }

  last_stable_value = current_value;
  unsigned long now_ms = millis();
  if (now_ms - last_change_ms < GCN_CHANGE_BUFFER_DEBOUNCE_MS) {
#ifdef GCN_DEBUG_DEBOUNCER_SINGLE
    Serial.println("unstable");
#endif  // GCN_DEBUG_DEBOUNCER_SINGLE
    return false;
  }

  last_change_ms = now_ms;
#ifdef GCN_DEBUG_DEBOUNCER_SINGLE
  Serial.println(last_change_ms);
#endif  // GCN_DEBUG_DEBOUNCER_SINGLE
  new_value = current_value;
  return true;
}

void SinglePinTimeDebouncer::setup(InterruptServiceRoutine callback) {
  Serial.print("Configure 'change' interrupt for single pin time debouncer : name=");
  Serial.print(gpio_name);
  Serial.print(" number=");
  Serial.println(gpio_number);
  // TODO: make pullup configurable ?
  pinMode(gpio_number, INPUT_PULLUP);
  last_stable_value = digitalRead(gpio_number);
  last_change_ms = millis();
  attachInterrupt(digitalPinToInterrupt(gpio_number), callback, CHANGE);
}

DualPinComplementDebouncer::DualPinComplementDebouncer(const char* gpio_name, const uint8_t gpio_number, const char* gpio_name_inverted, uint8_t gpio_number_inverted)
  : gpio_name(gpio_name), gpio_number(gpio_number), gpio_name_inverted(gpio_name_inverted), gpio_number_inverted(gpio_number_inverted) {
}

bool DualPinComplementDebouncer::update(bool& new_value) {
  bool current_value = digitalRead(gpio_number);
  bool current_value_inverted = digitalRead(gpio_number_inverted);

#ifdef GCN_DEBUG_DEBOUNCER_DUAL
  print_millis();
  Serial.print("DualDebouncer ");
  Serial.print(current_value);
  Serial.print(" ");
  Serial.print(current_value_inverted);
  Serial.print(" ");
#endif  // GCN_DEBUG_DEBOUNCER_DUAL

  // invalid state, ignore
  if (!(current_value ^ current_value_inverted)) {
#ifdef GCN_DEBUG_DEBOUNCER_DUAL
    Serial.println("invalid");
#endif  // GCN_DEBUG_DEBOUNCER_DUAL
    return false;
  }

  if (current_value == last_stable_value) {
#ifdef GCN_DEBUG_DEBOUNCER_DUAL
    Serial.println("same");
#endif  // GCN_DEBUG_DEBOUNCER_DUAL
    return false;
  }

  new_value = last_stable_value = current_value;
#ifdef GCN_DEBUG_DEBOUNCER_DUAL
  Serial.println(new_value);
#endif  // GCN_DEBUG_DEBOUNCER_DUAL
  return true;
}

void DualPinComplementDebouncer::setup(InterruptServiceRoutine callback) {
  Serial.print("Configure 'change' interrupt for dual pin complement debouncer : normal=");
  Serial.print(gpio_name);
  Serial.print(" number=");
  Serial.print(gpio_number);
  Serial.print(" / inverted=");
  Serial.print(gpio_name_inverted);
  Serial.print(" number=");
  Serial.println(gpio_number_inverted);
  // TODO: make pullup configurable ?
  pinMode(gpio_number, INPUT_PULLUP);
  pinMode(gpio_number_inverted, INPUT_PULLUP);
  last_stable_value = digitalRead(gpio_number);
  attachInterrupt(digitalPinToInterrupt(gpio_number), callback, CHANGE);
  attachInterrupt(digitalPinToInterrupt(gpio_number_inverted), callback, CHANGE);
}

GpioChangeBuffer::GpioChangeBuffer()
  : head(0), tail(0) {
}

void GpioChangeBuffer::push_front(const uint32_t timestamp, const uint8_t bit) {
  // optimize and serialize
  uint32_t item = ((timestamp - timestamp_offset) & 0x7FFFFFFF) | ((bit & 0x01) << 31);

#ifdef GCN_DEBUG_BUFFER_PUSH
  print_millis();
  Serial.print("BufferPush bit=");
  Serial.print(bit);
  Serial.print(" timestamp=");
  Serial.print(timestamp);
  Serial.print(" item=");
  Serial.println(item);
  print();
#endif  // GCN_DEBUG_BUFFER_PUSH

  noInterrupts();

  // store
  unsigned int next = (head + 1) & mask;
  if (next == tail) {
    tail = (tail + 1) & mask;    // drop oldest element
    total_dropped_item_count++;  // count dropped elements
  }
  ring[head] = item;
  head = next;

  interrupts();
}

bool GpioChangeBuffer::pop_back(uint32_t& timestamp, uint8_t& bit) {
  // check empty
  if (head == tail) {
    return false;
  }

  noInterrupts();

  // retrieve and advance tail
  uint32_t item = ring[tail];
  tail = (tail + 1) & mask;
  // deserialize and rehydrate
  bit = (item >> 31) & 0x1;
  timestamp = (item & 0x7FFFFFFF) + timestamp_offset;

  interrupts();

#ifdef GCN_DEBUG_BUFFER_POP
  print_millis();
  Serial.print("BufferPop item=");
  Serial.print(item);
  Serial.print(" bit=");
  Serial.print(bit);
  Serial.print(" timestamp=");
  Serial.print(timestamp);
  print();
#endif  // GCN_DEBUG_BUFFER_POP

  return true;
}

void GpioChangeBuffer::print() {
  char buf[sizeof("ffffffff")];
  print_millis();
  Serial.print("ChangeBuffer STATE:");
  for (unsigned int i = 0; i <= mask; i++) {
    Serial.print(" ");
    snprintf(buf, sizeof(buf), "%08x", ring[i]);
    Serial.print(buf);
  }
  Serial.print(" head=");
  Serial.print(head);
  Serial.print(" tail=");
  Serial.println(tail);
}

uint32_t GpioChangeBuffer::get_total_dropped_item_count() {
  return total_dropped_item_count;
}

uint32_t GpioChangeBuffer::total_dropped_item_count = 0;
