/*
 * Vessel Motion P4 — main application.
 *
 * Runs the 200 Hz motion pipeline ported 1:1 from the Raspberry Pi project
 * (motion.py / service.py) and feeds the web dashboard with Pi-identical
 * telemetry frames, overview history and trip logs.
 *
 * MPU-6050 on GPIO32 (SCL) / GPIO33 (SDA).
 */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "motion.h"
#include "sensor.h"
#include "network.h"
#include "webserver.h"
#include "storage.h"
#include "store.h"
#include "wifi_mgr.h"
#include "time_mgr.h"
#include "settings.h"
#include "overview.h"

static const char *TAG = "vessel-motion";

#define SAMPLE_HZ            200
#define TELEMETRY_HZ         50
#define MAX_TRIP_SECONDS     28800   /* 8 h */
#define SEVERITY_DT          0.1f    /* 10 Hz RSI update */
#define OVERVIEW_DT_US       2000000 /* overview sample every 2 s */
#define CALIBRATION_SAMPLES  1000

static algorithm_cfg_t       s_cfg;
static axis_map_t            s_axis_map;
static orientation_filter_t  s_orient;
static motion_analyzer_t     s_analyzer;
static ride_severity_t       s_severity;
static motion_exposure_t     s_exposure;
static impact_detector_t     s_impacts;

static int64_t  s_last_sample_us;
static int64_t  s_last_severity_us;
static int64_t  s_last_overview_us;
static float    s_idle_seconds;
static bool     s_idle;
static uint32_t s_errors, s_missed, s_invalid;
static bool     s_rsi_enabled = true, s_mei_enabled = true;
static uint32_t s_reset_seq;
static int32_t  s_hs[6];                    /* last 5 Hz trip sample (Pi last_history) */
static float    s_sample_hz = SAMPLE_HZ;    /* measured */
static bool     s_prev_impact_active;
static vec3_t   s_cal_accel[CALIBRATION_SAMPLES];  /* recent uncalibrated samples */
static vec3_t   s_cal_gyro[CALIBRATION_SAMPLES];
static int      s_cal_count;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static int16_t b16(float v)
{
    long r = lroundf(v);
    return r < -32768 ? -32768 : (r > 32767 ? 32767 : (int16_t)r);
}

/* parse "a,b,c" CSV into out[count] (Pi parse_float_list). Returns false on bad count. */
static bool parse_float_list(const char *str, float *out, int count)
{
    int n = 0;
    char *copy = strdup(str);
    if (!copy) return false;
    char *save = NULL;
    for (char *t = strtok_r(copy, ",", &save); t && n < count; t = strtok_r(NULL, ",", &save)) {
        out[n++] = strtof(t, NULL);
    }
    free(copy);
    return n == count;
}

