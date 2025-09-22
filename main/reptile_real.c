#include "reptile_real.h"
#include "env_control.h"
#include "gpio.h"
#include "sensors.h"
#include "lvgl.h"
#include "lvgl_compat.h"
#include "lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "settings.h"
#include "logging.h"
#include "ui_theme.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHART_POINT_COUNT 120
#define SPARKLINE_POINT_COUNT 48
#define TEMP_NEEDLE_LENGTH 58
#define UV_BAR_MAX 150

#define COLOR_CARD_GRADIENT_TOP lv_color_hex(0xF7F1E5)
#define COLOR_CARD_GRADIENT_BOTTOM lv_color_hex(0xE0F5ED)
#define COLOR_AVATAR_PRIMARY lv_color_hex(0x3A7D60)
#define COLOR_AVATAR_SECONDARY lv_color_hex(0x6AC8A1)
#define COLOR_STATUS_OK lv_color_hex(0x2E7D32)
#define COLOR_STATUS_MANUAL lv_color_hex(0xFF8F00)
#define COLOR_STATUS_ALARM lv_color_hex(0xC62828)

static const char *TAG = "reptile_real";

typedef struct terrarium_ui {
    size_t index;
    lv_obj_t *card;
    lv_obj_t *header;
    lv_obj_t *species_avatar;
    lv_obj_t *species_icon_label;
    lv_obj_t *title;
    lv_obj_t *status_icon_label;
    lv_obj_t *status_badge;
    lv_obj_t *metrics_label;
    lv_obj_t *energy_label;
    lv_obj_t *alarm_label;
    lv_obj_t *temp_scale;
    lv_obj_t *temp_needle;
    lv_obj_t *hum_bar;
    lv_obj_t *uv_bar;
    lv_obj_t *uv_info_label;
    lv_obj_t *btn_heat;
    lv_obj_t *btn_heat_label;
    lv_obj_t *btn_pump;
    lv_obj_t *btn_pump_label;
    lv_obj_t *btn_uv;
    lv_obj_t *btn_uv_label;
    lv_obj_t *history_chart;
    lv_obj_t *sparkline_temp;
    lv_obj_t *sparkline_hum;
    lv_chart_series_t *history_temp_series;
    lv_chart_series_t *history_hum_series;
    lv_chart_series_t *sparkline_temp_series;
    lv_chart_series_t *sparkline_hum_series;
    lv_coord_t temp_points[CHART_POINT_COUNT];
    lv_coord_t hum_points[CHART_POINT_COUNT];
    lv_coord_t temp_sparkline_points[SPARKLINE_POINT_COUNT];
    lv_coord_t hum_sparkline_points[SPARKLINE_POINT_COUNT];
} terrarium_ui_t;

static void feed_task(void *arg);
static void env_state_cb(size_t index, const reptile_env_terrarium_state_t *state, void *ctx);
static void pump_btn_cb(lv_event_t *e);
static void heat_btn_cb(lv_event_t *e);
static void uv_btn_cb(lv_event_t *e);
static void feed_btn_cb(lv_event_t *e);
static void menu_btn_cb(lv_event_t *e);
static void emergency_stop_cb(lv_event_t *e);
static void update_summary_panel(void);
static void apply_actuator_button_style(lv_obj_t *btn, bool manual_active, bool alarm_active);
static void refresh_status_header(terrarium_ui_t *ui, const reptile_env_terrarium_state_t *state);
static void show_manual_action_feedback(const terrarium_ui_t *ui, const char *action, esp_err_t status);
static void show_manual_action_toast(const char *text, bool success);
static void manual_toast_timer_cb(lv_timer_t *timer);
static void format_species_avatar_text(char *buffer, size_t len, size_t index, const char *name);
static void fill_chart_buffers(lv_coord_t *temp_buffer,
                               lv_coord_t *hum_buffer,
                               size_t buffer_len,
                               size_t start_index,
                               size_t sample_count);
static bool ensure_history_buffer(void);

static lv_obj_t *screen;
static lv_obj_t *feed_status_label;
static volatile bool feed_running;
static TaskHandle_t feed_task_handle;
static size_t s_ui_count;
static lv_obj_t *summary_panel;
static lv_obj_t *summary_energy_label;
static lv_obj_t *summary_alarm_label;
static lv_obj_t *emergency_button;
static bool s_emergency_engaged;
static lv_obj_t *manual_toast;
static lv_timer_t *manual_toast_timer;

static terrarium_ui_t s_ui[REPTILE_ENV_MAX_TERRARIUMS];
static reptile_env_history_entry_t *s_history_buf;
static size_t s_history_capacity;
static reptile_env_terrarium_state_t s_last_states[REPTILE_ENV_MAX_TERRARIUMS];
static bool s_state_valid[REPTILE_ENV_MAX_TERRARIUMS];

extern lv_obj_t *menu_screen;

static void update_feed_status(void)
{
    if (!feed_status_label) {
        return;
    }
    lv_label_set_text(feed_status_label, feed_running ? "Nourrissage: ON" : "Nourrissage: OFF");
}

static void feed_task(void *arg)
{
    (void)arg;
    feed_running = true;
    if (lvgl_port_lock(-1)) {
        update_feed_status();
        lvgl_port_unlock();
    }
    reptile_feed_gpio();
    feed_running = false;
    if (lvgl_port_lock(-1)) {
        update_feed_status();
        lvgl_port_unlock();
    }
    feed_task_handle = NULL;
    vTaskDelete(NULL);
}

static lv_obj_t *create_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user_data, lv_obj_t **label_out)
{
    lv_obj_t *btn = ui_theme_create_button(parent, text, UI_THEME_BUTTON_SECONDARY, cb, user_data);
    if (label_out) {
        *label_out = lv_obj_get_child(btn, 0);
    }
    return btn;
}

