idf_component_register(SRCS "gpio.c" "gpio_sim.c"
                        INCLUDE_DIRS "."
                        REQUIRES driver freertos esp_system
                        PRIV_REQUIRES config waveshare_io
                    )
