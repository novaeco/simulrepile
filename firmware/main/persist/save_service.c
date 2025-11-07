#include "persist/save_service.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "i18n/i18n_manager.h"
#include "lvgl_port.h"
#include "persist/save_manager.h"
#include "persist/schema_version.h"
#include "sdkconfig.h"
#include "sim/sim_engine.h"
#include "tts/tts_stub.h"
#include "ui/ui_slots.h"

#define SAVE_SERVICE_QUEUE_DEPTH 8
#define SAVE_SERVICE_TASK_STACK 4096
#define SAVE_SERVICE_TASK_PRIORITY 5

typedef enum {
    SAVE_SERVICE_REQ_AUTOSAVE = 0,
    SAVE_SERVICE_REQ_MANUAL_SAVE,
    SAVE_SERVICE_REQ_MANUAL_LOAD,
} save_service_request_type_t;

typedef struct {
    save_service_request_type_t type;
    uint32_t slot_mask;
} save_service_request_t;

static const char *TAG = "save_service";

static QueueHandle_t s_request_queue = NULL;
static TaskHandle_t s_worker_task = NULL;
static TimerHandle_t s_autosave_timer = NULL;
static uint32_t s_autosave_interval_s = CONFIG_APP_AUTOSAVE_INTERVAL_S;

static void save_service_worker_task(void *param);
static void save_service_timer_cb(TimerHandle_t timer);
static void save_service_report(const char *text, bool success, bool speak);
static uint32_t save_service_compute_autosave_mask(void);
static esp_err_t save_service_handle_save_slot(int slot_index, bool autosave);
static esp_err_t save_service_handle_load_slot(int slot_index);
static esp_err_t save_service_parse_payload(const save_slot_t *slot, sim_saved_slot_t *out_state);

static void save_service_set_pending_message(const char *key)
{
    const char *text = i18n_manager_get_string(key);
    if (!text) {
        return;
    }
    lvgl_port_lock();
    ui_slots_show_status(text, true);
    lvgl_port_unlock();
}

