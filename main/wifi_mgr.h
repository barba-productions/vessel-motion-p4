/*
 * Wi-Fi manager for Vessel Motion P4 (Phase 3).
 * Uses the ESP32-C6 coprocessor over SDIO via esp_wifi_remote / esp_hosted.
 * Replicates the Pi project's Wi-Fi behavior:
 *   - station connect/disconnect/scan (rssi = signal/2 - 100, dedup, sorted)
 *   - fallback SoftAP "Vessel-Motion" (192.168.4.1) when no client connection
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_FALLBACK_SSID "Vessel-Motion"
#define WIFI_FALLBACK_PASS "vessel-motion"
#define WIFI_FALLBACK_IP   "192.168.4.1"

typedef struct {
    char ssid[33];
    int  rssi;
    bool secure;
} wifi_network_t;

/* Initialise Wi-Fi (C6 coprocessor), netif for STA+AP, event handlers.
 * Brings up the fallback AP if no saved connection succeeds.
 * (vm_ prefix avoids symbol clashes with esp_hosted's RPC layer.) */
esp_err_t vm_wifi_init(void);

/* Status snapshot */
bool wifi_is_connected(void);
bool wifi_is_fallback_active(void);
esp_err_t wifi_sta_ip(char *buf, size_t len);

/* Scan. Fills out[] (max), returns count. Sorted by rssi desc. */
int  wifi_scan(wifi_network_t *out, int max);

/* Connect to a network (saved to NVS for auto-reconnect). */
esp_err_t vm_wifi_connect(const char *ssid, const char *password);

/* Disconnect the station; fallback AP will come up. */
esp_err_t vm_wifi_disconnect(void);

/* Forget saved credentials. */
esp_err_t wifi_forget(void);

/* Background monitor: ensure fallback AP when disconnected. */
void wifi_monitor_start(void);

#ifdef __cplusplus
}
#endif
