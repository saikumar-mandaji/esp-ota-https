/*
 * main.c -- ESP-IDF HTTPS OTA application.
 *
 * NOT HOST-TESTABLE: this file drives the real esp_http_client,
 * esp_https_ota, Wi-Fi driver, and mbedtls TLS stack. It has only been
 * built by inspection in this dev environment (no ESP32 hardware or IDF
 * toolchain available here) -- see docs/VERIFICATION.md for full
 * disclosure. The manifest parsing, semantic version comparison, and
 * hex/SHA-256-bytes helpers it calls into (src/manifest.c, src/version.c,
 * src/hexutil.c) ARE host-tested; see tests/host/.
 *
 * Flow (see docs/ARCHITECTURE.md for the full rationale):
 *   1. Connect to Wi-Fi.
 *   2. Fetch the JSON update manifest over HTTPS, with the connection's
 *      server certificate pinned against the embedded CA cert (no
 *      "skip verification" shortcuts).
 *   3. Parse the manifest with the portable, hand-rolled parser in
 *      src/manifest.c -- reject anything malformed BEFORE going near
 *      esp_https_ota.
 *   4. Compare the manifest's version against CURRENT_FW_VERSION. Only
 *      proceed if it's strictly newer.
 *   5. Run esp_https_ota against the manifest's url, again with the
 *      same pinned CA cert.
 *   6. Best-effort post-flash SHA-256 check: read back the bytes just
 *      written to the (not-yet-booted) update partition and compare
 *      against the manifest's sha256. If esp_https_ota already set that
 *      partition as the boot target and our hash check fails, revert
 *      the boot partition pointer back to the currently-running
 *      partition so a corrupt/wrong image is never booted. See
 *      docs/VERIFICATION.md for the narrow window this does NOT close
 *      (a brief moment after esp_https_ota_finish() where the bad image
 *      is technically marked bootable) and why it doesn't matter in
 *      practice (nothing reboots in between on this code path).
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "mbedtls/sha256.h"

#include "esp_ota_https/manifest.h"
#include "esp_ota_https/version.h"
#include "esp_ota_https/hexutil.h"

static const char *TAG = "esp-ota-https";

/* --- Replace these three with your own deployment's values. --- */
#define WIFI_SSID       "your-wifi-ssid"
#define WIFI_PASSWORD   "your-wifi-password"
#define MANIFEST_URL    "https://example-ota-server.invalid/manifest.json"

/* Version baked into THIS build. Bump it (and re-flash via a trusted
 * path) whenever you cut a release; the manifest on the server
 * advertises the version devices should be upgrading TO. */
#define CURRENT_FW_VERSION "1.0.0"

/* Max manifest response size we'll buffer in RAM. The manifest is a
 * few dozen bytes of JSON, not a firmware image -- this is generous
 * headroom, not an attempt to stream something large. */
#define MANIFEST_MAX_BODY_LEN 2048

/* CA certificate embedded at build time via EMBED_TXTFILES (see
 * main/CMakeLists.txt). This is a throwaway self-signed EXAMPLE
 * certificate generated for this repo -- see main/certs/README.md.
 * Replace main/certs/example_ca_cert.pem with your real update
 * server's actual CA certificate before using this in any real
 * deployment; a placeholder cert will make every TLS handshake against
 * a real server fail closed (which is the safe failure mode, but not
 * a working one). */
extern const uint8_t server_cert_pem_start[] asm("_binary_example_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[]   asm("_binary_example_ca_cert_pem_end");

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected, retrying...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_connect_blocking(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to Wi-Fi SSID \"%s\"...", WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                            pdFALSE, pdTRUE, pdMS_TO_TICKS(30000));
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "Wi-Fi connect timed out");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Wi-Fi connected");
    return ESP_OK;
}

/* Accumulates the HTTP response body for the manifest fetch into a
 * fixed-size caller-owned buffer. */
typedef struct {
    char *buf;
    size_t cap;
    size_t len;
} manifest_fetch_ctx_t;

