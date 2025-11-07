#include "link/core_host_link.h"

#include <math.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "sdkconfig.h"

#define CORE_LINK_SOF 0xA5
#define CORE_LINK_MAX_PAYLOAD 512
#define CORE_HOST_EVENT_HANDSHAKE BIT0
#define CORE_HOST_EVENT_DISPLAY_READY BIT1

#define CORE_HOST_WATCHDOG_PERIOD_MS 250
#define CORE_HOST_WATCHDOG_PERIOD_TICKS pdMS_TO_TICKS(CORE_HOST_WATCHDOG_PERIOD_MS)
#define CORE_HOST_STATE_TIMEOUT_TICKS pdMS_TO_TICKS(CONFIG_CORE_APP_LINK_STATE_TIMEOUT_MS)
#define CORE_HOST_PING_TIMEOUT_TICKS pdMS_TO_TICKS(CONFIG_CORE_APP_LINK_PING_TIMEOUT_MS)

static const char *TAG = "core_host_link";

typedef struct __attribute__((packed)) {
    uint8_t sof;
    uint8_t type;
    uint16_t length;
} core_link_frame_header_t;

typedef struct __attribute__((packed)) {
    uint8_t protocol_version;
    uint8_t capabilities;
} core_link_hello_ack_payload_t;

typedef struct __attribute__((packed)) {
    uint16_t width;
    uint16_t height;
    uint8_t protocol_version;
} core_link_display_ready_payload_t;

typedef struct __attribute__((packed)) {
    uint32_t epoch_seconds;
    uint8_t terrarium_count;
} core_link_state_header_wire_t;

typedef struct __attribute__((packed)) {
    uint8_t terrarium_id;
    char scientific_name[CORE_LINK_NAME_MAX_LEN + 1];
    char common_name[CORE_LINK_NAME_MAX_LEN + 1];
    float temp_day_c;
    float temp_night_c;
    float humidity_day_pct;
    float humidity_night_pct;
    float lux_day;
    float lux_night;
    float hydration_pct;
    float stress_pct;
    float health_pct;
    uint32_t last_feeding_timestamp;
    float activity_score;
} core_link_snapshot_wire_t;

typedef struct __attribute__((packed)) {
    uint32_t epoch_seconds;
    uint8_t terrarium_count;
    uint8_t changed_count;
} core_link_state_delta_header_wire_t;

typedef struct __attribute__((packed)) {
    uint8_t terrarium_id;
    core_link_delta_field_mask_t field_mask;
} core_link_state_delta_entry_wire_t;

#define CORE_HOST_DELTA_FLOAT_EPSILON (0.0005f)
#define CORE_HOST_MAX_DELTAS_BEFORE_FULL 20U
#define CORE_HOST_FULL_REFRESH_SECONDS 30U

static core_host_link_config_t s_config;
static bool s_initialized = false;
static bool s_started = false;
static uint8_t s_peer_version = 0;
static EventGroupHandle_t s_events = NULL;
static TaskHandle_t s_rx_task = NULL;
static core_host_display_ready_cb_t s_display_cb = NULL;
static void *s_display_ctx = NULL;
static core_host_request_state_cb_t s_request_cb = NULL;
static void *s_request_ctx = NULL;
static core_host_touch_cb_t s_touch_cb = NULL;
static void *s_touch_ctx = NULL;
static core_host_command_cb_t s_command_cb = NULL;
static void *s_command_ctx = NULL;
static core_host_display_info_t s_display_info = {0};
static TimerHandle_t s_watchdog_timer = NULL;
static TickType_t s_last_activity_tick = 0;
static TickType_t s_last_ping_tick = 0;
static bool s_ping_in_flight = false;
static bool s_display_alive = false;
static bool s_watchdog_triggered = false;
static core_link_state_frame_t s_last_sent_state = {0};
static bool s_last_state_valid = false;
static bool s_force_next_full = true;
static uint32_t s_delta_since_full = 0;
static uint32_t s_last_full_epoch = 0;

