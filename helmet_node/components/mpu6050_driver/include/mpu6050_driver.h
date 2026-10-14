#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MPU6050_DEFAULT_I2C_PORT       0
#define MPU6050_DEFAULT_I2C_CLOCK_HZ   400000U
#define MPU6050_DEFAULT_ADDRESS        0x68U
#define MPU6050_DEFAULT_MOCK_ENABLED   false

typedef enum {
    MPU6050_ACCEL_RANGE_2G = 0x00,
    MPU6050_ACCEL_RANGE_4G = 0x08,
    MPU6050_ACCEL_RANGE_8G = 0x10,
    MPU6050_ACCEL_RANGE_16G = 0x18,
} mpu6050_accel_range_t;

typedef enum {
    MPU6050_GYRO_RANGE_250DPS = 0x00,
    MPU6050_GYRO_RANGE_500DPS = 0x08,
    MPU6050_GYRO_RANGE_1000DPS = 0x10,
    MPU6050_GYRO_RANGE_2000DPS = 0x18,
} mpu6050_gyro_range_t;

typedef enum {
    MPU6050_DLPF_260HZ = 0x00,
    MPU6050_DLPF_184HZ = 0x01,
    MPU6050_DLPF_94HZ = 0x02,
    MPU6050_DLPF_44HZ = 0x03,
    MPU6050_DLPF_21HZ = 0x04,
    MPU6050_DLPF_10HZ = 0x05,
    MPU6050_DLPF_5HZ = 0x06,
} mpu6050_dlpf_t;

#define MPU6050_DEFAULT_ACCEL_RANGE     MPU6050_ACCEL_RANGE_4G
#define MPU6050_DEFAULT_GYRO_RANGE      MPU6050_GYRO_RANGE_250DPS
#define MPU6050_DEFAULT_DLPF            MPU6050_DLPF_44HZ
#define MPU6050_DEFAULT_SAMPLE_DIVIDER  0x07U

typedef struct {
    int i2c_port;
    int sda_pin;
    int scl_pin;
    uint32_t clock_speed_hz;
    uint8_t device_address;
    mpu6050_accel_range_t accel_range;
    mpu6050_gyro_range_t gyro_range;
    mpu6050_dlpf_t dlpf;
    uint8_t sample_rate_divider;
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
