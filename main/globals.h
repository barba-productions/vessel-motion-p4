#pragma once

/* Defaults seed new/reset devices; saved settings take precedence.
 * Policies and rates are firmware-only. Keep units and dependent rates aligned. */
#define FIRMWARE_PRODUCT "vessel-motion-p4"
#define FIRMWARE_VERSION "1.0.5"
#ifndef WIRELESS_ALLOWED
#define WIRELESS_ALLOWED 0
#endif
#define DEFAULT_NETWORK_DHCP 0
#define DEFAULT_HOSTNAME "vessel-motion"
#define DEFAULT_IP_ADDRESS "192.168.11.222"
#define DEFAULT_SUBNET_MASK "255.255.255.0"
#define DEFAULT_GATEWAY "192.168.11.1"
#define DEFAULT_DNS_SERVER "192.168.11.1"
#define DEFAULT_VESSEL_NAME "Vessel"
#define DEFAULT_PRODUCT_NAME "VESSEL MOTION"
#define DEFAULT_TIMEZONE "UTC"
#define WIFI_FALLBACK_SSID "Vessel-Motion"
#define WIFI_FALLBACK_PASS "vessel-motion"
#define WIFI_FALLBACK_IP "192.168.4.1"
#define WIFI_FALLBACK_NETMASK "255.255.255.0"
/* GPIO54 drives C6 CHIP_PU on the Nano: low holds the coprocessor in reset. */
#define WIRELESS_RESET_GPIO 54
#define WIRELESS_RESET_LEVEL 0

#define SAMPLE_HZ 200
#define TELEMETRY_HZ 50
#define TRIP_SAMPLE_HZ 5
#define SEVERITY_HZ 10
#define OVERVIEW_SAMPLE_HZ 0.5f
#define CALIBRATION_SECONDS 5
#define MAX_TRIP_SECONDS 28800
#define TRIP_RETENTION 100
#define DEFAULT_ATTITUDE_Y_RANGE_DEG 15
#define DEFAULT_VIBRATION_ENABLED 0
#define DEFAULT_VIBRATION_STRENGTH 3
#define VIBRATION_CUTOFFS_HZ { 20.0f, 15.0f, 10.0f, 7.0f, 5.0f }

#define ALGORITHM_VERSION 1
#define DEFAULT_RSI_ENABLED 1
#define DEFAULT_MEI_ENABLED 1
#define DEFAULT_ALPHA 0.98f
#define DEFAULT_FAST_HZ 25.0f
#define DEFAULT_SLOW_HZ 4.0f
#define DEFAULT_DERIVATIVE_HZ 12.0f
#define DEFAULT_EXPONENT 2.0f
#define DEFAULT_PEAK_LOW 0.10f
#define DEFAULT_PEAK_HIGH 1.50f
#define DEFAULT_RMS_LOW 0.03f
#define DEFAULT_RMS_HIGH 0.50f
#define DEFAULT_JERK_LOW 0.5f
#define DEFAULT_JERK_HIGH 15.0f
#define DEFAULT_ROLL_LOW 2.0f
#define DEFAULT_ROLL_HIGH 25.0f
#define DEFAULT_PITCH_LOW 2.0f
#define DEFAULT_PITCH_HIGH 25.0f
#define DEFAULT_REPEAT_LOW 1.0f
#define DEFAULT_REPEAT_HIGH 20.0f
#define DEFAULT_IMPACT_START_G 0.25f
#define DEFAULT_IMPACT_END_G 0.15f
/* Impact threshold slider: rows are levels 1-5, in increasing threshold order.
 * Each row is {start_g, end_g}, measured on gravity-removed, fast-filtered
 * resultant acceleration (not raw acceleration including gravity).
 * Start: crossing this value begins an impact. End: acceleration must remain
 * at/below this lower value for DEFAULT_IMPACT_END_MS to finish that impact.
 * End < start prevents one movement repeatedly starting/ending an event.
 * End is initially 60% of start; tune both values together if changing a row.
 * Level 3 uses the defaults above, preserving the original 0.25/0.15 g behavior.
 * Presets affect detection/counts, NOT measured G or severity normalization.
 * Saved numeric thresholds take precedence; changing this table never rewrites
 * historical trips or saved settings (nonmatching saved pairs become Custom).
 */
