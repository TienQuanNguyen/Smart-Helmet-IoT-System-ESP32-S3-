idf_component_register(
    SRCS
        "hardware_test_runner.c"
        "phase1_board_test.c"
        "phase2_mpu6050_test.c"
        "phase3_gps_test.c"
        "phase4_mq3_test.c"
        "phase5_accident_test.c"
        "phase6_fsm_test.c"
        "i2c_oled_test.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES
        esp_system
        spi_flash
        driver
        esp_adc
        freertos
        log
        mpu6050_driver
        gps_driver
        mq3_driver
        accident_detector
        event_manager
        system_state
)
