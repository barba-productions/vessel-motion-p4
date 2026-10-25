/*
 * HTTP + WebSocket telemetry server for Vessel Motion P4 (Phase 2).
 * Replicates the Pi project's /ws (50 Hz JSON) and /api/telemetry endpoints.
 * Static dashboard assets are served from SD card (added in the storage step).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One telemetry frame — 1:1 with the Pi telemetry() JSON fields. */
typedef struct {
    int64_t t_ms;
    float   roll_deg, pitch_deg;
    float   raw[3];            /* calibrated accel, sensor frame (m/s^2) */
    float   mapped[3];         /* accel, vessel frame (m/s^2) */
    float   gyro[3];           /* rad/s */
    float   gravity[3];        /* m/s^2 */
    float   a[3];              /* fast linear accel, g */
    float   af, as;            /* fast / slow resultant, g */
    float   peak_g, rms_g;     /* 1 s window, g */
    float   jerk_gps;          /* jerk resultant, g/s */
    float   rates_dps[2];      /* roll/pitch rate, deg/s */
    float   rsi_components[5]; /* peak, rms, jerk, angular, repeat */
    float   rsi_raw, rsi_current, rsi_trip;
    float   mei_rms, mei_shock, mei_dose, mei_current, mei_trip;
    bool    impact, trip;
    uint32_t trip_seconds;
    int64_t trip_start_ms;
    int32_t hs[6];             /* last 5 Hz trip sample record */
    uint32_t reset_seq;
    bool    sensor_ok;
    float   sample_hz;
    uint32_t errors, missed, invalid;
} telemetry_frame_t;

/* Start the HTTP server (call after network_init). */
esp_err_t webserver_start(void);

/* Called by the motion task at 50 Hz; broadcasts the frame to /ws clients. */
void webserver_push_telemetry(const telemetry_frame_t *frame);

/* Command bridge implemented in main.c (start/end/zero/reset). */
esp_err_t vessel_command(const char *cmd);

#ifdef __cplusplus
}
#endif