esp_err_t save_service_init(void)
{
    if (s_request_queue || s_worker_task) {
        return ESP_OK;
    }

    s_request_queue = xQueueCreate(SAVE_SERVICE_QUEUE_DEPTH, sizeof(save_service_request_t));
    if (!s_request_queue) {
        ESP_LOGE(TAG, "Failed to create request queue");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(save_service_worker_task,
                    "save_service",
                    SAVE_SERVICE_TASK_STACK,
                    NULL,
                    SAVE_SERVICE_TASK_PRIORITY,
                    &s_worker_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create worker task");
        vQueueDelete(s_request_queue);
        s_request_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_autosave_interval_s = CONFIG_APP_AUTOSAVE_INTERVAL_S;
    if (s_autosave_interval_s < 30U) {
        s_autosave_interval_s = 30U;
    }

    s_autosave_timer = xTimerCreate("autosave",
                                    pdMS_TO_TICKS(s_autosave_interval_s * 1000U),
                                    pdTRUE,
                                    NULL,
                                    save_service_timer_cb);
    if (!s_autosave_timer) {
        ESP_LOGE(TAG, "Failed to create autosave timer");
        vTaskDelete(s_worker_task);
        s_worker_task = NULL;
        vQueueDelete(s_request_queue);
        s_request_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    if (xTimerStart(s_autosave_timer, 0) != pdPASS) {
        ESP_LOGW(TAG, "Autosave timer start failed");
    }

    save_service_notify_language_changed();
    ESP_LOGI(TAG, "Save service initialized (interval=%us)", (unsigned)s_autosave_interval_s);
    return ESP_OK;
}

esp_err_t save_service_set_interval(uint32_t seconds)
{
    if (seconds < 30U) {
        seconds = 30U;
    }
    if (seconds > 3600U) {
        seconds = 3600U;
    }
    s_autosave_interval_s = seconds;
    if (!s_autosave_timer) {
        return ESP_OK;
    }
    if (xTimerChangePeriod(s_autosave_timer, pdMS_TO_TICKS(seconds * 1000U), pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGW(TAG, "Failed to update autosave interval to %u s", (unsigned)seconds);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Autosave interval updated to %u s", (unsigned)seconds);
    const char *label = i18n_manager_get_string("save_status_interval_updated");
    if (label) {
        char buffer[96];
        snprintf(buffer, sizeof(buffer), label, (unsigned)seconds);
        save_service_report(buffer, true, false);
    }
    return ESP_OK;
}

esp_err_t save_service_trigger_manual_save(uint32_t slot_mask)
{
    if (!s_request_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    if (slot_mask == 0U) {
        save_service_report(i18n_manager_get_string("save_error_no_selection"), false, false);
        return ESP_ERR_INVALID_ARG;
    }

    save_service_set_pending_message("save_status_pending");

    save_service_request_t request = {
        .type = SAVE_SERVICE_REQ_MANUAL_SAVE,
        .slot_mask = slot_mask,
    };
    if (xQueueSend(s_request_queue, &request, pdMS_TO_TICKS(200)) != pdPASS) {
        save_service_report(i18n_manager_get_string("save_error_queue_full"), false, false);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t save_service_trigger_manual_load(uint32_t slot_mask)
{
    if (!s_request_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    if (slot_mask == 0U) {
        save_service_report(i18n_manager_get_string("save_error_no_selection"), false, false);
        return ESP_ERR_INVALID_ARG;
    }

    save_service_set_pending_message("save_status_pending");

    save_service_request_t request = {
        .type = SAVE_SERVICE_REQ_MANUAL_LOAD,
        .slot_mask = slot_mask,
    };
    if (xQueueSend(s_request_queue, &request, pdMS_TO_TICKS(200)) != pdPASS) {
        save_service_report(i18n_manager_get_string("save_error_queue_full"), false, false);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

void save_service_notify_language_changed(void)
{
    const char *idle = i18n_manager_get_string("save_status_idle");
    if (!idle) {
        return;
    }
    lvgl_port_lock();
    ui_slots_show_status(idle, true);
    lvgl_port_unlock();
}

static void save_service_worker_task(void *param)
{
    (void)param;
    save_service_request_t request;
    while (true) {
        if (xQueueReceive(s_request_queue, &request, portMAX_DELAY) != pdPASS) {
            continue;
        }

        uint32_t mask = request.slot_mask;
        if (request.type == SAVE_SERVICE_REQ_AUTOSAVE) {
            mask = save_service_compute_autosave_mask();
            if (mask == 0U) {
                continue;
            }
            ESP_LOGD(TAG, "Autosave triggered for mask 0x%08x", (unsigned)mask);
        }

        bool all_ok = true;
        for (int slot = 0; slot < CONFIG_APP_MAX_TERRARIUMS; ++slot) {
            if (((mask >> slot) & 0x1U) == 0U) {
                continue;
            }
            esp_err_t err = ESP_OK;
            if (request.type == SAVE_SERVICE_REQ_MANUAL_LOAD) {
                err = save_service_handle_load_slot(slot);
                if (err == ESP_OK) {
                    const char *fmt = i18n_manager_get_string("save_result_load_success_fmt");
                    if (fmt) {
                        char buffer[96];
                        snprintf(buffer, sizeof(buffer), fmt, slot + 1);
                        save_service_report(buffer, true, true);
                    }
                }
            } else {
                bool autosave = (request.type == SAVE_SERVICE_REQ_AUTOSAVE);
                err = save_service_handle_save_slot(slot, autosave);
                if (err == ESP_OK) {
                    const char *key = autosave ? "save_result_autosave_slot_fmt" : "save_result_save_success_fmt";
                    const char *fmt = i18n_manager_get_string(key);
                    if (fmt) {
                        char buffer[96];
                        snprintf(buffer, sizeof(buffer), fmt, slot + 1);
                        save_service_report(buffer, true, autosave ? false : true);
                    }
                }
            }

            if (err != ESP_OK) {
                all_ok = false;
                const char *fmt = i18n_manager_get_string("save_result_error_fmt");
                if (fmt) {
                    char buffer[96];
                    snprintf(buffer, sizeof(buffer), fmt, slot + 1, esp_err_to_name(err));
                    save_service_report(buffer, false, false);
                }
            }
        }

        lvgl_port_lock();
        ui_slots_refresh();
        lvgl_port_unlock();

        if (request.type == SAVE_SERVICE_REQ_AUTOSAVE) {
            const char *summary_key = all_ok ? "save_result_autosave_complete" : "save_result_autosave_partial";
            const char *summary = i18n_manager_get_string(summary_key);
            save_service_report(summary, all_ok, false);
        }
    }
}

static void save_service_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    if (!s_request_queue) {
        return;
    }
    save_service_request_t request = {
        .type = SAVE_SERVICE_REQ_AUTOSAVE,
        .slot_mask = 0U,
    };
    (void)xQueueSendFromISR(s_request_queue, &request, NULL);
}

static void save_service_report(const char *text, bool success, bool speak)
{
    if (!text || text[0] == '\0') {
        return;
    }
    lvgl_port_lock();
    ui_slots_show_status(text, success);
    lvgl_port_unlock();
    if (speak) {
        tts_stub_speak(text, false);
    }
}

static uint32_t save_service_compute_autosave_mask(void)
{
    uint32_t mask = 0U;
    size_t count = sim_engine_get_count();
    for (size_t i = 0; i < count && i < CONFIG_APP_MAX_TERRARIUMS; ++i) {
        mask |= (1U << i);
    }
    return mask;
}

static esp_err_t save_service_handle_save_slot(int slot_index, bool autosave)
{
    sim_saved_slot_t snapshot;
    esp_err_t err = sim_engine_export_slot((size_t)slot_index, &snapshot);
    if (err != ESP_OK) {
        return err;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddNumberToObject(root, "schema", SIMULREPILE_SAVE_VERSION);
    cJSON_AddNumberToObject(root, "slot", slot_index);
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));
    cJSON_AddStringToObject(root, "mode", autosave ? "auto" : "manual");

    cJSON *profile = cJSON_CreateObject();
    cJSON *state = cJSON_CreateObject();
    if (!profile || !state) {
        cJSON_Delete(root);
        if (profile) {
            cJSON_Delete(profile);
        }
        if (state) {
            cJSON_Delete(state);
        }
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(profile, "scientific_name", snapshot.scientific_name);
    cJSON_AddStringToObject(profile, "common_name", snapshot.common_name);
    cJSON_AddNumberToObject(profile, "feeding_interval_days", snapshot.feeding_interval_days);

    cJSON *environment = cJSON_CreateObject();
    if (!environment) {
        cJSON_Delete(root);
        cJSON_Delete(profile);
        cJSON_Delete(state);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(environment, "temp_day_c", snapshot.environment.temp_day_c);
    cJSON_AddNumberToObject(environment, "temp_night_c", snapshot.environment.temp_night_c);
    cJSON_AddNumberToObject(environment, "humidity_day_pct", snapshot.environment.humidity_day_pct);
    cJSON_AddNumberToObject(environment, "humidity_night_pct", snapshot.environment.humidity_night_pct);
    cJSON_AddNumberToObject(environment, "lux_day", snapshot.environment.lux_day);
    cJSON_AddNumberToObject(environment, "lux_night", snapshot.environment.lux_night);

    cJSON_AddItemToObject(profile, "environment", environment);
    cJSON_AddItemToObject(root, "profile", profile);

    cJSON_AddNumberToObject(state, "hydration_pct", snapshot.health.hydration_pct);
    cJSON_AddNumberToObject(state, "stress_pct", snapshot.health.stress_pct);
    cJSON_AddNumberToObject(state, "health_pct", snapshot.health.health_pct);
    cJSON_AddNumberToObject(state, "last_feeding_timestamp", snapshot.health.last_feeding_timestamp);
    cJSON_AddNumberToObject(state, "activity_score", snapshot.activity_score);

    cJSON_AddItemToObject(root, "state", state);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }

    save_slot_t slot = {0};
    slot.meta.schema_version = SIMULREPILE_SAVE_VERSION;
    slot.meta.payload_length = strlen(json);
    slot.meta.flags = 0;
    slot.payload = (uint8_t *)json;

    err = save_manager_save_slot(slot_index, &slot, true);
    free(json);
    return err;
}

static esp_err_t save_service_parse_payload(const save_slot_t *slot, sim_saved_slot_t *out_state)
{
    if (!slot || !slot->payload || slot->meta.payload_length == 0U || !out_state) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *root = cJSON_ParseWithLength((const char *)slot->payload, slot->meta.payload_length);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *profile = cJSON_GetObjectItemCaseSensitive(root, "profile");
    cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
    if (!cJSON_IsObject(profile) || !cJSON_IsObject(state)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *scientific = cJSON_GetObjectItemCaseSensitive(profile, "scientific_name");
    cJSON *common = cJSON_GetObjectItemCaseSensitive(profile, "common_name");
    cJSON *feeding = cJSON_GetObjectItemCaseSensitive(profile, "feeding_interval_days");
    cJSON *environment = cJSON_GetObjectItemCaseSensitive(profile, "environment");
    if (!cJSON_IsString(scientific) || !cJSON_IsString(common) || !cJSON_IsObject(environment)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    memset(out_state, 0, sizeof(*out_state));
    strlcpy(out_state->scientific_name, scientific->valuestring, sizeof(out_state->scientific_name));
    strlcpy(out_state->common_name, common->valuestring, sizeof(out_state->common_name));
    out_state->feeding_interval_days = (uint8_t)(cJSON_IsNumber(feeding) ? feeding->valuedouble : 0);

    cJSON *temp_day = cJSON_GetObjectItemCaseSensitive(environment, "temp_day_c");
    cJSON *temp_night = cJSON_GetObjectItemCaseSensitive(environment, "temp_night_c");
    cJSON *hum_day = cJSON_GetObjectItemCaseSensitive(environment, "humidity_day_pct");
    cJSON *hum_night = cJSON_GetObjectItemCaseSensitive(environment, "humidity_night_pct");
    cJSON *lux_day = cJSON_GetObjectItemCaseSensitive(environment, "lux_day");
    cJSON *lux_night = cJSON_GetObjectItemCaseSensitive(environment, "lux_night");

    out_state->environment.temp_day_c = cJSON_IsNumber(temp_day) ? temp_day->valuedouble : 0.0f;
    out_state->environment.temp_night_c = cJSON_IsNumber(temp_night) ? temp_night->valuedouble : 0.0f;
    out_state->environment.humidity_day_pct = cJSON_IsNumber(hum_day) ? hum_day->valuedouble : 0.0f;
    out_state->environment.humidity_night_pct = cJSON_IsNumber(hum_night) ? hum_night->valuedouble : 0.0f;
    out_state->environment.lux_day = cJSON_IsNumber(lux_day) ? lux_day->valuedouble : 0.0f;
    out_state->environment.lux_night = cJSON_IsNumber(lux_night) ? lux_night->valuedouble : 0.0f;

    cJSON *hydration = cJSON_GetObjectItemCaseSensitive(state, "hydration_pct");
    cJSON *stress = cJSON_GetObjectItemCaseSensitive(state, "stress_pct");
    cJSON *health = cJSON_GetObjectItemCaseSensitive(state, "health_pct");
    cJSON *feeding_ts = cJSON_GetObjectItemCaseSensitive(state, "last_feeding_timestamp");
    cJSON *activity = cJSON_GetObjectItemCaseSensitive(state, "activity_score");

    out_state->health.hydration_pct = cJSON_IsNumber(hydration) ? hydration->valuedouble : 0.0f;
    out_state->health.stress_pct = cJSON_IsNumber(stress) ? stress->valuedouble : 0.0f;
    out_state->health.health_pct = cJSON_IsNumber(health) ? health->valuedouble : 0.0f;
    out_state->health.last_feeding_timestamp = cJSON_IsNumber(feeding_ts) ? (uint32_t)feeding_ts->valuedouble : 0U;
    out_state->activity_score = cJSON_IsNumber(activity) ? activity->valuedouble : 0.0f;

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t save_service_handle_load_slot(int slot_index)
{
    save_slot_t slot = {0};
    esp_err_t err = save_manager_load_slot(slot_index, &slot);
    if (err != ESP_OK) {
        return err;
    }

    sim_saved_slot_t state;
    err = save_service_parse_payload(&slot, &state);
    save_manager_free_slot(&slot);
    if (err != ESP_OK) {
        return err;
    }

    err = sim_engine_restore_slot((size_t)slot_index, &state);
    if (err == ESP_OK) {
        tts_stub_speak(i18n_manager_get_string("save_result_load_tts"), false);
    }
    return err;
}
