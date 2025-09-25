#include "lvgl.h"
#include "image.h"

static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t
    terrarium_ok_map[] = {
        0xE0, 0x07,
};

const lv_image_dsc_t gImage_terrarium_ok = {
    .header.w = 1,
    .header.h = 1,
    .data_size = sizeof(terrarium_ok_map),
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .data = terrarium_ok_map,
};
