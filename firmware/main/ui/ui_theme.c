#include "ui/ui_theme.h"

#include "esp_log.h"

#include "lvgl_port.h"

enum {
    UI_THEME_FLAG_HIGH_CONTRAST = 1 << 0,
};

typedef struct {
    lv_color_t screen_bg;
    lv_color_t panel_bg;
    lv_color_t panel_border;
    lv_color_t text_primary;
    lv_color_t text_accent;
    lv_color_t focus_outline;
    lv_opa_t focus_outline_opa;
    lv_coord_t focus_outline_width;
    const lv_font_t *font_body;
    const lv_font_t *font_accent;
} ui_theme_palette_t;

#if defined(LV_FONT_MONTSERRAT_18) && LV_FONT_MONTSERRAT_18
LV_FONT_DECLARE(lv_font_montserrat_18);
#endif

#if defined(LV_FONT_MONTSERRAT_20) && LV_FONT_MONTSERRAT_20
LV_FONT_DECLARE(lv_font_montserrat_20);
#endif

static const char *TAG = "ui_theme";

static lv_style_t s_style_screen;
static lv_style_t s_style_panel;
static lv_style_t s_style_label_primary;
static lv_style_t s_style_label_accent;
static bool s_styles_initialized = false;

static unsigned s_flags = 0;

static void ui_theme_init_styles(void);
static void ui_theme_apply_palette(const ui_theme_palette_t *palette);
static const lv_font_t *ui_theme_select_font_default(void);
static const lv_font_t *ui_theme_select_font_large(void);

void ui_theme_apply_default(void)
{
    ui_theme_init_styles();
    const ui_theme_palette_t palette = {
        .screen_bg = lv_color_hex(0xf7f9fc),
        .panel_bg = lv_color_hex(0xffffff),
        .panel_border = lv_color_hex(0xd0d7de),
        .text_primary = lv_color_hex(0x1b1f24),
        .text_accent = lv_color_hex(0x0057b7),
        .focus_outline = lv_color_hex(0x1b1f24),
        .focus_outline_opa = LV_OPA_TRANSP,
        .focus_outline_width = 0,
        .font_body = ui_theme_select_font_default(),
        .font_accent = ui_theme_select_font_large(),
    };

    s_flags &= ~UI_THEME_FLAG_HIGH_CONTRAST;
    ui_theme_apply_palette(&palette);
    ESP_LOGI(TAG, "Applying default theme");
    lvgl_port_invalidate();
}

void ui_theme_apply_high_contrast(bool enabled)
{
    ui_theme_init_styles();

    const ui_theme_palette_t palette = {
        .screen_bg = lv_color_hex(0x000000),
        .panel_bg = lv_color_hex(0x111111),
        .panel_border = lv_color_hex(0x444444),
        .text_primary = lv_color_hex(0xf5f5f5),
        .text_accent = lv_color_hex(0xffd600),
        .focus_outline = lv_color_hex(0xffd600),
        .focus_outline_opa = LV_OPA_COVER,
        .focus_outline_width = 4,
        .font_body = ui_theme_select_font_large(),
        .font_accent = ui_theme_select_font_large(),
    };

    if (enabled) {
        s_flags |= UI_THEME_FLAG_HIGH_CONTRAST;
        ESP_LOGI(TAG, "High contrast ON");
        ui_theme_apply_palette(&palette);
    } else {
        s_flags &= ~UI_THEME_FLAG_HIGH_CONTRAST;
        ESP_LOGI(TAG, "High contrast OFF");
        ui_theme_apply_default();
        return;
    }

    lvgl_port_invalidate();
}

bool ui_theme_is_high_contrast(void)
{
    return (s_flags & UI_THEME_FLAG_HIGH_CONTRAST) != 0;
}

void ui_theme_apply_screen_style(lv_obj_t *screen)
{
    if (!screen) {
        return;
    }
    ui_theme_init_styles();
    lv_obj_add_style(screen, &s_style_screen, LV_PART_MAIN);
}

