#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mpu6050_driver.h"
#include "pin_config.h"
#include "system_config.h"

static const char *TAG = SYSTEM_NAME;

static void mpu6050_read_task(void *arg) {
  (void)arg;

  mpu6050_raw_t raw = {0};

  while (true) {
    if (mpu6050_read_raw(&raw)) {
      ESP_LOGI(TAG, "MPU6050 raw accel=(%d, %d, %d) gyro=(%d, %d, %d)", raw.ax,
               raw.ay, raw.az, raw.gx, raw.gy, raw.gz);
    } else {
      ESP_LOGW(TAG, "Failed to read MPU6050 raw data");
    }

    vTaskDelay(pdMS_TO_TICKS(IMU_SAMPLE_PERIOD_MS));
  }
}

void app_main(void) {
  ESP_LOGI(TAG, "Booting %s", SYSTEM_NAME);
  ESP_LOGI(TAG, "MPU6050 I2C pins: SDA=%d, SCL=%d", PIN_I2C_SDA, PIN_I2C_SCL);

  const mpu6050_config_t mpu6050_config = {
      .i2c_port = MPU6050_DEFAULT_I2C_PORT,
      .sda_pin = PIN_I2C_SDA,
      .scl_pin = PIN_I2C_SCL,
      .clock_speed_hz = MPU6050_DEFAULT_I2C_CLOCK_HZ,
      .device_address = MPU6050_DEFAULT_ADDRESS,
  };

  if (!mpu6050_init_with_config(&mpu6050_config)) {
    ESP_LOGE(TAG, "MPU6050 init failed");
    return;
  }

  uint8_t who_am_i = 0;
  if (mpu6050_read_who_am_i(&who_am_i)) {
    ESP_LOGI(TAG, "MPU6050 WHO_AM_I=0x%02X", who_am_i);
  }

  BaseType_t task_status =
      xTaskCreate(mpu6050_read_task, "mpu6050_read", 4096, NULL, 5, NULL);

  if (task_status != pdPASS) {
    ESP_LOGE(TAG, "Failed to create MPU6050 read task");
  }
}
