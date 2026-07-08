/* Host tests for the manifest JSON parser, compiled against the real
 * production source in src/manifest.c. */
#include "esp_ota_https/manifest.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else { printf("  ok  : %s\n", msg); } \
} while (0)

#define VALID_SHA256 "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

static bool parse(const char *s, eoh_manifest_t *out)
{
    return eoh_manifest_parse(s, strlen(s), out);
}

static void test_well_formed(void)
{
    printf("[manifest] well-formed manifests parse correctly\n");
    eoh_manifest_t m;

    CHECK(parse("{\"version\":\"1.2.3\",\"url\":\"https://example.com/fw.bin\",\"sha256\":\"" VALID_SHA256 "\"}", &m),
          "canonical field order parses");
    CHECK(strcmp(m.version, "1.2.3") == 0, "version field extracted correctly");
    CHECK(strcmp(m.url, "https://example.com/fw.bin") == 0, "url field extracted correctly");
    CHECK(strcmp(m.sha256_hex, VALID_SHA256) == 0, "sha256 field extracted correctly");

    CHECK(parse("{ \"sha256\" : \"" VALID_SHA256 "\" , \"url\":\"https://x/y\", \"version\":\"0.0.1\" }", &m),
          "reordered fields with extra whitespace still parse");
    CHECK(strcmp(m.version, "0.0.1") == 0, "reordered manifest: version correct");

    CHECK(parse("{\"version\":\"1.0.0\",\"url\":\"https://x\",\"sha256\":\"" VALID_SHA256 "\",\"note\":\"ignored\"}", &m),
          "unknown extra string field is tolerated (forward-compatible)");
}

static void test_missing_field(void)
{
    printf("[manifest] missing required fields are rejected\n");
    eoh_manifest_t m;
    CHECK(!parse("{\"url\":\"https://x\",\"sha256\":\"" VALID_SHA256 "\"}", &m),
          "missing version field is rejected");
    CHECK(!parse("{\"version\":\"1.0.0\",\"sha256\":\"" VALID_SHA256 "\"}", &m),
          "missing url field is rejected");
    CHECK(!parse("{\"version\":\"1.0.0\",\"url\":\"https://x\"}", &m),
          "missing sha256 field is rejected");
    CHECK(!parse("{}", &m), "empty object is rejected");
}

static void test_truncated(void)
{
    printf("[manifest] truncated JSON is rejected\n");
    eoh_manifest_t m;
    CHECK(!parse("{\"version\":\"1.0.0\",\"url\":\"https://x\",\"sha256\":\"" VALID_SHA256, &m),
          "manifest missing closing brace is rejected");
    CHECK(!parse("{\"version\":\"1.0", &m), "manifest cut off mid-string is rejected");
    CHECK(!parse("", &m), "empty input is rejected");
    CHECK(!parse("{", &m), "lone opening brace is rejected");
}

static void test_wrong_types(void)
{
    printf("[manifest] wrong JSON value types are rejected\n");
    eoh_manifest_t m;
    CHECK(!parse("{\"version\":1.0,\"url\":\"https://x\",\"sha256\":\"" VALID_SHA256 "\"}", &m),
          "numeric version (not a string) is rejected");
    CHECK(!parse("{\"version\":\"1.0.0\",\"url\":null,\"sha256\":\"" VALID_SHA256 "\"}", &m),
          "null url (not a string) is rejected");
    CHECK(!parse("{\"version\":\"1.0.0\",\"url\":[\"https://x\"],\"sha256\":\"" VALID_SHA256 "\"}", &m),
          "array url (not a string) is rejected");
    CHECK(!parse("[\"not\",\"an\",\"object\"]", &m), "top-level JSON array is rejected");
}

static void test_bad_sha256(void)
{
    printf("[manifest] sha256 field that isn't exactly 64 hex chars is rejected\n");
    eoh_manifest_t m;
    CHECK(!parse("{\"version\":\"1.0.0\",\"url\":\"https://x\",\"sha256\":\"abcd\"}", &m),
          "too-short sha256 is rejected");
    CHECK(!parse("{\"version\":\"1.0.0\",\"url\":\"https://x\",\"sha256\":\"" VALID_SHA256 "ff\"}", &m),
          "too-long (66 char) sha256 is rejected");
    CHECK(!parse("{\"version\":\"1.0.0\",\"url\":\"https://x\","
                 "\"sha256\":\"zz23456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}", &m),
          "64-char sha256 containing non-hex characters is rejected");
    CHECK(!parse("{\"version\":\"1.0.0\",\"url\":\"https://x\",\"sha256\":\"\"}", &m),
          "empty sha256 is rejected");
}

int main(void)
{
    test_well_formed();
    test_missing_field();
    test_truncated();
    test_wrong_types();
    test_bad_sha256();

    if (failures == 0) {
        printf("ALL HOST TESTS PASSED\n");
        return 0;
    }
    printf("%d FAILURE(S)\n", failures);
    return 1;
}
