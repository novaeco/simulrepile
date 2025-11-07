#include "ui/ui_slots.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_err.h"

#include "persist/save_manager.h"
#include "sim/sim_engine.h"
#include "ui/ui_theme.h"

#define UI_SLOTS_MAX CONFIG_APP_MAX_TERRARIUMS

typedef struct {
    lv_obj_t *button;
    lv_obj_t *label;
    lv_obj_t *save_label;
    lv_obj_t *alerts_label;
} slot_widget_t;

static const char *TAG = "ui_slots";

static lv_obj_t *s_root = NULL;
static slot_widget_t s_slots[UI_SLOTS_MAX];
static save_slot_status_t s_slot_status[UI_SLOTS_MAX];
static uint32_t s_selection_mask = 0;
static bool s_ignore_events = false;

static void ui_slots_create_slot(size_t index);
static void ui_slots_update_slot(size_t index,
                                 const terrarium_state_t *state,
                                 const save_slot_status_t *status,
                                 esp_err_t status_err);
static void ui_slots_button_event_cb(lv_event_t *event);
static void ui_slots_format_save_status(const save_slot_status_t *status,
                                        char *buffer,
                                        size_t buffer_len);
static void ui_slots_format_timestamp(uint64_t unix_seconds, char *buffer, size_t buffer_len);
static void ui_slots_update_alert_label(slot_widget_t *slot,
                                        const terrarium_state_t *state,
                                        const save_slot_status_t *status,
                                        esp_err_t status_err);

