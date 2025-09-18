idf_component_register(
    SRCS "ch422g.c"
    INCLUDE_DIRS "include"
    REQUIRES driver i2c
    PRIV_REQUIRES esp_timer
)
