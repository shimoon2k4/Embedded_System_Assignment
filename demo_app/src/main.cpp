#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>           
#include <ArduinoJson.h>         
#include <DHT.h>
#include <Preferences.h>
#include <WiFiManager.h>          
#include <Firebase_ESP_Client.h>  

// ================================================================
// CẤU HÌNH FIREBASE 
// ================================================================
// Lấy từ Project Settings -> General -> Web API Key
#define API_KEY "YOUR_WEB_API_KEY" 

// Lấy từ Realtime Database -> Data (link ở trên cùng)
#define DATABASE_URL "https://smart-fire-alarm-project-default-rtdb.asia-southeast1.firebasedatabase.app/" 

// Lấy từ Project Settings -> General -> Project ID
#define PROJECT_ID "smart-fire-alarm-project"

#define USER_EMAIL "test.user@gmail.com"
#define USER_PASSWORD "test123456"


// ================================================================
// CẤU HÌNH PHẦN CỨNG 
// ================================================================
#define DHTPIN   14
#define DHTTYPE  DHT11
#define LED_PIN  2
#define LDR_PIN  34 // Cảm biến ánh sáng


// ================================================================
// KHỞI TẠO CÁC ĐỐI TƯỢNG TOÀN CỤC ===
// ================================================================
DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);            
Preferences preferences;         

// Biến toàn cục của Firebase
FirebaseData fbdo;
FirebaseData stream_fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Biến toàn cục
unsigned long send_interval = 30000; // Mặc định 30 giây (sẽ được cập nhật từ Firebase)
unsigned long last_sent_time = 0;
String DEVICE_ID;

//Biến lưu cảm biến để cho /data endpoint
float currentTemperature = NAN;
float currentHumidity = NAN;
int currentLight = -1;

// Helper tạo Device ID
String build_device_id() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[32];
  sprintf(buf, "esp32-%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String(buf);
}

// Hàm này sẽ gửi data lên FIREBASE
void read_and_publish_data() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int l = analogRead(LDR_PIN);

  if (isnan(h) || isnan(t)) {
    Serial.println("Failed to read DHT");
    return;
  }

  // Cập nhật biến toàn cục để /data web server dùng
  currentTemperature = t;
  currentHumidity = h;
  currentLight = l;

  Serial.printf("[%lu] Sending data: T=%.1f C, H=%.1f %%, Light=%d\n",
                  millis() / 1000, t, h, l);

  // ----------------------------------------------------------------
  // Gửi dữ liệu lên Firestore
  // ----------------------------------------------------------------
  if (!Firebase.ready()) {
    Serial.println("Firebase not ready, skipping publish");
    return;
  }

  FirebaseJson content;
  content.set("fields/temperature/doubleValue", String(t));
  content.set("fields/humidity/doubleValue", String(h));
  content.set("fields/light/integerValue", String(l)); // Thêm cảm biến ánh sáng
  content.set("fields/deviceId/stringValue", DEVICE_ID);
  content.set("fields/timestamp/timestampValue", Firebase.getServerTime(fbdo));

  String documentPath = "sensor_logs"; // Gửi vào collection này
  
  Serial.println("Sending data to Firestore...");
  if (Firebase.Firestore.createDocument(&fbdo, PROJECT_ID, "", documentPath.c_str(), content.raw())) {
    Serial.println(">>> SUCCESS: Sent data to Firestore");
  } else {
    Serial.println("!!! FAILED: " + fbdo.errorReason());
  }
}


// ================================================================
// CÁC HÀM CỦA FIREBASE 
// ================================================================

// Hàm này được gọi khi có thay đổi trên Realtime Database
// (Khi App Flutter thay đổi cấu hình)
void streamCallback(StreamData data) {
  Serial.println("====================================");
  Serial.println("STREAM DATA RECEIVED (Config changed)");
  Serial.println("====================================");

  if (data.dataType() == "json") {
    FirebaseJson *json = data.to<FirebaseJson *>();
    FirebaseJsonData result;

    // Yêu cầu: App cho phép điều chỉnh chu kỳ gửi dữ liệu
    if (json->get(result, "sendInterval")) {
      if (result.type == "int") {
        send_interval = result.to<int>();
        Serial.println(">>> Updated sendInterval to: " + String(send_interval) + " ms");
      }
    }
    // thêm code xử lý "deviceId" hoặc "wifi" ở đây
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
// HÀM CÀI ĐẶT (SETUP)
// ================================================================
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LDR_PIN, INPUT);
  delay(200);

  dht.begin();
  DEVICE_ID = build_device_id();
  Serial.println("Device ID: " + DEVICE_ID);

  // ----------------------------------------------------------------
  // THAY THẾ code WiFi gốc bằng WiFiManager
  // ----------------------------------------------------------------
  WiFiManager wm;
  // wm.resetSettings(); // Bỏ comment nếu muốn xóa WiFi đã lưu
  Serial.println("Connecting to WiFi (or starting AP mode)...");
  bool res = wm.autoConnect("ESP32-FireAlarm-Setup"); // Tên WiFi AP để cấu hình

  if (!res) {
    Serial.println("!!! Failed to connect to WiFi. Restarting...");
    ESP.restart();
  } else {
    Serial.println(">>> WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  }

  // ----------------------------------------------------------------
  // Start server routes
  // ----------------------------------------------------------------
  server.on("/status", HTTP_GET, [](){
    DynamicJsonDocument sdoc(256);
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char macbuf[18];
    sprintf(macbuf, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    sdoc["device_id"] = DEVICE_ID;
    sdoc["mac"] = String(macbuf);
    sdoc["ip"] = WiFi.localIP().toString();
    sdoc["uptime_ms"] = millis();
    String out;
    serializeJson(sdoc, out);
    server.send(200, "application/json", out);
  });
  
  server.on("/data", HTTP_GET, [](){
    DynamicJsonDocument rdoc(128);
    if (!isnan(currentTemperature)) rdoc["temperature"] = currentTemperature;
    if (!isnan(currentHumidity)) rdoc["humidity"] = currentHumidity;
    if (currentLight >= 0) rdoc["light"] = currentLight;
    rdoc["uptime_ms"] = millis();
    String out;
    serializeJson(rdoc, out);
    server.send(200, "application/json", out);
  });
  
  server.begin();
  Serial.println("Web server started for /data and /status endpoints.");


  // ----------------------------------------------------------------
  // Khởi tạo Firebase
  // ----------------------------------------------------------------
  Serial.println("Initializing Firebase...");
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.token_status_callback = tokenStatusCallback;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // ----------------------------------------------------------------
  // Cấu hình từ App (RTDB)
  // ----------------------------------------------------------------
  String configPath = "devices/" + DEVICE_ID + "/config";
  if (!Firebase.RTDB.beginStream(&stream_fbdo, configPath.c_str())) {
    Serial.println("!!! FAILED to begin RTDB stream: " + stream_fbdo.errorReason());
  }
  Firebase.RTDB.setStreamCallback(&stream_fbdo, streamCallback, streamTimeoutCallback);
  Serial.println(">>> Firebase Stream setup. Listening for config at:");
  Serial.println(configPath);
}

void loop() {
  server.handleClient(); 

  if (Firebase.ready() && (millis() - last_sent_time > send_interval)) {
    last_sent_time = millis();
    read_and_publish_data(); 
  }
}