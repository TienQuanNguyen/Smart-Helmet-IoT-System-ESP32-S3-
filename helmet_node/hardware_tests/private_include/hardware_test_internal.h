#pragma once

#include <stdbool.h>

#include "hardware_test.h"

bool phase1_board_test_start(void);
bool phase2_mpu6050_test_start(const hardware_test_config_t *config);
bool phase3_gps_test_start(const hardware_test_config_t *config);
bool phase4_mq3_test_start(const hardware_test_config_t *config);
bool phase5_accident_test_start(const hardware_test_config_t *config);
bool phase6_fsm_test_start(void);
bool i2c_oled_test_start(const hardware_test_config_t *config);
