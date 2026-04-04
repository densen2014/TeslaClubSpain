/*
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

// ============================================================
// Target: ESP32 with native TWAI (built-in CAN controller)
// External transceiver required: SN65HVD230, TJA1050, or MCP2551
// No MCP2515 needed!
// ============================================================

#include <memory>
#include <algorithm>
#include "driver/twai.h"

// ============================================================
// Hardware configuration
// ============================================================

// TWAI pins — adjust according to your wiring to the CAN transceiver
#define CAN_TX_PIN    GPIO_NUM_5
#define CAN_RX_PIN    GPIO_NUM_4

// Transceiver standby (if applicable, e.g. SN65HVD230 Rs pin)
#define CAN_STBY_PIN  GPIO_NUM_16   // set to -1 if not wired

#define LED_PIN       GPIO_NUM_2    // onboard LED

// ============================================================
// Vehicle selection
// ============================================================


#define HW4    HW4Handler  
#define HW HW4

bool enablePrint = true;

// ============================================================
// TWAI wrapper — sends a CAN frame
// ============================================================

struct CanFrame {
  uint32_t can_id;
  uint8_t  can_dlc;
  uint8_t  data[8];
};

bool twaiSend(CanFrame& frame) {
  twai_message_t msg = {};
  msg.identifier      = frame.can_id;
  msg.data_length_code = frame.can_dlc;
  msg.flags            = 0;  // standard frame, no RTR
  memcpy(msg.data, frame.data, frame.can_dlc);

  esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(10));
  if (err != ESP_OK) {
    Serial.printf("TWAI TX err: 0x%x\n", err);
    return false;
  }
  return true;
}

bool twaiReceive(CanFrame& frame) {
  twai_message_t msg;
  esp_err_t err = twai_receive(&msg, pdMS_TO_TICKS(1));
  if (err != ESP_OK) {
    return false;
  }
  // Ignore RTR and extended frames
  if (msg.rtr || msg.extd) {
    return false;
  }
  frame.can_id  = msg.identifier;
  frame.can_dlc = msg.data_length_code;
  memcpy(frame.data, msg.data, msg.data_length_code);
  return true;
}

// ============================================================
// Utility functions
// ============================================================

inline void setBit(CanFrame& frame, int bit, bool value) {
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

// ============================================================
// Handlers
// ============================================================

struct CarManagerBase {
  int  speedProfile = 1;
  bool FSDEnabled   = false;
  virtual void handelMessage(CanFrame& frame) = 0;
  virtual ~CarManagerBase() = default;
};

struct HW4Handler : public CarManagerBase {
  void handelMessage(CanFrame& frame) override {    
  
  // 只接收 880 / 921 / 1016 / 1021  
  if (!(frame.can_id == 880 ||
      frame.can_id == 921 ||
      frame.can_id == 1016 ||
      frame.can_id == 1021)) {
    return;
  }

  // =========================
  // 921 - ISA CHIME SUPPRESS
  // =========================
  if (frame.can_id == 921 && frame.can_dlc >= 8) {

    if (!(frame.data[1] & 0x20)) {

      frame.data[1] |= 0x20;

      uint8_t sum = 0;
      for (int i = 0; i < 7; ++i) {
        sum += frame.data[i];
      }

      const uint16_t ID = 921;
      sum += (uint8_t)(ID & 0xFF);
      sum += (uint8_t)(ID >> 8);

      frame.data[7] = sum;

      twaiSend(frame);

      if (enablePrint) {
        Serial.println("ISA SPEED CHIME SUPPRESS");
      }
    }
    return;
  }

  // =========================
  // 880 - NAG（只处理880！！！）
  // =========================
  if (frame.can_id == 880 && frame.can_dlc >= 8) {

    // 防止处理自己发的帧（关键）
    if (frame.data[4] & 0x40) {
      return;
    }

    uint8_t handsOn = (frame.data[4] >> 6) & 0x03;

    if (handsOn == 0) {
      // ===== 防检测控制 =====
      static uint32_t lastSend = 0;

      uint32_t now = millis();
      uint32_t interval = 10 + random(0, 5);

      if (now - lastSend < interval) return;

      // ===== 概率发送 =====
      if (random(0, 100) < 10) {
          return;
      }

      lastSend = now;

      CanFrame echo;
      echo.can_id = 880;
      echo.can_dlc = 8;

      echo.data[0] = frame.data[0];
      echo.data[1] = frame.data[1];
      echo.data[2] = frame.data[2];
      echo.data[5] = frame.data[5];

      // ===== torque随机化 =====
      uint8_t baseTorque = 0xB6;
      echo.data[3] = baseTorque + random(-2, 3);

      // handsOn = 1
      echo.data[4] = frame.data[4] | 0x40;

      // counter++
      uint8_t cnt = (frame.data[6] & 0x0F);
      cnt = (cnt + 1) & 0x0F;
      echo.data[6] = (frame.data[6] & 0xF0) | cnt;

      // checksum
      uint16_t sum =
          echo.data[0] + echo.data[1] + echo.data[2] +
          echo.data[3] + echo.data[4] + echo.data[5] +
          echo.data[6];

      echo.data[7] = (uint8_t)((sum + 0x73) & 0xFF);

      framesSent++;
      nagEchoCount++;

      twaiSend(echo);

      if (enablePrint && (nagEchoCount % 500 == 1)) {
        Serial.print("Nag echo=");
        Serial.println(nagEchoCount);
      }
    }

    return;
  }

  // =========================
  // 1016 - Speed Profile
  // =========================
  if (frame.can_id == 1016 && frame.can_dlc >= 6) {

    uint8_t fd = (frame.data[5] & 0b11100000) >> 5;

    switch (fd) {
      case 1: speedProfile = 3; break;
      case 2: speedProfile = 2; break;
      case 3: speedProfile = 1; break;
      case 4: speedProfile = 0; break;
      case 5: speedProfile = 4; break;
    }

    return;
  }

  // =========================
  // 1021 - FSD 控制
  // =========================
  if (frame.can_id == 1021 && frame.can_dlc >= 8) {

    uint8_t index = readMuxID(frame);

    if (index == 0) {
      FSDEnabled = isFSDSelectedInUI(frame);

      if (FSDEnabled) {
        setBit(frame, 46, true);
        setBit(frame, 60, true);
        setBit(frame, 59, true);
        twaiSend(frame);
      }

      if (enablePrint) {
        Serial.print("FSD: ");
        Serial.print(FSDEnabled);
        Serial.print(" profile: ");
        Serial.println(speedProfile);
      }
    }

    if (index == 1) {
      setBit(frame, 19, false);
      setBit(frame, 47, true);
      twaiSend(frame);
    }

    if (index == 2) {
      frame.data[7] &= ~(0x07 << 4);
      frame.data[7] |= (speedProfile & 0x07) << 4;
      twaiSend(frame);
    }

    return;
  }
};

// ============================================================
// Global instance
// ============================================================

std::unique_ptr<CarManagerBase> handler;

// ============================================================
// Setup
// ============================================================

void setup() {
  // LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Transceiver standby (LOW = active)
  if (CAN_STBY_PIN >= 0) {
    pinMode(CAN_STBY_PIN, OUTPUT);
    digitalWrite(CAN_STBY_PIN, LOW);
  }

  // Vehicle handler
  handler = std::make_unique<HW>();

  delay(1500);

  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 2000) {}
  Serial.println("ESP32 TWAI CAN FSD Handler starting...");

  // ---- TWAI configuration ----
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
  g_config.rx_queue_len = 32;   // larger RX buffer to avoid dropping frames
  g_config.tx_queue_len = 8;

  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();

  // Filter: accept only the IDs we care about
  // For simplicity we accept all, but could filter on 1006/1016/1021
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  // Install and start the driver
  esp_err_t err;
  err = twai_driver_install(&g_config, &t_config, &f_config);
  if (err != ESP_OK) {
    Serial.printf("TWAI install failed: 0x%x\n", err);
    while (1) { delay(1000); }
  }

  err = twai_start();
  if (err != ESP_OK) {
    Serial.printf("TWAI start failed: 0x%x\n", err);
    while (1) { delay(1000); }
  }

  Serial.println("TWAI ready @ 500kbps (native CAN)");
}

// ============================================================
// Loop
// ============================================================

void loop() {
  CanFrame frame;
  if (!twaiReceive(frame)) {
    digitalWrite(LED_PIN, HIGH);
    return;
  }
  digitalWrite(LED_PIN, LOW);
  handler->handelMessage(frame);
}
