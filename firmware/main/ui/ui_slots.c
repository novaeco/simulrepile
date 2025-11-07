#include "ui/ui_slots.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_err.h"

#include "i18n/i18n_manager.h"
#include "persist/save_manager.h"
#include "persist/save_service.h"
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
static lv_obj_t *s_action_row = NULL;
static lv_obj_t *s_save_button = NULL;
static lv_obj_t *s_load_button = NULL;
static lv_obj_t *s_save_button_label = NULL;
static lv_obj_t *s_load_button_label = NULL;
static lv_obj_t *s_status_label = NULL;

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
static void ui_slots_action_save_cb(lv_event_t *event);
static void ui_slots_action_load_cb(lv_event_t *event);

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

    s_action_row = lv_obj_create(s_root);
    lv_obj_set_size(s_action_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_action_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_action_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_action_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(s_action_row, 16, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_action_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_action_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_save_button = lv_button_create(s_action_row);
    ui_theme_apply_panel_style(s_save_button);
    lv_obj_set_size(s_save_button, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_add_event_cb(s_save_button, ui_slots_action_save_cb, LV_EVENT_CLICKED, NULL);
    s_save_button_label = lv_label_create(s_save_button);
    ui_theme_apply_label_style(s_save_button_label, true);
    lv_obj_center(s_save_button_label);

    s_load_button = lv_button_create(s_action_row);
    ui_theme_apply_panel_style(s_load_button);
    lv_obj_set_size(s_load_button, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_add_event_cb(s_load_button, ui_slots_action_load_cb, LV_EVENT_CLICKED, NULL);
    s_load_button_label = lv_label_create(s_load_button);
    ui_theme_apply_label_style(s_load_button_label, true);
    lv_obj_center(s_load_button_label);

    s_status_label = lv_label_create(s_root);
    lv_obj_set_width(s_status_label, LV_PCT(100));
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_WRAP);
    ui_theme_apply_label_style(s_status_label, false);

    ui_slots_show_status(NULL, true);
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

void ui_slots_refresh_language(void)
{
    if (!s_root) {
        return;
    }
    if (s_save_button_label) {
        const char *text = i18n_manager_get_string("slots_action_save");
        if (!text) {
            text = "Save";
        }
        lv_label_set_text(s_save_button_label, text);
    }
    if (s_load_button_label) {
        const char *text = i18n_manager_get_string("slots_action_restore");
        if (!text) {
            text = "Restore";
        }
        lv_label_set_text(s_load_button_label, text);
    }
    ui_slots_refresh();
}

void ui_slots_show_status(const char *message, bool success)
{
    if (!s_status_label) {
        return;
    }
    const char *text = message;
    if (!text || text[0] == '\0') {
        text = i18n_manager_get_string("save_status_idle");
        if (!text) {
            text = "";
        }
    }
    lv_label_set_text(s_status_label, text);
    lv_color_t color;
    if (text[0] == '\0') {
        color = lv_palette_main(LV_PALETTE_GREY);
    } else {
        color = success ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_RED);
    }
    lv_obj_set_style_text_color(s_status_label, color, LV_PART_MAIN);
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

    const char *empty_title_fmt = i18n_manager_get_string("slots_empty_title_fmt");
    if (!empty_title_fmt) {
        empty_title_fmt = "Slot %u\nAvailable";
    }
    const char *empty_status = i18n_manager_get_string("slots_empty_status");
    if (!empty_status) {
        empty_status = "No save present";
    }
    const char *default_name = i18n_manager_get_string("dashboard_default_name");
    if (!default_name || default_name[0] == '\0') {
        default_name = "Terrarium";
    }
    const char *slot_state_fmt = i18n_manager_get_string("slots_state_fmt");
    if (!slot_state_fmt) {
        slot_state_fmt = "Slot %u - %s\n%.1f °C | %.0f %% RH\nStress %.0f %%";
    }
    const char *saved_ready_fmt = i18n_manager_get_string("slots_saved_ready_fmt");
    if (!saved_ready_fmt) {
        saved_ready_fmt = "Slot %u - Save available\nData ready to load";
    }
    const char *save_error_fmt = i18n_manager_get_string("slots_save_error_fmt");
    if (!save_error_fmt) {
        save_error_fmt = "Saves unavailable (%s)";
    }

    if (!state && !has_save) {
        lv_obj_add_state(slot->button, LV_STATE_DISABLED);
        lv_obj_clear_state(slot->button, LV_STATE_CHECKED);
        s_selection_mask &= ~(1u << index);
        lv_label_set_text_fmt(slot->label, empty_title_fmt, (unsigned)(index + 1));
        lv_label_set_text(slot->save_label, empty_status);
    } else {
        lv_obj_clear_state(slot->button, LV_STATE_DISABLED);
        const char *name = default_name;
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
                                  slot_state_fmt,
                                  (unsigned)(index + 1),
                                  name,
                                  temperature,
                                  humidity,
                                  stress);
        } else {
            lv_label_set_text_fmt(slot->label, saved_ready_fmt, (unsigned)(index + 1));
        }

        char save_buffer[128];
        if (status_err != ESP_OK) {
            snprintf(save_buffer, sizeof(save_buffer), save_error_fmt, esp_err_to_name(status_err));
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
        const char *text = i18n_manager_get_string("slots_status_unknown");
        if (!text) {
            text = "Status unknown";
        }
        snprintf(buffer, buffer_len, "%s", text);
        return;
    }

    char primary_time[48];
    char backup_time[48];
    ui_slots_format_timestamp(status->primary.meta.saved_at_unix, primary_time, sizeof(primary_time));
    ui_slots_format_timestamp(status->backup.meta.saved_at_unix, backup_time, sizeof(backup_time));

    const char *state_ok = i18n_manager_get_string("slots_save_state_ok");
    const char *state_corrupt = i18n_manager_get_string("slots_save_state_corrupt");
    const char *state_empty = i18n_manager_get_string("slots_save_state_empty");
    if (!state_ok) {
        state_ok = "OK";
    }
    if (!state_corrupt) {
        state_corrupt = "Corrupt";
    }
    if (!state_empty) {
        state_empty = "Empty";
    }

    const char *primary_state = status->primary.exists ? (status->primary.valid ? state_ok : state_corrupt) : state_empty;
    const char *backup_state = status->backup.exists ? (status->backup.valid ? state_ok : state_corrupt) : state_empty;

    if (status->primary.last_error != ESP_OK) {
        primary_state = esp_err_to_name(status->primary.last_error);
    }
    if (status->backup.last_error != ESP_OK) {
        backup_state = esp_err_to_name(status->backup.last_error);
    }

    const char *fmt = i18n_manager_get_string("slots_save_status_fmt");
    if (!fmt) {
        fmt = "Primary: %s (%s)\nBackup: %s (%s)";
    }
    snprintf(buffer, buffer_len, fmt, primary_state, primary_time, backup_state, backup_time);
}

static void ui_slots_format_timestamp(uint64_t unix_seconds, char *buffer, size_t buffer_len)
{
    if (!buffer || buffer_len == 0) {
        return;
    }

    if (unix_seconds == 0ULL) {
        const char *text = i18n_manager_get_string("slots_timestamp_missing");
        if (!text) {
            text = "n/a";
        }
        snprintf(buffer, buffer_len, "%s", text);
        return;
    }

    time_t ts = (time_t)unix_seconds;
    struct tm tm_buf;
    if (gmtime_r(&ts, &tm_buf) == NULL) {
        const char *fmt = i18n_manager_get_string("slots_timestamp_epoch_fmt");
        if (!fmt) {
            fmt = "epoch %llu";
        }
        snprintf(buffer, buffer_len, fmt, (unsigned long long)unix_seconds);
        return;
    }

    if (strftime(buffer, buffer_len, "%Y-%m-%d %H:%MZ", &tm_buf) == 0) {
        const char *fmt = i18n_manager_get_string("slots_timestamp_epoch_fmt");
        if (!fmt) {
            fmt = "epoch %llu";
        }
        snprintf(buffer, buffer_len, fmt, (unsigned long long)unix_seconds);
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

    const char *no_alert = i18n_manager_get_string("slots_alert_none");
    if (!no_alert) {
        no_alert = "No alerts";
    }
    const char *save_error_fmt = i18n_manager_get_string("slots_alert_save_error_fmt");
    if (!save_error_fmt) {
        save_error_fmt = "%s Saves unavailable (%s)";
    }
    const char *primary_corrupt_fmt = i18n_manager_get_string("slots_alert_primary_corrupt");
    if (!primary_corrupt_fmt) {
        primary_corrupt_fmt = "%s Primary save corrupt";
    }
    const char *backup_corrupt_fmt = i18n_manager_get_string("slots_alert_backup_corrupt");
    if (!backup_corrupt_fmt) {
        backup_corrupt_fmt = "%s Backup save corrupt";
    }
    const char *feeding_fmt = i18n_manager_get_string("slots_alert_feeding");
    if (!feeding_fmt) {
        feeding_fmt = "%s Terrarium needs feeding";
    }
    const char *stress_fmt = i18n_manager_get_string("slots_alert_stress_fmt");
    if (!stress_fmt) {
        stress_fmt = "%s High stress (%.0f %%)";
    }
    const char *hydration_fmt = i18n_manager_get_string("slots_alert_hydration_fmt");
    if (!hydration_fmt) {
        hydration_fmt = "%s Low hydration (%.0f %%)";
    }

    char buffer[160];
    const char *message = no_alert;

    if (status_err != ESP_OK) {
        snprintf(buffer, sizeof(buffer), save_error_fmt, LV_SYMBOL_WARNING, esp_err_to_name(status_err));
        message = buffer;
    } else if (status) {
        if (status->primary.exists && !status->primary.valid) {
            snprintf(buffer, sizeof(buffer), primary_corrupt_fmt, LV_SYMBOL_WARNING);
            message = buffer;
        } else if (status->backup.exists && !status->backup.valid) {
            snprintf(buffer, sizeof(buffer), backup_corrupt_fmt, LV_SYMBOL_WARNING);
            message = buffer;
        }
    }

    if (message != buffer && state) {
        uint32_t now = (uint32_t)time(NULL);
        if (terrarium_state_needs_feeding(state, now)) {
            snprintf(buffer, sizeof(buffer), feeding_fmt, LV_SYMBOL_WARNING);
            message = buffer;
        } else if (state->health.stress_pct > 70.0f) {
            snprintf(buffer, sizeof(buffer), stress_fmt, LV_SYMBOL_WARNING, state->health.stress_pct);
            message = buffer;
        } else if (state->health.hydration_pct < 45.0f) {
            snprintf(buffer, sizeof(buffer), hydration_fmt, LV_SYMBOL_WARNING, state->health.hydration_pct);
            message = buffer;
        }
    }

    lv_label_set_text(slot->alerts_label, message);
}

static void ui_slots_action_save_cb(lv_event_t *event)
{
    (void)event;
    uint32_t mask = ui_slots_get_selection_mask();
    esp_err_t err = save_service_trigger_manual_save(mask);
    if (err != ESP_OK) {
        const char *fmt = i18n_manager_get_string("save_error_dispatch_fmt");
        if (!fmt) {
            fmt = "Save request failed (%s)";
        }
        char buffer[96];
        snprintf(buffer, sizeof(buffer), fmt, esp_err_to_name(err));
        ui_slots_show_status(buffer, false);
    }
}

static void ui_slots_action_load_cb(lv_event_t *event)
{
    (void)event;
    uint32_t mask = ui_slots_get_selection_mask();
    esp_err_t err = save_service_trigger_manual_load(mask);
    if (err != ESP_OK) {
        const char *fmt = i18n_manager_get_string("save_error_dispatch_fmt");
        if (!fmt) {
            fmt = "Save request failed (%s)";
        }
        char buffer[96];
        snprintf(buffer, sizeof(buffer), fmt, esp_err_to_name(err));
        ui_slots_show_status(buffer, false);
    }
}