static uint8_t checksum_compute(uint8_t type, uint16_t length, const uint8_t *payload);
static esp_err_t uart_send_frame(core_link_msg_type_t type, const void *payload, uint16_t length);
static void rx_task(void *arg);
static void handle_frame(core_link_msg_type_t type, const uint8_t *payload, uint16_t length);
static void update_display_alive(bool alive);
static void watchdog_timer_cb(TimerHandle_t timer);
static esp_err_t send_state_full(const core_link_state_frame_t *frame);
static esp_err_t send_state_delta(const core_link_state_frame_t *frame, bool *out_any_change);
static const core_link_terrarium_snapshot_t *find_previous_snapshot(uint8_t terrarium_id);
static void store_last_state(const core_link_state_frame_t *frame);
static void schedule_full_frame(void);
static bool ensure_baseline_compatible(const core_link_state_frame_t *frame);
static bool float_field_changed(float a, float b);
static bool string_field_changed(const char *a, const char *b);

esp_err_t core_host_link_init(const core_host_link_config_t *config)
{
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "config is null");
    if (s_initialized) {
        return ESP_OK;
    }

    s_config = *config;
    if (s_config.task_stack_size <= 0) {
        s_config.task_stack_size = 4096;
    }
    if (s_config.task_priority <= 0) {
        s_config.task_priority = 5;
    }

    uart_config_t uart_cfg = {
        .baud_rate = s_config.baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_REF_TICK,
    };

    ESP_RETURN_ON_ERROR(uart_driver_install(s_config.uart_port, CORE_LINK_MAX_PAYLOAD * 2, 0, 0, NULL, 0), TAG, "uart_driver_install failed");
    ESP_RETURN_ON_ERROR(uart_param_config(s_config.uart_port, &uart_cfg), TAG, "uart_param_config failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(s_config.uart_port, s_config.tx_gpio, s_config.rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "uart_set_pin failed");

    if (!s_events) {
        s_events = xEventGroupCreate();
        ESP_RETURN_ON_FALSE(s_events, ESP_ERR_NO_MEM, TAG, "event group alloc failed");
    }

    if (!s_watchdog_timer) {
        s_watchdog_timer = xTimerCreate("core_host_wd", CORE_HOST_WATCHDOG_PERIOD_TICKS, pdTRUE, NULL, watchdog_timer_cb);
        ESP_RETURN_ON_FALSE(s_watchdog_timer, ESP_ERR_NO_MEM, TAG, "watchdog timer alloc failed");
    }

    TickType_t now = xTaskGetTickCount();
    s_last_activity_tick = now;
    s_last_ping_tick = now;
    s_ping_in_flight = false;
    s_display_alive = false;
    s_watchdog_triggered = false;

    s_initialized = true;
    ESP_LOGI(TAG, "UART host ready on port %d (TX=%d RX=%d @ %d bps)", s_config.uart_port, s_config.tx_gpio, s_config.rx_gpio, s_config.baud_rate);
    return ESP_OK;
}

esp_err_t core_host_link_start(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "core_host_link_init not called");
    if (s_started) {
        return ESP_OK;
    }

    BaseType_t task_ok = xTaskCreatePinnedToCore(rx_task, "core_host_link_rx", s_config.task_stack_size, NULL, s_config.task_priority, &s_rx_task, 0);
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "rx task creation failed");
    if (s_watchdog_timer && !xTimerIsTimerActive(s_watchdog_timer)) {
        ESP_RETURN_ON_FALSE(xTimerStart(s_watchdog_timer, 0) == pdPASS, ESP_FAIL, TAG, "watchdog timer start failed");
    }
    s_started = true;
    return ESP_OK;
}

esp_err_t core_host_link_send_hello(void)
{
    uint8_t payload = CORE_LINK_PROTOCOL_VERSION;
    return uart_send_frame(CORE_LINK_MSG_HELLO, &payload, sizeof(payload));
}

