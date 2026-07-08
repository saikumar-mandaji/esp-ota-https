/*
 * Hand-rolled recursive-descent-ish parser for the fixed manifest shape
 * documented in include/esp_ota_https/manifest.h. Not a general JSON
 * parser: object values are only ever JSON strings, keys are matched
 * against a fixed known set, and any structural surprise is a hard
 * rejection rather than a best-effort partial parse.
 */
#include "esp_ota_https/manifest.h"
#include <string.h>

typedef struct {
    const char *p;
    const char *end;
} cursor_t;

static void skip_ws(cursor_t *c)
{
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r')) {
        c->p++;
    }
}

static bool expect_char(cursor_t *c, char ch)
{
    skip_ws(c);
    if (c->p < c->end && *c->p == ch) {
        c->p++;
        return true;
    }
    return false;
}

/* Parses a JSON string literal (including surrounding quotes) into
 * out[0..out_cap-1] (out_cap includes room for the NUL terminator).
 * Supports only the escapes \" \\ \/ -- anything else (including
 * \uXXXX) is rejected, since none of the manifest's fields need them. */
static bool parse_string(cursor_t *c, char *out, size_t out_cap)
{
    skip_ws(c);
    if (c->p >= c->end || *c->p != '"') {
        return false;
    }
    c->p++;

    size_t n = 0;
    while (c->p < c->end && *c->p != '"') {
        char ch = *c->p;
        char actual;

        if (ch == '\\') {
            c->p++;
            if (c->p >= c->end) {
                return false;
            }
            switch (*c->p) {
                case '"':  actual = '"';  break;
                case '\\': actual = '\\'; break;
                case '/':  actual = '/';  break;
                default:   return false; /* unsupported escape */
            }
            c->p++;
        } else if ((unsigned char)ch < 0x20) {
            return false; /* raw control character not allowed in a JSON string */
        } else {
            actual = ch;
            c->p++;
        }

        if (n + 1 >= out_cap) {
            return false; /* value too long for destination buffer */
        }
        out[n++] = actual;
    }

    if (c->p >= c->end || *c->p != '"') {
        return false; /* unterminated string */
    }
    c->p++;
    out[n] = '\0';
    return true;
}

bool eoh_manifest_parse(const char *json, size_t json_len, eoh_manifest_t *out)
{
    if (json == NULL || out == NULL || json_len == 0) {
        return false;
    }

    cursor_t c;
    c.p = json;
    c.end = json + json_len;
    if (c.end > c.p && *(c.end - 1) == '\0') {
        c.end--; /* tolerate a counted trailing NUL */
    }

    bool have_version = false, have_url = false, have_sha = false;
    char version_buf[EOH_MANIFEST_MAX_VERSION_LEN + 1];
    char url_buf[EOH_MANIFEST_MAX_URL_LEN + 1];
    char sha_buf[EOH_SHA256_HEX_LEN + 1];

    if (!expect_char(&c, '{')) {
        return false;
    }

    skip_ws(&c);
    if (c.p < c.end && *c.p == '}') {
        c.p++;
    } else {
        for (;;) {
            char key[32];
            if (!parse_string(&c, key, sizeof(key))) {
                return false;
            }
            if (!expect_char(&c, ':')) {
                return false;
            }

            if (strcmp(key, "version") == 0) {
                if (have_version || !parse_string(&c, version_buf, sizeof(version_buf))) {
                    return false;
                }
                have_version = true;
            } else if (strcmp(key, "url") == 0) {
                if (have_url || !parse_string(&c, url_buf, sizeof(url_buf))) {
                    return false;
                }
                have_url = true;
            } else if (strcmp(key, "sha256") == 0) {
                if (have_sha || !parse_string(&c, sha_buf, sizeof(sha_buf))) {
                    return false;
                }
                have_sha = true;
            } else {
                /* Unknown field: still must be a well-formed string value
                 * (the manifest shape has no non-string fields), discard it. */
                char discard[512];
                if (!parse_string(&c, discard, sizeof(discard))) {
                    return false;
                }
            }

            skip_ws(&c);
            if (c.p < c.end && *c.p == ',') {
                c.p++;
                continue;
            }
            break;
        }
        if (!expect_char(&c, '}')) {
            return false;
        }
    }

    if (!have_version || !have_url || !have_sha) {
        return false;
    }

    /* sha256 must be exactly 64 valid hex characters -- validate by
     * actually decoding it, not just checking length. */
    if (strlen(sha_buf) != EOH_SHA256_HEX_LEN) {
        return false;
    }
    uint8_t discard_bytes[EOH_SHA256_LEN];
    if (!eoh_hex_decode(sha_buf, EOH_SHA256_HEX_LEN, discard_bytes)) {
        return false;
    }

    memcpy(out->version, version_buf, sizeof(version_buf));
    memcpy(out->url, url_buf, sizeof(url_buf));
    memcpy(out->sha256_hex, sha_buf, sizeof(sha_buf));
    return true;
}
