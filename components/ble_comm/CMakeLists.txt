idf_component_register(
    SRCS "ble_comm.c"
    INCLUDE_DIRS "include"
    REQUIRES system_state event_manager log
)