static void format_species_avatar_text(char *buffer, size_t len, size_t index, const char *name)
{
    if (!buffer || len == 0) {
        return;
    }
    buffer[0] = '\0';
    size_t out_len = 0;
    bool new_word = true;
    if (name) {
        for (const char *p = name; *p != '\0'; ++p) {
            unsigned char c = (unsigned char)*p;
            if (isalpha(c)) {
                if (new_word && out_len < len - 1U) {
                    buffer[out_len++] = (char)toupper(c);
                    new_word = false;
                }
            } else if (isdigit(c)) {
                if (new_word && out_len < len - 1U) {
                    buffer[out_len++] = (char)c;
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
        snprintf(buffer, len, "T%zu", index + 1U);
    } else {
        buffer[out_len] = '\0';
    }
}

static void manual_toast_timer_cb(lv_timer_t *timer)
{
    if (manual_toast && lv_obj_is_valid(manual_toast)) {
        lv_obj_del(manual_toast);
    }
    manual_toast = NULL;
    if (timer) {
        lv_timer_del(timer);
    }
    manual_toast_timer = NULL;
}

static void show_manual_action_toast(const char *text, bool success)
{
    if (manual_toast_timer) {
        lv_timer_del(manual_toast_timer);
        manual_toast_timer = NULL;
    }
    if (manual_toast && lv_obj_is_valid(manual_toast)) {
        lv_obj_del(manual_toast);
    }

    manual_toast = ui_theme_create_card(lv_layer_top());
    lv_obj_set_style_pad_all(manual_toast, 16, 0);
    lv_obj_set_style_pad_gap(manual_toast, 8, 0);
    lv_obj_set_style_radius(manual_toast, 18, LV_PART_MAIN);
    lv_obj_set_style_border_width(manual_toast, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(manual_toast, LV_OPA_COVER, LV_PART_MAIN);

    lv_color_t base = success ? COLOR_STATUS_OK : COLOR_STATUS_ALARM;
    lv_obj_set_style_bg_color(manual_toast, base, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(manual_toast, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(manual_toast, lv_color_darken(base, 40), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(manual_toast, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(manual_toast,
                                  lv_color_mix(base, lv_color_white(), 120),
                                  LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(manual_toast, LV_OPA_60, LV_PART_MAIN);
    lv_obj_align(manual_toast, LV_ALIGN_BOTTOM_MID, 0, -24);

    lv_obj_t *label = lv_label_create(manual_toast);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, 260);

    manual_toast_timer = lv_timer_create(manual_toast_timer_cb, 2600, NULL);
}

static void show_manual_action_feedback(const terrarium_ui_t *ui, const char *action, esp_err_t status)
{
    const char *name = (ui && ui->title) ? lv_label_get_text(ui->title) : NULL;
    char fallback[16];
    if (!name || name[0] == '\0') {
        snprintf(fallback, sizeof(fallback), "T%zu", ui ? ui->index + 1U : 0U);
        name = fallback;
    }
    char message[160];
    if (status == ESP_OK) {
        snprintf(message, sizeof(message), "%s – %s manuel déclenché", name, action ? action : "Action");
        show_manual_action_toast(message, true);
    } else {
        snprintf(message, sizeof(message), "%s – échec %s (%s)",
                 name,
                 action ? action : "action",
                 esp_err_to_name(status));
        show_manual_action_toast(message, false);
    }
}

static void apply_actuator_button_style(lv_obj_t *btn, bool manual_active, bool alarm_active)
{
    if (!btn) {
        return;
    }
    lv_color_t base = alarm_active ? COLOR_STATUS_ALARM : (manual_active ? COLOR_STATUS_MANUAL : COLOR_STATUS_OK);
    lv_obj_set_style_bg_color(btn, base, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(btn, lv_color_darken(base, 40), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_darken(base, 80), LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 14, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 16, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(btn,
                                  lv_color_mix(base, lv_color_black(), 96),
                                  LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_text_color(btn, lv_color_white(), LV_PART_MAIN);
}

static void create_temp_scale(terrarium_ui_t *ui, lv_obj_t *parent)
{
    ui->temp_scale = lv_scale_create(parent);
    lv_obj_set_size(ui->temp_scale, 130, 130);
    lv_scale_set_mode(ui->temp_scale, LV_SCALE_MODE_ROUND_OUTER);
    lv_scale_set_range(ui->temp_scale, 0, 45);
    lv_scale_set_angle_range(ui->temp_scale, 270);
    lv_scale_set_rotation(ui->temp_scale, 135);
    lv_scale_set_total_tick_count(ui->temp_scale, 19);
    lv_scale_set_major_tick_every(ui->temp_scale, 2);
    lv_scale_set_label_show(ui->temp_scale, true);
    lv_obj_set_style_line_width(ui->temp_scale, 2, LV_PART_MAIN);
    lv_obj_set_style_line_color(ui->temp_scale, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);

    ui->temp_needle = lv_line_create(ui->temp_scale);
    lv_obj_remove_style_all(ui->temp_needle);
    lv_obj_remove_flag(ui->temp_needle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_line_width(ui->temp_needle, 4, LV_PART_MAIN);
    lv_obj_set_style_line_color(ui->temp_needle, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(ui->temp_needle, true, LV_PART_MAIN);
    lv_scale_set_line_needle_value(ui->temp_scale, ui->temp_needle, TEMP_NEEDLE_LENGTH, 0);
}

static void init_terrarium_ui(size_t index,
                              terrarium_ui_t *ui,
                              lv_obj_t *parent,
                              const reptile_env_terrarium_config_t *cfg)
{
    memset(ui, 0, sizeof(*ui));
    ui->index = index;
    ui->card = ui_theme_create_card(parent);
    lv_obj_set_width(ui->card, LV_PCT(100));
    lv_obj_set_style_pad_all(ui->card, 20, 0);
    lv_obj_set_style_pad_gap(ui->card, 14, 0);
    lv_obj_set_flex_flow(ui->card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(ui->card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(ui->card, COLOR_CARD_GRADIENT_TOP, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(ui->card, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(ui->card, COLOR_CARD_GRADIENT_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(ui->card, 22, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(ui->card,
                                  lv_color_mix(COLOR_AVATAR_SECONDARY, lv_color_white(), 140),
                                  LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(ui->card, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_color(ui->card, COLOR_STATUS_OK, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(ui->card, 18, LV_PART_MAIN);

    ui->header = lv_obj_create(ui->card);
    lv_obj_remove_style_all(ui->header);
    lv_obj_set_width(ui->header, LV_PCT(100));
    lv_obj_set_style_bg_opa(ui->header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(ui->header, 0, 0);
    lv_obj_set_style_pad_gap(ui->header, 12, 0);
    lv_obj_set_flex_flow(ui->header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui->header, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ui->species_avatar = lv_obj_create(ui->header);
    lv_obj_remove_style_all(ui->species_avatar);
    lv_obj_set_size(ui->species_avatar, 56, 56);
    lv_obj_set_style_radius(ui->species_avatar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui->species_avatar, COLOR_AVATAR_PRIMARY, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(ui->species_avatar, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(ui->species_avatar, COLOR_AVATAR_SECONDARY, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(ui->species_avatar, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(ui->species_avatar,
                                  lv_color_mix(COLOR_AVATAR_SECONDARY, lv_color_white(), 120),
                                  LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(ui->species_avatar, LV_OPA_40, LV_PART_MAIN);

    ui->species_icon_label = lv_label_create(ui->species_avatar);
    lv_obj_center(ui->species_icon_label);
    char avatar_text[8];
    const char *display_name = NULL;
    if (cfg && cfg->name[0] != '\0') {
        display_name = cfg->name;
    }
    format_species_avatar_text(avatar_text, sizeof(avatar_text), index, display_name);
    lv_label_set_text(ui->species_icon_label, avatar_text);
    lv_obj_set_style_text_color(ui->species_icon_label, lv_color_white(), LV_PART_MAIN);
    lv_label_set_long_mode(ui->species_icon_label, LV_LABEL_LONG_CLIP);

    ui->title = lv_label_create(ui->header);
    ui_theme_apply_title(ui->title);
    lv_label_set_text(ui->title, (cfg && cfg->name[0] != '\0') ? cfg->name : "Terrarium");
    lv_obj_set_flex_grow(ui->title, 1);

    ui->status_icon_label = lv_label_create(ui->header);
    lv_label_set_text(ui->status_icon_label, LV_SYMBOL_OK);
    lv_obj_set_style_text_color(ui->status_icon_label, COLOR_STATUS_OK, LV_PART_MAIN);

    ui->status_badge = ui_theme_create_badge(ui->header, UI_THEME_BADGE_SUCCESS, "OK");

    lv_obj_t *instrument_row = lv_obj_create(ui->card);
    lv_obj_remove_style_all(instrument_row);
    lv_obj_set_width(instrument_row, LV_PCT(100));
    lv_obj_set_style_bg_opa(instrument_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(instrument_row, 0, 0);
    lv_obj_set_style_pad_gap(instrument_row, 24, 0);
    lv_obj_set_flex_flow(instrument_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(instrument_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    create_temp_scale(ui, instrument_row);

    lv_obj_t *hum_container = lv_obj_create(instrument_row);
    lv_obj_remove_style_all(hum_container);
    lv_obj_set_flex_flow(hum_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(hum_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(hum_container, 0, 0);
    lv_obj_set_style_pad_gap(hum_container, 8, 0);
    lv_obj_set_flex_align(hum_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    lv_obj_t *hum_caption = lv_label_create(hum_container);
    ui_theme_apply_caption(hum_caption);
    lv_label_set_text(hum_caption, "Humidité");

    ui->hum_bar = lv_bar_create(hum_container);
    lv_bar_set_range(ui->hum_bar, 0, 100);
    lv_obj_set_size(ui->hum_bar, 42, 128);
    lv_bar_set_value(ui->hum_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ui->hum_bar, lv_color_hex(0xE3F2FD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->hum_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui->hum_bar, lv_palette_main(LV_PALETTE_LIGHT_BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(ui->hum_bar, LV_GRAD_DIR_VER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(ui->hum_bar, lv_palette_darken(LV_PALETTE_LIGHT_BLUE, 2), LV_PART_INDICATOR);
    lv_obj_set_style_radius(ui->hum_bar, 14, LV_PART_MAIN);
    lv_obj_set_style_radius(ui->hum_bar, 14, LV_PART_INDICATOR);

    lv_obj_t *uv_container = lv_obj_create(instrument_row);
    lv_obj_remove_style_all(uv_container);
    lv_obj_set_flex_flow(uv_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(uv_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(uv_container, 0, 0);
    lv_obj_set_style_pad_gap(uv_container, 8, 0);
    lv_obj_set_flex_align(uv_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    lv_obj_t *uv_caption = lv_label_create(uv_container);
    ui_theme_apply_caption(uv_caption);
    lv_label_set_text(uv_caption, "UV (ratio)");

    ui->uv_bar = lv_bar_create(uv_container);
    lv_bar_set_range(ui->uv_bar, 0, UV_BAR_MAX);
    lv_obj_set_size(ui->uv_bar, 200, 20);
    lv_obj_set_style_bg_color(ui->uv_bar, lv_color_hex(0xFFFDE7), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->uv_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui->uv_bar, lv_palette_main(LV_PALETTE_YELLOW), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(ui->uv_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(ui->uv_bar, lv_palette_darken(LV_PALETTE_YELLOW, 2), LV_PART_INDICATOR);
    lv_obj_set_style_radius(ui->uv_bar, 12, LV_PART_MAIN);
    lv_obj_set_style_radius(ui->uv_bar, 12, LV_PART_INDICATOR);
    lv_bar_set_value(ui->uv_bar, 0, LV_ANIM_OFF);

    ui->uv_info_label = lv_label_create(uv_container);
    ui_theme_apply_caption(ui->uv_info_label);
    lv_label_set_text(ui->uv_info_label, "UV: auto");

    lv_obj_t *controls_row = lv_obj_create(ui->card);
    lv_obj_remove_style_all(controls_row);
    lv_obj_set_width(controls_row, LV_PCT(100));
    lv_obj_set_style_bg_opa(controls_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(controls_row, 0, 0);
    lv_obj_set_style_pad_gap(controls_row, 12, 0);
    lv_obj_set_flex_flow(controls_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ui->btn_heat = create_button(controls_row, "Chauffage", heat_btn_cb, ui, &ui->btn_heat_label);
    ui->btn_pump = create_button(controls_row, "Brumiser", pump_btn_cb, ui, &ui->btn_pump_label);
    ui->btn_uv = create_button(controls_row, "UV", uv_btn_cb, ui, &ui->btn_uv_label);
    lv_obj_set_flex_grow(ui->btn_heat, 1);
    lv_obj_set_flex_grow(ui->btn_pump, 1);
    lv_obj_set_flex_grow(ui->btn_uv, 1);

    lv_obj_t *sparkline_row = lv_obj_create(ui->card);
    lv_obj_remove_style_all(sparkline_row);
    lv_obj_set_width(sparkline_row, LV_PCT(100));
    lv_obj_set_style_bg_opa(sparkline_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(sparkline_row, 0, 0);
    lv_obj_set_style_pad_gap(sparkline_row, 20, 0);
    lv_obj_set_flex_flow(sparkline_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sparkline_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *temp_spark_container = lv_obj_create(sparkline_row);
    lv_obj_remove_style_all(temp_spark_container);
    lv_obj_set_flex_flow(temp_spark_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(temp_spark_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(temp_spark_container, 0, 0);
    lv_obj_set_style_pad_gap(temp_spark_container, 6, 0);
    lv_obj_set_flex_grow(temp_spark_container, 1);

    lv_obj_t *temp_spark_label = lv_label_create(temp_spark_container);
    ui_theme_apply_caption(temp_spark_label);
    lv_label_set_text(temp_spark_label, "Historique Temp.");

    ui->sparkline_temp = lv_chart_create(temp_spark_container);
    lv_chart_set_point_count(ui->sparkline_temp, SPARKLINE_POINT_COUNT);
    lv_chart_set_range(ui->sparkline_temp, LV_CHART_AXIS_PRIMARY_Y, 0, 45);
    lv_chart_set_div_line_count(ui->sparkline_temp, 0, 0);
    lv_chart_set_type(ui->sparkline_temp, LV_CHART_TYPE_LINE);
    lv_obj_set_height(ui->sparkline_temp, 70);
    lv_obj_set_style_bg_opa(ui->sparkline_temp, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->sparkline_temp, 0, LV_PART_MAIN);
    lv_obj_set_style_line_width(ui->sparkline_temp, 3, LV_PART_ITEMS);
    ui->sparkline_temp_series = lv_chart_add_series(ui->sparkline_temp, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_ext_y_array(ui->sparkline_temp, ui->sparkline_temp_series, ui->temp_sparkline_points);

    lv_obj_t *hum_spark_container = lv_obj_create(sparkline_row);
    lv_obj_remove_style_all(hum_spark_container);
    lv_obj_set_flex_flow(hum_spark_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(hum_spark_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(hum_spark_container, 0, 0);
    lv_obj_set_style_pad_gap(hum_spark_container, 6, 0);
    lv_obj_set_flex_grow(hum_spark_container, 1);

    lv_obj_t *hum_spark_label = lv_label_create(hum_spark_container);
    ui_theme_apply_caption(hum_spark_label);
    lv_label_set_text(hum_spark_label, "Historique Hum.");

    ui->sparkline_hum = lv_chart_create(hum_spark_container);
    lv_chart_set_point_count(ui->sparkline_hum, SPARKLINE_POINT_COUNT);
    lv_chart_set_range(ui->sparkline_hum, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(ui->sparkline_hum, 0, 0);
    lv_chart_set_type(ui->sparkline_hum, LV_CHART_TYPE_LINE);
    lv_obj_set_height(ui->sparkline_hum, 70);
    lv_obj_set_style_bg_opa(ui->sparkline_hum, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->sparkline_hum, 0, LV_PART_MAIN);
    lv_obj_set_style_line_width(ui->sparkline_hum, 3, LV_PART_ITEMS);
    ui->sparkline_hum_series = lv_chart_add_series(ui->sparkline_hum, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_ext_y_array(ui->sparkline_hum, ui->sparkline_hum_series, ui->hum_sparkline_points);

    ui->metrics_label = lv_label_create(ui->card);
    ui_theme_apply_body(ui->metrics_label);
    lv_label_set_text(ui->metrics_label, "");

    ui->energy_label = lv_label_create(ui->card);
    ui_theme_apply_body(ui->energy_label);
    lv_label_set_text(ui->energy_label, "");

    ui->alarm_label = lv_label_create(ui->card);
    ui_theme_apply_body(ui->alarm_label);
    lv_label_set_text(ui->alarm_label, "");

    ui->history_chart = lv_chart_create(ui->card);
    lv_chart_set_point_count(ui->history_chart, CHART_POINT_COUNT);
    lv_chart_set_range(ui->history_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 45);
    lv_chart_set_range(ui->history_chart, LV_CHART_AXIS_SECONDARY_Y, 0, 100);
    lv_chart_set_div_line_count(ui->history_chart, 4, 6);
    lv_chart_set_type(ui->history_chart, LV_CHART_TYPE_LINE);
    lv_obj_set_height(ui->history_chart, 160);
    lv_obj_set_style_bg_opa(ui->history_chart, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->history_chart, 0, LV_PART_MAIN);
    ui->history_temp_series = lv_chart_add_series(ui->history_chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    ui->history_hum_series = lv_chart_add_series(ui->history_chart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_SECONDARY_Y);
    lv_chart_set_ext_y_array(ui->history_chart, ui->history_temp_series, ui->temp_points);
    lv_chart_set_ext_y_array(ui->history_chart, ui->history_hum_series, ui->hum_points);

    for (size_t i = 0; i < CHART_POINT_COUNT; ++i) {
        ui->temp_points[i] = LV_CHART_POINT_NONE;
        ui->hum_points[i] = LV_CHART_POINT_NONE;
    }
    for (size_t i = 0; i < SPARKLINE_POINT_COUNT; ++i) {
        ui->temp_sparkline_points[i] = LV_CHART_POINT_NONE;
        ui->hum_sparkline_points[i] = LV_CHART_POINT_NONE;
    }

    apply_actuator_button_style(ui->btn_heat, false, false);
    apply_actuator_button_style(ui->btn_pump, false, false);
    apply_actuator_button_style(ui->btn_uv, false, false);
}

static void describe_alarms(uint32_t flags, char *buffer, size_t len)
{
    if (flags == REPTILE_ENV_ALARM_NONE) {
        snprintf(buffer, len, "Aucune alarme");
        return;
    }
    buffer[0] = '\0';
    if (flags & REPTILE_ENV_ALARM_SENSOR_FAILURE) {
        strncat(buffer, "Capteur ", len - strlen(buffer) - 1);
    }
    if (flags & REPTILE_ENV_ALARM_TEMP_LOW) {
        strncat(buffer, "Temp basse ", len - strlen(buffer) - 1);
    }
    if (flags & REPTILE_ENV_ALARM_TEMP_HIGH) {
        strncat(buffer, "Temp haute ", len - strlen(buffer) - 1);
    }
    if (flags & REPTILE_ENV_ALARM_HUM_LOW) {
        strncat(buffer, "Hum basse ", len - strlen(buffer) - 1);
    }
    if (flags & REPTILE_ENV_ALARM_HUM_HIGH) {
        strncat(buffer, "Hum haute ", len - strlen(buffer) - 1);
    }
    if (flags & REPTILE_ENV_ALARM_LIGHT_LOW) {
        strncat(buffer, "Lum basse ", len - strlen(buffer) - 1);
    }
}

static void fill_chart_buffers(lv_coord_t *temp_buffer,
                               lv_coord_t *hum_buffer,
                               size_t buffer_len,
                               size_t start_index,
                               size_t sample_count)
{
    if (!temp_buffer || !hum_buffer) {
        return;
    }
    size_t out_idx = 0;
    for (size_t i = start_index; i < sample_count && out_idx < buffer_len; ++i, ++out_idx) {
        float temp = s_history_buf[i].temperature_c;
        float hum = s_history_buf[i].humidity_pct;
        if (!isfinite(temp)) {
            temp_buffer[out_idx] = LV_CHART_POINT_NONE;
        } else {
            if (temp < 0.0f) {
                temp = 0.0f;
            }
            if (temp > 45.0f) {
                temp = 45.0f;
            }
            temp_buffer[out_idx] = (lv_coord_t)lroundf(temp);
        }
        if (!isfinite(hum)) {
            hum_buffer[out_idx] = LV_CHART_POINT_NONE;
        } else {
            if (hum < 0.0f) {
                hum = 0.0f;
            }
            if (hum > 100.0f) {
                hum = 100.0f;
            }
            hum_buffer[out_idx] = (lv_coord_t)lroundf(hum);
        }
    }
    for (; out_idx < buffer_len; ++out_idx) {
        temp_buffer[out_idx] = LV_CHART_POINT_NONE;
        hum_buffer[out_idx] = LV_CHART_POINT_NONE;
    }
}

static bool ensure_history_buffer(void)
{
    if (s_history_buf) {
        return true;
    }

    const size_t desired = REPTILE_ENV_HISTORY_LENGTH;
    const size_t min_capacity = CHART_POINT_COUNT > SPARKLINE_POINT_COUNT
                                    ? CHART_POINT_COUNT
                                    : SPARKLINE_POINT_COUNT;
    static const uint32_t caps_priority[] = {
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
    };

    size_t entries = desired;
    while (entries >= min_capacity) {
        for (size_t i = 0; i < sizeof(caps_priority) / sizeof(caps_priority[0]); ++i) {
            reptile_env_history_entry_t *buf = heap_caps_calloc(entries, sizeof(*buf), caps_priority[i]);
            if (buf) {
                s_history_buf = buf;
                s_history_capacity = entries;
                if (entries < desired) {
                    ESP_LOGW(TAG,
                             "Tampon historique réduit à %zu échantillons (demande %zu)",
                             entries,
                             desired);
                }
                return true;
            }
        }
        if (entries == min_capacity) {
            break;
        }
        entries /= 2;
        if (entries < min_capacity) {
            entries = min_capacity;
        }
    }

    ESP_LOGE(TAG,
             "Allocation du tampon historique impossible (%zu échantillons)",
             (size_t)REPTILE_ENV_HISTORY_LENGTH);
    return false;
}

static void update_chart(terrarium_ui_t *ui, size_t index)
{
    if (!s_history_buf && !ensure_history_buffer()) {
        return;
    }
    size_t capacity = s_history_capacity ? s_history_capacity : REPTILE_ENV_HISTORY_LENGTH;
    size_t count = reptile_env_get_history(index, s_history_buf, capacity);
    size_t start = 0;
    if (count > CHART_POINT_COUNT) {
        start = count - CHART_POINT_COUNT;
    }
    fill_chart_buffers(ui->temp_points, ui->hum_points, CHART_POINT_COUNT, start, count);

    size_t spark_start = 0;
    if (count > SPARKLINE_POINT_COUNT) {
        spark_start = count - SPARKLINE_POINT_COUNT;
    }
    fill_chart_buffers(ui->temp_sparkline_points,
                       ui->hum_sparkline_points,
                       SPARKLINE_POINT_COUNT,
                       spark_start,
                       count);

    if (ui->history_chart) {
        lv_chart_refresh(ui->history_chart);
    }
    if (ui->sparkline_temp) {
        lv_chart_refresh(ui->sparkline_temp);
    }
    if (ui->sparkline_hum) {
        lv_chart_refresh(ui->sparkline_hum);
    }
}

static void refresh_status_header(terrarium_ui_t *ui, const reptile_env_terrarium_state_t *state)
{
    if (!ui || !state) {
        return;
    }
    bool alarm_active = state->alarm_flags != REPTILE_ENV_ALARM_NONE;
    bool manual_active = state->manual_heat || state->manual_pump || state->manual_uv_override;
    const char *status_text = "OK";
    const char *status_icon = LV_SYMBOL_OK;
    lv_color_t status_color = COLOR_STATUS_OK;
    ui_theme_badge_kind_t badge_kind = UI_THEME_BADGE_SUCCESS;

    if (alarm_active) {
        status_text = "Alerte";
        status_icon = LV_SYMBOL_WARNING;
        status_color = COLOR_STATUS_ALARM;
        badge_kind = UI_THEME_BADGE_CRITICAL;
    } else if (manual_active) {
        status_text = "Manuel";
        status_icon = LV_SYMBOL_SETTINGS;
        status_color = COLOR_STATUS_MANUAL;
        badge_kind = UI_THEME_BADGE_WARNING;
    }

    if (ui->status_badge) {
        lv_label_set_text(ui->status_badge, status_text);
        ui_theme_badge_set_kind(ui->status_badge, badge_kind);
    }
    if (ui->status_icon_label) {
        lv_label_set_text(ui->status_icon_label, status_icon);
        lv_obj_set_style_text_color(ui->status_icon_label, status_color, LV_PART_MAIN);
    }
    if (ui->card) {
        lv_obj_set_style_border_width(ui->card, alarm_active ? 3 : (manual_active ? 2 : 1), LV_PART_MAIN);
        lv_obj_set_style_border_color(ui->card, status_color, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(ui->card,
                                      lv_color_mix(status_color, lv_color_white(), 160),
                                      LV_PART_MAIN);
    }
}

static void update_summary_panel(void)
{
    if (!summary_energy_label || !summary_alarm_label) {
        return;
    }

    bool any_valid = false;
    float total_heat = 0.0f;
    float total_pump = 0.0f;
    float total_uv = 0.0f;
    uint32_t alarm_count = 0;
    char alarm_details[256];
    alarm_details[0] = '\0';

    for (size_t i = 0; i < s_ui_count; ++i) {
        if (!s_state_valid[i]) {
            continue;
        }
        any_valid = true;
        const reptile_env_terrarium_state_t *state = &s_last_states[i];
        total_heat += state->energy_heat_Wh;
        total_pump += state->energy_pump_Wh;
        total_uv += state->energy_uv_Wh;
        if (state->alarm_flags != REPTILE_ENV_ALARM_NONE) {
            if (alarm_details[0] != '\0') {
                strncat(alarm_details, ", ", sizeof(alarm_details) - strlen(alarm_details) - 1);
            }
            const char *name = (s_ui[i].title) ? lv_label_get_text(s_ui[i].title) : NULL;
            char fallback[16];
            if (!name || name[0] == '\0') {
                snprintf(fallback, sizeof(fallback), "T%zu", i + 1U);
                name = fallback;
            }
            size_t remaining = sizeof(alarm_details) - strlen(alarm_details) - 1;
            if (remaining > 0) {
                strncat(alarm_details, name, remaining);
            }
            alarm_count++;
        }
    }

    if (any_valid) {
        float total = total_heat + total_pump + total_uv;
        lv_label_set_text_fmt(summary_energy_label,
                              "Énergie totale: %.2f Wh\nChauffage %.2f / Pompe %.2f / UV %.2f",
                              total,
                              total_heat,
                              total_pump,
                              total_uv);
    } else {
        lv_label_set_text(summary_energy_label,
                          "Énergie totale: -- Wh\nChauffage -- / Pompe -- / UV --");
    }

    if (s_emergency_engaged) {
        lv_label_set_text(summary_alarm_label, alarm_count > 0 ? "Arrêt d'urgence ACTIF + alarmes" : "Arrêt d'urgence ACTIF");
        if (alarm_count > 0 && alarm_details[0] != '\0') {
            lv_label_set_text_fmt(summary_alarm_label,
                                  "Arrêt d'urgence ACTIF\nAlarmes (%" PRIu32 "): %s",
                                  alarm_count,
                                  alarm_details);
        }
        lv_obj_set_style_text_color(summary_alarm_label, COLOR_STATUS_ALARM, LV_PART_MAIN);
        if (summary_panel) {
            lv_obj_set_style_border_color(summary_panel, COLOR_STATUS_ALARM, LV_PART_MAIN);
            lv_obj_set_style_shadow_color(summary_panel,
                                          lv_color_mix(COLOR_STATUS_ALARM, lv_color_white(), 150),
                                          LV_PART_MAIN);
        }
        return;
    }

    if (alarm_count == 0) {
        lv_label_set_text(summary_alarm_label, "Alarmes actives: aucune");
        lv_obj_set_style_text_color(summary_alarm_label, COLOR_STATUS_OK, LV_PART_MAIN);
        if (summary_panel) {
            lv_obj_set_style_border_color(summary_panel, COLOR_STATUS_OK, LV_PART_MAIN);
            lv_obj_set_style_shadow_color(summary_panel,
                                          lv_color_mix(COLOR_STATUS_OK, lv_color_white(), 170),
                                          LV_PART_MAIN);
        }
    } else {
        if (alarm_details[0] != '\0') {
            lv_label_set_text_fmt(summary_alarm_label,
                                  "Alarmes (%" PRIu32 "): %s",
                                  alarm_count,
                                  alarm_details);
        } else {
            lv_label_set_text_fmt(summary_alarm_label,
                                  "Alarmes actives: %" PRIu32,
                                  alarm_count);
        }
        lv_obj_set_style_text_color(summary_alarm_label, COLOR_STATUS_ALARM, LV_PART_MAIN);
        if (summary_panel) {
            lv_obj_set_style_border_color(summary_panel, COLOR_STATUS_ALARM, LV_PART_MAIN);
            lv_obj_set_style_shadow_color(summary_panel,
                                          lv_color_mix(COLOR_STATUS_ALARM, lv_color_white(), 150),
                                          LV_PART_MAIN);
        }
    }
}

static void update_terrarium_ui(terrarium_ui_t *ui, const reptile_env_terrarium_state_t *state)
{
    if (!ui || !state) {
        return;
    }
    if (state->temperature_valid) {
        float temp = state->temperature_c;
        if (temp < 0) {
            temp = 0;
        }
        if (temp > 45) {
            temp = 45;
        }
        if (ui->temp_scale && ui->temp_needle) {
            lv_scale_set_line_needle_value(ui->temp_scale,
                                           ui->temp_needle,
                                           TEMP_NEEDLE_LENGTH,
                                           (int32_t)lroundf(temp));
        }
    }
    if (state->humidity_valid) {
        float hum = state->humidity_pct;
        if (hum < 0) {
            hum = 0;
        }
        if (hum > 100) {
            hum = 100;
        }
        lv_bar_set_value(ui->hum_bar, (int32_t)lroundf(hum), LV_ANIM_OFF);
    } else {
        lv_bar_set_value(ui->hum_bar, 0, LV_ANIM_OFF);
    }

    char temp_str[16];
    char hum_str[16];
    char lux_str[16];
    char target_lux_str[16];
    if (state->temperature_valid && isfinite(state->temperature_c)) {
        snprintf(temp_str, sizeof(temp_str), "%.1f", state->temperature_c);
    } else {
        snprintf(temp_str, sizeof(temp_str), "N/A");
    }
    if (state->humidity_valid && isfinite(state->humidity_pct)) {
        snprintf(hum_str, sizeof(hum_str), "%.1f", state->humidity_pct);
    } else {
        snprintf(hum_str, sizeof(hum_str), "N/A");
    }
    if (state->light_valid && isfinite(state->light_lux)) {
        snprintf(lux_str, sizeof(lux_str), "%.1f", state->light_lux);
    } else {
        snprintf(lux_str, sizeof(lux_str), "N/A");
    }
    if (state->target_light_lux > 0.0f) {
        snprintf(target_lux_str, sizeof(target_lux_str), "%.0f", state->target_light_lux);
    } else {
        snprintf(target_lux_str, sizeof(target_lux_str), "OFF");
    }

    lv_label_set_text_fmt(ui->metrics_label,
                          "Temp %s/%.1f°C  Hum %s/%.1f%%  Lum %s/%s lx\nChauffage %s  Pompe %s",
                          temp_str,
                          state->target_temperature_c,
                          hum_str,
                          state->target_humidity_pct,
                          lux_str,
                          target_lux_str,
                          state->heating ? "ON" : "OFF",
                          state->pumping ? "ON" : "OFF");

    float total = state->energy_heat_Wh + state->energy_pump_Wh + state->energy_uv_Wh;
    lv_label_set_text_fmt(ui->energy_label,
                          "Énergie: %.2f Wh (Chauffage %.2f / Pompe %.2f / UV %.2f)",
                          total,
                          state->energy_heat_Wh,
                          state->energy_pump_Wh,
                          state->energy_uv_Wh);

    char alarm_text[128];
    describe_alarms(state->alarm_flags, alarm_text, sizeof(alarm_text));
    lv_label_set_text(ui->alarm_label, alarm_text);
    lv_obj_set_style_text_color(ui->alarm_label,
                                state->alarm_flags != REPTILE_ENV_ALARM_NONE ? COLOR_STATUS_ALARM : COLOR_STATUS_OK,
                                LV_PART_MAIN);

    bool heat_alarm = (state->alarm_flags & (REPTILE_ENV_ALARM_SENSOR_FAILURE | REPTILE_ENV_ALARM_TEMP_LOW |
                                             REPTILE_ENV_ALARM_TEMP_HIGH)) != 0;
    bool pump_alarm = (state->alarm_flags & (REPTILE_ENV_ALARM_SENSOR_FAILURE | REPTILE_ENV_ALARM_HUM_LOW |
                                             REPTILE_ENV_ALARM_HUM_HIGH)) != 0;
    bool uv_alarm = (state->alarm_flags & (REPTILE_ENV_ALARM_SENSOR_FAILURE | REPTILE_ENV_ALARM_LIGHT_LOW)) != 0;

    if (ui->btn_heat_label) {
        lv_label_set_text_fmt(ui->btn_heat_label,
                              state->manual_heat ? "Chauffage (man)" : (state->heating ? "Chauffage (actif)" : "Chauffage"));
    }
    if (ui->btn_pump_label) {
        lv_label_set_text_fmt(ui->btn_pump_label,
                              state->manual_pump ? "Brumiser (man)" : (state->pumping ? "Brumiser (actif)" : "Brumiser"));
    }
    if (ui->btn_uv_label) {
        lv_label_set_text_fmt(ui->btn_uv_label,
                              state->manual_uv_override ? "UV (manuel)" : (state->uv_light ? "UV (actif)" : "UV"));
    }

    apply_actuator_button_style(ui->btn_heat, state->manual_heat, heat_alarm);
    apply_actuator_button_style(ui->btn_pump, state->manual_pump, pump_alarm);
    apply_actuator_button_style(ui->btn_uv, state->manual_uv_override, uv_alarm);

    float uv_percent = 0.0f;
    if (state->target_light_lux > 0.0f) {
        if (state->light_valid && isfinite(state->light_lux)) {
            uv_percent = (state->light_lux / state->target_light_lux) * 100.0f;
        } else {
            uv_percent = state->uv_light ? 100.0f : 0.0f;
        }
    } else {
        uv_percent = state->uv_light ? 100.0f : 0.0f;
    }
    if (uv_percent < 0.0f) {
        uv_percent = 0.0f;
    }
    if (uv_percent > (float)UV_BAR_MAX) {
        uv_percent = (float)UV_BAR_MAX;
    }

    if (ui->uv_bar) {
        lv_color_t uv_color = uv_alarm ? COLOR_STATUS_ALARM
                                       : (state->manual_uv_override ? COLOR_STATUS_MANUAL
                                                                    : lv_palette_main(LV_PALETTE_YELLOW));
        lv_bar_set_value(ui->uv_bar, (int32_t)lroundf(uv_percent), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(ui->uv_bar, uv_color, LV_PART_INDICATOR);
        lv_obj_set_style_bg_grad_color(ui->uv_bar, lv_color_darken(uv_color, 40), LV_PART_INDICATOR);
    }
    if (ui->uv_info_label) {
        if (state->target_light_lux > 0.0f && state->light_valid && isfinite(state->light_lux)) {
            lv_label_set_text_fmt(ui->uv_info_label,
                                  "UV: %s (%s) %.0f%% (%.0f/%.0f lx)",
                                  state->uv_light ? "ON" : "OFF",
                                  state->manual_uv_override ? "manuel" : "auto",
                                  uv_percent,
                                  state->light_lux,
                                  state->target_light_lux);
        } else if (state->target_light_lux > 0.0f) {
            lv_label_set_text_fmt(ui->uv_info_label,
                                  "UV: %s (%s) cible %.0f lx",
                                  state->uv_light ? "ON" : "OFF",
                                  state->manual_uv_override ? "manuel" : "auto",
                                  state->target_light_lux);
        } else {
            lv_label_set_text_fmt(ui->uv_info_label,
                                  "UV: %s (%s) cible OFF",
                                  state->uv_light ? "ON" : "OFF",
                                  state->manual_uv_override ? "manuel" : "auto");
        }
    }

    refresh_status_header(ui, state);

    update_chart(ui, ui->index);

    s_last_states[ui->index] = *state;
    s_state_valid[ui->index] = true;
    update_summary_panel();
}

static void env_state_cb(size_t index, const reptile_env_terrarium_state_t *state, void *ctx)
{
    (void)ctx;
    logging_real_append(index, state);
    if (!lvgl_port_lock(-1)) {
        return;
    }
    if (index < s_ui_count) {
        update_terrarium_ui(&s_ui[index], state);
    }
    lvgl_port_unlock();
}

static void emergency_stop_cb(lv_event_t *e)
{
    (void)e;
    if (s_emergency_engaged) {
        show_manual_action_toast("Arrêt d'urgence déjà actif", false);
        return;
    }
    s_emergency_engaged = true;

    reptile_env_stop();
    logging_real_stop();
    reptile_actuators_deinit();

    if (feed_task_handle) {
        vTaskDelete(feed_task_handle);
        feed_task_handle = NULL;
        feed_running = false;
        if (lvgl_port_lock(-1)) {
            update_feed_status();
            lvgl_port_unlock();
        }
    }

    if (emergency_button) {
        lv_obj_add_state(emergency_button, LV_STATE_DISABLED);
    }

    show_manual_action_toast("Arrêt d'urgence déclenché", false);
    update_summary_panel();
}

static void pump_btn_cb(lv_event_t *e)
{
    terrarium_ui_t *ui = lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    esp_err_t err = reptile_env_manual_pump(ui->index);
    show_manual_action_feedback(ui, "Brumisation", err);
}

static void heat_btn_cb(lv_event_t *e)
{
    terrarium_ui_t *ui = lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    esp_err_t err = reptile_env_manual_heat(ui->index);
    show_manual_action_feedback(ui, "Chauffage", err);
}

static void uv_btn_cb(lv_event_t *e)
{
    terrarium_ui_t *ui = lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    esp_err_t err = reptile_env_manual_uv_toggle(ui->index);
    show_manual_action_feedback(ui, "UV", err);
}

static void feed_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!feed_running) {
        xTaskCreate(feed_task, "feed_task", 2048, NULL, 5, &feed_task_handle);
    }
}

static void menu_btn_cb(lv_event_t *e)
{
    (void)e;
    reptile_env_stop();
    logging_real_stop();
    sensors_deinit();
    if (feed_task_handle) {
        vTaskDelete(feed_task_handle);
        feed_task_handle = NULL;
        feed_running = false;
    }
    reptile_actuators_deinit();

    if (manual_toast_timer) {
        lv_timer_del(manual_toast_timer);
        manual_toast_timer = NULL;
    }
    if (manual_toast && lv_obj_is_valid(manual_toast)) {
        lv_obj_del(manual_toast);
    }
    manual_toast = NULL;
    summary_panel = NULL;
    summary_energy_label = NULL;
    summary_alarm_label = NULL;
    emergency_button = NULL;
    s_emergency_engaged = false;
    memset(s_state_valid, 0, sizeof(s_state_valid));

    if (lvgl_port_lock(-1)) {
        lv_scr_load(menu_screen);
        if (screen) {
            lv_obj_del(screen);
            screen = NULL;
        }
        lvgl_port_unlock();
    }
}

void reptile_real_start(esp_lcd_panel_handle_t panel, esp_lcd_touch_handle_t tp)
{
    (void)panel;
    (void)tp;

    if (!ensure_history_buffer()) {
        ESP_LOGE(TAG, "Historique non disponible: mémoire insuffisante");
    }

    if (!lvgl_port_lock(-1)) {
        return;
    }

    screen = lv_obj_create(NULL);
    ui_theme_apply_screen(screen);
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(screen, 16, 0);
    lv_obj_set_style_pad_gap(screen, 14, 0);

    lv_obj_t *header = ui_theme_create_card(screen);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_style_pad_all(header, 16, 0);
    lv_obj_set_style_pad_gap(header, 12, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_scrollbar_mode(header, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = lv_label_create(header);
    ui_theme_apply_title(title);
    lv_label_set_text(title, "Mode réel");

    lv_obj_t *spacer = lv_obj_create(header);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_flex_grow(spacer, 1);

    create_button(header, "Menu", menu_btn_cb, NULL, NULL);

    feed_status_label = lv_label_create(screen);
    ui_theme_apply_body(feed_status_label);
    update_feed_status();

    lv_obj_t *feed_btn = ui_theme_create_button(screen, "Nourrir",
                                                UI_THEME_BUTTON_PRIMARY,
                                                feed_btn_cb, NULL);
    lv_obj_set_width(feed_btn, 200);

    lv_obj_t *content = lv_obj_create(screen);
    lv_obj_remove_style_all(content);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_gap(content, 20, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *terrarium_column = lv_obj_create(content);
    lv_obj_remove_style_all(terrarium_column);
    lv_obj_set_style_bg_opa(terrarium_column, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(terrarium_column, 0, 0);
    lv_obj_set_style_pad_gap(terrarium_column, 18, 0);
    lv_obj_set_flex_flow(terrarium_column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(terrarium_column, 1);

    summary_panel = ui_theme_create_card(content);
    lv_obj_set_width(summary_panel, 320);
    lv_obj_set_style_pad_all(summary_panel, 18, 0);
    lv_obj_set_style_pad_gap(summary_panel, 14, 0);
    lv_obj_set_flex_flow(summary_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_grad_dir(summary_panel, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(summary_panel, lv_color_hex(0xF8F3ED), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(summary_panel, lv_color_hex(0xF1FBF5), LV_PART_MAIN);
    lv_obj_set_style_border_color(summary_panel, COLOR_STATUS_OK, LV_PART_MAIN);

    lv_obj_t *summary_title = lv_label_create(summary_panel);
    ui_theme_apply_title(summary_title);
    lv_label_set_text(summary_title, "Synthèse exploitation");

    summary_energy_label = lv_label_create(summary_panel);
    ui_theme_apply_body(summary_energy_label);
    lv_label_set_text(summary_energy_label, "Énergie totale: -- Wh\nChauffage -- / Pompe -- / UV --");

    summary_alarm_label = lv_label_create(summary_panel);
    ui_theme_apply_body(summary_alarm_label);
    lv_label_set_text(summary_alarm_label, "Alarmes actives: --");

    lv_obj_t *summary_divider = lv_obj_create(summary_panel);
    lv_obj_remove_style_all(summary_divider);
    lv_obj_set_height(summary_divider, 2);
    lv_obj_set_width(summary_divider, LV_PCT(100));
    lv_obj_set_style_bg_color(summary_divider, lv_color_hex(0xD7E5DC), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(summary_divider, LV_OPA_60, LV_PART_MAIN);

    emergency_button = lv_btn_create(summary_panel);
    lv_obj_remove_style_all(emergency_button);
    lv_obj_set_style_bg_color(emergency_button, COLOR_STATUS_ALARM, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(emergency_button, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(emergency_button, lv_color_darken(COLOR_STATUS_ALARM, 40), LV_PART_MAIN);
    lv_obj_set_style_radius(emergency_button, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_all(emergency_button, 18, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(emergency_button, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(emergency_button,
                                  lv_color_mix(COLOR_STATUS_ALARM, lv_color_white(), 120),
                                  LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(emergency_button, LV_OPA_50, LV_PART_MAIN);
    lv_obj_add_event_cb(emergency_button, emergency_stop_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *em_label = lv_label_create(emergency_button);
    lv_label_set_text(em_label, LV_SYMBOL_WARNING " Arrêt d'urgence");
    lv_obj_set_style_text_color(em_label, lv_color_white(), LV_PART_MAIN);
    lv_label_set_long_mode(em_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(em_label, LV_PCT(100));

    const reptile_env_config_t *cfg = &g_settings.env_config;
    s_ui_count = cfg->terrarium_count;
    if (s_ui_count > REPTILE_ENV_MAX_TERRARIUMS) {
        s_ui_count = REPTILE_ENV_MAX_TERRARIUMS;
    }

    memset(s_last_states, 0, sizeof(s_last_states));
    memset(s_state_valid, 0, sizeof(s_state_valid));
    s_emergency_engaged = false;

    for (size_t i = 0; i < s_ui_count; ++i) {
        init_terrarium_ui(i, &s_ui[i], terrarium_column, &cfg->terrarium[i]);
    }

    update_summary_panel();

    lv_disp_load_scr(screen);
    lvgl_port_unlock();

    logging_real_start(s_ui_count, cfg);
    reptile_env_start(cfg, env_state_cb, NULL);
}

