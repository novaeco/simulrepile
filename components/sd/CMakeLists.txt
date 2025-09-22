idf_component_register(
    SRCS "sd_spi.c"
    INCLUDE_DIRS "include"
    REQUIRES driver vfs fatfs sdmmc esp_driver_sdspi
)
