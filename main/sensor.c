/* MPU-6050 driver — see sensor.h. Matches Pi sensor.py register map exactly. */
#include "sensor.h"
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "mpu6050";

#define I2C_SCL_GPIO        32
#define I2C_SDA_GPIO        33
#define I2C_PORT            I2C_NUM_0
#define I2C_SPEED_HZ        100000
#define MPU_ADDR            0x68

/* Register map */
#define REG_SMPLRT_DIV      0x19
#define REG_CONFIG          0x1A
#define REG_GYRO_CONFIG     0x1B
#define REG_ACCEL_CONFIG    0x1C
#define REG_ACCEL_XOUT_H    0x3B
#define REG_WHO_AM_I        0x75
#define REG_PWR_MGMT_1      0x6B

/* Scaling (accel +/-2 g, gyro +/-500 dps) — identical to Pi */
#define ACCEL_SCALE         (MOTION_G0 / 16384.0f)
#define GYRO_SCALE          ((float)M_PI / (65.5f * 180.0f))

#define FROZEN_LIMIT        400      /* 2 s at 200 Hz */
#define OVERRANGE_G         2.1f

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static axis_map_t s_axis_map;
static vec3_t s_accel_offset, s_gyro_offset;
static uint8_t s_identity;
static int16_t s_prev_raw[7];
static bool s_have_prev;
static int s_unchanged;

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

static esp_err_t read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, data, len, 100);
}

esp_err_t sensor_init(const axis_map_t *axis_map, vec3_t accel_offset, vec3_t gyro_offset,
                      const char **model)
{
    s_axis_map = *axis_map;
    s_accel_offset = accel_offset;
    s_gyro_offset = gyro_offset;
    s_have_prev = false;
    s_unchanged = 0;

    /* Create the I2C bus once; reuse it across retries (a failed probe must not
     * leak the bus — otherwise every retry fails with "bus already acquired"). */
    esp_err_t err;
    if (!s_bus) {
        i2c_master_bus_config_t bus_cfg = {
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .i2c_port = I2C_PORT,
            .scl_io_num = I2C_SCL_GPIO,
            .sda_io_num = I2C_SDA_GPIO,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        err = i2c_new_master_bus(&bus_cfg, &s_bus);
        if (err != ESP_OK) return err;
    }
    /* Drop any previous device handle before re-adding. */
    if (s_dev) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPU_ADDR,
        .scl_speed_hz = I2C_SPEED_HZ,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) return err;

    /* WHO_AM_I */
    err = read_reg(REG_WHO_AM_I, &s_identity, 1);
    if (err != ESP_OK) return err;
    if (s_identity != 0x68 && s_identity != 0x74) {
        ESP_LOGE(TAG, "Unsupported IMU identity 0x%02x", s_identity);
        return ESP_ERR_NOT_FOUND;
    }
    if (model) *model = (s_identity == 0x68) ? "MPU-6050" : "MPU-6515-compatible";

    /* Init sequence: identical to Pi ((0x19,0),(0x1A,0),(0x1B,8),(0x1C,0),(0x6B,1)) */
    const uint8_t init[][2] = {
        { REG_SMPLRT_DIV, 0 }, { REG_CONFIG, 0 }, { REG_GYRO_CONFIG, 0x08 },
        { REG_ACCEL_CONFIG, 0 }, { REG_PWR_MGMT_1, 1 },
    };
    for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); i++) {
        err = write_reg(init[i][0], init[i][1]);
        if (err != ESP_OK) return err;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Verify */
    for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); i++) {
        uint8_t actual = 0;
        err = read_reg(init[i][0], &actual, 1);
        if (err != ESP_OK) return err;
        if (actual != init[i][1]) {
            ESP_LOGE(TAG, "Reg 0x%02x: expected %d got %d", init[i][0], init[i][1], actual);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    ESP_LOGI(TAG, "Connected to %s at I2C 0x%02x", model ? *model : "MPU", MPU_ADDR);
    return ESP_OK;
}

void sensor_set_offsets(vec3_t accel_offset, vec3_t gyro_offset)
{
    s_accel_offset = accel_offset;
    s_gyro_offset = gyro_offset;
}

void sensor_set_axis_map(const axis_map_t *axis_map)
{
    if (axis_map) s_axis_map = *axis_map;
}

esp_err_t sensor_read(sensor_sample_t *out)
{
    uint8_t raw[14];
    uint8_t reg = REG_ACCEL_XOUT_H;
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, raw, sizeof(raw), 100);
    if (err != ESP_OK) return err;

    int16_t vals[7];
    for (int i = 0; i < 7; i++) {
        vals[i] = (int16_t)((raw[i * 2] << 8) | raw[i * 2 + 1]);
    }

    /* frozen-channel guard */
    if (s_have_prev && memcmp(vals, s_prev_raw, sizeof(vals)) == 0) {
        if (++s_unchanged >= FROZEN_LIMIT) return ESP_ERR_INVALID_STATE;
    } else {
        s_unchanged = 0;
    }
    memcpy(s_prev_raw, vals, sizeof(vals));
    s_have_prev = true;

    vec3_t accel = { vals[0] * ACCEL_SCALE, vals[1] * ACCEL_SCALE, vals[2] * ACCEL_SCALE };
    if (fabsf(accel.x) > OVERRANGE_G * MOTION_G0 ||
        fabsf(accel.y) > OVERRANGE_G * MOTION_G0 ||
        fabsf(accel.z) > OVERRANGE_G * MOTION_G0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    vec3_t gyro = { vals[4] * GYRO_SCALE, vals[5] * GYRO_SCALE, vals[6] * GYRO_SCALE };

    out->accel_uncalibrated = accel;
    out->gyro_uncalibrated = gyro;
    out->accel = vec3_sub(accel, s_accel_offset);
    vec3_t g = vec3_sub(gyro, s_gyro_offset);
    out->gyro = axis_map_apply_axial(&s_axis_map, g);
    out->accel_vessel = axis_map_apply(&s_axis_map, out->accel);
    out->temperature_raw = vals[3];
    out->timestamp_us = esp_timer_get_time();
    return ESP_OK;
}
