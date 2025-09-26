#include "ui_theme.h"

#include "app_config.h"

static lv_style_t s_panel_style;
static bool s_theme_initialised = false;

void ui_theme_init(void)
{
    if (s_theme_initialised) {
        return;
    }
    lv_style_init(&s_panel_style);
    lv_style_set_bg_color(&s_panel_style, lv_color_hex(0x101820));
    lv_style_set_border_color(&s_panel_style, lv_color_hex(0x7FDBFF));
    lv_style_set_border_width(&s_panel_style, 2);
    lv_style_set_pad_all(&s_panel_style, 8);
    lv_style_set_radius(&s_panel_style, 8);
    s_theme_initialised = true;
}

void ui_theme_apply_root(lv_obj_t *root)
{
    ui_theme_init();
    lv_obj_set_style_bg_color(root, lv_color_hex(0x0B1F2F), 0);
    lv_obj_set_style_text_color(root, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
}

void ui_theme_style_panel(lv_obj_t *obj)
{
    ui_theme_init();
    lv_obj_add_style(obj, &s_panel_style, 0);
    if (CONFIG_APP_THEME_HIGH_CONTRAST) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x1F2F3F), 0);
        lv_obj_set_style_text_color(obj, lv_color_hex(0xFFFFFF), 0);
    }
}
