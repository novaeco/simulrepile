idf_component_register(
    SRCS "real_mode.c" "sensors.c" "actuators.c" "dashboard.c" "logging.c"
    INCLUDE_DIRS "."
    REQUIRES storage
)
