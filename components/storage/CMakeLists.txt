idf_component_register(
    SRCS "sd_spi.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES driver vfs fatfs sdmmc ch422g esp_driver_sdspi
    PRIV_REQUIRES esp_mm esp_timer
)
