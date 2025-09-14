idf_component_register(SRCS "game.c" "room.c" "terrarium/terrarium.c"
                        INCLUDE_DIRS "." "terrarium"
                        REQUIRES lvgl storage)
