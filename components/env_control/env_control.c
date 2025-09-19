#include "env_control.h"
#include "sensors.h"
#include "gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TAG "env_control"

#define HISTORY_SAMPLE_PERIOD_S 60
#define MIN_UV_LUX_THRESHOLD 50.0f

typedef struct {
    reptile_env_history_entry_t samples[REPTILE_ENV_HISTORY_LENGTH];
    size_t head;
    size_t count;
    time_t last_timestamp;
} history_buffer_t;

typedef struct {
    size_t index;
    reptile_env_terrarium_config_t cfg;
    reptile_env_terrarium_state_t state;
    history_buffer_t history;
    TaskHandle_t heat_task;
    TaskHandle_t pump_task;
    time_t last_heat_command;
    time_t last_pump_command;
    bool heat_demand;
    bool pump_demand;
    bool manual_heat_requested;
    bool manual_pump_requested;
    bool uv_manual;
    bool uv_manual_state;
} terrarium_ctrl_t;

typedef struct {
    reptile_env_config_t config;
    reptile_env_update_cb_t cb;
    void *cb_ctx;
    TimerHandle_t timer;
    SemaphoreHandle_t lock;
    terrarium_ctrl_t terrariums[REPTILE_ENV_MAX_TERRARIUMS];
    bool running;
} env_controller_t;

static env_controller_t s_ctrl = {0};

static void heat_task(void *ctx);
static void pump_task(void *ctx);

static inline uint32_t time_point_to_minutes(reptile_env_time_point_t tp)
{
    return (uint32_t)(tp.hour % 24) * 60u + (uint32_t)(tp.minute % 60);
}

static bool minutes_in_range(uint32_t minute, uint32_t start, uint32_t end)
{
    if (start == end) {
        return true;
    }
    if (start < end) {
        return (minute >= start) && (minute < end);
    }
    return (minute >= start) || (minute < end);
}

static bool is_day_profile_active(const terrarium_ctrl_t *terr, const struct tm *tm_now)
{
    uint32_t minute = (uint32_t)tm_now->tm_hour * 60u + (uint32_t)tm_now->tm_min;
    uint32_t day_start = time_point_to_minutes(terr->cfg.day_start);
    uint32_t night_start = time_point_to_minutes(terr->cfg.night_start);
    return minutes_in_range(minute, day_start, night_start);
}

static bool uv_schedule_should_enable(const terrarium_ctrl_t *terr, const struct tm *tm_now)
{
    if (!terr->cfg.uv.enabled) {
        return false;
    }
    uint32_t minute = (uint32_t)tm_now->tm_hour * 60u + (uint32_t)tm_now->tm_min;
    uint32_t uv_on = time_point_to_minutes(terr->cfg.uv.on);
    uint32_t uv_off = time_point_to_minutes(terr->cfg.uv.off);
    return minutes_in_range(minute, uv_on, uv_off);
}

static void history_reset(history_buffer_t *history)
{
    memset(history, 0, sizeof(*history));
}

static void history_push(history_buffer_t *history, const reptile_env_history_entry_t *entry)
{
    history->samples[history->head] = *entry;
    history->head = (history->head + 1u) % REPTILE_ENV_HISTORY_LENGTH;
    if (history->count < REPTILE_ENV_HISTORY_LENGTH) {
        history->count++;
    }
    history->last_timestamp = entry->timestamp;
}

static void notify_state(size_t index)
{
    if (!s_ctrl.cb) {
        return;
    }
    reptile_env_terrarium_state_t snapshot;
    if (reptile_env_get_state(index, &snapshot) == ESP_OK) {
        s_ctrl.cb(index, &snapshot, s_ctrl.cb_ctx);
    }
}