void ui_theme_apply_panel_style(lv_obj_t *panel)
{
    if (!panel) {
        return;
    }
    ui_theme_init_styles();
    lv_obj_add_style(panel, &s_style_panel, LV_PART_MAIN);
}

void ui_theme_apply_label_style(lv_obj_t *label, bool accent)
{
    if (!label) {
        return;
    }
    ui_theme_init_styles();
    lv_obj_add_style(label, accent ? &s_style_label_accent : &s_style_label_primary, LV_PART_MAIN);
}

static void ui_theme_init_styles(void)
{
    if (s_styles_initialized) {
        return;
    }

    lv_style_init(&s_style_screen);
    lv_style_init(&s_style_panel);
    lv_style_init(&s_style_label_primary);
    lv_style_init(&s_style_label_accent);

    lv_style_set_pad_all(&s_style_panel, 16);
    lv_style_set_radius(&s_style_panel, 12);
    lv_style_set_border_width(&s_style_panel, 2);
    lv_style_set_border_opa(&s_style_panel, LV_OPA_80);
    lv_style_set_outline_pad(&s_style_panel, 4);

    lv_style_set_text_line_space(&s_style_label_primary, 6);
    lv_style_set_text_letter_space(&s_style_label_primary, 1);
    lv_style_set_text_line_space(&s_style_label_accent, 8);
    lv_style_set_text_letter_space(&s_style_label_accent, 2);

    s_styles_initialized = true;
}

static void ui_theme_apply_palette(const ui_theme_palette_t *palette)
{
    if (!palette) {
        return;
    }

    lv_style_set_bg_color(&s_style_screen, palette->screen_bg);
    lv_style_set_bg_opa(&s_style_screen, LV_OPA_COVER);
    lv_style_set_text_color(&s_style_screen, palette->text_primary);
    lv_style_set_text_font(&s_style_screen, palette->font_body);

    lv_style_set_bg_color(&s_style_panel, palette->panel_bg);
    lv_style_set_bg_opa(&s_style_panel, LV_OPA_COVER);
    lv_style_set_border_color(&s_style_panel, palette->panel_border);
    lv_style_set_shadow_width(&s_style_panel, 8);
    lv_style_set_shadow_opa(&s_style_panel, LV_OPA_30);
    lv_style_set_shadow_color(&s_style_panel, palette->panel_border);
    lv_style_set_text_color(&s_style_panel, palette->text_primary);
    lv_style_set_text_font(&s_style_panel, palette->font_body);
    lv_style_set_outline_color(&s_style_panel, palette->focus_outline);
    lv_style_set_outline_opa(&s_style_panel, palette->focus_outline_opa);
    lv_style_set_outline_width(&s_style_panel, palette->focus_outline_width);

    lv_style_set_text_color(&s_style_label_primary, palette->text_primary);
    lv_style_set_text_font(&s_style_label_primary, palette->font_body);

    lv_style_set_text_color(&s_style_label_accent, palette->text_accent);
    lv_style_set_text_font(&s_style_label_accent, palette->font_accent);

    lv_obj_report_style_change(&s_style_screen);
    lv_obj_report_style_change(&s_style_panel);
    lv_obj_report_style_change(&s_style_label_primary);
    lv_obj_report_style_change(&s_style_label_accent);
}

static const lv_font_t *ui_theme_select_font_default(void)
{
#if defined(LV_FONT_MONTSERRAT_18) && LV_FONT_MONTSERRAT_18
    return &lv_font_montserrat_18;
#else
    return LV_FONT_DEFAULT;
#endif
}

static const lv_font_t *ui_theme_select_font_large(void)
{
#if defined(LV_FONT_MONTSERRAT_20) && LV_FONT_MONTSERRAT_20
    return &lv_font_montserrat_20;
#elif defined(LV_FONT_MONTSERRAT_18) && LV_FONT_MONTSERRAT_18
    return &lv_font_montserrat_18;
#else
    return LV_FONT_DEFAULT;
#endif
}
