#include "hardware_test_internal.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PHASE2_I2C_PORT I2C_NUM_0
#define PHASE2_I2C_FREQ_HZ 100000
#define PHASE2_I2C_TIMEOUT_MS 1000
#define PHASE2_INTERRUPT_TIMEOUT_MS 2000
#define PHASE2_LOG_EVERY_SAMPLES 50U

#define MPU6050_ADDRESS_AD0_LOW 0x68
#define MPU6050_ADDRESS_AD0_HIGH 0x69

#define MPU6050_REG_SMPLRT_DIV 0x19
#define MPU6050_REG_CONFIG 0x1A
#define MPU6050_REG_GYRO_CONFIG 0x1B
#define MPU6050_REG_ACCEL_CONFIG 0x1C
#define MPU6050_REG_INT_PIN_CFG 0x37
#define MPU6050_REG_INT_ENABLE 0x38
#define MPU6050_REG_INT_STATUS 0x3A
#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_REG_PWR_MGMT_1 0x6B
#define MPU6050_REG_WHO_AM_I 0x75

#define MPU6050_EXPECTED_WHO_AM_I 0x68
#define MPU6050_PWR_WAKEUP 0x00
#define MPU6050_DLPF_CONFIG 0x03
#define MPU6050_SAMPLE_RATE_DIV 0x07
#define MPU6050_GYRO_CONFIG_250DPS 0x00
#define MPU6050_ACCEL_CONFIG_4G 0x08
#define MPU6050_INT_PIN_CONFIG 0x30
#define MPU6050_DATA_READY_ENABLE 0x01

#define MPU6050_ACCEL_LSB_PER_G 8192.0f
#define MPU6050_GYRO_LSB_PER_DPS 131.0f
#define MPU6050_MOTION_FRAME_SIZE 14

static const char *TAG = "test_phase2";

static uint8_t s_mpu_address;
static int s_mpu_int_pin;
static TaskHandle_t s_mpu_task_handle;
static bool s_i2c_installed;
static bool s_isr_service_owned;
static bool s_isr_handler_added;

static void phase2_log_i2c_error(const char *action, uint8_t address,
                                 esp_err_t err) {
  if (err == ESP_FAIL) {
    ESP_LOGW(TAG, "%s 0x%02X -> ESP_FAIL: slave did not ACK", action, address);
  } else if (err == ESP_ERR_TIMEOUT) {
    ESP_LOGE(TAG, "%s 0x%02X -> ESP_ERR_TIMEOUT: bus busy/stuck", action,
             address);
  } else if (err == ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "%s 0x%02X -> ESP_ERR_INVALID_STATE", action, address);
  } else {
    ESP_LOGE(TAG, "%s 0x%02X -> %s (0x%x)", action, address,
             esp_err_to_name(err), (unsigned int)err);
  }
}

static esp_err_t phase2_probe_address(uint8_t address) {
  i2c_cmd_handle_t command = i2c_cmd_link_create();
  if (command == NULL) {
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = i2c_master_start(command);
  if (err == ESP_OK) {
    err = i2c_master_write_byte(
        command, (uint8_t)((address << 1) | I2C_MASTER_WRITE), true);
  }
  if (err == ESP_OK) {
    err = i2c_master_stop(command);
  }
  if (err == ESP_OK) {
    err = i2c_master_cmd_begin(PHASE2_I2C_PORT, command,
                               pdMS_TO_TICKS(PHASE2_I2C_TIMEOUT_MS));
  }

  i2c_cmd_link_delete(command);
  return err;
}

static esp_err_t phase2_write_register(uint8_t reg, uint8_t value) {
  const uint8_t data[2] = {reg, value};
  return i2c_master_write_to_device(PHASE2_I2C_PORT, s_mpu_address, data,
                                    sizeof(data),
                                    pdMS_TO_TICKS(PHASE2_I2C_TIMEOUT_MS));
}

static esp_err_t phase2_read_register(uint8_t reg, uint8_t *value) {
  if (value == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  return i2c_master_write_read_device(PHASE2_I2C_PORT, s_mpu_address, &reg, 1,
                                      value, 1,
                                      pdMS_TO_TICKS(PHASE2_I2C_TIMEOUT_MS));
}

static bool phase2_verify_register(uint8_t reg, uint8_t expected,
                                   const char *name) {
  uint8_t actual = 0;
  const esp_err_t err = phase2_read_register(reg, &actual);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "FAIL: %s readback: %s", name, esp_err_to_name(err));
    return false;
  }
  if (actual != expected) {
    ESP_LOGE(TAG, "FAIL: %s expected=0x%02X actual=0x%02X", name, expected,
             actual);
    return false;
  }
  ESP_LOGI(TAG, "PASS: %s readback=0x%02X", name, actual);
  return true;
}

