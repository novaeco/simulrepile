idf_component_register(
    SRCS "sd_spi.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_sdspi esp_driver_spi sdmmc fatfs wear_levelling vfs esp_partition log
)
