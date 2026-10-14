#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct {
  int phase;
  int relay_gpio;
  int relay_active_level;
  int oled_sda_gpio;
  int oled_scl_gpio;
  uint8_t oled_addr;
  int rfid_cs_gpio;
  int rfid_sck_gpio;
  int rfid_miso_gpio;
  int rfid_mosi_gpio;
  int rfid_rst_gpio;
} bike_hardware_test_config_t;

esp_err_t bike_hardware_test_start(const bike_hardware_test_config_t *config);