static esp_err_t phase2_read_motion(int16_t *ax, int16_t *ay, int16_t *az,
                                    int16_t *gx, int16_t *gy, int16_t *gz) {
  if ((ax == NULL) || (ay == NULL) || (az == NULL) || (gx == NULL) ||
      (gy == NULL) || (gz == NULL)) {
    return ESP_ERR_INVALID_ARG;
  }

  const uint8_t start_reg = MPU6050_REG_ACCEL_XOUT_H;
  uint8_t data[MPU6050_MOTION_FRAME_SIZE] = {0};
  esp_err_t err = i2c_master_write_read_device(
      PHASE2_I2C_PORT, s_mpu_address, &start_reg, 1, data, sizeof(data),
      pdMS_TO_TICKS(PHASE2_I2C_TIMEOUT_MS));
  if (err != ESP_OK) {
    return err;
  }

  *ax = (int16_t)(((uint16_t)data[0] << 8) | data[1]);
  *ay = (int16_t)(((uint16_t)data[2] << 8) | data[3]);
  *az = (int16_t)(((uint16_t)data[4] << 8) | data[5]);
  *gx = (int16_t)(((uint16_t)data[8] << 8) | data[9]);
  *gy = (int16_t)(((uint16_t)data[10] << 8) | data[11]);
  *gz = (int16_t)(((uint16_t)data[12] << 8) | data[13]);
  return ESP_OK;
}

static bool phase2_i2c_init(const hardware_test_config_t *config) {
  const i2c_config_t bus_config = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = (gpio_num_t)config->i2c_sda_pin,
      .scl_io_num = (gpio_num_t)config->i2c_scl_pin,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = PHASE2_I2C_FREQ_HZ,
      .clk_flags = 0,
  };

  ESP_LOGI(TAG, "I2C init: port=%d SDA=GPIO%d SCL=GPIO%d freq=%dHz",
           PHASE2_I2C_PORT, config->i2c_sda_pin, config->i2c_scl_pin,
           PHASE2_I2C_FREQ_HZ);

  esp_err_t err = i2c_param_config(PHASE2_I2C_PORT, &bus_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "FAIL: i2c_param_config: %s", esp_err_to_name(err));
    return false;
  }

  err = i2c_driver_install(PHASE2_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "FAIL: i2c_driver_install: %s", esp_err_to_name(err));
    return false;
  }
  s_i2c_installed = true;

  /* Let the open-drain lines settle before checking their idle levels. */
  vTaskDelay(pdMS_TO_TICKS(10));

  const int sda_level = gpio_get_level((gpio_num_t)config->i2c_sda_pin);
  const int scl_level = gpio_get_level((gpio_num_t)config->i2c_scl_pin);
  ESP_LOGI(TAG, "I2C idle: SDA(GPIO%d)=%d SCL(GPIO%d)=%d", config->i2c_sda_pin,
           sda_level, config->i2c_scl_pin, scl_level);

  if ((sda_level == 0) || (scl_level == 0)) {
    ESP_LOGE(TAG, "FAIL: I2C bus is not idle HIGH/HIGH");
    return false;
  }
  return true;
}

static bool phase2_detect_mpu(void) {
  static const uint8_t addresses[] = {MPU6050_ADDRESS_AD0_LOW,
                                      MPU6050_ADDRESS_AD0_HIGH};
  unsigned int device_count = 0;

  ESP_LOGI(TAG, "Probing MPU6050 addresses 0x68 and 0x69 first...");
  for (size_t index = 0; index < sizeof(addresses) / sizeof(addresses[0]);
       ++index) {
    const esp_err_t err = phase2_probe_address(addresses[index]);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "ACK at 0x%02X", addresses[index]);
      s_mpu_address = addresses[index];
      device_count++;
    } else {
      phase2_log_i2c_error("Probe", addresses[index], err);
      if ((err == ESP_ERR_TIMEOUT) || (err == ESP_ERR_INVALID_STATE)) {
        return false;
      }
    }
  }

  ESP_LOGI(TAG, "Scanning remaining I2C addresses 0x08-0x77...");
  for (uint8_t address = 0x08; address <= 0x77; ++address) {
    if ((address == MPU6050_ADDRESS_AD0_LOW) ||
        (address == MPU6050_ADDRESS_AD0_HIGH)) {
      continue;
    }

    const esp_err_t err = phase2_probe_address(address);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "I2C device found at 0x%02X", address);
      device_count++;
    } else if ((err == ESP_ERR_TIMEOUT) || (err == ESP_ERR_INVALID_STATE)) {
      phase2_log_i2c_error("Scan", address, err);
      return false;
    }
  }

  ESP_LOGI(TAG, "I2C scan complete: %u device(s)", device_count);
  if (s_mpu_address == 0) {
    ESP_LOGE(TAG, "FAIL: MPU6050 did not ACK at 0x68 or 0x69");
    return false;
  }
  return true;
}

