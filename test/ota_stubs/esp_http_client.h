#pragma once
#include "esp_err.h"
#define ESP_ERR_INVALID_RESPONSE 0x108
typedef void *esp_http_client_handle_t;
typedef struct {
    const char *url;
    int timeout_ms;
    esp_err_t (*crt_bundle_attach)(void *);
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int length);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);
int esp_http_client_fetch_headers(esp_http_client_handle_t client);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
int esp_http_client_read(esp_http_client_handle_t client, char *buffer, int length);
esp_err_t esp_http_client_close(esp_http_client_handle_t client);
