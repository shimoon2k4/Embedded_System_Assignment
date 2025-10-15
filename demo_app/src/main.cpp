// Single coherent demo sketch combining DHT reading, LDR, WiFi config page, Preferences and MQTT
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <Preferences.h>

// ----- Pin map -----
// Change if you wired differently
#define DHTPIN   14
#define DHTTYPE  DHT11
#define LED_PIN  2
#define LDR_PIN  34

// MQTT defaults (change to your broker)
const char* MQTT_BROKER = "test.mosquitto.org";
const uint16_t MQTT_PORT = 1883;

// Globals
DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Preferences preferences;

String wifi_ssid = "";
String wifi_pass = "";
unsigned long send_interval = 30000;
unsigned long last_sent_time = 0;
String DEVICE_ID;
// latest sensor readings (shared with web endpoint)
float currentTemperature = NAN;
float currentHumidity = NAN;
int currentLight = -1;

// Forward declarations
void setup_wifi_ap_mode();
void connect_to_wifi();
void mqtt_callback(char* topic, byte* payload, unsigned int length);
void reconnect_mqtt();
void read_and_publish_data();
void handle_root();
void handle_save();

// Helper to build device id from MAC
String build_device_id() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[32];
  sprintf(buf, "esp32-%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String(buf);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LDR_PIN, INPUT);
  delay(200);

  dht.begin();

  // Preferences
  preferences.begin("iot-config", false);
  wifi_ssid = preferences.getString("wifi_ssid", "Redmi Note 11S");
  wifi_pass = preferences.getString("wifi_pass", "shimoon0852");
  send_interval = preferences.getLong("interval", 30000);

  DEVICE_ID = build_device_id();

  // Start server routes
  server.on("/", HTTP_GET, handle_root);
  server.on("/save", HTTP_POST, handle_save);
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
    Serial.println("HTTP /status requested");
    server.send(200, "application/json", out);
  });
  // data endpoint returns the latest sensor readings as JSON
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
  // Do not start server here; start it after WiFi interface is ready (in connect_to_wifi or AP mode)

  // Setup MQTT client
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqtt_callback);

  if (wifi_ssid.length() == 0) {
    Serial.println("No WiFi config found. Starting AP mode.");
    setup_wifi_ap_mode();
  } else {
    Serial.println("Found WiFi config. Connecting to existing WiFi.");
    connect_to_wifi();
  }
}

void loop() {
  // Always handle HTTP requests (server runs in AP or STA)
  server.handleClient();

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) reconnect_mqtt();
    mqttClient.loop();

    if (millis() - last_sent_time > send_interval) {
      read_and_publish_data();
      last_sent_time = millis();
    }
  } else {
    // Try reconnecting occasionally
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
    // Start web server now that STA interface is active
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

  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  DynamicJsonDocument doc(256);
  #pragma GCC diagnostic pop
  auto err = deserializeJson(doc, message);
  if (err) return;

  if (doc["interval"].is<long>()) {
    long new_interval = doc["interval"].as<long>();
    if (new_interval > 0) {
      send_interval = new_interval;
      preferences.putLong("interval", send_interval);
      Serial.print("Updated interval: "); Serial.println(send_interval);
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
  if (String(MQTT_BROKER).length() == 0) return; // no broker
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

void read_and_publish_data() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int l = analogRead(LDR_PIN);

  if (isnan(h) || isnan(t)) {
    Serial.println("Failed to read DHT");
    return;
  }

  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  DynamicJsonDocument doc(128);
  #pragma GCC diagnostic pop
  doc["temperature"] = t;
  doc["humidity"] = h;
  doc["light"] = l;
  char out[128];
  serializeJson(doc, out);

  String data_topic = "iot/device/" + DEVICE_ID + "/data";
  if (mqttClient.connected()) mqttClient.publish(data_topic.c_str(), out);
  Serial.print("Published: "); Serial.println(out);
}

void handle_root() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Dashboard</title>
  <style>body{font-family:Arial; padding:10px;} .card{padding:12px;background:#f7f7f7;border-radius:8px;margin:8px 0}</style>
  <script>
    async function fetchData(){
      try{
        const res = await fetch('/data');
        if(!res.ok) return;
        const j = await res.json();
        document.getElementById('temp').innerText = j.temperature ?? 'n/a';
        document.getElementById('hum').innerText = j.humidity ?? 'n/a';
        document.getElementById('light').innerText = j.light ?? 'n/a';
        document.getElementById('uptime').innerText = j.uptime_ms ?? 'n/a';
      }catch(e){ console.log(e); }
    }
    setInterval(fetchData, 2000);
    window.onload = fetchData;
  </script>
</head>
<body>
  <h2>ESP32 Dashboard</h2>
  <div class="card">Temperature: <b id="temp">-</b> &deg;C</div>
  <div class="card">Humidity: <b id="hum">-</b> %</div>
  <div class="card">Light: <b id="light">-</b></div>
  <div class="card">Uptime (ms): <b id="uptime">-</b></div>
  <hr>
  <h3>WiFi Config</h3>
  <form method="POST" action="/save">
    SSID: <input name="ssid" /><br/>
    Password: <input name="pass" /><br/>
    <input type="submit" value="Save" />
  </form>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handle_save() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length() > 0) {
    preferences.putString("wifi_ssid", ssid);
    preferences.putString("wifi_pass", pass);
    server.send(200, "text/html", "Saved. Rebooting...");
    delay(1000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "SSID required");
  }
}
