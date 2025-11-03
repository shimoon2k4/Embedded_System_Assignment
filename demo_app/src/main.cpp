// ===== Unified ESP32 IoT Demo: DHT20 (I2C) + LDR + Flame (LM393) + MQ-2 =====
// WiFi config page (AP fallback) + Preferences + HTTP Dashboard + REST + MQTT

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// --- DHT20 (AHT20) ---
#include <Wire.h>
#include <Adafruit_AHTX0.h>

// ---------------- Pins (Yolo UNO / ESP32-S3) ----------------
// DHT20 dùng I2C, không cần chân data riêng
#define I2C_SDA_PIN   11      // SDA theo pinout Yolo UNO
#define I2C_SCL_PIN   12      // SCL theo pinout Yolo UNO

#define LED_PIN       2
#define FLAME_DO_PIN 10       // D7 -> GPIO10 (LM393 DO) - LOW = phát hiện lửa
#define MQ2_AO_PIN    1       // A0 -> GPIO1 (ADC1_CH0)
#define LDR_PIN       2       // A1 -> GPIO2 (ADC1_CH1)

// ---------- MQ-2 params / ADC ----------
const int   N_SAMPLES       = 20;      // số mẫu trung bình ADC MQ-2
const int   MQ2_THRESHOLD   = 2000;    // ngưỡng cảnh báo gas (chỉnh theo thực tế)

// ---------- MQTT defaults ----------
const char* MQTT_BROKER = "test.mosquitto.org";
const uint16_t MQTT_PORT = 1883;

// ---------- Globals ----------
Adafruit_AHTX0 aht;                    // đối tượng cho DHT20/AHT20

WebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Preferences preferences;

String wifi_ssid = "";
String wifi_pass = "";
unsigned long send_interval = 5000;   // ms
unsigned long last_sent_time = 0;
String DEVICE_ID;

// Last-known readings (HTTP /data dùng)
float currentTemperature = NAN;
float currentHumidity    = NAN;
int   currentLight       = -1;
bool  currentFlame       = false;
int   currentMQ2ADC      = -1;
bool  currentGas         = false;

// ---------- Forward decl ----------
void setup_wifi_ap_mode();
void connect_to_wifi();
void mqtt_callback(char* topic, byte* payload, unsigned int length);
void reconnect_mqtt();
void read_all_sensors_and_publish(bool publishMqtt);
void handle_root();
void handle_save();

// Helper: device id from MAC
String build_device_id() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[32];
  sprintf(buf, "esp32-%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String(buf);
}

