#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <coap-simple.h>
#include "DHT.h"

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

// Konfigurasi Layar OLED (128x64 pixel)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1 // -1 jika menggunakan pin reset bersama dengan ESP8266
#define SCREEN_ADDRESS 0x3C // Alamat I2C umum OLED 0.96 inch
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// IP Address Raspberry Pi (Gateway CoAP)
IPAddress ipGateway(192, 168, 1, 53); // Ganti dengan IP Raspberry Pi Anda
const int portCoap = 5683;

// Config DHT11 (Dipindahkan ke D4 / GPIO2)
#define DHTPIN D4     // Pin Data DHT11 terhubung ke D4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// UDP & CoAP Setup
WiFiUDP udp;
Coap coap(udp);

unsigned long previousMillis = 0;
const long interval = 5000; // Kirim data setiap 5 detik

void callbackResponse(CoapPacket &packet, IPAddress ip, int port) {
  Serial.println("[CoAP ACK] Response/ACK diterima dari Gateway!");
}

void setupOTA() {
  // Nama Host ESP8266 yang akan muncul di port Arduino IDE / Jaringan
  ArduinoOTA.setHostname("ESP-CoAP-Device");

  // Anda dapat menambahkan password untuk upload via OTA jika diperlukan:
  // ArduinoOTA.setPassword("admin123");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
    Serial.println("Start updating " + type);

    // Tampilkan indikator update pada OLED
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
    
    // Tampilkan persentase progress pada OLED
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
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.println("OTA Service Ready!");
}

void setup() {
  Serial.begin(115200);
  dht.begin();

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

  Serial.println("\n--- Memulai Sistem ---");

  // Inisialisasi WiFiManager
  WiFiManager wifiManager;

  // Tampilkan petunjuk koneksi di OLED jika masuk ke mode Access Point
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connect ke AP:");
  display.setTextSize(2);
  display.println("ESP-CoAP");
  display.setTextSize(1);
  display.println("\nIP: 192.168.4.1");
  display.display();

  // Membuka Access Point jika tidak terkoneksi
  if (!wifiManager.autoConnect("ESP-CoAP")) {
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

  // Tampilkan status terhubung pada OLED
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi Connected!");
  display.print("IP: ");
  display.println(WiFi.localIP());
  display.display();
  delay(2000);

  // Inisialisasi Fitur OTA setelah Wi-Fi terhubung
  setupOTA();

  // Daftarkan handler response CoAP
  coap.response(callbackResponse);

  // Start CoAP Client
  coap.start();
}

void loop() {
  // Wajib dipanggil di setiap siklus loop untuk menangani request OTA
  ArduinoOTA.handle();

  coap.loop();

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    float hum = dht.readHumidity();
    float temp = dht.readTemperature();

    if (isnan(hum) || isnan(temp)) {
      Serial.println("Gagal membaca dari sensor DHT11!");
      
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("Status: DHT Error!");
      display.display();
      return;
    }

    // Update Tampilan OLED dengan Bacaan Sensor
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("--- SENSOR DATA ---");
    
    display.setTextSize(2);
    display.setCursor(0, 18);
    display.print("Suhu: ");
    display.print(temp, 1);
    display.println(" C");

    display.setCursor(0, 42);
    display.print("Humi: ");
    display.print(hum, 1);
    display.println(" %");
    display.display();

    // Format Payload JSON
    String payload = "{\"suhu\":" + String(temp, 1) + ",\"kelembapan\":" + String(hum, 1) + "}";
    
    Serial.print("Mengirim CoAP POST ke Gateway: ");
    Serial.println(payload);

    // Kirim CoAP POST
    int msgId = coap.send(
      ipGateway,
      portCoap,
      "sensor/dht",
      COAP_CON,                  // Type: Confirmable (butuh balasan)
      COAP_POST,                 // Method: POST
      NULL,                      // Token (optional)
      0,                         // Token Length
      (uint8_t*)payload.c_str(), // Payload data dalam bentuk byte array
      payload.length()           // Panjang payload
    );

    Serial.print("Message ID: ");
    Serial.println(msgId);
  }
}