#include "esp_ota_https/version.h"
#include <stdlib.h>

bool eoh_version_parse(const char *str, eoh_version_t *out_version)
{
    if (str == NULL) {
        return false;
    }

    const char *p = str;
    char *end;

    long major = strtol(p, &end, 10);
    if (end == p || *end != '.' || major < 0 || major > 255) {
        return false;
    }
    p = end + 1;

    long minor = strtol(p, &end, 10);
    if (end == p || *end != '.' || minor < 0 || minor > 255) {
        return false;
    }
    p = end + 1;

    long revision = strtol(p, &end, 10);
    if (end == p || *end != '\0' || revision < 0 || revision > 65535) {
        return false;
    }

    out_version->major = (uint8_t)major;
    out_version->minor = (uint8_t)minor;
    out_version->revision = (uint16_t)revision;
    return true;
}

int eoh_version_compare(const eoh_version_t *a, const eoh_version_t *b)
{
    if (a->major != b->major) {
        return (int)a->major - (int)b->major;
    }
    if (a->minor != b->minor) {
        return (int)a->minor - (int)b->minor;
    }
    return (int)a->revision - (int)b->revision;
}

bool eoh_version_is_upgrade(const eoh_version_t *current, const eoh_version_t *candidate)
{
    return eoh_version_compare(candidate, current) > 0;
}
