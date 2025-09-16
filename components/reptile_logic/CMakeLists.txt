idf_component_register(
    SRCS "reptile_logic.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash gpio config
    PRIV_REQUIRES sensors sd
)
