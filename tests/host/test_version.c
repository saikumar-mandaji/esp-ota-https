/* Host tests for the version-compare utility, compiled against the
 * real production source in src/version.c. */
#include "esp_ota_https/version.h"
#include <stdio.h>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
    else { printf("  ok  : %s\n", msg); } \
} while (0)

static void test_parse_valid(void)
{
    printf("[parse] well-formed version strings\n");
    eoh_version_t v;
    CHECK(eoh_version_parse("1.2.3", &v) && v.major == 1 && v.minor == 2 && v.revision == 3,
          "\"1.2.3\" parses to {1,2,3}");
    CHECK(eoh_version_parse("0.0.1", &v) && v.major == 0 && v.minor == 0 && v.revision == 1,
          "\"0.0.1\" parses to {0,0,1}");
    CHECK(eoh_version_parse("255.255.65535", &v) && v.major == 255 && v.minor == 255 && v.revision == 65535,
          "max-range values parse correctly");
}

static void test_parse_invalid(void)
{
    printf("[parse] malformed strings are rejected, not silently misparsed\n");
    eoh_version_t v;
    CHECK(!eoh_version_parse("1.4", &v), "missing revision field is rejected");
    CHECK(!eoh_version_parse("1.4.0.1", &v), "trailing extra field is rejected");
    CHECK(!eoh_version_parse("a.b.c", &v), "non-numeric fields are rejected");
    CHECK(!eoh_version_parse("", &v), "empty string is rejected");
    CHECK(!eoh_version_parse("256.0.0", &v), "out-of-range major (>255) is rejected");
    CHECK(!eoh_version_parse("1.256.0", &v), "out-of-range minor (>255) is rejected");
    CHECK(!eoh_version_parse("1.0.65536", &v), "out-of-range revision (>65535) is rejected");
    CHECK(!eoh_version_parse(NULL, &v), "NULL input is rejected, not dereferenced");
}

static void test_compare_and_upgrade(void)
{
    printf("[compare] ordering matches semantic-version precedence (major > minor > revision)\n");
    eoh_version_t v1_0_0, v1_1_0, v1_0_5, v2_0_0;
    eoh_version_parse("1.0.0", &v1_0_0);
    eoh_version_parse("1.1.0", &v1_1_0);
    eoh_version_parse("1.0.5", &v1_0_5);
    eoh_version_parse("2.0.0", &v2_0_0);

    CHECK(eoh_version_compare(&v1_0_0, &v1_0_0) == 0, "a version equals itself");
    CHECK(eoh_version_compare(&v1_1_0, &v1_0_0) > 0, "higher minor outranks lower minor at same major");
    CHECK(eoh_version_compare(&v1_0_5, &v1_0_0) > 0, "higher revision outranks lower revision");
    CHECK(eoh_version_compare(&v2_0_0, &v1_1_0) > 0, "higher major always outranks any minor/revision");
    CHECK(eoh_version_compare(&v1_0_0, &v1_0_5) < 0, "compare is anti-symmetric");

    CHECK(eoh_version_is_upgrade(&v1_0_0, &v1_1_0), "1.1.0 is an upgrade from 1.0.0");
    CHECK(!eoh_version_is_upgrade(&v1_1_0, &v1_0_0), "1.0.0 is NOT an upgrade from 1.1.0 (downgrade)");
    CHECK(!eoh_version_is_upgrade(&v1_0_0, &v1_0_0), "identical version is not an upgrade (no-op update rejected)");
}

int main(void)
{
    test_parse_valid();
    test_parse_invalid();
    test_compare_and_upgrade();

    if (failures == 0) {
        printf("ALL HOST TESTS PASSED\n");
        return 0;
    }
    printf("%d FAILURE(S)\n", failures);
    return 1;
}
