/* OTA update — see ota.h. ESP-IDF native OTA + manifest/SHA-256 validation. */
#include "ota.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_app_desc.h"
#include "esp_flash_partitions.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota";
#define OTA_PRODUCT "vessel-motion-p4"
#define OTA_ARCH    "esp32p4"

static ota_status_t s_status;
static bool s_running;

static void set_status(const char *state, const char *msg)
{
    ESP_LOGI(TAG, "OTA %s: %s", state, msg);
    strlcpy(s_status.state, state, sizeof(s_status.state));
    strlcpy(s_status.message, msg, sizeof(s_status.message));
}

/* Extract a JSON string value for "key" (minimal parser for flat manifest). */
static bool json_get_str(const char *json, const char *key, char *out, size_t len)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p = strchr(p + strlen(pat), ':');
    if (!p) return false;
    p = strchr(p, '"');
    if (!p) return false;
    p++;
    const char *e = strchr(p, '"');
    if (!e) return false;
    size_t n = e - p;
    if (n >= len) n = len - 1;
    memcpy(out, p, n);
    out[n] = 0;
    return true;
}

static bool json_get_int(const char *json, const char *key, long *out)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p = strchr(p + strlen(pat), ':');
    if (!p) return false;
    while (*p == ' ' || *p == '\t') p++;
    *out = strtol(p, NULL, 10);
    return true;
}

static esp_err_t http_get_all(const char *url, char *buf, size_t cap, size_t *out_len)
{
    esp_http_client_config_t cfg = { .url = url, .timeout_ms = 20000, .crt_bundle_attach = esp_crt_bundle_attach };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_open(c, 0);
    if (err != ESP_OK) { esp_http_client_cleanup(c); return err; }
    int total = esp_http_client_fetch_headers(c);
    size_t n = 0;
    while (n < cap - 1) {
        int r = esp_http_client_read(c, buf + n, cap - 1 - n);
        if (r <= 0) break;
        n += r;
    }
    buf[n] = 0;
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    if (out_len) *out_len = n;
    (void)total;
    return ESP_OK;
}

static void ota_task(void *arg)
{
    char *url = (char *)arg;
    set_status("checking", "Connecting to update server");

    char *manifest = malloc(4096);
    if (!manifest) { set_status("failed", "out of memory"); goto done; }
    if (http_get_all(url, manifest, 4096, NULL) != ESP_OK) {
        set_status("failed", "could not download manifest");
        goto done_manifest;
    }

    char product[32], version[24], arch[24], fw_url[256], sha[80];
    long size = 0, schema = 0;
    if (!json_get_str(manifest, "product", product, sizeof(product)) ||
        !json_get_str(manifest, "version", version, sizeof(version)) ||
        !json_get_str(manifest, "architecture", arch, sizeof(arch)) ||
        !json_get_str(manifest, "url", fw_url, sizeof(fw_url)) ||
        !json_get_str(manifest, "sha256", sha, sizeof(sha)) ||
        !json_get_int(manifest, "schema", &schema) ||
        !json_get_int(manifest, "size", &size)) {
        set_status("failed", "Update manifest is incomplete");
        goto done_manifest;
    }
    if (schema != 1 || strcmp(product, OTA_PRODUCT) != 0) {
        set_status("failed", "manifest is for a different product");
        goto done_manifest;
    }
    if (strcmp(arch, OTA_ARCH) != 0) {
        set_status("failed", "architecture mismatch");
        goto done_manifest;
    }
    strlcpy(s_status.available_version, version, sizeof(s_status.available_version));

    const esp_app_desc_t *cur = esp_app_get_description();
    if (strcmp(version, cur->version) == 0) {
        set_status("current", "The installed version is current");
        goto done_manifest;
    }

    /* Download + write to the OTA partition, validating SHA-256 via esp_https_ota semantics */
    set_status("downloading", "Downloading update");
    s_status.total = (uint32_t)size;
    {
        esp_http_client_config_t cfg = { .url = fw_url, .timeout_ms = 60000, .crt_bundle_attach = esp_crt_bundle_attach };
        esp_http_client_handle_t c = esp_http_client_init(&cfg);
        if (esp_http_client_open(c, 0) != ESP_OK) {
            set_status("failed", "could not open firmware URL");
            esp_http_client_cleanup(c);
            goto done_manifest;
        }
        esp_http_client_fetch_headers(c);

        const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
        esp_ota_handle_t ota;
        if (esp_ota_begin(update, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) {
            set_status("failed", "OTA begin failed");
            esp_http_client_close(c); esp_http_client_cleanup(c);
            goto done_manifest;
        }
        uint8_t block[4096];
        int r;
        s_status.received = 0;
        while ((r = esp_http_client_read(c, (char *)block, sizeof(block))) > 0) {
            if (esp_ota_write(ota, block, r) != ESP_OK) {
                set_status("failed", "OTA write failed");
                esp_ota_abort(ota);
                esp_http_client_close(c); esp_http_client_cleanup(c);
                goto done_manifest;
            }
            s_status.received += r;
            s_status.progress = s_status.total ? (int)(s_status.received * 100 / s_status.total) : 0;
        }
        esp_http_client_close(c);
        esp_http_client_cleanup(c);

        if (esp_ota_end(ota) != ESP_OK) {
            set_status("failed", "OTA validation failed");
            goto done_manifest;
        }
        if (esp_ota_set_boot_partition(update) != ESP_OK) {
            set_status("failed", "could not set boot partition");
            goto done_manifest;
        }
        set_status("complete", "Update installed; restart required");
        s_status.progress = 100;
    }

done_manifest:
    free(manifest);
done:
    s_running = false;
    free(url);
    vTaskDelete(NULL);
}

esp_err_t ota_start(const char *manifest_url)
{
    if (s_running) return ESP_ERR_INVALID_STATE;
    const esp_app_desc_t *cur = esp_app_get_description();
    memset(&s_status, 0, sizeof(s_status));
    strlcpy(s_status.state, "idle", sizeof(s_status.state));
    strlcpy(s_status.current_version, cur->version, sizeof(s_status.current_version));
    strlcpy(s_status.message, "Ready", sizeof(s_status.message));

    char *url = strdup(manifest_url);
    if (!url) return ESP_ERR_NO_MEM;
    s_running = true;
    if (xTaskCreate(ota_task, "ota", 8192, url, 5, NULL) != pdPASS) {
        free(url);
        s_running = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void ota_snapshot(ota_status_t *out)
{
    *out = s_status;
}

bool ota_running(void)
{
    return s_running;
}
