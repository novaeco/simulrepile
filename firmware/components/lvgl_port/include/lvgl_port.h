#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t lvgl_port_init(void);
void lvgl_port_lock(void);
void lvgl_port_unlock(void);
void lvgl_port_invalidate(void);
void lvgl_port_feed_touch_event(bool pressed, uint16_t x, uint16_t y);

#ifdef __cplusplus
}
#endif
