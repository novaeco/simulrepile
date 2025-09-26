#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t lvgl_port_init(void);
void lvgl_port_lock(void);
void lvgl_port_unlock(void);
void lvgl_port_invalidate(void);

#ifdef __cplusplus
}
#endif
