idf_component_register(
    SRCS "cloud_client.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client esp-tls freertos gps_driver wifi_manager system_state log nvs_flash json esp_hw_support
)
