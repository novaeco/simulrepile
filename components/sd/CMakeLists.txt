idf_component_register(
    SRCS "sd_spi.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        esp_timer
    REQUIRES
        esp_driver_spi
        esp_driver_gpio
        sdmmc
        fatfs
        vfs
        freertos
        driver
        esp_system
        log
)
