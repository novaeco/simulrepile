#include "lvgl.h"
#include "image.h"

static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t reptile_sad_map[] = {
    0x00, 0x00,
};

const lv_image_dsc_t gImage_reptile_sad = {
    .header.w = 1,
    .header.h = 1,
    .data_size = sizeof(reptile_sad_map),
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .data = reptile_sad_map,
};
