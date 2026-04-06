
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
.row {
  display: flex;
  justify-content: space-between;
  gap: 10px;
}

.row button {
  flex: 1;
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
    <span>FSD Force</span>
    <div id="fsdforceDot" class="dot"></div>
  </div>

  <div class="status">
    <span>ISA Chim Suppress</span>
    <div id="isaDot" class="dot"></div>
  </div>

  <div class="status">
    <span>Emergency Vehicles</span>
    <div id="emergencyVehiclesDot" class="dot"></div>
  </div>

  <div class="status">
    <span>Nag Killer</span>
    <div id="nagDot" class="dot"></div>
  </div>

  <div class="status">
    <span>R79</span>
    <div id="r79Dot" class="dot"></div>
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
</div>

<div class="card">
  <h3>Function</h3>
  <div class="row">
    <button onclick="toggleCommand('fsdforce')">FSD</button>
    <button onclick="toggleCommand('isa')">ISA</button>
    <button onclick="toggleCommand('nag')">Nag</button>
    <button onclick="toggleCommand('emergencyVehicles')">EmerVehi</button>
   </div>
   <div class="row">
    <button onclick="toggleCommand('r79')">R79</button>
    <button onclick="toggleCommand('print')">SerialLog</button>
    <button onclick="location.href='/config'">Config</button>
  </div>
</div>

<div class="card">
  <h3>Speed Offset (km/h)</h3>
  <span> hw3 only</span>
  <div class="row">
    <button id="off0" onclick="setOffset(0)">Off</button>
    <button id="off1" onclick="setOffset(1)">+5</button>
    <button id="off2" onclick="setOffset(2)">+7</button>
    <button id="off3" onclick="setOffset(3)">+10</button>
    <button id="off4" onclick="setOffset(4)">+15</button> 
  </div>
</div>

<div class="card">
<h3>OTA 升级</h3>
<form id="otaForm">
<input type="file" id="file">
<button type="button" onclick="upload()">上传固件</button>
</form>

<br/>
<div id="progress">0%</div>
</div>

<div style="margin:3px; font-size:12px; color:#888;">
  Firmware: <span id="version">-</span> 
  Build: <span id="build"></span>
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
    setDot('fsdforce', d.fsdforce);
    setDot('isaDot', d.isa);
    setDot('nagDot', d.nag);
    setDot('emergencyVehiclesDot', d.emergencyVehicles);
    setDot('r79Dot', d.r79);
    setDot('printDot', d.print);
    highlightOffset(d.offsetval);
    el('profile').innerText = d.profile;
    el('offset').innerText = d.offset;
    el('version').innerText = d.version || '-';
    el('build').innerText = d.build;
  });
}
function setOffset(v){
  ws.send(JSON.stringify({cmd:"offset", val:v}));
}
function highlightOffset(val){
  for(let i=0;i<=4;i++){
    let b = document.getElementById('off'+i);
    if(b){
      b.style.background = (i==val) ? "#00ff88" : "#222";
    }
  }
}
function toggleCommand(command){
  ws.send(JSON.stringify({cmd:command}));
} 
function upload(){
  let file = document.getElementById("file").files[0];
  let form = new FormData();
  form.append("update", file);

  let xhr = new XMLHttpRequest();

  xhr.upload.onprogress = function(e){
    if(e.lengthComputable){
      let p = Math.round((e.loaded / e.total) * 100);
      document.getElementById("progress").innerText = p + "%";
    }
  };

  xhr.onload = function(){
    alert("升级完成，设备即将重启。" + xhr.responseText);
    setTimeout(()=>location.reload(), 3000);
  };

  xhr.open("POST", "/update", true);
  xhr.send(form);
}
function connectWS(){
  let ws = new WebSocket("ws://" + location.hostname + ":81/");
  ws.onopen = () => console.log("WS connected");
  ws.onmessage = function(event) {
    try {
      let d = JSON.parse(event.data);

      // 状态灯
      setDot('fsdDot', d.fsd);
      setDot('fsdforceDot', d.fsdforce);
      setDot('isaDot', d.isa);
      setDot('nagDot', d.nag);
      setDot('emergencyVehiclesDot', d.emergencyVehicles);
      setDot('r79Dot', d.r79); 
      setDot('printDot', d.print);

      // 文本显示
      el('profile').innerText = d.profile || '-';
      el('offset').innerText = d.offset || '-';
      el('version').innerText = d.version || '-';
      el('build').innerText = d.build || '-';

      // ✅ 高亮 offset 按钮（关键）
      if (d.offsetval !== undefined) {
        highlightOffset(d.offsetval);
      }

    } catch(e) {
      console.error("WS JSON error", e);
    }
  };
  ws.onclose = () => {
    console.log("WS reconnect...");
    setTimeout(connectWS, 1000);
  };

  return ws;
}
let ws = connectWS();
//refresh();
</script>

</body>
</html>
)rawliteral";

String getSetupPage() {

  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Tesla FSD Setup</title>
<meta name="viewport" content="width=device-width, initial-scale=1"> 
<style>
body { font-family: Arial; text-align:center; background:#111; color:#fff; }
input, button { padding:10px; margin:10px; width:80%; }
</style>
</head>
<body>

<h2>🚗 Tesla FSD Setup</h2>

<input id="ssid" placeholder="WiFi SSID" value=")rawliteral";

  html += savedSSID;

  html += R"rawliteral("><br>

<input id="pass" placeholder="Password" type="password" value=")rawliteral";

  html += savedPASS;

  html += R"rawliteral("><br>

<button onclick="save()">保存并连接</button>

<button onclick="location.href='/'">返回</button>

<script>
function save(){
  let ssid = document.getElementById("ssid").value;
  let pass = document.getElementById("pass").value;

  fetch("/saveWifi", {
    method: "POST",
    headers: {"Content-Type":"application/json"},
    body: JSON.stringify({ssid:ssid, pass:pass})
  }).then(r=>r.text()).then(t=>{
    alert(t);
  });
}
</script>

</body>
</html>
)rawliteral";

  return html;
}

void onWsEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {

    case WStype_CONNECTED: 
      lastWS = "";
      sendWSData();
      Serial.printf("WS Client %u connected\n", num);
      break;

    case WStype_DISCONNECTED:
      Serial.printf("WS Client %u disconnected\n", num);
      break;

    case WStype_TEXT: 
    {
      String msg = String((char*)payload);
      Serial.println("WS recv: " + msg);

      StaticJsonDocument<200> doc;
      DeserializationError err = deserializeJson(doc, msg);

      if (err) {
        Serial.println("JSON parse failed");
        return;
      }

      String cmd = doc["cmd"];

      if (cmd == "offset") {
        int val = doc["val"];
        val = constrain(val, 0, 4);
        speedOffset = val;
      } 
      else if (cmd == "fsdforce") {
        FSDForceEnabled = !FSDForceEnabled;
      } 
      else if (cmd == "isa") {
        ISAChimSuppressEnabled = !ISAChimSuppressEnabled;
      } 
      else if (cmd == "emergencyVehicles") {
        emergencyVehiclesEnabled = !emergencyVehiclesEnabled;
      } 
      else if (cmd == "r79") {
        eceR79Enabled = !eceR79Enabled;
      } 
      else if (cmd == "nag") {
        nagEnabled = !nagEnabled;
      } 
      else if (cmd == "print") {
        serialPrintEnabled = !serialPrintEnabled;
      }

      // 👉 状态变化后立即推送
      sendWSData();
      break;
    }
    default:
      break;
  }
}