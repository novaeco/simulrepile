#include "settings.h"
#include "env_control.h"
#include "lvgl.h"
#include "nvs.h"
#include "sleep.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "ui_theme.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NVS_NS "cfg"
#define KEY_ENV "env_cfg"
#define KEY_SLEEP "sleep_def"
#define KEY_LOG "log_lvl"

#define DEFAULT_SLEEP true
#define DEFAULT_LOG_LEVEL ESP_LOG_INFO
#define SPIN_SCALE_1DP 10

app_settings_t g_settings = {0};

static lv_obj_t *screen;
static lv_obj_t *tabview;
static lv_obj_t *nav_list;
static lv_obj_t *nav_general_btn;
static lv_obj_t *nav_general_label;
static lv_obj_t *btn_apply;
static lv_obj_t *btn_save;
static lv_obj_t *btn_close;
static lv_obj_t *unsaved_modal;
static lv_obj_t *feedback_modal;
static lv_obj_t *status_chip;
static bool s_initializing;
static bool s_ui_dirty;
static bool s_pending_save;

typedef struct {
    lv_obj_t *spinbox;
    lv_obj_t *slider;
    int32_t scale;
    int32_t step;
    const char *unit;
    uint8_t terrarium_index;
} spin_slider_pair_t;

typedef struct {
    lv_obj_t *slider;
    lv_obj_t *hour_spinbox;
    lv_obj_t *minute_spinbox;
    uint32_t step;
    uint8_t terrarium_index;
    lv_obj_t *value_label;
} time_control_t;

typedef struct {
    lv_obj_t *container;
    lv_obj_t *label;
} nav_entry_t;

static lv_style_t s_invalid_style;
static bool s_invalid_style_ready;
static lv_obj_t *sw_sleep;
static lv_obj_t *dd_log;
static lv_obj_t *sb_count;
static lv_obj_t *sb_period;

typedef struct {
    lv_obj_t *tab;
    lv_obj_t *tab_label;
    lv_obj_t *name;
    lv_obj_t *enabled;
    spin_slider_pair_t day_temp;
    spin_slider_pair_t day_hum;
    spin_slider_pair_t night_temp;
    spin_slider_pair_t night_hum;
    spin_slider_pair_t heat_on;
    spin_slider_pair_t heat_off;
    spin_slider_pair_t hum_on;
    spin_slider_pair_t hum_off;
    time_control_t day_start;
    time_control_t night_start;
    time_control_t uv_on;
    time_control_t uv_off;
    lv_obj_t *uv_enabled;
    spin_slider_pair_t min_heat;
    spin_slider_pair_t min_pump;
    lv_obj_t *nav_btn;
    lv_obj_t *nav_icon_label;
    lv_obj_t *nav_text_label;
} terrarium_widgets_t;

static terrarium_widgets_t s_t_widgets[REPTILE_ENV_MAX_TERRARIUMS];
static lv_obj_t *terrarium_tabs[REPTILE_ENV_MAX_TERRARIUMS];

extern lv_obj_t *menu_screen;

static lv_obj_t *create_label(lv_obj_t *parent, const char *txt)
{
    lv_obj_t *label = lv_label_create(parent);
    ui_theme_apply_body(label);
    lv_label_set_text(label, txt);
    return label;
}

static void ensure_invalid_style(void)
{
    if (s_invalid_style_ready) {
        return;
    }
    s_invalid_style_ready = true;
    lv_style_init(&s_invalid_style);
    lv_style_set_border_color(&s_invalid_style, lv_color_hex(0xC44536));
    lv_style_set_border_width(&s_invalid_style, 2);
    lv_style_set_outline_color(&s_invalid_style, lv_color_hex(0xC44536));
    lv_style_set_outline_width(&s_invalid_style, 1);
    lv_style_set_outline_pad(&s_invalid_style, 2);
}

static void set_widget_invalid(lv_obj_t *obj, bool invalid)
{
    if (!obj) {
        return;
    }
    ensure_invalid_style();
    if (invalid) {
        lv_obj_add_style(obj, &s_invalid_style, LV_PART_MAIN);
    } else {
        lv_obj_remove_style(obj, &s_invalid_style, LV_PART_MAIN);
    }
}

static void set_pair_valid(spin_slider_pair_t *pair, bool valid)
{
    if (!pair) {
        return;
    }
    set_widget_invalid(pair->spinbox, !valid);
    set_widget_invalid(pair->slider, !valid);
}

static void set_time_control_valid(time_control_t *ctrl, bool valid)
{
    if (!ctrl) {
        return;
    }
    set_widget_invalid(ctrl->slider, !valid);
    set_widget_invalid(ctrl->hour_spinbox, !valid);
    set_widget_invalid(ctrl->minute_spinbox, !valid);
    if (ctrl->value_label) {
        lv_color_t color = valid ? lv_color_hex(0x2F4F43) : lv_color_hex(0xC44536);
        lv_obj_set_style_text_color(ctrl->value_label, color, LV_PART_MAIN);
    }
}

static void update_time_control_label(time_control_t *ctrl)
{
    if (!ctrl || !ctrl->value_label) {
        return;
    }
    int32_t hour = lv_spinbox_get_value(ctrl->hour_spinbox);
    int32_t minute = lv_spinbox_get_value(ctrl->minute_spinbox);
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", (int)hour, (int)minute);
    lv_label_set_text(ctrl->value_label, buf);
}

static lv_obj_t *create_row_container(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_set_style_pad_gap(row, 12, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    return row;
}

static lv_obj_t *create_card_with_title(lv_obj_t *parent, const char *title, const char *subtitle)
{
    lv_obj_t *card = ui_theme_create_card(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_style_pad_gap(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    if (title) {
        lv_obj_t *label = lv_label_create(card);
        ui_theme_apply_title(label);
        lv_label_set_text(label, title);
    }
    if (subtitle) {
        lv_obj_t *label = lv_label_create(card);
        ui_theme_apply_caption(label);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_text(label, subtitle);
    }
    return card;
}

static void apply_slider_theme(lv_obj_t *slider)
{
    if (!slider) {
        return;
    }
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xE0F2E9), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x2A9D8F), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x2A9D8F), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, 10, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 10, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
}

