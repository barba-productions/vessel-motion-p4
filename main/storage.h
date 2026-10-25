/*
 * SD card storage for Vessel Motion P4.
 * SDMMC 4-wire on the Waveshare ESP32-P4-NANO TF slot.
 * Holds the web dashboard assets and the trip/impact binary logs.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mount point for the SD card filesystem. */
#define SD_MOUNT_POINT "/sdcard"

/* Mount the SD card (SDMMC 4-wire). Returns ESP_OK on success.
 * Safe to call once at startup; failure is non-fatal (dashboard/logs
 * degrade gracefully). */
esp_err_t storage_mount_sd(void);

/* Mount the LittleFS 'web' partition (internal flash) holding dashboard assets. */
esp_err_t storage_mount_web(void);
bool storage_web_ready(void);

/* LittleFS mount point for dashboard assets. */
#define WEB_FS_ROOT "/flash"

/* True if the SD card is mounted and writable. */
bool storage_sd_ready(void);

/* SD card capacity for logging data (total/free bytes). ESP_ERR_INVALID_STATE
 * when the SD card is not mounted. */
esp_err_t storage_sd_info(uint64_t *total_bytes, uint64_t *free_bytes);

#ifdef __cplusplus
}
#endif
