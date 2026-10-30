idf_component_register(
    SRCS "power_manager.c" "power_model.c" "power_json.c"
    INCLUDE_DIRS "include"
    REQUIRES event_manager freertos esp_timer log json
)
