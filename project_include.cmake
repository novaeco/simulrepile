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
        foreach(_lang IN ITEMS C CXX ASM)
            idf_build_set_property(${_lang}_COMPILE_OPTIONS "${ESP_IDF_COMPAT_INCLUDE_FLAG}" APPEND)
            idf_build_set_property(${_lang}_COMPILE_OPTIONS "${ESP_IDF_COMPAT_INCLUDE_OPTION}" APPEND)
        endforeach()
    endif()

    foreach(_lang IN ITEMS C CXX ASM)
        if(_lang STREQUAL "C")
            set(_flags_var "CMAKE_C_FLAGS")
        elseif(_lang STREQUAL "CXX")
            set(_flags_var "CMAKE_CXX_FLAGS")
        else()
            set(_flags_var "CMAKE_ASM_FLAGS")
        endif()

        if(NOT "${${_flags_var}}" MATCHES "${ESP_IDF_COMPAT_INCLUDE_OPTION}")
            set(_updated_flags "${${_flags_var}} ${ESP_IDF_COMPAT_INCLUDE_FLAG} \"${ESP_IDF_COMPAT_INCLUDE_OPTION}\"")
            set(${_flags_var} "${_updated_flags}" CACHE STRING "" FORCE)
            unset(_updated_flags)
        endif()

        unset(_flags_var)
    endforeach()
endif()
