#include <DHT.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// --- LIBRARY EDGE IMPULSE ---
#include <TralaliloTroliaIO_inferencing.h> 

// --- KONFIGURASI I2C OLED ---
#define I2C_SDA 21
#define I2C_SCL 22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- KONFIGURASI MQTT ---
const char* mqtt_server = "broker.hivemq.com";
const char* mqtt_topic_sensor = "project/tralalilo_trolia/sensor";
const char* mqtt_topic_control = "project/tralalilo_trolia/control"; // TOPIC KONTROL

// --- KONFIGURASI API WAQI ---
const char* waqi_url = "https://api.waqi.info/feed/A416815/?token=6c3c2225a38fb8ba47d81291d60a9fdcc7fde07a";

// --- CLIENT OBJECTS ---
WiFiClient espClient; 
PubSubClient client(espClient);

// --- PIN DEFINITIONS ---
#define DHTPIN        4
#define DHTTYPE       DHT22
#define PIN_DUST_LED  12
#define PIN_DUST_ANA  32
#define PIN_MQ7       35
#define PIN_BUZZER    13

// LED RGB
#define RGB_R         14
#define RGB_G         2
#define RGB_B         15

// --- KALIBRASI SENSOR ---
#define V_REF         3.3
#define ADC_RES       4095.0
#define DIVIDER       1.5 
#define DUST_VOC      0.6 
#define CO_CONV       1.15 

DHT dht(DHTPIN, DHTTYPE);
float R0 = 10.0;
unsigned long lastMsgTime = 0;
const long intervalMsg = 5000; 

// Variabel Data Global
float g_temp=0, g_hum=0, g_co=0, g_pm25=0;
float g_no2=0, g_pm10=0, g_so2=0, g_o3=0;
String g_ai_label = "Menunggu...";
float g_ai_score = 0.0;

// Flag Kontrol Manual
bool manual_alert = false;

// ==========================================
// 0. FUNGSI HELPER
// ==========================================
float readADC_Voltage(int pin) {
  int raw = analogRead(pin);
  float v_pin = raw * (V_REF / ADC_RES);
  return v_pin * DIVIDER;
}

float roundTwoDecimals(float val) {
  return (int)(val * 100 + 0.5) / 100.0;
}

// ==========================================
// 1. FUNGSI SENSOR & AKTUATOR
// ==========================================

void setRGB(int r, int g, int b) {
  analogWrite(RGB_R, r);
  analogWrite(RGB_G, g);
  analogWrite(RGB_B, b);
}

void updateActuators(String label, float score) {
  // JIKA MODE MANUAL AKTIF, ABAIKAN LOGIKA OTOMATIS
  if (manual_alert) {
    return; 
  }

  digitalWrite(PIN_BUZZER, LOW); 

  if (label == "BAIK") setRGB(0, 255, 0); 
  else if (label == "SEDANG") setRGB(0, 0, 255); 
  else if (label == "TIDAK SEHAT") setRGB(255, 140, 0); 
  else if (label == "SANGAT TIDAK SEHAT") setRGB(255, 0, 0); 
  else if (label == "BERBAHAYA") setRGB(255, 0, 255); 
  else setRGB(0, 0, 0); 

  bool conditionBad = (label == "TIDAK SEHAT" || label == "SANGAT TIDAK SEHAT" || label == "BERBAHAYA");
  if (conditionBad && score > 0.70) {
      digitalWrite(PIN_BUZZER, HIGH);
  } else {
      digitalWrite(PIN_BUZZER, LOW);
  }
}

float get_dust_density() {
  float total_volt = 0;
  for(int i=0; i<10; i++) {
      digitalWrite(PIN_DUST_LED, LOW);
      delayMicroseconds(280);
      int raw = analogRead(PIN_DUST_ANA);
      total_volt += raw * (V_REF / ADC_RES) * DIVIDER;
      delayMicroseconds(40);
      digitalWrite(PIN_DUST_LED, HIGH);
      delayMicroseconds(9680);
  }
  float avg_volt = total_volt / 10.0;
  if (avg_volt > DUST_VOC) {
      return (avg_volt - DUST_VOC) * 200.0;
  }
  return 0.0;
}

float get_co_concentration() {
  float volt = readADC_Voltage(PIN_MQ7);
  if(volt <= 0.1) return 0.0;
  float Rs = ((5.0 * 10.0) / volt) - 10.0;
  float ratio = Rs / R0;
  float ppm = 100.0 * pow(ratio, -1.47);
  return ppm * 1.15; 
}

// ==========================================
// 2. MQTT CALLBACK (UNTUK MENERIMA PERINTAH)
// ==========================================
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("Pesan masuk: ");
  Serial.println(message);

  if (String(topic) == mqtt_topic_control) {
    if (message == "ALERT") {
      manual_alert = true;
      setRGB(255, 0, 255); // UNGU
      digitalWrite(PIN_BUZZER, HIGH); // NYALA
      Serial.println("MODE MANUAL: ALARM ON (UNGU)");
    } 
    else if (message == "NORMAL") {
      manual_alert = false;
      digitalWrite(PIN_BUZZER, LOW); // MATI
      setRGB(255, 0, 0); // MERAH (Sesuai Request)
      Serial.println("MODE MANUAL: RESET (MERAH -> AUTO)");
      // Note: Warna Merah ini akan bertahan sampai update sensor berikutnya (maks 5 detik)
    }
  }
}

