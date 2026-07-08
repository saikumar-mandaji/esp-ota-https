/* Host tests for the hex encode/decode helpers, compiled against the
 * real production source in src/hexutil.c. */
#include "esp_ota_https/hexutil.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else { printf("  ok  : %s\n", msg); } \
} while (0)

static void test_roundtrip(void)
{
    printf("[hex] encode/decode round-trip\n");
    uint8_t original[EOH_SHA256_LEN];
    for (int i = 0; i < EOH_SHA256_LEN; i++) {
        original[i] = (uint8_t)(i * 7 + 1);
    }

    char hex[EOH_SHA256_HEX_LEN + 1];
    eoh_hex_encode(original, EOH_SHA256_LEN, hex);
    CHECK(strlen(hex) == EOH_SHA256_HEX_LEN, "encoded hex string has expected length");

    uint8_t decoded[EOH_SHA256_LEN];
    CHECK(eoh_hex_decode_sha256(hex, decoded), "re-decoding the encoded hex string succeeds");
    CHECK(eoh_sha256_equal(original, decoded), "round-tripped bytes match the original");

    CHECK(eoh_hex_decode_sha256(
              "0001020304050607080910111213141516171819202122232425262728293031ff",
              decoded) == false,
          "66-hex-char string (wrong total length) is rejected");
}

static void test_known_vector(void)
{
    printf("[hex] known encode vector\n");
    uint8_t bytes[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    char hex[9];
    eoh_hex_encode(bytes, 4, hex);
    CHECK(strcmp(hex, "deadbeef") == 0, "{0xDE,0xAD,0xBE,0xEF} encodes to \"deadbeef\" (lowercase)");
}

static void test_decode_rejects_bad_input(void)
{
    printf("[hex] malformed hex input is rejected\n");
    uint8_t out[EOH_SHA256_LEN];

    char too_short[EOH_SHA256_HEX_LEN]; /* 63 chars + NUL is wrong length */
    memset(too_short, 'a', sizeof(too_short) - 1);
    too_short[sizeof(too_short) - 1] = '\0';
    CHECK(!eoh_hex_decode_sha256(too_short, out), "63-char string is rejected (not 64)");

    char has_g[EOH_SHA256_HEX_LEN + 1];
    memset(has_g, 'a', EOH_SHA256_HEX_LEN);
    has_g[EOH_SHA256_HEX_LEN] = '\0';
    has_g[0] = 'g'; /* 'g' is not a hex digit */
    CHECK(!eoh_hex_decode_sha256(has_g, out), "non-hex character 'g' is rejected");

    CHECK(!eoh_hex_decode_sha256(NULL, out), "NULL input is rejected, not dereferenced");
    CHECK(!eoh_hex_decode("ab", 1, out), "odd hex_len is rejected");
    CHECK(!eoh_hex_decode("ab", 0, out), "zero hex_len is rejected");

    char mixed_case[EOH_SHA256_HEX_LEN + 1];
    memset(mixed_case, 'A', EOH_SHA256_HEX_LEN);
    mixed_case[EOH_SHA256_HEX_LEN] = '\0';
    CHECK(eoh_hex_decode_sha256(mixed_case, out), "uppercase hex digits are accepted");
}

static void test_sha256_equal(void)
{
    printf("[hex] sha256 byte-buffer comparison\n");
    uint8_t a[EOH_SHA256_LEN], b[EOH_SHA256_LEN];
    memset(a, 0x42, sizeof(a));
    memset(b, 0x42, sizeof(b));
    CHECK(eoh_sha256_equal(a, b), "identical buffers compare equal");
    b[EOH_SHA256_LEN - 1] ^= 0x01;
    CHECK(!eoh_sha256_equal(a, b), "a single differing byte at the end is detected");
    b[EOH_SHA256_LEN - 1] ^= 0x01;
    b[0] ^= 0x01;
    CHECK(!eoh_sha256_equal(a, b), "a single differing byte at the start is detected");
}

int main(void)
{
    test_roundtrip();
    test_known_vector();
    test_decode_rejects_bad_input();
    test_sha256_equal();

    if (failures == 0) {
        printf("ALL HOST TESTS PASSED\n");
        return 0;
    }
    printf("%d FAILURE(S)\n", failures);
    return 1;
}