static esp_err_t manifest_http_event_handler(esp_http_client_event_t *evt)
{
    manifest_fetch_ctx_t *ctx = (manifest_fetch_ctx_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (ctx->len + evt->data_len >= ctx->cap) {
            ESP_LOGE(TAG, "Manifest response too large for buffer (>%u bytes)", (unsigned)ctx->cap);
            return ESP_FAIL;
        }
        memcpy(ctx->buf + ctx->len, evt->data, evt->data_len);
        ctx->len += evt->data_len;
    }
    return ESP_OK;
}

/* Fetches the manifest over HTTPS with the server certificate pinned
 * against our embedded CA cert (esp_http_client_config_t.cert_pem).
 * We deliberately do NOT set skip_cert_common_name_check, and we do
 * NOT pass a NULL cert_pem -- both of those are the common insecure
 * shortcuts this project exists to avoid. See docs/ARCHITECTURE.md. */
static esp_err_t fetch_manifest(char *out_buf, size_t out_cap, size_t *out_len)
{
    manifest_fetch_ctx_t ctx = { .buf = out_buf, .cap = out_cap, .len = 0 };

    esp_http_client_config_t config = {
        .url = MANIFEST_URL,
        .cert_pem = (const char *)server_cert_pem_start,
        .event_handler = manifest_http_event_handler,
        .user_data = &ctx,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Manifest fetch failed: %s", esp_err_to_name(err));
        return err;
    }
    if (status != 200) {
        ESP_LOGE(TAG, "Manifest fetch got HTTP status %d", status);
        return ESP_FAIL;
    }

    out_buf[ctx.len] = '\0';
    *out_len = ctx.len;
    return ESP_OK;
}

/* Best-effort post-flash integrity check: reads back exactly the
 * number of bytes esp_https_ota reports having written to the update
 * partition and hashes them, then compares against the manifest's
 * sha256. This is defense-in-depth ON TOP OF TLS -- it catches a
 * compromised or misconfigured server serving a corrupted/wrong binary
 * even over an otherwise-valid, cert-pinned TLS connection. It is NOT
 * a substitute for TLS (a MITM could otherwise swap both the manifest
 * AND the binary together) -- see docs/ARCHITECTURE.md. */
static bool verify_flashed_image_sha256(const esp_partition_t *part, int image_len,
                                         const uint8_t expected[EOH_SHA256_LEN])
{
    if (part == NULL || image_len <= 0) {
        return false;
    }

    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    bool mbedtls_ok = (mbedtls_sha256_starts(&sha_ctx, 0 /* SHA-256, not SHA-224 */) == 0);

    uint8_t chunk[512];
    size_t remaining = (size_t)image_len;
    size_t offset = 0;
    esp_err_t err = ESP_OK;

    while (mbedtls_ok && remaining > 0 && err == ESP_OK) {
        size_t take = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        err = esp_partition_read(part, offset, chunk, take);
        if (err == ESP_OK) {
            mbedtls_ok = (mbedtls_sha256_update(&sha_ctx, chunk, take) == 0);
            offset += take;
            remaining -= take;
        }
    }

    uint8_t computed[EOH_SHA256_LEN];
    if (mbedtls_ok) {
        mbedtls_ok = (mbedtls_sha256_finish(&sha_ctx, computed) == 0);
    }
    mbedtls_sha256_free(&sha_ctx);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Reading back flashed image for verification failed: %s", esp_err_to_name(err));
        return false;
    }
    if (!mbedtls_ok) {
        ESP_LOGE(TAG, "SHA-256 computation over flashed image failed");
        return false;
    }

    return eoh_sha256_equal(computed, expected);
}

/* Runs the actual OTA update against manifest->url, using the advanced
 * (non-blocking-loop) esp_https_ota API so we can learn exactly how
 * many bytes were written and read them back afterwards for the
 * SHA-256 check above. Same pinned CA cert as the manifest fetch. */