static bool phase2_configure_mpu(void) {
  uint8_t who_am_i = 0;
  esp_err_t err = phase2_read_register(MPU6050_REG_WHO_AM_I, &who_am_i);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "FAIL: WHO_AM_I read: %s", esp_err_to_name(err));
    return false;
  }

  ESP_LOGI(TAG, "WHO_AM_I register 0x75 = 0x%02X", who_am_i);
  if (who_am_i != MPU6050_EXPECTED_WHO_AM_I) {
    ESP_LOGE(TAG, "FAIL: expected WHO_AM_I=0x%02X", MPU6050_EXPECTED_WHO_AM_I);
    return false;
  }

  err = phase2_write_register(MPU6050_REG_PWR_MGMT_1, MPU6050_PWR_WAKEUP);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "FAIL: wake-up: %s", esp_err_to_name(err));
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(100));

  if ((phase2_write_register(MPU6050_REG_CONFIG, MPU6050_DLPF_CONFIG) !=
       ESP_OK) ||
      (phase2_write_register(MPU6050_REG_SMPLRT_DIV, MPU6050_SAMPLE_RATE_DIV) !=
       ESP_OK) ||
      (phase2_write_register(MPU6050_REG_GYRO_CONFIG,
                             MPU6050_GYRO_CONFIG_250DPS) != ESP_OK) ||
      (phase2_write_register(MPU6050_REG_ACCEL_CONFIG,
                             MPU6050_ACCEL_CONFIG_4G) != ESP_OK)) {
    ESP_LOGE(TAG, "FAIL: MPU6050 configuration write failed");
    return false;
  }

  if (!phase2_verify_register(MPU6050_REG_CONFIG, MPU6050_DLPF_CONFIG,
                              "CONFIG") ||
      !phase2_verify_register(MPU6050_REG_SMPLRT_DIV,
                              MPU6050_SAMPLE_RATE_DIV, "SMPLRT_DIV") ||
      !phase2_verify_register(MPU6050_REG_GYRO_CONFIG,
                              MPU6050_GYRO_CONFIG_250DPS, "GYRO_CONFIG") ||
      !phase2_verify_register(MPU6050_REG_ACCEL_CONFIG,
                              MPU6050_ACCEL_CONFIG_4G, "ACCEL_CONFIG")) {
    return false;
  }

  ESP_LOGI(TAG, "MPU configured: 125Hz, accel +/-4g, gyro +/-250dps");
  return true;
}

static void IRAM_ATTR phase2_mpu_isr(void *arg) {
  (void)arg;
  BaseType_t higher_priority_task_woken = pdFALSE;
  if (s_mpu_task_handle != NULL) {
    vTaskNotifyGiveFromISR(s_mpu_task_handle, &higher_priority_task_woken);
  }
  if (higher_priority_task_woken == pdTRUE) {
    portYIELD_FROM_ISR();
  }
}

static bool phase2_interrupt_init(int int_pin) {
  const gpio_config_t io_config = {
      .pin_bit_mask = 1ULL << int_pin,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_POSEDGE,
  };

  esp_err_t err = gpio_config(&io_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "FAIL: GPIO%d config: %s", int_pin, esp_err_to_name(err));
    return false;
  }

  err = gpio_install_isr_service(0);
  if (err == ESP_OK) {
    s_isr_service_owned = true;
  } else if (err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "FAIL: gpio_install_isr_service: %s", esp_err_to_name(err));
    return false;
  }

  err = gpio_isr_handler_add((gpio_num_t)int_pin, phase2_mpu_isr, NULL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "FAIL: GPIO%d ISR handler: %s", int_pin,
             esp_err_to_name(err));
    return false;
  }
  s_isr_handler_added = true;
  ESP_LOGI(TAG, "MPU6050 DATA_READY interrupt configured on GPIO%d", int_pin);
  return true;
}

