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

WebServer server(80);

// ===== Web控制变量 =====
bool webNagEnabled = true;   // 控制是否启用 Nag
bool webPrintEnabled = true; // 控制串口输出
String lastLog = ""; 
bool isaTriggered = false;   // 本轮是否触发 ISA
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
// ============================================================
// Handlers
// ============================================================

struct CarManagerBase {
  int  speedProfile = 1;
  // 0|off, 1|+5, 2|+7, 3|+10, 4|+15
  int  speedOffset = 0;
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
  // 抑制声音提示，仪表盘显示视觉指示器
  // =========================
  // 务必注意限速规定。此功能仅抑制声音提示，视觉指示器仍然可见。驾驶时请勿仅依赖蜂鸣声来判断车速。
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
      
      frame.data[7] = sum & 0xFF;
      twaiSend(frame);

      if (webPrintEnabled) {
        isaTriggered = true; 
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

    if (handsOn == 0 && webNagEnabled)
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
      echo.data[2] = 0x08; // Keep flag bits, clear upper torque bits
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

      if (webPrintEnabled) {
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
    speedOffset = 0;
    switch (fd) {
      case 1: speedProfile = 3; break;
      case 2: speedProfile = 2; break;
      case 3: speedProfile = 1; break;
      case 4: speedProfile = 0; break;
      case 5: speedProfile = 4; break;
      case 6: speedOffset = 1;speedProfile = 3; break;
    }

    return;
  }

  // =========================
  // 1021 - FSD 控制
  // =========================
  if (frame.can_id == 1021 && frame.can_dlc >= 8) {

    uint8_t index = readMuxID(frame);

    if (index == 0) {
      FSDEnabled = isFSDSelectedInUI(frame);//读取FSD状态，默认使用仅当在车辆设置中启用“交通信号灯和停车标志控制”时，FSD 才会激活

      if (FSDEnabled) {
        setBit(frame, 46, true);//启用FSD
        setBit(frame, 60, true);//启用 V14
        setBit(frame, 59, true);//启用检测紧急车辆
        twaiSend(frame);
      }

      if (webPrintEnabled) {
        printMergedLog();
      }
    }

    if (index == 1) {
      //驾驶时请始终双手放在方向盘上，并保持注意力集中。此功能仅用于测试目的。您始终对车辆安全驾驶负有责任。
      setBit(frame, 19, false);//UI_applyEceR79 清除“双手放在方向盘上”的提示音，抑制周期性的“对方向盘施加压力”警告
      setBit(frame, 47, true);//ASS 不受欧盟监管限制的智能召唤功能
      twaiSend(frame);
    }

    if (index == 2) {
      frame.data[7] &= ~(0x07 << 4); 
      frame.data[7] |= (speedProfile & 0x07) << 4; //速度配置文件
      frame.data[0] |= (speedOffset & 0x03) << 6;
      frame.data[1] |= (speedOffset >> 2);
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


void handleData() {
  if (!handler) {
    server.send(500, "text/plain", "handler null");
    return;
  }

  HW4Handler* h = static_cast<HW4Handler*>(handler.get());

  String json = "{";
  json += "\"fsd\":" + String(h->FSDEnabled ? "true":"false") + ",";
  json += "\"profile\":\"" + String(getProfileText(h->speedProfile)) + "\",";
  json += "\"offset\":\"" + String(getSpeedOffsetText(h->speedOffset)) + "\",";
  json += "\"nag\":" + String(webNagEnabled ? "true":"false") + ",";
  json += "\"print\":" + String(webPrintEnabled ? "true":"false");
  json += "}";

  server.send(200, "application/json", json);
}
void handleToggleNag() {
  webNagEnabled = !webNagEnabled;
  server.send(200, "text/plain", webNagEnabled ? "ON":"OFF");
}

void handleTogglePrint() {
  webPrintEnabled = !webPrintEnabled;
  server.send(200, "text/plain", webPrintEnabled ? "ON":"OFF");
}
void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    Update.begin(UPDATE_SIZE_UNKNOWN);
  } 
  else if (upload.status == UPLOAD_FILE_WRITE) {
    Update.write(upload.buf, upload.currentSize);
  } 
  else if (upload.status == UPLOAD_FILE_END) {
    Update.end(true);
  }
}
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Tesla FSD Panel</title>
<meta name="viewport" content="width=device-width, initial-scale=1">

<style>
body {
  margin:0;
  font-family: -apple-system, BlinkMacSystemFont;
  background: #0b0b0b;
  color: #fff;
  text-align:center;
}

h2 {
  margin-top:20px;
}

.container {
  padding:20px;
}

.card {
  background:#151515;
  border-radius:16px;
  padding:20px;
  margin:10px;
  box-shadow: 0 0 10px rgba(0,0,0,0.5);
}

.status {
  display:flex;
  justify-content:space-between;
  align-items:center;
  margin:12px 0;
  font-size:18px;
}

.dot {
  width:14px;
  height:14px;
  border-radius:50%;
  background:#444;
  box-shadow:0 0 5px #000;
}

.on {
  background:#00ff88;
  box-shadow:0 0 10px #00ff88;
}

.off {
  background:#ff4444;
  box-shadow:0 0 10px #ff4444;
}

button {
  width:100%;
  padding:14px;
  margin-top:10px;
  border:none;
  border-radius:12px;
  font-size:16px;
  background:#222;
  color:#fff;
}

button:active {
  background:#333;
}

.value {
  font-weight:bold;
  color:#0af;
}

input {
  margin-top:10px;
}
</style>
</head>

<body>

<h2>🚗 FSD Control Panel</h2>

<div class="container">

<div class="card">

<div class="status">
  <span>FSD</span>
  <div id="fsdDot" class="dot"></div>
</div>

<div class="status">
  <span>Nag Killer</span>
  <div id="nagDot" class="dot"></div>
</div>

<div class="status">
  <span>Serial Print</span>
  <div id="printDot" class="dot"></div>
</div>

<div class="status">
  <span>Profile</span>
  <span id="profile" class="value">-</span>
</div>

<div class="status">
  <span>Offset</span>
  <span id="offset" class="value">-</span>
</div>

<button onclick="toggleNag()">切换 Nag</button>
<button onclick="togglePrint()">切换打印</button>

</div>

<div class="card">
<h3>OTA 升级</h3>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="update">
<br>
<button type="submit">上传固件</button>
</form>
</div>

</div>

<script>
const el = id => document.getElementById(id);

function setDot(id, state){
  let d = el(id);
  d.classList.remove('on','off');
  d.classList.add(state ? 'on' : 'off');
}

function refresh(){
 fetch('/data')
  .then(r=>r.json())
  .then(d=>{
    setDot('fsdDot', d.fsd);
    setDot('nagDot', d.nag);
    setDot('printDot', d.print);

    el('profile').innerText = d.profile;
    el('offset').innerText = d.offset;
  });
}

function toggleNag(){
 fetch('/toggleNag').then(refresh);
}

function togglePrint(){
 fetch('/togglePrint').then(refresh);
}

setInterval(refresh, 1000);
refresh();
</script>

</body>
</html>
)rawliteral";

void printMergedLog() {
  if (!webPrintEnabled) return;

  HW4Handler* h = static_cast<HW4Handler*>(handler.get());

  String msg = "";

  if (isaTriggered) {
    msg += "[ISA] ";
  }

  msg += "FSD: ";
  msg += (h->FSDEnabled ? "ON" : "OFF");

  msg += " | Profile: ";
  msg += getProfileText(h->speedProfile);

  msg += " | Offset: ";
  msg += (h->speedOffset > 0 ? "+" : "");
  msg += String(h->speedOffset);

  // 去重
  if (msg != lastLog) {
    Serial.println(msg);
    lastLog = msg;
  }

  // 重置标记
  isaTriggered = false;
}
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

  WiFi.begin("你的wifi","你的密码");
  while(WiFi.status()!=WL_CONNECTED) delay(500);

  Serial.println(WiFi.localIP());

  server.on("/", [](){
    server.send(200,"text/html",htmlPage);
  });

  server.on("/data", handleData);
  server.on("/toggleNag", handleToggleNag);
  server.on("/togglePrint", handleTogglePrint);

  server.on("/update", HTTP_POST, [](){
    server.send(200,"text/plain","OK");
    ESP.restart();
  }, handleUpdateUpload);

  server.begin();
}

// ============================================================
// Loop
// ============================================================

void loop() {
  server.handleClient();
  CanFrame frame;
  if (!twaiReceive(frame)) {
    digitalWrite(LED_PIN, HIGH);
    return;
  }
  digitalWrite(LED_PIN, LOW);
  handler->handelMessage(frame);
}