static void create_spin_slider_float(size_t terrarium_index,
                                     spin_slider_pair_t *pair,
                                     lv_obj_t *card,
                                     const char *label_text,
                                     const char *unit_text,
                                     float min,
                                     float max,
                                     float step,
                                     float value,
                                     const char *tooltip)
{
    lv_obj_t *row = create_row_container(card);
    lv_obj_t *label = create_label(row, label_text);
    lv_obj_set_width(label, 220);

    lv_obj_t *spinbox = create_spinbox_1dp(row, min, max, step, value);
    lv_obj_set_width(spinbox, 120);
    lv_obj_set_style_text_align(spinbox, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t *unit = lv_label_create(row);
    ui_theme_apply_caption(unit);
    lv_label_set_text(unit, unit_text ? unit_text : "");
    lv_obj_set_style_pad_right(unit, 12, 0);

    lv_obj_t *slider = lv_slider_create(row);
    apply_slider_theme(slider);
    lv_obj_set_flex_grow(slider, 1);
    lv_obj_set_style_margin_left(slider, 8, 0);
    int32_t scale = SPIN_SCALE_1DP;
    int32_t min_i = (int32_t)lroundf(min * (float)scale);
    int32_t max_i = (int32_t)lroundf(max * (float)scale);
    int32_t step_i = (int32_t)lroundf(step * (float)scale);
    if (step_i <= 0) {
        step_i = 1;
    }
    int32_t value_i = (int32_t)lroundf(value * (float)scale);
    lv_slider_set_range(slider, min_i, max_i);
    lv_slider_set_value(slider, value_i, LV_ANIM_OFF);

    if (tooltip) {
        lv_obj_set_tooltip_text(spinbox, tooltip);
        lv_obj_set_tooltip_text(slider, tooltip);
    }

    bind_spin_slider_pair(pair, (uint8_t)terrarium_index, spinbox, slider, scale, step_i,
                          unit_text);
}

static void create_spin_slider_int(size_t terrarium_index,
                                   spin_slider_pair_t *pair,
                                   lv_obj_t *card,
                                   const char *label_text,
                                   const char *unit_text,
                                   int32_t min,
                                   int32_t max,
                                   int32_t step,
                                   int32_t value,
                                   const char *tooltip)
{
    lv_obj_t *row = create_row_container(card);
    lv_obj_t *label = create_label(row, label_text);
    lv_obj_set_width(label, 220);

    lv_obj_t *spinbox = create_spinbox_int(row, min, max, step, value);
    lv_obj_set_width(spinbox, 120);
    lv_obj_set_style_text_align(spinbox, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t *unit = lv_label_create(row);
    ui_theme_apply_caption(unit);
    lv_label_set_text(unit, unit_text ? unit_text : "");
    lv_obj_set_style_pad_right(unit, 12, 0);

    lv_obj_t *slider = lv_slider_create(row);
    apply_slider_theme(slider);
    lv_obj_set_flex_grow(slider, 1);
    lv_obj_set_style_margin_left(slider, 8, 0);
    if (step <= 0) {
        step = 1;
    }
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);

    if (tooltip) {
        lv_obj_set_tooltip_text(spinbox, tooltip);
        lv_obj_set_tooltip_text(slider, tooltip);
    }

    bind_spin_slider_pair(pair, (uint8_t)terrarium_index, spinbox, slider, 1, step,
                          unit_text);
}

static void create_time_control(size_t terrarium_index,
                                time_control_t *ctrl,
                                lv_obj_t *card,
                                const char *label_text,
                                const char *tooltip,
                                uint32_t initial_minutes,
                                uint32_t step_minutes)
{
    lv_obj_t *row = create_row_container(card);
    lv_obj_t *label = create_label(row, label_text);
    lv_obj_set_width(label, 220);

    lv_obj_t *slider = lv_slider_create(row);
    apply_slider_theme(slider);
    lv_slider_set_range(slider, 0, 24 * 60 - 1);
    lv_obj_set_flex_grow(slider, 1);
    lv_obj_set_style_margin_left(slider, 8, 0);
    lv_slider_set_value(slider, (int32_t)initial_minutes, LV_ANIM_OFF);

    lv_obj_t *value_label = lv_label_create(row);
    ui_theme_apply_caption(value_label);
    lv_obj_set_width(value_label, 70);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t *box = lv_obj_create(row);
    lv_obj_remove_style_all(box);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(box, 6, 0);
    lv_obj_set_style_pad_all(box, 0, 0);

    uint32_t hour = initial_minutes / 60U;
    uint32_t minute = initial_minutes % 60U;

    lv_obj_t *hour_sb = create_spinbox_int(box, 0, 23, 1, (int32_t)hour);
    lv_obj_set_width(hour_sb, 70);
    lv_obj_set_style_text_align(hour_sb, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_t *hour_unit = lv_label_create(box);
    ui_theme_apply_caption(hour_unit);
    lv_label_set_text(hour_unit, "h");

    lv_obj_t *min_sb = create_spinbox_int(box, 0, 59, (int32_t)step_minutes,
                                          (int32_t)minute);
    lv_obj_set_width(min_sb, 70);
    lv_obj_set_style_text_align(min_sb, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_t *min_unit = lv_label_create(box);
    ui_theme_apply_caption(min_unit);
    lv_label_set_text(min_unit, "min");

    if (tooltip) {
        lv_obj_set_tooltip_text(slider, tooltip);
        lv_obj_set_tooltip_text(hour_sb, tooltip);
        lv_obj_set_tooltip_text(min_sb, tooltip);
    }

    bind_time_control(ctrl, (uint8_t)terrarium_index, slider, hour_sb, min_sb,
                      value_label, step_minutes);
}

static bool settings_has_unsaved_changes(void)
{
    return s_ui_dirty || s_pending_save;
}

static void update_status_chip(void)
{
    if (!status_chip) {
        return;
    }

    const char *text = "Synchronisé";
    ui_theme_badge_kind_t kind = UI_THEME_BADGE_SUCCESS;

    if (s_ui_dirty) {
        text = "Modifications en cours";
        kind = UI_THEME_BADGE_WARNING;
    } else if (s_pending_save) {
        text = "Appliqué, à sauvegarder";
        kind = UI_THEME_BADGE_WARNING;
    } else if (settings_has_unsaved_changes()) {
        text = "À sauvegarder";
        kind = UI_THEME_BADGE_WARNING;
    }

    lv_label_set_text(status_chip, text);
    ui_theme_badge_set_kind(status_chip, kind);
}

static void update_action_buttons(void)
{
    if (btn_apply) {
        if (s_ui_dirty) {
            lv_obj_clear_state(btn_apply, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(btn_apply, LV_STATE_DISABLED);
        }
    }
    if (btn_save) {
        if (settings_has_unsaved_changes()) {
            lv_obj_clear_state(btn_save, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(btn_save, LV_STATE_DISABLED);
        }
    }
    update_status_chip();
}

static void settings_mark_pending_save(void)
{
    s_pending_save = true;
    update_action_buttons();
}

static void settings_mark_dirty(void)
{
    if (s_initializing) {
        return;
    }
    s_ui_dirty = true;
    update_action_buttons();
}

static void settings_ui_throttle(void)
{
#if CONFIG_ESP_TASK_WDT
    (void)esp_task_wdt_reset();
#endif
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        TickType_t delay = pdMS_TO_TICKS(1);
        if (delay == 0) {
            delay = 1;
        }
        vTaskDelay(delay);
    }
}

static uint8_t digits_required_int_range(int32_t min, int32_t max)
{
    if (min > max) {
        int32_t tmp = min;
        min = max;
        max = tmp;
    }

    long long max_abs = llabs((long long)max);
    long long min_abs = llabs((long long)min);
    if (min_abs > max_abs) {
        max_abs = min_abs;
    }

    uint8_t digits = 1;
    while (max_abs >= 10) {
        max_abs /= 10;
        ++digits;
    }
    return digits;
}

static uint8_t digits_required_float_range(float min, float max, uint8_t decimal_pos)
{
    float floor_min = floorf(fminf(min, max));
    float ceil_max = ceilf(fmaxf(min, max));
    uint8_t digits = digits_required_int_range((int32_t)floor_min, (int32_t)ceil_max);
    digits += decimal_pos;

    uint8_t min_digits = (uint8_t)(decimal_pos + 1U);
    if (digits < min_digits) {
        digits = min_digits;
    }

    return digits;
}

static void format_species_monogram(size_t index, const char *name, char *out, size_t len)
{
    if (!out || len == 0) {
        return;
    }
    size_t out_len = 0;
    bool new_word = true;
    if (name) {
        for (const char *p = name; *p != '\0'; ++p) {
            unsigned char c = (unsigned char)*p;
            if (isalpha(c)) {
                if (new_word && out_len < len - 1U) {
                    out[out_len++] = (char)toupper(c);
                    new_word = false;
                }
            } else if (isdigit(c)) {
                if (new_word && out_len < len - 1U) {
                    out[out_len++] = (char)c;
                    new_word = false;
                }
            } else {
                new_word = true;
            }
            if (out_len >= len - 1U) {
                break;
            }
        }
    }
    if (out_len == 0U) {
        snprintf(out, len, "T%zu", index + 1U);
    } else {
        out[out_len] = '\0';
    }
}

static void format_terrarium_title(size_t index, const char *name, char *out, size_t len)
{
    if (!out || len == 0) {
        return;
    }
    char monogram[8];
    format_species_monogram(index, name, monogram, sizeof(monogram));
    const char *display = (name && name[0] != '\0') ? name : "Terrarium";
    if (name && name[0] != '\0') {
        snprintf(out, len, "%s %s", monogram, name);
    } else {
        snprintf(out, len, "%s %s %zu", monogram, display, index + 1U);
    }
}

static void bind_spin_slider_pair(spin_slider_pair_t *pair,
                                  uint8_t terrarium_index,
                                  lv_obj_t *spinbox,
                                  lv_obj_t *slider,
                                  int32_t scale,
                                  int32_t step,
                                  const char *unit);

static void bind_time_control(time_control_t *ctrl,
                              uint8_t terrarium_index,
                              lv_obj_t *slider,
                              lv_obj_t *hour_sb,
                              lv_obj_t *minute_sb,
                              lv_obj_t *value_label,
                              uint32_t step);

static bool validate_terrarium(size_t index);
static void update_nav_summary(size_t index);
static void update_tab_title(size_t index);
static void update_nav_highlight(size_t active_tab_index);

static lv_obj_t *create_spinbox_int(lv_obj_t *parent, int32_t min, int32_t max, int32_t step, int32_t value)
{
    int32_t min_bound = (min < max) ? min : max;
    int32_t max_bound = (max > min) ? max : min;
    int32_t clamped_value = value;
    if (clamped_value < min_bound) {
        clamped_value = min_bound;
    } else if (clamped_value > max_bound) {
        clamped_value = max_bound;
    }

    if (step <= 0) {
        step = 1;
    }

    lv_obj_t *sb = lv_spinbox_create(parent);
    lv_spinbox_set_range(sb, min_bound, max_bound);
    lv_spinbox_set_step(sb, step);
    lv_spinbox_set_digit_format(sb, digits_required_int_range(min_bound, max_bound), 0);
    lv_spinbox_set_value(sb, clamped_value);
    return sb;
}

static lv_obj_t *create_spinbox_1dp(lv_obj_t *parent, float min, float max, float step, float value)
{
    float range_min = fminf(min, max);
    float range_max = fmaxf(min, max);

    int32_t imin = (int32_t)lroundf(range_min * SPIN_SCALE_1DP);
    int32_t imax = (int32_t)lroundf(range_max * SPIN_SCALE_1DP);
    int32_t istep = (int32_t)lroundf(step * SPIN_SCALE_1DP);
    if (istep <= 0) {
        istep = 1;
    }

    int32_t ivalue = (int32_t)lroundf(value * SPIN_SCALE_1DP);
    if (ivalue < imin) {
        ivalue = imin;
    } else if (ivalue > imax) {
        ivalue = imax;
    }

    lv_obj_t *sb = lv_spinbox_create(parent);
    lv_spinbox_set_range(sb, imin, imax);
    lv_spinbox_set_step(sb, istep);
    lv_spinbox_set_digit_format(sb, digits_required_float_range(range_min, range_max, 1), 1);
    lv_spinbox_set_value(sb, ivalue);
    return sb;
}

static void apply_count_visibility(uint32_t count)
{
    for (size_t i = 0; i < REPTILE_ENV_MAX_TERRARIUMS; ++i) {
        if (!terrarium_tabs[i]) {
            continue;
        }
        if (i < count) {
            lv_obj_clear_flag(terrarium_tabs[i], LV_OBJ_FLAG_HIDDEN);
            if (s_t_widgets[i].nav_btn) {
                lv_obj_clear_flag(s_t_widgets[i].nav_btn, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            lv_obj_add_flag(terrarium_tabs[i], LV_OBJ_FLAG_HIDDEN);
            if (s_t_widgets[i].nav_btn) {
                lv_obj_add_flag(s_t_widgets[i].nav_btn, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    if (tabview) {
        update_nav_highlight(lv_tabview_get_active(tabview));
    }
}

static void count_changed_cb(lv_event_t *e)
{
    (void)e;
    uint32_t count = (uint32_t)lv_spinbox_get_value(sb_count);
    settings_mark_dirty();
    apply_count_visibility(count);
    update_general_nav_summary();
}

static int32_t round_to_step(int32_t value, int32_t step, int32_t min, int32_t max)
{
    if (value < min) {
        value = min;
    } else if (value > max) {
        value = max;
    }
    if (step <= 1) {
        return value;
    }
    int32_t offset = value - min;
    int32_t remainder = offset % step;
    if (remainder != 0) {
        if (remainder >= step / 2) {
            offset += step - remainder;
        } else {
            offset -= remainder;
        }
    }
    value = min + offset;
    if (value < min) {
        value = min;
    } else if (value > max) {
        value = max;
    }
    return value;
}

static void spinbox_pair_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    spin_slider_pair_t *pair = lv_event_get_user_data(e);
    if (!pair || !pair->spinbox || !pair->slider) {
        return;
    }
    int32_t value = lv_spinbox_get_value(pair->spinbox);
    int32_t min = lv_slider_get_min_value(pair->slider);
    int32_t max = lv_slider_get_max_value(pair->slider);
    value = round_to_step(value, pair->step, min, max);
    if (lv_slider_get_value(pair->slider) != value) {
        lv_slider_set_value(pair->slider, value, LV_ANIM_OFF);
    }
    settings_mark_dirty();
    if (pair->unit) {
        /* Tooltip déjà défini statiquement. */
    }
    update_nav_summary(pair->terrarium_index);
}

static void slider_pair_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    spin_slider_pair_t *pair = lv_event_get_user_data(e);
    if (!pair || !pair->spinbox || !pair->slider) {
        return;
    }
    int32_t min = lv_slider_get_min_value(pair->slider);
    int32_t max = lv_slider_get_max_value(pair->slider);
    int32_t value = lv_slider_get_value(pair->slider);
    value = round_to_step(value, pair->step, min, max);
    if (lv_slider_get_value(pair->slider) != value) {
        lv_slider_set_value(pair->slider, value, LV_ANIM_OFF);
    }
    if (lv_spinbox_get_value(pair->spinbox) != value) {
        lv_spinbox_set_value(pair->spinbox, value);
    }
    settings_mark_dirty();
    update_nav_summary(pair->terrarium_index);
}

static void time_slider_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    time_control_t *ctrl = lv_event_get_user_data(e);
    if (!ctrl || !ctrl->slider || !ctrl->hour_spinbox || !ctrl->minute_spinbox) {
        return;
    }
    int32_t min = lv_slider_get_min_value(ctrl->slider);
    int32_t max = lv_slider_get_max_value(ctrl->slider);
    int32_t value = lv_slider_get_value(ctrl->slider);
    value = round_to_step(value, (int32_t)ctrl->step, min, max);
    if (lv_slider_get_value(ctrl->slider) != value) {
        lv_slider_set_value(ctrl->slider, value, LV_ANIM_OFF);
    }
    int32_t hour = value / 60;
    int32_t minute = value % 60;
    if ((int32_t)ctrl->step > 1) {
        minute = round_to_step(minute, (int32_t)ctrl->step, 0, 59);
        value = hour * 60 + minute;
        if (lv_slider_get_value(ctrl->slider) != value) {
            lv_slider_set_value(ctrl->slider, value, LV_ANIM_OFF);
        }
    }
    if (lv_spinbox_get_value(ctrl->hour_spinbox) != hour) {
        lv_spinbox_set_value(ctrl->hour_spinbox, hour);
    }
    if (lv_spinbox_get_value(ctrl->minute_spinbox) != minute) {
        lv_spinbox_set_value(ctrl->minute_spinbox, minute);
    }
    update_time_control_label(ctrl);
    settings_mark_dirty();
    update_nav_summary(ctrl->terrarium_index);
}

static void time_spinbox_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    time_control_t *ctrl = lv_event_get_user_data(e);
    if (!ctrl || !ctrl->slider || !ctrl->hour_spinbox || !ctrl->minute_spinbox) {
        return;
    }
    int32_t hour = lv_spinbox_get_value(ctrl->hour_spinbox);
    int32_t minute = lv_spinbox_get_value(ctrl->minute_spinbox);
    if ((int32_t)ctrl->step > 1) {
        minute = round_to_step(minute, (int32_t)ctrl->step, 0, 59);
        if (lv_spinbox_get_value(ctrl->minute_spinbox) != minute) {
            lv_spinbox_set_value(ctrl->minute_spinbox, minute);
        }
    }
    int32_t value = hour * 60 + minute;
    int32_t min = lv_slider_get_min_value(ctrl->slider);
    int32_t max = lv_slider_get_max_value(ctrl->slider);
    value = round_to_step(value, (int32_t)ctrl->step, min, max);
    if (lv_slider_get_value(ctrl->slider) != value) {
        lv_slider_set_value(ctrl->slider, value, LV_ANIM_OFF);
    }
    update_time_control_label(ctrl);
    settings_mark_dirty();
    update_nav_summary(ctrl->terrarium_index);
}

static void name_text_changed_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    terrarium_widgets_t *w = lv_event_get_user_data(e);
    if (!w) {
        return;
    }
    settings_mark_dirty();
    update_tab_title((size_t)(w - s_t_widgets));
    update_nav_summary((size_t)(w - s_t_widgets));
}

static void enabled_switch_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    terrarium_widgets_t *w = lv_event_get_user_data(e);
    if (!w) {
        return;
    }
    settings_mark_dirty();
    update_nav_summary((size_t)(w - s_t_widgets));
}

static void uv_switch_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    terrarium_widgets_t *w = lv_event_get_user_data(e);
    if (!w) {
        return;
    }
    settings_mark_dirty();
    update_nav_summary((size_t)(w - s_t_widgets));
}

static void nav_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    uintptr_t target = (uintptr_t)lv_event_get_user_data(e);
    if (!tabview) {
        return;
    }
    lv_tabview_set_active(tabview, (uint16_t)target, LV_ANIM_OFF);
}

static void tabview_value_changed_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    if (!tabview) {
        return;
    }
    uint16_t index = lv_tabview_get_active(tabview);
    update_nav_highlight(index);
}

static void generic_dirty_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    settings_mark_dirty();
}

