#include "link/core_link.h"

#include <string.h>

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "sdkconfig.h"

#define CORE_LINK_SOF 0xA5
#define CORE_LINK_MAX_PAYLOAD 512
#define CORE_LINK_EVENT_HANDSHAKE BIT0
#define CORE_LINK_TOUCH_QUEUE_LENGTH 8
#define CORE_LINK_TOUCH_DISPATCH_STACK 3072
#define CORE_LINK_TOUCH_MAX_POINTS 5

static const char *TAG = "core_link";

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

static core_link_config_t s_config;
static bool s_initialized = false;
static bool s_started = false;
static bool s_handshake_done = false;
static bool s_link_alive = false;
static EventGroupHandle_t s_events = NULL;
static TaskHandle_t s_rx_task = NULL;
static core_link_state_cb_t s_state_cb = NULL;
static void *s_state_ctx = NULL;
static uint8_t s_peer_version = 0;
static core_link_status_cb_t s_status_cb = NULL;
static void *s_status_ctx = NULL;
static core_link_command_ack_cb_t s_command_cb = NULL;
static void *s_command_ctx = NULL;
static TimerHandle_t s_watchdog_timer = NULL;
static TickType_t s_last_state_tick = 0;
static TickType_t s_last_ping_tick = 0;
static bool s_ping_in_flight = false;
static bool s_state_timeout_logged = false;
static bool s_watchdog_triggered = false;
static QueueHandle_t s_touch_queue = NULL;
static TaskHandle_t s_touch_dispatch_task = NULL;
static portMUX_TYPE s_touch_queue_lock = portMUX_INITIALIZER_UNLOCKED;
static core_link_touch_event_t s_touch_last_sent[CORE_LINK_TOUCH_MAX_POINTS] = {0};
static bool s_touch_last_sent_valid[CORE_LINK_TOUCH_MAX_POINTS] = {0};
static bool s_touch_active_expected[CORE_LINK_TOUCH_MAX_POINTS] = {0};
static core_link_state_frame_t s_cached_state = {0};
static bool s_cached_state_valid = false;

#define CORE_LINK_WATCHDOG_PERIOD_MS 250
#define CORE_LINK_STATE_TIMEOUT_TICKS pdMS_TO_TICKS(CONFIG_APP_CORE_LINK_STATE_TIMEOUT_MS)
#define CORE_LINK_PING_TIMEOUT_TICKS pdMS_TO_TICKS(CONFIG_APP_CORE_LINK_PING_TIMEOUT_MS)
#define CORE_LINK_WATCHDOG_PERIOD_TICKS pdMS_TO_TICKS(CORE_LINK_WATCHDOG_PERIOD_MS)

static uint8_t checksum_compute(uint8_t type, uint16_t length, const uint8_t *payload);
static esp_err_t uart_send_frame(core_link_msg_type_t type, const void *payload, uint16_t length);
static void rx_task(void *arg);
static esp_err_t handle_state_full_frame(const uint8_t *payload, uint16_t length);
static esp_err_t handle_state_delta_frame(const uint8_t *payload, uint16_t length);
static void dispatch_frame(core_link_msg_type_t type, const uint8_t *payload, uint16_t length);
static void update_link_alive(bool alive);
static void watchdog_timer_cb(TimerHandle_t timer);
static void touch_dispatch_task(void *arg);
static void touch_queue_reset(void);
static core_link_terrarium_snapshot_t *find_cached_snapshot(core_link_state_frame_t *frame, uint8_t terrarium_id);

static void touch_queue_reset(void)
{
    portENTER_CRITICAL(&s_touch_queue_lock);
    if (s_touch_queue) {
        xQueueReset(s_touch_queue);
    }
    memset(s_touch_last_sent, 0, sizeof(s_touch_last_sent));
    memset(s_touch_last_sent_valid, 0, sizeof(s_touch_last_sent_valid));
    memset(s_touch_active_expected, 0, sizeof(s_touch_active_expected));
    portEXIT_CRITICAL(&s_touch_queue_lock);
}

