#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_ROOT_VIEW_BOOT_SPLASH,
    UI_ROOT_VIEW_DISCLAIMER,
    UI_ROOT_VIEW_DASHBOARD,
    UI_ROOT_VIEW_SLOTS,
    UI_ROOT_VIEW_DOCS,
    UI_ROOT_VIEW_SETTINGS,
} ui_root_view_t;

esp_err_t ui_root_init(void);
void ui_root_show_boot_splash(void);
void ui_root_show_disclaimer(void);
void ui_root_update(void);
esp_err_t ui_root_set_view(ui_root_view_t view);
void ui_root_show_dashboard(void);
void ui_root_show_slots(void);
void ui_root_show_docs(void);
void ui_root_show_settings(void);
void ui_root_set_link_alert(bool visible, const char *message);

#ifdef __cplusplus
}
#endif
