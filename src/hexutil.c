#include "esp_ota_https/hexutil.h"
#include <string.h>

static bool hex_nibble(char c, uint8_t *out)
{
    if (c >= '0' && c <= '9') {
        *out = (uint8_t)(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        *out = (uint8_t)(c - 'a' + 10);
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        *out = (uint8_t)(c - 'A' + 10);
        return true;
    }
    return false;
}

bool eoh_hex_decode(const char *hex, size_t hex_len, uint8_t *out_bytes)
{
    if (hex == NULL || out_bytes == NULL) {
        return false;
    }
    if (hex_len == 0 || (hex_len % 2) != 0) {
        return false;
    }

    for (size_t i = 0; i < hex_len / 2; i++) {
        uint8_t hi, lo;
        if (!hex_nibble(hex[2 * i], &hi) || !hex_nibble(hex[2 * i + 1], &lo)) {
            return false;
        }
        out_bytes[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

bool eoh_hex_decode_sha256(const char *hex, uint8_t out_bytes[EOH_SHA256_LEN])
{
    if (hex == NULL) {
        return false;
    }
    if (strlen(hex) != EOH_SHA256_HEX_LEN) {
        return false;
    }
    return eoh_hex_decode(hex, EOH_SHA256_HEX_LEN, out_bytes);
}

void eoh_hex_encode(const uint8_t *bytes, size_t len, char *out_hex)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out_hex[2 * i] = digits[(bytes[i] >> 4) & 0x0F];
        out_hex[2 * i + 1] = digits[bytes[i] & 0x0F];
    }
    out_hex[2 * len] = '\0';
}

bool eoh_sha256_equal(const uint8_t a[EOH_SHA256_LEN], const uint8_t b[EOH_SHA256_LEN])
{
    uint8_t diff = 0;
    for (size_t i = 0; i < EOH_SHA256_LEN; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}
