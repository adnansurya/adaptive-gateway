#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// --- KONFIGURASI ---
const char* ssid = "Hana";
const char* password = "74757677";
const char* mqtt_server = "192.168.74.53"; // IP Address PC Anda (Broker Lokal)
const int mqtt_port = 1883;

// Konfigurasi Pin
#define DHTPIN 1
#define DHTTYPE DHT22

DHT dht(DHTPIN, DHTTYPE);
WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastMsg = 0;
const long interval = 5000; // Kirim data setiap 5 detik

void setup_wifi() {
  delay(10);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi Terhubung!");
}

void reconnect() {
  // Loop sampai terhubung ke MQTT
  while (!client.connected()) {
    if (client.connect("ESP32_Jalur_Hijau")) {
      Serial.println("MQTT Terhubung");
    } else {
      delay(5000); // Tunggu 5 detik sebelum coba lagi
    }
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
}

void loop() {
  // Pastikan koneksi MQTT tetap terjaga
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Non-blocking timer
  unsigned long now = millis();
  if (now - lastMsg > interval) {
    lastMsg = now;

    float suhu = dht.readTemperature();
    float hum = dht.readHumidity();

    if (!isnan(suhu) && !isnan(hum)) {
      // Format data: suhu,kelembaban
      String payload = String(suhu) + "," + String(hum);
      
      // Publish ke topik "sensor/hijau"
      client.publish("sensor/hijau", payload.c_str());
      
      Serial.print("Data dikirim: ");
      Serial.println(payload);
    }
  }
}