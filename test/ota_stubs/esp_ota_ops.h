#pragma once
#include <stddef.h>
#include "esp_err.h"
typedef struct { int unused; } esp_partition_t;
typedef unsigned esp_ota_handle_t;
#define OTA_WITH_SEQUENTIAL_WRITES 0
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *start);
esp_err_t esp_ota_begin(const esp_partition_t *partition, size_t size, esp_ota_handle_t *handle);
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t length);
esp_err_t esp_ota_abort(esp_ota_handle_t handle);
esp_err_t esp_ota_end(esp_ota_handle_t handle);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition);
