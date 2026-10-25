/*
 * Rolling overview history for Vessel Motion P4.
 * Mirrors the Pi project's service.py overview buffer: one sample every 2 s,
 * age-based decimation (2 s within 10 min, 6 s within 1 h, 30 s within 6 h,
 * 60 s beyond), 12 h retention, 1900-sample cap, plus a 256-entry impact
 * marker ring. Serialized to the exact VMOVR2 wire format for
 * GET /api/overview-samples.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Record one overview sample (call at most every 2 s; 'ms' is monotonic ms,
 * the six values are the bounded int16 wire values). */
void overview_record(uint32_t ms, int16_t roll100, int16_t pitch100,
                     int16_t ax1000, int16_t ay1000, int16_t az1000, int16_t res1000);

/* Note the rising edge of an impact (monotonic ms). */
void overview_note_impact(uint32_t ms);

/* Clear all history (Pi "reset" command). */
void overview_reset(void);

/* Serialize the VMOVR2 binary snapshot into out. Returns byte count. */
size_t overview_write_binary(uint8_t *out, size_t cap);

/* Maximum possible snapshot size (24 header + 1900*16 + 256*4). */
#define OVERVIEW_MAX_BYTES (24 + 1900 * 16 + 256 * 4)

#ifdef __cplusplus
}
#endif
