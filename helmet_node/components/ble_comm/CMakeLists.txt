idf_component_register(
    SRCS "ble_comm.c"
    INCLUDE_DIRS "include" "../../../shared/protocol"
    REQUIRES system_state event_manager log bt nvs_flash
)
