#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "DHT.h"

// --- KONFIGURASI ---
const char* ssid = "GeGe";
const char* password = "22032020";

// Detail HiveMQ Cloud
const char* mqtt_server = "2d3014c691e840d98b7e4292008d7f8c.s1.eu.hivemq.cloud"; // Ganti sesuai URL Anda
const int mqtt_port = 8883;
const char* mqtt_user = "donat";
const char* mqtt_pass = "12345678";

// Pin DHT22
#define DHTPIN 1
#define DHTTYPE DHT22

WiFiClientSecure espClient;
PubSubClient client(espClient);
DHT dht(DHTPIN, DHTTYPE);

void setup_wifi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi Terhubung!");
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Menghubungkan ke MQTT HiveMQ...");
    // Gunakan username dan password dari Access Management HiveMQ
    if (client.connect("ESP32_Jalur_Hijau", mqtt_user, mqtt_pass)) {
      Serial.println("Berhasil!");
    } else {
      Serial.print("Gagal, rc=");
      Serial.print(client.state());
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  setup_wifi();

  // Konfigurasi TLS agar bisa connect ke Cloud
  espClient.setInsecure(); 
  client.setServer(mqtt_server, mqtt_port);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Kirim data setiap 10 detik
  static unsigned long lastMsg = 0;
  if (millis() - lastMsg > 10000) {
    lastMsg = millis();
    float suhu = dht.readTemperature();
    float hum = dht.readHumidity();

    if (!isnan(suhu) && !isnan(hum)) {
      // Mengirim ke topik terpisah
      client.publish("sensor/suhu", String(suhu).c_str());
      client.publish("sensor/kelembaban", String(hum).c_str());
      
      Serial.print("Data dikirim -> Suhu: ");
      Serial.print(suhu);
      Serial.print(" C, Hum: ");
      Serial.print(hum);
      Serial.println(" %");
    }
  }
}