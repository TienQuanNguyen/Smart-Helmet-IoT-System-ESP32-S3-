#include "hardware_test_internal.h"

#include "accident_detector.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mpu6050_driver.h"

#define ACCIDENT_TEST_SAMPLE_PERIOD_MS 20U
#define ACCIDENT_TEST_LOG_PERIOD_MS 500U

static const char *TAG = "test_phase5";

static bool phase5_detector_preflight(void) {
  accident_result_t result = {0};

  if (!accident_detector_init()) {
    ESP_LOGE(TAG, "PREFLIGHT FAIL: detector initialization");
    return false;
  }

  if (!accident_detector_update(0.0f, 0.0f, 1.0f, 0U, &result) ||
      result.impact_detected || result.fall_confirmed) {
    ESP_LOGE(TAG, "PREFLIGHT FAIL: upright sample must remain normal");
    return false;
  }
  ESP_LOGI(TAG, "PREFLIGHT PASS: upright sample remains normal");

  if (!accident_detector_update(4.0f, 0.0f, 0.0f, 20U, &result) ||
      !result.impact_detected) {
    ESP_LOGE(TAG, "PREFLIGHT FAIL: threshold-crossing impact not detected");
    return false;
  }
  ESP_LOGI(TAG, "PREFLIGHT PASS: impact detected");

  accident_detector_reset();
  if (!accident_detector_update(1.0f, 0.0f, 0.1f, 1000U, &result) ||
      result.fall_confirmed ||
      !accident_detector_update(1.0f, 0.0f, 0.1f, 3000U, &result) ||
      !result.fall_confirmed) {
    ESP_LOGE(TAG, "PREFLIGHT FAIL: sustained tilt confirmation");
    return false;
  }
  ESP_LOGI(TAG, "PREFLIGHT PASS: sustained tilt confirmed");

  accident_detector_reset();
  if (!accident_detector_update(4.0f, 0.0f, 0.0f, 4000U, &result) ||
      !result.impact_detected) {
    ESP_LOGE(TAG, "PREFLIGHT FAIL: second incident after reset");
    return false;
  }
  ESP_LOGI(TAG, "PREFLIGHT PASS: second incident after reset");

  accident_detector_reset();
  return true;
}

static void phase5_monitor_task(void *arg) {
  (void)arg;
  uint32_t elapsed_ms = 0;

  while (true) {
    mpu6050_accel_t accel = {0};
    accident_result_t result = {0};
    if (!mpu6050_read_accel_g(&accel) ||
        !accident_detector_update(accel.x, accel.y, accel.z, elapsed_ms,
                                  &result)) {
      ESP_LOGE(TAG, "FAIL: sensor/detector update error");
    } else {
      if ((elapsed_ms % ACCIDENT_TEST_LOG_PERIOD_MS) == 0) {
        ESP_LOGI(TAG, "sample: magnitude=%.2fg tilt=%.1fdeg",
                 result.accel_magnitude_g, result.tilt_angle_deg);
      }
      if (result.impact_detected) {
        ESP_LOGW(TAG, "PASS IMPACT: magnitude=%.2fg", result.accel_magnitude_g);
      }
      if (result.fall_confirmed) {
        ESP_LOGW(TAG, "PASS FALL: tilt=%.1fdeg", result.tilt_angle_deg);
        accident_detector_reset();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(ACCIDENT_TEST_SAMPLE_PERIOD_MS));
    elapsed_ms += ACCIDENT_TEST_SAMPLE_PERIOD_MS;
  }
}

bool phase5_accident_test_start(const hardware_test_config_t *config) {
  if ((config == NULL) || !phase5_detector_preflight()) {
    ESP_LOGE(TAG, "FAIL: accident detector preflight failed");
    return false;
  }

  const mpu6050_config_t mpu_config = {
      .i2c_port = MPU6050_DEFAULT_I2C_PORT,
      .sda_pin = config->i2c_sda_pin,
      .scl_pin = config->i2c_scl_pin,
      .clock_speed_hz = MPU6050_DEFAULT_I2C_CLOCK_HZ,
      .device_address = MPU6050_DEFAULT_ADDRESS,
      .accel_range = MPU6050_DEFAULT_ACCEL_RANGE,
      .gyro_range = MPU6050_DEFAULT_GYRO_RANGE,
      .dlpf = MPU6050_DEFAULT_DLPF,
      .sample_rate_divider = MPU6050_DEFAULT_SAMPLE_DIVIDER,
      .mock_enabled = false,
  };

  if (!mpu6050_init_with_config(&mpu_config) || !accident_detector_init()) {
    ESP_LOGE(TAG, "FAIL: MPU6050 or detector initialization failed");
    return false;
  }

  ESP_LOGI(TAG, "Use controlled motion only; do not drop the assembled unit");
  return xTaskCreate(phase5_monitor_task, "phase5_test", 4096, NULL, 5,
                     NULL) == pdPASS;
}
