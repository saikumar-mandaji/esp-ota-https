/*
 * esp-ota-https -- portable, hand-rolled parser for the fixed-shape
 * update manifest JSON document:
 *
 *   {"version":"1.2.3","url":"https://host/path/firmware.bin","sha256":"<64 hex chars>"}
 *
 * This is deliberately NOT a general JSON parser. The manifest shape is
 * fixed and small, so a full JSON library would be more attack surface
 * and more dependency weight than value -- see docs/ARCHITECTURE.md.
 * The parser is strict: unexpected structure, missing fields, wrong
 * types, or a sha256 field that isn't exactly 64 hex characters are all
 * rejected rather than silently accepted or partially parsed.
 */
#ifndef ESP_OTA_HTTPS_MANIFEST_H
#define ESP_OTA_HTTPS_MANIFEST_H

#include <stddef.h>
#include <stdbool.h>
#include "esp_ota_https/hexutil.h"

/** Maximum length (excluding NUL) accepted for the manifest's "url" field. */
#define EOH_MANIFEST_MAX_URL_LEN 255

/** Maximum length (excluding NUL) accepted for the manifest's "version" field. */
#define EOH_MANIFEST_MAX_VERSION_LEN 31

typedef struct {
    char version[EOH_MANIFEST_MAX_VERSION_LEN + 1];
    char url[EOH_MANIFEST_MAX_URL_LEN + 1];
    /* Raw sha256 field as read from the manifest, always exactly
     * EOH_SHA256_HEX_LEN chars + NUL. Use eoh_hex_decode_sha256() on
     * this to get raw bytes for comparison against a computed digest. */
    char sha256_hex[EOH_SHA256_HEX_LEN + 1];
} eoh_manifest_t;

/**
 * Parses `json` (a NUL-terminated buffer of length `json_len`, which
 * may or may not include the terminating NUL in the count -- both are
 * accepted) into `out`. Returns true only if all three required fields
 * (version, url, sha256) were present, were JSON strings, fit within
 * the buffer limits above, and sha256 was exactly 64 hex characters.
 * On failure, *out is left in an unspecified (partially written) state
 * and must not be used by the caller.
 */
bool eoh_manifest_parse(const char *json, size_t json_len, eoh_manifest_t *out);

#endif /* ESP_OTA_HTTPS_MANIFEST_H */