static void update_alarm_flags(terrarium_ctrl_t *terr)
{
    uint32_t flags = REPTILE_ENV_ALARM_NONE;
    bool sensor_failure = false;
    if (!terr->state.temperature_valid || !terr->state.humidity_valid) {
        sensor_failure = true;
    }
    if (terr->state.target_light_lux > 0.0f && !terr->state.light_valid) {
        sensor_failure = true;
    }
    if (sensor_failure) {
        flags |= REPTILE_ENV_ALARM_SENSOR_FAILURE;
    }
    if (terr->state.temperature_valid) {
        float temp = terr->state.temperature_c;
        float target = terr->state.target_temperature_c;
        float low_threshold = target - terr->cfg.hysteresis.heat_on_delta * 1.5f;
        float high_threshold = target + terr->cfg.hysteresis.heat_off_delta * 1.5f;
        if (temp <= low_threshold) {
            flags |= REPTILE_ENV_ALARM_TEMP_LOW;
        }
        if (temp >= high_threshold) {
            flags |= REPTILE_ENV_ALARM_TEMP_HIGH;
        }
    }
    if (terr->state.humidity_valid) {
        float hum = terr->state.humidity_pct;
        float target_h = terr->state.target_humidity_pct;
        float low_h = target_h - terr->cfg.hysteresis.humidity_on_delta * 1.5f;
        float high_h = target_h + terr->cfg.hysteresis.humidity_off_delta * 1.5f;
        if (hum <= low_h) {
            flags |= REPTILE_ENV_ALARM_HUM_LOW;
        }
        if (hum >= high_h) {
            flags |= REPTILE_ENV_ALARM_HUM_HIGH;
        }
    }
    if (terr->state.light_valid && terr->state.target_light_lux > 0.0f) {
        if (terr->state.light_lux < terr->state.target_light_lux) {
            flags |= REPTILE_ENV_ALARM_LIGHT_LOW;
        }
    }
    terr->state.alarm_flags = flags;
}

static void apply_uv_gpio(size_t index, bool on)
{
    if (index == 0) {
        reptile_uv_gpio(on);
    }
}

