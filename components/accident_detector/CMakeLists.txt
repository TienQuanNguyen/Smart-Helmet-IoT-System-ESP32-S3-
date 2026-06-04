idf_component_register(
    SRCS "accident_detector.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../../main"
    REQUIRES log
)

target_link_libraries(${COMPONENT_LIB} INTERFACE m)
