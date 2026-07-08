# Architecture

## Flow: manifest first, then OTA

```
  device                              update server
    |                                       |
    |--- HTTPS GET /manifest.json --------->|   (cert pinned against
    |<-- {"version","url","sha256"} -------|    embedded CA cert)
    |
    | parse manifest (src/manifest.c)
    | reject if malformed -- STOP here, no download yet
    |
    | compare manifest.version vs CURRENT_FW_VERSION (src/version.c)
    | if not strictly newer -- STOP here, nothing to do
    |
    |--- HTTPS GET <manifest.url> ---------->|   (cert pinned, same CA)
    |<-- firmware.bin (streamed) ------------|
    |
    | esp_https_ota() writes to inactive OTA partition,
    | validates image header/checksum internally
    |
    | read back written bytes, compute SHA-256,
    | compare against manifest.sha256 (src/hexutil.c)
    | mismatch -- revert boot partition, do NOT reboot into it
    | match    -- reboot into new firmware
```

The manifest is deliberately fetched and fully validated **before** any
firmware bytes are requested. A malformed manifest, or a manifest whose
version isn't actually newer, means the device never opens a second HTTPS
connection to download a multi-hundred-KB binary at all -- cheaper failure
mode, and one less network round-trip an attacker-controlled or broken
server can use to feed the device garbage.

## Why certificate pinning, not "skip verification"

ESP-IDF's `esp_http_client`/`esp-tls` layer offers a shortcut,
`skip_cert_common_name_check` (and, more drastically, simply not supplying
any `cert_pem`/`crt_bundle_attach`, which some example code does during
early bring-up), that lets a TLS handshake succeed against *any* server
presenting *any* certificate. It is common in tutorials because it makes
first-time bring-up easier -- and it is exactly the shortcut this project
does not take, because it silently turns "encrypted connection to server
X" into "encrypted connection to something," with no guarantee that
"something" is your update server. An attacker on the same network path
(malicious AP, ARP spoofing, a compromised router) can then transparently
MITM the connection and serve whatever firmware they like, over what looks
to casual inspection like a working HTTPS connection.

This project instead embeds the update server's actual CA certificate at
build time (`main/certs/example_ca_cert.pem`, via `EMBED_TXTFILES` --
see `main/certs/README.md`) and passes it as `cert_pem` to both the
manifest-fetch `esp_http_client_config_t` and the OTA
`esp_https_ota_config_t`'s `http_config`. TLS then only succeeds against a
server presenting a certificate chaining to that specific pinned CA --
nothing else, even if it has an otherwise perfectly valid certificate from
a public CA. `sdkconfig.defaults` also explicitly pins
`CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY=n` and
`CONFIG_ESP_TLS_INSECURE=n` so a future config edit can't quietly flip
this off.

## Why SHA-256 verification on top of TLS

TLS with a pinned certificate guarantees you're talking to the right
*server*. It does not guarantee the *file that server serves* is the
right file. Consider: a misconfigured deployment pipeline that uploads
last week's binary under this week's URL; a partially-written file on the
server due to a failed upload; a server that's legitimately yours but has
itself been compromised and had its files swapped by an attacker who
never touched your CA/TLS setup at all. In every one of these cases the
TLS handshake is completely valid -- the connection really is to your
pinned server -- and the firmware is still wrong.

The manifest's `sha256` field is an independently-supplied statement of
"this specific file, and no other, is what you should be running" --
independent in the sense that it was written into the manifest at release
time (ideally by your build/release pipeline, from the actual built
artifact), not derived from whatever bytes happen to arrive over the
wire. After `esp_https_ota` finishes writing the image, this project reads
the written bytes back out of flash, computes their SHA-256
(`src/hexutil.c` + `mbedtls_sha256_*` in `main/main.c`), and compares
against the manifest's digest. A mismatch means something -- not
necessarily malicious, could just be an ops mistake -- caused the served
bytes to not match what the release process intended, and the device
reverts the boot partition rather than booting into an unverified image.
This is a defense-in-depth check layered on top of TLS, not a replacement
for it: TLS + a wrong-but-correctly-hashed manifest is still a successful
attack if the attacker controls the manifest AND the binary together (see
docs/VERIFICATION.md for what this design does and does not protect
against).

## Why a hand-rolled manifest parser instead of a JSON library

The manifest has exactly one fixed shape:
`{"version":"...","url":"...","sha256":"..."}`. Pulling in a general JSON
library (cJSON, jsmn, etc.) for that is more parsing surface than the
problem needs, and more code the device has to trust. `src/manifest.c` is
a small, strict, hand-rolled parser: it accepts only JSON string values
for the three known keys (ignoring unrecognized string-valued keys for
forward compatibility, but rejecting non-string values or structural
surprises outright), and it validates `sha256` by actually decoding it as
64 hex characters rather than just checking `strlen() == 64`. This keeps
the portable core dependency-free and fully host-testable (see
`tests/host/`), matching this portfolio's "from-scratch, understand it"
approach elsewhere (e.g. the version-compare logic, reused as a pattern
from the sibling `secure-ota` project's `version_check.c`).
