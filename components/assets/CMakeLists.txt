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

if(ASSET_BIN)
    # Mark generated assets so that CMake does not require them to exist at
    # configure time. The actual conversion is handled by the custom command
    # below.
    set_source_files_properties(${ASSET_BIN} PROPERTIES GENERATED TRUE)
endif()

if(ASSET_BIN_REL)
    set_source_files_properties(${ASSET_BIN_REL} PROPERTIES GENERATED TRUE)
endif()

set(EMBED_ARGS)
if(ASSET_BIN_REL)
    set(EMBED_ARGS EMBED_FILES ${ASSET_BIN_REL})
endif()

idf_component_register(SRCS "assets.c"
                      INCLUDE_DIRS "."
                      REQUIRES storage
                      ${EMBED_ARGS})

if(ASSET_PNG AND NOT CMAKE_SCRIPT_MODE_FILE)
    if(EXISTS ${GEN_SCRIPT})
        execute_process(COMMAND ${CMAKE_COMMAND} -E chmod +x ${GEN_SCRIPT})
    else()
        message(FATAL_ERROR "Asset generation script not found: ${GEN_SCRIPT}")
    endif()

    add_custom_command(OUTPUT ${ASSET_BIN}
        COMMAND ${GEN_SCRIPT}
        DEPENDS ${ASSET_PNG} ${GEN_SCRIPT}
        WORKING_DIRECTORY ${ASSET_ROOT}
        BYPRODUCTS ${ASSET_BIN}
        VERBATIM
        COMMENT "Generating LVGL assets")

    add_custom_target(gen_assets DEPENDS ${ASSET_BIN})
    add_dependencies(${COMPONENT_LIB} gen_assets)
endif()
