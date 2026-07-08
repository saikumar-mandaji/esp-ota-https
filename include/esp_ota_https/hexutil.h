/*
 * esp-ota-https -- portable hex-string <-> bytes helpers used to turn
 * the manifest's "sha256":"<64 hex chars>" field into raw bytes for
 * comparison against a computed digest, and back (e.g. for logging).
 */
#ifndef ESP_OTA_HTTPS_HEXUTIL_H
#define ESP_OTA_HTTPS_HEXUTIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/** Number of raw bytes in a SHA-256 digest. */
#define EOH_SHA256_LEN 32

/** Number of hex characters representing a SHA-256 digest (2 per byte). */
#define EOH_SHA256_HEX_LEN (EOH_SHA256_LEN * 2)

/**
 * Decodes exactly `hex_len` hex characters from `hex` into `out_bytes`
 * (which must have room for hex_len/2 bytes). Accepts lowercase and
 * uppercase hex digits. Returns false (leaving out_bytes unmodified)
 * if hex_len is odd, zero, or any character is not a valid hex digit.
 */
bool eoh_hex_decode(const char *hex, size_t hex_len, uint8_t *out_bytes);

/**
 * Convenience wrapper specifically for a 64-hex-char SHA-256 string:
 * requires strlen(hex) == EOH_SHA256_HEX_LEN exactly.
 */
bool eoh_hex_decode_sha256(const char *hex, uint8_t out_bytes[EOH_SHA256_LEN]);

/**
 * Encodes `len` bytes from `bytes` into lowercase hex into `out_hex`,
 * which must have room for at least (2*len + 1) bytes (the +1 for the
 * trailing NUL terminator that this function always writes).
 */
void eoh_hex_encode(const uint8_t *bytes, size_t len, char *out_hex);

/**
 * Constant-time-ish comparison of two equal-length byte buffers (both
 * EOH_SHA256_LEN bytes). Returns true iff they are identical. Not a
 * cryptographically hardened constant-time compare -- see
 * docs/VERIFICATION.md for why that's acceptable here (comparing a
 * downloaded firmware digest is not a secret-comparison / timing-attack
 * surface the way e.g. an HMAC verification would be).
 */
bool eoh_sha256_equal(const uint8_t a[EOH_SHA256_LEN], const uint8_t b[EOH_SHA256_LEN]);

#endif /* ESP_OTA_HTTPS_HEXUTIL_H */