void ui_slots_create(lv_obj_t *parent)
{
    if (!parent) {
        return;
    }

    ESP_LOGI(TAG, "Creating slot overview");

    memset(s_slots, 0, sizeof(s_slots));
    memset(s_slot_status, 0, sizeof(s_slot_status));

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
    esp_err_t status_err = save_manager_list_slots(s_slot_status, UI_SLOTS_MAX);
    if (status_err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to list save slots: %s", esp_err_to_name(status_err));
        memset(s_slot_status, 0, sizeof(s_slot_status));
    }

    for (size_t i = 0; i < UI_SLOTS_MAX; ++i) {
        const terrarium_state_t *state = NULL;
        if (i < terrarium_count) {
            state = sim_engine_get_state(i);
        }
        const save_slot_status_t *status = (status_err == ESP_OK) ? &s_slot_status[i] : NULL;
        ui_slots_update_slot(i, state, status, status_err);
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

    slot->save_label = lv_label_create(slot->button);
    lv_label_set_long_mode(slot->save_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(slot->save_label, LV_PCT(100));
    ui_theme_apply_label_style(slot->save_label, false);

    slot->alerts_label = lv_label_create(slot->button);
    lv_label_set_long_mode(slot->alerts_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(slot->alerts_label, LV_PCT(100));
    ui_theme_apply_label_style(slot->alerts_label, false);

    lv_obj_add_event_cb(slot->button, ui_slots_button_event_cb, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)index);
}

static void ui_slots_update_slot(size_t index,
                                 const terrarium_state_t *state,
                                 const save_slot_status_t *status,
                                 esp_err_t status_err)
{
    slot_widget_t *slot = &s_slots[index];
    if (!slot->button || !slot->label || !slot->save_label) {
        return;
    }

    s_ignore_events = true;

    bool has_save = status && (status->primary.exists || status->backup.exists);

    if (!state && !has_save) {
        lv_obj_add_state(slot->button, LV_STATE_DISABLED);
        lv_obj_clear_state(slot->button, LV_STATE_CHECKED);
        s_selection_mask &= ~(1u << index);
        lv_label_set_text_fmt(slot->label, "Emplacement %u\nDisponible", (unsigned)(index + 1));
        lv_label_set_text(slot->save_label, "Aucune sauvegarde enregistrée");
    } else {
        lv_obj_clear_state(slot->button, LV_STATE_DISABLED);
        const char *name = "Terrarium";
        if (state && state->profile && state->profile->common_name) {
            name = state->profile->common_name;
        }
        float temperature = state ? state->current_environment.temp_day_c : 0.0f;
        float humidity = state ? state->current_environment.humidity_day_pct : 0.0f;
        float stress = state ? state->health.stress_pct : 0.0f;

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

        if (state) {
            lv_label_set_text_fmt(slot->label,
                                  "Slot %u - %s\n%.1f °C | %.0f %% H\nStress %.0f %%",
                                  (unsigned)(index + 1),
                                  name,
                                  temperature,
                                  humidity,
                                  stress);
        } else {
            lv_label_set_text_fmt(slot->label,
                                  "Slot %u - Sauvegarde disponible\nDonnées prêtes à charger",
                                  (unsigned)(index + 1));
        }

        char save_buffer[128];
        if (status_err != ESP_OK) {
            snprintf(save_buffer, sizeof(save_buffer), "Sauvegardes indisponibles : %s", esp_err_to_name(status_err));
        } else {
            ui_slots_format_save_status(status, save_buffer, sizeof(save_buffer));
        }
        lv_label_set_text(slot->save_label, save_buffer);
    }

    ui_slots_update_alert_label(slot, state, status, status_err);

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

static void ui_slots_format_save_status(const save_slot_status_t *status,
                                        char *buffer,
                                        size_t buffer_len)
{
    if (!buffer || buffer_len == 0) {
        return;
    }

    if (!status) {
        snprintf(buffer, buffer_len, "%s", "Statut inconnu");
        return;
    }

    char primary_time[48];
    char backup_time[48];
    ui_slots_format_timestamp(status->primary.meta.saved_at_unix, primary_time, sizeof(primary_time));
    ui_slots_format_timestamp(status->backup.meta.saved_at_unix, backup_time, sizeof(backup_time));

    const char *primary_state = status->primary.exists ? (status->primary.valid ? "OK" : "Corrompu") : "Vide";
    const char *backup_state = status->backup.exists ? (status->backup.valid ? "OK" : "Corrompu") : "Vide";

    if (status->primary.last_error != ESP_OK) {
        primary_state = esp_err_to_name(status->primary.last_error);
    }
    if (status->backup.last_error != ESP_OK) {
        backup_state = esp_err_to_name(status->backup.last_error);
    }

    snprintf(buffer,
             buffer_len,
             "Prim: %s (%s)\nSecours: %s (%s)",
             primary_state,
             primary_time,
             backup_state,
             backup_time);
}

static void ui_slots_format_timestamp(uint64_t unix_seconds, char *buffer, size_t buffer_len)
{
    if (!buffer || buffer_len == 0) {
        return;
    }

    if (unix_seconds == 0ULL) {
        snprintf(buffer, buffer_len, "%s", "n/a");
        return;
    }

    time_t ts = (time_t)unix_seconds;
    struct tm tm_buf;
    if (gmtime_r(&ts, &tm_buf) == NULL) {
        snprintf(buffer, buffer_len, "epoch %llu", (unsigned long long)unix_seconds);
        return;
    }

    if (strftime(buffer, buffer_len, "%Y-%m-%d %H:%MZ", &tm_buf) == 0) {
        snprintf(buffer, buffer_len, "epoch %llu", (unsigned long long)unix_seconds);
    }
}

static void ui_slots_update_alert_label(slot_widget_t *slot,
                                        const terrarium_state_t *state,
                                        const save_slot_status_t *status,
                                        esp_err_t status_err)
{
    if (!slot || !slot->alerts_label) {
        return;
    }

    const char *message = "Alertes : aucune";
    char buffer[160];

    if (status_err != ESP_OK) {
        snprintf(buffer, sizeof(buffer), "%s Sauvegardes indisponibles (%s)", LV_SYMBOL_WARNING, esp_err_to_name(status_err));
        message = buffer;
    } else if (status) {
        if (status->primary.exists && !status->primary.valid) {
            snprintf(buffer,
                     sizeof(buffer),
                     "%s Sauvegarde primaire corrompue", LV_SYMBOL_WARNING);
            message = buffer;
        } else if (status->backup.exists && !status->backup.valid) {
            snprintf(buffer,
                     sizeof(buffer),
                     "%s Sauvegarde secours corrompue", LV_SYMBOL_WARNING);
            message = buffer;
        }
    }

    if (message == buffer) {
        lv_label_set_text(slot->alerts_label, message);
        return;
    }

    if (state) {
        uint32_t now = (uint32_t)time(NULL);
        if (terrarium_state_needs_feeding(state, now)) {
            snprintf(buffer, sizeof(buffer), "%s Terrarium à nourrir", LV_SYMBOL_WARNING);
            message = buffer;
        } else if (state->health.stress_pct > 70.0f) {
            snprintf(buffer, sizeof(buffer), "%s Stress élevé (%.0f %%)", LV_SYMBOL_WARNING, state->health.stress_pct);
            message = buffer;
        } else if (state->health.hydration_pct < 45.0f) {
            snprintf(buffer, sizeof(buffer), "%s Hydratation faible (%.0f %%)", LV_SYMBOL_WARNING, state->health.hydration_pct);
            message = buffer;
        }
    }

    lv_label_set_text(slot->alerts_label, message);
}
