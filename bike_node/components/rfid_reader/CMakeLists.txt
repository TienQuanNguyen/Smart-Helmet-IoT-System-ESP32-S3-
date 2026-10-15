idf_component_register(
    SRCS "rfid_reader.c"
    INCLUDE_DIRS "include"
    REQUIRES log rc522 esp_driver_spi esp_driver_gpio
)
