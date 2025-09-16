#include "lvgl.h"
#include "image.h"

static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t
    currency_card_map[] = {
        0xA0, 0xFE,
};

const lv_image_dsc_t gImage_currency_card = {
    .header.w = 1,
    .header.h = 1,
    .data_size = sizeof(currency_card_map),
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .data = currency_card_map,
};