/* Push the persisted settings into the motion core + sensor (Pi apply_settings). */
static void apply_settings(void)
{
    /* axis mapping from axisX/axisY/axisZ (signed 1-based), Pi apply_settings() */
    int axis_values[3] = { (int)settings_num("axisX"), (int)settings_num("axisY"),
                           (int)settings_num("axisZ") };
    int absvals[3] = { abs(axis_values[0]), abs(axis_values[1]), abs(axis_values[2]) };
    if (absvals[0] >= 1 && absvals[0] <= 3 && absvals[1] >= 1 && absvals[1] <= 3 &&
        absvals[2] >= 1 && absvals[2] <= 3 &&
        absvals[0] != absvals[1] && absvals[1] != absvals[2] && absvals[0] != absvals[2]) {
        const int mounting_sign[3] = { 1, -1, 1 };
        for (int i = 0; i < 3; i++) {
            int source = absvals[i] - 1;
            s_axis_map.source[i] = source;
            s_axis_map.sign[i] = (axis_values[i] > 0 ? 1 : -1) * mounting_sign[source];
        }
        sensor_set_axis_map(&s_axis_map);
    }

    s_rsi_enabled = settings_bool("rsi");
    s_mei_enabled = settings_bool("mei");

    s_cfg.complementary_alpha = (float)settings_num("alpha");
    s_cfg.fast_cutoff_hz      = (float)settings_num("fast");
    s_cfg.slow_cutoff_hz      = (float)settings_num("slow");
    s_cfg.derivative_cutoff_hz = (float)settings_num("derivative");
    s_cfg.impact_start_g      = (float)settings_num("impactStart");
    s_cfg.impact_end_g        = (float)settings_num("impactEnd");
    s_cfg.impact_end_ms       = (int)settings_num("impactEndMs");
    s_cfg.impact_deadband_ms  = (int)settings_num("impactDeadbandMs");

    float exponent = (float)settings_num("exponent");
    struct { range_cfg_t *range; const char *low, *high; } ranges[] = {
        { &s_cfg.peak, "peakLow", "peakHigh" },
        { &s_cfg.rms, "rmsLow", "rmsHigh" },
        { &s_cfg.jerk, "jerkLow", "jerkHigh" },
        { &s_cfg.roll_rate, "rollLow", "rollHigh" },
        { &s_cfg.pitch_rate, "pitchLow", "pitchHigh" },
        { &s_cfg.repeat, "repeatLow", "repeatHigh" },
        { &s_cfg.impact_accel, "impactLow", "impactHigh" },
    };
    for (size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++) {
        ranges[i].range->low = (float)settings_num(ranges[i].low);
        ranges[i].range->high = (float)settings_num(ranges[i].high);
        ranges[i].range->exponent = exponent;
    }
    s_cfg.impact_jerk.exponent = exponent;
    s_cfg.mei_rms.exponent = exponent;
    s_cfg.mei_shock.exponent = exponent;

    parse_float_list(settings_str("rsiWeights"), s_cfg.rsi_weights, 5);
    s_cfg.rsi_attack_seconds = (float)settings_num("attack");
    s_cfg.rsi_decay_seconds  = (float)settings_num("decay");
    parse_float_list(settings_str("meiAxis"), s_cfg.mei_axis_weights, 3);
    s_cfg.mei_rms.low   = (float)settings_num("meiRmsLow");
    s_cfg.mei_rms.high  = (float)settings_num("meiRmsHigh");
    s_cfg.mei_shock.low  = (float)settings_num("meiShockLow");
    s_cfg.mei_shock.high = (float)settings_num("meiShockHigh");
    s_cfg.mei_duration_tau_hours = (float)settings_num("meiTau");
    parse_float_list(settings_str("meiCurrentWeights"), s_cfg.mei_current_weights, 3);
    parse_float_list(settings_str("meiTripWeights"), s_cfg.mei_trip_weights, 3);

    s_orient.roll_zero  = (float)settings_num("rollZero");
    s_orient.pitch_zero = (float)settings_num("pitchZero");

    float off[3];
    if (settings_arr3("accelOffset", off)) {
        vec3_t a = { off[0], off[1], off[2] };
        if (settings_arr3("gyroOffset", off)) {
            vec3_t g = { off[0], off[1], off[2] };
            sensor_set_offsets(a, g);
        }
    }
}

/* Live trip scores from the store accumulators (Pi TripRuntime math). */
static void live_trip_scores(float *rsi_trip, float *mei_trip)
{
    trip_live_t live;
    store_trip_live(&live);
    float iph = live.exposure_seconds ? live.weighted_impacts * 3600.0f / live.exposure_seconds : 0.0f;
    float impact_score = normalizef(iph, s_cfg.trip_impact_per_hour.low,
                                    s_cfg.trip_impact_per_hour.high,
                                    s_cfg.trip_impact_per_hour.exponent);
    float rv[4] = { live.rsi_mean, live.rsi_p95, live.rsi_max, 10.0f * impact_score };
    float acc = 0.0f;
    for (int i = 0; i < 4; i++) acc += s_cfg.trip_rsi_weights[i] * rv[i];
    *rsi_trip = clampf(acc, 0.0f, 10.0f);
    *mei_trip = motion_exposure_score(live.sum_a2, live.dose4, live.exposure_seconds, &s_cfg);
}

