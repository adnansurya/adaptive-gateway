#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <coap-simple.h>
#include "DHT.h"

// Config Wi-Fi
const char* ssid = "WARKOP LATEMMAMALA";
const char* password = "bulanjuni";

// IP Address Raspberry Pi (Gateway CoAP)
IPAddress ipGateway(192, 168, 1, 53); // Ganti dengan IP Raspberry Pi Anda
const int portCoap = 5683;

// Config DHT11
#define DHTPIN D2     // Pin Data DHT11 terhubung ke D2
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

void setup() {
  Serial.begin(115200);
  dht.begin();

  // Koneksi Wi-Fi
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("IP Address Wemos: ");
  Serial.println(WiFi.localIP());

  // Daftarkan handler response
  coap.response(callbackResponse);

  // Start CoAP Client
  coap.start();
}

void loop() {
  coap.loop();

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    float hum = dht.readHumidity();
    float temp = dht.readTemperature();

    if (isnan(hum) || isnan(temp)) {
      Serial.println("Gagal membaca dari sensor DHT11!");
      return;
    }

    // Format Payload JSON
    String payload = "{\"suhu\":" + String(temp, 1) + ",\"kelembapan\":" + String(hum, 1) + "}";
    
    Serial.print("Mengirim CoAP POST ke Gateway: ");
    Serial.println(payload);

    // Kirim CoAP POST yang kompatibel dengan coap-simple
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