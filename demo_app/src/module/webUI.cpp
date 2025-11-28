#include "webUI.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "sensor.h"

// --- THƯ VIỆN FIREBASE ---
#include <FirebaseESP32.h>

// ==============================================================================
// CẤU HÌNH MẶC ĐỊNH
// ==============================================================================
#define DEFAULT_API_KEY "AIzaSyDw9gghwYr_agyJzMKdKSi42P5aIHcUyIA"
#define DEFAULT_DB_URL  "https://smart-fire-alarm-project-default-rtdb.asia-southeast1.firebasedatabase.app"
#define DEFAULT_FB_EMAIL "admin@gmail.com" 
#define DEFAULT_FB_PASS  "123456"

WebServer server(80);
Preferences preferences;

// --- BIẾN FIREBASE ---
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool firebaseEnabled = false;
unsigned long lastFirebaseSend = 0;
const unsigned long FIREBASE_INTERVAL = 5000;

// --- BIẾN QUẢN LÝ WIFI ---
unsigned long lastWifiCheck = 0;
const unsigned long WIFI_RECONNECT_INTERVAL = 10000; // Thử kết nối lại sau mỗi 10s nếu mất mạng
bool isAPMode = false; // Biến trạng thái để biết đang ở AP hay STA

// --- HTML Code (Giữ nguyên) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
  <title>Smart Home Monitor</title>
  <link href="https://fonts.googleapis.com/css2?family=Poppins:wght@300;400;500;600&display=swap" rel="stylesheet">
  <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.0.0/css/all.min.css">
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <style>
    /* --- CSS GLOBAL --- */
    :root {
      --primary-orange: #FF5722;
      --light-orange: #FF8A65;
      --bg-color: #FAFAFA;
      --text-color: #37474F;
      --danger-color: #e74c3c;
      --safe-color: #2ecc71;
    }
    * { margin: 0; padding: 0; box-sizing: border-box; font-family: 'Poppins', sans-serif; }
    body { background-color: var(--bg-color); color: var(--text-color); height: 100vh; overflow-x: hidden; }
    
    .hidden { display: none !important; }
    .text-danger { color: var(--danger-color) !important; font-weight: bold; }
    .text-safe { color: var(--safe-color) !important; }

    /* --- LOGIN SCREEN --- */
    #view-login {
      height: 100vh; display: flex; align-items: center; justify-content: center;
      background: #FFFFFF; position: fixed; top: 0; left: 0; width: 100%; z-index: 2000;
    }
    .login-wrapper { width: 100%; max-width: 420px; padding: 40px 30px; text-align: center; }
    
    .logo-box {
      width: 70px; height: 70px; border-radius: 18px;
      background: linear-gradient(135deg, #FF7043, #F4511E);
      color: white; font-size: 32px; line-height: 70px;
      margin: 0 auto 25px;
      box-shadow: 0 10px 20px rgba(244, 81, 30, 0.3);
    }
    
    .login-title { font-size: 26px; font-weight: 600; color: #263238; margin-bottom: 5px; }
    .login-sub { font-size: 14px; color: #90A4AE; margin-bottom: 40px; font-weight: 300; }

    .input-wrapper { margin-bottom: 20px; text-align: left; }
    .input-label { font-size: 13px; color: #546E7A; margin-bottom: 8px; display: block; font-weight: 500; }
    
    .input-field-group { position: relative; }
    .input-field-group i { position: absolute; top: 50%; left: 15px; transform: translateY(-50%); color: #B0BEC5; font-size: 18px; }
    .custom-input {
      width: 100%; padding: 14px 15px 14px 45px;
      border: 1px solid #ECEFF1; border-radius: 12px;
      font-size: 15px; color: #37474F; outline: none; transition: 0.3s;
      background: #FAFAFA;
    }
    .custom-input:focus { border-color: var(--primary-orange); background: #fff; box-shadow: 0 0 0 4px rgba(255, 87, 34, 0.1); }

    .btn-submit {
      width: 100%; padding: 15px; border: none; border-radius: 12px;
      background: linear-gradient(to right, #FF5722, #FF7043);
      color: white; font-size: 16px; font-weight: 600; cursor: pointer;
      box-shadow: 0 8px 15px rgba(255, 87, 34, 0.25);
      transition: transform 0.2s; margin-top: 10px;
    }
    .btn-submit:active { transform: scale(0.98); }

    .divider { margin: 30px 0; position: relative; }
    .divider::before { content: ""; position: absolute; top: 50%; left: 0; width: 100%; height: 1px; background: #ECEFF1; }
    .divider span { position: relative; background: #fff; padding: 0 15px; color: #B0BEC5; font-size: 13px; }

    .btn-google {
      width: 100%; padding: 12px; border: 1px solid #ECEFF1; border-radius: 12px;
      background: white; color: #546E7A; font-weight: 500; font-size: 14px;
      display: flex; align-items: center; justify-content: center; gap: 10px;
      cursor: pointer; transition: 0.3s;
    }
    .btn-google:hover { background: #FAFAFA; border-color: #CFD8DC; }

    /* --- MAIN APP UI --- */
    .app-wrapper { padding-bottom: 80px; } 
    
    .header { padding: 40px 20px 60px; border-radius: 0 0 40px 40px; color: white; text-align: center; box-shadow: 0 10px 20px rgba(0,0,0,0.1); transition: all 0.5s ease; }
    .header.home-mode { background: linear-gradient(135deg, #2ecc71, #27ae60); }
    .header.settings-mode { background: linear-gradient(135deg, #FF7043, #F4511E); }
    .header.danger-mode { background: linear-gradient(135deg, #c0392b, #e74c3c); animation: pulse-red 1s infinite; }
    @keyframes pulse-red { 0% { box-shadow: 0 0 0 0 rgba(231, 76, 60, 0.7); } 70% { box-shadow: 0 0 0 20px rgba(231, 76, 60, 0); } 100% { box-shadow: 0 0 0 0 rgba(231, 76, 60, 0); } }
    
    .status-icon-large { font-size: 50px; background: rgba(255,255,255,0.2); width: 80px; height: 80px; line-height: 80px; border-radius: 50%; margin: 0 auto 15px; backdrop-filter: blur(5px); }
    .main-title { font-size: 22px; font-weight: 600; margin-bottom: 5px; }
    .sub-title { font-size: 13px; opacity: 0.9; font-weight: 300; }
    
    .container { padding: 20px; margin-top: -30px; }
    .grid-container { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; }
    .card { background: white; border-radius: 20px; padding: 20px; text-align: center; box-shadow: 0 5px 15px rgba(0,0,0,0.05); display: flex; flex-direction: column; align-items: center; justify-content: center; }
    .card-icon { font-size: 24px; margin-bottom: 10px; }
    .card-label { font-size: 12px; color: #90A4AE; margin-bottom: 5px; font-weight: 500; }
    .card-value { font-size: 18px; font-weight: 600; transition: color 0.3s; }
    
    .icon-temp { color: #e74c3c; } .icon-hum { color: #2980b9; } .icon-smoke { color: #7f8c8d; } .icon-flame { color: #d35400; }
    .chart-card, .settings-card { background: white; border-radius: 20px; padding: 20px; margin-top: 20px; box-shadow: 0 5px 15px rgba(0,0,0,0.05); }
    
    .form-group { margin-bottom: 20px; }
    .form-label { display: block; margin-bottom: 8px; font-weight: 500; color: #546E7A; font-size: 13px; }
    .form-input { width: 100%; padding: 12px 15px; border-radius: 12px; border: 1px solid #ECEFF1; background: #FAFAFA; font-size: 15px; outline: none; transition: 0.3s; }
    .form-input:focus { border-color: var(--primary-orange); background: white; }
    
    .bottom-nav { position: fixed; bottom: 0; left: 0; right: 0; background: white; height: 70px; display: flex; justify-content: space-around; align-items: center; border-radius: 30px 30px 0 0; box-shadow: 0 -5px 20px rgba(0,0,0,0.05); z-index: 100; }
    .nav-item { text-align: center; color: #B0BEC5; transition: 0.3s; cursor: pointer; }
    .nav-item.active { color: var(--primary-orange); transform: translateY(-5px); }
    .nav-item i { font-size: 24px; display: block; margin-bottom: 5px; }
    .nav-item span { font-size: 11px; font-weight: 500; }
  </style>
</head>
<body>

  <div id="view-login">
    <div class="login-wrapper">
      <div class="logo-box"><i class="fas fa-user-shield"></i></div>
      <h2 class="login-title">Fire Alarm System</h2>
      <p class="login-sub">Keep your space safe and monitored</p>

      <div class="input-wrapper">
        <label class="input-label">Email</label>
        <div class="input-field-group">
          <i class="far fa-envelope"></i>
          <input type="email" id="login-email" class="custom-input" placeholder="your@email.com">
        </div>
      </div>

      <div class="input-wrapper">
        <label class="input-label">Password</label>
        <div class="input-field-group">
          <i class="fas fa-lock"></i>
          <input type="password" id="login-pass" class="custom-input" placeholder="........">
        </div>
      </div>

      <button class="btn-submit" onclick="handleLogin()">Sign In</button>
      <div class="divider"><span>or</span></div>
      <button class="btn-google" onclick="handleGoogleLogin()">
        <img src="https://img.icons8.com/color/24/000000/google-logo.png" alt="G"> Continue with Google
      </button>
    </div>
  </div>

  <div id="app-content" class="app-wrapper hidden">
    <div id="main-header" class="header home-mode">
      <div id="header-icon" class="status-icon-large"><i class="fas fa-check"></i></div>
      <h1 id="header-title" class="main-title">Hệ Thống An Toàn</h1>
      <p id="header-sub" class="sub-title">Đang giám sát môi trường...</p>
    </div>
    
    <div class="container">
      <div id="view-home">
        <div class="grid-container">
          <div class="card"><i class="fas fa-temperature-high card-icon icon-temp"></i><span class="card-label">Nhiệt độ</span><span class="card-value" id="temp-val">-- °C</span></div>
          <div class="card"><i class="fas fa-tint card-icon icon-hum"></i><span class="card-label">Độ ẩm</span><span class="card-value" id="hum-val">-- %</span></div>
          <div class="card"><i class="fas fa-smog card-icon icon-smoke"></i><span class="card-label">Khói / Gas</span><span class="card-value" id="mq2-val">--</span></div>
          <div class="card"><i class="fas fa-fire card-icon icon-flame"></i><span class="card-label">Cảm biến Lửa</span><span class="card-value" id="flame-val">--</span></div>
        </div>
        <div class="chart-card"><canvas id="envChart"></canvas></div>
      </div>

      <div id="view-settings" class="hidden">
        <div class="settings-card">
          <h3 style="margin-bottom: 20px; color: var(--primary-orange);">Cấu hình Thiết bị</h3>
          <form action="/save" method="POST">
            <div class="form-group"><label class="form-label">Tên WiFi</label><input type="text" name="ssid" class="form-input" value="%CURRENT_SSID%" required></div>
            <div class="form-group"><label class="form-label">Mật khẩu WiFi</label><input type="password" name="pass" class="form-input"></div>
            
            <div class="divider"><span>Firebase Cloud</span></div>
            <div class="form-group"><label class="form-label">API Key</label><input type="text" name="apikey" class="form-input" value="%API_KEY%"></div>
            <div class="form-group"><label class="form-label">DB URL</label><input type="text" name="dburl" class="form-input" value="%DB_URL%"></div>
            
            <div class="form-group"><label class="form-label">Email Xác Thực</label><input type="email" name="fb_email" class="form-input" value="%FB_EMAIL%"></div>
            <div class="form-group"><label class="form-label">Mật khẩu Xác Thực</label><input type="password" name="fb_pass" class="form-input" value="%FB_PASS%"></div>

            <button type="submit" class="btn-save">Lưu & Khởi Động Lại</button>
          </form>
          <div class="divider"><span>Tài khoản</span></div>
          <button class="btn-google" style="color: #e74c3c; border-color: #e74c3c;" onclick="handleLogout()"><i class="fas fa-sign-out-alt"></i> Đăng xuất</button>
        </div>
      </div>
    </div>

    <div class="bottom-nav">
      <div class="nav-item active" onclick="switchTab('home')" id="nav-home"><i class="fas fa-home"></i><span>Trang chủ</span></div>
      <div class="nav-item" onclick="switchTab('settings')" id="nav-settings"><i class="fas fa-cog"></i><span>Cài đặt</span></div>
    </div>
  </div>

  <script>
    const CONFIG_EMAIL = "%FB_EMAIL%";
    const CONFIG_PASS = "%FB_PASS%";
    const DEFAULT_EMAIL = "admin@gmail.com";
    const DEFAULT_PASS = "123456";

    const THRESHOLD_MQ2 = 2000; const THRESHOLD_TEMP = 50;
    let isDangerMode = false; let dangerReason = ""; let dataInterval = null;

    function checkLogin() { if (localStorage.getItem('isLoggedIn') === 'true') showApp(); }
    
    function handleLogin() {
      const email = document.getElementById('login-email').value;
      const pass = document.getElementById('login-pass').value;
      let isValidConfig = (email === CONFIG_EMAIL && pass === CONFIG_PASS && CONFIG_EMAIL !== "");
      let isValidDefault = (email === DEFAULT_EMAIL && pass === DEFAULT_PASS);

      if (isValidConfig || isValidDefault) { localStorage.setItem('isLoggedIn', 'true'); showApp(); } 
      else { alert('Email hoặc mật khẩu không chính xác!'); }
    }
    
    function handleGoogleLogin() { alert('Đăng nhập Google thành công! (Chế độ Demo)'); localStorage.setItem('isLoggedIn', 'true'); showApp(); }
    function handleLogout() { localStorage.removeItem('isLoggedIn'); location.reload(); }
    
    function showApp() {
      document.getElementById('view-login').classList.add('hidden');
      document.getElementById('app-content').classList.remove('hidden');
      initChart(); updateData();
      dataInterval = setInterval(updateData, 2000);
    }

    let envChart; const ctx = document.getElementById('envChart').getContext('2d');
    function initChart() { if (envChart) return; envChart = new Chart(ctx, { type: 'line', data: { labels: [], datasets: [{ label: 'Nhiệt độ', borderColor: '#e74c3c', backgroundColor: 'rgba(231,76,60,0.1)', data: [], tension: 0.4, fill: true }, { label: 'Độ ẩm', borderColor: '#3498db', backgroundColor: 'rgba(52,152,219,0.1)', data: [], tension: 0.4, fill: true }] }, options: { responsive: true, plugins: { legend: { position: 'bottom' } }, scales: { x: { display: false } } } }); }
    
    function switchTab(tab) { 
      if (tab === 'home') { 
        document.getElementById('view-home').classList.remove('hidden'); document.getElementById('view-settings').classList.add('hidden'); 
        document.getElementById('nav-home').classList.add('active'); document.getElementById('nav-settings').classList.remove('active'); 
        updateHeaderVisuals(); 
      } else { 
        document.getElementById('view-home').classList.add('hidden'); document.getElementById('view-settings').classList.remove('hidden'); 
        document.getElementById('nav-home').classList.remove('active'); document.getElementById('nav-settings').classList.add('active'); 
        const h = document.getElementById('main-header'); h.className = 'header settings-mode'; 
        document.getElementById('header-title').innerText = "Cài đặt"; document.getElementById('header-sub').innerText = "Cấu hình hệ thống"; 
        document.getElementById('header-icon').innerHTML = '<i class="fas fa-sliders-h"></i>'; 
      } 
    }

    function updateHeaderVisuals() { 
      if (!document.getElementById('view-home').classList.contains('hidden')) { 
        const h = document.getElementById('main-header'); 
        const hTitle = document.getElementById('header-title'); 
        const hSub = document.getElementById('header-sub'); 
        const hIcon = document.getElementById('header-icon'); 
        if (isDangerMode) { h.className = 'header danger-mode'; hTitle.innerText = "CẢNH BÁO NGUY HIỂM!"; hSub.innerText = dangerReason; hIcon.innerHTML = '<i class="fas fa-exclamation-triangle"></i>'; } 
        else { h.className = 'header home-mode'; hTitle.innerText = "Hệ Thống An Toàn"; hSub.innerText = "Mọi chỉ số đều ổn định"; hIcon.innerHTML = '<i class="fas fa-check"></i>'; } 
      } 
    }

    function updateData() { 
      fetch('/data').then(r => r.json()).then(data => { 
        const tempEl = document.getElementById('temp-val'); tempEl.innerText = data.temperature + " °C"; 
        if (data.temperature > THRESHOLD_TEMP) tempEl.className = "card-value text-danger"; else tempEl.className = "card-value"; 
        document.getElementById('hum-val').innerText = data.humidity + " %"; 
        const mq2El = document.getElementById('mq2-val'); mq2El.innerText = data.mq2_adc + " PPM"; 
        if (data.mq2_adc > THRESHOLD_MQ2) mq2El.className = "card-value text-danger"; else mq2El.className = "card-value"; 
        const flameEl = document.getElementById('flame-val'); const isFire = (data.flame == true || data.flame == 1); 
        flameEl.innerText = isFire ? "PHÁT HIỆN LỬA!" : "An toàn"; 
        if (isFire) flameEl.className = "card-value text-danger"; else flameEl.className = "card-value text-safe"; 
        let reasons = []; if (isFire) reasons.push("LỬA"); if (data.mq2_adc > THRESHOLD_MQ2) reasons.push("KHÓI"); if (data.temperature > THRESHOLD_TEMP) reasons.push("NHIỆT ĐỘ"); 
        if (reasons.length > 0) { isDangerMode = true; dangerReason = "Phát hiện: " + reasons.join(", "); } else { isDangerMode = false; dangerReason = ""; } 
        updateHeaderVisuals(); 
        if (envChart) { const now = new Date().toLocaleTimeString(); if (envChart.data.labels.length > 10) { envChart.data.labels.shift(); envChart.data.datasets.forEach(d => d.data.shift()); } envChart.data.labels.push(now); envChart.data.datasets[0].data.push(data.temperature); envChart.data.datasets[1].data.push(data.humidity); envChart.update(); } 
      }); 
    }
    window.onload = checkLogin;
  </script>
</body>
</html>
)rawliteral";

// --- HÀM KẾT NỐI WIFI VÀ KHỞI ĐỘNG SERVER ---
void connectToWiFi() {
    String ssid = preferences.getString("ssid", "");
    String pass = preferences.getString("pass", "");

    if (ssid == "") {
        Serial.println("Chua co SSID, chuyen sang AP Mode.");
        WiFi.mode(WIFI_AP);
        WiFi.softAP("ESP32_SmartHome", NULL, 6);
        Serial.println("PHAT WIFI: ESP32_SmartHome (IP: 192.168.4.1)");
        isAPMode = true;
        server.begin(); // Khoi dong server o che do AP
        return;
    }

    Serial.print("Dang ket noi WiFi: "); Serial.println(ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    unsigned long startAttemptTime = millis();
    // Thử kết nối trong 20 giây (giống code cũ của bạn)
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 20000) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi Connected!");
        Serial.print("IP Address: "); Serial.println(WiFi.localIP());
        isAPMode = false;

        // --- KHỞI ĐỘNG WEB SERVER ---
        // QUAN TRỌNG: Gọi lại server.begin() để bind vào IP mới
        server.begin(); 
        Serial.println("Web Server Started on STA IP.");

        // --- KẾT NỐI FIREBASE ---
        String apiKey = preferences.getString("apikey", DEFAULT_API_KEY);
        String dbUrl = preferences.getString("dburl", DEFAULT_DB_URL);
        String fbEmail = preferences.getString("fb_email", DEFAULT_FB_EMAIL);
        String fbPass = preferences.getString("fb_pass", DEFAULT_FB_PASS);

        if (apiKey != "" && dbUrl != "") {
            config.api_key = apiKey;
            config.database_url = dbUrl;
            auth.user.email = fbEmail;
            auth.user.password = fbPass;
            Firebase.begin(&config, &auth);
            // Firebase.reconnectWiFi(true);
            firebaseEnabled = true;
            Serial.println("Firebase Enabled.");
        }
    } else {
        Serial.println("\nKet noi that bai! Chuyen sang AP Mode.");
        WiFi.disconnect(true); // Xóa cấu hình cũ
        WiFi.mode(WIFI_AP);
        WiFi.softAP("ESP32_SmartHome", NULL, 6);
        Serial.println("PHAT WIFI: ESP32_SmartHome (IP: 192.168.4.1)");
        isAPMode = true;
        server.begin();
    }
}

void setupWebUI() {
    preferences.begin("wifi-config", false);
    Serial.println("--- KHOI DONG HE THONG ---");

    // Gọi hàm kết nối WiFi (bao gồm cả khởi động Server)
    connectToWiFi();

    // --- CẤU HÌNH ROUTE SERVER ---
    server.on("/", HTTP_GET, [](){
        String html = index_html;
        html.replace("%CURRENT_SSID%", preferences.getString("ssid", ""));
        
        String currentKey = preferences.getString("apikey", DEFAULT_API_KEY);
        String currentUrl = preferences.getString("dburl", DEFAULT_DB_URL);
        String currentEmail = preferences.getString("fb_email", DEFAULT_FB_EMAIL);
        String currentPass = preferences.getString("fb_pass", DEFAULT_FB_PASS);

        html.replace("%API_KEY%", currentKey);
        html.replace("%DB_URL%", currentUrl);
        html.replace("%FB_EMAIL%", currentEmail);
        html.replace("%FB_PASS%", currentPass);
        
        server.send(200, "text/html", html);
    });

    server.on("/data", HTTP_GET, [](){
        DynamicJsonDocument rdoc(512);
        float t = getTemperature(); float h = getHumidity();
        if (isnan(t)) t = 0; if (isnan(h)) h = 0;
        rdoc["temperature"] = t; rdoc["humidity"] = h;
        rdoc["mq2_adc"] = getMQ2ADC(); rdoc["flame"] = getFlame();
        String out; serializeJson(rdoc, out);
        server.send(200, "application/json", out);
    });

    server.on("/save", HTTP_POST, [](){
        if (server.hasArg("ssid")) {
            preferences.putString("ssid", server.arg("ssid"));
            preferences.putString("pass", server.arg("pass"));
            if (server.hasArg("apikey")) preferences.putString("apikey", server.arg("apikey"));
            if (server.hasArg("dburl")) preferences.putString("dburl", server.arg("dburl"));
            if (server.hasArg("fb_email")) preferences.putString("fb_email", server.arg("fb_email"));
            if (server.hasArg("fb_pass")) preferences.putString("fb_pass", server.arg("fb_pass"));
            
            String html = "<html><head><meta charset='UTF-8'></head><body style='text-align:center;padding:50px;font-family:sans-serif'><h1>Đã lưu!</h1><p>Thiết bị đang khởi động lại...</p></body></html>";
            server.send(200, "text/html", html);
            delay(1000); ESP.restart();
        }
    });
    // Thêm đoạn này vào setupWebUI
    server.on("/test", HTTP_GET, [](){
    server.send(200, "text/plain", "HELLO WORLD! KET NOI THANH CONG!");
    });
}

void handleWebRequests() { 
    // 1. LUÔN LUÔN xử lý Client
    server.handleClient(); 
    
    // 2. CHECK KẾT NỐI WIFI LIÊN TỤC (Tương tự code cũ)
    // Nếu không phải chế độ AP và WiFi bị mất kết nối -> Thử kết nối lại
    if (!isAPMode && WiFi.status() != WL_CONNECTED) {
        if (millis() - lastWifiCheck > WIFI_RECONNECT_INTERVAL) {
            lastWifiCheck = millis();
            Serial.println("Mat ket noi WiFi! Dang thu ket noi lai...");
            
            // Gọi lại hàm kết nối (hàm này có chứa server.begin() để hồi sinh server)
            connectToWiFi(); 
        }
    }

    // 3. Xử lý Firebase (chỉ khi có mạng)
    if (firebaseEnabled && WiFi.status() == WL_CONNECTED && (millis() - lastFirebaseSend > FIREBASE_INTERVAL)) {
        lastFirebaseSend = millis();
        FirebaseJson json;
        float t = getTemperature(); if (isnan(t)) t = 0;
        float h = getHumidity(); if (isnan(h)) h = 0;
        json.set("temperature", t); json.set("humidity", h); json.set("gas_value", getMQ2ADC()); json.set("flame_detected", getFlame()); json.set("timestamp", millis()); 
        Firebase.setJSON(fbdo, "/sensor_data", json);
    }
}