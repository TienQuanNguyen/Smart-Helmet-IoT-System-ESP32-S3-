#include "hardware_test_internal.h"

#include <stdint.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mq3_driver.h"

static const char *TAG = "test_phase4";
static hardware_test_config_t s_config;

static bool phase4_adc_mapping_preflight(
    const hardware_test_config_t *config) {
  adc_unit_t gpio_unit = ADC_UNIT_1;
  adc_channel_t gpio_channel = ADC_CHANNEL_0;
  esp_err_t err = adc_oneshot_io_to_channel(config->mq3_adc_pin, &gpio_unit,
                                             &gpio_channel);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "PREFLIGHT FAIL: GPIO%d is not a valid ADC input: %s",
             config->mq3_adc_pin, esp_err_to_name(err));
    return false;
  }

  const adc_unit_t configured_unit = MQ3_DEFAULT_ADC_UNIT;
  const adc_channel_t configured_channel = MQ3_DEFAULT_ADC_CHANNEL;
  int configured_gpio = -1;
  err = adc_oneshot_channel_to_io(configured_unit, configured_channel,
                                  &configured_gpio);
  if (err != ESP_OK) {
    ESP_LOGE(TAG,
             "PREFLIGHT FAIL: configured ADC unit=%d channel=%d is invalid: %s",
             configured_unit, configured_channel, esp_err_to_name(err));
    return false;
  }

  if ((gpio_unit != configured_unit) ||
      (gpio_channel != configured_channel) ||
      (configured_gpio != config->mq3_adc_pin)) {
    ESP_LOGE(TAG,
             "PREFLIGHT FAIL: GPIO%d maps to ADC%d channel %d, but driver "
             "selects ADC%d channel %d (GPIO%d)",
             config->mq3_adc_pin, gpio_unit + 1, gpio_channel,
             configured_unit + 1, configured_channel, configured_gpio);
    return false;
  }

  ESP_LOGI(TAG, "PREFLIGHT PASS: GPIO%d -> ADC%d channel %d",
           config->mq3_adc_pin, gpio_unit + 1, gpio_channel);
  return true;
}

static void phase4_read_task(void *arg) {
  (void)arg;
  mq3_power_on();

  uint32_t remaining_ms = s_config.mq3_warmup_time_ms;
  while (remaining_ms > 0) {
    ESP_LOGI(TAG, "Warming up MQ-3: %lus remaining",
             (unsigned long)((remaining_ms + 999U) / 1000U));
    uint32_t delay_ms = remaining_ms > 5000U ? 5000U : remaining_ms;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    remaining_ms -= delay_ms;
  }

  ESP_LOGI(TAG, "Warm-up complete; starting ADC samples");
  while (true) {
    uint16_t raw = 0;
    float average_voltage = 0.0f;
    if (mq3_read_raw(&raw) &&
        mq3_sample_average(&average_voltage, s_config.mq3_sample_count)) {
      ESP_LOGI(TAG, "SAMPLE: raw=%u ADC-node average=%.3fV alcohol=%d", raw,
               average_voltage, mq3_is_alcohol_detected(average_voltage));
    } else {
      ESP_LOGE(TAG, "FAIL: MQ-3 ADC read error");
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

bool phase4_mq3_test_start(const hardware_test_config_t *config) {
  if ((config == NULL) || !phase4_adc_mapping_preflight(config)) {
    ESP_LOGE(TAG, "FAIL: MQ-3 ADC mapping preflight failed");
    return false;
  }

  const mq3_config_t mq3_config = {
      .adc_pin = config->mq3_adc_pin,
      .adc_unit = MQ3_DEFAULT_ADC_UNIT,
      .adc_channel = MQ3_DEFAULT_ADC_CHANNEL,
      .adc_atten = MQ3_DEFAULT_ATTEN,
      .power_en_pin = config->mq3_power_en_pin,
      .alcohol_threshold_voltage = config->mq3_alcohol_threshold_voltage,
      .mock_enabled = false,
  };

  if ((config->mq3_sample_count == 0) || !mq3_init_with_config(&mq3_config)) {
    ESP_LOGE(TAG, "FAIL: MQ-3 initialization failed");
    return false;
  }

  float zero_voltage = 0.0f;
  float mid_voltage = 0.0f;
  float high_voltage = 0.0f;
  if (!mq3_convert_raw_to_voltage(0U, &zero_voltage) ||
      !mq3_convert_raw_to_voltage(2048U, &mid_voltage) ||
      !mq3_convert_raw_to_voltage(4095U, &high_voltage) ||
      (zero_voltage > mid_voltage) || (mid_voltage >= high_voltage)) {
    ESP_LOGE(TAG,
             "FAIL: calibrated conversion zero=%.3fV mid=%.3fV high=%.3fV",
             zero_voltage, mid_voltage, high_voltage);
    return false;
  }
  ESP_LOGI(TAG,
           "PREFLIGHT PASS: calibrated conversion zero=%.3fV mid=%.3fV "
           "high=%.3fV",
           zero_voltage, mid_voltage, high_voltage);

  s_config = *config;
  ESP_LOGW(TAG,
           "Logged voltage is at GPIO%d after the divider; verify it never "
           "exceeds 3.3V",
           config->mq3_adc_pin);
  return xTaskCreate(phase4_read_task, "phase4_test", 4096, NULL, 4,
                     NULL) == pdPASS;
}
