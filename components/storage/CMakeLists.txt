idf_component_register(SRCS "storage.c" "logs.c"
                        INCLUDE_DIRS "."
                        REQUIRES fatfs sdmmc)
