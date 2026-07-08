# Hardware Bill of Materials

| Qty | Item | Part number / spec | Approx. cost |
|-----|------|---------------------|--------------|
| 1 | Dev board | **ESP32-DevKitC-32E** (Espressif) | $6-10 |
| 1 | USB cable | USB-A to Micro-USB (data-capable, not charge-only) | $3-5 |
| 1 | Wi-Fi access point | Any 2.4GHz 802.11b/g/n AP with internet access | (existing) |
| 1 | Host PC | Linux/macOS/WSL with ESP-IDF v5.2 installed | (existing) |

Total incremental hardware cost: **roughly $9-15** if you don't already
have a spare USB cable.

Notes:
- The ESP32-DevKitC-32E specifically (not the plain ESP32-DevKitC, which
  ships with older/varied module revisions) uses the ESP32-WROOM-32E
  module and is Espressif's current recommended dev board for new ESP32
  (not S2/S3/C3) designs.
- The device must be on the **2.4GHz** band -- the ESP32 does not support
  5GHz Wi-Fi.
- No special OTA hardware is required. ESP-IDF's OTA support relies on a
  partition table with two app slots (`ota_0` / `ota_1`) plus an `otadata`
  partition, which is flashed like any other partition table -- see
  `sdkconfig.defaults` (`CONFIG_PARTITION_TABLE_TWO_OTA=y`).

## Standing up a test HTTPS update server

`python3 -m http.server` is plain HTTP and will NOT work here -- this
project intentionally refuses insecure/no-TLS servers. Below is a minimal,
real way to serve HTTPS locally for testing on your LAN.

### 1. Generate a self-signed server certificate

```bash
# Generate a private key + self-signed cert valid for your test server's
# hostname or IP (replace CN with your actual test server's address).
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout server_key.pem \
  -out server_cert.pem \
  -days 365 \
  -subj "/CN=192.168.1.50"

# server_cert.pem IS the "CA cert" you embed on the device side (main/certs/) --
# for a self-signed cert, the cert itself is its own trust anchor.
```

### 2. Serve manifest.json + firmware.bin over HTTPS

A tiny Python HTTPS server (wraps the standard library's `http.server`
with an SSL socket -- this is the simplest "real HTTPS, no extra
dependencies" option for local testing):

```python
# serve_https.py
import http.server, ssl

server_address = ('0.0.0.0', 8443)
httpd = http.server.HTTPServer(server_address, http.server.SimpleHTTPRequestHandler)

ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(certfile="server_cert.pem", keyfile="server_key.pem")
httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)

print("Serving HTTPS on https://0.0.0.0:8443 ...")
httpd.serve_forever()
```

Run it from the directory containing `manifest.json` and `firmware.bin`:

```bash
python3 serve_https.py
```

Alternatively, for something closer to production, use **Caddy**
(automatically handles TLS, including for a real domain name with a
real CA-issued cert via Let's Encrypt) or **nginx** with a manually
configured `ssl_certificate` / `ssl_certificate_key` pointing at the
files generated above -- either is a drop-in replacement for the Python
server above; nothing about the device-side code changes except which
CA cert you embed.

### 3. Compute the manifest's `sha256` field

```bash
sha256sum firmware.bin
# e.g.: 3a7bd3e2360a3d... 2fabc  firmware.bin
```

Take just the hex digest (first field) and put it in `manifest.json`:

```json
{
  "version": "1.2.3",
  "url": "https://192.168.1.50:8443/firmware.bin",
  "sha256": "3a7bd3e2360a3d...2fabc"
}
```

### 4. Point the device at your test server

Edit `main/main.c`:
- `WIFI_SSID` / `WIFI_PASSWORD` -- your test AP's credentials.
- `MANIFEST_URL` -- `https://<your-server-ip>:8443/manifest.json`.

Replace `main/certs/example_ca_cert.pem` with your `server_cert.pem` from
step 1 (see `main/certs/README.md`), then:

```bash
idf.py set-target esp32
idf.py build flash monitor
```