static void general_settings_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    settings_mark_dirty();
    update_general_nav_summary();
}

static void bind_spin_slider_pair(spin_slider_pair_t *pair,
                                  uint8_t terrarium_index,
                                  lv_obj_t *spinbox,
                                  lv_obj_t *slider,
                                  int32_t scale,
                                  int32_t step,
                                  const char *unit)
{
    if (!pair) {
        return;
    }
    pair->terrarium_index = terrarium_index;
    pair->spinbox = spinbox;
    pair->slider = slider;
    pair->scale = scale;
    pair->step = (step <= 0) ? 1 : step;
    pair->unit = unit;
    if (spinbox) {
        lv_obj_add_event_cb(spinbox, spinbox_pair_event_cb, LV_EVENT_VALUE_CHANGED, pair);
    }
    if (slider) {
        lv_obj_add_event_cb(slider, slider_pair_event_cb, LV_EVENT_VALUE_CHANGED, pair);
    }
}

static void bind_time_control(time_control_t *ctrl,
                              uint8_t terrarium_index,
                              lv_obj_t *slider,
                              lv_obj_t *hour_sb,
                              lv_obj_t *minute_sb,
                              lv_obj_t *value_label,
                              uint32_t step)
{
    if (!ctrl) {
        return;
    }
    ctrl->terrarium_index = terrarium_index;
    ctrl->slider = slider;
    ctrl->hour_spinbox = hour_sb;
    ctrl->minute_spinbox = minute_sb;
    ctrl->value_label = value_label;
    ctrl->step = (step == 0U) ? 1U : step;
    if (slider) {
        lv_obj_add_event_cb(slider, time_slider_event_cb, LV_EVENT_VALUE_CHANGED, ctrl);
    }
    if (hour_sb) {
        lv_obj_add_event_cb(hour_sb, time_spinbox_event_cb, LV_EVENT_VALUE_CHANGED, ctrl);
    }
    if (minute_sb) {
        lv_obj_add_event_cb(minute_sb, time_spinbox_event_cb, LV_EVENT_VALUE_CHANGED, ctrl);
    }
    update_time_control_label(ctrl);
}

