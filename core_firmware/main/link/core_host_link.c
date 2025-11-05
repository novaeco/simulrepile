#include "link/core_host_link.h"

#include <string.h>

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#define CORE_LINK_SOF 0xA5
#define CORE_LINK_MAX_PAYLOAD 512
#define CORE_HOST_EVENT_HANDSHAKE BIT0
#define CORE_HOST_EVENT_DISPLAY_READY BIT1

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
static core_host_display_info_t s_display_info = {0};

static uint8_t checksum_compute(uint8_t type, uint16_t length, const uint8_t *payload);
static esp_err_t uart_send_frame(core_link_msg_type_t type, const void *payload, uint16_t length);
static void rx_task(void *arg);
static void handle_frame(core_link_msg_type_t type, const uint8_t *payload, uint16_t length);

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

    if (frame->terrarium_count > CORE_LINK_MAX_TERRARIUMS) {
        ESP_LOGW(TAG, "Clamping terrarium count from %u to %u", frame->terrarium_count, CORE_LINK_MAX_TERRARIUMS);
    }

    uint8_t count = frame->terrarium_count > CORE_LINK_MAX_TERRARIUMS ? CORE_LINK_MAX_TERRARIUMS : frame->terrarium_count;
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
        strncpy(wire.common_name, snap->common_name, CORE_LINK_NAME_MAX_LEN);
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

static void handle_frame(core_link_msg_type_t type, const uint8_t *payload, uint16_t length)
{
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
                ESP_LOGI(TAG, "Display ready: %ux%u (protocol v%u)", info.width, info.height, info.protocol_version);
                if (s_display_cb) {
                    s_display_cb(&s_display_info, s_display_ctx);
                }
            }
            break;
        case CORE_LINK_MSG_REQUEST_STATE:
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
