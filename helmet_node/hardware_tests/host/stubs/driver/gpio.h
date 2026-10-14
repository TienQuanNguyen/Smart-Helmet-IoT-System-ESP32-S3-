#pragma once
#include <stdint.h>
#include "esp_err.h"
#define GPIO_MODE_INPUT 1
#define GPIO_MODE_OUTPUT 2
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en; } gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_set_level(int pin, int level);
esp_err_t gpio_set_direction(int pin, int direction);
int gpio_get_level(int pin);
