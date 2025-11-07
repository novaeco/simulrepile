#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WAVESHARE_7B_LCD_HOR_RES    1024
#define WAVESHARE_7B_LCD_VER_RES    600

esp_err_t waveshare_7b_lgfx_init(uint16_t hor_res, uint16_t ver_res);
bool waveshare_7b_lgfx_flush(int32_t x, int32_t y, int32_t w, int32_t h, const void *pixel_data);
void waveshare_7b_lgfx_set_backlight(uint8_t percent);

#ifdef __cplusplus
}
#endif
