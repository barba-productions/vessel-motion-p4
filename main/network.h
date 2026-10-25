/*
 * Network bring-up for the Waveshare ESP32-P4-NANO (Phase 2).
 * Ethernet via the onboard IP101GRI PHY (RMII), primary connection.
 * Wi-Fi (ESP32-C6 coprocessor) is added in Phase 3.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise Ethernet (IP101GRI over RMII) + esp_netif, register IP event
 * handlers. Returns ESP_OK once the driver is started (link/IP come up
 * asynchronously). */
esp_err_t network_init(void);

/* True once an IP address has been assigned. */
bool network_is_connected(void);

/* Copy the current IPv4 address (e.g. "192.168.1.90") into buf. */
esp_err_t network_get_ip(char *buf, size_t len);

#ifdef __cplusplus
}
#endif
