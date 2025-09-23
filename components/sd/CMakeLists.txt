idf_component_register(
    SRCS "sd_spi.c"
    INCLUDE_DIRS "include"
    REQUIRES driver sdmmc fatfs wear_levelling vfs esp_partition log
)