esp_err_t core_link_init(const core_link_config_t *config)
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
        s_watchdog_timer = xTimerCreate("core_link_wd", CORE_LINK_WATCHDOG_PERIOD_TICKS, pdTRUE, NULL, watchdog_timer_cb);
        ESP_RETURN_ON_FALSE(s_watchdog_timer, ESP_ERR_NO_MEM, TAG, "watchdog timer alloc failed");
    }

    if (!s_touch_queue) {
        s_touch_queue = xQueueCreate(CORE_LINK_TOUCH_QUEUE_LENGTH, sizeof(core_link_touch_event_t));
        ESP_RETURN_ON_FALSE(s_touch_queue, ESP_ERR_NO_MEM, TAG, "touch queue alloc failed");
    }
    touch_queue_reset();

    s_last_state_tick = xTaskGetTickCount();
    s_last_ping_tick = s_last_state_tick;
    s_ping_in_flight = false;
    s_state_timeout_logged = false;
    s_watchdog_triggered = false;
    s_link_alive = false;

    s_initialized = true;
    ESP_LOGI(TAG, "UART bridge ready on port %d (TX=%d RX=%d @ %d bps)", s_config.uart_port, s_config.tx_gpio, s_config.rx_gpio, s_config.baud_rate);
    return ESP_OK;
}

esp_err_t core_link_start(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "core_link_init not called");
    if (s_started) {
        return ESP_OK;
    }

    BaseType_t task_ok = xTaskCreatePinnedToCore(rx_task, "core_link_rx", s_config.task_stack_size, NULL, s_config.task_priority, &s_rx_task, 0);
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "rx task creation failed");

    if (!s_touch_dispatch_task) {
        uint32_t dispatch_stack = (uint32_t)s_config.task_stack_size / 2U;
        if (dispatch_stack < CORE_LINK_TOUCH_DISPATCH_STACK) {
            dispatch_stack = CORE_LINK_TOUCH_DISPATCH_STACK;
        }
        BaseType_t touch_task_ok = xTaskCreatePinnedToCore(touch_dispatch_task, "core_link_touch", dispatch_stack, NULL,
                                                           s_config.task_priority, &s_touch_dispatch_task, tskNO_AFFINITY);
        ESP_RETURN_ON_FALSE(touch_task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "touch dispatch task creation failed");
    }

    if (!xTimerIsTimerActive(s_watchdog_timer)) {
        ESP_RETURN_ON_FALSE(xTimerStart(s_watchdog_timer, 0) == pdPASS, ESP_FAIL, TAG, "watchdog timer start failed");
    }

    s_started = true;
    return ESP_OK;
}

esp_err_t core_link_register_state_callback(core_link_state_cb_t cb, void *ctx)
{
    s_state_cb = cb;
    s_state_ctx = ctx;
    return ESP_OK;
}

esp_err_t core_link_register_status_callback(core_link_status_cb_t cb, void *ctx)
{
    s_status_cb = cb;
    s_status_ctx = ctx;
    return ESP_OK;
}

esp_err_t core_link_register_command_ack_callback(core_link_command_ack_cb_t cb, void *ctx)
{
    s_command_cb = cb;
    s_command_ctx = ctx;
    return ESP_OK;
}

