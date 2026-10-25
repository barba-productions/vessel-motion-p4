/*
 * Time & RTC for Vessel Motion P4 (Phase 3).
 * SNTP sync (when network available), IANA timezone, manual set.
 * The board's rechargeable RTC battery keeps the internal RTC across power loss.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise time subsystem (loads timezone from NVS, defers SNTP). */
esp_err_t time_init(void);

/* Start SNTP. Call only after the network/lwIP stack is up. */
void time_start_sntp(void);

/* Force an immediate NTP re-query (internet sync button). */
void time_sync_now(void);

/* Number of successful SNTP syncs since boot (0 = never synced). */
uint32_t time_sync_count(void);

/* Current epoch in milliseconds (UTC). */
int64_t time_epoch_ms(void);

/* ISO-8601 local time in the configured timezone, e.g. "2026-10-19T14:30:00". */
void time_local_iso(char *buf, size_t len);

/* Current IANA timezone string. */
const char *time_timezone(void);

/* Set IANA timezone (validated against the TZ database); persists to NVS. */
esp_err_t time_set_timezone(const char *tz);

/* Manually set time from an ISO-8601 local value (used when NTP unavailable). */
esp_err_t time_set_local_iso(const char *iso);

/* Set time from a Unix epoch in milliseconds (browser push, Pi POST /api/time). */
esp_err_t time_set_epoch_ms(int64_t epoch_ms);

/* True once SNTP has delivered a time. */
bool time_is_synchronized(void);

/* True if the RTC battery path is present (always true on this board). */
bool time_rtc_present(void);

#ifdef __cplusplus
}
#endif
