#pragma once

#include <stdbool.h>
#include <stdint.h>

void app_test_run_startup_checks(void);
bool app_test_mpu6050_once(void);
bool app_test_gps_once(void);
bool app_test_mq3_once(void);
bool app_test_get_mock_imu_sample(float *ax, float *ay, float *az,
                                  uint32_t elapsed_ms);
void app_test_publish_mock_events_if_needed(uint32_t elapsed_ms);