static esp_err_t start_heat_cycle_locked(terrarium_ctrl_t *terr, bool manual)
{
    if (terr->heat_task) {
        return ESP_ERR_INVALID_STATE;
    }
    terr->state.heating = true;
    terr->state.manual_heat = manual;
    terr->manual_heat_requested = false;
    terr->last_heat_command = time(NULL);
    BaseType_t ok = xTaskCreate(heat_task,
                                "heat_cycle",
                                4096,
                                terr,
                                configMAX_PRIORITIES - 2,
                                &terr->heat_task);
    if (ok != pdPASS) {
        terr->state.heating = false;
        terr->state.manual_heat = false;
        terr->heat_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t start_pump_cycle_locked(terrarium_ctrl_t *terr, bool manual)
{
    if (terr->pump_task) {
        return ESP_ERR_INVALID_STATE;
    }
    terr->state.pumping = true;
    terr->state.manual_pump = manual;
    terr->manual_pump_requested = false;
    terr->last_pump_command = time(NULL);
    BaseType_t ok = xTaskCreate(pump_task,
                                "pump_cycle",
                                4096,
                                terr,
                                configMAX_PRIORITIES - 2,
                                &terr->pump_task);
    if (ok != pdPASS) {
        terr->state.pumping = false;
        terr->state.manual_pump = false;
        terr->pump_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void update_uv_state_locked(terrarium_ctrl_t *terr, bool desired)
{
    if (terr->state.uv_light == desired) {
        terr->state.manual_uv_override = terr->uv_manual;
        return;
    }
    time_t now = time(NULL);
    double dt = difftime(now, terr->state.last_update);
    if (dt < 0) {
        dt = 0;
    }
    if (terr->state.uv_light) {
        terr->state.energy_uv_Wh += (float)(dt * terr->cfg.power.uv_power_w / 3600.0f);
    }
    terr->state.uv_light = desired;
    terr->state.manual_uv_override = terr->uv_manual;
    terr->state.last_update = now;
    apply_uv_gpio(terr->index, desired);
}

static void evaluate_heat(terrarium_ctrl_t *terr, time_t now)
{
    if (!terr->cfg.enabled) {
        terr->heat_demand = false;
        return;
    }
    bool running = terr->heat_task != NULL;
    bool manual = terr->manual_heat_requested;
    double seconds_since_last = difftime(now, terr->last_heat_command);
    bool interval_ok = manual || seconds_since_last >= (double)terr->cfg.min_minutes_between_heat * 60.0;

    if (!terr->state.temperature_valid) {
        terr->heat_demand = false;
        manual = false;
    } else {
        float temp = terr->state.temperature_c;
        float target = terr->state.target_temperature_c;
        if (!terr->heat_demand) {
            if (temp <= target - terr->cfg.hysteresis.heat_on_delta) {
                terr->heat_demand = true;
            }
        } else {
            if (temp >= target + terr->cfg.hysteresis.heat_off_delta) {
                terr->heat_demand = false;
            }
        }
    }

    bool should_trigger = manual || terr->heat_demand;
    if (should_trigger && !running && interval_ok) {
        esp_err_t err = start_heat_cycle_locked(terr, manual);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start heat cycle: %s", esp_err_to_name(err));
        }
        terr->heat_demand = false;
    }
}

static void evaluate_pump(terrarium_ctrl_t *terr, time_t now)
{
    if (!terr->cfg.enabled) {
        terr->pump_demand = false;
        return;
    }
    bool running = terr->pump_task != NULL;
    bool manual = terr->manual_pump_requested;
    double seconds_since_last = difftime(now, terr->last_pump_command);
    bool interval_ok = manual || seconds_since_last >= (double)terr->cfg.min_minutes_between_pump * 60.0;

    if (!terr->state.humidity_valid) {
        terr->pump_demand = false;
        manual = false;
    } else {
        float hum = terr->state.humidity_pct;
        float target = terr->state.target_humidity_pct;
        if (!terr->pump_demand) {
            if (hum <= target - terr->cfg.hysteresis.humidity_on_delta) {
                terr->pump_demand = true;
            }
        } else {
            if (hum >= target + terr->cfg.hysteresis.humidity_off_delta) {
                terr->pump_demand = false;
            }
        }
    }

    bool should_trigger = manual || terr->pump_demand;
    if (should_trigger && !running && interval_ok) {
        esp_err_t err = start_pump_cycle_locked(terr, manual);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start pump cycle: %s", esp_err_to_name(err));
        }
        terr->pump_demand = false;
    }
}

static void heat_task(void *ctx)
{
    terrarium_ctrl_t *terr = (terrarium_ctrl_t *)ctx;
    size_t idx = terr->index;
    notify_state(idx);
    time_t start = time(NULL);
    if (idx == 0) {
        reptile_heat_gpio();
    } else {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    time_t end = time(NULL);
    double duration = difftime(end, start);
    if (duration < 0) {
        duration = 0;
    }
    xSemaphoreTake(s_ctrl.lock, portMAX_DELAY);
    terr->state.heating = false;
    terr->state.manual_heat = false;
    terr->heat_task = NULL;
    terr->last_heat_command = end;
    terr->state.energy_heat_Wh += (float)(duration * terr->cfg.power.heater_power_w / 3600.0f);
    xSemaphoreGive(s_ctrl.lock);
    notify_state(idx);
    vTaskDelete(NULL);
}

static void pump_task(void *ctx)
{
    terrarium_ctrl_t *terr = (terrarium_ctrl_t *)ctx;
    size_t idx = terr->index;
    notify_state(idx);
    time_t start = time(NULL);
    if (idx == 0) {
        reptile_water_gpio();
    } else {
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
    time_t end = time(NULL);
    double duration = difftime(end, start);
    if (duration < 0) {
        duration = 0;
    }
    xSemaphoreTake(s_ctrl.lock, portMAX_DELAY);
    terr->state.pumping = false;
    terr->state.manual_pump = false;
    terr->pump_task = NULL;
    terr->last_pump_command = end;
    terr->state.energy_pump_Wh += (float)(duration * terr->cfg.power.pump_power_w / 3600.0f);
    xSemaphoreGive(s_ctrl.lock);
    notify_state(idx);
    vTaskDelete(NULL);
}

static void controller_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    if (!s_ctrl.running) {
        return;
    }
    time_t now = time(NULL);
    struct tm tm_now = {0};
    localtime_r(&now, &tm_now);
    size_t available_channels = sensors_get_channel_count();

    xSemaphoreTake(s_ctrl.lock, portMAX_DELAY);
    for (size_t i = 0; i < s_ctrl.config.terrarium_count; ++i) {
        terrarium_ctrl_t *terr = &s_ctrl.terrariums[i];
        if (terr->state.last_update == 0) {
            terr->state.last_update = now;
        }
        double dt = difftime(now, terr->state.last_update);
        if (dt < 0) {
            dt = 0;
        }
        if (terr->state.heating) {
            terr->state.energy_heat_Wh += (float)(dt * terr->cfg.power.heater_power_w / 3600.0f);
        }
        if (terr->state.pumping) {
            terr->state.energy_pump_Wh += (float)(dt * terr->cfg.power.pump_power_w / 3600.0f);
        }
        if (terr->state.uv_light) {
            terr->state.energy_uv_Wh += (float)(dt * terr->cfg.power.uv_power_w / 3600.0f);
        }
        terr->state.last_update = now;

        bool channel_valid = terr->cfg.sensor_channel < available_channels;
        float temp = channel_valid ? sensors_read_temperature_channel(terr->cfg.sensor_channel) : NAN;
        float hum = channel_valid ? sensors_read_humidity_channel(terr->cfg.sensor_channel) : NAN;
        float lux = channel_valid ? sensors_read_lux_channel(terr->cfg.sensor_channel) : NAN;
        terr->state.temperature_c = temp;
        terr->state.humidity_pct = hum;
        terr->state.light_lux = lux;
        terr->state.temperature_valid = !isnan(temp);
        terr->state.humidity_valid = !isnan(hum);
        terr->state.light_valid = !isnan(lux);

        bool day_active = is_day_profile_active(terr, &tm_now);
        terr->state.day_profile_active = day_active;
        terr->state.target_temperature_c = day_active ? terr->cfg.day.temperature_c
                                                      : terr->cfg.night.temperature_c;
        terr->state.target_humidity_pct = day_active ? terr->cfg.day.humidity_pct
                                                     : terr->cfg.night.humidity_pct;

        bool uv_desired = terr->uv_manual ? terr->uv_manual_state
                                          : uv_schedule_should_enable(terr, &tm_now);
        update_uv_state_locked(terr, uv_desired);
        terr->state.target_light_lux = terr->state.uv_light ? MIN_UV_LUX_THRESHOLD : 0.0f;

        update_alarm_flags(terr);
        evaluate_heat(terr, now);
        evaluate_pump(terr, now);

        if (difftime(now, terr->history.last_timestamp) >= HISTORY_SAMPLE_PERIOD_S || terr->history.count == 0) {
            reptile_env_history_entry_t entry = {
                .timestamp = now,
                .temperature_c = terr->state.temperature_c,
                .humidity_pct = terr->state.humidity_pct,
                .light_lux = terr->state.light_lux,
                .target_temperature_c = terr->state.target_temperature_c,
                .target_humidity_pct = terr->state.target_humidity_pct,
                .target_light_lux = terr->state.target_light_lux,
            };
            history_push(&terr->history, &entry);
        }
    }
    xSemaphoreGive(s_ctrl.lock);

    for (size_t i = 0; i < s_ctrl.config.terrarium_count; ++i) {
        notify_state(i);
    }
}

void reptile_env_get_default_config(reptile_env_config_t *cfg)
{
    if (!cfg) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->terrarium_count = 2;
    cfg->period_ms = REPTILE_ENV_DEFAULT_PERIOD_MS;
    for (size_t i = 0; i < cfg->terrarium_count; ++i) {
        reptile_env_terrarium_config_t *terr = &cfg->terrarium[i];
        snprintf(terr->name, sizeof(terr->name), "Terrarium %u", (unsigned)(i + 1));
        terr->enabled = true;
        terr->sensor_channel = i;
        terr->day_start = (reptile_env_time_point_t){.hour = 8, .minute = 0};
        terr->night_start = (reptile_env_time_point_t){.hour = 20, .minute = 0};
        terr->day = (reptile_env_profile_thresholds_t){.temperature_c = 30.0f, .humidity_pct = 70.0f};
        terr->night = (reptile_env_profile_thresholds_t){.temperature_c = 26.0f, .humidity_pct = 60.0f};
        terr->hysteresis = (reptile_env_hysteresis_t){
            .heat_on_delta = 1.5f,
            .heat_off_delta = 0.5f,
            .humidity_on_delta = 8.0f,
            .humidity_off_delta = 4.0f,
        };
        terr->uv = (reptile_env_uv_schedule_t){
            .enabled = true,
            .on = {.hour = 9, .minute = 0},
            .off = {.hour = 18, .minute = 0},
        };
        terr->power = (reptile_env_power_profile_t){
            .heater_power_w = 120.0f,
            .pump_power_w = 18.0f,
            .uv_power_w = 40.0f,
        };
        terr->min_minutes_between_heat = 10;
        terr->min_minutes_between_pump = 20;
    }
}

static void controller_reset_locked(void)
{
    for (size_t i = 0; i < s_ctrl.config.terrarium_count; ++i) {
        terrarium_ctrl_t *terr = &s_ctrl.terrariums[i];
        terr->index = i;
        terr->cfg = s_ctrl.config.terrarium[i];
        memset(&terr->state, 0, sizeof(terr->state));
        terr->state.temperature_c = NAN;
        terr->state.humidity_pct = NAN;
        terr->state.light_lux = NAN;
        terr->state.target_temperature_c = terr->cfg.day.temperature_c;
        terr->state.target_humidity_pct = terr->cfg.day.humidity_pct;
        terr->state.target_light_lux = 0.0f;
        terr->state.last_update = time(NULL);
        terr->heat_task = NULL;
        terr->pump_task = NULL;
        terr->last_heat_command = 0;
        terr->last_pump_command = 0;
        terr->heat_demand = false;
        terr->pump_demand = false;
        terr->manual_heat_requested = false;
        terr->manual_pump_requested = false;
        terr->uv_manual = false;
        terr->uv_manual_state = false;
        history_reset(&terr->history);
        apply_uv_gpio(i, false);
    }
}

esp_err_t reptile_env_start(const reptile_env_config_t *cfg,
                            reptile_env_update_cb_t cb,
                            void *user_ctx)
{
    if (s_ctrl.running) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!cfg || cfg->terrarium_count == 0 || cfg->terrarium_count > REPTILE_ENV_MAX_TERRARIUMS) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = sensors_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    s_ctrl.config = *cfg;
    if (s_ctrl.config.period_ms == 0) {
        s_ctrl.config.period_ms = REPTILE_ENV_DEFAULT_PERIOD_MS;
    }
    s_ctrl.cb = cb;
    s_ctrl.cb_ctx = user_ctx;
    if (!s_ctrl.lock) {
        s_ctrl.lock = xSemaphoreCreateMutex();
        if (!s_ctrl.lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    xSemaphoreTake(s_ctrl.lock, portMAX_DELAY);
    controller_reset_locked();
    xSemaphoreGive(s_ctrl.lock);

    const TickType_t period = pdMS_TO_TICKS(s_ctrl.config.period_ms);
    s_ctrl.timer = xTimerCreate("env_ctrl", period, pdTRUE, NULL, controller_timer_cb);
    if (!s_ctrl.timer) {
        return ESP_ERR_NO_MEM;
    }
    s_ctrl.running = true;
    if (xTimerStart(s_ctrl.timer, 0) != pdPASS) {
        s_ctrl.running = false;
        xTimerDelete(s_ctrl.timer, 0);
        s_ctrl.timer = NULL;
        return ESP_FAIL;
    }
    controller_timer_cb(NULL);
    return ESP_OK;
}

void reptile_env_stop(void)
{
    if (!s_ctrl.running) {
        return;
    }
    s_ctrl.running = false;
    if (s_ctrl.timer) {
        xTimerStop(s_ctrl.timer, portMAX_DELAY);
        xTimerDelete(s_ctrl.timer, portMAX_DELAY);
        s_ctrl.timer = NULL;
    }
    xSemaphoreTake(s_ctrl.lock, portMAX_DELAY);
    for (size_t i = 0; i < s_ctrl.config.terrarium_count; ++i) {
        terrarium_ctrl_t *terr = &s_ctrl.terrariums[i];
        if (terr->heat_task) {
            vTaskDelete(terr->heat_task);
            terr->heat_task = NULL;
        }
        if (terr->pump_task) {
            vTaskDelete(terr->pump_task);
            terr->pump_task = NULL;
        }
        apply_uv_gpio(i, false);
    }
    xSemaphoreGive(s_ctrl.lock);
}

esp_err_t reptile_env_update_config(const reptile_env_config_t *cfg)
{
    if (!cfg || cfg->terrarium_count == 0 || cfg->terrarium_count > REPTILE_ENV_MAX_TERRARIUMS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ctrl.running) {
        s_ctrl.config = *cfg;
        if (s_ctrl.config.period_ms == 0) {
            s_ctrl.config.period_ms = REPTILE_ENV_DEFAULT_PERIOD_MS;
        }
        if (s_ctrl.lock) {
            xSemaphoreTake(s_ctrl.lock, portMAX_DELAY);
            controller_reset_locked();
            xSemaphoreGive(s_ctrl.lock);
        }
        return ESP_OK;
    }
    reptile_env_update_cb_t cb = s_ctrl.cb;
    void *ctx = s_ctrl.cb_ctx;
    reptile_env_stop();
    return reptile_env_start(cfg, cb, ctx);
}

const reptile_env_config_t *reptile_env_get_config(void)
{
    return &s_ctrl.config;
}

size_t reptile_env_get_terrarium_count(void)
{
    return s_ctrl.config.terrarium_count;
}

esp_err_t reptile_env_get_state(size_t terrarium_index,
                                reptile_env_terrarium_state_t *out_state)
{
    if (!out_state) {
        return ESP_ERR_INVALID_ARG;
    }
    if (terrarium_index >= s_ctrl.config.terrarium_count) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_ctrl.lock, portMAX_DELAY);
    *out_state = s_ctrl.terrariums[terrarium_index].state;
    xSemaphoreGive(s_ctrl.lock);
    return ESP_OK;
}

size_t reptile_env_get_history(size_t terrarium_index,
                               reptile_env_history_entry_t *out,
                               size_t max_entries)
{
    if (!out || terrarium_index >= s_ctrl.config.terrarium_count || max_entries == 0) {
        return 0;
    }
    xSemaphoreTake(s_ctrl.lock, portMAX_DELAY);
    history_buffer_t *history = &s_ctrl.terrariums[terrarium_index].history;
    size_t count = history->count < max_entries ? history->count : max_entries;
    for (size_t i = 0; i < count; ++i) {
        size_t idx = (history->head + REPTILE_ENV_HISTORY_LENGTH - count + i) % REPTILE_ENV_HISTORY_LENGTH;
        out[i] = history->samples[idx];
    }
    xSemaphoreGive(s_ctrl.lock);
    return count;
}

esp_err_t reptile_env_manual_pump(size_t terrarium_index)
{
    if (terrarium_index >= s_ctrl.config.terrarium_count) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_ctrl.lock, portMAX_DELAY);
    terrarium_ctrl_t *terr = &s_ctrl.terrariums[terrarium_index];
    terr->manual_pump_requested = true;
    esp_err_t err = ESP_OK;
    if (!terr->pump_task) {
        err = start_pump_cycle_locked(terr, true);
    }
    xSemaphoreGive(s_ctrl.lock);
    return err;
}

esp_err_t reptile_env_manual_heat(size_t terrarium_index)
{
    if (terrarium_index >= s_ctrl.config.terrarium_count) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_ctrl.lock, portMAX_DELAY);
    terrarium_ctrl_t *terr = &s_ctrl.terrariums[terrarium_index];
    terr->manual_heat_requested = true;
    esp_err_t err = ESP_OK;
    if (!terr->heat_task) {
        err = start_heat_cycle_locked(terr, true);
    }
    xSemaphoreGive(s_ctrl.lock);
    return err;
}

esp_err_t reptile_env_manual_uv_toggle(size_t terrarium_index)
{
    if (terrarium_index >= s_ctrl.config.terrarium_count) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_ctrl.lock, portMAX_DELAY);
    terrarium_ctrl_t *terr = &s_ctrl.terrariums[terrarium_index];
    terr->uv_manual = !terr->uv_manual;
    if (terr->uv_manual) {
        terr->uv_manual_state = !terr->state.uv_light;
        update_uv_state_locked(terr, terr->uv_manual_state);
    }
    xSemaphoreGive(s_ctrl.lock);
    notify_state(terrarium_index);
    return ESP_OK;
}

esp_err_t reptile_env_manual_uv_set(size_t terrarium_index, bool force_on)
{
    if (terrarium_index >= s_ctrl.config.terrarium_count) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_ctrl.lock, portMAX_DELAY);
    terrarium_ctrl_t *terr = &s_ctrl.terrariums[terrarium_index];
    terr->uv_manual = true;
    terr->uv_manual_state = force_on;
    update_uv_state_locked(terr, force_on);
    xSemaphoreGive(s_ctrl.lock);
    notify_state(terrarium_index);
    return ESP_OK;
}