esp_err_t core_link_queue_touch_event(const core_link_touch_event_t *event)
{
    ESP_RETURN_ON_FALSE(event, ESP_ERR_INVALID_ARG, TAG, "touch event null");

    ESP_RETURN_ON_FALSE(s_initialized && s_touch_queue, ESP_ERR_INVALID_STATE, TAG, "core link not ready");

    uint8_t id = event->point_id;
    if (id >= CORE_LINK_TOUCH_MAX_POINTS) {
        return ESP_OK;
    }

    core_link_touch_event_t normalized = *event;

    portENTER_CRITICAL(&s_touch_queue_lock);

    bool drop_event = false;
    bool prev_expected = s_touch_active_expected[id];
    bool expected_after = prev_expected;

    switch (event->type) {
        case CORE_LINK_TOUCH_DOWN:
            if (prev_expected) {
                normalized.type = CORE_LINK_TOUCH_MOVE;
                expected_after = prev_expected;
            } else {
                normalized.type = CORE_LINK_TOUCH_DOWN;
                expected_after = true;
            }
            break;
        case CORE_LINK_TOUCH_MOVE:
            if (!prev_expected) {
                normalized.type = CORE_LINK_TOUCH_DOWN;
                expected_after = true;
            } else {
                normalized.type = CORE_LINK_TOUCH_MOVE;
                expected_after = prev_expected;
            }
            break;
        case CORE_LINK_TOUCH_UP:
            if (!prev_expected) {
                drop_event = true;
                normalized.type = CORE_LINK_TOUCH_UP;
                expected_after = prev_expected;
            } else {
                normalized.type = CORE_LINK_TOUCH_UP;
                expected_after = false;
            }
            break;
        default:
            drop_event = true;
            break;
    }

    core_link_touch_event_t queue_buffer[CORE_LINK_TOUCH_QUEUE_LENGTH] = {0};
    size_t queue_len = 0;
    while (queue_len < CORE_LINK_TOUCH_QUEUE_LENGTH && s_touch_queue &&
           xQueueReceive(s_touch_queue, &queue_buffer[queue_len], 0) == pdTRUE) {
        queue_len++;
    }

    bool coalesced = false;

    if (!drop_event) {
        for (size_t i = 0; i < queue_len; ++i) {
            core_link_touch_event_t *existing = &queue_buffer[i];
            if (existing->point_id != id) {
                continue;
            }
            if (existing->type == normalized.type) {
                if (existing->x == normalized.x && existing->y == normalized.y) {
                    drop_event = true;
                } else {
                    *existing = normalized;
                    coalesced = true;
                }
                break;
            }
        }
    }

    if (!drop_event && queue_len == 0 && s_touch_last_sent_valid[id] &&
        s_touch_last_sent[id].type == normalized.type && s_touch_last_sent[id].x == normalized.x &&
        s_touch_last_sent[id].y == normalized.y) {
        drop_event = true;
    }

    if (!drop_event && !coalesced) {
        if (queue_len == CORE_LINK_TOUCH_QUEUE_LENGTH) {
            bool removed = false;
            for (size_t i = 0; i < queue_len; ++i) {
                if (queue_buffer[i].point_id == id) {
                    for (size_t j = i; j + 1 < queue_len; ++j) {
                        queue_buffer[j] = queue_buffer[j + 1];
                    }
                    queue_len--;
                    removed = true;
                    break;
                }
            }
            if (!removed && queue_len > 0) {
                for (size_t j = 0; j + 1 < queue_len; ++j) {
                    queue_buffer[j] = queue_buffer[j + 1];
                }
                queue_len--;
            }
        }
        if (queue_len < CORE_LINK_TOUCH_QUEUE_LENGTH) {
            queue_buffer[queue_len++] = normalized;
        } else {
            drop_event = true;
        }
    }

    for (size_t i = 0; i < queue_len; ++i) {
        if (s_touch_queue && xQueueSend(s_touch_queue, &queue_buffer[i], 0) != pdTRUE) {
            ESP_LOGW(TAG, "Touch queue overflow");
            drop_event = true;
        }
    }

    if (!drop_event) {
        s_touch_active_expected[id] = expected_after;
    } else {
        s_touch_active_expected[id] = prev_expected;
    }

    portEXIT_CRITICAL(&s_touch_queue_lock);

    return ESP_OK;
}

esp_err_t core_link_send_touch_event(const core_link_touch_event_t *event)
{
    ESP_RETURN_ON_FALSE(event, ESP_ERR_INVALID_ARG, TAG, "touch event null");
    return uart_send_frame(CORE_LINK_MSG_TOUCH_EVENT, event, sizeof(*event));
}

esp_err_t core_link_send_display_ready(void)
{
    core_link_display_ready_payload_t payload = {
        .width = 1024,
        .height = 600,
        .protocol_version = CORE_LINK_PROTOCOL_VERSION,
    };
    return uart_send_frame(CORE_LINK_MSG_DISPLAY_READY, &payload, sizeof(payload));
}

esp_err_t core_link_request_state_sync(void)
{
    s_cached_state_valid = false;
    return uart_send_frame(CORE_LINK_MSG_REQUEST_STATE, NULL, 0);
}

