#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h"

#include "link/core_link_protocol.h"
#include "core_link_protocol.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*core_link_state_cb_t)(const core_link_state_frame_t *frame, void *ctx);
typedef void (*core_link_status_cb_t)(bool connected, void *ctx);

typedef struct {
    int uart_port;
    int tx_gpio;
    int rx_gpio;
    int baud_rate;
    int task_stack_size;
    int task_priority;
    TickType_t handshake_timeout_ticks;
} core_link_config_t;

esp_err_t core_link_init(const core_link_config_t *config);
esp_err_t core_link_start(void);
esp_err_t core_link_register_state_callback(core_link_state_cb_t cb, void *ctx);
esp_err_t core_link_register_status_callback(core_link_status_cb_t cb, void *ctx);
esp_err_t core_link_queue_touch_event(const core_link_touch_event_t *event);
esp_err_t core_link_send_touch_event(const core_link_touch_event_t *event);
esp_err_t core_link_send_display_ready(void);
esp_err_t core_link_request_state_sync(void);
esp_err_t core_link_wait_for_handshake(TickType_t ticks_to_wait);
bool core_link_is_ready(void);
uint8_t core_link_get_peer_version(void);

#ifdef __cplusplus
}
#endif