// ----- MQ-2 averaging -----
static int readAvgADC(int pin, int n = N_SAMPLES) {
  long s = 0;
  for (int i = 0; i < n; ++i) {
    s += analogRead(pin);
    delay(5);
  }
  return (int)(s / n);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(FLAME_DO_PIN, INPUT_PULLUP);
  pinMode(MQ2_AO_PIN,   INPUT);
  pinMode(LDR_PIN,      INPUT);
  delay(200);

  // ADC config
  analogReadResolution(12);
  analogSetPinAttenuation(MQ2_AO_PIN, ADC_11db);
  analogSetPinAttenuation(LDR_PIN,    ADC_11db);

  // --- I2C + DHT20 (AHT20) ---
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  bool ok = aht.begin(&Wire);   // địa chỉ mặc định 0x38
  if (!ok) {
    Serial.println("AHT20 init FAILED (check SDA/SCL wiring, power).");
  } else {
    Serial.println("AHT20 init OK.");
  }
  delay(1000); // cho sensor ổn định

  // Preferences
  preferences.begin("iot-config", false);
  wifi_ssid = preferences.getString("wifi_ssid", "ACLAB");
  wifi_pass = preferences.getString("wifi_pass", "ACLAB2023");
  send_interval = preferences.getLong("interval", send_interval); // ms

  DEVICE_ID = build_device_id();

  // ---------- HTTP routes ----------
  server.on("/", HTTP_GET, handle_root);
  server.on("/save", HTTP_POST, handle_save);

  server.on("/status", HTTP_GET, [](){
    DynamicJsonDocument sdoc(256);
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char macbuf[18];
    sprintf(macbuf, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    sdoc["device_id"] = DEVICE_ID;
    sdoc["mac"] = String(macbuf);
    sdoc["ip"] = WiFi.localIP().toString();
    sdoc["uptime_ms"] = millis();
    String out; serializeJson(sdoc, out);
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(200, "application/json", out);
  });

  // /data: tổng hợp tất cả cảm biến
  server.on("/data", HTTP_GET, [](){
    DynamicJsonDocument rdoc(256);
    if (!isnan(currentTemperature)) rdoc["temperature"] = currentTemperature;
    if (!isnan(currentHumidity))    rdoc["humidity"]    = currentHumidity;
    if (currentLight >= 0)          rdoc["light"]       = currentLight;
    rdoc["flame"]   = currentFlame;
    if (currentMQ2ADC >= 0)         rdoc["mq2_adc"]     = currentMQ2ADC;
    rdoc["gas"]     = currentGas;
    rdoc["uptime_ms"] = millis();
    rdoc["ts_epoch"]  = (uint64_t)(millis()); // dùng uptime giả epoch
    String out; serializeJson(rdoc, out);
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(200, "application/json", out);
  });

  // CORS preflight
  server.on("/data", HTTP_OPTIONS, [](){
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204, "text/plain", "");
  });
  server.on("/status", HTTP_OPTIONS, [](){
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204, "text/plain", "");
  });

  // ---------- MQTT ----------
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqtt_callback);

  // ---------- WiFi ----------
  if (wifi_ssid.length() == 0) {
    Serial.println("No WiFi config found. Starting AP mode.");
    setup_wifi_ap_mode();
  } else {
    Serial.println("Found WiFi config. Connecting to existing WiFi.");
    connect_to_wifi();
  }

  // Đọc nhanh lần đầu để có dữ liệu cho /data
  read_all_sensors_and_publish(false);
}

void loop() {
  server.handleClient();

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) reconnect_mqtt();
    mqttClient.loop();

    if (millis() - last_sent_time > send_interval) {
      read_all_sensors_and_publish(true);
      last_sent_time = millis();
    }
  } else {
    static unsigned long last_try = 0;
    if (millis() - last_try > 5000) {
      last_try = millis();
      connect_to_wifi();
    }
  }
}

// ------------------ Implementations ------------------

void setup_wifi_ap_mode() {
  const char* ap_ssid = "ESP32-Config";
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid);
  Serial.print("AP Mode started. SSID: "); Serial.println(ap_ssid);
  Serial.print("IP Address: "); Serial.println(WiFi.softAPIP());
  server.begin();
}

void connect_to_wifi() {
  Serial.print("Connecting to: "); Serial.println(wifi_ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    Serial.print('.');
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP Address: "); Serial.println(WiFi.localIP());
    server.begin();
    Serial.println("Web server started on STA interface.");
  } else {
    Serial.println("\nFailed to connect. Starting AP mode.");
    preferences.remove("wifi_ssid");
    preferences.remove("wifi_pass");
    setup_wifi_ap_mode();
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  Serial.print("MQTT msg: "); Serial.println(message);

  DynamicJsonDocument doc(256);
  auto err = deserializeJson(doc, message);
  if (err) return;

  if (doc["interval"].is<long>()) {
    long new_interval = doc["interval"].as<long>();
    if (new_interval > 0) {
      // new_interval giả định là ms
      send_interval = new_interval;
      preferences.putLong("interval", send_interval);
      Serial.print("Updated interval (ms): "); Serial.println(send_interval);
    }
  }

  if (doc["wifi_ssid"].is<const char*>() && doc["wifi_pass"].is<const char*>()) {
    wifi_ssid = String(doc["wifi_ssid"].as<const char*>());
    wifi_pass = String(doc["wifi_pass"].as<const char*>());
    preferences.putString("wifi_ssid", wifi_ssid);
    preferences.putString("wifi_pass", wifi_pass);
    Serial.println("Saved new WiFi credentials. Restarting...");
    delay(1000);
    ESP.restart();
  }
}

void reconnect_mqtt() {
  if (String(MQTT_BROKER).length() == 0) return;
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (mqttClient.connect(DEVICE_ID.c_str())) {
      Serial.println("connected");
      String topic = "iot/device/" + DEVICE_ID + "/config";
      mqttClient.subscribe(topic.c_str());
    } else {
      Serial.print("failed, rc="); Serial.println(mqttClient.state());
      delay(5000);
    }
  }
}

