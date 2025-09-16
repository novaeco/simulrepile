#include "schedule.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include <string.h>
#include <time.h>

#define SCHEDULE_MINUTES_PER_DAY 1440U

static SemaphoreHandle_t s_mutex;
static schedule_config_t s_cfg;
static bool s_initialized;

static const char *SCHEDULE_NVS_NAMESPACE = "schedule";
static const char *SCHEDULE_NVS_KEY = "cfg";

static void schedule_default_config(schedule_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->heating[0].enabled = true;
    cfg->heating[0].start_minute = 0;
    cfg->heating[0].end_minute = 0; // 0 == 24h
}

static schedule_slot_t *get_slots(schedule_config_t *cfg, schedule_actuator_t act)
{
    switch (act) {
    case SCHEDULE_ACTUATOR_HEATING:
        return cfg->heating;
    case SCHEDULE_ACTUATOR_UV:
        return cfg->uv;
    case SCHEDULE_ACTUATOR_LIGHTING:
        return cfg->lighting;
    case SCHEDULE_ACTUATOR_VENTILATION:
        return cfg->ventilation;
    default:
        return NULL;
    }
}

static void normalize_slot(schedule_slot_t *slot)
{
    slot->start_minute %= SCHEDULE_MINUTES_PER_DAY;
    slot->end_minute %= SCHEDULE_MINUTES_PER_DAY;
    slot->enabled = slot->enabled ? true : false;
}

static void sanitize_config(schedule_config_t *cfg)
{
    for (int act = 0; act < SCHEDULE_ACTUATOR_COUNT; ++act) {
        schedule_slot_t *slots = get_slots(cfg, (schedule_actuator_t)act);
        if (!slots)
            continue;
        for (int i = 0; i < SCHEDULE_SLOTS_PER_ACTUATOR; ++i) {
            normalize_slot(&slots[i]);
        }
    }
}

static void schedule_lock(void)
{
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

static void schedule_unlock(void)
{
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
}

static esp_err_t schedule_save_to_nvs(const schedule_config_t *cfg)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(SCHEDULE_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(nvs, SCHEDULE_NVS_KEY, cfg, sizeof(*cfg));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static bool schedule_slot_active(const schedule_slot_t *slot, uint16_t minute)
{
    if (!slot->enabled) {
        return false;
    }
    uint16_t start = slot->start_minute % SCHEDULE_MINUTES_PER_DAY;
    uint16_t end = slot->end_minute % SCHEDULE_MINUTES_PER_DAY;
    if (start == end) {
        return true; // 24h
    }
    if (start < end) {
        return (minute >= start) && (minute < end);
    }
    // chevauchement minuit
    return (minute >= start) || (minute < end);
}

static bool evaluate_actuator(const schedule_slot_t *slots, uint16_t minute)
{
    for (int i = 0; i < SCHEDULE_SLOTS_PER_ACTUATOR; ++i) {
        if (schedule_slot_active(&slots[i], minute)) {
            return true;
        }
    }
    return false;
}

esp_err_t schedule_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }

    schedule_default_config(&s_cfg);

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(SCHEDULE_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_OK) {
        schedule_config_t tmp;
        size_t required = sizeof(tmp);
        err = nvs_get_blob(nvs, SCHEDULE_NVS_KEY, &tmp, &required);
        if (err == ESP_OK && required == sizeof(tmp)) {
            sanitize_config(&tmp);
            schedule_lock();
            s_cfg = tmp;
            schedule_unlock();
        }
        nvs_close(nvs);
        err = ESP_OK;
    } else {
        err = ESP_OK;
    }

    s_initialized = true;
    return err;
}

void schedule_get_config(schedule_config_t *cfg)
{
    if (!cfg) {
        return;
    }
    if (!s_initialized) {
        schedule_init();
    }
    schedule_lock();
    *cfg = s_cfg;
    schedule_unlock();
}

esp_err_t schedule_set_config(const schedule_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        esp_err_t err = schedule_init();
        if (err != ESP_OK) {
            return err;
        }
    }
    schedule_config_t sanitized = *cfg;
    sanitize_config(&sanitized);
    schedule_lock();
    s_cfg = sanitized;
    schedule_unlock();
    return schedule_save_to_nvs(&sanitized);
}

bool schedule_get_state_for_minute(uint16_t minute_of_day, schedule_state_t *out)
{
    if (!out) {
        return false;
    }
    if (!s_initialized) {
        schedule_init();
    }
    minute_of_day %= SCHEDULE_MINUTES_PER_DAY;
    schedule_config_t cfg;
    schedule_lock();
    cfg = s_cfg;
    schedule_unlock();

    out->heating = evaluate_actuator(cfg.heating, minute_of_day);
    out->uv = evaluate_actuator(cfg.uv, minute_of_day);
    out->lighting = evaluate_actuator(cfg.lighting, minute_of_day);
    out->ventilation = evaluate_actuator(cfg.ventilation, minute_of_day);
    return true;
}

bool schedule_get_current_state(schedule_state_t *out)
{
    if (!out) {
        return false;
    }
    time_t now = time(NULL);
    if (now == (time_t)-1) {
        return false;
    }
    struct tm tm_now;
    if (!localtime_r(&now, &tm_now)) {
        return false;
    }
    uint16_t minute = (uint16_t)(((tm_now.tm_hour % 24) * 60) + (tm_now.tm_min % 60));
    return schedule_get_state_for_minute(minute, out);
}

