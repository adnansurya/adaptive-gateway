import asyncio
import logging
import json
import time
import ssl
import socket
import urllib.parse
import urllib.request
import random
import subprocess  
import aiocoap.resource as resource
import aiocoap
import paho.mqtt.client as mqtt

# Meredam log internal aiocoap
logging.basicConfig(level=logging.INFO)
logging.getLogger("coap-server").setLevel(logging.WARNING)

# ==================== KONFIGURASI TELEGRAM BOT ====================
TELEGRAM_BOT_TOKEN = "8027503464:AAEtNkIbhf4saWoOzUD8irC8ug1hBy0ho-Y"
TELEGRAM_CHAT_ID = "8054572628"

# Variable pelacak status koneksi Cloud (mencegah pesan spam jika disconnect berulang)
is_cloud_connected = False

# ==================== KONFIGURASI MQTT BROKER UTAMA (CLOUD) ====================
MQTT_DEST_BROKER = "2d3014c691e840d98b7e4292008d7f8c.s1.eu.hivemq.cloud"
MQTT_DEST_PORT = 8883
MQTT_DEST_TOPIC = "gateway/all_data"

MQTT_USER = "donat"
MQTT_PASS = "12345678"

# ==================== KONFIGURASI MQTT BROKER LOKAL ====================
MQTT_SRC_BROKER = "localhost"
MQTT_SRC_PORT = 1883
MQTT_SRC_TOPICS = [("node/pir", 0), ("node/mq135", 0)]

