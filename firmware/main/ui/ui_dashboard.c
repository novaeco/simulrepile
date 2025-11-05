#include "ui/ui_dashboard.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "sim/sim_engine.h"
#include "ui/ui_theme.h"

#define UI_DASHBOARD_MAX_TERRARIUMS CONFIG_APP_MAX_TERRARIUMS

typedef struct {
    lv_obj_t *card;
    lv_obj_t *title;
    lv_obj_t *temperature;
    lv_obj_t *humidity;
    lv_obj_t *stress;
    lv_obj_t *activity;
} terrarium_card_t;

static const char *TAG = "ui_dashboard";

static lv_obj_t *s_container = NULL;
static terrarium_card_t s_cards[UI_DASHBOARD_MAX_TERRARIUMS];

static void ui_dashboard_create_card(size_t index);
static const terrarium_state_t *ui_dashboard_get_state(size_t index, const terrarium_state_t *first_state);

void ui_dashboard_create(lv_obj_t *parent)
{
    if (!parent) {
        return;
    }

    ESP_LOGI(TAG, "Creating dashboard widgets");

    s_container = lv_obj_create(parent);
    lv_obj_set_size(s_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_container, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(s_container, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_container, 24, LV_PART_MAIN);
    lv_obj_set_style_pad_column(s_container, 24, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_container, LV_OPA_TRANSP, LV_PART_MAIN);

    for (size_t i = 0; i < UI_DASHBOARD_MAX_TERRARIUMS; ++i) {
        ui_dashboard_create_card(i);
    }
}

void ui_dashboard_refresh(size_t terrarium_count, const terrarium_state_t *first_state)
{
    if (!s_container) {
        return;
    }

    for (size_t i = 0; i < UI_DASHBOARD_MAX_TERRARIUMS; ++i) {
        terrarium_card_t *card = &s_cards[i];
        if (!card->card) {
            continue;
        }

        if (i >= terrarium_count) {
            lv_obj_add_flag(card->card, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        const terrarium_state_t *state = ui_dashboard_get_state(i, first_state);
        if (!state) {
            lv_obj_add_flag(card->card, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        const char *name = (state->profile && state->profile->common_name) ? state->profile->common_name : "Terrarium";
        lv_label_set_text_fmt(card->title, "#%u %s", (unsigned)(i + 1), name);

        float temperature = state->current_environment.temp_day_c;
        float humidity = state->current_environment.humidity_day_pct;
        float stress = state->health.stress_pct;
        float activity_pct = state->activity_score * 100.0f;

        if (humidity < 0.0f) {
            humidity = 0.0f;
        }
        if (humidity > 100.0f) {
            humidity = 100.0f;
        }
        if (stress < 0.0f) {
            stress = 0.0f;
        }
        if (stress > 100.0f) {
            stress = 100.0f;
        }
        if (activity_pct < 0.0f) {
            activity_pct = 0.0f;
        }
        if (activity_pct > 100.0f) {
            activity_pct = 100.0f;
        }

        lv_label_set_text_fmt(card->temperature, "Température : %.1f °C", temperature);
        lv_label_set_text_fmt(card->humidity, "Hygrométrie : %.0f %%", humidity);
        lv_label_set_text_fmt(card->stress, "Stress : %.0f %%", stress);
        lv_label_set_text_fmt(card->activity, "Activité : %.0f %%", activity_pct);

        lv_obj_clear_flag(card->card, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_dashboard_create_card(size_t index)
{
    terrarium_card_t *card = &s_cards[index];

    card->card = lv_obj_create(s_container);
    lv_obj_set_width(card->card, LV_PCT(47));
    lv_obj_set_style_min_height(card->card, 200, LV_PART_MAIN);
    lv_obj_set_flex_flow(card->card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card->card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    ui_theme_apply_panel_style(card->card);

    card->title = lv_label_create(card->card);
    ui_theme_apply_label_style(card->title, true);
    lv_label_set_long_mode(card->title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(card->title, LV_PCT(100));

    card->temperature = lv_label_create(card->card);
    ui_theme_apply_label_style(card->temperature, false);

    card->humidity = lv_label_create(card->card);
    ui_theme_apply_label_style(card->humidity, false);

    card->stress = lv_label_create(card->card);
    ui_theme_apply_label_style(card->stress, false);

    card->activity = lv_label_create(card->card);
    ui_theme_apply_label_style(card->activity, false);

    lv_obj_add_flag(card->card, LV_OBJ_FLAG_HIDDEN);
}

static const terrarium_state_t *ui_dashboard_get_state(size_t index, const terrarium_state_t *first_state)
{
    if (first_state) {
        return &first_state[index];
    }
    return sim_engine_get_state(index);
}
