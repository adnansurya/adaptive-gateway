#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// Library untuk WiFiManager
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

// Library untuk Display OLED SSD1306 (0.96 inch)
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Library untuk Arduino OTA (Over The Air) Update
#include <ArduinoOTA.h>

// Library untuk mDNS Auto-Discovery
#include <ESP8266mDNS.h>

// Konfigurasi Layar OLED (128x64 pixel)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Hostname mDNS Raspberry Pi Gateway (tanpa .local)
const char* targetHostname = "mksrobotics";
IPAddress ipGateway(0, 0, 0, 0);  // Akan diisi otomatis oleh mDNS
const int portMQTT = 1883;

// Config Sensor PIN
#define PIR_PIN D4    // Sensor PIR di pin D4 (GPIO2)
#define MQ135_PIN A0  // Sensor MQ135 di pin Analog A0

// MQTT Setup
WiFiClient espClient;
PubSubClient mqttClient(espClient);

unsigned long previousMillis = 0;
const long interval = 5000;  // Kirim data setiap 5 detik

unsigned long t_start;

// Variabel Status MQTT ("IDLE", "CONNECTING", "OK", "ERR")
String mqttStatus = "IDLE";

// Variabel Sensor Global untuk Tampilan OLED
bool globalAdaGerakan = false;
int globalMq135Raw = 0;
float globalPPM = 0.0;

// Variabel untuk Latency dan Throughput
unsigned long lastLatencyMs = 0;
float lastThroughputBps = 0.0; // Dalam Bytes per second (B/s)

// Variabel manajemen pergantian halaman OLED
int currentPage = 0; // 0 = Halaman Sensor, 1 = Halaman Metrik
unsigned long lastPageSwitchMillis = 0;
const long pageSwitchInterval = 2000; // Berganti setiap 2 detik

// Fungsi untuk mengonversi nilai Raw ADC ke PPM (Estimasi CO2)
float hitungPPM(int rawADC) {
  if (rawADC <= 0) rawADC = 1;

  float voltase = rawADC * (3.3 / 1023.0);
  float RS_gas = (3.3 - voltase) / voltase;
  float R0 = 3.6;
  float ratio = RS_gas / R0;
  float ppm = 110.47 * pow(ratio, -2.862);

  if (ppm < 400.0) ppm = 400.0;
  if (ppm > 9999.0) ppm = 9999.0;

  return ppm;
}

// Fungsi menggambar Halaman 1: Sensor & Status MQTT (Tampilan Asli)
void drawPageSensor() {
  display.clearDisplay();

  // Header
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("--- SENSOR MQTT ---");

  // 1. Status Sensor PIR (Posisi Y = 14)
  display.setTextSize(1);
  display.setCursor(0, 14);
  display.print("Gerakan : ");
  display.println(globalAdaGerakan ? "ADA" : "TIDAK ADA");

  // 2. Data Kualitas Udara PPM (Posisi Y = 28)
  display.setCursor(0, 28);
  display.print("Polusi  : ");
  display.print((int)globalPPM);  // Menampilkan angka PPM
  display.println(" PPM");

  // 3. Indikator Status MQTT (Pojok Kanan Bawah: X=96, Y=54)
  display.setCursor(96, 54);
  if (mqttStatus == "CONNECTING") {
    display.print("[..]");
  } else if (mqttStatus == "OK") {
    display.print("[OK]");
  } else if (mqttStatus == "ERR") {
    display.print("[ERR]");
  } else {
    display.print("    ");
  }

  display.display();
}