#define IMPACT_THRESHOLD_LEVELS_G { \
    { 0.10f, 0.06f }, \
    { 0.17f, 0.102f }, \
    { DEFAULT_IMPACT_START_G, DEFAULT_IMPACT_END_G }, \
    { 0.40f, 0.24f }, \
    { 0.60f, 0.36f } \
}
#define DEFAULT_IMPACT_END_MS 100
#define DEFAULT_IMPACT_DEADBAND_MS 150
#define DEFAULT_IMPACT_LOW 0.25f
#define DEFAULT_IMPACT_HIGH 1.50f
#define DEFAULT_ATTACK_SECONDS 0.20f
#define DEFAULT_DECAY_SECONDS 5.0f
#define DEFAULT_MEI_RMS_LOW 0.02f
#define DEFAULT_MEI_RMS_HIGH 0.30f
#define DEFAULT_MEI_SHOCK_LOW 0.03f
#define DEFAULT_MEI_SHOCK_HIGH 0.50f
#define DEFAULT_MEI_TAU_HOURS 4.0f
/* No 'f' suffixes in these lists: the same values also generate settings CSV. */
#define VM_STRINGIFY_LIST_(...) #__VA_ARGS__
#define VM_STRINGIFY_LIST(...) VM_STRINGIFY_LIST_(__VA_ARGS__)
#define DEFAULT_RSI_WEIGHT_VALUES 0.35, 0.25, 0.20, 0.10, 0.10
#define DEFAULT_RSI_WEIGHTS_CSV VM_STRINGIFY_LIST(DEFAULT_RSI_WEIGHT_VALUES)
#define DEFAULT_RSI_WEIGHTS { DEFAULT_RSI_WEIGHT_VALUES }
#define DEFAULT_MEI_AXIS_VALUES 1, 1, 1
#define DEFAULT_MEI_AXIS_CSV VM_STRINGIFY_LIST(DEFAULT_MEI_AXIS_VALUES)
#define DEFAULT_MEI_AXIS { DEFAULT_MEI_AXIS_VALUES }
#define DEFAULT_MEI_CURRENT_WEIGHT_VALUES 0.45, 0.35, 0.20
#define DEFAULT_MEI_CURRENT_WEIGHTS_CSV VM_STRINGIFY_LIST(DEFAULT_MEI_CURRENT_WEIGHT_VALUES)
#define DEFAULT_MEI_CURRENT_WEIGHTS { DEFAULT_MEI_CURRENT_WEIGHT_VALUES }
#define DEFAULT_MEI_TRIP_WEIGHT_VALUES 0.40, 0.40, 0.20
#define DEFAULT_MEI_TRIP_WEIGHTS_CSV VM_STRINGIFY_LIST(DEFAULT_MEI_TRIP_WEIGHT_VALUES)
#define DEFAULT_MEI_TRIP_WEIGHTS { DEFAULT_MEI_TRIP_WEIGHT_VALUES }
#define DEFAULT_TRIP_RSI_WEIGHTS { 0.40f, 0.35f, 0.15f, 0.10f }
#define DEFAULT_TRIP_IMPACT_RANGE { 1.0f, 20.0f, DEFAULT_EXPONENT }
#define DEFAULT_IMPACT_JERK_RANGE { 1.0f, 15.0f, DEFAULT_EXPONENT }
#define DEFAULT_IDLE_RMS_G 0.015f
#define DEFAULT_IDLE_RATE_DEG_S 0.5f
#define DEFAULT_IDLE_SECONDS 30.0f

/* Sensor anti-alias bandwidth remains legacy by default pending boat validation.
 * MPU6050 DLPF: 0=260 Hz accel; 2=94 Hz; 3=44 Hz. Never change silently. */
#define SENSOR_DLPF_CONFIG 0
#define SENSOR_SAMPLE_DIVIDER 0

_Static_assert(DEFAULT_FAST_HZ > 0 && DEFAULT_FAST_HZ < SAMPLE_HZ / 2.0f,
               "Default fast cutoff must be below Nyquist");
_Static_assert(DEFAULT_SLOW_HZ > 0 && DEFAULT_SLOW_HZ < SAMPLE_HZ / 2.0f,
               "Default slow cutoff must be below Nyquist");
_Static_assert(SENSOR_DLPF_CONFIG >= 0 && SENSOR_DLPF_CONFIG <= 6,
               "Invalid IMU DLPF configuration");
_Static_assert(OVERVIEW_SAMPLE_HZ > 0 && OVERVIEW_SAMPLE_HZ <= SAMPLE_HZ,
               "Invalid overview sampling rate");
_Static_assert(CALIBRATION_SECONDS > 0, "Calibration must contain samples");

#if SAMPLE_HZ <= 0 || SAMPLE_HZ > 1000 || TELEMETRY_HZ <= 0 || TELEMETRY_HZ > SAMPLE_HZ
#error Invalid sampling or telemetry rate
#endif
#if TRIP_SAMPLE_HZ <= 0 || TRIP_SAMPLE_HZ > SAMPLE_HZ || SAMPLE_HZ % TRIP_SAMPLE_HZ != 0
#error Trip sample rate must divide the acquisition rate
#endif
#if SEVERITY_HZ <= 0 || SEVERITY_HZ > SAMPLE_HZ || TRIP_RETENTION < 1 || TRIP_RETENTION > 1000
#error Invalid severity rate or trip retention (supported retention: 1-1000)
#endif
#if MAX_TRIP_SECONDS < 1 || MAX_TRIP_SECONDS > 4294967
#error Trip duration exceeds the millisecond record format
#endif
