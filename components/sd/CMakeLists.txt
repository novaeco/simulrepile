idf_component_register(
    SRCS "sd_spi.c"
    INCLUDE_DIRS "include"
    REQUIRES sdmmc esp_driver_spi fatfs wear_levelling vfs esp_partition log
)
