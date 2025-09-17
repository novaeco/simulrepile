set(srcs
    "src/lgfx/v1/platforms/esp32s3/Bus_RGB.cpp"
)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "src"
    PRIV_REQUIRES lovyangfx
)

idf_component_get_property(lovyangfx_dir lovyangfx COMPONENT_DIR)

if(lovyangfx_dir)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${lovyangfx_dir}/src")
    if(EXISTS "${lovyangfx_dir}/src/lgfx/v1/platforms/esp32s3/Bus_RGB.cpp")
        set_source_files_properties(
            "${lovyangfx_dir}/src/lgfx/v1/platforms/esp32s3/Bus_RGB.cpp"
            PROPERTIES HEADER_FILE_ONLY TRUE
        )
    endif()
endif()