// Fungsi menggambar Halaman 2: Latency & Throughput Terakhir
void drawPageMetrics() {
  display.clearDisplay();

  // Header
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("-- NETWORK METRICS --");

  // Display Latency
  display.setCursor(0, 16);
  display.setTextSize(1);
  display.println("Latency (Publish):");
  display.setTextSize(2);
  display.setCursor(0, 26);
  display.print(lastLatencyMs);
  display.setTextSize(1);
  display.print(" ms");

  // Display Throughput
  display.setCursor(0, 44);
  display.setTextSize(1);
  display.println("Throughput:");
  display.setCursor(0, 54);
  display.setTextSize(1);
  
  // Format tampilan throughput (B/s atau KB/s)
  if (lastThroughputBps >= 1024.0) {
    display.print(lastThroughputBps / 1024.0, 2);
    display.print(" KB/s");
  } else {
    display.print(lastThroughputBps, 1);
    display.print(" B/s");
  }

  display.display();
}

// Handler pergerakan tampilan OLED bergantian (2 detik sekali)
void handleOLEDManager() {
  if (millis() - lastPageSwitchMillis >= pageSwitchInterval) {
    lastPageSwitchMillis = millis();
    currentPage = (currentPage + 1) % 2; // Switch antara 0 dan 1
    
    if (currentPage == 0) {
      drawPageSensor();
    } else {
      drawPageMetrics();
    }
  }
}

// Wrapper update layar sesuai halaman aktif saat ini
void updateOLEDDisplay() {
  if (currentPage == 0) {
    drawPageSensor();
  } else {
    drawPageMetrics();
  }
}

// Fungsi untuk mencari IP Raspberry Pi via mDNS
void findGatewayIP() {
  String fullHostname = String(targetHostname) + ".local";

  Serial.print("Mencari IP Gateway MQTT (");
  Serial.print(fullHostname);
  Serial.println(")...");

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Mencari Gateway...");
  display.println(fullHostname);
  display.display();

  while (ipGateway == IPAddress(0, 0, 0, 0)) {
    int result = WiFi.hostByName(fullHostname.c_str(), ipGateway);

    if (result == 1 && ipGateway != IPAddress(0, 0, 0, 0)) {
      break;  // IP Berhasil Ditemukan!
    } else {
      Serial.println("Gateway belum ditemukan, mencoba lagi...");
      ipGateway = IPAddress(0, 0, 0, 0);
      delay(2000);
    }
  }

  Serial.print("Gateway Ditemukan! IP: ");
  Serial.println(ipGateway);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Gateway Found!");
  display.print("IP: ");
  display.println(ipGateway);
  display.display();
  delay(2000);
}

// Reconnect Ke Broker MQTT jika terputus
void reconnectMQTT() {
  while (!mqttClient.connected()) {
    mqttStatus = "CONNECTING";
    updateOLEDDisplay();

    Serial.print("Menghubungkan ke MQTT Broker pada ");
    Serial.print(ipGateway);
    Serial.println("...");

    String clientId = "ESP8266Client-" + String(random(0xffff), HEX);

    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("MQTT Terhubung!");
      mqttStatus = "OK";
      updateOLEDDisplay();
    } else {
      Serial.print("Gagal terhubung, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" mencoba lagi dalam 5 detik...");

      mqttStatus = "ERR";
      updateOLEDDisplay();
      delay(5000);
    }
  }
}

void setupOTA() {
  ArduinoOTA.setHostname("ESP-MQTT-Device");

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("Start updating " + type);

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("--- OTA UPDATE ---");
    display.println("Updating Firmware...");
    display.display();
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Update Selesai!");
    display.println("Rebooting...");
    display.display();
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    int percent = (progress / (total / 100));
    Serial.printf("Progress: %u%%\r", percent);

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("--- OTA UPDATE ---");
    display.setTextSize(2);
    display.setCursor(20, 25);
    display.printf("%d%%", percent);
    display.display();
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
  });

  ArduinoOTA.begin();
  Serial.println("OTA Service Ready!");
}

