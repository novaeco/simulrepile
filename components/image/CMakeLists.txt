file(GLOB IMG_SRCS "*.c")
list(REMOVE_ITEM IMG_SRCS ${CMAKE_CURRENT_LIST_DIR}/image.c)
idf_component_register(
    SRCS ${IMG_SRCS} "image.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
)
