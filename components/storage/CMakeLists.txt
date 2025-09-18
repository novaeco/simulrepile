idf_component_register(
    SRCS "sd_spi.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_vfs fatfs sdmmc ch422g
)
