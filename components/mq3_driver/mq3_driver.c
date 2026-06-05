#include "mq3_driver.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_config.h"
#include "system_config.h"

#define MQ3_ADC_MAX_RAW_VALUE 4095.0f
#define MQ3_ADC_REFERENCE_VOLTAGE 3.30f
#define MQ3_SAMPLE_DELAY_MS 10

static const char *TAG = "mq3_driver";

static adc_oneshot_unit_handle_t s_adc_handle;
static int s_adc_channel = MQ3_DEFAULT_ADC_CHANNEL;
static int s_power_en_pin = MQ3_DEFAULT_POWER_EN_PIN;
static float s_threshold_voltage = MQ3_DEFAULT_ALCOHOL_THRESHOLD_V;
static bool s_initialized;
static bool s_powered;
static bool s_mock_enabled;
static uint16_t s_mock_raw = 1450;

static bool mq3_check_err(esp_err_t err, const char *action) {
  if (err == ESP_OK) {
    return true;
  }

  ESP_LOGE(TAG, "%s failed: %s", action, esp_err_to_name(err));
  return false;
}

static bool mq3_is_valid_config(const mq3_config_t *config) {
  return (config != NULL) && (config->adc_unit >= 1) &&
         (config->adc_unit <= 2) && (config->adc_channel >= 0) &&
         (config->adc_channel <= ADC_CHANNEL_10) &&
         (config->adc_atten >= ADC_ATTEN_DB_0) &&
         (config->adc_atten <= ADC_ATTEN_DB_12) &&
         (config->power_en_pin >= 0) &&
         (config->alcohol_threshold_voltage > 0.0f);
}

static bool mq3_gpio_init(int power_en_pin) {
  gpio_config_t io_config = {
      .pin_bit_mask = 1ULL << power_en_pin,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };

  if (!mq3_check_err(gpio_config(&io_config), "gpio_config")) {
    return false;
  }

  gpio_set_level((gpio_num_t)power_en_pin, 0);
  return true;
}

static bool mq3_adc_init(const mq3_config_t *config) {
  adc_oneshot_unit_init_cfg_t unit_config = {
      .unit_id = (adc_unit_t)config->adc_unit,
  };

  if (!mq3_check_err(adc_oneshot_new_unit(&unit_config, &s_adc_handle),
                     "adc_oneshot_new_unit")) {
    return false;
  }

  adc_oneshot_chan_cfg_t channel_config = {
      .atten = (adc_atten_t)config->adc_atten,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };

  return mq3_check_err(
      adc_oneshot_config_channel(
          s_adc_handle, (adc_channel_t)config->adc_channel, &channel_config),
      "adc_oneshot_config_channel");
}

bool mq3_init(void) {
  const mq3_config_t default_config = {
      .adc_unit = MQ3_DEFAULT_ADC_UNIT,
      .adc_channel = MQ3_DEFAULT_ADC_CHANNEL,
      .adc_atten = MQ3_DEFAULT_ATTEN,
      .power_en_pin = PIN_MQ3_POWER_EN,
      .alcohol_threshold_voltage = MQ3_ALCOHOL_THRESHOLD_VOLTAGE,
      .mock_enabled = USE_MOCK_SENSOR_DATA != 0,
  };

  return mq3_init_with_config(&default_config);
}

bool mq3_init_with_config(const mq3_config_t *config) {
  if (s_initialized) {
    return true;
  }

  if (!mq3_is_valid_config(config)) {
    ESP_LOGE(TAG, "Invalid MQ-3 config");
    return false;
  }

  s_adc_channel = config->adc_channel;
  s_power_en_pin = config->power_en_pin;
  s_threshold_voltage = config->alcohol_threshold_voltage;
  s_mock_enabled = config->mock_enabled;

  if (s_mock_enabled) {
    s_initialized = true;
    ESP_LOGW(TAG, "MQ-3 mock mode enabled");
    return true;
  }

  if (!mq3_gpio_init(config->power_en_pin) || !mq3_adc_init(config)) {
    return false;
  }

  s_initialized = true;
  ESP_LOGI(TAG, "MQ-3 initialized on ADC%d channel %d", config->adc_unit,
           config->adc_channel);

  return true;
}

void mq3_power_on(void) {
  if (!s_initialized) {
    ESP_LOGE(TAG, "MQ-3 is not initialized");
    return;
  }

  if (!s_mock_enabled) {
    gpio_set_level((gpio_num_t)s_power_en_pin, 1);
  }
  s_powered = true;
}

void mq3_power_off(void) {
  if (!s_initialized) {
    return;
  }

  if (!s_mock_enabled) {
    gpio_set_level((gpio_num_t)s_power_en_pin, 0);
  }
  s_powered = false;
}

bool mq3_read_raw(uint16_t *adc_raw) {
  if (adc_raw == NULL) {
    ESP_LOGE(TAG, "ADC raw output is NULL");
    return false;
  }

  if (!s_initialized) {
    ESP_LOGE(TAG, "MQ-3 is not initialized");
    return false;
  }

  if (s_mock_enabled) {
    s_mock_raw += 17;
    if (s_mock_raw > 1750) {
      s_mock_raw = 1450;
    }
    *adc_raw = s_mock_raw;
    return true;
  }

  if (!s_powered) {
    ESP_LOGE(TAG, "MQ-3 read requested while sensor power is off");
    return false;
  }

  int raw = 0;
  if (!mq3_check_err(
          adc_oneshot_read(s_adc_handle, (adc_channel_t)s_adc_channel, &raw),
          "adc_oneshot_read")) {
    return false;
  }

  *adc_raw = (uint16_t)raw;
  return true;
}

bool mq3_read_voltage(float *voltage) {
  if (voltage == NULL) {
    ESP_LOGE(TAG, "Voltage output is NULL");
    return false;
  }

  uint16_t adc_raw = 0;
  if (!mq3_read_raw(&adc_raw)) {
    return false;
  }

  *voltage =
      ((float)adc_raw / MQ3_ADC_MAX_RAW_VALUE) * MQ3_ADC_REFERENCE_VOLTAGE;
  return true;
}

bool mq3_sample_average(float *avg_voltage, uint16_t sample_count) {
  if ((avg_voltage == NULL) || (sample_count == 0)) {
    ESP_LOGE(TAG, "Invalid MQ-3 average input");
    return false;
  }

  float sum = 0.0f;
  for (uint16_t i = 0; i < sample_count; ++i) {
    float voltage = 0.0f;
    if (!mq3_read_voltage(&voltage)) {
      return false;
    }
    sum += voltage;
    vTaskDelay(pdMS_TO_TICKS(MQ3_SAMPLE_DELAY_MS));
  }

  *avg_voltage = sum / (float)sample_count;
  return true;
}

bool mq3_is_alcohol_detected(float voltage) {
  return voltage >= s_threshold_voltage;
}
