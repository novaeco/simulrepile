idf_component_register(
    SRCS "reptile_logic.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash gpio config species_db
    PRIV_REQUIRES sensors sd
)
