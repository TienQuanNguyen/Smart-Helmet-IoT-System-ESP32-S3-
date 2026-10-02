#include "mpu6050_driver.h"

#include <stddef.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MPU6050_DEFAULT_SDA_PIN 8
#define MPU6050_DEFAULT_SCL_PIN 9
#define MPU6050_I2C_TIMEOUT_MS 1000

#define MPU6050_REG_SMPLRT_DIV 0x19
#define MPU6050_REG_CONFIG 0x1A
#define MPU6050_REG_GYRO_CONFIG 0x1B
#define MPU6050_REG_ACCEL_CONFIG 0x1C
#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_REG_PWR_MGMT_1 0x6B
#define MPU6050_REG_WHO_AM_I 0x75

#define MPU6050_PWR_WAKEUP 0x00
#define MPU6050_RAW_FRAME_LEN 14

static const char *TAG = "mpu6050_driver";

static i2c_port_t s_i2c_port = (i2c_port_t)MPU6050_DEFAULT_I2C_PORT;
static uint8_t s_device_address = MPU6050_DEFAULT_ADDRESS;
static bool s_i2c_ready;
static bool s_initialized;
static bool s_mock_enabled;
static int16_t s_mock_sample_index;
static float s_accel_lsb_per_g = 8192.0f;
static float s_gyro_lsb_per_dps = 131.0f;

static bool mpu6050_is_valid_accel_range(mpu6050_accel_range_t range) {
  return (range == MPU6050_ACCEL_RANGE_2G) ||
         (range == MPU6050_ACCEL_RANGE_4G) ||
         (range == MPU6050_ACCEL_RANGE_8G) ||
         (range == MPU6050_ACCEL_RANGE_16G);
}

static bool mpu6050_is_valid_gyro_range(mpu6050_gyro_range_t range) {
  return (range == MPU6050_GYRO_RANGE_250DPS) ||
         (range == MPU6050_GYRO_RANGE_500DPS) ||
         (range == MPU6050_GYRO_RANGE_1000DPS) ||
         (range == MPU6050_GYRO_RANGE_2000DPS);
}

static float mpu6050_accel_scale(mpu6050_accel_range_t range) {
  switch (range) {
    case MPU6050_ACCEL_RANGE_2G:
      return 16384.0f;
    case MPU6050_ACCEL_RANGE_4G:
      return 8192.0f;
    case MPU6050_ACCEL_RANGE_8G:
      return 4096.0f;
    case MPU6050_ACCEL_RANGE_16G:
      return 2048.0f;
    default:
      return 0.0f;
  }
}

static float mpu6050_gyro_scale(mpu6050_gyro_range_t range) {
  switch (range) {
    case MPU6050_GYRO_RANGE_250DPS:
      return 131.0f;
    case MPU6050_GYRO_RANGE_500DPS:
      return 65.5f;
    case MPU6050_GYRO_RANGE_1000DPS:
      return 32.8f;
    case MPU6050_GYRO_RANGE_2000DPS:
      return 16.4f;
    default:
      return 0.0f;
  }
}

static bool mpu6050_is_valid_config(const mpu6050_config_t *config) {
  return (config != NULL) && (config->i2c_port >= I2C_NUM_0) &&
         (config->i2c_port < I2C_NUM_MAX) && (config->sda_pin >= 0) &&
         (config->scl_pin >= 0) && (config->clock_speed_hz > 0) &&
         (config->device_address > 0) && (config->device_address < 0x80) &&
         mpu6050_is_valid_accel_range(config->accel_range) &&
         mpu6050_is_valid_gyro_range(config->gyro_range) &&
         (config->dlpf >= MPU6050_DLPF_260HZ) &&
         (config->dlpf <= MPU6050_DLPF_5HZ);
}

static bool mpu6050_check_err(esp_err_t err, const char *action) {
  if (err == ESP_OK) {
    return true;
  }

  ESP_LOGE(TAG, "%s failed: %s", action, esp_err_to_name(err));
  return false;
}

static int16_t mpu6050_make_i16(uint8_t high, uint8_t low) {
  return (int16_t)(((uint16_t)high << 8) | low);
}

