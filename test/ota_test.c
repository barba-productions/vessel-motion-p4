#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../main/ota.c"

static esp_app_desc_t running_image = {.version = FIRMWARE_VERSION};
const esp_app_desc_t *esp_app_get_description(void) { return &running_image; }

/* Status reads must never create tasks, connect to servers or write flash. */
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) { (void)config; abort(); }
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int length) { (void)c; (void)length; abort(); }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c) { (void)c; abort(); }
int esp_http_client_fetch_headers(esp_http_client_handle_t c) { (void)c; abort(); }
int esp_http_client_get_status_code(esp_http_client_handle_t c) { (void)c; abort(); }
int esp_http_client_read(esp_http_client_handle_t c, char *buffer, int length) { (void)c; (void)buffer; (void)length; abort(); }
esp_err_t esp_http_client_close(esp_http_client_handle_t c) { (void)c; abort(); }
esp_err_t esp_crt_bundle_attach(void *config) { (void)config; abort(); }
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *p) { (void)p; abort(); }
esp_err_t esp_ota_begin(const esp_partition_t *p, size_t size, esp_ota_handle_t *h) { (void)p; (void)size; (void)h; abort(); }
esp_err_t esp_ota_write(esp_ota_handle_t h, const void *data, size_t length) { (void)h; (void)data; (void)length; abort(); }
esp_err_t esp_ota_abort(esp_ota_handle_t h) { (void)h; abort(); }
esp_err_t esp_ota_end(esp_ota_handle_t h) { (void)h; abort(); }
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p) { (void)p; abort(); }
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle)
{ (void)task; (void)name; (void)stack; (void)arg; (void)priority; (void)handle; abort(); }
void vTaskDelete(void *handle) { (void)handle; abort(); }

int main(void)
{
    ota_status_t status;
    memset(&status, 0xFF, sizeof(status));
    ota_snapshot(&status);
    assert(!strcmp(status.current_version, FIRMWARE_VERSION));
    assert(!strcmp(status.state, "idle"));
    assert(!strcmp(status.message, "Ready to check for updates"));
    assert(!status.available_version[0]);
    assert(status.progress == 0 && status.received == 0 && status.total == 0);
    assert(!ota_running());

    const char *states[] = {"checking", "downloading", "failed", "current", "complete"};
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        set_status(states[i], "Test status");
        strcpy(s_status.available_version, "1.0.6");
        s_status.progress = 42;
        s_status.received = 420;
        s_status.total = 1000;
        ota_snapshot(&status);
        assert(!strcmp(status.current_version, FIRMWARE_VERSION));
        assert(!strcmp(status.state, states[i]));
        assert(!strcmp(status.message, "Test status"));
        assert(!strcmp(status.available_version, "1.0.6"));
        assert(status.progress == 42 && status.received == 420 && status.total == 1000);
    }
    strcpy(running_image.version, "test-running-version");
    ota_snapshot(&status);
    assert(!strcmp(status.current_version, "test-running-version"));
    puts("PASS OTA status: installed image version before UPDATE, idle defaults and preserved update state");
    return 0;
}
