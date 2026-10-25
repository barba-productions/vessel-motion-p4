/* SD card mount for the Waveshare ESP32-P4-NANO TF slot (SDIO 3.0, 4-wire). */
#include "storage.h"
#include <string.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "esp_littlefs.h"

static const char *TAG = "storage";

/* Waveshare ESP32-P4-NANO SDMMC pin map (4-wire) */
#define SD_CLK   43
#define SD_CMD   44
#define SD_D0    39
#define SD_D1    40
#define SD_D2    41
#define SD_D3    42

static bool s_mounted;
static bool s_flash_mounted;

/* Mount the LittleFS 'web' partition (internal flash) holding dashboard assets.
 * Reliable — no SDMMC/DMA contention with the C6 coprocessor. */
esp_err_t storage_mount_web(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/flash",
        .partition_label = "web",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LittleFS mount failed (%s)", esp_err_to_name(err));
        return err;
    }
    size_t total = 0, used = 0;
    esp_littlefs_info("web", &total, &used);
    s_flash_mounted = true;
    ESP_LOGI(TAG, "LittleFS 'web' mounted at /flash: %u KB total, %u KB used",
             (unsigned)(total / 1024), (unsigned)(used / 1024));
    return ESP_OK;
}

bool storage_web_ready(void)
{
    return s_flash_mounted;
}

esp_err_t storage_mount_sd(void)
{
    /* SD card uses SDMMC host slot 0; the ESP32-C6 coprocessor (esp_hosted)
     * uses slot 1 (CONFIG_ESP_HOSTED_SDIO_SLOT_1). They must not collide.
     * Use the default 20 MHz clock — 40 MHz produced CRC/timeout write errors
     * on this board (Waveshare's own SD demo uses the default speed). */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;

    /* The P4-NANO's TF slot VDD is powered from the chip's internal LDO
     * (channel 4, per the Waveshare SD demo). Without it, reads work but
     * writes starve and fail with timeout/CRC errors. */
    sd_pwr_ctrl_ldo_config_t ldo_cfg = { .ldo_chan_id = 4 };
    sd_pwr_ctrl_handle_t pwr_ctrl = NULL;
    if (sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &pwr_ctrl) == ESP_OK) {
        host.pwr_ctrl_handle = pwr_ctrl;
    } else {
        ESP_LOGW(TAG, "SD LDO power control unavailable");
    }

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = SD_CLK;
    slot.cmd = SD_CMD;
    slot.d0 = SD_D0;
    slot.d1 = SD_D1;
    slot.d2 = SD_D2;
    slot.d3 = SD_D3;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_card_t *card;
    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mount_cfg, &card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed (%s) - dashboard/logs will be limited",
                 esp_err_to_name(err));
        s_mounted = false;
        return err;
    }
    s_mounted = true;
    ESP_LOGI(TAG, "SD mounted: %s, %.1f GB", card->cid.name,
             (double)card->csd.capacity / (1024.0 * 1024.0));
    return ESP_OK;
}

bool storage_sd_ready(void)
{
    return s_mounted;
}

esp_err_t storage_sd_info(uint64_t *total_bytes, uint64_t *free_bytes)
{
    if (!s_mounted) return ESP_ERR_INVALID_STATE;
    return esp_vfs_fat_info(SD_MOUNT_POINT, total_bytes, free_bytes);
}