static bool mpu6050_i2c_master_init(const mpu6050_config_t *config) {
  const i2c_config_t i2c_config = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = (gpio_num_t)config->sda_pin,
      .scl_io_num = (gpio_num_t)config->scl_pin,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = config->clock_speed_hz,
      .clk_flags = 0,
  };

  ESP_LOGI(TAG, "I2C init: port=%d SDA=GPIO%d SCL=GPIO%d freq=%luHz addr=0x%02X",
           config->i2c_port, config->sda_pin, config->scl_pin,
           (unsigned long)config->clock_speed_hz, config->device_address);

  esp_err_t err = i2c_param_config((i2c_port_t)config->i2c_port, &i2c_config);
  if (!mpu6050_check_err(err, "i2c_param_config")) {
    return false;
  }

  err = i2c_driver_install((i2c_port_t)config->i2c_port, I2C_MODE_MASTER, 0, 0,
                           0);
  if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
    ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
    return false;
  }

  if (err == ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "I2C driver already installed on port %d", config->i2c_port);
  } else {
    ESP_LOGI(TAG, "I2C driver installed on port %d", config->i2c_port);
  }

  s_i2c_port = (i2c_port_t)config->i2c_port;
  s_device_address = config->device_address;
  s_i2c_ready = true;
  return true;
}

static bool mpu6050_write_register(uint8_t reg_addr, uint8_t value) {
  if (!s_i2c_ready) {
    ESP_LOGE(TAG, "I2C is not initialized");
    return false;
  }

  const uint8_t write_buf[2] = {reg_addr, value};
  return mpu6050_check_err(
      i2c_master_write_to_device(s_i2c_port, s_device_address, write_buf,
                                 sizeof(write_buf),
                                 pdMS_TO_TICKS(MPU6050_I2C_TIMEOUT_MS)),
      "i2c_master_write_to_device");
}

static bool mpu6050_read_registers(uint8_t start_reg, uint8_t *data,
                                   size_t len) {
  if (!s_i2c_ready) {
    ESP_LOGE(TAG, "I2C is not initialized");
    return false;
  }
  if ((data == NULL) || (len == 0)) {
    ESP_LOGE(TAG, "Invalid read buffer");
    return false;
  }

  return mpu6050_check_err(
      i2c_master_write_read_device(
          s_i2c_port, s_device_address, &start_reg, 1, data, len,
          pdMS_TO_TICKS(MPU6050_I2C_TIMEOUT_MS)),
      "i2c_master_write_read_device");
}

static bool mpu6050_verify_register(uint8_t reg_addr, uint8_t expected,
                                    const char *name) {
  uint8_t actual = 0;
  if (!mpu6050_read_registers(reg_addr, &actual, 1)) {
    return false;
  }
  if (actual != expected) {
    ESP_LOGE(TAG, "%s readback mismatch: expected=0x%02X actual=0x%02X",
             name, expected, actual);
    return false;
  }
  ESP_LOGI(TAG, "%s readback verified: 0x%02X", name, actual);
  return true;
}

bool mpu6050_init(void) {
  const mpu6050_config_t default_config = {
      .i2c_port = MPU6050_DEFAULT_I2C_PORT,
      .sda_pin = MPU6050_DEFAULT_SDA_PIN,
      .scl_pin = MPU6050_DEFAULT_SCL_PIN,
      .clock_speed_hz = MPU6050_DEFAULT_I2C_CLOCK_HZ,
      .device_address = MPU6050_DEFAULT_ADDRESS,
      .accel_range = MPU6050_DEFAULT_ACCEL_RANGE,
      .gyro_range = MPU6050_DEFAULT_GYRO_RANGE,
      .dlpf = MPU6050_DEFAULT_DLPF,
      .sample_rate_divider = MPU6050_DEFAULT_SAMPLE_DIVIDER,
      .mock_enabled = MPU6050_DEFAULT_MOCK_ENABLED,
  };

  return mpu6050_init_with_config(&default_config);
}

