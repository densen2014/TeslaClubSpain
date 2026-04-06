// ============================================================
// Utility functions
// ============================================================

inline void setBit(CanFrame& frame, int bit, bool value) {
  if (bit < 0 || bit >= 64)
    return; // bounds guard: CanFrame.data is 8 bytes
  int byteIndex = bit / 8;
  int bitIndex  = bit % 8;
  uint8_t mask = static_cast<uint8_t>(1U << bitIndex);
  if (value) {
    frame.data[byteIndex] |= mask;
  } else {
    frame.data[byteIndex] &= static_cast<uint8_t>(~mask);
  }
}

inline uint8_t readMuxID(const CanFrame& frame) {
  return frame.data[0] & 0x07;
}

inline bool isFSDSelectedInUI(const CanFrame& frame) {
  return (frame.data[4] >> 6) & 0x01;
}

inline void setSpeedProfileV12V13(CanFrame& frame, int profile) {
  frame.data[6] &= ~0x06;
  frame.data[6] |= (profile << 1);
}
const char* getProfileText(int profile) {
  switch(profile) {
    case 3: return "Max";
    case 2: return "Hurry";
    case 1: return "Normal";
    case 0: return "Chill";
    case 4: return "Sloth";
    default: return "Unknown";
  }
}
// 0|off, 1|+5, 2|+7, 3|+10, 4|+15
const char* getSpeedOffsetText(int offset) {
  switch(offset) {
    case 0: return "Off";
    case 1: return "+5km/h";
    case 2: return "+7km/h";
    case 3: return "+10km/h";
    case 4: return "+15km/h";
    default: return "Unknown";
  }
}
unsigned long lastPrintTime = 0;

void smartPrint(const String& msg) {
  unsigned long now = millis();

  if (msg != lastLog || now - lastPrintTime > 1000) {
    Serial.println(msg);
    lastLog = msg;
    lastPrintTime = now;
  }
}

void loadConfig() {
  prefs.begin("config", true);
  savedSSID = prefs.getString("ssid", "");
  savedPASS = prefs.getString("pass", "");
  FSDForceEnabled   = prefs.getBool("fsdforce", false);
  nagEnabled   = prefs.getBool("nag", true);
  ISAChimSuppressEnabled   = prefs.getBool("isa", true);
  emergencyVehiclesEnabled   = prefs.getBool("emergencyVehicles", true);
  eceR79Enabled   = prefs.getBool("r79", true);
  serialPrintEnabled = prefs.getBool("print", true);
  speedOffset     = prefs.getInt("offset", 0);
  speedProfile    = prefs.getInt("profile", 1);
  prefs.end();
}
void saveConfig() {
  prefs.begin("config", false); // 可写

  prefs.putBool("fsdforce", FSDForceEnabled);
  prefs.putBool("nag", nagEnabled);
  prefs.putBool("isa", ISAChimSuppressEnabled);
  prefs.putBool("emergencyVehicles", emergencyVehiclesEnabled);
  prefs.putBool("r79", eceR79Enabled);
  prefs.putBool("print", serialPrintEnabled);
  prefs.putInt("offset", speedOffset);
  prefs.putInt("profile", speedProfile);

  prefs.end();
}
void saveWiFi(String ssid, String pass) {
  prefs.begin("config", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}

void canTask(void *pvParameters) {
  while (true) {
    CanFrame frame;
    if (!otaRunning) {
      if (!twaiReceive(frame)) {
        digitalWrite(LED_PIN, HIGH);
        return;
      }
      digitalWrite(LED_PIN, LOW);
      handler->handelMessage(frame);
    }
  }
}
volatile uint32_t idleCount0 = 0;
volatile uint32_t idleCount1 = 0;
void printCPU() {
  static uint32_t last0 = 0, last1 = 0;
  static unsigned long lastTime = 0;

  if (millis() - lastTime >= 1000) {
    uint32_t now0 = idleCount0;
    uint32_t now1 = idleCount1;

    float cpu0 = 100.0 - ((now0 - last0) / 200000.0 * 100.0);
    float cpu1 = 100.0 - ((now1 - last1) / 200000.0 * 100.0);

    last0 = now0;
    last1 = now1;
    lastTime = millis();

    Serial.printf("[CPU] Core0: %.1f%% | Core1: %.1f%%\n", cpu0, cpu1);
  }
}

void printMergedLog() {
  if (!serialPrintEnabled) return;

  String msg = "";

  if (isaTriggered) {
    msg += "[ISA] ";
  }

  msg += "FSD: ";
  msg += (FSDEnabled ? "ON" : "OFF");

  msg += " | Profile: ";
  msg += getProfileText(speedProfile);

  msg += " | Offset: ";
  msg += getSpeedOffsetText(speedOffset);

  // 去重
  if (msg != lastLog) {
    Serial.println(msg);
    lastLog = msg;
  }

}
