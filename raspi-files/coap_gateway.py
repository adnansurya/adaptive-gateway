import asyncio
import logging
import aiocoap.resource as resource
import aiocoap

# Konfigurasi logging untuk melihat aktivitas di terminal
logging.basicConfig(level=logging.INFO)
logging.getLogger("coap-server").setLevel(logging.INFO)

class DHTSensorResource(resource.Resource):
    """Resource CoAP untuk menerima data dari sensor DHT11."""

    def __init__(self):
        super().__init__()

    async def render_post(self, request):
        """Menangani permintaan HTTP POST / CoAP POST"""
        payload = request.payload.decode('utf-8')
        client_address = request.remote.hostinfo
        
        print(f"\n[+] Data diterima dari Client ({client_address}):")
        print(f"    Payload: {payload}")
        
        # Di sini Anda bisa menambahkan kode untuk menyimpan ke Database (MySQL/InfluxDB)
        # atau meneruskan data ke Cloud Broker (MQTT/HTTP Cloud).

        # Mengirimkan balasan/response ACK ke Wemos
        response_payload = b"Data DHT11 Berhasil Diterima"
        return aiocoap.Message(content_format=0, payload=response_payload)

async def main():
    # Membuat context resource CoAP
    root = resource.Site()
    
    # Menambahkan endpoint 'sensor/dht'
    root.add_resource(['sensor', 'dht'], DHTSensorResource())

    # Menjalankan CoAP Server di port standard 5683
    await aiocoap.Context.create_server_context(root, bind=('0.0.0.0', 5683))
    
    print("==========================================")
    print("  CoAP Gateway Running on Port 5683...")
    print("  Resource Endpoint: coap://<IP_RASPBERRY>:5683/sensor/dht")
    print("==========================================")
    
    # Tetap jalankan server secara terus menerus
    await asyncio.get_running_loop().create_future()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nGateway dihentikan.")

