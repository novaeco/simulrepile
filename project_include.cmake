set(ESP_IDF_COMPAT_HEADER "${CMAKE_CURRENT_LIST_DIR}/tools/esp_idf_compat/gpio_hal_compat.h")
if(EXISTS "${ESP_IDF_COMPAT_HEADER}")
    idf_build_set_property(COMPILE_OPTIONS "-include" "${ESP_IDF_COMPAT_HEADER}" APPEND)
endif()
