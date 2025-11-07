#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"

#include "esp_err.h"
#include "link/core_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t protocol_version;
} core_host_display_info_t;

typedef void (*core_host_display_ready_cb_t)(const core_host_display_info_t *info, void *ctx);
typedef void (*core_host_request_state_cb_t)(void *ctx);
typedef void (*core_host_touch_cb_t)(const core_link_touch_event_t *event, void *ctx);
typedef esp_err_t (*core_host_command_cb_t)(core_link_command_opcode_t opcode, const char *argument,
                                            uint8_t *out_terrarium_count, void *ctx);

typedef struct {
    int uart_port;
    int tx_gpio;
    int rx_gpio;
    int baud_rate;
    int task_stack_size;
    int task_priority;
    TickType_t handshake_timeout_ticks;
} core_host_link_config_t;

esp_err_t core_host_link_init(const core_host_link_config_t *config);
esp_err_t core_host_link_start(void);
esp_err_t core_host_link_send_hello(void);
esp_err_t core_host_link_send_state(const core_link_state_frame_t *frame);
esp_err_t core_host_link_send_ping(void);
esp_err_t core_host_link_wait_for_display_ready(TickType_t ticks_to_wait);
bool core_host_link_is_handshake_complete(void);
bool core_host_link_is_display_ready(void);
uint8_t core_host_link_get_peer_version(void);
const core_host_display_info_t *core_host_link_get_display_info(void);
esp_err_t core_host_link_register_display_ready_cb(core_host_display_ready_cb_t cb, void *ctx);
esp_err_t core_host_link_register_request_cb(core_host_request_state_cb_t cb, void *ctx);
esp_err_t core_host_link_register_touch_cb(core_host_touch_cb_t cb, void *ctx);
esp_err_t core_host_link_register_command_cb(core_host_command_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
