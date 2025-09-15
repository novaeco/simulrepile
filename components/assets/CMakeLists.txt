set(ASSET_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../assets)
set(GEN_SCRIPT ${ASSET_ROOT}/generate_assets.sh)

file(GLOB_RECURSE ASSET_PNG
     ${ASSET_ROOT}/icons/*.png
     ${ASSET_ROOT}/textures/*.png
     ${ASSET_ROOT}/sprites/*.png)

set(ASSET_BIN)
set(ASSET_BIN_REL)
foreach(png ${ASSET_PNG})
    get_filename_component(name ${png} NAME_WE)
    get_filename_component(dir ${png} DIRECTORY)
    set(bin ${dir}/${name}.bin)
    list(APPEND ASSET_BIN ${bin})
    file(RELATIVE_PATH rel ${CMAKE_SOURCE_DIR} ${bin})
    list(APPEND ASSET_BIN_REL ${rel})
endforeach()

add_custom_command(OUTPUT ${ASSET_BIN}
    COMMAND ${GEN_SCRIPT}
    DEPENDS ${ASSET_PNG}
    WORKING_DIRECTORY ${ASSET_ROOT}
    COMMENT "Generating LVGL assets")

add_custom_target(gen_assets DEPENDS ${ASSET_BIN})

idf_component_register(SRCS "assets.c"
                      INCLUDE_DIRS "."
                      REQUIRES storage
                      EMBED_FILES ${ASSET_BIN_REL})

add_dependencies(${COMPONENT_LIB} gen_assets)
