#include <Arduino.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <coap-simple.h>

// Konfigurasi Pin
#define DHTPIN 1       // Menggunakan Pin D1
#define DHTTYPE DHT22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Objek
DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
WiFiUDP udp;
Coap coap(udp);

float suhu = 0, kelembaban = 0;
unsigned long previousMillis = 0;
const long interval = 2000;

// Callback CoAP
void callback_suhu(CoapPacket &packet, IPAddress ip, int port) {
  String response = String(suhu) + "," + String(kelembaban);
  coap.sendResponse(ip, port, packet.messageid, response.c_str(), response.length());
}

// Callback saat masuk mode AP
void configModeCallback(WiFiManager *myWiFiManager) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Mode Konfigurasi");
  display.print("AP: ESP32-Monitoring");
  display.display();
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextColor(WHITE);

  // WiFiManager Setup
  WiFiManager wm;
  wm.setAPCallback(configModeCallback);
  wm.setConfigPortalTimeout(30);

  if (!wm.autoConnect("ESP32-Monitoring")) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Timeout/Gagal!");
    display.display();
    delay(3000);
    ESP.restart();
  }
  
  // CoAP Setup
  coap.server(callback_suhu, "data");
  coap.start();
  
  Serial.println("System Ready!");
}

void loop() {
  // CoAP loop harus selalu berjalan untuk merespon request
  coap.loop();

  // Non-blocking update data sensor dan display
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    suhu = dht.readTemperature();
    kelembaban = dht.readHumidity();
    
    display.clearDisplay();
    display.setCursor(0,0);
    
    display.print("WiFi: "); display.println(WiFi.SSID());
    display.print("IP: "); display.println(WiFi.localIP());
    display.println("----------------");
    display.print("Suhu: "); display.print(suhu); display.println(" C");
    display.print("Hum: "); display.print(kelembaban); display.println(" %");
    display.display();
  }
}