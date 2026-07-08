# esp-ota-https

Secure HTTPS OTA firmware updates for ESP32, built on ESP-IDF's
`esp_https_ota` component, with:

- **Certificate-pinned TLS** -- the update server's CA cert is embedded at
  build time; there is no "skip verification" shortcut anywhere in this
  code (see `docs/ARCHITECTURE.md`).
- **An application-level JSON update manifest** --
  `{"version","url","sha256"}` -- fetched and fully validated *before* any
  firmware bytes are downloaded or flashed.
- **Post-flash SHA-256 verification** -- defense-in-depth on top of TLS,
  catching a corrupted/wrong binary served even over a valid, pinned
  connection.

This is an ESP-IDF (not Arduino) project.

## Quick start

Requires ESP-IDF v5.2 and an ESP32-DevKitC-32E (see
`docs/hardware/BOM.md` for the full BOM and how to stand up a test HTTPS
server).

```bash
# Host tests (no hardware needed):
cd tests/host && make test

# Firmware (needs ESP-IDF installed and sourced):
idf.py set-target esp32
idf.py build flash monitor
```

Before flashing against a real server:
1. Replace `main/certs/example_ca_cert.pem` with your server's real CA
   cert (see `main/certs/README.md`).
2. Edit `WIFI_SSID`, `WIFI_PASSWORD`, `MANIFEST_URL` in `main/main.c`.
3. Serve `manifest.json` + your firmware binary over real HTTPS (see
   `docs/hardware/BOM.md`).

## Layout

```
include/esp_ota_https/   portable (pure C99) public headers
src/                      portable (pure C99) manifest parser, version
                          compare, hex/SHA-256 helpers -- no ESP-IDF headers
tests/host/               host-native unit tests for everything in src/
main/                     ESP-IDF application: Wi-Fi, HTTPS manifest fetch,
                          esp_https_ota, post-flash verification
main/certs/               embedded CA certificate (EMBED_TXTFILES)
docs/                     architecture, hardware BOM, verification status
```

## API summary

- `eoh_manifest_parse(json, json_len, eoh_manifest_t *out)` -- strict,
  hand-rolled parser for the fixed manifest shape. Rejects malformed
  input rather than partially parsing it.
- `eoh_version_parse` / `eoh_version_compare` / `eoh_version_is_upgrade`
  -- `MAJOR.MINOR.REVISION` semantic version comparison.
- `eoh_hex_decode` / `eoh_hex_decode_sha256` / `eoh_hex_encode` /
  `eoh_sha256_equal` -- hex string <-> raw bytes for comparing a
  computed firmware digest against the manifest's expected one.

Full doc comments in `include/esp_ota_https/*.h`.

## Known limitations

See `docs/VERIFICATION.md` for the full, honest breakdown. Highlights:
- `main/main.c` (Wi-Fi, HTTPS fetch, `esp_https_ota`, post-flash
  verification) is **not host-testable** and has not run on real
  hardware in this dev environment -- reviewed by inspection and
  compiled by CI against real ESP-IDF headers, nothing more.
- The embedded CA certificate is a throwaway placeholder -- replace it
  before any real deployment.
- No automatic rollback-on-failed-boot is wired up (ESP-IDF supports it
  via `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` + marking the app valid;
  this repo doesn't call that API as shipped -- see
  `docs/VERIFICATION.md`).

## License

MIT -- see [LICENSE](LICENSE).
