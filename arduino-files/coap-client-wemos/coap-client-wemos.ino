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

unsigned long t_start = 0;

// Config DHT11 (Pin D4 / GPIO2)
#define DHTPIN D4     
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// UDP & CoAP Setup
WiFiUDP udp;
Coap coap(udp);

unsigned long previousMillis = 0;
const long interval = 5000; // Kirim data setiap 5 detik

// Variabel Status Pengiriman CoAP untuk OLED
// "IDLE" = Belum kirim, "SEND" = Mengirim, "ACK" = Diterima/Sukses, "ERR" = Gagal
String coapStatus = "IDLE"; 

// Variabel data sensor global untuk update layar
float globalTemp = 0.0;
float globalHum = 0.0;

// Variabel untuk Latency dan Throughput
unsigned long lastLatencyMs = 0;
float lastThroughputBps = 0.0; // Dalam Bytes per second (B/s) atau bps (bits/s)

// Variabel manajemen pergantian halaman OLED
int currentPage = 0; // 0 = Halaman Sensor, 1 = Halaman Metrik
unsigned long lastPageSwitchMillis = 0;
const long pageSwitchInterval = 2000; // Berganti setiap 2 detik

// Fungsi menggambar Halaman 1: Sensor & Status CoAP (Tampilan Asli)
void drawPageSensor() {
  display.clearDisplay();
  
  // Header
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("--- SENSOR COAP ---");
  
  // Data Suhu (Y=14)
  display.setTextSize(2);
  display.setCursor(0, 14);
  display.print("T:");
  display.print(globalTemp, 1);
  display.setTextSize(1);
  display.print(" C");

  // Data Kelembapan (Y=36)
  display.setTextSize(2);
  display.setCursor(0, 36);
  display.print("H:");
  display.print(globalHum, 1);
  display.setTextSize(1);
  display.print(" %");

  // INDIKATOR STATUS COAP (Pojok Kanan Bawah: X=96, Y=54)
  display.setTextSize(1);
  display.setCursor(96, 54);

  if (coapStatus == "SEND") {
    display.print("[..]");  // Sedang mengirim / Menunggu ACK
  } else if (coapStatus == "ACK") {
    display.print("[OK]");  // ACK Berhasil diterima dari Gateway
  } else if (coapStatus == "ERR") {
    display.print("[ERR]"); // Gagal mengirim paket UDP
  } else {
    display.print("    ");   // Idle
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
  display.println("Latency (RTT):");
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

// Handler pergerakan tampilan OLED bergantian
void handleOLEDManager() {
  // Hanya jalankan switch tampilan otomatis jika tidak dalam status Error DHT
  if (millis() - lastPageSwitchMillis >= pageSwitchInterval) {
    lastPageSwitchMillis = millis();
    currentPage = (currentPage + 1) % 2; // Toggle antara 0 dan 1
    
    if (currentPage == 0) {
      drawPageSensor();
    } else {
      drawPageMetrics();
    }
  }
}

// Callback ketika ACK/Response diterima dari Raspberry Pi Gateway
void callbackResponse(CoapPacket &packet, IPAddress ip, int port) {
  unsigned long latency_coap = millis() - t_start;
  lastLatencyMs = latency_coap;

  // Hitung total payload yang ditransfer (Payload Request + ACK CoAP header)
  // Header dasar CoAP ~4 Byte + panjang payload response (jika ada)
  size_t totalBytesTransferred = packet.payloadlen + 4;

  // Hitung throughput dalam Bytes/detik
  if (latency_coap > 0) {
    lastThroughputBps = ((float)totalBytesTransferred / (float)latency_coap) * 1000.0;
  } else {
    lastThroughputBps = 0.0;
  }

  Serial.print("[LATENCY CoAP] RTT: ");
  Serial.print(lastLatencyMs);
  Serial.println(" ms");

  Serial.print("[THROUGHPUT CoAP] ");
  Serial.print(lastThroughputBps, 2);
  Serial.println(" B/s");

  Serial.println("[CoAP ACK] Response/ACK diterima dari Gateway!");
  coapStatus = "ACK";

  // Perbarui tampilan jika saat ini berada di halaman sensor
  if (currentPage == 0) {
    drawPageSensor();
  } else {
    drawPageMetrics();
  }
}

// Fungsi untuk mencari IP Raspberry Pi via mDNS
void findGatewayIP() {
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
    int result = WiFi.hostByName(fullHostname.c_str(), ipGateway);

    if (result == 1 && ipGateway != IPAddress(0, 0, 0, 0)) {
      break; 
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

void setupOTA() {
  ArduinoOTA.setHostname("ESP-CoAP-Device");

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

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connect ke AP:");
  display.setTextSize(2);
  display.println("ESP-CoAP");
  display.setTextSize(1);
  display.println("\nIP: 192.168.4.1");
  display.display();

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
  ArduinoOTA.handle();
  MDNS.update();

  coap.loop();

  // Atur pergantian layar otomatis setiap 2 detik
  handleOLEDManager();

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

    // Simpan ke variabel global
    globalTemp = temp;
    globalHum = hum;

    // Set status sedang mengirim
    coapStatus = "SEND";

    if (currentPage == 0) {
      drawPageSensor();
    } else {
      drawPageMetrics();
    }

    // Format Payload JSON
    String payload = "{\"suhu\":" + String(temp, 1) + ",\"kelembapan\":" + String(hum, 1) + "}";
    
    Serial.print("Mengirim CoAP POST ke IP ");
    Serial.print(ipGateway);
    Serial.print(": ");
    Serial.println(payload);

    t_start = millis(); // Catat waktu sebelum kirim

    // Kirim CoAP POST
    int msgId = coap.send(
      ipGateway,
      portCoap,
      "sensor/dht",
      COAP_CON,                  // Type: Confirmable (Membutuhkan ACK)
      COAP_POST,                 // Method: POST
      NULL,                      // Token (optional)
      0,                         // Token Length
      (uint8_t*)payload.c_str(), // Payload data
      payload.length()           // Panjang payload
    );

    Serial.print("Message ID: ");
    Serial.println(msgId);

    // Jika gagal mengirim (msgId <= 0), langsung tampilkan status error
    if (msgId <= 0) {
      coapStatus = "ERR";
      if (currentPage == 0) {
        drawPageSensor();
      } else {
        drawPageMetrics();
      }
    }
  }
}