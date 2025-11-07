#include "ui/ui_dashboard.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "i18n/i18n_manager.h"
#include "sim/sim_engine.h"
#include "ui/ui_theme.h"

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define UI_DASHBOARD_MAX_TERRARIUMS CONFIG_APP_MAX_TERRARIUMS

typedef struct {
    lv_obj_t *card;
    lv_obj_t *title;
    lv_obj_t *temperature;
    lv_obj_t *temperature_bar;
    lv_obj_t *humidity;
    lv_obj_t *humidity_bar;
    lv_obj_t *hydration;
    lv_obj_t *hydration_bar;
    lv_obj_t *health;
    lv_obj_t *health_bar;
    lv_obj_t *stress;
    lv_obj_t *stress_bar;
    lv_obj_t *activity;
    lv_obj_t *activity_bar;
    lv_obj_t *feeding;
    lv_obj_t *alerts_title;
    lv_obj_t *alerts;
    lv_obj_t *history;
} terrarium_card_t;

static const char *TAG = "ui_dashboard";

static lv_obj_t *s_container = NULL;
static terrarium_card_t s_cards[UI_DASHBOARD_MAX_TERRARIUMS];

static void ui_dashboard_create_card(size_t index);
static void ui_dashboard_format_timestamp(uint32_t timestamp, char *buffer, size_t buffer_len);
static const terrarium_state_t *ui_dashboard_get_state(size_t index, const terrarium_state_t *first_state);
static bool ui_dashboard_format_alerts(const terrarium_state_t *state,
                                       uint32_t timestamp,
                                       char *buffer,
                                       size_t buffer_len);
static void ui_dashboard_format_history(const terrarium_state_t *state,
                                        uint32_t timestamp,
                                        char *buffer,
                                        size_t buffer_len);
