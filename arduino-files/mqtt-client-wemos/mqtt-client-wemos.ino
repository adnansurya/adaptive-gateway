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
#define OLED_RESET    -1 
#define SCREEN_ADDRESS 0x3C 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Hostname mDNS Raspberry Pi Gateway (tanpa .local)
const char* targetHostname = "mksrobotics"; 
IPAddress ipGateway(0, 0, 0, 0); // Akan diisi otomatis oleh mDNS
const int portMQTT = 1883;

// Config Sensor PIN
#define PIR_PIN   D4  // Sensor PIR di pin D4 (GPIO2)
#define MQ135_PIN A0  // Sensor MQ135 di pin Analog A0

// MQTT Setup
WiFiClient espClient;
PubSubClient mqttClient(espClient);

unsigned long previousMillis = 0;
const long interval = 5000; // Kirim data setiap 5 detik

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
      break; // IP Berhasil Ditemukan!
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
    Serial.print("Menghubungkan ke MQTT Broker pada ");
    Serial.print(ipGateway);
    Serial.println("...");

    String clientId = "ESP8266Client-" + String(random(0xffff), HEX);
    
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("MQTT Terhubung!");
    } else {
      Serial.print("Gagal terhubung, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" mencoba lagi dalam 5 detik...");
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
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
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

  // Membuka Access Point "ESP-MQTT" jika belum tersambung
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
  // Wajib dipanggil untuk menangani OTA & mDNS Service
  ArduinoOTA.handle();
  MDNS.update();

  // Pastikan koneksi MQTT tetap terjaga
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // Pembacaan Sensor
    int pirState = digitalRead(PIR_PIN);        // HIGH (1) jika ada gerakan, LOW (0) jika tidak
    int mq135Raw = analogRead(MQ135_PIN);       // Nilai ADC raw dari 0 - 1023

    bool adaGerakan = (pirState == HIGH);

    // Update Tampilan OLED
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("--- SENSOR MQTT ---");
    
    display.setCursor(0, 20);
    display.print("Gerakan : ");
    display.println(adaGerakan ? "ADA" : "CLEAR");

    display.setCursor(0, 40);
    display.print("MQ135   : ");
    display.print(mq135Raw);
    display.println(" PPM");
    display.display();

    // 1. Kirim Data PIR ke Topik MQTT "node/pir"
    String pirPayload = adaGerakan ? "true" : "false";
    mqttClient.publish("node/pir", pirPayload.c_str());

    // 2. Kirim Data MQ135 ke Topik MQTT "node/mq135"
    String mqPayload = String(mq135Raw);
    mqttClient.publish("node/mq135", mqPayload.c_str());

    Serial.print("MQTT Sent -> [node/pir]: ");
    Serial.print(pirPayload);
    Serial.print(" | [node/mq135]: ");
    Serial.println(mqPayload);
  }
}