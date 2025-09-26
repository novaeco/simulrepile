#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_init(void);
esp_err_t bsp_backlight_enable(bool enable);
esp_err_t bsp_backlight_set(uint8_t percent);
esp_err_t bsp_battery_read_mv(uint16_t *millivolts);
esp_err_t bsp_select_usb(bool usb_mode);

#ifdef __cplusplus
}
#endif
