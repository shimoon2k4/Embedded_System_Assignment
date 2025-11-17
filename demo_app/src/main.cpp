#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>          // Quản lý WiFi
#include <WebServer.h>            // Cho /data, /status
#include <ArduinoJson.h>          // Cho /data, /status
#include <Firebase_ESP_Client.h>  // Cho Firebase
#include <Preferences.h>

// Thư viện cảm biến mới của Sơn
#include <Wire.h>
#include <Adafruit_AHTX0.h>

// ================================================================
// MODULE: CẤU HÌNH (FIREBASE & PINS)
// ================================================================

// --- 1. CẤU HÌNH FIREBASE (Giữ nguyên của bạn) ---
#define API_KEY "AIzaSyDw9gghwYr_agyJzMKdKSi42P5aIHcUyIA" 
#define DATABASE_URL "https://smart-fire-alarm-project-default-rtdb.asia-southeast1.firebasedatabase.app/" 
#define PROJECT_ID "smart-fire-alarm-project"
#define USER_EMAIL "test.user@gmail.com"
#define USER_PASSWORD "test123456"

// --- 2. CẤU HÌNH PHẦN CỨNG (Lấy từ code mới của Sơn) ---
// DHT20 dùng I2C
#define I2C_SDA_PIN   11  // SDA
#define I2C_SCL_PIN   12  // SCL
// Cảm biến
#define LED_PIN       2
#define FLAME_DO_PIN  10  // Digital Out của cảm biến Lửa (LOW = có lửa)
#define MQ2_AO_PIN    1   // Analog Out của MQ2
#define LDR_PIN       2   // Analog In của LDR

// Ngưỡng báo động (Lấy từ code mới của Sơn)
#define MQ2_THRESHOLD     2000  // Ngưỡng ADC để báo Gas
#define N_SAMPLES         20    // Số mẫu lấy trung bình cho MQ2

// ================================================================
// MODULE: BIẾN TOÀN CỤC VÀ ĐỐI TƯỢNG
// ================================================================

// --- 1. Đối tượng Cảm biến & Server ---
Adafruit_AHTX0 aht;       // Đối tượng AHT20 (thay cho DHT)
WebServer server(80);
Preferences preferences;

// --- 2. Đối tượng Firebase ---
FirebaseData fbdo;
FirebaseData stream_fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// --- 3. Biến Trạng thái & Cấu hình ---
String DEVICE_ID;
unsigned long send_interval = 30000; // Mặc định 30 giây
unsigned long last_sent_time = 0;

// --- 4. Biến Lưu trữ Cảm biến (cho /data và Firebase) ---
float currentTemperature = NAN;
float currentHumidity  = NAN;
int   currentLight     = -1;
bool  currentFlame     = false; // (mới)
int   currentMQ2ADC    = -1;    // (mới)
bool  currentGas       = false; // (mới)

// ================================================================
// MODULE: CÁC HÀM TIỆN ÍCH (Helpers)
// ================================================================

String build_device_id() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[32];
  sprintf(buf, "esp32-%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String(buf);
}

static int readAvgADC(int pin, int n = N_SAMPLES) {
  long s = 0;
  for (int i = 0; i < n; ++i) {
    s += analogRead(pin);
    delay(5);
  }
  return (int)(s / n);
}

// ================================================================
// MODULE: CẢM BIẾN (Đọc & Gửi dữ liệu)
// ================================================================

void read_sensors_and_publish() {
  // --- 1. Đọc Cảm biến  ---
  
  // Đọc AHT20 (Nhiệt độ, Độ ẩm)
  sensors_event_t humidity, temp;
  bool ok = aht.getEvent(&humidity, &temp);
  if (ok) {
    currentTemperature = temp.temperature;
    currentHumidity  = humidity.relative_humidity;
  } else {
    Serial.println("Failed to read AHT20");
  }

  // Đọc LDR
  currentLight = analogRead(LDR_PIN);

  // Đọc Cảm biến Lửa (Digital)
  currentFlame = (digitalRead(FLAME_DO_PIN) == LOW); // LOW = phát hiện lửa

  // Đọc Cảm biến Khí Gas (Analog)
  currentMQ2ADC = readAvgADC(MQ2_AO_PIN);
  currentGas  = (currentMQ2ADC > MQ2_THRESHOLD);

  // Log ra Serial
  Serial.printf("[SENSOR] T=%.1fC H=%.1f%% | LDR=%d | Flame=%s | MQ2=%d Gas=%s\n",
                currentTemperature, currentHumidity, currentLight,
                currentFlame ? "YES" : "NO",
                currentMQ2ADC, currentGas ? "YES" : "NO");

  // --- 2. Gửi dữ liệu lên Firestore ---
  if (!Firebase.ready()) {
    Serial.println("Firebase not ready, skipping publish");
    return;
  }

  FirebaseJson content;
  // Thêm tất cả các trường mới
  content.set("fields/temperature/doubleValue", String(currentTemperature));
  content.set("fields/humidity/doubleValue", String(currentHumidity));
  content.set("fields/light/integerValue", String(currentLight));
  content.set("fields/flame/booleanValue", currentFlame); // Dùng kiểu boolean
  content.set("fields/mq2_adc/integerValue", String(currentMQ2ADC));
  content.set("fields/gas/booleanValue", currentGas); // Dùng kiểu boolean
  content.set("fields/deviceId/stringValue", DEVICE_ID);
  content.set("fields/timestamp/timestampValue", Firebase.getServerTime(fbdo));

  // Gửi lên collection 'sensor_logs'
  String documentPath = "sensor_logs";
  Serial.println("Sending data to Firestore...");
  if (Firebase.Firestore.createDocument(&fbdo, PROJECT_ID, "", documentPath.c_str(), content.raw())) {
    Serial.println(">>> SUCCESS: Sent data to Firestore");
  } else {
    Serial.println("!!! FAILED: " + fbdo.errorReason());
  }
}

