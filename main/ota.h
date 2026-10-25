/*
 * OTA update for Vessel Motion P4 (Phase 3).
 * ESP-IDF dual-partition OTA with manifest + SHA-256 validation, replicating
 * the Pi project's user-initiated update semantics (updater.py).
 * Product: vessel-motion-p4, architecture: esp32p4.
 * Manifest host: github.com/barba-productions/vessel-motion-p4-OTA (public).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char state[16];           /* idle/checking/downloading/complete/failed/current */
    char current_version[24];
    char available_version[24];
    char message[128];
    uint32_t received, total;
    int progress;             /* 0-100 */
} ota_status_t;

/* Start an update check+download in the background (user-initiated). */
esp_err_t ota_start(const char *manifest_url);

/* Snapshot the current update status. */
void ota_snapshot(ota_status_t *out);

/* True while an update is in progress. */
bool ota_running(void);

#ifdef __cplusplus
}
#endif
