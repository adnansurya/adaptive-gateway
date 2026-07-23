import paho.mqtt.client as mqtt
import json

# --- KONFIGURASI ---
HIVEMQ_HOST = "xxxxxxxx.s2.eu.hivemq.cloud" 
HIVEMQ_USER = "username"
HIVEMQ_PASS = "password"

# --- CLOUD SETUP (HiveMQ) ---
# Menggunakan callback_api_version=mqtt.CallbackAPIVersion.VERSION2
cloud_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
cloud_client.username_pw_set(HIVEMQ_USER, HIVEMQ_PASS)
cloud_client.tls_set()
cloud_client.connect(HIVEMQ_HOST, 8883)
cloud_client.loop_start()

# --- LOCAL SETUP (Mosquitto) ---
def on_message_local(client, userdata, msg):
    data_hijau = msg.payload.decode()
    print(f"Data Jalur Hijau diterima: {data_hijau}")
    
    payload = {"jalur_hijau": data_hijau}
    cloud_client.publish("data/gateway/final", json.dumps(payload))
    print(f"Data diteruskan ke HiveMQ Cloud")

# Update versi untuk client lokal juga
local_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
local_client.on_message = on_message_local

try:
    local_client.connect("localhost", 1883, 60)
    local_client.subscribe("sensor/hijau")
    print("Gateway berjalan. Menunggu data dari ESP32...")
    local_client.loop_forever()
except ConnectionRefusedError:
    print("ERROR: Tidak dapat terhubung ke Mosquitto Broker.")
    print("Pastikan layanan Mosquitto sudah aktif/berjalan di PC Anda.")