esp_err_t core_link_send_command(core_link_command_opcode_t opcode, const char *argument)
{
    ESP_RETURN_ON_FALSE(s_started, ESP_ERR_INVALID_STATE, TAG, "link not started");

    core_link_command_payload_t payload = {
        .opcode = (uint8_t)opcode,
        .argument = {0},
    };

    uint16_t payload_len = 1;
    if (argument && argument[0] != '\0') {
        strlcpy(payload.argument, argument, sizeof(payload.argument));
        size_t arg_len = strnlen(payload.argument, sizeof(payload.argument));
        if (arg_len > 0) {
            payload_len = (uint16_t)(1 + arg_len + 1);
        }
    }

    return uart_send_frame(CORE_LINK_MSG_COMMAND, &payload, payload_len);
}

esp_err_t core_link_request_profile_reload(const char *base_path)
{
    return core_link_send_command(CORE_LINK_CMD_RELOAD_PROFILES, base_path);
}

esp_err_t core_link_wait_for_handshake(TickType_t ticks_to_wait)
{
    ESP_RETURN_ON_FALSE(s_started, ESP_ERR_INVALID_STATE, TAG, "core_link_start not called");
    EventBits_t bits = xEventGroupWaitBits(s_events, CORE_LINK_EVENT_HANDSHAKE, pdFALSE, pdTRUE, ticks_to_wait);
    if ((bits & CORE_LINK_EVENT_HANDSHAKE) == 0) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

bool core_link_is_ready(void)
{
    return s_handshake_done && s_link_alive;
}

uint8_t core_link_get_peer_version(void)
{
    return s_peer_version;
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

static void update_link_alive(bool alive)
{
    if (s_link_alive == alive) {
        return;
    }

    s_link_alive = alive;
    if (!alive) {
        if (!s_watchdog_triggered) {
            ESP_LOGE(TAG, "DevKitC link watchdog expired, switching to local simulation");
        }
        s_watchdog_triggered = true;
        s_ping_in_flight = false;
        s_state_timeout_logged = false;
        touch_queue_reset();
        s_cached_state_valid = false;
        memset(&s_cached_state, 0, sizeof(s_cached_state));
    } else {
        if (s_watchdog_triggered) {
            s_watchdog_triggered = false;
            s_state_timeout_logged = false;
            if (s_handshake_done) {
                ESP_LOGI(TAG, "DevKitC link restored, requesting state resynchronization");
                esp_err_t err = core_link_request_state_sync();
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "State resync request failed: %s", esp_err_to_name(err));
                }
            }
        }
    }

    if (s_status_cb) {
        s_status_cb(alive, s_status_ctx);
    }
}

static void watchdog_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    if (!s_started || !s_handshake_done || !s_link_alive) {
        return;
    }

    TickType_t now = xTaskGetTickCount();
    TickType_t elapsed = now - s_last_state_tick;

    if (elapsed < CORE_LINK_STATE_TIMEOUT_TICKS) {
        return;
    }

    if (!s_ping_in_flight) {
        esp_err_t err = uart_send_frame(CORE_LINK_MSG_PING, NULL, 0);
        if (err == ESP_OK) {
            s_ping_in_flight = true;
            s_last_ping_tick = now;
            if (!s_state_timeout_logged) {
                ESP_LOGW(TAG, "State update timeout (%d ms), probing DevKitC", CONFIG_APP_CORE_LINK_STATE_TIMEOUT_MS);
                s_state_timeout_logged = true;
            }
        } else {
            ESP_LOGE(TAG, "Failed to send watchdog ping: %s", esp_err_to_name(err));
        }
        return;
    }

    TickType_t ping_elapsed = now - s_last_ping_tick;
    if (ping_elapsed >= CORE_LINK_PING_TIMEOUT_TICKS) {
        ESP_LOGE(TAG, "Ping timeout after %d ms, declaring DevKitC offline", CONFIG_APP_CORE_LINK_PING_TIMEOUT_MS);
        update_link_alive(false);
        s_state_timeout_logged = false;
    }
}

