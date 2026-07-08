/*
 * esp-ota-https -- portable (pure C99, no ESP-IDF headers) semantic
 * version comparison, used to decide whether a manifest-advertised
 * firmware version is actually newer than what's currently running,
 * before any bytes are downloaded or flashed.
 *
 * Same MAJOR.MINOR.REVISION precedence logic pattern as the sibling
 * secure-ota project's version_check.c -- reimplemented here from
 * scratch for this repo (no cross-repo import).
 */
#ifndef ESP_OTA_HTTPS_VERSION_H
#define ESP_OTA_HTTPS_VERSION_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t major;
    uint8_t minor;
    uint16_t revision;
} eoh_version_t;

/** Parses a "MAJOR.MINOR.REVISION" string (e.g. "1.2.3"). Returns false
 * on a malformed string (out_version is left unmodified in that case).
 * major/minor must fit in 0..255, revision in 0..65535. */
bool eoh_version_parse(const char *str, eoh_version_t *out_version);

/** Returns <0 if a<b, 0 if a==b, >0 if a>b (major, then minor, then revision). */
int eoh_version_compare(const eoh_version_t *a, const eoh_version_t *b);

/** Convenience wrapper: true iff `candidate` is strictly newer than `current`. */
bool eoh_version_is_upgrade(const eoh_version_t *current, const eoh_version_t *candidate);

#endif /* ESP_OTA_HTTPS_VERSION_H */
