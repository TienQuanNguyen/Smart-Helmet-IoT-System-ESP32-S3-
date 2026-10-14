idf_component_register(
    SRCS "main.c" "bike_app.c"
    INCLUDE_DIRS "."
    REQUIRES ble_comm bike_state esp_timer hardware_tests relay_driver rfid_reader
)
