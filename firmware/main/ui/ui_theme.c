#include "ui_theme.h"

#include "app_config.h"

static lv_style_t s_panel_style;
static bool s_theme_initialised = false;
static bool s_high_contrast = APP_THEME_HIGH_CONTRAST;

void ui_theme_init(void)
{
    if (s_theme_initialised) {
        return;
    }
    lv_style_init(&s_panel_style);
    lv_style_set_bg_color(&s_panel_style, s_high_contrast ? lv_color_hex(0x101820) : lv_color_hex(0x2E2E38));
    lv_style_set_border_color(&s_panel_style, lv_color_hex(0x7FDBFF));
    lv_style_set_border_width(&s_panel_style, 2);
    lv_style_set_pad_all(&s_panel_style, 8);
    lv_style_set_radius(&s_panel_style, 8);
    s_theme_initialised = true;
}

void ui_theme_apply_root(lv_obj_t *root)
{
    ui_theme_init();
    if (s_high_contrast) {
        lv_obj_set_style_bg_color(root, lv_color_hex(0x0B1F2F), 0);
        lv_obj_set_style_text_color(root, lv_color_hex(0xFFFFFF), 0);
    } else {
        lv_obj_set_style_bg_color(root, lv_color_hex(0x20252C), 0);
        lv_obj_set_style_text_color(root, lv_color_hex(0xE0E0E0), 0);
    }
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
}

void ui_theme_style_panel(lv_obj_t *obj)
{
    ui_theme_init();
    lv_obj_add_style(obj, &s_panel_style, 0);
    if (s_high_contrast) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x1F2F3F), 0);
        lv_obj_set_style_text_color(obj, lv_color_hex(0xFFFFFF), 0);
    } else {
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x2E3A44), 0);
        lv_obj_set_style_text_color(obj, lv_color_hex(0xE6E6E6), 0);
    }
}

void ui_theme_set_high_contrast(bool enabled)
{
    s_high_contrast = enabled;
    lv_style_set_bg_color(&s_panel_style, s_high_contrast ? lv_color_hex(0x101820) : lv_color_hex(0x2E2E38));
    lv_obj_report_style_change(&s_panel_style);
}

bool ui_theme_is_high_contrast(void)
{
    return s_high_contrast;
}
