#include "link/core_link.h"

#include <string.h>

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#define CORE_LINK_SOF 0xA5
#define CORE_LINK_MAX_PAYLOAD 512
#define CORE_LINK_EVENT_HANDSHAKE BIT0

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
static EventGroupHandle_t s_events = NULL;
static TaskHandle_t s_rx_task = NULL;
static core_link_state_cb_t s_state_cb = NULL;
static void *s_state_ctx = NULL;
static uint8_t s_peer_version = 0;

static uint8_t checksum_compute(uint8_t type, uint16_t length, const uint8_t *payload);
static esp_err_t uart_send_frame(core_link_msg_type_t type, const void *payload, uint16_t length);
static void rx_task(void *arg);
static esp_err_t handle_state_frame(const uint8_t *payload, uint16_t length);
static void dispatch_frame(core_link_msg_type_t type, const uint8_t *payload, uint16_t length);

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
    s_started = true;
    return ESP_OK;
}

esp_err_t core_link_register_state_callback(core_link_state_cb_t cb, void *ctx)
{
    s_state_cb = cb;
    s_state_ctx = ctx;
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
    return uart_send_frame(CORE_LINK_MSG_REQUEST_STATE, NULL, 0);
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
    return s_handshake_done;
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

static esp_err_t handle_state_frame(const uint8_t *payload, uint16_t length)
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

    if (s_state_cb) {
        s_state_cb(&frame, s_state_ctx);
    }
    return ESP_OK;
}

static void dispatch_frame(core_link_msg_type_t type, const uint8_t *payload, uint16_t length)
{
    switch (type) {
        case CORE_LINK_MSG_HELLO:
            if (length >= 1) {
                s_peer_version = payload[0];
            } else {
                s_peer_version = 0;
            }
            {
                core_link_hello_ack_payload_t ack = {
                    .protocol_version = CORE_LINK_PROTOCOL_VERSION,
                    .capabilities = 0x01, // display endpoint
                };
                uart_send_frame(CORE_LINK_MSG_HELLO_ACK, &ack, sizeof(ack));
            }
            if (!s_handshake_done) {
                s_handshake_done = true;
                xEventGroupSetBits(s_events, CORE_LINK_EVENT_HANDSHAKE);
                ESP_LOGI(TAG, "Handshake complete (peer protocol v%u)", s_peer_version);
            } else {
                ESP_LOGI(TAG, "Handshake refreshed (peer protocol v%u)", s_peer_version);
            }
            core_link_request_state_sync();
            break;
        case CORE_LINK_MSG_STATE_FULL:
            if (handle_state_frame(payload, length) != ESP_OK) {
                ESP_LOGW(TAG, "Invalid state frame received");
            }
            break;
        case CORE_LINK_MSG_PING:
            uart_send_frame(CORE_LINK_MSG_PONG, payload, length);
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
        dispatch_frame((core_link_msg_type_t)header.type, payload_buf, header.length);
    }
}
