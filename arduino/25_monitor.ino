// GPIO CHANGE NOTIFIER (C) NIPIL 2025+

InterruptGpioMonitor::InterruptGpioMonitor(const char* gpio_symbol_s, const uint8_t gpio_symbol_i)
  : gpio_name(gpio_symbol_s), gpio_number(gpio_symbol_i), head(0), tail(0) {
}

void InterruptGpioMonitor::setup() {
  print_millis();
  Serial.print("Configure 'change' interrupt for monitored pin: name=");
  Serial.print(gpio_name);
  Serial.print(" number=");
  Serial.println(gpio_number);
  pinMode(gpio_number, INPUT);
  last_value = digitalRead(gpio_number);
  last_change_ms = millis();
}

void InterruptGpioMonitor::push_front(const uint32_t timestamp, const uint8_t bit) {
  // debounce
  if (bit == last_value) {
    return;
  }
  last_value = bit;
  unsigned long now_ms = millis();
  if (now_ms - last_change_ms < GCN_MONITOR_CHANGE_QUEUE_DEBOUNCE_MS) {
    return;
  }
  last_change_ms = now_ms;

  // optimize and serialize
  uint32_t item = ((timestamp - timestamp_offset) & 0x7FFFFFFF) | ((bit & 0x01) << 31);

  // store
  unsigned int next = (head + 1) & mask;
  if (next == tail) {
    tail = (tail + 1) & mask;  // drop oldest element
  }
  ring[head] = item;
  head = next;

#ifdef GCN_DEBUG_MONITOR_PUSH
  print_millis();
  Serial.print("Monitor push_front: pin_number=");
  Serial.print(gpio_number);
  Serial.print(" bit=");
  Serial.print(bit);
  Serial.print(" timestamp=");
  Serial.print(timestamp);
  Serial.print(" item=");
  Serial.println(item);
  print();
#endif  // GCN_DEBUG_MONITOR_PUSH
}

bool InterruptGpioMonitor::pop_back(uint32_t& timestamp, uint8_t& bit) {
  // check empty
  if (head == tail) {
    return false;
  }

  // retrieve and advance tail
  uint32_t item = ring[tail];
  tail = (tail + 1) & mask;

  // deserialize and rehydrate
  bit = (item >> 31) & 0x1;
  timestamp = (item & 0x7FFFFFFF) + timestamp_offset;

#ifdef GCN_DEBUG_MONITOR_POP
  print_millis();
  Serial.print("Monitor pop_back: pin_number=");
  Serial.print(gpio_number);
  Serial.print(" bit=");
  Serial.print(bit);
  Serial.print(" timestamp=");
  Serial.print(timestamp);
  Serial.print(" item=");
  Serial.println(item);
  print();
#endif  // GCN_DEBUG_MONITOR_POP
  return true;
}

