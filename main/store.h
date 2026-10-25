/*
 * Trip & impact persistent store for Vessel Motion P4.
 * Settings persist to NVS flash; trip samples, impacts and heatmaps are
 * written as binary logs on the SD card using the exact same on-the-wire
 * formats as the Pi project (storage.py):
 *   trip sample  = 16 bytes  "<Ihhhhhh"  (elapsed_ms u32; roll*100, pitch*100,
 *                                        ax*1000, ay*1000, az*1000, res*1000 i16)
 *   100 trips retained, 8 h max duration, samples at 5 Hz.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "motion.h"   /* algorithm_cfg_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t id;
    int64_t  start_ms;
    char     label[40];
    uint32_t duration_seconds;
    uint32_t sample_count;
    float    max_roll, max_pitch;
    float    max_axis_g[3];
    float    max_resultant_g, max_jerk_g;
    float    rsi_mean, rsi_max, rsi_p95, rsi_trip;
    float    mei_trip;
    float    heatmap[400];
    uint32_t impact_counts[3];
    int      algorithm_version;
    bool     active;
} trip_meta_t;

/* Live accumulators of the active trip (Pi TripRuntime equivalent). */
typedef struct {
    float    rsi_mean, rsi_max, rsi_p95;
    float    weighted_impacts;
    float    sum_a2, dose4, exposure_seconds;
    uint32_t rsi_updates;
} trip_live_t;

typedef struct {
    uint32_t id;
    int64_t  epoch_ms;
    uint32_t elapsed_ms;
    uint32_t duration_ms;
    float    peak_x_g, peak_y_g, peak_z_g;
    float    peak_resultant_g, peak_jerk_g;
    float    roll_deg, pitch_deg, rsi, severity;
} impact_rec_t;

/* Initialise: mount NVS, open/create the store on SD. */
esp_err_t store_init(void);

/* --- trip lifecycle --- */
uint32_t  store_start_trip(int algorithm_version);
void      store_end_trip(const algorithm_cfg_t *cfg);
bool      store_trip_active(void);
uint32_t  store_active_trip_id(void);
int64_t   store_active_trip_start_ms(void);        /* epoch ms (for labels/JSON) */
int64_t   store_active_trip_start_mono_ms(void);   /* monotonic ms (for elapsed) */
uint32_t  store_active_trip_samples(void);
void      store_active_trip_label(char *buf, size_t len);

/* Live accumulators of the active trip (zeros when no trip). */
void      store_trip_live(trip_live_t *out);
/* Read one trip's metadata by id (any state). Returns false if absent. */
bool      store_trip_meta(uint32_t trip_id, trip_meta_t *out);

/* --- data ingest (called from motion loop) --- */
void store_add_sample(uint32_t elapsed_ms, float roll_deg, float pitch_deg,
                      float ax_g, float ay_g, float az_g, float resultant_g,
                      float rsi, float mei);
void store_add_impact(const impact_rec_t *impact);

/* --- queries --- */
/* List completed trips, newest first. Returns count written into out[]. */
int  store_list_trips(trip_meta_t *out, int max);
/* Read a trip's 16-byte binary sample records into buf. Returns byte count. */
size_t store_trip_samples_binary(uint32_t trip_id, uint8_t *buf, size_t cap);
/* Heatmap (400 floats) for a trip. */
esp_err_t store_trip_heatmap(uint32_t trip_id, float *out400);
/* Live heatmap of the continuous (always-on) accumulator shown on the dashboard. */
void store_live_heatmap(float *out400);
/* Feed the live always-on dashboard heatmap (called at RSI rate, trip or not). */
void store_heatmap_accumulate(float dt, float rsi, float mei);
/* Impact markers for a trip. Returns count. */
int  store_trip_impacts(uint32_t trip_id, impact_rec_t *out, int max);

/* Delete all history (fails if a trip is active). */
bool store_clear_history(void);

/* --- CSV exports --- */
esp_err_t store_trips_csv(char *buf, size_t cap, size_t *out_len);
esp_err_t store_impacts_csv(char *buf, size_t cap, size_t *out_len);

/* --- rolling trip accumulators (fed each sample by service) --- */
void store_trip_accumulate(float dt, float rsi, float mei,
                           float roll_deg, float pitch_deg,
                           float ax_g, float ay_g, float az_g,
                           float resultant_g, float jerk_g,
                           float slow_eff);
void store_trip_add_impact_severity(float severity);

/* Clear the live heatmap (Pi "reset" command). */
void store_heatmap_reset(void);

#ifdef __cplusplus
}
#endif
