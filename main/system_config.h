#pragma once

#define SYSTEM_NAME                    "SMART_HELMET_IOT"

#define USE_MOCK_SENSOR_DATA            1
#define SENSOR_MOCK_MODE                USE_MOCK_SENSOR_DATA

#define IMU_SAMPLE_PERIOD_MS            20
#define GPS_SAMPLE_PERIOD_MS            1000
#define POWER_TASK_PERIOD_MS            1000

#define PHASE2_DRIVER_LOG_PERIOD_MS     1000
#define GPS_READ_TIMEOUT_MS             200
#define GPS_RAW_LINE_BUFFER_SIZE        128

#define MQ3_WARMUP_TIME_MS              60000
#define MQ3_SAMPLE_COUNT                32
#define MQ3_TEST_SAMPLE_COUNT           4
#define MQ3_ALCOHOL_THRESHOLD_VOLTAGE   1.80f

#define ACCIDENT_ACCEL_THRESHOLD_G      3.0f
#define FALL_TILT_THRESHOLD_DEG         60.0f
#define FALL_CONFIRM_TIME_MS            2000

#define WIFI_RETRY_INTERVAL_MS          10000
#define CLOUD_SEND_TIMEOUT_MS           5000
