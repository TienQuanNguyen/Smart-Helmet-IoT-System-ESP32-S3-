#include "mpu6050_driver.h"

#include <stddef.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MPU6050_DEFAULT_SDA_PIN 21
#define MPU6050_DEFAULT_SCL_PIN 22

#define MPU6050_I2C_TIMEOUT_MS 1000

#define MPU6050_REG_SMPLRT_DIV 0x19
#define MPU6050_REG_CONFIG 0x1A
#define MPU6050_REG_GYRO_CONFIG 0x1B
#define MPU6050_REG_ACCEL_CONFIG 0x1C
#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_REG_PWR_MGMT_1 0x6B
#define MPU6050_REG_WHO_AM_I 0x75

#define MPU6050_PWR_WAKEUP 0x00
#define MPU6050_DLPF_CFG_44HZ 0x03
#define MPU6050_ACCEL_FS_2G 0x00
#define MPU6050_GYRO_FS_250DPS 0x00
#define MPU6050_SAMPLE_DIV_125HZ 0x07

#define MPU6050_ACCEL_LSB_PER_G 16384.0f
#define MPU6050_GYRO_LSB_PER_DPS 131.0f
#define MPU6050_RAW_FRAME_LEN 14

static const char *TAG = "mpu6050_driver";

static i2c_port_t s_i2c_port = (i2c_port_t)MPU6050_DEFAULT_I2C_PORT;
static uint8_t s_device_address = MPU6050_DEFAULT_ADDRESS;
static bool s_i2c_ready;
static bool s_initialized;

static bool mpu6050_is_valid_config(const mpu6050_config_t *config) {
  return (config != NULL) && (config->i2c_port >= I2C_NUM_0) &&
         (config->sda_pin >= 0) && (config->scl_pin >= 0) &&
         (config->clock_speed_hz > 0) && (config->device_address > 0) &&
         (config->device_address < 0x80);
}

static bool mpu6050_check_err(esp_err_t err, const char *action) {
  if (err == ESP_OK) {
    return true;
  }

  ESP_LOGE(TAG, "%s failed: %s", action, esp_err_to_name(err));
  return false;
}

static int16_t mpu6050_make_i16(uint8_t high, uint8_t low) {
  return (int16_t)((high << 8) | low);
}

static bool mpu6050_i2c_master_init(const mpu6050_config_t *config) {
  i2c_config_t i2c_config = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = (gpio_num_t)config->sda_pin,
      .scl_io_num = (gpio_num_t)config->scl_pin,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = config->clock_speed_hz,
      .clk_flags = 0,
  };

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

  uint8_t write_buf[2] = {reg_addr, value};
  esp_err_t err = i2c_master_write_to_device(
      s_i2c_port, s_device_address, write_buf, sizeof(write_buf),
      pdMS_TO_TICKS(MPU6050_I2C_TIMEOUT_MS));

  return mpu6050_check_err(err, "i2c_master_write_to_device");
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

  esp_err_t err = i2c_master_write_read_device(
      s_i2c_port, s_device_address, &start_reg, 1, data, len,
      pdMS_TO_TICKS(MPU6050_I2C_TIMEOUT_MS));

  return mpu6050_check_err(err, "i2c_master_write_read_device");
}

bool mpu6050_init(void) {
  const mpu6050_config_t default_config = {
      .i2c_port = MPU6050_DEFAULT_I2C_PORT,
      .sda_pin = MPU6050_DEFAULT_SDA_PIN,
      .scl_pin = MPU6050_DEFAULT_SCL_PIN,
      .clock_speed_hz = MPU6050_DEFAULT_I2C_CLOCK_HZ,
      .device_address = MPU6050_DEFAULT_ADDRESS,
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

  if (!s_i2c_ready && !mpu6050_i2c_master_init(config)) {
    return false;
  }

  if (!mpu6050_write_register(MPU6050_REG_PWR_MGMT_1, MPU6050_PWR_WAKEUP)) {
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(100));

  if (!mpu6050_write_register(MPU6050_REG_SMPLRT_DIV,
                              MPU6050_SAMPLE_DIV_125HZ) ||
      !mpu6050_write_register(MPU6050_REG_CONFIG, MPU6050_DLPF_CFG_44HZ) ||
      !mpu6050_write_register(MPU6050_REG_GYRO_CONFIG,
                              MPU6050_GYRO_FS_250DPS) ||
      !mpu6050_write_register(MPU6050_REG_ACCEL_CONFIG, MPU6050_ACCEL_FS_2G)) {
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
  ESP_LOGI(TAG, "MPU6050 initialized, WHO_AM_I=0x%02X", who_am_i);

  return true;
}

bool mpu6050_read_who_am_i(uint8_t *who_am_i) {
  if (who_am_i == NULL) {
    ESP_LOGE(TAG, "WHO_AM_I output is NULL");
    return false;
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

  accel->x = (float)raw.ax / MPU6050_ACCEL_LSB_PER_G;
  accel->y = (float)raw.ay / MPU6050_ACCEL_LSB_PER_G;
  accel->z = (float)raw.az / MPU6050_ACCEL_LSB_PER_G;

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

  gyro->x = (float)raw.gx / MPU6050_GYRO_LSB_PER_DPS;
  gyro->y = (float)raw.gy / MPU6050_GYRO_LSB_PER_DPS;
  gyro->z = (float)raw.gz / MPU6050_GYRO_LSB_PER_DPS;

  return true;
}