void read_all_sensors_and_publish(bool publishMqtt) {
  // --- DHT20 (AHT20) ---
  sensors_event_t humidity, temp;
  bool ok = aht.getEvent(&humidity, &temp); // đọc cả hai
  if (ok) {
    currentTemperature = temp.temperature;                  // °C
    currentHumidity    = humidity.relative_humidity;        // %
  } else {
    Serial.println("Failed to read AHT20 (check wiring)");
  }

  // LDR (A1/GPIO2)
  currentLight = analogRead(LDR_PIN);

  // Flame (LM393 DO): LOW = phát hiện lửa
  int rawFlame = digitalRead(FLAME_DO_PIN);
  currentFlame = (rawFlame == LOW);

  // MQ-2 ADC (trung bình, A0/GPIO1)
  currentMQ2ADC = readAvgADC(MQ2_AO_PIN);
  currentGas    = (currentMQ2ADC > MQ2_THRESHOLD);

  Serial.printf("[SENSOR] T=%.1fC H=%.1f%% | LDR=%d | Flame=%s | MQ2=%d Gas=%s\n",
                currentTemperature, currentHumidity, currentLight,
                currentFlame ? "YES" : "NO",
                currentMQ2ADC, currentGas ? "YES" : "NO");

  if (publishMqtt && mqttClient.connected()) {
    DynamicJsonDocument doc(256);
    if (!isnan(currentTemperature)) doc["temperature"] = currentTemperature;
    if (!isnan(currentHumidity))    doc["humidity"]    = currentHumidity;
    if (currentLight >= 0)          doc["light"]       = currentLight;
    doc["flame"]   = currentFlame;
    if (currentMQ2ADC >= 0)         doc["mq2_adc"]     = currentMQ2ADC;
    doc["gas"]     = currentGas;

    char out[256];
    serializeJson(doc, out);
    String data_topic = "iot/device/" + DEVICE_ID + "/data";
    mqttClient.publish(data_topic.c_str(), out);
    Serial.print("Published: "); Serial.println(out);
  }
}