static uint32_t time_control_get_minutes(const time_control_t *ctrl)
{
    if (!ctrl) {
        return 0U;
    }
    int32_t hour = ctrl->hour_spinbox ? lv_spinbox_get_value(ctrl->hour_spinbox) : 0;
    int32_t minute = ctrl->minute_spinbox ? lv_spinbox_get_value(ctrl->minute_spinbox) : 0;
    if (minute < 0) {
        minute = 0;
    }
    if (minute > 59) {
        minute = 59;
    }
    if (hour < 0) {
        hour = 0;
    }
    if (hour > 23) {
        hour = 23;
    }
    return (uint32_t)(hour * 60 + minute);
}

static void update_tab_title(size_t index)
{
    if (index >= REPTILE_ENV_MAX_TERRARIUMS) {
        return;
    }
    terrarium_widgets_t *w = &s_t_widgets[index];
    if (!w->tab_label) {
        return;
    }
    const char *name = w->name ? lv_textarea_get_text(w->name) : NULL;
    char title[64];
    format_terrarium_title(index, name ? name : "", title, sizeof(title));
    lv_label_set_text(w->tab_label, title);
}

static bool validate_terrarium(size_t index)
{
    if (index >= REPTILE_ENV_MAX_TERRARIUMS) {
        return true;
    }
    terrarium_widgets_t *w = &s_t_widgets[index];
    bool valid = true;

    if (w->heat_on.spinbox && w->heat_off.spinbox) {
        bool ok = lv_spinbox_get_value(w->heat_on.spinbox) > lv_spinbox_get_value(w->heat_off.spinbox);
        set_pair_valid(&w->heat_on, ok);
        set_pair_valid(&w->heat_off, ok);
        valid = valid && ok;
    }
    if (w->hum_on.spinbox && w->hum_off.spinbox) {
        bool ok = lv_spinbox_get_value(w->hum_on.spinbox) > lv_spinbox_get_value(w->hum_off.spinbox);
        set_pair_valid(&w->hum_on, ok);
        set_pair_valid(&w->hum_off, ok);
        valid = valid && ok;
    }

    uint32_t day_minutes = time_control_get_minutes(&w->day_start);
    uint32_t night_minutes = time_control_get_minutes(&w->night_start);
    bool schedule_ok = (day_minutes != night_minutes);
    set_time_control_valid(&w->day_start, schedule_ok);
    set_time_control_valid(&w->night_start, schedule_ok);
    valid = valid && schedule_ok;

    bool uv_enabled = w->uv_enabled && lv_obj_has_state(w->uv_enabled, LV_STATE_CHECKED);
    if (uv_enabled) {
        uint32_t uv_on_minutes = time_control_get_minutes(&w->uv_on);
        uint32_t uv_off_minutes = time_control_get_minutes(&w->uv_off);
        bool uv_ok = (uv_on_minutes != uv_off_minutes);
        set_time_control_valid(&w->uv_on, uv_ok);
        set_time_control_valid(&w->uv_off, uv_ok);
        valid = valid && uv_ok;
    } else {
        set_time_control_valid(&w->uv_on, true);
        set_time_control_valid(&w->uv_off, true);
    }

    return valid;
}

