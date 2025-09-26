#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ui_root_init(void);
void ui_root_show_boot_splash(void);
void ui_root_show_disclaimer(void);
void ui_root_update(void);

#ifdef __cplusplus
}
#endif