void setup() {
  Serial.begin(115200);

  // Inisialisasi Pin Sensor
  pinMode(PIR_PIN, INPUT);

  // Inisialisasi Layar OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("Gagal menginisialisasi OLED SSD1306!"));
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Inisialisasi System");
    display.println("Memulai WiFi...");
    display.display();
  }

  Serial.println("\n--- Memulai Sistem MQTT ---");

  // Inisialisasi WiFiManager
  WiFiManager wifiManager;

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connect ke AP:");
  display.setTextSize(2);
  display.println("ESP-MQTT");
  display.setTextSize(1);
  display.println("\nIP: 192.168.4.1");
  display.display();

  if (!wifiManager.autoConnect("ESP-MQTT")) {
    Serial.println("Gagal terhubung ke Wi-Fi dan waktu timeout habis.");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Gagal Wi-Fi!");
    display.println("Restarting...");
    display.display();

    ESP.restart();
    delay(1000);
  }

  Serial.println("\nWiFi Connected!");
  Serial.print("IP Address Wemos: ");
  Serial.println(WiFi.localIP());

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi Connected!");
  display.print("IP: ");
  display.println(WiFi.localIP());
  display.display();
  delay(1500);

  // Inisialisasi mDNS Client
  if (MDNS.begin("esp-mqtt-client")) {
    Serial.println("mDNS responder started");
  }

  // Cari IP Gateway Raspberry Pi secara otomatis
  findGatewayIP();

  // Inisialisasi MQTT Server Client
  mqttClient.setServer(ipGateway, portMQTT);

  // Inisialisasi Fitur OTA
  setupOTA();
}

void loop() {
  ArduinoOTA.handle();
  MDNS.update();

  // Pastikan koneksi MQTT tetap terjaga
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  // Pengelola pergantian tampilan OLED bergantian setiap 2 detik
  handleOLEDManager();

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // Pembacaan Sensor
    int pirState = digitalRead(PIR_PIN);
    int mq135Raw = analogRead(MQ135_PIN);

    globalAdaGerakan = (pirState == HIGH);

    // --- Hitung Nilai PPM dari ADC Raw ---
    globalPPM = mq135Raw; // Jika ingin memakai estimasi PPM nyata, ganti dengan: hitungPPM(mq135Raw);

    // Prepare Publish Payloads
    String pirTopic = "node/pir";
    String mqTopic = "node/mq135";
    String pirPayload = globalAdaGerakan ? "true" : "false";
    String mqPayload = String(globalPPM, 1);  // Kirim presisi 1 desimal

    // --- Hitung Latency & Throughput MQTT ---
    t_start = millis();

    // Kirim Data MQTT
    bool pub1 = mqttClient.publish(pirTopic.c_str(), pirPayload.c_str());
    bool pub2 = mqttClient.publish(mqTopic.c_str(), mqPayload.c_str());

    unsigned long latency_mqtt = millis() - t_start;
    
    // Mencegah nilai 0 ms agar tidak terjadi pembagian dengan nol
    if (latency_mqtt == 0) latency_mqtt = 1; 
    lastLatencyMs = latency_mqtt;

    // Hitung estimasi total Byte yang ditransfer (Fixed Header + Topic Length + Payload Length)
    // Standar MQTT Packet Overhead ~2-5 Byte per paket (diambil estimasi 4 Byte)
    size_t totalBytesTransferred = (4 + pirTopic.length() + pirPayload.length()) + 
                                   (4 + mqTopic.length() + mqPayload.length());

    // Hitung throughput (Byte per detik)
    lastThroughputBps = ((float)totalBytesTransferred / (float)latency_mqtt) * 1000.0;

    Serial.print("[LATENCY MQTT Local] Send Time: ");
    Serial.print(lastLatencyMs);
    Serial.println(" ms");

    Serial.print("[THROUGHPUT MQTT] ");
    Serial.print(lastThroughputBps, 2);
    Serial.println(" B/s");

    // Evaluasi status pengiriman
    if (pub1 && pub2) {
      mqttStatus = "OK";
    } else {
      mqttStatus = "ERR";
    }

    // Refresh Tampilan OLED
    updateOLEDDisplay();

    Serial.print("MQTT Sent -> [node/pir]: ");
    Serial.print(pirPayload);
    Serial.print(" | [node/mq135]: ");
    Serial.println(mqPayload);
  }
}