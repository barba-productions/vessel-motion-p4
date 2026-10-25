/*
 * Motion engine for Vessel Motion P4.
 *
 * This is a 1:1 C port of the Raspberry Pi project's motion.py
 * (vessel-motion-pi v0.1.7, itself derived from WT32-ETH01 firmware v1.1.4).
 * All constants, filter coefficients, normalisation ranges, weights and
 * formulas are preserved exactly so the golden acceptance tests hold
 * (e.g. MEI trip score == 1.8279 for the documented synthetic input).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOTION_G0 9.80665f
#define MOTION_WINDOW 200          /* 1 second at 200 Hz */
#define IMPACT_RECENT_MAX 64
#define RSI_HISTOGRAM_BINS 1000
#define HEATMAP_CELLS 400          /* 20 x 20 */

typedef struct {
    float x, y, z;
} vec3_t;

typedef struct {
    float low, high, exponent;
} range_cfg_t;

typedef struct {
    int   version;
    float complementary_alpha;
    float fast_cutoff_hz;
    float slow_cutoff_hz;
    float derivative_cutoff_hz;
    range_cfg_t peak;
    range_cfg_t rms;
    range_cfg_t jerk;
    range_cfg_t roll_rate;
    range_cfg_t pitch_rate;
    range_cfg_t repeat;
    float rsi_weights[5];
    float rsi_attack_seconds;
    float rsi_decay_seconds;
    float impact_start_g;
    float impact_end_g;
    int   impact_end_ms;
    int   impact_deadband_ms;
    range_cfg_t impact_accel;
    range_cfg_t impact_jerk;
    float trip_rsi_weights[4];
    range_cfg_t trip_impact_per_hour;
    float mei_axis_weights[3];
    range_cfg_t mei_rms;
    range_cfg_t mei_shock;
    float mei_duration_tau_hours;
    float mei_current_weights[3];
    float mei_trip_weights[3];
    float idle_accel_rms_g;
    float idle_rate_deg_s;
    float idle_seconds;
} algorithm_cfg_t;

/* Fill cfg with the exact defaults from the Pi AlgorithmConfig. */
void motion_default_config(algorithm_cfg_t *cfg);

/* --- math helpers --- */
float clampf(float value, float low, float high);
float normalizef(float value, float low, float high, float exponent);
float vec3_magnitude(vec3_t v);
vec3_t vec3_sub(vec3_t a, vec3_t b);

/* --- axis mapping --- */
typedef struct {
    int   source[3];
    int   sign[3];
} axis_map_t;

void  axis_map_init(axis_map_t *m);              /* default (0,1,2)/(1,-1,1) */
bool  axis_map_valid(const axis_map_t *m);
vec3_t axis_map_apply(const axis_map_t *m, vec3_t v);
vec3_t axis_map_apply_axial(const axis_map_t *m, vec3_t v);

/* --- orientation (complementary filter) --- */
typedef struct {
    float roll, pitch;
    float display_roll, display_pitch;
    vec3_t gravity;
} orientation_t;

typedef struct {
    float roll, pitch, roll_zero, pitch_zero;
    bool  initialized;
} orientation_filter_t;

void          orientation_filter_init(orientation_filter_t *f);
void          orientation_filter_reset(orientation_filter_t *f, vec3_t accel);
orientation_t orientation_filter_update(orientation_filter_t *f, vec3_t accel,
                                        vec3_t gyro, float dt, float alpha);
void          orientation_filter_zero(orientation_filter_t *f);

/* --- low-pass filter bank --- */
typedef struct {
    vec3_t state;
} lowpass3_t;

void   lowpass3_init(lowpass3_t *f);
vec3_t lowpass3_update(lowpass3_t *f, vec3_t value, float cutoff_hz, float dt);

/* --- per-sample motion analysis (windowed) --- */
typedef struct {
    vec3_t linear, fast, slow, jerk;
    float  fast_resultant, slow_resultant, jerk_resultant;
    float  roll_rate, pitch_rate;
    float  accel_peak, accel_rms, jerk_peak, roll_rate_peak, pitch_rate_peak;
} motion_t;

typedef struct {
    lowpass3_t fast_filter, slow_filter, jerk_filter;
    vec3_t  previous_fast;
    float   accel[MOTION_WINDOW];
    float   jerk[MOTION_WINDOW];
    float   roll[MOTION_WINDOW];
    float   pitch[MOTION_WINDOW];
    int     head;    /* ring-buffer write index */
    int     count;   /* number of valid entries (<= MOTION_WINDOW) */
} motion_analyzer_t;

void     motion_analyzer_init(motion_analyzer_t *a);
motion_t motion_analyzer_update(motion_analyzer_t *a, vec3_t accel, vec3_t gravity,
                                vec3_t gyro, float dt, const algorithm_cfg_t *cfg);

/* --- RSI --- */
typedef struct {
    float peak, rms, jerk, angular, repeat;
    float raw, current;
} severity_t;

typedef struct {
    severity_t state;
} ride_severity_t;

void        ride_severity_init(ride_severity_t *r);
severity_t  ride_severity_update(ride_severity_t *r, const motion_t *motion,
                                 float impacts_per_minute, float dt,
                                 const algorithm_cfg_t *cfg, bool enabled);

/* --- MEI / motion exposure --- */
typedef struct {
    float sum_a2, dose4, seconds;
    float rms, shock, current;
} exposure_t;

typedef struct {
    exposure_t state;
    float      recent_a2, recent_dose4, recent_seconds;
} motion_exposure_t;

void       motion_exposure_init(motion_exposure_t *e);
exposure_t motion_exposure_update(motion_exposure_t *e, vec3_t slow, float dt,
                                  bool active, const algorithm_cfg_t *cfg, bool enabled);
float      motion_exposure_score(float sum_a2, float dose4, float seconds,
                                 const algorithm_cfg_t *cfg);

/* --- impact detection --- */
typedef struct {
    int64_t start_ms, end_ms;
    int     duration_ms;
    vec3_t  peak;
    float   peak_resultant, peak_jerk;
    float   roll, pitch, rsi, severity;
} impact_t;

typedef struct {
    bool     active;
    int64_t  below_since, last_end;
    impact_t current;
    bool     has_current;
    int64_t  recent_ms[IMPACT_RECENT_MAX];
    float    recent_sev[IMPACT_RECENT_MAX];
    int      recent_head, recent_count;
} impact_detector_t;

void   impact_detector_init(impact_detector_t *d);
float  impact_detector_weighted_per_minute(const impact_detector_t *d, int64_t now_ms);
/* Returns true and fills *out when an impact completes this sample. */
bool   impact_detector_update(impact_detector_t *d, const motion_t *motion,
                              const orientation_t *orientation, float rsi,
                              int64_t now_ms, const algorithm_cfg_t *cfg,
                              impact_t *out);

/* --- trip scoring helper (p95 histogram + final rsi_trip) --- */
float trip_rsi_p95(const uint32_t histogram[RSI_HISTOGRAM_BINS], uint32_t updates);
float trip_rsi_score(float rsi_mean, float rsi_p95, float rsi_max,
                     float impacts_per_hour, const algorithm_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