bool mpu6050_init_with_config(const mpu6050_config_t *config) {
  if (s_initialized) {
    return true;
  }
  if (!mpu6050_is_valid_config(config)) {
    ESP_LOGE(TAG, "Invalid MPU6050 config");
    return false;
  }

  s_i2c_port = (i2c_port_t)config->i2c_port;
  s_device_address = config->device_address;
  s_mock_enabled = config->mock_enabled;
  s_accel_lsb_per_g = mpu6050_accel_scale(config->accel_range);
  s_gyro_lsb_per_dps = mpu6050_gyro_scale(config->gyro_range);

  if (s_mock_enabled) {
    s_initialized = true;
    ESP_LOGW(TAG, "MPU6050 mock mode enabled");
    return true;
  }

  if (!s_i2c_ready && !mpu6050_i2c_master_init(config)) {
    return false;
  }

  if (!mpu6050_write_register(MPU6050_REG_PWR_MGMT_1, MPU6050_PWR_WAKEUP)) {
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(100));

  if (!mpu6050_write_register(MPU6050_REG_SMPLRT_DIV,
                              config->sample_rate_divider) ||
      !mpu6050_write_register(MPU6050_REG_CONFIG, (uint8_t)config->dlpf) ||
      !mpu6050_write_register(MPU6050_REG_GYRO_CONFIG,
                              (uint8_t)config->gyro_range) ||
      !mpu6050_write_register(MPU6050_REG_ACCEL_CONFIG,
                              (uint8_t)config->accel_range)) {
    return false;
  }

  if (!mpu6050_verify_register(MPU6050_REG_SMPLRT_DIV,
                               config->sample_rate_divider, "SMPLRT_DIV") ||
      !mpu6050_verify_register(MPU6050_REG_CONFIG, (uint8_t)config->dlpf,
                               "CONFIG") ||
      !mpu6050_verify_register(MPU6050_REG_GYRO_CONFIG,
                               (uint8_t)config->gyro_range, "GYRO_CONFIG") ||
      !mpu6050_verify_register(MPU6050_REG_ACCEL_CONFIG,
                               (uint8_t)config->accel_range,
                               "ACCEL_CONFIG")) {
    return false;
  }

  uint8_t who_am_i = 0;
  if (!mpu6050_read_who_am_i(&who_am_i)) {
    return false;
  }
  if (who_am_i != MPU6050_DEFAULT_ADDRESS) {
    ESP_LOGE(TAG, "Unexpected WHO_AM_I: 0x%02X", who_am_i);
    return false;
  }

  s_initialized = true;
  ESP_LOGI(TAG,
           "MPU6050 initialized, WHO_AM_I=0x%02X accel_cfg=0x%02X "
           "(%.0f LSB/g) gyro_cfg=0x%02X (%.1f LSB/dps)",
           who_am_i, config->accel_range, s_accel_lsb_per_g,
           config->gyro_range, s_gyro_lsb_per_dps);
  return true;
}

bool mpu6050_read_who_am_i(uint8_t *who_am_i) {
  if (who_am_i == NULL) {
    ESP_LOGE(TAG, "WHO_AM_I output is NULL");
    return false;
  }
  if (s_mock_enabled) {
    *who_am_i = MPU6050_DEFAULT_ADDRESS;
    return true;
  }
  return mpu6050_read_registers(MPU6050_REG_WHO_AM_I, who_am_i, 1);
}

bool mpu6050_read_raw(mpu6050_raw_t *raw) {
  if (raw == NULL) {
    ESP_LOGE(TAG, "Raw output is NULL");
    return false;
  }
  if (!s_initialized) {
    ESP_LOGE(TAG, "MPU6050 is not initialized");
    return false;
  }

  if (s_mock_enabled) {
    s_mock_sample_index++;
    raw->ax = s_mock_sample_index % 256;
    raw->ay = -(s_mock_sample_index % 128);
    raw->az = (int16_t)(s_accel_lsb_per_g + (s_mock_sample_index % 64));
    raw->gx = s_mock_sample_index % 32;
    raw->gy = -(s_mock_sample_index % 16);
    raw->gz = s_mock_sample_index % 8;
    return true;
  }

  uint8_t data[MPU6050_RAW_FRAME_LEN] = {0};
  if (!mpu6050_read_registers(MPU6050_REG_ACCEL_XOUT_H, data, sizeof(data))) {
    return false;
  }

  raw->ax = mpu6050_make_i16(data[0], data[1]);
  raw->ay = mpu6050_make_i16(data[2], data[3]);
  raw->az = mpu6050_make_i16(data[4], data[5]);
  raw->gx = mpu6050_make_i16(data[8], data[9]);
  raw->gy = mpu6050_make_i16(data[10], data[11]);
  raw->gz = mpu6050_make_i16(data[12], data[13]);
  return true;
}

bool mpu6050_read_accel_g(mpu6050_accel_t *accel) {
  if (accel == NULL) {
    ESP_LOGE(TAG, "Accel output is NULL");
    return false;
  }

  mpu6050_raw_t raw = {0};
  if (!mpu6050_read_raw(&raw)) {
    return false;
  }
  accel->x = (float)raw.ax / s_accel_lsb_per_g;
  accel->y = (float)raw.ay / s_accel_lsb_per_g;
  accel->z = (float)raw.az / s_accel_lsb_per_g;
  return true;
}

bool mpu6050_read_gyro_dps(mpu6050_gyro_t *gyro) {
  if (gyro == NULL) {
    ESP_LOGE(TAG, "Gyro output is NULL");
    return false;
  }

  mpu6050_raw_t raw = {0};
  if (!mpu6050_read_raw(&raw)) {
    return false;
  }
  gyro->x = (float)raw.gx / s_gyro_lsb_per_dps;
  gyro->y = (float)raw.gy / s_gyro_lsb_per_dps;
  gyro->z = (float)raw.gz / s_gyro_lsb_per_dps;
  return true;
}
