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
const int portCoap = 5683;

// Config DHT11 (Pin D4 / GPIO2)
#define DHTPIN D4     
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

// Fungsi untuk mencari IP Raspberry Pi via mDNS
void findGatewayIP() {
  // Format nama host mDNS harus menggunakan ekstensi .local
  String fullHostname = String(targetHostname) + ".local";

  Serial.print("Mencari IP Gateway CoAP (");
  Serial.print(fullHostname);
  Serial.println(")...");

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Mencari Gateway...");
  display.println(fullHostname);
  display.display();

  while (ipGateway == IPAddress(0, 0, 0, 0)) {
    // Gunakan WiFi.hostByName untuk menyelesaikan nama host .local
    int result = WiFi.hostByName(fullHostname.c_str(), ipGateway);

    if (result == 1 && ipGateway != IPAddress(0, 0, 0, 0)) {
      break; // IP Berhasil Ditemukan!
    } else {
      Serial.println("Gateway belum ditemukan, mencoba lagi...");
      ipGateway = IPAddress(0, 0, 0, 0); // Reset ke 0 jika gagal
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
void setupOTA() {
  ArduinoOTA.setHostname("ESP-CoAP-Device");

  // Tambahkan password proteksi OTA (Opsional)
  // ArduinoOTA.setPassword("PasswordRahasia123");

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
  delay(1500);

  // Inisialisasi mDNS Client
  if (MDNS.begin("esp-coap-client")) {
    Serial.println("mDNS responder started");
  }

  // Cari IP Gateway Raspberry Pi secara otomatis
  findGatewayIP();

  // Inisialisasi Fitur OTA
  setupOTA();

  // Daftarkan handler response CoAP
  coap.response(callbackResponse);

  // Start CoAP Client
  coap.start();
}

void loop() {
  // Wajib dipanggil untuk menangani OTA & mDNS Service
  ArduinoOTA.handle();
  MDNS.update();

  coap.loop();

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    float hum = dht.readHumidity();
    float temp = dht.readTemperature();

    if (isnan(hum) || isnan(temp)) {
      Serial.println("Gagal membaca dari sensor DHT11!");
      
      display.clearDisplay();
      display.setTextSize(1);
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
    display.print("T: ");
    display.print(temp, 1);
    display.println(" C");

    display.setCursor(0, 42);
    display.print("H: ");
    display.print(hum, 1);
    display.println(" %");
    display.display();

    // Format Payload JSON
    String payload = "{\"suhu\":" + String(temp, 1) + ",\"kelembapan\":" + String(hum, 1) + "}";
    
    Serial.print("Mengirim CoAP POST ke IP ");
    Serial.print(ipGateway);
    Serial.print(": ");
    Serial.println(payload);

    // Kirim CoAP POST menggunakan IP Gateway yang ditemukan mDNS
    int msgId = coap.send(
      ipGateway,
      portCoap,
      "sensor/dht",
      COAP_CON,                  // Type: Confirmable
      COAP_POST,                 // Method: POST
      NULL,                      // Token (optional)
      0,                         // Token Length
      (uint8_t*)payload.c_str(), // Payload data
      payload.length()           // Panjang payload
    );

    Serial.print("Message ID: ");
    Serial.println(msgId);
  }
}