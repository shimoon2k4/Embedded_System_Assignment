#include "sensor.h"
#include <Arduino.h>
#include <DHT.h> // Sử dụng thư viện DHT sensor library

// ==============================================================================
// CẤU HÌNH CHÂN CHO ESP32 NODEMCU 32S (DHT11 Version)
// ==============================================================================

// 1. DHT11 (NHIỆT ĐỘ & ĐỘ ẨM)
// DHT11 dùng giao tiếp 1 dây (Single Wire)
#define DHT_PIN       14      // GPIO 14 (Nối chân Data của DHT11 vào đây)
#define DHT_TYPE      DHT11   // Định nghĩa loại cảm biến là DHT11

// 2. CẢM BIẾN LỬA (DIGITAL)
#define FLAME_DO_PIN  27      // GPIO 27

// 3. CẢM BIẾN GAS & ÁNH SÁNG (ANALOG)
// ADC1 (GPIO 32, 33, 34, 35, 36, 39) dùng tốt khi có WiFi
#define MQ2_AO_PIN    34      // GPIO 34 (Input Only)
#define LDR_PIN       35      // GPIO 35 (Input Only)

// ==============================================================================

// --- THAM SỐ CẤU HÌNH ---
const int N_SAMPLES = 20;       
const int MQ2_THRESHOLD = 2000; 

// --- ĐỐI TƯỢNG TOÀN CỤC CỦA MODULE ---
DHT dht(DHT_PIN, DHT_TYPE);

// --- BIẾN TRẠNG THÁI (STATIC) ---
static float temperature = NAN;
static float humidity = NAN;
static int light = -1;
static bool flame = false;
static int mq2adc = -1;
static bool gas = false;

// --- HÀM PHỤ TRỢ (PRIVATE) ---
static int readAvgADC(int pin, int n = N_SAMPLES) {
    long s = 0;
    for (int i = 0; i < n; ++i) {
        s += analogRead(pin);
        delay(2); 
    }
    return (int)(s / n);
}

// --- HÀM KHỞI TẠO (PUBLIC) ---
void initSensors() {
    Serial.println("--- KHOI TAO CAM BIEN (DHT11 - ESP32) ---");
    
    // 1. Cấu hình chân
    pinMode(FLAME_DO_PIN, INPUT_PULLUP);
    pinMode(MQ2_AO_PIN, INPUT);
    pinMode(LDR_PIN, INPUT);

    // 2. Cấu hình ADC
    analogReadResolution(12); // 0 - 4095
    analogSetPinAttenuation(MQ2_AO_PIN, ADC_11db);
    analogSetPinAttenuation(LDR_PIN, ADC_11db);

    // 3. Khởi tạo DHT11
    dht.begin();
    
    // Test thử đọc dữ liệu ban đầu
    delay(1000); // DHT11 cần thời gian khởi động lâu hơn chút
    float t = dht.readTemperature();
    if (isnan(t)) {
        Serial.println("LOI: Khong tim thay DHT11. Vui long kiem tra chan GPIO 14!");
    } else {
        Serial.println("DHT11 khoi tao OK.");
    }
}

// --- HÀM ĐỌC DỮ LIỆU (PUBLIC) ---
void readSensors() {
    // 1. Đọc DHT11
    // Lưu ý: DHT11 đọc khá chậm (tối đa 1Hz), thư viện sẽ tự xử lý cache
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    // Kiểm tra lỗi đọc (isnan)
    if (!isnan(t) && !isnan(h)) {
        temperature = t;
        humidity = h;
    } else {
        Serial.println("Loi doc DHT11 (Check wiring)");
        // Giữ nguyên giá trị cũ hoặc gán NAN tuỳ logic của bạn
        // temperature = NAN; 
        // humidity = NAN;
    }

    // 2. Đọc Ánh sáng (LDR) - Chân 35
    light = analogRead(LDR_PIN);

    // 3. Đọc Lửa (Flame) - Chân 27
    int rawFlame = digitalRead(FLAME_DO_PIN);
    flame = (rawFlame == LOW); // LOW thường là có lửa

    // 4. Đọc Gas (MQ-2) - Chân 34
    mq2adc = readAvgADC(MQ2_AO_PIN);
    gas = (mq2adc > MQ2_THRESHOLD);

    // Debug ra Serial
    Serial.printf("[SENSOR] T=%.1f C | H=%.1f %% | Light=%d | Flame=%s | MQ2=%d\n", 
                  temperature, humidity, light, flame ? "YES" : "NO", mq2adc);
}

// --- CÁC HÀM GETTER ---
float getTemperature() { return temperature; }
float getHumidity()    { return humidity; }
int getLight()         { return light; }
bool getFlame()        { return flame; }
int getMQ2ADC()        { return mq2adc; }
bool getGas()          { return gas; }