esp_err_t core_host_link_send_state(const core_link_state_frame_t *frame)
{
    ESP_RETURN_ON_FALSE(frame, ESP_ERR_INVALID_ARG, TAG, "frame null");
    ESP_RETURN_ON_FALSE(core_host_link_is_display_ready(), ESP_ERR_INVALID_STATE, TAG, "display not ready");

    core_link_state_frame_t sanitized = {0};
    uint8_t count = frame->terrarium_count;
    if (count > CORE_LINK_MAX_TERRARIUMS) {
        ESP_LOGW(TAG, "Clamping terrarium count from %u to %u", count, CORE_LINK_MAX_TERRARIUMS);
        count = CORE_LINK_MAX_TERRARIUMS;
    }

    sanitized.epoch_seconds = frame->epoch_seconds;
    sanitized.terrarium_count = count;
    for (uint8_t i = 0; i < count; ++i) {
        sanitized.terrariums[i] = frame->terrariums[i];
    }

    bool require_full = s_force_next_full || !s_last_state_valid;
    if (!require_full) {
        require_full = !ensure_baseline_compatible(&sanitized);
    }

    esp_err_t err = ESP_FAIL;
    if (!require_full) {
        bool any_change = false;
        err = send_state_delta(&sanitized, &any_change);
        if (err == ESP_OK) {
            store_last_state(&sanitized);
            if (any_change) {
                s_delta_since_full++;
                if (s_delta_since_full >= CORE_HOST_MAX_DELTAS_BEFORE_FULL) {
                    s_force_next_full = true;
                    s_delta_since_full = 0;
                }
            }
            if (s_last_full_epoch != 0 && sanitized.epoch_seconds >= s_last_full_epoch) {
                uint32_t elapsed = sanitized.epoch_seconds - s_last_full_epoch;
                if (elapsed >= CORE_HOST_FULL_REFRESH_SECONDS) {
                    s_force_next_full = true;
                }
            } else if (s_last_full_epoch != 0) {
                s_force_next_full = true;
            }
            return ESP_OK;
        }

        ESP_LOGW(TAG, "STATE_DELTA encode failed (%s), falling back to STATE_FULL", esp_err_to_name(err));
        require_full = true;
    }

    if (require_full) {
        err = send_state_full(&sanitized);
        if (err == ESP_OK) {
            store_last_state(&sanitized);
            s_last_full_epoch = sanitized.epoch_seconds;
            s_delta_since_full = 0;
            s_force_next_full = false;
        }
    }

    return err;
}