static void phase2_read_task(void *arg) {
  (void)arg;
  uint32_t sample_count = 0;

  while (true) {
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(PHASE2_INTERRUPT_TIMEOUT_MS)) ==
        0) {
      ESP_LOGE(TAG, "FAIL: no DATA_READY interrupt on GPIO%d for %dms",
               s_mpu_int_pin, PHASE2_INTERRUPT_TIMEOUT_MS);
      continue;
    }

    uint8_t int_status = 0;
    esp_err_t err = phase2_read_register(MPU6050_REG_INT_STATUS, &int_status);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "FAIL: INT_STATUS read: %s", esp_err_to_name(err));
      continue;
    }
    if ((int_status & MPU6050_DATA_READY_ENABLE) == 0) {
      ESP_LOGW(TAG, "GPIO%d interrupt without DATA_READY status (0x%02X)",
               s_mpu_int_pin, int_status);
      continue;
    }

    int16_t ax_raw = 0;
    int16_t ay_raw = 0;
    int16_t az_raw = 0;
    int16_t gx_raw = 0;
    int16_t gy_raw = 0;
    int16_t gz_raw = 0;
    err = phase2_read_motion(&ax_raw, &ay_raw, &az_raw, &gx_raw, &gy_raw,
                             &gz_raw);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "FAIL: motion read: %s", esp_err_to_name(err));
      continue;
    }

    sample_count++;
    if ((sample_count % PHASE2_LOG_EVERY_SAMPLES) == 0U) {
      const float ax = (float)ax_raw / MPU6050_ACCEL_LSB_PER_G;
      const float ay = (float)ay_raw / MPU6050_ACCEL_LSB_PER_G;
      const float az = (float)az_raw / MPU6050_ACCEL_LSB_PER_G;
      const float gx = (float)gx_raw / MPU6050_GYRO_LSB_PER_DPS;
      const float gy = (float)gy_raw / MPU6050_GYRO_LSB_PER_DPS;
      const float gz = (float)gz_raw / MPU6050_GYRO_LSB_PER_DPS;
      const float accel_magnitude = sqrtf(ax * ax + ay * ay + az * az);
      const float gyro_magnitude = sqrtf(gx * gx + gy * gy + gz * gz);

      ESP_LOGI(TAG,
               "PASS #%lu ACC[g] X=%+.3f Y=%+.3f Z=%+.3f | A=%.3f "
               "GYRO[dps] X=%+.1f Y=%+.1f Z=%+.1f | G=%.1f",
               (unsigned long)sample_count, ax, ay, az, accel_magnitude, gx, gy,
               gz, gyro_magnitude);
    }
  }
}

static void phase2_cleanup_failed_start(void) {
  if (s_isr_handler_added) {
    (void)gpio_isr_handler_remove((gpio_num_t)s_mpu_int_pin);
    s_isr_handler_added = false;
  }
  if (s_isr_service_owned) {
    gpio_uninstall_isr_service();
    s_isr_service_owned = false;
  }
  if (s_mpu_task_handle != NULL) {
    vTaskDelete(s_mpu_task_handle);
    s_mpu_task_handle = NULL;
  }
  if (s_i2c_installed) {
    (void)i2c_driver_delete(PHASE2_I2C_PORT);
    s_i2c_installed = false;
  }
}

bool phase2_mpu6050_test_start(const hardware_test_config_t *config) {
  if ((config == NULL) || (config->mpu_int_pin < 0)) {
    ESP_LOGE(TAG, "FAIL: invalid Phase 2 configuration");
    return false;
  }

  s_mpu_address = 0;
  s_mpu_int_pin = config->mpu_int_pin;
  ESP_LOGI(TAG, "================================");
  ESP_LOGI(TAG, "PHASE 2 - MPU6050 INTERRUPT TEST");
  ESP_LOGI(TAG, "SDA=GPIO%d SCL=GPIO%d INT=GPIO%d", config->i2c_sda_pin,
           config->i2c_scl_pin, config->mpu_int_pin);
  ESP_LOGI(TAG, "================================");

  if (!phase2_i2c_init(config) || !phase2_detect_mpu() ||
      !phase2_configure_mpu()) {
    phase2_cleanup_failed_start();
    return false;
  }

  BaseType_t task_result = xTaskCreate(phase2_read_task, "phase2_mpu", 4096,
                                       NULL, 10, &s_mpu_task_handle);
  if (task_result != pdPASS) {
    ESP_LOGE(TAG, "FAIL: cannot create MPU6050 task");
    phase2_cleanup_failed_start();
    return false;
  }

  if (!phase2_interrupt_init(config->mpu_int_pin) ||
      (phase2_write_register(MPU6050_REG_INT_PIN_CFG, MPU6050_INT_PIN_CONFIG) !=
       ESP_OK) ||
      (phase2_write_register(MPU6050_REG_INT_ENABLE,
                             MPU6050_DATA_READY_ENABLE) != ESP_OK)) {
    ESP_LOGE(TAG, "FAIL: MPU6050 interrupt setup failed");
    phase2_cleanup_failed_start();
    return false;
  }

  ESP_LOGI(TAG, "PASS: bus_address=0x%02X WHO_AM_I=0x68", s_mpu_address);
  ESP_LOGI(TAG, "PASS: continuous DATA_READY monitoring started");
  return true;
}
