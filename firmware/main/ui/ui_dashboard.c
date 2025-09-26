#include "ui_dashboard.h"

#include "i18n.h"
#include "sim/sim_engine.h"
#include "ui_theme.h"

#include <stdio.h>

typedef struct {
    lv_obj_t *card;
    lv_obj_t *title;
    lv_obj_t *environment;
    lv_obj_t *health;
    lv_obj_t *care;
} dashboard_widgets_t;

static lv_obj_t *s_container;
static dashboard_widgets_t s_widgets[APP_MAX_TERRARIUMS];

static void populate_card(dashboard_widgets_t *widget)
{
    widget->card = lv_obj_create(s_container);
    lv_obj_set_size(widget->card, LV_PCT(48), 220);
    ui_theme_style_panel(widget->card);
    lv_obj_set_style_pad_all(widget->card, 10, 0);
    lv_obj_set_flex_flow(widget->card, LV_FLEX_FLOW_COLUMN);

    widget->title = lv_label_create(widget->card);
    lv_obj_set_style_text_font(widget->title, LV_FONT_DEFAULT, 0);

    widget->environment = lv_label_create(widget->card);
    widget->health = lv_label_create(widget->card);
    widget->care = lv_label_create(widget->card);
}

void ui_dashboard_init(lv_obj_t *parent)
{
    s_container = lv_obj_create(parent);
    lv_obj_set_size(s_container, LV_PCT(100), LV_PCT(60));
    lv_obj_align(s_container, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(s_container, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_all(s_container, 10, 0);
    lv_obj_set_style_pad_gap(s_container, 10, 0);
    ui_theme_style_panel(s_container);

    for (size_t i = 0; i < APP_MAX_TERRARIUMS; ++i) {
        populate_card(&s_widgets[i]);
        lv_label_set_text(s_widgets[i].title, "");
        lv_label_set_text(s_widgets[i].environment, "");
        lv_label_set_text(s_widgets[i].health, "");
        lv_label_set_text(s_widgets[i].care, "");
    }
}

void ui_dashboard_show(size_t terrarium_index)
{
    (void)terrarium_index;
    lv_obj_clear_flag(s_container, LV_OBJ_FLAG_HIDDEN);
}

static void update_card(size_t index, const sim_terrarium_state_t *state)
{
    dashboard_widgets_t *widget = &s_widgets[index];
    if (!state) {
        lv_label_set_text(widget->title, i18n_translate("dashboard.empty_slot"));
        lv_label_set_text(widget->environment, "");
        lv_label_set_text(widget->health, "");
        lv_label_set_text(widget->care, "");
        return;
    }

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s (%s)", state->nickname, state->species.display_name);
    lv_label_set_text(widget->title, buffer);

    snprintf(buffer, sizeof(buffer), "%s: %.1f°C / %.1f°C\n%s: %.0f%%", i18n_translate("dashboard.temperature"),
             state->environment.day_temperature_target_c, state->environment.night_temperature_target_c,
             i18n_translate("dashboard.humidity"), state->environment.humidity_target_percent);
    lv_label_set_text(widget->environment, buffer);

    snprintf(buffer, sizeof(buffer), "%s: %.1f%%  %s: %.1f%%\nUV: %.1f  Lux: %.0f",
             i18n_translate("dashboard.hydration"), state->health.hydration_level * 100.0f,
             i18n_translate("dashboard.stress"), state->health.stress_level * 100.0f,
             state->health.uv_index, state->health.illumination_lux);
    lv_label_set_text(widget->health, buffer);

    const sim_care_entry_t *latest = NULL;
    if (state->care_history_count > 0) {
        size_t idx = (state->care_history_total - 1) % (sizeof(state->care_history) / sizeof(state->care_history[0]));
        latest = &state->care_history[idx];
    }
    if (latest) {
        snprintf(buffer, sizeof(buffer), "%s: %s\n%s",
                 i18n_translate("dashboard.last_care"), latest->timestamp_iso8601, latest->description);
    } else {
        snprintf(buffer, sizeof(buffer), "%s", i18n_translate("dashboard.no_care"));
    }
    lv_label_set_text(widget->care, buffer);
}

void ui_dashboard_refresh(size_t terrarium_index)
{
    if (terrarium_index >= APP_MAX_TERRARIUMS) {
        return;
    }
    const sim_terrarium_state_t *state = sim_engine_get_state(terrarium_index);
    update_card(terrarium_index, state);
}

lv_obj_t *ui_dashboard_container(void)
{
    return s_container;
}