static void update_nav_summary(size_t index)
{
    if (index >= REPTILE_ENV_MAX_TERRARIUMS) {
        return;
    }
    terrarium_widgets_t *w = &s_t_widgets[index];
    const char *name = w->name ? lv_textarea_get_text(w->name) : "";
    update_tab_title(index);

    bool valid = validate_terrarium(index);
    if (w->nav_icon_label) {
        char monogram[8];
        format_species_monogram(index, name, monogram, sizeof(monogram));
        lv_label_set_text(w->nav_icon_label, monogram);
    }

    if (!w->nav_text_label) {
        return;
    }

    float day_temp = (w->day_temp.spinbox) ?
                         (float)lv_spinbox_get_value(w->day_temp.spinbox) / (float)w->day_temp.scale :
                         0.0f;
    float day_hum = (w->day_hum.spinbox) ?
                        (float)lv_spinbox_get_value(w->day_hum.spinbox) :
                        0.0f;
    float night_temp = (w->night_temp.spinbox) ?
                           (float)lv_spinbox_get_value(w->night_temp.spinbox) / (float)w->night_temp.scale :
                           0.0f;
    float night_hum = (w->night_hum.spinbox) ?
                          (float)lv_spinbox_get_value(w->night_hum.spinbox) :
                          0.0f;

    bool enabled = w->enabled ? lv_obj_has_state(w->enabled, LV_STATE_CHECKED) : true;
    char title[64];
    format_terrarium_title(index, name, title, sizeof(title));

    char summary[160];
    snprintf(summary, sizeof(summary), "%s%s\nJour %.1f%s / %.0f%s\nNuit %.1f%s / %.0f%s%s",
             valid ? "" : LV_SYMBOL_WARNING " ",
             title,
             day_temp, w->day_temp.unit ? w->day_temp.unit : "",
             day_hum, w->day_hum.unit ? w->day_hum.unit : "",
             night_temp, w->night_temp.unit ? w->night_temp.unit : "",
             night_hum, w->night_hum.unit ? w->night_hum.unit : "",
             enabled ? "" : "\n(Désactivé)");
    lv_label_set_text(w->nav_text_label, summary);
}

static void update_nav_highlight(size_t active_tab_index)
{
    if (nav_general_btn) {
        ui_theme_set_card_selected(nav_general_btn, active_tab_index == 0U);
    }
    for (size_t i = 0; i < REPTILE_ENV_MAX_TERRARIUMS; ++i) {
        terrarium_widgets_t *w = &s_t_widgets[i];
        if (!w->nav_btn) {
            continue;
        }
        size_t tab_index = i + 1U;
        ui_theme_set_card_selected(w->nav_btn, tab_index == active_tab_index);
    }
}

static void update_general_nav_summary(void)
{
    if (!nav_general_label) {
        return;
    }
    long count = sb_count ? lv_spinbox_get_value(sb_count) : 0L;
    long period = sb_period ? lv_spinbox_get_value(sb_period) : 0L;
    char buffer[96];
    snprintf(buffer, sizeof(buffer),
             "Général\nTerrariums: %ld\nBoucle: %ld ms",
             count, period);
    lv_label_set_text(nav_general_label, buffer);
}

void settings_apply(void)
{
    sleep_set_enabled(g_settings.sleep_default);
    esp_log_level_set("*", g_settings.log_level);
    reptile_env_update_config(&g_settings.env_config);
}

esp_err_t settings_save(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(nvs, KEY_ENV, &g_settings.env_config, sizeof(g_settings.env_config));
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, KEY_SLEEP, g_settings.sleep_default);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, KEY_LOG, g_settings.log_level);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t settings_init(void)
{
    reptile_env_get_default_config(&g_settings.env_config);
    g_settings.sleep_default = DEFAULT_SLEEP;
    g_settings.log_level = DEFAULT_LOG_LEVEL;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        size_t size = sizeof(g_settings.env_config);
        if (nvs_get_blob(nvs, KEY_ENV, &g_settings.env_config, &size) != ESP_OK) {
            // keep defaults
        }
        uint8_t val8;
        if (nvs_get_u8(nvs, KEY_SLEEP, &val8) == ESP_OK) {
            g_settings.sleep_default = val8;
        }
        if (nvs_get_u8(nvs, KEY_LOG, &val8) == ESP_OK) {
            g_settings.log_level = val8;
        }
        nvs_close(nvs);
    }

    if (g_settings.env_config.terrarium_count == 0 ||
        g_settings.env_config.terrarium_count > REPTILE_ENV_MAX_TERRARIUMS) {
        g_settings.env_config.terrarium_count = 1;
    }

    settings_apply();
    return ESP_OK;
}

static void populate_terrarium_tab(size_t index,
                                   lv_obj_t *tab,
                                   const reptile_env_terrarium_config_t *cfg)
{
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(tab, 18, 0);

    terrarium_widgets_t *w = &s_t_widgets[index];
    w->tab = tab;

    lv_obj_t *card = create_card_with_title(
        tab, "Identification",
        "Active le contrôle et personnalise le nom utilisé dans les journaux.");
    lv_obj_t *row = create_row_container(card);
    create_label(row, "Activé");
    w->enabled = lv_switch_create(row);
    if (cfg->enabled) {
        lv_obj_add_state(w->enabled, LV_STATE_CHECKED);
    }
    lv_obj_set_tooltip_text(w->enabled,
                            "Désactive complètement le contrôle environnemental pour ce canal.");
    lv_obj_add_event_cb(w->enabled, enabled_switch_event_cb, LV_EVENT_VALUE_CHANGED, w);

    row = create_row_container(card);
    create_label(row, "Nom affiché");
    w->name = lv_textarea_create(row);
    lv_textarea_set_one_line(w->name, true);
    lv_textarea_set_max_length(w->name, sizeof(cfg->name) - 1);
    lv_textarea_set_text(w->name, cfg->name);
    lv_obj_set_flex_grow(w->name, 1);
    lv_obj_set_width(w->name, LV_PCT(60));
    lv_obj_set_tooltip_text(w->name,
                            "Nom convivial du terrarium (affiché dans le sommaire et les exports).");
    lv_obj_add_event_cb(w->name, name_text_changed_cb, LV_EVENT_VALUE_CHANGED, w);

    settings_ui_throttle();

    card = create_card_with_title(tab, "Profil jour",
                                  "Consignes appliquées durant la photopériode active.");
    create_spin_slider_float(index, &w->day_temp, card, "Température cible", "°C", 10.0f, 45.0f,
                             0.5f, cfg->day.temperature_c,
                             "Consigne de température en phase diurne.");
    create_spin_slider_int(index, &w->day_hum, card, "Humidité cible", "%", 0, 100, 1,
                           (int32_t)lroundf(cfg->day.humidity_pct),
                           "Hygrométrie visée pendant la journée.");

    settings_ui_throttle();

    card = create_card_with_title(tab, "Profil nuit",
                                  "Consignes appliquées lorsque le cycle nocturne est actif.");
    create_spin_slider_float(index, &w->night_temp, card, "Température cible", "°C", 5.0f, 40.0f,
                             0.5f, cfg->night.temperature_c,
                             "Consigne de température pendant la nuit.");
    create_spin_slider_int(index, &w->night_hum, card, "Humidité cible", "%", 0, 100, 1,
                           (int32_t)lroundf(cfg->night.humidity_pct),
                           "Hygrométrie cible pendant la nuit.");

    settings_ui_throttle();

    card = create_card_with_title(tab, "Hystérésis actionneurs",
                                  "Définit les marges de déclenchement et de relâche.");
    create_spin_slider_float(index, &w->heat_on, card, "Chauffage ON", "°C", 0.5f, 10.0f, 0.1f,
                             cfg->hysteresis.heat_on_delta,
                             "Delta sous la consigne provoquant un cycle de chauffage.");
    create_spin_slider_float(index, &w->heat_off, card, "Chauffage OFF", "°C", 0.1f, 10.0f, 0.1f,
                             cfg->hysteresis.heat_off_delta,
                             "Delta au-dessus de la consigne avant d'autoriser le chauffage suivant.");
    create_spin_slider_float(index, &w->hum_on, card, "Brumisation ON", "%", 1.0f, 30.0f, 0.5f,
                             cfg->hysteresis.humidity_on_delta,
                             "Décalage sous la consigne d'humidité déclenchant la pompe.");
    create_spin_slider_float(index, &w->hum_off, card, "Brumisation OFF", "%", 1.0f, 30.0f, 0.5f,
                             cfg->hysteresis.humidity_off_delta,
                             "Décalage au-dessus de la consigne avant la prochaine brumisation.");

    settings_ui_throttle();

    card = create_card_with_title(tab, "Cycle jour/nuit",
                                  "Programmation des bascules de profils.");
    create_time_control(index, &w->day_start, card, "Début du jour",
                        "Horodatage d'activation du profil diurne.",
                        (uint32_t)cfg->day_start.hour * 60U + cfg->day_start.minute, 5);
    create_time_control(index, &w->night_start, card, "Début de la nuit",
                        "Horodatage d'activation du profil nocturne.",
                        (uint32_t)cfg->night_start.hour * 60U + cfg->night_start.minute, 5);

    settings_ui_throttle();

    card = create_card_with_title(tab, "Éclairage UV",
                                  "Planification quotidienne des UV automatiques.");
    row = create_row_container(card);
    create_label(row, "UV automatiques");
    w->uv_enabled = lv_switch_create(row);
    if (cfg->uv.enabled) {
        lv_obj_add_state(w->uv_enabled, LV_STATE_CHECKED);
    }
    lv_obj_set_tooltip_text(w->uv_enabled,
                            "Active ou non la planification automatique de l'éclairage UV.");
    lv_obj_add_event_cb(w->uv_enabled, uv_switch_event_cb, LV_EVENT_VALUE_CHANGED, w);

    create_time_control(index, &w->uv_on, card, "Allumage",
                        "Heure d'allumage quotidienne des UV.",
                        (uint32_t)cfg->uv.on.hour * 60U + cfg->uv.on.minute, 5);
    create_time_control(index, &w->uv_off, card, "Extinction",
                        "Heure d'extinction quotidienne des UV.",
                        (uint32_t)cfg->uv.off.hour * 60U + cfg->uv.off.minute, 5);

    settings_ui_throttle();

    card = create_card_with_title(tab, "Intervalle minimal",
                                  "Temps minimum entre deux cycles pour limiter l'usure.");
    create_spin_slider_int(index, &w->min_heat, card, "Chauffage", "min", 0, 240, 1,
                           (int32_t)cfg->min_minutes_between_heat,
                           "Durée minimale entre deux cycles de chauffage.");
    create_spin_slider_int(index, &w->min_pump, card, "Brumisation", "min", 0, 240, 1,
                           (int32_t)cfg->min_minutes_between_pump,
                           "Durée minimale entre deux cycles de brumisation.");

    settings_ui_throttle();

    update_nav_summary(index);
}