// ==========================================
// 3. FUNGSI AI
// ==========================================
void run_ai_inference() {
    float features[] = { g_pm10, g_pm25, g_so2, g_co, g_o3, g_no2 };

    if (sizeof(features) / sizeof(float) != EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) return;

    signal_t signal;
    int err = numpy::signal_from_buffer(features, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &signal);
    if (err != 0) return;

    ei_impulse_result_t result = { 0 };
    err = run_classifier(&signal, &result, false);
    if (err != EI_IMPULSE_OK) return;

    float max_score = 0.0;
    String best_label = "Unknown";

    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        if (result.classification[ix].value > max_score) {
            max_score = result.classification[ix].value;
            best_label = String(result.classification[ix].label);
        }
    }
    
    g_ai_label = best_label;
    g_ai_score = max_score; 
    
    // Hanya update aktuator jika TIDAK dalam mode manual
    if (!manual_alert) {
      updateActuators(g_ai_label, g_ai_score);
    }
}

// ==========================================
// 4. SETUP & LOOP
// ==========================================

void reconnect() {
  while (!client.connected()) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Connecting MQTT...");
    display.display();
    
    String clientId = "ESP32-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      display.println("Connected!");
      display.display();
      // SUBSCRIBE KE TOPIC KONTROL
      client.subscribe(mqtt_topic_control);
      delay(500);
    } else {
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 Fail"));
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  pinMode(PIN_DUST_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(RGB_R, OUTPUT);
  pinMode(RGB_G, OUTPUT);
  pinMode(RGB_B, OUTPUT);
  
  digitalWrite(PIN_DUST_LED, HIGH);
  digitalWrite(PIN_BUZZER, LOW);
  dht.begin();

  setRGB(255, 255, 255); delay(500); setRGB(0, 0, 0);

  WiFiManager wm;
  display.setCursor(0,0);
  display.println("Connect WiFi:");
  display.println("AP: ESP32-SENSOR");
  display.display();
  
  bool res = wm.autoConnect("ESP32-SENSOR");
  if(!res) ESP.restart();

  display.clearDisplay();
  display.setCursor(0,0);
  display.println("WiFi Connected!");
  display.print("IP: "); display.println(WiFi.localIP());
  display.display();
  delay(2000);

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback); // Register Callback
  client.setBufferSize(2048); 

  float raw_mq = readADC_Voltage(PIN_MQ7);
  if (raw_mq > 0.1) {
     float Rs = ((5.0 * 10.0) / raw_mq) - 10.0;
     R0 = Rs / 27.0;
  }
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop(); // Wajib untuk menerima pesan MQTT

  unsigned long now = millis();
  if (now - lastMsgTime > intervalMsg) {
    lastMsgTime = now;

    // 1. Baca Sensor Lokal
    g_temp = dht.readTemperature();
    g_hum = dht.readHumidity();
    g_pm25 = get_dust_density();
    g_co = get_co_concentration(); 

    // 2. Fetch API & Process AI
    bool apiSuccess = false;
    
    if(WiFi.status() == WL_CONNECTED) {
        WiFiClientSecure httpsClient;
        httpsClient.setInsecure();
        HTTPClient http;
        http.setTimeout(3000);
        
        if (http.begin(httpsClient, waqi_url)) {
            int httpCode = http.GET();
            if (httpCode > 0) {
                String payload = http.getString();
                DynamicJsonDocument doc(4096);
                DeserializationError error = deserializeJson(doc, payload);
                
                if (!error && doc["status"] == "ok") {
                    g_no2  = doc["data"]["iaqi"]["no2"]["v"];
                    g_o3   = doc["data"]["iaqi"]["o3"]["v"];
                    g_pm10 = doc["data"]["iaqi"]["pm10"]["v"];
                    g_so2  = doc["data"]["iaqi"]["so2"]["v"];
                    apiSuccess = true; 
                }
            }
            http.end();
        }
    }

    if (apiSuccess && !isnan(g_temp) && !isnan(g_hum)) {
        
        run_ai_inference(); 

        // Update OLED
        display.clearDisplay();
        display.setCursor(0, 0);
        
        if (manual_alert) {
           display.println("MODE: MANUAL");
           display.println("ALARM ON!");
        } else {
           display.print("AI: "); display.println(g_ai_label);
           display.setCursor(80, 0);
           display.print((int)(g_ai_score * 100)); display.println("%");
        }
        
        display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
        display.setCursor(0, 15);
        display.printf("T:%.1f H:%.0f%%\n", g_temp, g_hum);
        display.printf("CO:%.2f mg\n", g_co);
        display.printf("PM2.5:%.1f ug\n", g_pm25);
        display.display();

        // Kirim MQTT
        DynamicJsonDocument jsonDoc(1024);
        jsonDoc["suhu"] = roundTwoDecimals(g_temp);
        jsonDoc["kelembaban"] = roundTwoDecimals(g_hum);
        jsonDoc["co"] = roundTwoDecimals(g_co);
        jsonDoc["pm25"] = roundTwoDecimals(g_pm25);
        jsonDoc["no2"] = g_no2;
        jsonDoc["pm10"] = g_pm10;
        jsonDoc["so2"] = g_so2;
        jsonDoc["o3"] = g_o3;
        jsonDoc["ai_label"] = g_ai_label;
        jsonDoc["ai_score"] = roundTwoDecimals(g_ai_score);

        String payload;
        serializeJson(jsonDoc, payload);
        client.publish(mqtt_topic, payload.c_str());
        
        Serial.println("Data Valid & Terkirim!");
    } else {
        Serial.println("Gagal Fetch API. Data tidak dikirim.");
    }
  }
}