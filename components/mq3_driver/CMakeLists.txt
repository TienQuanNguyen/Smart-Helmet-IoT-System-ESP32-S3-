idf_component_register(
    SRCS "mq3_driver.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../../main"
    REQUIRES driver esp_adc
)
