#include "ui/ui_slots.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include <stdint.h>

#include "sim/sim_engine.h"
#include "ui/ui_theme.h"

#define UI_SLOTS_MAX CONFIG_APP_MAX_TERRARIUMS

typedef struct {
    lv_obj_t *button;
    lv_obj_t *label;
} slot_widget_t;

static const char *TAG = "ui_slots";

static lv_obj_t *s_root = NULL;
static slot_widget_t s_slots[UI_SLOTS_MAX];
static uint32_t s_selection_mask = 0;
static bool s_ignore_events = false;

static void ui_slots_create_slot(size_t index);
static void ui_slots_update_slot(size_t index, const terrarium_state_t *state);
static void ui_slots_button_event_cb(lv_event_t *event);

void ui_slots_create(lv_obj_t *parent)
{
    if (!parent) {
        return;
    }

    ESP_LOGI(TAG, "Creating slot overview");

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_root, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(s_root, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_root, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_column(s_root, 20, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_TRANSP, LV_PART_MAIN);

    for (size_t i = 0; i < UI_SLOTS_MAX; ++i) {
        ui_slots_create_slot(i);
    }
}

void ui_slots_refresh(void)
{
    if (!s_root) {
        return;
    }

    size_t terrarium_count = sim_engine_get_count();
    for (size_t i = 0; i < UI_SLOTS_MAX; ++i) {
        const terrarium_state_t *state = NULL;
        if (i < terrarium_count) {
            state = sim_engine_get_state(i);
        }
        ui_slots_update_slot(i, state);
    }
}

uint32_t ui_slots_get_selection_mask(void)
{
    return s_selection_mask;
}

static void ui_slots_create_slot(size_t index)
{
    slot_widget_t *slot = &s_slots[index];

    slot->button = lv_button_create(s_root);
    lv_obj_add_flag(slot->button, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_width(slot->button, LV_PCT(47));
    lv_obj_set_style_pad_all(slot->button, 16, LV_PART_MAIN);
    lv_obj_set_style_min_height(slot->button, 140, LV_PART_MAIN);
    lv_obj_set_flex_flow(slot->button, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(slot->button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    ui_theme_apply_panel_style(slot->button);

    slot->label = lv_label_create(slot->button);
    lv_label_set_long_mode(slot->label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(slot->label, LV_PCT(100));
    ui_theme_apply_label_style(slot->label, false);

    lv_obj_add_event_cb(slot->button, ui_slots_button_event_cb, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)index);
}

static void ui_slots_update_slot(size_t index, const terrarium_state_t *state)
{
    slot_widget_t *slot = &s_slots[index];
    if (!slot->button || !slot->label) {
        return;
    }

    s_ignore_events = true;

    if (!state) {
        lv_obj_add_state(slot->button, LV_STATE_DISABLED);
        lv_obj_clear_state(slot->button, LV_STATE_CHECKED);
        s_selection_mask &= ~(1u << index);
        lv_label_set_text_fmt(slot->label, "Emplacement %u\nDisponible", (unsigned)(index + 1));
    } else {
        lv_obj_clear_state(slot->button, LV_STATE_DISABLED);
        const char *name = (state->profile && state->profile->common_name) ? state->profile->common_name : "Terrarium";
        float temperature = state->current_environment.temp_day_c;
        float humidity = state->current_environment.humidity_day_pct;
        float stress = state->health.stress_pct;

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

        lv_label_set_text_fmt(slot->label,
                              "Slot %u - %s\n%.1f °C | %.0f %% H\nStress %.0f %%",
                              (unsigned)(index + 1),
                              name,
                              temperature,
                              humidity,
                              stress);
    }

    s_ignore_events = false;
}

static void ui_slots_button_event_cb(lv_event_t *event)
{
    if (!event || s_ignore_events) {
        return;
    }

    lv_obj_t *obj = lv_event_get_target(event);
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
    if (!obj) {
        return;
    }

    if (lv_obj_has_state(obj, LV_STATE_DISABLED)) {
        return;
    }

    if (lv_obj_has_state(obj, LV_STATE_CHECKED)) {
        s_selection_mask |= (1u << index);
    } else {
        s_selection_mask &= ~(1u << index);
    }

    ESP_LOGD(TAG, "Selection mask updated: 0x%08x", (unsigned)s_selection_mask);
}
