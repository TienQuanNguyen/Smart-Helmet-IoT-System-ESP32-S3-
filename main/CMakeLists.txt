idf_component_register(
    SRCS "main.c" "app_test.c"
    INCLUDE_DIRS "."
    REQUIRES mpu6050_driver gps_driver mq3_driver accident_detector event_manager system_state
)
