import asyncio
import logging
import json
import time
import aiocoap.resource as resource
import aiocoap
import paho.mqtt.client as mqtt

# Meredam log internal aiocoap agar output terminal tetap bersih
logging.basicConfig(level=logging.INFO)
logging.getLogger("coap-server").setLevel(logging.WARNING)

# ==================== KONFIGURASI MQTT BROKER ====================
# Broker Server Utama (Tujuan Pengiriman Data Gabungan)
MQTT_DEST_BROKER = "broker.hivemq.com"  # Ganti dengan IP/Host Broker Anda
MQTT_DEST_PORT = 1883
MQTT_DEST_TOPIC = "gateway/data_sederhana"

# Broker Lokal/Input untuk Sensor PIR & MQ135
MQTT_SRC_BROKER = "localhost"          # Ganti ke IP Wemos/ESP lain jika broker ada di luar
MQTT_SRC_PORT = 1883
MQTT_SRC_TOPICS = [("node/pir", 0), ("node/mq135", 0)]  # Mendengarkan topik PIR & MQ135


class DataStore:
    """Buffer sederhana untuk menyimpan status terakhir dari semua sensor."""
    def __init__(self):
        self.suhu = 0.0
        self.kelembapan = 0.0
        self.gerakan = False
        self.mq135_ppm = 0

    def update_coap_dht(self, data):
        """Memproses data CoAP dari DHT11 (Wemos D1 Mini)"""
        self.suhu = data.get("suhu", self.suhu)
        self.kelembapan = data.get("kelembapan", self.kelembapan)
        print(f"[CoAP IN] Suhu: {self.suhu}°C | Kelembapan: {self.kelembapan}%")

    def update_mqtt_node(self, topic, payload_str):
        """Memproses data MQTT dari Sensor PIR dan MQ135"""
        try:
            # Jika payload berupa JSON, parse nilainya. Jika string angka/boolean, langsung baca.
            try:
                data = json.loads(payload_str)
            except json.JSONDecodeError:
                data = payload_str

            if "pir" in topic:
                # Menerima data PIR (misal: true/false, 1/0, atau "MOTION"/"CLEAR")
                if isinstance(data, dict):
                    self.gerakan = bool(data.get("gerakan", False))
                else:
                    self.gerakan = str(data).lower() in ["true", "1", "motion", "ada"]
                print(f"[MQTT IN] Sensor PIR (Gerakan): {self.gerakan}")

            elif "mq135" in topic:
                # Menerima data Kualitas Udara MQ135 (misal: 420 atau {"ppm": 420})
                if isinstance(data, dict):
                    self.mq135_ppm = int(data.get("ppm", 0))
                else:
                    self.mq135_ppm = int(data)
                print(f"[MQTT IN] Sensor MQ135 (Kualitas Udara): {self.mq135_ppm} PPM")

        except Exception as e:
            print(f"[MQTT IN Error] Format payload dari topic {topic} tidak valid: {e}")

    def get_simple_payload(self):
        """Menyatukan semua data sensor menjadi format JSON yang sederhana."""
        payload = {
            "suhu": round(self.suhu, 1),
            "kelembapan": round(self.kelembapan, 1),
            "gerakan": self.gerakan,
            "mq135_ppm": self.mq135_ppm,
            "timestamp": time.strftime("%Y-%m-%d %H:%M:%S")
        }
        return json.dumps(payload)


# Global Store & MQTT Client Publisher
store = DataStore()
mqtt_pub_client = mqtt.Client(client_id="AdaptiveGateway_Publisher")


class DHTSensorResource(resource.Resource):
    """Resource Endpoint CoAP (/sensor/dht)"""
    async def render_post(self, request):
        try:
            raw_payload = request.payload.decode('utf-8')
            json_data = json.loads(raw_payload)
            store.update_coap_dht(json_data)
            return aiocoap.Message(content_format=0, payload=b"ACK")
        except Exception as e:
            return aiocoap.Message(code=aiocoap.BAD_REQUEST, payload=b"Error")


def on_mqtt_message(client, userdata, msg):
    """Callback saat ada pesan masuk dari Sensor PIR / MQ135"""
    payload_str = msg.payload.decode('utf-8')
    store.update_mqtt_node(msg.topic, payload_str)


async def sync_and_publish_loop(interval=10):
    """Menggabungkan data CoAP + MQTT dan mempublikasikannya setiap X detik."""
    while True:
        await asyncio.sleep(interval)
        
        # Buat JSON sederhana
        json_payload = store.get_simple_payload()
        
        print("\n================ [ADAPTIVE GATEWAY AGGREGATION] ================")
        print(f"Penerbitan Ke Broker [{MQTT_DEST_TOPIC}]:")
        print(json_payload)
        print("================================================================\n")

        # Publish ke Broker Destinasi
        mqtt_pub_client.publish(MQTT_DEST_TOPIC, json_payload)


async def main():
    # 1. Start Client MQTT Outbound (Pengirim Data Utama)
    try:
        mqtt_pub_client.connect(MQTT_DEST_BROKER, MQTT_DEST_PORT, 60)
        mqtt_pub_client.loop_start()
        print(f"[System] MQTT Publisher terhubung ke {MQTT_DEST_BROKER}")
    except Exception as e:
        print(f"[System Error] Gagal terhubung ke MQTT Broker Utama: {e}")

    # 2. Start Client MQTT Inbound (Penerima PIR & MQ135)
    mqtt_sub_client = mqtt.Client(client_id="AdaptiveGateway_Subscriber")
    mqtt_sub_client.on_message = on_mqtt_message
    try:
        mqtt_sub_client.connect(MQTT_SRC_BROKER, MQTT_SRC_PORT, 60)
        mqtt_sub_client.subscribe(MQTT_SRC_TOPICS)
        mqtt_sub_client.loop_start()
        print(f"[System] Mendengarkan Topik MQTT PIR & MQ135 di {MQTT_SRC_BROKER}")
    except Exception as e:
        print(f"[System Warning] Broker MQTT Lokal tidak terjangkau ({e})")

    # 3. Start Server CoAP (Penerima DHT11 Wemos)
    root = resource.Site()
    root.add_resource(['sensor', 'dht'], DHTSensorResource())
    await aiocoap.Context.create_server_context(root, bind=('0.0.0.0', 5683))

    print("\n==================================================")
    print("  ADAPTIVE GATEWAY READY (CoAP + MQTT)")
    print("  - Endpoint CoAP : coap://<IP>:5683/sensor/dht")
    print("  - Topic MQTT In : node/pir & node/mq135")
    print("==================================================\n")

    # 4. Jalankan Loop Penggabungan Data Periodik
    asyncio.create_task(sync_and_publish_loop(interval=10))

    await asyncio.get_running_loop().create_future()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nGateway dihentikan.")
        mqtt_pub_client.loop_stop()
