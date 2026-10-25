/* Wi-Fi manager for the ESP32-P4-NANO's ESP32-C6 coprocessor.
 * esp_wifi_remote makes the standard esp_wifi_* API route to the C6 slave. */
#include "wifi_mgr.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/ip4_addr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wifi";

#define NVS_NS        "wifi"
#define NVS_KEY_SSID  "ssid"
#define NVS_KEY_PASS  "pass"
#define MAX_SCAN      24
#define MONITOR_MS    5000

static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static bool s_connected;
static bool s_fallback_active;
static esp_ip4_addr_t s_sta_ip;
static char s_saved_ssid[33];

static void save_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, NVS_KEY_SSID, ssid);
        nvs_set_str(h, NVS_KEY_PASS, pass ? pass : "");
        nvs_commit(h);
        nvs_close(h);
    }
}

static bool load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    bool ok = false;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        ok = nvs_get_str(h, NVS_KEY_SSID, ssid, &ssid_len) == ESP_OK && ssid[0];
        if (ok && nvs_get_str(h, NVS_KEY_PASS, pass, &pass_len) != ESP_OK) pass[0] = 0;
        nvs_close(h);
    }
    return ok;
}

static void start_fallback_ap(void)
{
    if (s_fallback_active) return;
    wifi_config_t ap = { 0 };
    strlcpy((char *)ap.ap.ssid, WIFI_FALLBACK_SSID, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(WIFI_FALLBACK_SSID);
    strlcpy((char *)ap.ap.password, WIFI_FALLBACK_PASS, sizeof(ap.ap.password));
    ap.ap.channel = 1;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection = 4;
    /* Demo order: mode -> config -> start. Pure AP when no STA credentials exist. */
    esp_wifi_set_mode(s_sta_netif ? WIFI_MODE_APSTA : WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    esp_wifi_start();
    s_fallback_active = true;
    ESP_LOGI(TAG, "Fallback AP up: %s (%s)", WIFI_FALLBACK_SSID, WIFI_FALLBACK_IP);
}

static void stop_fallback_ap(void)
{
    if (!s_fallback_active) return;
    s_fallback_active = false;
    wifi_config_t ap = { 0 };
    ap.ap.ssid[0] = 0;
    esp_wifi_set_config(WIFI_IF_AP, &ap);  /* empty SSID disables AP */
    ESP_LOGI(TAG, "Fallback AP down");
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            if (s_saved_ssid[0]) esp_wifi_connect();  /* only with real credentials */
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            s_connected = false;
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "Client joined AP");
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "Client left AP");
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        s_sta_ip = e->ip_info.ip;
        s_connected = true;
        ESP_LOGI(TAG, "Wi-Fi connected, IP: " IPSTR, IP2STR(&s_sta_ip));
        stop_fallback_ap();
    }
}

esp_err_t vm_wifi_init(void)
{
    s_ap_netif = esp_netif_create_default_wifi_ap();

    /* Fallback AP address 192.168.4.1 */
    esp_netif_ip_info_t ap_ip = { 0 };
    IP4_ADDR(&ap_ip.ip, 192, 168, 4, 1);
    IP4_ADDR(&ap_ip.gw, 192, 168, 4, 1);
    IP4_ADDR(&ap_ip.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(s_ap_netif);
    esp_netif_set_ip_info(s_ap_netif, &ap_ip);
    esp_netif_dhcps_start(s_ap_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        /* The ESP32-C6 slave may still be resetting; fail gracefully and let
         * the caller retry rather than aborting the whole chip. */
        ESP_LOGE(TAG, "esp_wifi_init failed (%s); Wi-Fi unavailable this boot",
                 esp_err_to_name(err));
        return err;
    }
    /* RAM storage per the Waveshare eth2ap demo; avoids flash-config interplay */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL));

    /* Try saved credentials, else fallback AP */
    char ssid[33] = { 0 }, pass[65] = { 0 };
    if (load_credentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        strlcpy(s_saved_ssid, ssid, sizeof(s_saved_ssid));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
        vm_wifi_connect(ssid, pass);
    } else {
        start_fallback_ap();
    }
    ESP_LOGI(TAG, "Wi-Fi initialised (ESP32-C6 coprocessor)");
    return ESP_OK;
}

