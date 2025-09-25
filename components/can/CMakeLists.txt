idf_component_register(SRCS "can.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES i2c gpio io_extension
                       REQUIRES driver
                      )
