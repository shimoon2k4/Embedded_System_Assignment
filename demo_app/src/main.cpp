// main.cpp tái cấu trúc: chỉ khởi tạo và gọi các module cảm biến và webUI
#include <Arduino.h>
#include "module/sensor.h"
#include "module/webUI.h"

void setup() {
    Serial.begin(115200);
    initSensors();
    setupWebUI();
}

void loop() {
    readSensors();
    handleWebRequests();
    // Nếu cần, thêm xử lý MQTT hoặc các chức năng khác ở đây
}
