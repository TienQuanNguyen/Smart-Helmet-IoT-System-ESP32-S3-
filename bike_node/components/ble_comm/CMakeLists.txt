idf_component_register(
    SRCS "bike_ble_comm.c"
    INCLUDE_DIRS "include" "../../../shared/protocol"
    REQUIRES log bt nvs_flash esp_timer
)