static esp_err_t send_state_full(const core_link_state_frame_t *frame)
{
    if (!frame) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t count = frame->terrarium_count;
    core_link_state_header_wire_t header = {
        .epoch_seconds = frame->epoch_seconds,
        .terrarium_count = count,
    };

    size_t payload_size = sizeof(header) + count * sizeof(core_link_snapshot_wire_t);
    if (payload_size > CORE_LINK_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t buffer[CORE_LINK_MAX_PAYLOAD];
    memcpy(buffer, &header, sizeof(header));
    uint8_t *cursor = buffer + sizeof(header);

    for (uint8_t i = 0; i < count; ++i) {
        const core_link_terrarium_snapshot_t *snap = &frame->terrariums[i];
        core_link_snapshot_wire_t wire = {0};
        wire.terrarium_id = snap->terrarium_id;
        strncpy(wire.scientific_name, snap->scientific_name, CORE_LINK_NAME_MAX_LEN);
        wire.scientific_name[CORE_LINK_NAME_MAX_LEN] = '\0';
        strncpy(wire.common_name, snap->common_name, CORE_LINK_NAME_MAX_LEN);
        wire.common_name[CORE_LINK_NAME_MAX_LEN] = '\0';
        wire.temp_day_c = snap->temp_day_c;
        wire.temp_night_c = snap->temp_night_c;
        wire.humidity_day_pct = snap->humidity_day_pct;
        wire.humidity_night_pct = snap->humidity_night_pct;
        wire.lux_day = snap->lux_day;
        wire.lux_night = snap->lux_night;
        wire.hydration_pct = snap->hydration_pct;
        wire.stress_pct = snap->stress_pct;
        wire.health_pct = snap->health_pct;
        wire.last_feeding_timestamp = snap->last_feeding_timestamp;
        wire.activity_score = snap->activity_score;
        memcpy(cursor, &wire, sizeof(wire));
        cursor += sizeof(wire);
    }

    return uart_send_frame(CORE_LINK_MSG_STATE_FULL, buffer, payload_size);
}

static esp_err_t send_state_delta(const core_link_state_frame_t *frame, bool *out_any_change)
{
    if (out_any_change) {
        *out_any_change = false;
    }
    if (!frame) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buffer[CORE_LINK_MAX_PAYLOAD];
    core_link_state_delta_header_wire_t header = {
        .epoch_seconds = frame->epoch_seconds,
        .terrarium_count = frame->terrarium_count,
        .changed_count = 0,
    };
    memcpy(buffer, &header, sizeof(header));
    size_t offset = sizeof(header);
    uint8_t changed = 0;

    for (uint8_t i = 0; i < frame->terrarium_count; ++i) {
        const core_link_terrarium_snapshot_t *snap = &frame->terrariums[i];
        const core_link_terrarium_snapshot_t *prev = find_previous_snapshot(snap->terrarium_id);
        if (!prev) {
            return ESP_ERR_INVALID_STATE;
        }

        core_link_delta_field_mask_t mask = 0;
        if (string_field_changed(snap->scientific_name, prev->scientific_name)) {
            mask |= CORE_LINK_DELTA_FIELD_SCIENTIFIC_NAME;
        }
        if (string_field_changed(snap->common_name, prev->common_name)) {
            mask |= CORE_LINK_DELTA_FIELD_COMMON_NAME;
        }
        if (float_field_changed(snap->temp_day_c, prev->temp_day_c)) {
            mask |= CORE_LINK_DELTA_FIELD_TEMP_DAY;
        }
        if (float_field_changed(snap->temp_night_c, prev->temp_night_c)) {
            mask |= CORE_LINK_DELTA_FIELD_TEMP_NIGHT;
        }
        if (float_field_changed(snap->humidity_day_pct, prev->humidity_day_pct)) {
            mask |= CORE_LINK_DELTA_FIELD_HUMIDITY_DAY;
        }
        if (float_field_changed(snap->humidity_night_pct, prev->humidity_night_pct)) {
            mask |= CORE_LINK_DELTA_FIELD_HUMIDITY_NIGHT;
        }
        if (float_field_changed(snap->lux_day, prev->lux_day)) {
            mask |= CORE_LINK_DELTA_FIELD_LUX_DAY;
        }
        if (float_field_changed(snap->lux_night, prev->lux_night)) {
            mask |= CORE_LINK_DELTA_FIELD_LUX_NIGHT;
        }
        if (float_field_changed(snap->hydration_pct, prev->hydration_pct)) {
            mask |= CORE_LINK_DELTA_FIELD_HYDRATION;
        }
        if (float_field_changed(snap->stress_pct, prev->stress_pct)) {
            mask |= CORE_LINK_DELTA_FIELD_STRESS;
        }
        if (float_field_changed(snap->health_pct, prev->health_pct)) {
            mask |= CORE_LINK_DELTA_FIELD_HEALTH;
        }
        if (snap->last_feeding_timestamp != prev->last_feeding_timestamp) {
            mask |= CORE_LINK_DELTA_FIELD_LAST_FEED;
        }
        if (float_field_changed(snap->activity_score, prev->activity_score)) {
            mask |= CORE_LINK_DELTA_FIELD_ACTIVITY;
        }

        if (!mask) {
            continue;
        }

        if (offset + sizeof(core_link_state_delta_entry_wire_t) > CORE_LINK_MAX_PAYLOAD) {
            return ESP_ERR_INVALID_SIZE;
        }

        core_link_state_delta_entry_wire_t entry = {
            .terrarium_id = snap->terrarium_id,
            .field_mask = mask,
        };
        memcpy(buffer + offset, &entry, sizeof(entry));
        offset += sizeof(entry);

        if (mask & CORE_LINK_DELTA_FIELD_SCIENTIFIC_NAME) {
            if (offset + CORE_LINK_DELTA_STRING_BYTES > CORE_LINK_MAX_PAYLOAD) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(buffer + offset, snap->scientific_name, CORE_LINK_DELTA_STRING_BYTES);
            offset += CORE_LINK_DELTA_STRING_BYTES;
        }
        if (mask & CORE_LINK_DELTA_FIELD_COMMON_NAME) {
            if (offset + CORE_LINK_DELTA_STRING_BYTES > CORE_LINK_MAX_PAYLOAD) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(buffer + offset, snap->common_name, CORE_LINK_DELTA_STRING_BYTES);
            offset += CORE_LINK_DELTA_STRING_BYTES;
        }
        if (mask & CORE_LINK_DELTA_FIELD_TEMP_DAY) {
            if (offset + sizeof(float) > CORE_LINK_MAX_PAYLOAD) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(buffer + offset, &snap->temp_day_c, sizeof(float));
            offset += sizeof(float);
        }
        if (mask & CORE_LINK_DELTA_FIELD_TEMP_NIGHT) {
            if (offset + sizeof(float) > CORE_LINK_MAX_PAYLOAD) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(buffer + offset, &snap->temp_night_c, sizeof(float));
            offset += sizeof(float);
        }
        if (mask & CORE_LINK_DELTA_FIELD_HUMIDITY_DAY) {
            if (offset + sizeof(float) > CORE_LINK_MAX_PAYLOAD) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(buffer + offset, &snap->humidity_day_pct, sizeof(float));
            offset += sizeof(float);
        }
        if (mask & CORE_LINK_DELTA_FIELD_HUMIDITY_NIGHT) {
            if (offset + sizeof(float) > CORE_LINK_MAX_PAYLOAD) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(buffer + offset, &snap->humidity_night_pct, sizeof(float));
            offset += sizeof(float);
        }
        if (mask & CORE_LINK_DELTA_FIELD_LUX_DAY) {
            if (offset + sizeof(float) > CORE_LINK_MAX_PAYLOAD) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(buffer + offset, &snap->lux_day, sizeof(float));
            offset += sizeof(float);
        }
        if (mask & CORE_LINK_DELTA_FIELD_LUX_NIGHT) {
            if (offset + sizeof(float) > CORE_LINK_MAX_PAYLOAD) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(buffer + offset, &snap->lux_night, sizeof(float));
            offset += sizeof(float);
        }
        if (mask & CORE_LINK_DELTA_FIELD_HYDRATION) {
            if (offset + sizeof(float) > CORE_LINK_MAX_PAYLOAD) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(buffer + offset, &snap->hydration_pct, sizeof(float));
            offset += sizeof(float);
        }
        if (mask & CORE_LINK_DELTA_FIELD_STRESS) {
            if (offset + sizeof(float) > CORE_LINK_MAX_PAYLOAD) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(buffer + offset, &snap->stress_pct, sizeof(float));
            offset += sizeof(float);
        }
        if (mask & CORE_LINK_DELTA_FIELD_HEALTH) {
            if (offset + sizeof(float) > CORE_LINK_MAX_PAYLOAD) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(buffer + offset, &snap->health_pct, sizeof(float));
            offset += sizeof(float);
        }
        if (mask & CORE_LINK_DELTA_FIELD_LAST_FEED) {
            if (offset + sizeof(uint32_t) > CORE_LINK_MAX_PAYLOAD) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(buffer + offset, &snap->last_feeding_timestamp, sizeof(uint32_t));
            offset += sizeof(uint32_t);
        }
        if (mask & CORE_LINK_DELTA_FIELD_ACTIVITY) {
            if (offset + sizeof(float) > CORE_LINK_MAX_PAYLOAD) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(buffer + offset, &snap->activity_score, sizeof(float));
            offset += sizeof(float);
        }

        ++changed;
    }

    ((core_link_state_delta_header_wire_t *)buffer)->changed_count = changed;
    if (out_any_change) {
        *out_any_change = changed > 0;
    }

    size_t payload_length = offset;
    return uart_send_frame(CORE_LINK_MSG_STATE_DELTA, buffer, payload_length);
}

static const core_link_terrarium_snapshot_t *find_previous_snapshot(uint8_t terrarium_id)
{
    if (!s_last_state_valid) {
        return NULL;
    }

    for (uint8_t i = 0; i < s_last_sent_state.terrarium_count; ++i) {
        if (s_last_sent_state.terrariums[i].terrarium_id == terrarium_id) {
            return &s_last_sent_state.terrariums[i];
        }
    }

    return NULL;
}

static void store_last_state(const core_link_state_frame_t *frame)
{
    if (!frame) {
        return;
    }

    memset(&s_last_sent_state, 0, sizeof(s_last_sent_state));
    memcpy(&s_last_sent_state, frame, sizeof(*frame));
    s_last_state_valid = true;
}

static void schedule_full_frame(void)
{
    s_force_next_full = true;
}

static bool ensure_baseline_compatible(const core_link_state_frame_t *frame)
{
    if (!frame || !s_last_state_valid) {
        return false;
    }

    if (frame->terrarium_count != s_last_sent_state.terrarium_count) {
        return false;
    }

    for (uint8_t i = 0; i < frame->terrarium_count; ++i) {
        if (!find_previous_snapshot(frame->terrariums[i].terrarium_id)) {
            return false;
        }
    }

    return true;
}

static bool float_field_changed(float a, float b)
{
    return fabsf(a - b) > CORE_HOST_DELTA_FLOAT_EPSILON;
}

static bool string_field_changed(const char *a, const char *b)
{
    if (!a || !b) {
        return a != b;
    }
    return strncmp(a, b, CORE_LINK_DELTA_STRING_BYTES) != 0;
}

esp_err_t core_host_link_send_ping(void)
{
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    return uart_send_frame(CORE_LINK_MSG_PING, &now_ms, sizeof(now_ms));
}

esp_err_t core_host_link_wait_for_display_ready(TickType_t ticks_to_wait)
{
    ESP_RETURN_ON_FALSE(s_started, ESP_ERR_INVALID_STATE, TAG, "core_host_link_start not called");
    EventBits_t bits = xEventGroupWaitBits(s_events, CORE_HOST_EVENT_HANDSHAKE | CORE_HOST_EVENT_DISPLAY_READY, pdFALSE, pdTRUE, ticks_to_wait);
    if ((bits & (CORE_HOST_EVENT_HANDSHAKE | CORE_HOST_EVENT_DISPLAY_READY)) != (CORE_HOST_EVENT_HANDSHAKE | CORE_HOST_EVENT_DISPLAY_READY)) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

bool core_host_link_is_handshake_complete(void)
{
    if (!s_events) {
        return false;
    }
    EventBits_t bits = xEventGroupGetBits(s_events);
    return (bits & CORE_HOST_EVENT_HANDSHAKE) != 0;
}

bool core_host_link_is_display_ready(void)
{
    if (!s_events) {
        return false;
    }
    EventBits_t bits = xEventGroupGetBits(s_events);
    return (bits & CORE_HOST_EVENT_DISPLAY_READY) != 0;
}

uint8_t core_host_link_get_peer_version(void)
{
    return s_peer_version;
}

const core_host_display_info_t *core_host_link_get_display_info(void)
{
    return core_host_link_is_display_ready() ? &s_display_info : NULL;
}

esp_err_t core_host_link_register_display_ready_cb(core_host_display_ready_cb_t cb, void *ctx)
{
    s_display_cb = cb;
    s_display_ctx = ctx;
    return ESP_OK;
}

esp_err_t core_host_link_register_request_cb(core_host_request_state_cb_t cb, void *ctx)
{
    s_request_cb = cb;
    s_request_ctx = ctx;
    return ESP_OK;
}

esp_err_t core_host_link_register_touch_cb(core_host_touch_cb_t cb, void *ctx)
{
    s_touch_cb = cb;
    s_touch_ctx = ctx;
    return ESP_OK;
}

esp_err_t core_host_link_register_command_cb(core_host_command_cb_t cb, void *ctx)
{
    s_command_cb = cb;
    s_command_ctx = ctx;
    return ESP_OK;
}

static uint8_t checksum_compute(uint8_t type, uint16_t length, const uint8_t *payload)
{
    uint32_t sum = type + (length & 0xFF) + (length >> 8);
    if (payload && length) {
        for (uint16_t i = 0; i < length; ++i) {
            sum += payload[i];
        }
    }
    return (uint8_t)(sum & 0xFF);
}

static esp_err_t uart_send_frame(core_link_msg_type_t type, const void *payload, uint16_t length)
{
    ESP_RETURN_ON_FALSE(s_started, ESP_ERR_INVALID_STATE, TAG, "link not started");
    core_link_frame_header_t header = {
        .sof = CORE_LINK_SOF,
        .type = (uint8_t)type,
        .length = length,
    };
    uint8_t checksum = checksum_compute(header.type, header.length, payload);

    uart_write_bytes(s_config.uart_port, (const char *)&header, sizeof(header));
    if (payload && length > 0) {
        uart_write_bytes(s_config.uart_port, (const char *)payload, length);
    }
    uart_write_bytes(s_config.uart_port, (const char *)&checksum, sizeof(checksum));
    return ESP_OK;
}

static void update_display_alive(bool alive)
{
    if (alive) {
        if (!s_display_alive) {
            if (s_watchdog_triggered) {
                ESP_LOGI(TAG, "Display link restored, waiting for DISPLAY_READY");
            }
            s_display_alive = true;
        }
        s_watchdog_triggered = false;
        s_ping_in_flight = false;
        return;
    }

    if (!s_display_alive) {
        return;
    }

    if (!s_watchdog_triggered) {
        ESP_LOGE(TAG, "Display watchdog expired, marking panel offline");
    }
    s_watchdog_triggered = true;
    s_display_alive = false;
    s_ping_in_flight = false;
    schedule_full_frame();
    if (s_events) {
        xEventGroupClearBits(s_events, CORE_HOST_EVENT_DISPLAY_READY);
    }
}

static void watchdog_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    if (!s_started || !s_events) {
        return;
    }

    EventBits_t bits = xEventGroupGetBits(s_events);
    if ((bits & CORE_HOST_EVENT_HANDSHAKE) == 0 || !s_display_alive) {
        return;
    }

    TickType_t now = xTaskGetTickCount();
    TickType_t elapsed = now - s_last_activity_tick;
    if (elapsed < CORE_HOST_STATE_TIMEOUT_TICKS) {
        return;
    }

    if (!s_ping_in_flight) {
        esp_err_t err = core_host_link_send_ping();
        if (err == ESP_OK) {
            s_ping_in_flight = true;
            s_last_ping_tick = now;
            ESP_LOGW(TAG, "No DISPLAY activity for %d ms, sending ping", CONFIG_CORE_APP_LINK_STATE_TIMEOUT_MS);
        } else {
            ESP_LOGE(TAG, "Failed to send watchdog ping: %s", esp_err_to_name(err));
        }
        return;
    }

    TickType_t ping_elapsed = now - s_last_ping_tick;
    if (ping_elapsed >= CORE_HOST_PING_TIMEOUT_TICKS) {
        ESP_LOGE(TAG, "Ping timeout after %d ms, marking display offline", CONFIG_CORE_APP_LINK_PING_TIMEOUT_MS);
        update_display_alive(false);
    }
}

static void handle_frame(core_link_msg_type_t type, const uint8_t *payload, uint16_t length)
{
    s_last_activity_tick = xTaskGetTickCount();
    update_display_alive(true);
    switch (type) {
        case CORE_LINK_MSG_HELLO_ACK:
            if (length >= sizeof(core_link_hello_ack_payload_t)) {
                core_link_hello_ack_payload_t ack;
                memcpy(&ack, payload, sizeof(ack));
                s_peer_version = ack.protocol_version;
            } else {
                s_peer_version = 0;
            }
            if (!core_host_link_is_handshake_complete()) {
                xEventGroupSetBits(s_events, CORE_HOST_EVENT_HANDSHAKE);
                ESP_LOGI(TAG, "Handshake acknowledged (peer protocol v%u)", s_peer_version);
            }
            break;
        case CORE_LINK_MSG_DISPLAY_READY:
            if (length >= sizeof(core_link_display_ready_payload_t)) {
                core_link_display_ready_payload_t info;
                memcpy(&info, payload, sizeof(info));
                s_display_info.width = info.width;
                s_display_info.height = info.height;
                s_display_info.protocol_version = info.protocol_version;
                xEventGroupSetBits(s_events, CORE_HOST_EVENT_DISPLAY_READY);
                schedule_full_frame();
                ESP_LOGI(TAG, "Display ready: %ux%u (protocol v%u)", info.width, info.height, info.protocol_version);
                if (s_display_cb) {
                    s_display_cb(&s_display_info, s_display_ctx);
                }
            }
            break;
        case CORE_LINK_MSG_REQUEST_STATE:
            schedule_full_frame();
            if (s_request_cb) {
                s_request_cb(s_request_ctx);
            }
            break;
        case CORE_LINK_MSG_TOUCH_EVENT:
            if (length >= sizeof(core_link_touch_event_t) && s_touch_cb) {
                core_link_touch_event_t event;
                memcpy(&event, payload, sizeof(event));
                s_touch_cb(&event, s_touch_ctx);
            }
            break;
        case CORE_LINK_MSG_COMMAND: {
            if (length < 1) {
                ESP_LOGW(TAG, "Command frame too short");
                break;
            }

            uint8_t opcode = payload[0];
            const char *argument_ptr = NULL;
            char argument[CORE_LINK_COMMAND_MAX_ARG_LEN] = {0};
            if (length > 1) {
                size_t arg_len = length - 1;
                if (arg_len >= sizeof(argument)) {
                    arg_len = sizeof(argument) - 1;
                }
                memcpy(argument, payload + 1, arg_len);
                argument[arg_len] = '\0';
                argument_ptr = argument;
            }

            uint8_t terrarium_count = 0;
            esp_err_t status = ESP_ERR_NOT_SUPPORTED;
            if (s_command_cb) {
                ESP_LOGI(TAG, "Command opcode=0x%02X arg=%s", opcode,
                         argument_ptr && argument_ptr[0] ? argument_ptr : "<default>");
                status = s_command_cb((core_link_command_opcode_t)opcode, argument_ptr, &terrarium_count, s_command_ctx);
            }

            if (terrarium_count > CORE_LINK_MAX_TERRARIUMS) {
                terrarium_count = CORE_LINK_MAX_TERRARIUMS;
            }

            core_link_command_ack_payload_t ack = {
                .opcode = opcode,
                .status = (int32_t)status,
                .terrarium_count = terrarium_count,
            };
            esp_err_t ack_err = uart_send_frame(CORE_LINK_MSG_COMMAND_ACK, &ack, sizeof(ack));
            if (ack_err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to send command ACK: %s", esp_err_to_name(ack_err));
            }
            break;
        }
        case CORE_LINK_MSG_PING:
            uart_send_frame(CORE_LINK_MSG_PONG, payload, length);
            break;
        case CORE_LINK_MSG_PONG:
            ESP_LOGV(TAG, "PONG received");
            break;
        case CORE_LINK_MSG_HELLO:
            // Display may unexpectedly send HELLO if it rebooted; respond with ACK.
            {
                core_link_hello_ack_payload_t ack = {
                    .protocol_version = CORE_LINK_PROTOCOL_VERSION,
                    .capabilities = 0x02, // host controller
                };
                uart_send_frame(CORE_LINK_MSG_HELLO_ACK, &ack, sizeof(ack));
            }
            if (!core_host_link_is_handshake_complete()) {
                xEventGroupSetBits(s_events, CORE_HOST_EVENT_HANDSHAKE);
            }
            break;
        default:
            ESP_LOGW(TAG, "Unhandled frame type 0x%02X", type);
            break;
    }
}

static void rx_task(void *arg)
{
    uint8_t header_buf[sizeof(core_link_frame_header_t)];
    uint8_t payload_buf[CORE_LINK_MAX_PAYLOAD];

    while (true) {
        int read = uart_read_bytes(s_config.uart_port, header_buf, 1, portMAX_DELAY);
        if (read != 1) {
            continue;
        }
        if (header_buf[0] != CORE_LINK_SOF) {
            continue;
        }

        int remaining = uart_read_bytes(s_config.uart_port, header_buf + 1, sizeof(core_link_frame_header_t) - 1, pdMS_TO_TICKS(50));
        if (remaining != (int)(sizeof(core_link_frame_header_t) - 1)) {
            continue;
        }

        core_link_frame_header_t header;
        memcpy(&header, header_buf, sizeof(header));

        if (header.length > CORE_LINK_MAX_PAYLOAD) {
            ESP_LOGW(TAG, "Frame payload too large: %u", header.length);
            uart_flush_input(s_config.uart_port);
            continue;
        }

        if (header.length > 0) {
            int got = uart_read_bytes(s_config.uart_port, payload_buf, header.length, pdMS_TO_TICKS(50));
            if (got != header.length) {
                ESP_LOGW(TAG, "Failed to read payload (%d/%u)", got, header.length);
                continue;
            }
        }

        uint8_t rx_checksum = 0;
        int chk = uart_read_bytes(s_config.uart_port, &rx_checksum, 1, pdMS_TO_TICKS(20));
        if (chk != 1) {
            ESP_LOGW(TAG, "Missing checksum byte");
            continue;
        }

        uint8_t computed = checksum_compute(header.type, header.length, payload_buf);
        if (computed != rx_checksum) {
            ESP_LOGW(TAG, "Checksum mismatch (got 0x%02X expected 0x%02X)", rx_checksum, computed);
            continue;
        }

        handle_frame((core_link_msg_type_t)header.type, payload_buf, header.length);
    }
}
