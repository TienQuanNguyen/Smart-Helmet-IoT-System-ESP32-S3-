#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef struct {
  int gpio;
  int active_level;
  int inactive_level;
} relay_driver_config_t;

esp_err_t relay_driver_init(const relay_driver_config_t *config);
esp_err_t relay_driver_set_ignition_allowed(bool allowed);
bool relay_driver_is_ignition_allowed(void);
