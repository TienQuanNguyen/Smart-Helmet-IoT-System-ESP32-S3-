#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MPU6050_DEFAULT_I2C_PORT       0
#define MPU6050_DEFAULT_I2C_CLOCK_HZ   400000U
#define MPU6050_DEFAULT_ADDRESS        0x68U
#define MPU6050_DEFAULT_MOCK_ENABLED   false

typedef struct {
    int i2c_port;
    int sda_pin;
    int scl_pin;
    uint32_t clock_speed_hz;
    uint8_t device_address;
    bool mock_enabled;
} mpu6050_config_t;

typedef struct {
    int16_t ax;
    int16_t ay;
    int16_t az;
    int16_t gx;
    int16_t gy;
    int16_t gz;
} mpu6050_raw_t;

typedef struct {
    float x;
    float y;
    float z;
} mpu6050_accel_t;

typedef struct {
    float x;
    float y;
    float z;
} mpu6050_gyro_t;

bool mpu6050_init(void);
bool mpu6050_init_with_config(const mpu6050_config_t *config);
bool mpu6050_read_who_am_i(uint8_t *who_am_i);
bool mpu6050_read_raw(mpu6050_raw_t *raw);
bool mpu6050_read_accel_g(mpu6050_accel_t *accel);
bool mpu6050_read_gyro_dps(mpu6050_gyro_t *gyro);
