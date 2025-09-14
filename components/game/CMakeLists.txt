idf_component_register(SRCS "game.c" "room.c" "economy.c" "environment.c" "terrarium/terrarium.c" "reptiles/reptiles.c"
                        INCLUDE_DIRS "." "terrarium" "reptiles"
                        REQUIRES lvgl storage)
