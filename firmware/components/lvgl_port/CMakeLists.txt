idf_component_register(
    SRCS "lvgl_port.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "../../main/bsp"
    REQUIRES lvgl__lvgl espressif__esp_lvgl_port
)
