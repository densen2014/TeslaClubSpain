// ============================================================
// TWAI wrapper — sends a CAN frame
// ============================================================



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
