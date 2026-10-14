#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  int phase;
  int i2c_sda_pin;
  int i2c_scl_pin;
  int mpu_int_pin;
  int gps_rx_pin;
  int gps_tx_pin;
  int mq3_adc_pin;
  int mq3_power_en_pin;
  uint32_t mq3_warmup_time_ms;
  uint16_t mq3_sample_count;
  float mq3_alcohol_threshold_voltage;
} hardware_test_config_t;

bool hardware_test_start(const hardware_test_config_t *config);