void InterruptGpioMonitor::print() {
  char buf[sizeof("ffffffff")];
  print_millis();
  Serial.print("Monitor STATE: pin_number=");
  Serial.print(gpio_number);
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

// Used with GCN_MONITORED_DIGITAL_PIN_* in main arduino.ino

#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)

#define GCN_MONITORED_DIGITAL_PIN_MONITOR_DECLARATION(GPIO_NAME) \
  static InterruptGpioMonitor gpio_changed_monitor_##GPIO_NAME

#define GCN_MONITORED_DIGITAL_PIN_MONITOR_INITIALIZATION(GPIO_NAME) \
  InterruptGpioMonitor InterruptGpioMonitors::gpio_changed_monitor_##GPIO_NAME(TO_STRING(GPIO_NAME), GPIO_NAME)

#define GCN_MONITORED_DIGITAL_PIN_SETUP(GPIO_NAME) \
  do { \
    InterruptGpioMonitors::gpio_changed_monitor_##GPIO_NAME.setup(); \
    attachInterrupt(digitalPinToInterrupt(GPIO_NAME), InterruptGpioMonitors::gpio_changed_isr_##GPIO_NAME, CHANGE); \
  } while (0);

#define GCN_MONITORED_DIGITAL_PIN_FLUSH(GPIO_NAME, TARGET) \
  do { \
    TARGET.mqtt_flush_monitor_once(InterruptGpioMonitors::gpio_changed_monitor_##GPIO_NAME); \
  } while (0);

#define GCN_MONITORED_DIGITAL_PIN_CHANGED_ISR(GPIO_NAME) \
  IRAM_ATTR static void gpio_changed_isr_##GPIO_NAME() { \
    uint8_t bit = digitalRead(GPIO_NAME); \
    uint32_t timestamp = time(nullptr); \
    InterruptGpioMonitors::gpio_changed_monitor_##GPIO_NAME.push_front(timestamp, bit); \
  }

class InterruptGpioMonitors {
public:

  static void setup() {
#ifdef GCN_MONITORED_DIGITAL_PIN_A
    GCN_MONITORED_DIGITAL_PIN_SETUP(GCN_MONITORED_DIGITAL_PIN_A);
#endif  // GCN_MONITORED_DIGITAL_PIN_A
#ifdef GCN_MONITORED_DIGITAL_PIN_B
    GCN_MONITORED_DIGITAL_PIN_SETUP(GCN_MONITORED_DIGITAL_PIN_B);
#endif  // GCN_MONITORED_DIGITAL_PIN_B
#ifdef GCN_MONITORED_DIGITAL_PIN_C
    GCN_MONITORED_DIGITAL_PIN_SETUP(GCN_MONITORED_DIGITAL_PIN_C);
#endif  // GCN_MONITORED_DIGITAL_PIN_C
#ifdef GCN_MONITORED_DIGITAL_PIN_D
    GCN_MONITORED_DIGITAL_PIN_SETUP(GCN_MONITORED_DIGITAL_PIN_D);
#endif  // GCN_MONITORED_DIGITAL_PIN_D
#ifdef GCN_MONITORED_DIGITAL_PIN_E
    GCN_MONITORED_DIGITAL_PIN_SETUP(GCN_MONITORED_DIGITAL_PIN_E);
#endif  // GCN_MONITORED_DIGITAL_PIN_E
#ifdef GCN_MONITORED_DIGITAL_PIN_F
    GCN_MONITORED_DIGITAL_PIN_SETUP(GCN_MONITORED_DIGITAL_PIN_F);
#endif  // GCN_MONITORED_DIGITAL_PIN_F
  }

  static void mqtt_flush_all_once(MainStateMachine& target) {
#ifdef GCN_MONITORED_DIGITAL_PIN_A
    GCN_MONITORED_DIGITAL_PIN_FLUSH(GCN_MONITORED_DIGITAL_PIN_A, target);
#endif  // GCN_MONITORED_DIGITAL_PIN_A
#ifdef GCN_MONITORED_DIGITAL_PIN_B
    GCN_MONITORED_DIGITAL_PIN_FLUSH(GCN_MONITORED_DIGITAL_PIN_B, target);
#endif  // GCN_MONITORED_DIGITAL_PIN_B
#ifdef GCN_MONITORED_DIGITAL_PIN_C
    GCN_MONITORED_DIGITAL_PIN_FLUSH(GCN_MONITORED_DIGITAL_PIN_C, target);
#endif  // GCN_MONITORED_DIGITAL_PIN_C
#ifdef GCN_MONITORED_DIGITAL_PIN_D
    GCN_MONITORED_DIGITAL_PIN_FLUSH(GCN_MONITORED_DIGITAL_PIN_D, target);
#endif  // GCN_MONITORED_DIGITAL_PIN_D
#ifdef GCN_MONITORED_DIGITAL_PIN_E
    GCN_MONITORED_DIGITAL_PIN_FLUSH(GCN_MONITORED_DIGITAL_PIN_E, target);
#endif  // GCN_MONITORED_DIGITAL_PIN_E
#ifdef GCN_MONITORED_DIGITAL_PIN_F
    GCN_MONITORED_DIGITAL_PIN_FLUSH(GCN_MONITORED_DIGITAL_PIN_F, target);
#endif  // GCN_MONITORED_DIGITAL_PIN_F
  }

// Generate one interrupt service routing per monitored GPIO
#ifdef GCN_MONITORED_DIGITAL_PIN_A
  GCN_MONITORED_DIGITAL_PIN_CHANGED_ISR(GCN_MONITORED_DIGITAL_PIN_A)
#endif  // GCN_MONITORED_DIGITAL_PIN_A
#ifdef GCN_MONITORED_DIGITAL_PIN_B
  GCN_MONITORED_DIGITAL_PIN_CHANGED_ISR(GCN_MONITORED_DIGITAL_PIN_B)
#endif  // GCN_MONITORED_DIGITAL_PIN_B
#ifdef GCN_MONITORED_DIGITAL_PIN_C
  GCN_MONITORED_DIGITAL_PIN_CHANGED_ISR(GCN_MONITORED_DIGITAL_PIN_C)
#endif  // GCN_MONITORED_DIGITAL_PIN_C
#ifdef GCN_MONITORED_DIGITAL_PIN_D
  GCN_MONITORED_DIGITAL_PIN_CHANGED_ISR(GCN_MONITORED_DIGITAL_PIN_D)
#endif  // GCN_MONITORED_DIGITAL_PIN_D
#ifdef GCN_MONITORED_DIGITAL_PIN_E
  GCN_MONITORED_DIGITAL_PIN_CHANGED_ISR(GCN_MONITORED_DIGITAL_PIN_E)
#endif  // GCN_MONITORED_DIGITAL_PIN_E
#ifdef GCN_MONITORED_DIGITAL_PIN_F
  GCN_MONITORED_DIGITAL_PIN_CHANGED_ISR(GCN_MONITORED_DIGITAL_PIN_F)
#endif  // GCN_MONITORED_DIGITAL_PIN_F

private:
// Declare one monitor per monitored GPIO
#ifdef GCN_MONITORED_DIGITAL_PIN_A
  GCN_MONITORED_DIGITAL_PIN_MONITOR_DECLARATION(GCN_MONITORED_DIGITAL_PIN_A);
#endif  // GCN_MONITORED_DIGITAL_PIN_A
#ifdef GCN_MONITORED_DIGITAL_PIN_B
  GCN_MONITORED_DIGITAL_PIN_MONITOR_DECLARATION(GCN_MONITORED_DIGITAL_PIN_B);
#endif  // GCN_MONITORED_DIGITAL_PIN_B
#ifdef GCN_MONITORED_DIGITAL_PIN_C
  GCN_MONITORED_DIGITAL_PIN_MONITOR_DECLARATION(GCN_MONITORED_DIGITAL_PIN_C);
#endif  // GCN_MONITORED_DIGITAL_PIN_C
#ifdef GCN_MONITORED_DIGITAL_PIN_D
  GCN_MONITORED_DIGITAL_PIN_MONITOR_DECLARATION(GCN_MONITORED_DIGITAL_PIN_D);
#endif  // GCN_MONITORED_DIGITAL_PIN_D
#ifdef GCN_MONITORED_DIGITAL_PIN_E
  GCN_MONITORED_DIGITAL_PIN_MONITOR_DECLARATION(GCN_MONITORED_DIGITAL_PIN_E);
#endif  // GCN_MONITORED_DIGITAL_PIN_E
#ifdef GCN_MONITORED_DIGITAL_PIN_F
  GCN_MONITORED_DIGITAL_PIN_MONITOR_DECLARATION(GCN_MONITORED_DIGITAL_PIN_F);
#endif  // GCN_MONITORED_DIGITAL_PIN_F
};

// Initialize each monitor
#ifdef GCN_MONITORED_DIGITAL_PIN_A
GCN_MONITORED_DIGITAL_PIN_MONITOR_INITIALIZATION(GCN_MONITORED_DIGITAL_PIN_A);
#endif  // GCN_MONITORED_DIGITAL_PIN_A
#ifdef GCN_MONITORED_DIGITAL_PIN_B
GCN_MONITORED_DIGITAL_PIN_MONITOR_INITIALIZATION(GCN_MONITORED_DIGITAL_PIN_B);
#endif  // GCN_MONITORED_DIGITAL_PIN_B
#ifdef GCN_MONITORED_DIGITAL_PIN_C
GCN_MONITORED_DIGITAL_PIN_MONITOR_INITIALIZATION(GCN_MONITORED_DIGITAL_PIN_C);
#endif  // GCN_MONITORED_DIGITAL_PIN_C
#ifdef GCN_MONITORED_DIGITAL_PIN_D
GCN_MONITORED_DIGITAL_PIN_MONITOR_INITIALIZATION(GCN_MONITORED_DIGITAL_PIN_D);
#endif  // GCN_MONITORED_DIGITAL_PIN_D
#ifdef GCN_MONITORED_DIGITAL_PIN_E
GCN_MONITORED_DIGITAL_PIN_MONITOR_INITIALIZATION(GCN_MONITORED_DIGITAL_PIN_E);
#endif  // GCN_MONITORED_DIGITAL_PIN_E
#ifdef GCN_MONITORED_DIGITAL_PIN_F
GCN_MONITORED_DIGITAL_PIN_MONITOR_INITIALIZATION(GCN_MONITORED_DIGITAL_PIN_F);
#endif  // GCN_MONITORED_DIGITAL_PIN_F
