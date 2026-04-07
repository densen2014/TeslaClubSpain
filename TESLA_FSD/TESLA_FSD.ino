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
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#define FW_VERSION "0.0.4"
#define BUILD_TIME __DATE__ " " __TIME__ 

// ============================================================
// Hardware configuration
// ============================================================

// TWAI pins — adjust according to your wiring to the CAN transceiver
#define CAN_TX_PIN    GPIO_NUM_5
#define CAN_RX_PIN    GPIO_NUM_4
#define LED_PIN       GPIO_NUM_2    // onboard LED

// ============================================================
// Vehicle selection
// ============================================================


#define HW4    HW4Handler  
#define HW HW4 

Preferences prefs;
WebServer server(80);
WebSocketsServer ws(81);

// ===== 变量 =====
volatile bool FSDEnabled   = false;
volatile bool FSDForceEnabled   = false;
volatile bool ISAChimSuppressEnabled   = true;
volatile bool emergencyVehiclesEnabled   = true;
volatile bool eceR79Enabled   = true;
volatile bool nagEnabled = true;   // 控制是否启用 Nag
volatile bool serialPrintEnabled = true; // 控制串口输出
volatile bool otaRunning = false;
volatile bool webEnabled   = true;
volatile int  speedProfile = 1;
volatile int  speedOffset = 0;

bool isaTriggered = false;   // 本轮是否触发 ISA
String lastLog = ""; 
String lastWS = "";
String savedSSID;
String savedPASS;

struct CanFrame {
  uint32_t can_id;
  uint8_t  can_dlc;
  uint8_t  data[8];
};
bool twaiSend(CanFrame& frame);
void smartPrint(const String& msg);
inline uint8_t readMuxID(const CanFrame& frame);
inline bool isFSDSelectedInUI(const CanFrame& frame);
inline void setBit(CanFrame& frame, int bit, bool value);
void printMergedLog();
void sendWSData();
void onWsEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
extern const char* htmlPage;
String getSetupPage();
// ============================================================
// Handlers
// ============================================================
struct CarManagerBase {
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
  // 抑制声音提示，仪表盘显示视觉指示器
  // =========================
  // 务必注意限速规定。此功能仅抑制声音提示，视觉指示器仍然可见。驾驶时请勿仅依赖蜂鸣声来判断车速。
  if (ISAChimSuppressEnabled && frame.can_id == 921 && frame.can_dlc >= 8) {

    if (!(frame.data[1] & 0x20)) {

      frame.data[1] |= 0x20;

      uint8_t sum = 0;
      for (int i = 0; i < 7; ++i) {
        sum += frame.data[i];
      }


      sum += (921 & 0xFF) + (921 >> 8);
      frame.data[7] = sum & 0xFF;
      twaiSend(frame);

      isaTriggered = true; 
      if (serialPrintEnabled) {
        printMergedLog();
      }
      sendWSData();
    }
    return;
  }

  // =========================
  // 880 - NAG（只处理880！！！）
  // =========================
  if (nagEnabled && frame.can_id == 880 && frame.can_dlc >= 8) {

    // 防止处理自己发的帧（关键）
    if (frame.data[4] & 0x40) {
      return;
    }

    uint8_t handsOn = (frame.data[4] >> 6) & 0x03;

    if (handsOn == 0)
    {
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
      echo.data[2] = (frame.data[2] & 0xF0) | 0x08;
      echo.data[5] = frame.data[5];

      // Fixed torque = 1.80 Nm 
      echo.data[3] = 0xB6;

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

      twaiSend(echo);

      if (serialPrintEnabled) {
        smartPrint("Nag ");
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
      case 6: speedOffset = 1;speedProfile = 3; break;
    }
  }

  // =========================
  // 1021 - FSD 控制
  // =========================
  if (frame.can_id == 1021 && frame.can_dlc >= 8) {

    uint8_t index = readMuxID(frame);

    if (index == 0) {
      FSDEnabled = isFSDSelectedInUI(frame);//读取FSD状态，默认使用仅当在车辆设置中启用“交通信号灯和停车标志控制”时，FSD 才会激活

      if (FSDEnabled || FSDForceEnabled) {
        setBit(frame, 46, true);//启用FSD
        setBit(frame, 60, true);//启用 V14
        if (emergencyVehiclesEnabled) {
          setBit(frame, 59, true);//启用检测紧急车辆
        }
        twaiSend(frame);
      }

      if (serialPrintEnabled) {
        printMergedLog();
      }
      sendWSData();
    }

    if (index == 1) {
      //驾驶时请始终双手放在方向盘上，并保持注意力集中。此功能仅用于测试目的。您始终对车辆安全驾驶负有责任。
      if (eceR79Enabled){
        setBit(frame, 19, false);//UI_applyEceR79 清除“双手放在方向盘上”的提示音，抑制周期性的“对方向盘施加压力”警告
      }
      setBit(frame, 47, true);//ASS 不受欧盟监管限制的智能召唤功能
      twaiSend(frame);
    }

    if (index == 2) {
      frame.data[7] &= ~(0x07 << 4); 
      frame.data[7] |= (speedProfile & 0x07) << 4; //速度配置文件
      //frame.data[0] |= (speedOffset & 0x03) << 6; //HW3 only
      //frame.data[1] |= (speedOffset >> 2); //HW3 only
      frame.data[1] = (frame.data[1] & 0xC0) | 0x0A; // Speed offset +7-8 km/h
      twaiSend(frame);
    }

    return;
  }
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

  loadConfig();

  if (webEnabled)
  {
    webserver();
    ws.begin();
    ws.onEvent(onWsEvent);
  }
}

// ============================================================
// Loop
// ============================================================

void loop() {
  if (webEnabled)
  {
    server.handleClient();
    ws.loop();
  }
  CanFrame frame;
  if (!otaRunning) {
    if (!twaiReceive(frame)) {
      digitalWrite(LED_PIN, HIGH);
      return;
    }
    digitalWrite(LED_PIN, LOW);
    handler->handelMessage(frame);
  }
  //printCPU();
}
