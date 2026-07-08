# Verification Status -- Honest Disclosure

This dev environment has **no ESP32 hardware** and **no real HTTPS server**
available. The split below is deliberate and should be read before trusting
any part of this repository for a real deployment.

## What IS verified (host-tested, CI-checked on every push)

All of `include/esp_ota_https/*.h` + `src/*.c` is pure C99 with zero
ESP-IDF/FreeRTOS/hardware dependencies, and is compiled and run on the CI
runner's native toolchain (`tests/host/`, `make test`, GitHub Actions
`host-tests` job):

- **Manifest parsing** (`src/manifest.c`): well-formed manifests parse
  correctly and extract the right fields; missing fields, truncated JSON,
  wrong-typed values (number/null/array where a string is expected), and
  malformed/wrong-length `sha256` fields are all rejected rather than
  partially accepted. See `tests/host/test_manifest.c`.
- **Semantic version comparison** (`src/version.c`): parsing of
  `MAJOR.MINOR.REVISION` strings, rejection of malformed strings
  (missing fields, non-numeric, out-of-range, NULL), and ordering/
  upgrade-vs-downgrade detection. See `tests/host/test_version.c`.
- **Hex <-> bytes helpers** (`src/hexutil.c`): encode/decode round-trip,
  a known encode vector, rejection of wrong-length and non-hex input, and
  byte-buffer equality comparison. See `tests/host/test_hexutil.c`.

This is real logic, genuinely exercised -- not a placeholder or a stub.

## What is NOT verified (no hardware, no real server, inspection only)

`main/main.c` -- the Wi-Fi connection, the HTTPS manifest fetch via
`esp_http_client`, the call into `esp_https_ota`, and the post-flash
SHA-256 read-back/compare -- has **only been written and reviewed by
inspection**. It has never been built with the real ESP-IDF toolchain in
this environment (CI's `firmware-build` job does compile it against real
ESP-IDF v5.2 headers/libraries in a container, which catches type errors,
missing includes, and API misuse, but that is a compile check, not a
runtime one), and it has never run on real ESP32 silicon against a real
HTTPS server. Concretely, none of the following has been observed to
actually happen:

- A real TLS handshake succeeding against a real pinned CA certificate
  (or, just as importantly, actually *failing closed* against an
  unpinned/wrong certificate -- the negative case is the one that matters
  most here and is asserted by design, not by a test run).
- A real `esp_https_ota()` call downloading and flashing an image to an
  ESP32's OTA partition.
- The post-flash `esp_partition_read()` + `mbedtls_sha256_*` verification
  path actually detecting a real mismatch and actually reverting the boot
  partition via `esp_ota_set_boot_partition()`.
- Wi-Fi actually associating to a real access point via this code's
  event-handler wiring.

## Known limitations

- **Placeholder CA certificate.** `main/certs/example_ca_cert.pem` is a
  throwaway self-signed certificate generated for this repo to build
  standalone (see `main/certs/README.md`). It must be replaced with your
  real update server's actual CA certificate before this is useful
  against anything other than itself. Left as-is, TLS will simply fail
  closed against any real server -- the safe failure, but not a working
  deployment.
- **Hardcoded Wi-Fi credentials and manifest URL.** `WIFI_SSID`,
  `WIFI_PASSWORD`, and `MANIFEST_URL` are `#define`s in `main/main.c` for
  clarity/simplicity in this portfolio piece, not loaded from NVS or a
  provisioning flow (contrast with the sibling `esp-provision` project,
  which specifically implements BLE-based Wi-Fi provisioning -- a real
  product would likely combine the two rather than hardcode credentials
  into firmware).
- **No rollback-on-failed-boot state machine, unlike MCUboot.** The
  sibling `secure-ota` project (Zephyr + nRF52840 + MCUboot) gets
  swap/revert-on-failed-boot, image signature verification, and
  anti-rollback security counters from MCUboot itself. ESP-IDF's own OTA
  scheme (`app_update` component, `ota_0`/`ota_1`/`otadata` partitions) is
  a different mechanism: it supports **app rollback** via
  `esp_ota_mark_app_valid_cancel_rollback()` /
  `esp_ota_mark_app_invalid_rollback_and_reboot()` when
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`, where a newly-booted image
  that never explicitly marks itself valid gets automatically rolled back
  on the next reset. This project's `sdkconfig.defaults` currently sets
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=n` and `main/main.c` does not
  call the mark-valid API -- i.e. **automatic rollback-on-boot-failure is
  not wired up in this repo as shipped.** Enabling it and calling
  `esp_ota_mark_app_valid_cancel_rollback()` after the new firmware
  confirms it's healthy (e.g. after a successful reconnect + a health
  check) would be the natural next step for a real deployment, and is
  called out here rather than silently assumed.
- **Post-flash SHA-256 check has a narrow timing window.**
  `esp_https_ota_finish()` validates the image and sets the new partition
  as the boot target *before* this code's own SHA-256 comparison runs. If
  the comparison then fails, `main.c` calls `esp_ota_set_boot_partition()`
  to point back at the currently-running partition -- but if the device
  lost power in the brief window between `esp_https_ota_finish()`
  returning and that revert call completing, it would boot into the
  unverified image once. This is a real, narrow gap, not something this
  design closes; it's disclosed rather than glossed over.
- **`esp_https_ota`'s own internal image validation is not a substitute
  for the SHA-256 check** (and vice versa): `esp_https_ota_finish()`
  checks that the downloaded data is a structurally valid ESP-IDF app
  image (correct header, image checksum) -- it does NOT know what the
  manifest expected the SHA-256 to be. The two checks catch different
  failure classes; both are needed for the guarantee described in
  `docs/ARCHITECTURE.md`.
- **Manifest response size cap.** `MANIFEST_MAX_BODY_LEN` (2048 bytes) in
  `main/main.c` is a fixed stack/static buffer size for the manifest HTTP
  response body. A manifest larger than that is rejected outright
  (`manifest_http_event_handler` returns `ESP_FAIL`) rather than silently
  truncated -- intentional fail-closed behavior, but worth knowing if you
  ever add more fields to the manifest shape.