/* "calibrate" command: average the recent 1000 uncalibrated samples (Pi calibrate()). */
static esp_err_t do_calibrate(void)
{
    if (s_cal_count < CALIBRATION_SAMPLES) {
        ESP_LOGW(TAG, "calibrate: only %d/%d samples", s_cal_count, CALIBRATION_SAMPLES);
        return ESP_ERR_INVALID_STATE;
    }
    vec3_t accel = { 0, 0, 0 }, gyro = { 0, 0, 0 };
    for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
        accel.x += s_cal_accel[i].x; accel.y += s_cal_accel[i].y; accel.z += s_cal_accel[i].z;
        gyro.x += s_cal_gyro[i].x; gyro.y += s_cal_gyro[i].y; gyro.z += s_cal_gyro[i].z;
    }
    accel.x /= CALIBRATION_SAMPLES; accel.y /= CALIBRATION_SAMPLES; accel.z /= CALIBRATION_SAMPLES;
    gyro.x /= CALIBRATION_SAMPLES; gyro.y /= CALIBRATION_SAMPLES; gyro.z /= CALIBRATION_SAMPLES;

    float expected[3] = { 0, 0, 0 };
    expected[s_axis_map.source[2]] = s_axis_map.sign[2] * MOTION_G0;
    char body[160];
    snprintf(body, sizeof(body),
             "{\"accelOffset\":[%.6f,%.6f,%.6f],\"gyroOffset\":[%.6f,%.6f,%.6f]}",
             (double)(accel.x - expected[0]), (double)(accel.y - expected[1]),
             (double)(accel.z - expected[2]),
             (double)gyro.x, (double)gyro.y, (double)gyro.z);
    char err[96];
    esp_err_t rc = settings_patch_json(body, err, sizeof(err));
    if (rc != ESP_OK) return rc;
    sensor_set_offsets((vec3_t){ accel.x - expected[0], accel.y - expected[1], accel.z - expected[2] },
                       gyro);
    ESP_LOGI(TAG, "calibrated: accel offset %.3f %.3f %.3f",
             (double)(accel.x - expected[0]), (double)(accel.y - expected[1]),
             (double)(accel.z - expected[2]));
    return ESP_OK;
}

