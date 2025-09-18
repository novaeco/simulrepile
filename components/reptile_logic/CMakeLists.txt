idf_component_register(
    SRCS "reptile_logic.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash gpio config regulations
    PRIV_REQUIRES sensors storage
)
