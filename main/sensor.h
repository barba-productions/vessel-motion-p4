/*
 * MPU-6050 driver for Vessel Motion P4.
 * Mirrors the Pi project's sensor.py: WHO_AM_I check, identical register init
 * sequence and scaling, frozen-channel and over-range guards.
 *
 * I2C on GPIO32 (SCL) / GPIO33 (SDA), 100 kHz, address 0x68.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "motion.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t timestamp_us;      /* esp_timer microseconds */
    vec3_t  accel_uncalibrated;
    vec3_t  gyro_uncalibrated;
    vec3_t  accel;             /* calibrated, sensor frame */
    vec3_t  accel_vessel;      /* axis-mapped */
    vec3_t  gyro;              /* calibrated + axis-mapped (axial) */
    int16_t temperature_raw;
} sensor_sample_t;

/* Initialise I2C bus + MPU-6050. Returns ESP_OK and fills model (e.g. "MPU-6050").
 * Applies axis map and calibration offsets. */
esp_err_t sensor_init(const axis_map_t *axis_map, vec3_t accel_offset, vec3_t gyro_offset,
                      const char **model);

/* Read one 14-byte burst sample. Returns ESP_OK on success. */
esp_err_t sensor_read(sensor_sample_t *out);

/* Update calibration offsets at runtime (after a "calibrate" command). */
void sensor_set_offsets(vec3_t accel_offset, vec3_t gyro_offset);

/* Update the axis mapping at runtime (after a settings change). */
void sensor_set_axis_map(const axis_map_t *axis_map);

#ifdef __cplusplus
}
#endif