/* Commands from the web layer (WS text or /api/command). Mirrors Pi command(). */
esp_err_t vessel_command(const char *cmd)
{
    if (!cmd) return ESP_ERR_INVALID_ARG;
    if (!strcasecmp(cmd, "start")) {
        store_start_trip(s_cfg.version);
        if (settings_bool("resetOnTripStart")) {
            /* clear live charts/heatmap so the overview shows only trip data */
            s_reset_seq++;
            overview_reset();
            store_heatmap_reset();
        }
        return ESP_OK;
    }
    if (!strcasecmp(cmd, "end")) {
        store_end_trip(&s_cfg);
        return ESP_OK;
    }
    if (!strcasecmp(cmd, "zero")) {
        orientation_filter_zero(&s_orient);
        char body[96];
        snprintf(body, sizeof(body), "{\"rollZero\":%.6f,\"pitchZero\":%.6f}",
                 (double)s_orient.roll_zero, (double)s_orient.pitch_zero);
        char err[96];
        settings_patch_json(body, err, sizeof(err));
        return ESP_OK;
    }
    if (!strcasecmp(cmd, "reset")) {
        s_reset_seq++;
        overview_reset();
        store_heatmap_reset();
        return ESP_OK;
    }
    if (!strcasecmp(cmd, "calibrate")) {
        return do_calibrate();
    }
    if (!strcasecmp(cmd, "defaults")) {
        settings_restore_algorithm_defaults();
        apply_settings();
        return ESP_OK;
    }
    if (!strcasecmp(cmd, "__apply")) {
        apply_settings();   /* internal: settings changed via the web API */
        return ESP_OK;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

static void sensor_task(void *arg)
{
    const char *model = NULL;
    while (sensor_init(&s_axis_map, (vec3_t){0,0,0}, (vec3_t){0,0,0}, &model) != ESP_OK) {
        ESP_LOGW(TAG, "MPU-6050 not found, retrying...");
        s_errors++;
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    apply_settings();   /* push persisted offsets/axis map into the live sensor */
    ESP_LOGI(TAG, "Motion core online (%s), %d Hz sampling", model, SAMPLE_HZ);

    const int64_t period_us = 1000000 / SAMPLE_HZ;
    int64_t next_sample = esp_timer_get_time();
    int64_t last_telemetry = 0;
    int64_t last_trip_sample = 0;
    severity_t rsi = { 0 };
    exposure_t mei = { 0 };

    for (;;) {
        sensor_sample_t sample;
        esp_err_t err = sensor_read(&sample);
        if (err != ESP_OK) {
            s_errors++;
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }

        /* dt, mirroring service.py (_sample) */
        int64_t t_us = sample.timestamp_us;
        float dt = s_last_sample_us ? (t_us - s_last_sample_us) / 1e6f : 0.005f;
        s_last_sample_us = t_us;
        if (!(dt > 0.0f && dt <= 0.05f)) { s_missed++; dt = 0.005f; }
        else { s_sample_hz = s_sample_hz * 0.99f + (1.0f / dt) * 0.01f; }

        orientation_t orientation = orientation_filter_update(&s_orient, sample.accel_vessel,
                                                              sample.gyro, dt, s_cfg.complementary_alpha);
        motion_t motion = motion_analyzer_update(&s_analyzer, sample.accel_vessel,
                                                 orientation.gravity, sample.gyro, dt, &s_cfg);

        /* idle detection */
        float rates = fmaxf(fabsf(motion.roll_rate), fabsf(motion.pitch_rate)) * (180.0f / (float)M_PI);
        if (motion.accel_rms / MOTION_G0 < s_cfg.idle_accel_rms_g && rates < s_cfg.idle_rate_deg_s) {
            s_idle_seconds += dt;
            if (s_idle_seconds >= s_cfg.idle_seconds) s_idle = true;
        } else {
            s_idle_seconds = 0.0f;
            s_idle = false;
        }

        int64_t ms = now_ms();

        /* RSI at 10 Hz */
        float severity_dt = s_last_severity_us ? (t_us - s_last_severity_us) / 1e6f : SEVERITY_DT;
        if (severity_dt >= SEVERITY_DT) {
            s_last_severity_us = t_us;
            rsi = ride_severity_update(&s_severity, &motion,
                                       impact_detector_weighted_per_minute(&s_impacts, ms),
                                       severity_dt, &s_cfg, s_rsi_enabled);
            /* live dashboard heatmap runs with or without an active trip */
            store_heatmap_accumulate(severity_dt, rsi.current, mei.current);
        } else {
            rsi = s_severity.state;
        }

        /* MEI every sample (active when a trip runs or not idle) */
        mei = motion_exposure_update(&s_exposure, motion.slow, dt,
                                     store_trip_active() || !s_idle, &s_cfg, s_mei_enabled);

        /* Impact detection */
        impact_t impact;
        if (impact_detector_update(&s_impacts, &motion, &orientation, rsi.current, ms, &s_cfg, &impact)) {
            ESP_LOGI(TAG, "IMPACT peak=%.2fg severity=%.2f dur=%dms",
                     impact.peak_resultant / MOTION_G0, impact.severity, impact.duration_ms);
            int64_t mono_elapsed = ms - store_active_trip_start_mono_ms();
            impact_rec_t rec = {
                .id = store_active_trip_id(),
                .epoch_ms = time_epoch_ms(),
                .elapsed_ms = (uint32_t)(mono_elapsed > 0 ? mono_elapsed : 0),
                .duration_ms = (uint32_t)impact.duration_ms,
                .peak_x_g = impact.peak.x / MOTION_G0, .peak_y_g = impact.peak.y / MOTION_G0,
                .peak_z_g = impact.peak.z / MOTION_G0,
                .peak_resultant_g = impact.peak_resultant / MOTION_G0,
                .peak_jerk_g = impact.peak_jerk / MOTION_G0,
                .roll_deg = impact.roll * (180.0f / (float)M_PI),
                .pitch_deg = impact.pitch * (180.0f / (float)M_PI),
                .rsi = impact.rsi, .severity = impact.severity,
            };
            store_add_impact(&rec);
            if (store_trip_active()) store_trip_add_impact_severity(impact.severity);
        }
        if (s_impacts.active && !s_prev_impact_active) overview_note_impact((uint32_t)ms);
        s_prev_impact_active = s_impacts.active;

        /* Trip accumulation + 5 Hz sampling (monotonic clock for elapsed) */
        if (store_trip_active()) {
            float slow_eff = sqrtf(
                powf(s_cfg.mei_axis_weights[0] * motion.slow.x, 2.0f) +
                powf(s_cfg.mei_axis_weights[1] * motion.slow.y, 2.0f) +
                powf(s_cfg.mei_axis_weights[2] * motion.slow.z, 2.0f));
            store_trip_accumulate(dt, rsi.current, mei.current,
                                  orientation.display_roll * (180.0f / (float)M_PI),
                                  orientation.display_pitch * (180.0f / (float)M_PI),
                                  motion.fast.x / MOTION_G0, motion.fast.y / MOTION_G0,
                                  motion.fast.z / MOTION_G0,
                                  motion.fast_resultant / MOTION_G0,
                                  motion.jerk_resultant / MOTION_G0,
                                  slow_eff);
            int64_t trip_elapsed = ms - store_active_trip_start_mono_ms();
            if (t_us - last_trip_sample >= 1000000 / 5) {
                last_trip_sample = t_us;
                uint32_t elapsed = (uint32_t)(trip_elapsed > 0 ? trip_elapsed : 0);
                float roll_deg = orientation.display_roll * (180.0f / (float)M_PI);
                float pitch_deg = orientation.display_pitch * (180.0f / (float)M_PI);
                float ax = motion.fast.x / MOTION_G0, ay = motion.fast.y / MOTION_G0,
                      az = motion.fast.z / MOTION_G0;
                store_add_sample(elapsed, roll_deg, pitch_deg, ax, ay, az,
                                 motion.fast_resultant / MOTION_G0, rsi.current, mei.current);
                s_hs[0] = (int32_t)elapsed;
                s_hs[1] = b16(roll_deg * 100.0f);
                s_hs[2] = b16(pitch_deg * 100.0f);
                s_hs[3] = b16(ax * 1000.0f);
                s_hs[4] = b16(ay * 1000.0f);
                s_hs[5] = b16(az * 1000.0f);
            }
            if (trip_elapsed >= (int64_t)MAX_TRIP_SECONDS * 1000) store_end_trip(&s_cfg);
        }

        /* Overview history every 2 s */
        if (t_us - s_last_overview_us >= OVERVIEW_DT_US) {
            s_last_overview_us = t_us;
            overview_record((uint32_t)ms,
                            b16(orientation.display_roll * (180.0f / (float)M_PI) * 100.0f),
                            b16(orientation.display_pitch * (180.0f / (float)M_PI) * 100.0f),
                            b16(motion.fast.x / MOTION_G0 * 1000.0f),
                            b16(motion.fast.y / MOTION_G0 * 1000.0f),
                            b16(motion.fast.z / MOTION_G0 * 1000.0f),
                            b16(motion.fast_resultant / MOTION_G0 * 1000.0f));
        }

        /* recent calibration window */
        if (s_cal_count < CALIBRATION_SAMPLES) {
            s_cal_accel[s_cal_count] = sample.accel_uncalibrated;
            s_cal_gyro[s_cal_count] = sample.gyro_uncalibrated;
            s_cal_count++;
        } else {
            memmove(s_cal_accel, s_cal_accel + 1, sizeof(s_cal_accel) - sizeof(s_cal_accel[0]));
            memmove(s_cal_gyro, s_cal_gyro + 1, sizeof(s_cal_gyro) - sizeof(s_cal_gyro[0]));
            s_cal_accel[CALIBRATION_SAMPLES - 1] = sample.accel_uncalibrated;
            s_cal_gyro[CALIBRATION_SAMPLES - 1] = sample.gyro_uncalibrated;
        }

        /* Telemetry at 50 Hz */
        if (t_us - last_telemetry >= 1000000 / TELEMETRY_HZ) {
            last_telemetry = t_us;
            bool trip = store_trip_active();
            float rsi_trip = 0.0f, mei_trip = 0.0f;
            live_trip_scores(&rsi_trip, &mei_trip);
            int64_t mono_elapsed = ms - store_active_trip_start_mono_ms();
            telemetry_frame_t frame = {
                .t_ms = ms,
                .roll_deg = orientation.display_roll * (180.0f / (float)M_PI),
                .pitch_deg = orientation.display_pitch * (180.0f / (float)M_PI),
                .raw = { sample.accel.x, sample.accel.y, sample.accel.z },
                .mapped = { sample.accel_vessel.x, sample.accel_vessel.y, sample.accel_vessel.z },
                .gyro = { sample.gyro.x, sample.gyro.y, sample.gyro.z },
                .gravity = { orientation.gravity.x, orientation.gravity.y, orientation.gravity.z },
                .a = { motion.fast.x / MOTION_G0, motion.fast.y / MOTION_G0,
                       motion.fast.z / MOTION_G0 },
                .af = motion.fast_resultant / MOTION_G0,
                .as = motion.slow_resultant / MOTION_G0,
                .peak_g = motion.accel_peak / MOTION_G0,
                .rms_g = motion.accel_rms / MOTION_G0,
                .jerk_gps = motion.jerk_resultant / MOTION_G0,
                .rates_dps = { motion.roll_rate * (180.0f / (float)M_PI),
                               motion.pitch_rate * (180.0f / (float)M_PI) },
                .rsi_components = { rsi.peak, rsi.rms, rsi.jerk, rsi.angular, rsi.repeat },
                .rsi_raw = rsi.raw,
                .rsi_current = rsi.current,
                .rsi_trip = rsi_trip,
                .mei_rms = mei.rms,
                .mei_shock = mei.shock,
                .mei_dose = mei.dose4,
                .mei_current = mei.current,
                .mei_trip = mei_trip,
                .impact = s_impacts.active,
                .trip = trip,
                .trip_seconds = trip ? (uint32_t)(mono_elapsed > 0 ? mono_elapsed / 1000 : 0) : 0,
                .trip_start_ms = trip ? store_active_trip_start_ms() : 0,
                .hs = { s_hs[0], s_hs[1], s_hs[2], s_hs[3], s_hs[4], s_hs[5] },
                .reset_seq = s_reset_seq,
                .sensor_ok = true,
                .sample_hz = s_sample_hz,
                .errors = s_errors,
                .missed = s_missed,
                .invalid = s_invalid,
            };
            webserver_push_telemetry(&frame);
        }

        /* 200 Hz pacing */
        next_sample += period_us;
        int64_t delay = next_sample - esp_timer_get_time();
        if (delay > 0) {
            vTaskDelay(pdMS_TO_TICKS(delay / 1000 > 0 ? delay / 1000 : 1));
        } else {
            s_missed++;
            next_sample = esp_timer_get_time();
            taskYIELD();
        }
    }
}

void app_main(void)
{
    motion_default_config(&s_cfg);
    axis_map_init(&s_axis_map);
    orientation_filter_init(&s_orient);
    motion_analyzer_init(&s_analyzer);
    ride_severity_init(&s_severity);
    motion_exposure_init(&s_exposure);
    impact_detector_init(&s_impacts);

    ESP_LOGI(TAG, "Vessel Motion P4 Phase 3 - wifi + time + ota");

    /* NVS + settings + time first: network_init() reads the dhcp/ip/hostname
     * settings, so they must be loaded before any network bring-up. */
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    settings_init();                /* NVS-backed settings (Pi DEFAULT_SETTINGS) */
    time_init();

    /* Order matters on this board: the ESP32-C6 coprocessor (esp_hosted) and the
     * SD card share the SDMMC host hardware. Bring up Wi-Fi/C6 FIRST so esp_hosted
     * claims host slot 0 and resets the C6, then mount the SD card on slot 1. */
    bool net_ok = (network_init() == ESP_OK);
    if (net_ok) {
        esp_err_t wifi_err = vm_wifi_init();   /* brings up esp_hosted SDIO + C6 */
        if (wifi_err == ESP_OK) {
            wifi_monitor_start();
        } else {
            ESP_LOGW(TAG, "Wi-Fi init deferred/failed (%s); Ethernet continues",
                     esp_err_to_name(wifi_err));
        }
        webserver_start();
        time_start_sntp();
    } else {
        ESP_LOGW(TAG, "Network init failed; running headless (UART telemetry only)");
    }

    vTaskDelay(pdMS_TO_TICKS(1000));  /* let the C6 SDIO fully settle before SD mount */
    storage_mount_web();            /* dashboard assets: internal flash (reliable) */
    storage_mount_sd();             /* trip/impact logs: SD card */
    store_init();
    apply_settings();               /* configure the motion core from settings */

    xTaskCreatePinnedToCore(sensor_task, "motion-200hz", 8192, NULL, 10, NULL, 1);
}
