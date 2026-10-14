#include "relay_driver.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "relay_driver";

static relay_driver_config_t s_config;
static bool s_initialized;
static bool s_ignition_allowed;

static bool relay_driver_is_valid_level(int level) {
  return (level == 0) || (level == 1);
}

esp_err_t relay_driver_init(const relay_driver_config_t *config) {
  ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                      "Relay config is NULL");
  ESP_RETURN_ON_FALSE(relay_driver_is_valid_level(config->active_level),
                      ESP_ERR_INVALID_ARG, TAG, "Invalid active level");
  ESP_RETURN_ON_FALSE(relay_driver_is_valid_level(config->inactive_level),
                      ESP_ERR_INVALID_ARG, TAG, "Invalid inactive level");
  ESP_RETURN_ON_FALSE(config->active_level != config->inactive_level,
                      ESP_ERR_INVALID_ARG, TAG,
                      "Active and inactive levels must differ");

  s_config = *config;
  ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(s_config.gpio),
                      ESP_ERR_INVALID_ARG, TAG, "Invalid relay GPIO");
  ESP_RETURN_ON_ERROR(gpio_set_level(s_config.gpio, s_config.inactive_level),
                      TAG, "Failed to preload inactive relay level");

  const gpio_config_t io_config = {
      .pin_bit_mask = (1ULL << s_config.gpio),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };

  ESP_RETURN_ON_ERROR(gpio_config(&io_config), TAG,
                      "Failed to configure relay GPIO");
  ESP_RETURN_ON_ERROR(
      gpio_set_level((gpio_num_t)s_config.gpio, s_config.inactive_level), TAG,
      "Failed to set relay inactive level");

  s_ignition_allowed = false;
  s_initialized = true;
  ESP_LOGI(TAG, "Relay initialized GPIO=%d active=%d inactive=%d",
           s_config.gpio, s_config.active_level, s_config.inactive_level);
  return ESP_OK;
}

esp_err_t relay_driver_set_ignition_allowed(bool allowed) {
  ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG,
                      "Relay driver is not initialized");
  if (allowed == s_ignition_allowed) return ESP_OK;

  const int level = allowed ? s_config.active_level : s_config.inactive_level;
  ESP_RETURN_ON_ERROR(gpio_set_level((gpio_num_t)s_config.gpio, level), TAG,
                      "Failed to update relay level");

  s_ignition_allowed = allowed;
  ESP_LOGI(TAG, "Ignition allowed=%d relay_level=%d", allowed ? 1 : 0, level);
  return ESP_OK;
}

bool relay_driver_is_ignition_allowed(void) { return s_ignition_allowed; }
