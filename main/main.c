#include "accident_detector.h"
#include "app_test.h"
#include "esp_log.h"
#include "event_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gps_driver.h"
#include "mpu6050_driver.h"
#include "mq3_driver.h"
#include "pin_config.h"
#include "system_config.h"
#include "system_state.h"

#define SYSTEM_EVENT_WAIT_TIMEOUT_MS 200

static const char *TAG = SYSTEM_NAME;

static bool init_mpu6050_driver(void) {
  const mpu6050_config_t config = {
      .i2c_port = MPU6050_DEFAULT_I2C_PORT,
      .sda_pin = PIN_I2C_SDA,
      .scl_pin = PIN_I2C_SCL,
      .clock_speed_hz = MPU6050_DEFAULT_I2C_CLOCK_HZ,
      .device_address = MPU6050_DEFAULT_ADDRESS,
      .mock_enabled = USE_MOCK_SENSOR_DATA != 0,
  };

  if (!mpu6050_init_with_config(&config)) {
    ESP_LOGE(TAG, "MPU6050 init failed");
    return false;
  }

  uint8_t who_am_i = 0;
  if (mpu6050_read_who_am_i(&who_am_i)) {
    ESP_LOGI(TAG, "MPU6050 WHO_AM_I=0x%02X", who_am_i);
  }

  return true;
}

static bool read_accel_sample(uint32_t timestamp_ms, mpu6050_accel_t *accel) {
  if (accel == NULL) {
    return false;
  }

  if (USE_MOCK_SENSOR_DATA != 0) {
    return app_test_get_mock_imu_sample(&accel->x, &accel->y, &accel->z,
                                        timestamp_ms);
  }

  return mpu6050_read_accel_g(accel);
}

static void task_imu_monitor(void *arg) {
  (void)arg;

  uint32_t timestamp_ms = 0;
  bool impact_event_sent = false;
  bool fall_event_sent = false;

  while (true) {
    mpu6050_accel_t accel = {0};
    accident_result_t result = {0};

    if (read_accel_sample(timestamp_ms, &accel) &&
        accident_detector_update(accel.x, accel.y, accel.z, timestamp_ms,
                                 &result)) {
      if (result.impact_detected && !impact_event_sent) {
        ESP_LOGW(TAG, "IMU impact: %.2fg tilt=%.1fdeg",
                 result.accel_magnitude_g, result.tilt_angle_deg);
        event_manager_publish(SYSTEM_EVENT_IMU_IMPACT);
        impact_event_sent = true;
      }

      if (result.fall_confirmed && !fall_event_sent) {
        ESP_LOGW(TAG, "Fall confirmed: %.2fg tilt=%.1fdeg",
                 result.accel_magnitude_g, result.tilt_angle_deg);
        event_manager_publish(SYSTEM_EVENT_FALL_CONFIRMED);
        fall_event_sent = true;
      }
    } else {
      ESP_LOGW(TAG, "Failed to update accident detector");
    }

    vTaskDelay(pdMS_TO_TICKS(IMU_SAMPLE_PERIOD_MS));
    timestamp_ms += IMU_SAMPLE_PERIOD_MS;
  }
}

static void task_system_manager(void *arg) {
  (void)arg;

  uint32_t elapsed_ms = 0;

  while (true) {
    app_test_publish_mock_events_if_needed(elapsed_ms);

    system_event_t event = SYSTEM_EVENT_NONE;
    if (event_manager_wait(&event, SYSTEM_EVENT_WAIT_TIMEOUT_MS)) {
      ESP_LOGI(TAG, "System event: %s", system_event_to_string(event));
      system_state_handle_event(event);
      ESP_LOGI(TAG, "System state: %s",
               system_state_to_string(system_state_get()));
    }

    elapsed_ms += SYSTEM_EVENT_WAIT_TIMEOUT_MS;
  }
}

void app_main(void) {
  ESP_LOGI(TAG, "Booting %s", SYSTEM_NAME);
  ESP_LOGI(TAG, "Target board: ESP32-S3 DevKit");
  ESP_LOGI(TAG, "Sensor mock mode: %d", USE_MOCK_SENSOR_DATA);
  ESP_LOGI(TAG, "MPU6050 I2C pins: SDA=%d, SCL=%d", PIN_I2C_SDA, PIN_I2C_SCL);
  ESP_LOGI(TAG, "GPS UART pins: RX=%d, TX=%d", PIN_GPS_RX, PIN_GPS_TX);
  ESP_LOGI(TAG, "MQ-3 ADC channel=%d, power pin=%d", MQ3_DEFAULT_ADC_CHANNEL,
           PIN_MQ3_POWER_EN);

  if (!event_manager_init() || !system_state_init() ||
      !accident_detector_init() || !gps_init() || !mq3_init() ||
      !init_mpu6050_driver()) {
    ESP_LOGE(TAG, "System init failed");
    return;
  }

  app_test_run_startup_checks();

  BaseType_t imu_task_status =
      xTaskCreate(task_imu_monitor, "imu_monitor", 4096, NULL, 5, NULL);
  BaseType_t system_task_status =
      xTaskCreate(task_system_manager, "system_manager", 4096, NULL, 6, NULL);

  if ((imu_task_status != pdPASS) || (system_task_status != pdPASS)) {
    ESP_LOGE(TAG, "Failed to create system tasks");
  }
}