bool wifi_is_connected(void) { return s_connected; }
bool wifi_is_fallback_active(void) { return s_fallback_active; }

esp_err_t wifi_sta_ip(char *buf, size_t len)
{
    if (!s_connected) return ESP_ERR_INVALID_STATE;
    snprintf(buf, len, IPSTR, IP2STR(&s_sta_ip));
    return ESP_OK;
}

int wifi_scan(wifi_network_t *out, int max)
{
    /* Scanning requires the STA interface. When no credentials are saved the
     * board runs the fallback AP only (WIFI_MODE_AP), which cannot scan —
     * create the STA netif and switch to APSTA first (AP stays up). */
    if (!s_sta_netif) s_sta_netif = esp_netif_create_default_wifi_sta();
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) != ESP_OK) mode = WIFI_MODE_NULL;
    if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) {
        esp_wifi_set_mode(s_fallback_active ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    }
    wifi_scan_config_t scan = { .show_hidden = false };
    if (esp_wifi_scan_start(&scan, true) != ESP_OK) return 0;
    uint16_t n = MAX_SCAN;
    wifi_ap_record_t aps[MAX_SCAN];
    if (esp_wifi_scan_get_ap_records(&n, aps) != ESP_OK) return 0;

    /* dedup by ssid, keep strongest */
    int count = 0;
    for (int i = 0; i < n; i++) {
        const char *ssid = (const char *)aps[i].ssid;
        if (!ssid[0]) continue;
        int rssi = aps[i].rssi;  /* esp_wifi already gives dBm */
        int idx = -1;
        for (int j = 0; j < count; j++) if (!strcmp(out[j].ssid, ssid)) { idx = j; break; }
        if (idx >= 0) {
            if (rssi > out[idx].rssi) out[idx].rssi = rssi;
        } else if (count < max) {
            strlcpy(out[count].ssid, ssid, sizeof(out[count].ssid));
            out[count].rssi = rssi;
            out[count].secure = aps[i].authmode != WIFI_AUTH_OPEN;
            count++;
        }
    }
    /* sort by rssi desc */
    for (int i = 0; i < count - 1; i++)
        for (int j = i + 1; j < count; j++)
            if (out[j].rssi > out[i].rssi) { wifi_network_t t = out[i]; out[i] = out[j]; out[j] = t; }
    return count;
}

esp_err_t vm_wifi_connect(const char *ssid, const char *password)
{
    if (!ssid || !ssid[0] || strlen(ssid) > 32) return ESP_ERR_INVALID_ARG;
    if (!s_sta_netif) s_sta_netif = esp_netif_create_default_wifi_sta();
    /* Leaving fallback AP: stop before switching to STA mode. */
    if (s_fallback_active) {
        esp_wifi_stop();
        s_fallback_active = false;
    }
    wifi_config_t sta = { 0 };
    strlcpy((char *)sta.sta.ssid, ssid, sizeof(sta.sta.ssid));
    if (password) strlcpy((char *)sta.sta.password, password, sizeof(sta.sta.password));
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta);
    if (err != ESP_OK) return err;
    err = esp_wifi_connect();
    if (err == ESP_OK) {
        save_credentials(ssid, password ? password : "");
        strlcpy(s_saved_ssid, ssid, sizeof(s_saved_ssid));
    }
    return err;
}

esp_err_t vm_wifi_disconnect(void)
{
    return esp_wifi_disconnect();
}

esp_err_t wifi_forget(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_KEY_SSID);
        nvs_erase_key(h, NVS_KEY_PASS);
        nvs_commit(h);
        nvs_close(h);
    }
    s_saved_ssid[0] = 0;
    return esp_wifi_disconnect();
}

static void monitor_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(MONITOR_MS));
        if (!s_connected && !s_fallback_active) {
            start_fallback_ap();
        }
    }
}

void wifi_monitor_start(void)
{
    xTaskCreate(monitor_task, "wifi-mon", 3072, NULL, 3, NULL);
}