# --- FUNGSI MENDAPATKAN IP & SSID WARRINGAN ---
def get_wlan_ip():
    """Mengambil IP address aktif dari interface jaringan/wlan yang terhubung."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "Tidak Terhubung"


def get_wifi_ssid():
    """Mengambil nama SSID Wi-Fi tempat Raspberry Pi sedang terhubung."""
    try:
        # Coba cara 1: Menggunakan iwgetid (Paling cepat di Raspberry Pi OS)
        result = subprocess.check_output(["iwgetid", "-r"], stderr=subprocess.DEVNULL)
        ssid = result.decode("utf-8").strip()
        if ssid:
            return ssid
    except Exception:
        pass

    try:
        # Coba cara 2: Menggunakan nmcli (NetworkManager)
        result = subprocess.check_output(
            ["nmcli", "-t", "-f", "active,ssid", "dev", "wifi"], 
            stderr=subprocess.DEVNULL
        )
        for line in result.decode("utf-8").split("\n"):
            if line.startswith("yes:"):
                return line.split(":")[1]
    except Exception:
        pass

    return "Tidak Diketahui / Kabel LAN"


# --- FUNGSI PENGIRIMAN TELEGRAM ---
def send_telegram_sync(message):
    """Pengiriman synchronous yang dipanggil dari thread paho MQTT / asyncio."""
    if TELEGRAM_BOT_TOKEN == "GANTI_DENGAN_TOKEN_BOT_ANDA":
        return
    try:
        url = f"https://api.telegram.org/bot{TELEGRAM_BOT_TOKEN}/sendMessage"
        payload = urllib.parse.urlencode({
            "chat_id": TELEGRAM_CHAT_ID,
            "text": message,
            "parse_mode": "Markdown"
        }).encode('utf-8')
        
        req = urllib.request.Request(url, data=payload)
        with urllib.request.urlopen(req, timeout=10) as response:
            pass
    except Exception as e:
        print(f"[Telegram Error] Gagal mengirim pesan: {e}")

async def send_telegram_notification(message):
    """Wrapper Asynchronous untuk pengiriman dari asyncio loop."""
    await asyncio.to_thread(send_telegram_sync, message)


class DataStore:
    def __init__(self):
        self.suhu = 0.0
        self.kelembapan = 0.0
        self.gerakan = False
        self.mq135_ppm = 0

    def update_coap_dht(self, data):
        self.suhu = data.get("suhu", self.suhu)
        self.kelembapan = data.get("kelembapan", self.kelembapan)
        print(f"[CoAP IN] Suhu: {self.suhu}°C | Kelembapan: {self.kelembapan}%")

    def update_mqtt_node(self, topic, payload_str):
        try:
            try:
                data = json.loads(payload_str)
            except json.JSONDecodeError:
                data = payload_str

            if "pir" in topic:
                if isinstance(data, dict):
                    self.gerakan = bool(data.get("gerakan", False))
                else:
                    self.gerakan = str(data).lower() in ["true", "1", "motion", "ada"]
                print(f"[MQTT IN] Sensor PIR (Gerakan): {self.gerakan}")

            elif "mq135" in topic:
                if isinstance(data, dict):
                    self.mq135_ppm = int(data.get("ppm", 0))
                else:
                    self.mq135_ppm = int(data)
                print(f"[MQTT IN] Sensor MQ135 (Kualitas Udara): {self.mq135_ppm} PPM")

        except Exception as e:
            print(f"[MQTT IN Error] Format payload tidak valid: {e}")

    def get_simple_payload(self):
        payload = {
            "suhu": round(self.suhu, 1),
            "kelembapan": round(self.kelembapan, 1),
            "gerakan": self.gerakan,
            "mq135_ppm": self.mq135_ppm,
            "timestamp": time.strftime("%Y-%m-%d %H:%M:%S")
        }
        return json.dumps(payload)


store = DataStore()

client_random_id = f"AdaptiveGateway_Pub_{random.randint(1000, 9999)}"
mqtt_pub_client = mqtt.Client(
    callback_api_version=mqtt.CallbackAPIVersion.VERSION2, 
    client_id=client_random_id
)


# ==================== CALLBACKS (STATUS KONEKSI CLOUD) ====================
def on_dest_connect(client, userdata, flags, rc, properties=None):
    global is_cloud_connected
    if rc == 0:
        print("\n[Cloud MQTT Success] Terhubung KE HIVEMQ CLOUD!\n")
        
        # Kirim Notifikasi jika sebelumnya dalam posisi disconnected/terputus
        if not is_cloud_connected:
            is_cloud_connected = True
            
            # Ambil SSID dan IP terbaru saat koneksi terhubung
            wifi_ssid = get_wifi_ssid()
            ip_wlan = get_wlan_ip()
            
            msg = (
                f"✅ *CLOUD MQTT CONNECTED*\n"
                f"----------------------------------------\n"
                f"• *Broker:* `{MQTT_DEST_BROKER}`\n"
                f"• *SSID Wi-Fi:* `{wifi_ssid}`\n"
                f"• *IP WLAN / Local:* `{ip_wlan}`\n"
                f"• *Status:* Terhubung ke HiveMQ Cloud *BERHASIL*\n"
                f"• *Waktu:* `{time.strftime('%Y-%m-%d %H:%M:%S')}`"
            )
            send_telegram_sync(msg)
    else:
        print(f"\n[Cloud MQTT Error] Gagal terhubung ke HiveMQ Cloud! Reason/Code: {rc}")


def on_dest_disconnect(client, userdata, flags, rc, properties=None):
    global is_cloud_connected
    print(f"\n[Cloud MQTT Warning] Terputus dari HiveMQ Cloud! Reason Code: {rc}\n")
    
    # Kirim Notifikasi jika sebelumnya terhubung lalu terputus
    if is_cloud_connected:
        is_cloud_connected = False
        
        # Ambil SSID dan IP saat koneksi terputus
        wifi_ssid = get_wifi_ssid()
        ip_wlan = get_wlan_ip()
        
        msg = (
            f"⚠️ *CLOUD MQTT DISCONNECTED*\n"
            f"----------------------------------------\n"
            f"• *Broker:* `{MQTT_DEST_BROKER}`\n"
            f"• *SSID Wi-Fi:* `{wifi_ssid}`\n"
            f"• *IP WLAN / Local:* `{ip_wlan}`\n"
            f"• *Status:* Koneksi Terputus! Mengirim rekoneksi otomatis...\n"
            f"• *Waktu:* `{time.strftime('%Y-%m-%d %H:%M:%S')}`"
        )
        send_telegram_sync(msg)

def on_dest_publish(client, userdata, mid, reason_code=None, properties=None):
    print(f"[Cloud MQTT Verifikasi] Data BERHASIL dikirim ke Cloud! (Msg ID: {mid})")


class DHTSensorResource(resource.Resource):
    async def render_post(self, request):
        try:
            raw_payload = request.payload.decode('utf-8')
            json_data = json.loads(raw_payload)
            store.update_coap_dht(json_data)
            return aiocoap.Message(content_format=0, payload=b"ACK")
        except Exception as e:
            return aiocoap.Message(code=aiocoap.BAD_REQUEST, payload=b"Error")


def on_local_mqtt_message(client, userdata, msg):
    payload_str = msg.payload.decode('utf-8')
    store.update_mqtt_node(msg.topic, payload_str)


async def sync_and_publish_loop(interval=10):
    while True:
        await asyncio.sleep(interval)
        
        json_payload = store.get_simple_payload()
        
        print("\n================ [ADAPTIVE GATEWAY AGGREGATION] ================")
        print(f"Mengirim Ke Topic Cloud [{MQTT_DEST_TOPIC}]:")
        print(json_payload)
        print("================================================================\n")

        info = mqtt_pub_client.publish(MQTT_DEST_TOPIC, json_payload, qos=1)
        if info.rc != mqtt.MQTT_ERR_SUCCESS:
            print(f"[Cloud MQTT Warning] Belum terhubung ke Cloud (Code: {info.rc})")


async def main():
    # 1. Konfigurasi Koneksi Outbound ke HiveMQ Cloud
    mqtt_pub_client.on_connect = on_dest_connect
    mqtt_pub_client.on_disconnect = on_dest_disconnect 
    mqtt_pub_client.on_publish = on_dest_publish
    
    mqtt_pub_client.username_pw_set(MQTT_USER.strip(), MQTT_PASS.strip())

    mqtt_pub_client.tls_set(
        cert_reqs=ssl.CERT_REQUIRED, 
        tls_version=ssl.PROTOCOL_TLSv1_2
    )

    try:
        print(f"[System] Menghubungkan ke HiveMQ Cloud ({MQTT_DEST_BROKER}:8883)...")
        mqtt_pub_client.connect(MQTT_DEST_BROKER, MQTT_DEST_PORT, 60)
        mqtt_pub_client.loop_start()
    except Exception as e:
        print(f"[System Error] Gagal koneksi awal ke Cloud: {e}")

    # 2. Setup Client MQTT Inbound Lokal
    mqtt_sub_client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id="AdaptiveGateway_Subscriber"
    )
    mqtt_sub_client.on_message = on_local_mqtt_message
    try:
        mqtt_sub_client.connect(MQTT_SRC_BROKER, MQTT_SRC_PORT, 60)
        mqtt_sub_client.subscribe(MQTT_SRC_TOPICS)
        mqtt_sub_client.loop_start()
        print(f"[System] Mendengarkan Topik MQTT PIR & MQ135 di {MQTT_SRC_BROKER}")
    except Exception as e:
        print(f"[System Warning] Broker MQTT Lokal tidak terjangkau ({e})")

    # 3. Start Server CoAP
    root = resource.Site()
    root.add_resource(['sensor', 'dht'], DHTSensorResource())
    await aiocoap.Context.create_server_context(root, bind=('0.0.0.0', 5683))

    print("\n==================================================")
    print("  ADAPTIVE GATEWAY READY (CoAP + MQTT -> CLOUD)")
    print("  - Endpoint CoAP : coap://<IP>:5683/sensor/dht")
    print("  - Topic MQTT In : node/pir & node/mq135")
    print("==================================================\n")

    # 4. Kirim Notifikasi Telegram (Gateway Startup)
    hostname = socket.gethostname()
    ip_wlan = get_wlan_ip()
    wifi_ssid = get_wifi_ssid()  # <--- Ambil SSID Wi-Fi aktif

    pesan_telegram = (
        f"🚀 *ADAPTIVE GATEWAY ONLINE*\n"
        f"----------------------------------------\n"
        f"• *Device:* `{hostname}`\n"
        f"• *SSID Wi-Fi:* `{wifi_ssid}`\n"  # <--- Menampilkan SSID
        f"• *IP WLAN / Local:* `{ip_wlan}`\n"
        f"• *Status:* Service Aktif & Berjalan\n"
        f"• *Waktu:* `{time.strftime('%Y-%m-%d %H:%M:%S')}`\n"
        f"----------------------------------------\n"
        f" Gateway siap memproses data CoAP & MQTT."
    )
    asyncio.create_task(send_telegram_notification(pesan_telegram))

    # 5. Loop Penggabungan Data Periodik
    asyncio.create_task(sync_and_publish_loop(interval=10))

    await asyncio.get_running_loop().create_future()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nGateway dihentikan.")
        mqtt_pub_client.loop_stop()