static void touch_dispatch_task(void *arg)
{
    (void)arg;

    while (true) {
        if (!s_touch_queue) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        core_link_touch_event_t event;
        if (xQueueReceive(s_touch_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        esp_err_t err = core_link_send_touch_event(&event);
        if (err == ESP_OK) {
            portENTER_CRITICAL(&s_touch_queue_lock);
            if (event.point_id < CORE_LINK_TOUCH_MAX_POINTS) {
                s_touch_last_sent[event.point_id] = event;
                s_touch_last_sent_valid[event.point_id] = true;
            }
            portEXIT_CRITICAL(&s_touch_queue_lock);
        } else if (err == ESP_ERR_INVALID_STATE) {
            ESP_LOGD(TAG, "Queued touch event dropped (link not ready)");
        } else {
            ESP_LOGW(TAG, "Failed to dispatch touch event: %s", esp_err_to_name(err));
        }
    }
}

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

static esp_err_t handle_state_full_frame(const uint8_t *payload, uint16_t length)
{
    if (length < sizeof(core_link_state_header_wire_t)) {
        return ESP_ERR_INVALID_SIZE;
    }

    core_link_state_header_wire_t header;
    memcpy(&header, payload, sizeof(header));
    if (header.terrarium_count > CORE_LINK_MAX_TERRARIUMS) {
        ESP_LOGW(TAG, "Terrarium count %u exceeds max", header.terrarium_count);
        header.terrarium_count = CORE_LINK_MAX_TERRARIUMS;
    }

    size_t expected_length = sizeof(core_link_state_header_wire_t) + header.terrarium_count * sizeof(core_link_snapshot_wire_t);
    if (length < expected_length) {
        ESP_LOGW(TAG, "State frame length mismatch (%u < %zu)", length, expected_length);
        return ESP_ERR_INVALID_SIZE;
    }

    core_link_state_frame_t frame = {
        .epoch_seconds = header.epoch_seconds,
        .terrarium_count = header.terrarium_count,
    };

    const uint8_t *cursor = payload + sizeof(core_link_state_header_wire_t);
    for (uint8_t i = 0; i < frame.terrarium_count; ++i) {
        core_link_snapshot_wire_t wire;
        memcpy(&wire, cursor, sizeof(wire));
        cursor += sizeof(wire);
        frame.terrariums[i].terrarium_id = wire.terrarium_id;
        strncpy(frame.terrariums[i].scientific_name, wire.scientific_name, CORE_LINK_NAME_MAX_LEN);
        frame.terrariums[i].scientific_name[CORE_LINK_NAME_MAX_LEN] = '\0';
        strncpy(frame.terrariums[i].common_name, wire.common_name, CORE_LINK_NAME_MAX_LEN);
        frame.terrariums[i].common_name[CORE_LINK_NAME_MAX_LEN] = '\0';
        frame.terrariums[i].temp_day_c = wire.temp_day_c;
        frame.terrariums[i].temp_night_c = wire.temp_night_c;
        frame.terrariums[i].humidity_day_pct = wire.humidity_day_pct;
        frame.terrariums[i].humidity_night_pct = wire.humidity_night_pct;
        frame.terrariums[i].lux_day = wire.lux_day;
        frame.terrariums[i].lux_night = wire.lux_night;
        frame.terrariums[i].hydration_pct = wire.hydration_pct;
        frame.terrariums[i].stress_pct = wire.stress_pct;
        frame.terrariums[i].health_pct = wire.health_pct;
        frame.terrariums[i].last_feeding_timestamp = wire.last_feeding_timestamp;
        frame.terrariums[i].activity_score = wire.activity_score;
    }

    s_cached_state = frame;
    s_cached_state_valid = true;

    if (s_state_cb) {
        s_state_cb(&frame, s_state_ctx);
    }
    return ESP_OK;
}

static esp_err_t handle_state_delta_frame(const uint8_t *payload, uint16_t length)
{
    if (length < sizeof(core_link_state_delta_header_wire_t)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!s_cached_state_valid) {
        ESP_LOGW(TAG, "STATE_DELTA received without baseline");
        return ESP_ERR_INVALID_STATE;
    }

    core_link_state_delta_header_wire_t header;
    memcpy(&header, payload, sizeof(header));

    if (header.terrarium_count != s_cached_state.terrarium_count) {
        ESP_LOGW(TAG, "STATE_DELTA terrarium mismatch (%u != %u)", header.terrarium_count, s_cached_state.terrarium_count);
        return ESP_ERR_INVALID_SIZE;
    }

    size_t offset = sizeof(header);
    core_link_state_frame_t next = s_cached_state;
    next.epoch_seconds = header.epoch_seconds;
    next.terrarium_count = header.terrarium_count;

    if (header.changed_count > next.terrarium_count) {
        ESP_LOGW(TAG, "STATE_DELTA change count %u exceeds terrariums %u", header.changed_count, next.terrarium_count);
        header.changed_count = next.terrarium_count;
    }

    for (uint8_t i = 0; i < header.changed_count; ++i) {
        if (offset + sizeof(core_link_state_delta_entry_wire_t) > length) {
            return ESP_ERR_INVALID_SIZE;
        }

        core_link_state_delta_entry_wire_t entry;
        memcpy(&entry, payload + offset, sizeof(entry));
        offset += sizeof(entry);

        core_link_terrarium_snapshot_t *snap = find_cached_snapshot(&next, entry.terrarium_id);
        if (!snap) {
            ESP_LOGW(TAG, "STATE_DELTA unknown terrarium id %u", entry.terrarium_id);
            return ESP_ERR_INVALID_STATE;
        }

        core_link_delta_field_mask_t mask = entry.field_mask;

        if (mask & CORE_LINK_DELTA_FIELD_SCIENTIFIC_NAME) {
            if (offset + CORE_LINK_DELTA_STRING_BYTES > length) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(snap->scientific_name, payload + offset, CORE_LINK_DELTA_STRING_BYTES);
            snap->scientific_name[CORE_LINK_NAME_MAX_LEN] = '\0';
            offset += CORE_LINK_DELTA_STRING_BYTES;
        }
        if (mask & CORE_LINK_DELTA_FIELD_COMMON_NAME) {
            if (offset + CORE_LINK_DELTA_STRING_BYTES > length) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(snap->common_name, payload + offset, CORE_LINK_DELTA_STRING_BYTES);
            snap->common_name[CORE_LINK_NAME_MAX_LEN] = '\0';
            offset += CORE_LINK_DELTA_STRING_BYTES;
        }
        if (mask & CORE_LINK_DELTA_FIELD_TEMP_DAY) {
            if (offset + sizeof(float) > length) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(&snap->temp_day_c, payload + offset, sizeof(float));
            offset += sizeof(float);
        }
        if (mask & CORE_LINK_DELTA_FIELD_TEMP_NIGHT) {
            if (offset + sizeof(float) > length) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(&snap->temp_night_c, payload + offset, sizeof(float));
            offset += sizeof(float);
        }
        if (mask & CORE_LINK_DELTA_FIELD_HUMIDITY_DAY) {
            if (offset + sizeof(float) > length) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(&snap->humidity_day_pct, payload + offset, sizeof(float));
            offset += sizeof(float);
        }
        if (mask & CORE_LINK_DELTA_FIELD_HUMIDITY_NIGHT) {
            if (offset + sizeof(float) > length) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(&snap->humidity_night_pct, payload + offset, sizeof(float));
            offset += sizeof(float);
        }
        if (mask & CORE_LINK_DELTA_FIELD_LUX_DAY) {
            if (offset + sizeof(float) > length) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(&snap->lux_day, payload + offset, sizeof(float));
            offset += sizeof(float);
        }
        if (mask & CORE_LINK_DELTA_FIELD_LUX_NIGHT) {
            if (offset + sizeof(float) > length) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(&snap->lux_night, payload + offset, sizeof(float));
            offset += sizeof(float);
        }
        if (mask & CORE_LINK_DELTA_FIELD_HYDRATION) {
            if (offset + sizeof(float) > length) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(&snap->hydration_pct, payload + offset, sizeof(float));
            offset += sizeof(float);
        }
        if (mask & CORE_LINK_DELTA_FIELD_STRESS) {
            if (offset + sizeof(float) > length) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(&snap->stress_pct, payload + offset, sizeof(float));
            offset += sizeof(float);
        }
        if (mask & CORE_LINK_DELTA_FIELD_HEALTH) {
            if (offset + sizeof(float) > length) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(&snap->health_pct, payload + offset, sizeof(float));
            offset += sizeof(float);
        }
        if (mask & CORE_LINK_DELTA_FIELD_LAST_FEED) {
            if (offset + sizeof(uint32_t) > length) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(&snap->last_feeding_timestamp, payload + offset, sizeof(uint32_t));
            offset += sizeof(uint32_t);
        }
        if (mask & CORE_LINK_DELTA_FIELD_ACTIVITY) {
            if (offset + sizeof(float) > length) {
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(&snap->activity_score, payload + offset, sizeof(float));
            offset += sizeof(float);
        }
    }

    s_cached_state = next;
    s_cached_state_valid = true;

    if (s_state_cb) {
        core_link_state_frame_t frame = next;
        s_state_cb(&frame, s_state_ctx);
    }

    return ESP_OK;
}

static core_link_terrarium_snapshot_t *find_cached_snapshot(core_link_state_frame_t *frame, uint8_t terrarium_id)
{
    if (!frame) {
        return NULL;
    }

    for (uint8_t i = 0; i < frame->terrarium_count; ++i) {
        if (frame->terrariums[i].terrarium_id == terrarium_id) {
            return &frame->terrariums[i];
        }
    }

    return NULL;
}

static void dispatch_frame(core_link_msg_type_t type, const uint8_t *payload, uint16_t length)
{
    switch (type) {
        case CORE_LINK_MSG_HELLO: {
            if (length >= 1) {
                s_peer_version = payload[0];
            } else {
                s_peer_version = 0;
            }
            core_link_hello_ack_payload_t ack = {
                .protocol_version = CORE_LINK_PROTOCOL_VERSION,
                .capabilities = 0x01, // display endpoint
            };
            uart_send_frame(CORE_LINK_MSG_HELLO_ACK, &ack, sizeof(ack));

            if (!s_handshake_done) {
                s_handshake_done = true;
                xEventGroupSetBits(s_events, CORE_LINK_EVENT_HANDSHAKE);
                ESP_LOGI(TAG, "Handshake complete (peer protocol v%u)", s_peer_version);
            } else {
                ESP_LOGI(TAG, "Handshake refreshed (peer protocol v%u)", s_peer_version);
            }

            TickType_t now = xTaskGetTickCount();
            s_last_state_tick = now;
            s_last_ping_tick = now;
            s_ping_in_flight = false;
            s_state_timeout_logged = false;
            bool triggered = s_watchdog_triggered;
            update_link_alive(true);
            if (!triggered) {
                core_link_request_state_sync();
            }
            break;
        }
        case CORE_LINK_MSG_STATE_FULL: {
            TickType_t now = xTaskGetTickCount();
            s_last_state_tick = now;
            s_last_ping_tick = now;
            s_ping_in_flight = false;
            s_state_timeout_logged = false;
            update_link_alive(true);
            if (handle_state_full_frame(payload, length) != ESP_OK) {
                ESP_LOGW(TAG, "Invalid STATE_FULL frame received");
            }
            break;
        }
        case CORE_LINK_MSG_STATE_DELTA: {
            TickType_t now = xTaskGetTickCount();
            s_last_state_tick = now;
            s_last_ping_tick = now;
            s_ping_in_flight = false;
            s_state_timeout_logged = false;
            update_link_alive(true);
            if (handle_state_delta_frame(payload, length) != ESP_OK) {
                ESP_LOGW(TAG, "Invalid STATE_DELTA received, requesting resync");
                s_cached_state_valid = false;
                esp_err_t err = core_link_request_state_sync();
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "State resync request failed: %s", esp_err_to_name(err));
                }
            }
            break;
        }
        case CORE_LINK_MSG_COMMAND_ACK: {
            if (length < sizeof(core_link_command_ack_payload_t)) {
                ESP_LOGW(TAG, "Command ACK too short (%u)", length);
                break;
            }
            core_link_command_ack_payload_t ack;
            memcpy(&ack, payload, sizeof(ack));
            esp_err_t status = (esp_err_t)ack.status;
            ESP_LOGI(TAG, "Command ACK opcode=0x%02X status=%s terrariums=%u", ack.opcode, esp_err_to_name(status),
                     (unsigned)ack.terrarium_count);
            if (s_command_cb) {
                s_command_cb((core_link_command_opcode_t)ack.opcode, status, ack.terrarium_count, s_command_ctx);
            }
            break;
        }
        case CORE_LINK_MSG_PING:
            uart_send_frame(CORE_LINK_MSG_PONG, payload, length);
            break;
        case CORE_LINK_MSG_PONG: {
            TickType_t now = xTaskGetTickCount();
            s_last_ping_tick = now;
            update_link_alive(true);
            break;
        }
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
        dispatch_frame((core_link_msg_type_t)header.type, payload_buf, header.length);
    }
}
