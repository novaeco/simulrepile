#include "ui_settings.h"

#include "app_config.h"
#include "i18n.h"
#include "ui_theme.h"

static lv_obj_t *s_container;

void ui_settings_init(lv_obj_t *parent)
{
    s_container = lv_obj_create(parent);
    lv_obj_set_size(s_container, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_container, LV_OBJ_FLAG_HIDDEN);
    ui_theme_style_panel(s_container);

    lv_obj_t *label = lv_label_create(s_container);
    lv_label_set_text(label, "Paramètres (stub)");
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 10, 10);
}

void ui_settings_show(void)
{
    lv_obj_clear_flag(s_container, LV_OBJ_FLAG_HIDDEN);
}
