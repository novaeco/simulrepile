idf_component_register(SRCS "game.c" "room.c" "economy.c" "environment.c" \
                        "terrarium/terrarium.c" "reptiles/reptiles.c" \
                        "terrarium_ui/ui.c" "render3d/render3d.cpp"
                        INCLUDE_DIRS "." "terrarium" "reptiles" "terrarium_ui" "render3d"
                        REQUIRES lvgl storage lovyangfx cJSON assets
                        EMBED_TXTFILES "reptiles/reptiles.json")
