idf_component_register(
    SRCS "main.c" "reptile_game.c" "settings.c" "reptile_real.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        rgb_lcd_port
        lvgl
        lvgl_port
        touch
        esp_lcd
        fonts
        reptile_logic
        env_control
        config
        sd
        logging
        can
        gpio
    PRIV_REQUIRES
        image
        sensors
    WHOLE_ARCHIVE
)

if(TARGET lvgl__lvgl)
    idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
    target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
endif()
