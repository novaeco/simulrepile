idf_component_register(
    SRCS "sd_spi.c"
    INCLUDE_DIRS "include"
    REQUIRES driver vfs fatfs sdmmc ch422g
    PRIV_REQUIRES esp_timer
)
