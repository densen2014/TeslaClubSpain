String getJsonData() {
  StaticJsonDocument<200> doc;

  doc["fsd"] = FSDEnabled;
  // ✅ 文本（UI用）
  doc["profile"] = getProfileText(speedProfile);
  doc["offset"]  = getSpeedOffsetText(speedOffset);

  // ✅ 数值（逻辑用）
  doc["offsetval"] = speedOffset;

  // ✅ 状态
  doc["fsdforce"] = FSDForceEnabled;
  doc["isa"] = ISAChimSuppressEnabled;
  doc["emergencyVehicles"] = emergencyVehiclesEnabled;
  doc["r79"] = eceR79Enabled;
  doc["nag"] = nagEnabled;
  doc["print"] = serialPrintEnabled;
  doc["version"] = FW_VERSION;
  doc["build"] = BUILD_TIME;

  String json;
  serializeJson(doc, json);
  return json;
}
void handleData() {
  if (!handler) {
    server.send(500, "text/plain", "handler null");
    return;
  }
  server.send(200, "application/json", getJsonData());
}

void sendWSData() {
  if (!handler) return;

  String json = getJsonData();

  if (json != lastWS) {
    Serial.println("WS broadcastT:\n" +  json);
    ws.broadcastTXT(json);
    lastWS = json;
    saveConfig();
  }

}
void handleToggleNag() {
  nagEnabled = !nagEnabled;
  server.send(200, "text/plain", nagEnabled ? "ON":"OFF");
}
void handleSetOffset() {
  if (!handler) {
    server.send(500, "text/plain", "handler null");
    return;
  }

  if (!server.hasArg("val")) {
    server.send(400, "text/plain", "missing val");
    return;
  }

  int val = server.arg("val").toInt();

  // 限制范围
  val = constrain(val, 0, 4);
  speedOffset = val;

  server.send(200, "text/plain", "OK");
}
void handleTogglePrint() {
  serialPrintEnabled = !serialPrintEnabled;
  server.send(200, "text/plain", serialPrintEnabled ? "ON":"OFF");
}
void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaRunning = true;
    Serial.println("OTA Start");

    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }

  } 
  else if (upload.status == UPLOAD_FILE_WRITE) {

    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }

  } 
  else if (upload.status == UPLOAD_FILE_END) {

    if (Update.end(true)) {
      Serial.println("OTA Success");
    } else {
      Update.printError(Serial);
    }
  }
}
void handleSaveWifi() {

  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "No Data");
    return;
  }

  String body = server.arg("plain");

  StaticJsonDocument<200> doc;
  deserializeJson(doc, body);

  String ssid = doc["ssid"];
  String pass = doc["pass"];

  if (ssid.length() == 0) {
    server.send(400, "text/plain", "SSID empty");
    return;
  }
  saveWiFi(ssid, pass);

  server.send(200, "text/plain", "Saved! Rebooting...");
  delay(1000);
  ESP.restart();
}

void webserver()
{
  WiFi.mode(WIFI_AP_STA);
  IPAddress IP(192,168,4,1);
  IPAddress gateway(192,168,4,1);
  IPAddress subnet(255,255,255,0);
  WiFi.softAPConfig(IP, gateway, subnet);
  WiFi.softAP("Tesla-FSD-TOOL", "1234mima");
  Serial.println("AP Started");
  Serial.println(WiFi.softAPIP());
  
  if (savedSSID.length() > 0) {
    WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 2000) {
      delay(500);
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WIFI CONNECTED");
      Serial.println(WiFi.localIP());
    }
  }

  server.on("/", [](){
    //if (savedSSID.length() > 0) {
    server.send(200,"text/html",htmlPage);
    //}else{
    //  server.send(200,"text/html",getSetupPage());
    //}
  });

  server.on("/config", [](){
    server.send(200, "text/html", getSetupPage());
  });
  server.on("/data", handleData);
  server.on("/toggleNag", handleToggleNag);
  server.on("/togglePrint", handleTogglePrint);
  server.on("/setOffset", handleSetOffset); 
  server.on("/saveWifi", HTTP_POST, handleSaveWifi);

  server.on("/update", HTTP_POST, [](){

    if (Update.hasError()) {
      server.send(500, "text/plain", "FAIL");
    } else {
      server.send(200, "text/plain", "OK");
      delay(1000);
      ESP.restart();
    }

  }, handleUpdateUpload);

  server.begin();
}