static esp_err_t run_ota_update(const eoh_manifest_t *manifest, const uint8_t expected_sha256[EOH_SHA256_LEN])
{
    esp_http_client_config_t http_config = {
        .url = manifest->url,
        .cert_pem = (const char *)server_cert_pem_start,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Remember which partition we're about to overwrite -- this is
     * deterministic (esp_https_ota_begin already claimed it internally
     * via the same esp_ota_get_next_update_partition() logic) so it is
     * safe to compute independently here for the post-flash read-back. */
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *target = esp_ota_get_next_update_partition(running);

    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_perform failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(https_ota_handle);
        return err;
    }

    if (!esp_https_ota_is_complete_data_received(https_ota_handle)) {
        ESP_LOGE(TAG, "OTA data download incomplete");
        esp_https_ota_abort(https_ota_handle);
        return ESP_FAIL;
    }

    int image_len_read = esp_https_ota_get_image_len_read(https_ota_handle);

    esp_err_t finish_err = esp_https_ota_finish(https_ota_handle);
    if (finish_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_finish failed: %s", esp_err_to_name(finish_err));
        return finish_err;
    }

    /* At this point esp_https_ota_finish() has already validated the
     * image header/checksum and set `target` as the boot partition.
     * Our SHA-256 check below is an EXTRA layer on top of that --
     * comparing against the manifest's independently-supplied digest,
     * not just internal image consistency. If it fails, revert the
     * boot partition back to the currently-running one so the device
     * does not boot into a wrong/corrupt image on next reset. */
    if (!verify_flashed_image_sha256(target, image_len_read, expected_sha256)) {
        ESP_LOGE(TAG, "Post-flash SHA-256 mismatch -- reverting boot partition, NOT booting new image");
        esp_ota_set_boot_partition(running);
        return ESP_ERR_INVALID_CRC;
    }

    ESP_LOGI(TAG, "OTA image SHA-256 verified against manifest -- safe to reboot into new firmware");
    return ESP_OK;
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "esp-ota-https starting, current firmware version %s", CURRENT_FW_VERSION);
    ESP_LOGI(TAG, "Pinned CA cert size: %d bytes",
             (int)(server_cert_pem_end - server_cert_pem_start));

    if (wifi_connect_blocking() != ESP_OK) {
        ESP_LOGE(TAG, "Cannot proceed without Wi-Fi; giving up");
        return;
    }

    char manifest_buf[MANIFEST_MAX_BODY_LEN];
    size_t manifest_len = 0;
    if (fetch_manifest(manifest_buf, sizeof(manifest_buf), &manifest_len) != ESP_OK) {
        ESP_LOGE(TAG, "Could not fetch update manifest; not updating");
        return;
    }

    eoh_manifest_t manifest;
    if (!eoh_manifest_parse(manifest_buf, manifest_len, &manifest)) {
        ESP_LOGE(TAG, "Manifest failed validation (malformed JSON, missing/invalid fields, or "
                      "sha256 not exactly 64 hex chars) -- refusing to proceed with OTA");
        return;
    }
    ESP_LOGI(TAG, "Manifest OK: version=%s url=%s sha256=%s",
             manifest.version, manifest.url, manifest.sha256_hex);

    eoh_version_t current, candidate;
    if (!eoh_version_parse(CURRENT_FW_VERSION, &current) || !eoh_version_parse(manifest.version, &candidate)) {
        ESP_LOGE(TAG, "Could not parse version strings for comparison; not updating");
        return;
    }

    if (!eoh_version_is_upgrade(&current, &candidate)) {
        ESP_LOGI(TAG, "Manifest version %s is not newer than running version %s -- nothing to do",
                 manifest.version, CURRENT_FW_VERSION);
        return;
    }

    uint8_t expected_sha256[EOH_SHA256_LEN];
    if (!eoh_hex_decode_sha256(manifest.sha256_hex, expected_sha256)) {
        /* Should be unreachable -- eoh_manifest_parse() already validated
         * this -- but never trust that from two functions away. */
        ESP_LOGE(TAG, "Manifest sha256 failed to decode after passing parse validation; aborting");
        return;
    }

    ESP_LOGI(TAG, "Manifest version %s is newer than running version %s -- starting OTA",
             manifest.version, CURRENT_FW_VERSION);

    err = run_ota_update(&manifest, expected_sha256);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA update succeeded and verified -- rebooting");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA update failed or failed verification (%s) -- staying on current firmware",
                 esp_err_to_name(err));
    }
}
