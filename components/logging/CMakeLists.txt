idf_component_register(
    SRCS "logging.c" "logging_real.c"
    INCLUDE_DIRS "."
    REQUIRES storage reptile_logic lvgl env_control
)