static void feedback_modal_close_cb(lv_event_t *e)
{
    (void)e;
    if (feedback_modal) {
        lv_obj_del_async(feedback_modal);
        feedback_modal = NULL;
    }
}

static void show_feedback_modal(const char *title, const char *message, bool warning)
{
    if (feedback_modal) {
        lv_obj_del_async(feedback_modal);
        feedback_modal = NULL;
    }
    feedback_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(feedback_modal);
    lv_obj_set_style_bg_color(feedback_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(feedback_modal, LV_OPA_50, 0);
    lv_obj_set_size(feedback_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_center(feedback_modal);

    lv_obj_t *card = ui_theme_create_card(feedback_modal);
    lv_obj_set_width(card, 420);
    lv_obj_center(card);
    lv_obj_set_style_pad_gap(card, 16, 0);

    if (title) {
        lv_obj_t *title_label = lv_label_create(card);
        ui_theme_apply_title(title_label);
        lv_label_set_text(title_label, title);
    }

    if (message) {
        lv_obj_t *body_label = lv_label_create(card);
        ui_theme_apply_body(body_label);
        lv_label_set_long_mode(body_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(body_label, LV_PCT(100));
        lv_label_set_text(body_label, message);
    }

    lv_obj_t *btn_row = lv_obj_create(card);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(btn_row, 12, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_width(btn_row, LV_PCT(100));

    lv_obj_t *btn = ui_theme_create_button(btn_row, "OK",
                                           warning ? UI_THEME_BUTTON_PRIMARY
                                                   : UI_THEME_BUTTON_SECONDARY,
                                           feedback_modal_close_cb, NULL);
    lv_obj_set_width(btn, LV_SIZE_CONTENT);
}

static void show_validation_error_dialog(void)
{
    show_feedback_modal("Validation requise",
                        "Corrigez les paramètres surlignés en rouge avant d'appliquer.",
                        true);
}

static void show_save_error_dialog(esp_err_t err)
{
    char buffer[160];
    snprintf(buffer, sizeof(buffer),
             "NVS a renvoyé %s (%d). Les paramètres sont appliqués mais non sauvegardés.",
             esp_err_to_name(err), (int)err);
    show_feedback_modal("Échec de la sauvegarde", buffer, true);
}

static bool copy_ui_to_settings(void)
{
    if (!sb_count || !sb_period) {
        return false;
    }

    bool all_valid = true;
    for (size_t i = 0; i < REPTILE_ENV_MAX_TERRARIUMS; ++i) {
        if (!s_t_widgets[i].tab) {
            continue;
        }
        if (!validate_terrarium(i)) {
            all_valid = false;
        }
        update_nav_summary(i);
    }

    if (!all_valid) {
        return false;
    }

    g_settings.sleep_default = sw_sleep && lv_obj_has_state(sw_sleep, LV_STATE_CHECKED);
    if (dd_log) {
        g_settings.log_level = lv_dropdown_get_selected(dd_log);
    }
    g_settings.env_config.terrarium_count = (size_t)lv_spinbox_get_value(sb_count);
    if (g_settings.env_config.terrarium_count == 0U) {
        g_settings.env_config.terrarium_count = 1U;
    }
    g_settings.env_config.period_ms = (uint32_t)lv_spinbox_get_value(sb_period);

    for (size_t i = 0; i < REPTILE_ENV_MAX_TERRARIUMS; ++i) {
        terrarium_widgets_t *w = &s_t_widgets[i];
        reptile_env_terrarium_config_t *cfg = &g_settings.env_config.terrarium[i];
        if (!w->tab) {
            continue;
        }
        const char *name = w->name ? lv_textarea_get_text(w->name) : "";
        if (!name) {
            name = "";
        }
        strncpy(cfg->name, name, sizeof(cfg->name) - 1U);
        cfg->name[sizeof(cfg->name) - 1U] = '\0';
        cfg->enabled = w->enabled && lv_obj_has_state(w->enabled, LV_STATE_CHECKED);

        if (w->day_temp.spinbox) {
            cfg->day.temperature_c =
                (float)lv_spinbox_get_value(w->day_temp.spinbox) / (float)w->day_temp.scale;
        }
        if (w->day_hum.spinbox) {
            cfg->day.humidity_pct = (float)lv_spinbox_get_value(w->day_hum.spinbox);
        }
        if (w->night_temp.spinbox) {
            cfg->night.temperature_c =
                (float)lv_spinbox_get_value(w->night_temp.spinbox) /
                (float)w->night_temp.scale;
        }
        if (w->night_hum.spinbox) {
            cfg->night.humidity_pct = (float)lv_spinbox_get_value(w->night_hum.spinbox);
        }
        if (w->heat_on.spinbox) {
            cfg->hysteresis.heat_on_delta =
                (float)lv_spinbox_get_value(w->heat_on.spinbox) / (float)w->heat_on.scale;
        }
        if (w->heat_off.spinbox) {
            cfg->hysteresis.heat_off_delta =
                (float)lv_spinbox_get_value(w->heat_off.spinbox) / (float)w->heat_off.scale;
        }
        if (w->hum_on.spinbox) {
            cfg->hysteresis.humidity_on_delta =
                (float)lv_spinbox_get_value(w->hum_on.spinbox) / (float)w->hum_on.scale;
        }
        if (w->hum_off.spinbox) {
            cfg->hysteresis.humidity_off_delta =
                (float)lv_spinbox_get_value(w->hum_off.spinbox) / (float)w->hum_off.scale;
        }

        uint32_t minutes = time_control_get_minutes(&w->day_start);
        cfg->day_start.hour = (uint8_t)(minutes / 60U);
        cfg->day_start.minute = (uint8_t)(minutes % 60U);
        minutes = time_control_get_minutes(&w->night_start);
        cfg->night_start.hour = (uint8_t)(minutes / 60U);
        cfg->night_start.minute = (uint8_t)(minutes % 60U);

        cfg->uv.enabled = w->uv_enabled && lv_obj_has_state(w->uv_enabled, LV_STATE_CHECKED);
        minutes = time_control_get_minutes(&w->uv_on);
        cfg->uv.on.hour = (uint8_t)(minutes / 60U);
        cfg->uv.on.minute = (uint8_t)(minutes % 60U);
        minutes = time_control_get_minutes(&w->uv_off);
        cfg->uv.off.hour = (uint8_t)(minutes / 60U);
        cfg->uv.off.minute = (uint8_t)(minutes % 60U);

        if (w->min_heat.spinbox) {
            cfg->min_minutes_between_heat =
                (uint32_t)lv_spinbox_get_value(w->min_heat.spinbox);
        }
        if (w->min_pump.spinbox) {
            cfg->min_minutes_between_pump =
                (uint32_t)lv_spinbox_get_value(w->min_pump.spinbox);
        }
    }

    update_general_nav_summary();
    return true;
}

static void close_btn_cb(lv_event_t *e)
{
    (void)e;
    settings_close_screen(false);
}

static void apply_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!copy_ui_to_settings()) {
        show_validation_error_dialog();
        return;
    }
    settings_apply();
    s_ui_dirty = false;
    settings_mark_pending_save();
    update_action_buttons();
}

static void unsaved_modal_cancel_cb(lv_event_t *e)
{
    (void)e;
    if (unsaved_modal) {
        lv_obj_del_async(unsaved_modal);
        unsaved_modal = NULL;
    }
}

static void settings_close_screen(bool force);

static void unsaved_modal_quit_cb(lv_event_t *e)
{
    (void)e;
    if (unsaved_modal) {
        lv_obj_del_async(unsaved_modal);
        unsaved_modal = NULL;
    }
    settings_close_screen(true);
}

static void show_unsaved_modal(void)
{
    if (unsaved_modal) {
        return;
    }
    unsaved_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(unsaved_modal);
    lv_obj_set_style_bg_color(unsaved_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(unsaved_modal, LV_OPA_50, 0);
    lv_obj_set_size(unsaved_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_center(unsaved_modal);

    lv_obj_t *card = ui_theme_create_card(unsaved_modal);
    lv_obj_set_width(card, 460);
    lv_obj_center(card);
    lv_obj_set_style_pad_gap(card, 18, 0);

    lv_obj_t *title = lv_label_create(card);
    ui_theme_apply_title(title);
    lv_label_set_text(title, "Modifications non sauvegardées");

    lv_obj_t *body = lv_label_create(card);
    ui_theme_apply_body(body);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, LV_PCT(100));
    lv_label_set_text(body,
                      "Des changements ne sont pas sauvegardés. Quitter sans enregistrer ?");

    lv_obj_t *btn_row = lv_obj_create(card);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(btn_row, 12, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_width(btn_row, LV_PCT(100));

    ui_theme_create_button(btn_row, "Annuler", UI_THEME_BUTTON_SECONDARY,
                           unsaved_modal_cancel_cb, NULL);
    ui_theme_create_button(btn_row, "Quitter sans sauver", UI_THEME_BUTTON_PRIMARY,
                           unsaved_modal_quit_cb, NULL);
}

static void settings_close_screen(bool force)
{
    if (!force && settings_has_unsaved_changes()) {
        show_unsaved_modal();
        return;
    }
    if (unsaved_modal) {
        lv_obj_del_async(unsaved_modal);
        unsaved_modal = NULL;
    }
    if (feedback_modal) {
        lv_obj_del_async(feedback_modal);
        feedback_modal = NULL;
    }
    if (screen && lv_obj_is_valid(screen)) {
        lv_scr_load(menu_screen);
        lv_obj_del_async(screen);
    } else {
        lv_scr_load(menu_screen);
    }
    screen = NULL;
    tabview = NULL;
    nav_list = NULL;
    nav_general_btn = NULL;
    nav_general_label = NULL;
    status_chip = NULL;
    btn_apply = NULL;
    btn_save = NULL;
    btn_close = NULL;
}

static void save_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!copy_ui_to_settings()) {
        show_validation_error_dialog();
        return;
    }

    settings_apply();
    esp_err_t err = settings_save();
    if (err != ESP_OK) {
        s_ui_dirty = false;
        settings_mark_pending_save();
        show_save_error_dialog(err);
        update_action_buttons();
        return;
    }

    s_ui_dirty = false;
    s_pending_save = false;
    update_action_buttons();
    settings_close_screen(true);
}

void settings_screen_show(void)
{
    s_initializing = true;
    s_ui_dirty = false;
    s_pending_save = false;

    memset(s_t_widgets, 0, sizeof(s_t_widgets));
    memset(terrarium_tabs, 0, sizeof(terrarium_tabs));

    screen = lv_obj_create(NULL);
    ui_theme_apply_screen(screen);
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(screen, 24, 0);
    lv_obj_set_style_pad_gap(screen, 24, 0);

    lv_obj_t *header = lv_obj_create(screen);
    lv_obj_remove_style_all(header);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_pad_gap(header, 16, 0);

    lv_obj_t *title_col = lv_obj_create(header);
    lv_obj_remove_style_all(title_col);
    lv_obj_set_flex_flow(title_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(title_col, 0, 0);
    lv_obj_set_style_pad_gap(title_col, 4, 0);

    lv_obj_t *title = lv_label_create(title_col);
    ui_theme_apply_title(title);
    lv_label_set_text(title, "Configuration terrariums");

    lv_obj_t *subtitle = lv_label_create(title_col);
    ui_theme_apply_caption(subtitle);
    lv_label_set_text(subtitle,
                      "Profils jour/nuit, UV, hystérésis et état de persistance.");

    status_chip = ui_theme_create_badge(header, UI_THEME_BADGE_SUCCESS, "Synchronisé");
    lv_obj_set_style_align_self(status_chip, LV_ALIGN_CENTER, 0);

    lv_obj_t *body = lv_obj_create(screen);
    lv_obj_remove_style_all(body);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_pad_gap(body, 24, 0);
    lv_obj_set_flex_grow(body, 1);

    nav_list = lv_obj_create(body);
    lv_obj_remove_style_all(nav_list);
    lv_obj_set_width(nav_list, 320);
    lv_obj_set_height(nav_list, LV_PCT(100));
    lv_obj_set_flex_flow(nav_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(nav_list, 0, 0);
    lv_obj_set_style_pad_gap(nav_list, 16, 0);
    lv_obj_set_scroll_dir(nav_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(nav_list, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *content = lv_obj_create(body);
    lv_obj_remove_style_all(content);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_gap(content, 18, 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_height(content, LV_PCT(100));

    tabview = lv_tabview_create(content);
    lv_obj_set_flex_grow(tabview, 1);
    lv_obj_set_size(tabview, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(tabview, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(tabview, 0, LV_PART_MAIN);
    lv_tabview_set_anim_time(tabview, 0);
    lv_obj_add_event_cb(tabview, tabview_value_changed_cb, LV_EVENT_VALUE_CHANGED,
                        NULL);

    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tabview);
    if (tab_bar) {
        lv_obj_add_flag(tab_bar, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t *tab_content = lv_tabview_get_content(tabview);
    if (tab_content) {
        lv_obj_set_style_pad_all(tab_content, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_gap(tab_content, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tab_content, LV_OPA_TRANSP, LV_PART_MAIN);
    }

    nav_general_btn = ui_theme_create_nav_card(nav_list, "Général", "",
                                               LV_SYMBOL_SETTINGS,
                                               UI_THEME_NAV_ICON_SYMBOL,
                                               nav_btn_event_cb, NULL);
    nav_general_label = lv_obj_get_child(nav_general_btn, 2);

    lv_obj_t *tab_general = lv_tabview_add_tab(tabview, "Général");
    lv_obj_set_style_pad_all(tab_general, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(tab_general, 18, LV_PART_MAIN);
    lv_obj_set_flex_flow(tab_general, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(tab_general, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(tab_general, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *card = create_card_with_title(
        tab_general, "Configuration globale",
        "Définit le nombre de canaux et la période de régulation.");
    lv_obj_t *row = create_row_container(card);
    lv_obj_t *label = create_label(row, "Nombre de terrariums");
    lv_obj_set_width(label, 260);
    sb_count = create_spinbox_int(row, 1, REPTILE_ENV_MAX_TERRARIUMS, 1,
                                  (int32_t)g_settings.env_config.terrarium_count);
    lv_obj_set_width(sb_count, 120);
    lv_obj_set_style_text_align(sb_count, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_add_event_cb(sb_count, count_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_tooltip_text(sb_count,
                            "Nombre de terrariums physiques/simulés pilotés.");

    row = create_row_container(card);
    label = create_label(row, "Période boucle");
    lv_obj_set_width(label, 260);
    sb_period = create_spinbox_int(row, 200, 10000, 100,
                                   (int32_t)g_settings.env_config.period_ms);
    lv_obj_set_width(sb_period, 120);
    lv_obj_set_style_text_align(sb_period, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_add_event_cb(sb_period, general_settings_event_cb, LV_EVENT_VALUE_CHANGED,
                        NULL);
    lv_obj_set_tooltip_text(sb_period,
                            "Intervalle d'actualisation du contrôleur (millisecondes).");
    lv_obj_t *unit = lv_label_create(row);
    ui_theme_apply_caption(unit);
    lv_label_set_text(unit, "ms");

    settings_ui_throttle();

    card = create_card_with_title(tab_general, "Session & journalisation",
                                  "Veille écran et verbosité console série.");
    row = create_row_container(card);
    label = create_label(row, "Veille automatique");
    lv_obj_set_width(label, 260);
    sw_sleep = lv_switch_create(row);
    if (g_settings.sleep_default) {
        lv_obj_add_state(sw_sleep, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw_sleep, general_settings_event_cb, LV_EVENT_VALUE_CHANGED,
                        NULL);
    lv_obj_set_tooltip_text(
        sw_sleep,
        "Active la mise en veille écran après inactivité lors du démarrage.");

    row = create_row_container(card);
    label = create_label(row, "Niveau logs série");
    lv_obj_set_width(label, 260);
    dd_log = lv_dropdown_create(row);
    ui_theme_apply_dropdown(dd_log);
    lv_dropdown_set_options_static(dd_log,
                                   "NONE\nERROR\nWARN\nINFO\nDEBUG\nVERBOSE");
    lv_dropdown_set_selected(dd_log, g_settings.log_level);
    lv_obj_set_width(dd_log, 220);
    lv_obj_add_event_cb(dd_log, general_settings_event_cb, LV_EVENT_VALUE_CHANGED,
                        NULL);
    lv_obj_set_tooltip_text(dd_log,
                            "Sévérité minimale remontée sur l'UART de débogage.");

    settings_ui_throttle();

    card = create_card_with_title(tab_general, "Flux Appliquer/Sauver",
                                  "Comprendre le cycle de validation de la configuration.");
    lv_obj_t *body_label = lv_label_create(card);
    ui_theme_apply_body(body_label);
    lv_label_set_long_mode(body_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body_label, LV_PCT(100));
    lv_label_set_text(body_label,
                      "« Appliquer » pousse immédiatement les consignes au contrôleur. "
                      "« Sauver » persiste en NVS après validation.");

    settings_ui_throttle();

    reptile_env_config_t defaults;
    reptile_env_get_default_config(&defaults);

    for (size_t i = 0; i < REPTILE_ENV_MAX_TERRARIUMS; ++i) {
        reptile_env_terrarium_config_t cfg =
            defaults.terrarium[(i < defaults.terrarium_count) ? i
                                                              : defaults.terrarium_count - 1];
        if (i < g_settings.env_config.terrarium_count) {
            cfg = g_settings.env_config.terrarium[i];
        }

        char title_buf[64];
        format_terrarium_title(i, cfg.name, title_buf, sizeof(title_buf));
        char monogram[8];
        format_species_monogram(i, cfg.name, monogram, sizeof(monogram));

        lv_obj_t *nav_card = ui_theme_create_nav_card(
            nav_list, title_buf, "", monogram, UI_THEME_NAV_ICON_SYMBOL,
            nav_btn_event_cb, (void *)(uintptr_t)(i + 1U));
        s_t_widgets[i].nav_btn = nav_card;
        s_t_widgets[i].nav_icon_label = lv_obj_get_child(nav_card, 0);
        s_t_widgets[i].tab_label = lv_obj_get_child(nav_card, 1);
        s_t_widgets[i].nav_text_label = lv_obj_get_child(nav_card, 2);

        lv_obj_t *tab = lv_tabview_add_tab(tabview, "");
        terrarium_tabs[i] = tab;
        if (tab) {
            lv_obj_set_style_pad_all(tab, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_gap(tab, 18, LV_PART_MAIN);
            lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_scroll_dir(tab, LV_DIR_VER);
            lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_AUTO);
        }

        populate_terrarium_tab(i, tab, &cfg);
        settings_ui_throttle();
    }

    apply_count_visibility(g_settings.env_config.terrarium_count);
    lv_tabview_set_active(tabview, 0, LV_ANIM_OFF);
    update_nav_highlight(0U);
    update_general_nav_summary();

    lv_obj_t *action_bar = lv_obj_create(screen);
    lv_obj_remove_style_all(action_bar);
    lv_obj_set_width(action_bar, LV_PCT(100));
    lv_obj_set_flex_flow(action_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(action_bar, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(action_bar, 0, 0);
    lv_obj_set_style_pad_gap(action_bar, 16, 0);

    btn_close = ui_theme_create_button(action_bar, "Fermer",
                                       UI_THEME_BUTTON_SECONDARY, close_btn_cb,
                                       NULL);
    btn_apply = ui_theme_create_button(action_bar, "Appliquer",
                                       UI_THEME_BUTTON_SECONDARY, apply_btn_cb,
                                       NULL);
    btn_save = ui_theme_create_button(action_bar, "Sauver",
                                      UI_THEME_BUTTON_PRIMARY, save_btn_cb, NULL);

    s_initializing = false;
    update_action_buttons();
    update_nav_highlight(lv_tabview_get_active(tabview));

    lv_scr_load(screen);
}



