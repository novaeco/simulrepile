idf_component_register(SRCS "game.c" "room.c" "economy.c" "environment.c" "terrarium/terrarium.c" "reptiles/reptiles.c" "render3d/render3d.cpp"
                        INCLUDE_DIRS "." "terrarium" "reptiles" "render3d"
                        REQUIRES lvgl storage lovyangfx)
