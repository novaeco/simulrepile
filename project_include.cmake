set(ESP_IDF_COMPAT_HEADER "${CMAKE_CURRENT_LIST_DIR}/tools/esp_idf_compat/gpio_hal_compat.h")
if(EXISTS "${ESP_IDF_COMPAT_HEADER}")
    cmake_path(CONVERT "${ESP_IDF_COMPAT_HEADER}" TO_CMAKE_PATH_LIST ESP_IDF_COMPAT_HEADER_CMAKE_STYLE)

    # Compose the forced-include flag once and reuse it across the different
    # mechanisms that ESP-IDF consumes during the build graph generation.  Using
    # a list keeps the flag and its argument as two distinct entries, which is
    # required for MSYS/MinGW toolchains on Windows.
    set(ESP_IDF_COMPAT_INCLUDE_FLAG "-include")
    set(ESP_IDF_COMPAT_INCLUDE_OPTION ${ESP_IDF_COMPAT_HEADER_CMAKE_STYLE})

    add_compile_options(${ESP_IDF_COMPAT_INCLUDE_FLAG} ${ESP_IDF_COMPAT_INCLUDE_OPTION})

    if(COMMAND idf_build_set_property)
        idf_build_set_property(COMPILE_OPTIONS "${ESP_IDF_COMPAT_INCLUDE_FLAG}" APPEND)
        idf_build_set_property(COMPILE_OPTIONS "${ESP_IDF_COMPAT_INCLUDE_OPTION}" APPEND)
    endif()

    foreach(_lang C CXX ASM)
        if(NOT CMAKE_${_lang}_FLAGS MATCHES "${ESP_IDF_COMPAT_INCLUDE_OPTION}")
            set(_updated_flags
                "${CMAKE_${_lang}_FLAGS} ${ESP_IDF_COMPAT_INCLUDE_FLAG} \"${ESP_IDF_COMPAT_INCLUDE_OPTION}\"")
            set(CMAKE_${_lang}_FLAGS "${_updated_flags}" CACHE STRING "" FORCE)
            unset(_updated_flags)
        endif()
    endforeach()
endif()