void handle_root() {
  // (Giữ nguyên HTML; chỉ nhãn vẫn đúng vì không ghi tên cảm biến)
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>IoT Sensor Dashboard</title>
  <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.2/dist/css/bootstrap.min.css" rel="stylesheet">
  <style>
    body { background-color: #f8f9fa; }
    .card-title { font-weight: bold; }
    .card-text { font-size: 2.0rem; font-weight: 300; }
    .status-dot { height: 15px; width: 15px; border-radius: 50%; display: inline-block; }
    .connected { background-color: #28a745; }
    .disconnected { background-color: #dc3545; }
    .bad { color: #dc3545; font-weight: 600; }
    .ok  { color: #28a745; }
    .mono{ font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, "Liberation Mono", monospace; }
  </style>
</head>
<body>
  <div class="container mt-4">
    <div class="d-flex justify-content-between align-items-center mb-4">
      <h2>Bảng điều khiển IoT</h2>
      <div>
        <span id="status-dot" class="status-dot disconnected"></span>
        <span id="status-text" class="ms-2">Đang kết nối...</span>
      </div>
    </div>

    <!-- Data Display -->
    <div class="row">
      <div class="col-md-4 mb-4">
        <div class="card text-center">
          <div class="card-header"><h5 class="card-title">Nhiệt độ</h5></div>
          <div class="card-body"><p class="card-text" id="temp-value">--.- &deg;C</p></div>
        </div>
      </div>
      <div class="col-md-4 mb-4">
        <div class="card text-center">
          <div class="card-header"><h5 class="card-title">Độ ẩm</h5></div>
          <div class="card-body"><p class="card-text" id="hum-value">--.- %</p></div>
        </div>
      </div>
      <div class="col-md-4 mb-4">
        <div class="card text-center">
          <div class="card-header"><h5 class="card-title">Ánh sáng (A1/GPIO2)</h5></div>
          <div class="card-body"><p class="card-text" id="light-value">----</p></div>
        </div>
      </div>

      <div class="col-md-4 mb-4">
        <div class="card text-center">
          <div class="card-header"><h5 class="card-title">Lửa (LM393)</h5></div>
          <div class="card-body">
            <p class="card-text"><span id="flame" class="ok">--</span></p>
          </div>
        </div>
      </div>
      <div class="col-md-4 mb-4">
        <div class="card text-center">
          <div class="card-header"><h5 class="card-title">MQ-2 ADC (A0/GPIO1)</h5></div>
          <div class="card-body"><p class="card-text mono" id="mq2-adc">----</p></div>
        </div>
      </div>
      <div class="col-md-4 mb-4">
        <div class="card text-center">
          <div class="card-header"><h5 class="card-title">Gas</h5></div>
          <div class="card-body"><p class="card-text"><span id="gas" class="ok">--</span></p></div>
          <div class="card-footer"><small>Ngưỡng: <code id="thresh">2000</code></small></div>
        </div>
      </div>
    </div>

    <!-- Charts -->
    <div class="row">
      <div class="col-lg-12 mb-4">
        <div class="card">
          <div class="card-header"><h5 class="card-title">Biểu đồ Nhiệt độ & Độ ẩm</h5></div>
          <div class="card-body"><canvas id="tempHumChart"></canvas></div>
        </div>
      </div>
    </div>

    <!-- Configuration -->
    <div class="row">
      <div class="col-lg-12 mb-4">
        <div class="card">
          <div class="card-header"><h5 class="card-title">Cấu hình thiết bị</h5></div>
          <div class="card-body">
            <form id="config-form">
              <div class="row">
                <div class="col-md-6 mb-3">
                  <label class="form-label">Tên WiFi (SSID)</label>
                  <input type="text" class="form-control" id="wifi-ssid" placeholder="Để trống nếu không muốn đổi">
                </div>
                <div class="col-md-6 mb-3">
                  <label class="form-label">Mật khẩu WiFi</label>
                  <input type="password" class="form-control" id="wifi-pass" placeholder="Để trống nếu không muốn đổi">
                </div>
              </div>
              <div class="row">
                <div class="col-md-6 mb-3">
                  <label class="form-label">ID Thiết bị</label>
                  <input type="text" class="form-control" id="device-id" disabled>
                </div>
                <div class="col-md-6 mb-3">
                  <label class="form-label">Chu kỳ gửi dữ liệu (giây)</label>
                  <input type="number" class="form-control" id="send-interval" placeholder="ví dụ: 30">
                </div>
              </div>
              <button type="submit" class="btn btn-primary">Cập nhật cấu hình</button>
            </form>
          </div>
        </div>
      </div>
    </div>
  </div>

  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <script>
    const MAX_CHART_POINTS = 20;
    const statusDot = document.getElementById('status-dot');
    const statusText = document.getElementById('status-text');
    const tempValue = document.getElementById('temp-value');
    const humValue  = document.getElementById('hum-value');
    const lightValue= document.getElementById('light-value');
    const flameEl   = document.getElementById('flame');
    const mq2El     = document.getElementById('mq2-adc');
    const gasEl     = document.getElementById('gas');
    document.getElementById('thresh').textContent = '2000';

    const ctx = document.getElementById('tempHumChart').getContext('2d');
    const tempHumChart = new Chart(ctx, {
      type: 'line',
      data: { labels: [], datasets: [
        { label: 'Nhiệt độ (°C)', data: [], borderColor: 'rgba(255,99,132,1)', backgroundColor: 'rgba(255,99,132,0.2)', yAxisID: 'y-temp' },
        { label: 'Độ ẩm (%)', data: [], borderColor: 'rgba(54,162,235,1)', backgroundColor: 'rgba(54,162,235,0.2)', yAxisID: 'y-hum' }
      ]},
      options: { responsive: true, scales: {
        x: { title: { display: true, text: 'Thời gian' } },
        'y-temp': { type: 'linear', position: 'left', title: { display: true, text: 'Nhiệt độ (°C)' } },
        'y-hum': { type: 'linear', position: 'right', grid: { drawOnChartArea: false }, title: { display: true, text: 'Độ ẩm (%)' } }
      }}
    });

    async function pollLocalData(){
      try{
        const res = await fetch('/data', {cache:'no-store'});
        if(!res.ok) throw new Error('HTTP '+res.status);
        const d = await res.json();
        const t = d.temperature, h = d.humidity, l = d.light;
        const flame = !!d.flame;
        const mq2   = d.mq2_adc ?? null;
        const gas   = !!d.gas;

        const now = new Date(); const label = now.toLocaleTimeString();
        if (typeof t === 'number') tempValue.innerHTML = `${t.toFixed(1)} &deg;C`;
        if (typeof h === 'number') humValue.innerHTML  = `${h.toFixed(1)} %`;
        if (typeof l === 'number') lightValue.textContent = l;
        if (typeof mq2 === 'number') mq2El.textContent = mq2;

        flameEl.textContent = flame ? 'PHÁT HIỆN LỬA' : 'Không có';
        flameEl.className = flame ? 'bad' : 'ok';
        gasEl.textContent = gas ? 'CẢNH BÁO GAS' : 'An toàn';
        gasEl.className = gas ? 'bad' : 'ok';

        if (typeof t === 'number' && typeof h === 'number') {
          if (tempHumChart.data.labels.length > MAX_CHART_POINTS) {
            tempHumChart.data.labels.shift();
            tempHumChart.data.datasets.forEach(ds=>ds.data.shift());
          }
          tempHumChart.data.labels.push(label);
          tempHumChart.data.datasets[0].data.push(t);
          tempHumChart.data.datasets[1].data.push(h);
          tempHumChart.update();
        }

        statusDot.classList.remove('disconnected'); statusDot.classList.add('connected');
        statusText.textContent = 'Đã kết nối (HTTP)';
      }catch(e){
        console.error('Poll error', e);
        statusDot.classList.remove('connected'); statusDot.classList.add('disconnected');
        statusText.textContent = 'HTTP lỗi';
      }
    }

    // simple config form -> POST /save
    document.getElementById('config-form').addEventListener('submit', async (ev)=>{
      ev.preventDefault();
      const ssid = document.getElementById('wifi-ssid').value.trim();
      const pass = document.getElementById('wifi-pass').value.trim();
      const interval = document.getElementById('send-interval').value.trim();
      const fd = new FormData();
      if (ssid.length) fd.append('ssid', ssid);
      if (pass.length) fd.append('pass', pass);
      if (interval.length) fd.append('interval', interval);
      const r = await fetch('/save', { method:'POST', body: fd });
      const txt = await r.text();
      alert(txt);
    });

    // init
    window.addEventListener('load', ()=>{
      pollLocalData();
      setInterval(pollLocalData, 2000);
      fetch('/status').then(r=>r.json()).then(s=>{
        const el = document.getElementById('device-id');
        if (s && s.device_id) el.value = s.device_id;
      }).catch(()=>{});
    });
  </script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handle_save() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  String interval = server.arg("interval");

  if (interval.length() > 0) {
    long iv = interval.toInt();
    if (iv > 0) {
      send_interval = (unsigned long)iv * 1000UL; // giây -> ms
      preferences.putLong("interval", send_interval);
    }
  }

  if (ssid.length() > 0) {
    preferences.putString("wifi_ssid", ssid);
    preferences.putString("wifi_pass", pass);
    server.send(200, "text/html", "Saved WiFi/interval. Rebooting...");
    delay(1000);
    ESP.restart();
  } else {
    server.send(200, "text/html", "Saved interval (nếu có). Để đổi WiFi, điền SSID.");
  }
}
