idf_component_register(SRCS "sensors.c" "sensors_real.c" "sensors_sim.c"
                        INCLUDE_DIRS "."
                        REQUIRES i2c freertos config esp_system)