// ================================================================
// MODULE: FIREBASE (Callback)
// ================================================================

void streamCallback(StreamData data) {
  Serial.println("STREAM DATA RECEIVED (Config changed)");
  if (data.dataType() == "json") {
    FirebaseJson *json = data.to<FirebaseJson *>();
    FirebaseJsonData result;

    if (json->get(result, "sendInterval")) {
      if (result.type == "int") {
        send_interval = result.to<int>();
        Serial.println(">>> Updated sendInterval to: " + String(send_interval) + " ms");
      }
    }
  }
}

void streamTimeoutCallback(bool timeout) {
  if (timeout) Serial.println("!!! Stream timeout, resuming stream...");
}

void tokenStatusCallback(TokenInfo info) {
  if (info.status == token_status_ready) Serial.println(">>> Firebase Token ready");
  else Serial.println("!!! Firebase Token status: " + info.error.message);
}

// ================================================================
// HÀM CHÍNH: SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  // --- 1. Khởi tạo Cảm biến  ---
  pinMode(LED_PIN, OUTPUT);
  pinMode(FLAME_DO_PIN, INPUT_PULLUP); // Dùng INPUT_PULLUP
  pinMode(MQ2_AO_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);

  // Cấu hình ADC
  analogReadResolution(12);
  analogSetPinAttenuation(MQ2_AO_PIN, ADC_11db);
  analogSetPinAttenuation(LDR_PIN, ADC_11db);

  // Khởi tạo I2C và AHT20
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (!aht.begin(&Wire)) {
    Serial.println("AHT20 init FAILED");
  } else {
    Serial.println("AHT20 init OK.");
  }

  // --- 2. Lấy Device ID ---
  DEVICE_ID = build_device_id();
  Serial.println("Device ID: " + DEVICE_ID);

  // --- 3. Khởi tạo WiFi ---
  WiFiManager wm;
  // wm.resetSettings(); // Bỏ comment nếu muốn xóa WiFi đã lưu
  Serial.println("Connecting to WiFi (or starting AP mode)...");
  bool res = wm.autoConnect("ESP32-FireAlarm-Setup");
  if (!res) {
    Serial.println("!!! Failed to connect WiFi. Restarting...");
    ESP.restart();
  } else {
    Serial.println(">>> WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  }

  // --- 4. Khởi tạo WebServer (Cho /data và /status) ---
  // (Endpoint /status giữ nguyên)
  server.on("/status", HTTP_GET, [](){
    DynamicJsonDocument sdoc(256);
    sdoc["device_id"] = DEVICE_ID;
    sdoc["ip"] = WiFi.localIP().toString();
    sdoc["uptime_ms"] = millis();
    String out;
    serializeJson(sdoc, out);
    server.send(200, "application/json", out);
  });
  
  // (CẬP NHẬT /data để trả về TẤT CẢ cảm biến)
  server.on("/data", HTTP_GET, [](){
    DynamicJsonDocument rdoc(256); // Tăng size
    if (!isnan(currentTemperature)) rdoc["temperature"] = currentTemperature;
    if (!isnan(currentHumidity)) rdoc["humidity"] = currentHumidity;
    if (currentLight >= 0) rdoc["light"] = currentLight;
    rdoc["flame"] = currentFlame;     // (mới)
    rdoc["mq2_adc"] = currentMQ2ADC;  // (mới)
    rdoc["gas"] = currentGas;         // (mới)
    rdoc["uptime_ms"] = millis();
    String out;
    serializeJson(rdoc, out);
    server.send(200, "application/json", out);
  });
  
  server.begin();
  Serial.println("Web server started for /data and /status endpoints.");

  // --- 5. Khởi tạo Firebase ---
  Serial.println("Initializing Firebase...");
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Đăng ký nghe thay đổi cấu hình từ Firebase RTDB
  String configPath = "devices/" + DEVICE_ID + "/config";
  if (!Firebase.RTDB.beginStream(&stream_fbdo, configPath.c_str())) {
    Serial.println("!!! FAILED to begin RTDB stream: " + stream_fbdo.errorReason());
  }
  Firebase.RTDB.setStreamCallback(&stream_fbdo, streamCallback, streamTimeoutCallback);
  Serial.println(">>> Firebase Stream setup. Listening for config at: " + configPath);
}

// ================================================================
// HÀM CHÍNH: LOOP
// ================================================================
void loop() {
  // Luôn chạy WebServer
  server.handleClient(); 

  // Chỉ gửi dữ liệu khi Firebase sẵn sàng và đủ thời gian
  if (Firebase.ready() && (millis() - last_sent_time > send_interval)) {
    last_sent_time = millis();
    read_sensors_and_publish(); // Gọi hàm đọc và gửi dữ liệu
  }
}