static bool ui_dashboard_append_line(char *buffer, size_t buffer_len, size_t *written, const char *format, ...);

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

    const char *default_name = i18n_manager_get_string("dashboard_default_name");
    if (!default_name || default_name[0] == '\0') {
        default_name = "Terrarium";
    }
    const char *title_fmt = i18n_manager_get_string("dashboard_title_fmt");
    if (!title_fmt) {
        title_fmt = "#%u %s";
    }
    const char *temp_fmt = i18n_manager_get_string("dashboard_temperature_fmt");
    if (!temp_fmt) {
        temp_fmt = "Temp %.1f °C";
    }
    const char *humidity_fmt = i18n_manager_get_string("dashboard_humidity_fmt");
    if (!humidity_fmt) {
        humidity_fmt = "Humidity %.0f %%";
    }
    const char *hydration_fmt = i18n_manager_get_string("dashboard_hydration_fmt");
    if (!hydration_fmt) {
        hydration_fmt = "Hydration %.0f %%";
    }
    const char *health_fmt = i18n_manager_get_string("dashboard_health_fmt");
    if (!health_fmt) {
        health_fmt = "Health %.0f %%";
    }
    const char *stress_fmt = i18n_manager_get_string("dashboard_stress_fmt");
    if (!stress_fmt) {
        stress_fmt = "Stress %.0f %%";
    }
    const char *activity_fmt = i18n_manager_get_string("dashboard_activity_fmt");
    if (!activity_fmt) {
        activity_fmt = "Activity %.0f %%";
    }
    const char *feeding_interval_fmt = i18n_manager_get_string("dashboard_feeding_interval_fmt");
    if (!feeding_interval_fmt) {
        feeding_interval_fmt = "Last feeding: %s (every %u d)";
    }
    const char *feeding_simple_fmt = i18n_manager_get_string("dashboard_feeding_simple_fmt");
    if (!feeding_simple_fmt) {
        feeding_simple_fmt = "Last feeding: %s";
    }
    const char *alerts_title_text = i18n_manager_get_string("dashboard_alerts_title");
    if (!alerts_title_text) {
        alerts_title_text = "Alerts";
    }
    const char *alerts_none_text = i18n_manager_get_string("dashboard_alerts_none");
    if (!alerts_none_text) {
        alerts_none_text = "Alerts (none)";
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

        const char *name = (state->profile && state->profile->common_name) ? state->profile->common_name : default_name;
        lv_label_set_text_fmt(card->title, title_fmt, (unsigned)(i + 1), name);

        float temperature = state->current_environment.temp_day_c;
        float humidity = state->current_environment.humidity_day_pct;
        float hydration = state->health.hydration_pct;
        float health = state->health.health_pct;
        float stress = state->health.stress_pct;
        float activity_pct = state->activity_score * 100.0f;

        if (humidity < 0.0f) {
            humidity = 0.0f;
        }
        if (humidity > 100.0f) {
            humidity = 100.0f;
        }
        if (hydration < 0.0f) {
            hydration = 0.0f;
        }
        if (hydration > 100.0f) {
            hydration = 100.0f;
        }
        if (health < 0.0f) {
            health = 0.0f;
        }
        if (health > 100.0f) {
            health = 100.0f;
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

        lv_label_set_text_fmt(card->temperature, temp_fmt, temperature);
        if (card->temperature_bar) {
            lv_bar_set_range(card->temperature_bar, 0, 600);
            int32_t temp_value = (int32_t)(temperature * 10.0f);
            if (temp_value < 0) {
                temp_value = 0;
            }
            if (temp_value > 600) {
                temp_value = 600;
            }
            lv_bar_set_value(card->temperature_bar, temp_value, LV_ANIM_OFF);
        }

        lv_label_set_text_fmt(card->humidity, humidity_fmt, humidity);
        if (card->humidity_bar) {
            lv_bar_set_range(card->humidity_bar, 0, 100);
            lv_bar_set_value(card->humidity_bar, (int32_t)humidity, LV_ANIM_OFF);
        }

        if (card->hydration) {
            lv_label_set_text_fmt(card->hydration, hydration_fmt, hydration);
        }
        if (card->hydration_bar) {
            lv_bar_set_range(card->hydration_bar, 0, 100);
            lv_bar_set_value(card->hydration_bar, (int32_t)hydration, LV_ANIM_OFF);
        }

        if (card->health) {
            lv_label_set_text_fmt(card->health, health_fmt, health);
        }
        if (card->health_bar) {
            lv_bar_set_value(card->health_bar, (int32_t)health, LV_ANIM_OFF);
        }

        lv_label_set_text_fmt(card->stress, stress_fmt, stress);
        if (card->stress_bar) {
            lv_bar_set_range(card->stress_bar, 0, 100);
            lv_bar_set_value(card->stress_bar, (int32_t)stress, LV_ANIM_OFF);
        }

        lv_label_set_text_fmt(card->activity, activity_fmt, activity_pct);
        if (card->activity_bar) {
            lv_bar_set_range(card->activity_bar, 0, 100);
            lv_bar_set_value(card->activity_bar, (int32_t)activity_pct, LV_ANIM_OFF);
        }

        uint32_t now = (uint32_t)time(NULL);

        if (card->feeding) {
            char buffer[48];
            ui_dashboard_format_timestamp(state->health.last_feeding_timestamp, buffer, sizeof(buffer));
            if (state->profile && state->profile->feeding_interval_days > 0) {
                lv_label_set_text_fmt(card->feeding,
                                      feeding_interval_fmt,
                                      buffer,
                                      state->profile->feeding_interval_days);
            } else {
                lv_label_set_text_fmt(card->feeding, feeding_simple_fmt, buffer);
            }
        }

        if (card->alerts) {
            char alert_buffer[192];
            bool has_alert = ui_dashboard_format_alerts(state, now, alert_buffer, sizeof(alert_buffer));
            lv_label_set_text(card->alerts, alert_buffer);
            if (card->alerts_title) {
                lv_label_set_text(card->alerts_title, has_alert ? alerts_title_text : alerts_none_text);
            }
        }

        if (card->history) {
            char history_buffer[160];
            ui_dashboard_format_history(state, now, history_buffer, sizeof(history_buffer));
            lv_label_set_text(card->history, history_buffer);
        }

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
    card->temperature_bar = lv_bar_create(card->card);
    lv_obj_set_width(card->temperature_bar, LV_PCT(100));

    card->humidity = lv_label_create(card->card);
    ui_theme_apply_label_style(card->humidity, false);
    card->humidity_bar = lv_bar_create(card->card);
    lv_obj_set_width(card->humidity_bar, LV_PCT(100));

    card->hydration = lv_label_create(card->card);
    ui_theme_apply_label_style(card->hydration, false);
    card->hydration_bar = lv_bar_create(card->card);
    lv_obj_set_width(card->hydration_bar, LV_PCT(100));

    card->health = lv_label_create(card->card);
    ui_theme_apply_label_style(card->health, false);
    card->health_bar = lv_bar_create(card->card);
    lv_obj_set_width(card->health_bar, LV_PCT(100));

    card->stress = lv_label_create(card->card);
    ui_theme_apply_label_style(card->stress, false);
    card->stress_bar = lv_bar_create(card->card);
    lv_obj_set_width(card->stress_bar, LV_PCT(100));

    card->activity = lv_label_create(card->card);
    ui_theme_apply_label_style(card->activity, false);
    card->activity_bar = lv_bar_create(card->card);
    lv_obj_set_width(card->activity_bar, LV_PCT(100));

    card->feeding = lv_label_create(card->card);
    ui_theme_apply_label_style(card->feeding, false);
    lv_label_set_long_mode(card->feeding, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(card->feeding, LV_PCT(100));

    lv_obj_t *alerts_container = lv_obj_create(card->card);
    ui_theme_apply_panel_style(alerts_container);
    lv_obj_set_style_bg_opa(alerts_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(alerts_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(alerts_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(alerts_container, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(alerts_container, LV_FLEX_FLOW_COLUMN);

    card->alerts_title = lv_label_create(alerts_container);
    ui_theme_apply_label_style(card->alerts_title, true);

    card->alerts = lv_label_create(alerts_container);
    lv_label_set_long_mode(card->alerts, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(card->alerts, LV_PCT(100));
    ui_theme_apply_label_style(card->alerts, false);

    card->history = lv_label_create(card->card);
    lv_label_set_long_mode(card->history, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(card->history, LV_PCT(100));
    ui_theme_apply_label_style(card->history, false);

    lv_obj_add_flag(card->card, LV_OBJ_FLAG_HIDDEN);
}

static const terrarium_state_t *ui_dashboard_get_state(size_t index, const terrarium_state_t *first_state)
{
    if (first_state) {
        return &first_state[index];
    }
    return sim_engine_get_state(index);
}

static void ui_dashboard_format_timestamp(uint32_t timestamp, char *buffer, size_t buffer_len)
{
    if (!buffer || buffer_len == 0) {
        return;
    }

    if (timestamp == 0 || timestamp == TERRARIUM_INVALID_TIMESTAMP) {
        const char *missing = i18n_manager_get_string("dashboard_timestamp_missing");
        if (!missing) {
            missing = "not recorded";
        }
        snprintf(buffer, buffer_len, "%s", missing);
        return;
    }

    time_t ts = (time_t)timestamp;
    struct tm tm_buf;
    if (gmtime_r(&ts, &tm_buf) == NULL) {
        const char *epoch_fmt = i18n_manager_get_string("dashboard_timestamp_epoch_fmt");
        if (!epoch_fmt) {
            epoch_fmt = "epoch %u";
        }
        snprintf(buffer, buffer_len, epoch_fmt, (unsigned)timestamp);
        return;
    }

    if (strftime(buffer, buffer_len, "%Y-%m-%d %H:%MZ", &tm_buf) == 0) {
        const char *epoch_fmt = i18n_manager_get_string("dashboard_timestamp_epoch_fmt");
        if (!epoch_fmt) {
            epoch_fmt = "epoch %u";
        }
        snprintf(buffer, buffer_len, epoch_fmt, (unsigned)timestamp);
    }
}

static bool ui_dashboard_format_alerts(const terrarium_state_t *state,
                                       uint32_t timestamp,
                                       char *buffer,
                                       size_t buffer_len)
{
    if (!buffer || buffer_len == 0) {
        return false;
    }

    buffer[0] = '\0';

    if (!state) {
        const char *no_data = i18n_manager_get_string("dashboard_alerts_unavailable");
        if (!no_data) {
            no_data = "No data available";
        }
        snprintf(buffer, buffer_len, "%s", no_data);
        return false;
    }

    size_t written = 0;
    bool has_alert = false;
    const environment_profile_t *target = state->profile ? &state->profile->environment : NULL;

    const char *temp_fmt = i18n_manager_get_string("dashboard_alert_temp_fmt");
    if (!temp_fmt) {
        temp_fmt = "%s Temperature drift %.1f °C\n";
    }
    const char *humidity_fmt = i18n_manager_get_string("dashboard_alert_humidity_fmt");
    if (!humidity_fmt) {
        humidity_fmt = "%s Humidity ±%.0f %%\n";
    }
    const char *lux_fmt = i18n_manager_get_string("dashboard_alert_lux_fmt");
    if (!lux_fmt) {
        lux_fmt = "%s Light variation %.0f lux\n";
    }
    const char *hydration_fmt = i18n_manager_get_string("dashboard_alert_hydration_fmt");
    if (!hydration_fmt) {
        hydration_fmt = "%s Low hydration (%.0f %%)\n";
    }
    const char *stress_fmt = i18n_manager_get_string("dashboard_alert_stress_fmt");
    if (!stress_fmt) {
        stress_fmt = "%s High stress (%.0f %%)\n";
    }
    const char *feeding_fmt = i18n_manager_get_string("dashboard_alert_feeding_fmt");
    if (!feeding_fmt) {
        feeding_fmt = "%s Feeding required (overdue)\n";
    }
    const char *no_alerts_text = i18n_manager_get_string("dashboard_alerts_no_active");
    if (!no_alerts_text) {
        no_alerts_text = "No critical alerts";
    }

    if (target) {
        float delta_temp = fabsf(state->current_environment.temp_day_c - target->temp_day_c);
        if (delta_temp > 3.0f) {
            has_alert |= ui_dashboard_append_line(buffer,
                                                  buffer_len,
                                                  &written,
                                                  temp_fmt,
                                                  LV_SYMBOL_WARNING,
                                                  delta_temp);
        }

        float delta_humidity = fabsf(state->current_environment.humidity_day_pct - target->humidity_day_pct);
        if (delta_humidity > 10.0f) {
            has_alert |= ui_dashboard_append_line(buffer,
                                                  buffer_len,
                                                  &written,
                                                  humidity_fmt,
                                                  LV_SYMBOL_WARNING,
                                                  delta_humidity);
        }

        float delta_lux = fabsf(state->current_environment.lux_day - target->lux_day);
        if (delta_lux > 200.0f) {
            has_alert |= ui_dashboard_append_line(buffer,
                                                  buffer_len,
                                                  &written,
                                                  lux_fmt,
                                                  LV_SYMBOL_WARNING,
                                                  delta_lux);
        }
    }

    if (state->health.hydration_pct < 45.0f) {
        has_alert |= ui_dashboard_append_line(buffer,
                                              buffer_len,
                                              &written,
                                              hydration_fmt,
                                              LV_SYMBOL_WARNING,
                                              state->health.hydration_pct);
    }

    if (state->health.stress_pct > 70.0f) {
        has_alert |= ui_dashboard_append_line(buffer,
                                              buffer_len,
                                              &written,
                                              stress_fmt,
                                              LV_SYMBOL_WARNING,
                                              state->health.stress_pct);
    }

    if (terrarium_state_needs_feeding(state, timestamp)) {
        has_alert |= ui_dashboard_append_line(buffer,
                                              buffer_len,
                                              &written,
                                              feeding_fmt,
                                              LV_SYMBOL_WARNING);
    }

    if (!has_alert) {
        snprintf(buffer, buffer_len, "%s", no_alerts_text);
        return false;
    }

    if (written > 0 && written < buffer_len && buffer[written - 1] == '\n') {
        buffer[written - 1] = '\0';
    }
    return true;
}

static void ui_dashboard_format_history(const terrarium_state_t *state,
                                        uint32_t timestamp,
                                        char *buffer,
                                        size_t buffer_len)
{
    if (!buffer || buffer_len == 0) {
        return;
    }

    if (!state) {
        const char *text = i18n_manager_get_string("dashboard_history_unavailable");
        if (!text) {
            text = "History unavailable";
        }
        snprintf(buffer, buffer_len, "%s", text);
        return;
    }

    uint32_t elapsed = terrarium_state_time_since_feeding(state, timestamp);
    uint32_t elapsed_hours = elapsed / 3600U;
    uint32_t elapsed_days = elapsed_hours / 24U;
    uint32_t elapsed_minutes = (elapsed % 3600U) / 60U;

    if (elapsed == 0) {
        const char *text = i18n_manager_get_string("dashboard_history_recent");
        if (!text) {
            text = "History: feeding just occurred or unknown";
        }
        snprintf(buffer, buffer_len, "%s", text);
        return;
    }

    if (elapsed_days > 0) {
        const char *fmt = i18n_manager_get_string("dashboard_history_days_fmt");
        if (!fmt) {
            fmt = "Last feeding %u d %u h ago\nActivity %.0f %% | Health %.0f %%";
        }
        snprintf(buffer,
                 buffer_len,
                 fmt,
                 (unsigned)elapsed_days,
                 (unsigned)(elapsed_hours % 24U),
                 state->activity_score * 100.0f,
                 state->health.health_pct);
    } else if (elapsed_hours > 0) {
        const char *fmt = i18n_manager_get_string("dashboard_history_hours_fmt");
        if (!fmt) {
            fmt = "Last feeding %u h %u min ago\nActivity %.0f %% | Health %.0f %%";
        }
        snprintf(buffer,
                 buffer_len,
                 fmt,
                 (unsigned)elapsed_hours,
                 (unsigned)elapsed_minutes,
                 state->activity_score * 100.0f,
                 state->health.health_pct);
    } else {
        const char *fmt = i18n_manager_get_string("dashboard_history_minutes_fmt");
        if (!fmt) {
            fmt = "Last feeding %u min ago\nActivity %.0f %% | Health %.0f %%";
        }
        snprintf(buffer,
                 buffer_len,
                 fmt,
                 (unsigned)elapsed_minutes,
                 state->activity_score * 100.0f,
                 state->health.health_pct);
    }
}

static bool ui_dashboard_append_line(char *buffer, size_t buffer_len, size_t *written, const char *format, ...)
{
    if (!buffer || !written || buffer_len == 0 || *written >= buffer_len - 1) {
        return false;
    }

    va_list args;
    va_start(args, format);
    int ret = vsnprintf(buffer + *written, buffer_len - *written, format, args);
    va_end(args);

    if (ret <= 0) {
        buffer[*written] = '\0';
        return false;
    }

    size_t consumed = (size_t)ret;
    if (*written + consumed >= buffer_len) {
        *written = buffer_len - 1;
        buffer[*written] = '\0';
        return true;
    }

    *written += consumed;
    return true;
}
