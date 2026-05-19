#pragma once

#define SYSTEM_NAME                    "SMART_HELMET_IOT"

#define IMU_SAMPLE_PERIOD_MS            20
#define GPS_SAMPLE_PERIOD_MS            1000
#define POWER_TASK_PERIOD_MS            1000

#define MQ3_WARMUP_TIME_MS              60000
#define MQ3_SAMPLE_COUNT                32

#define ACCIDENT_ACCEL_THRESHOLD_G      3.0f
#define FALL_TILT_THRESHOLD_DEG         60.0f
#define FALL_CONFIRM_TIME_MS            2000

#define WIFI_RETRY_INTERVAL_MS          10000
#define CLOUD_SEND_TIMEOUT_MS           5000