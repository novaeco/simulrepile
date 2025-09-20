#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

#include "esp_err.h"

typedef uint32_t TickType_t;

typedef struct {
    uint32_t clk_src_hz;
} twai_timing_config_t;

typedef struct {
    uint32_t acceptance_code;
} twai_filter_config_t;

typedef struct {
    uint32_t mode;
} twai_general_config_t;

typedef struct {
    uint32_t identifier;
    uint8_t data_length_code;
    uint8_t data[8];
    uint32_t flags;
    bool extd;
    bool rtr;
} twai_message_t;

typedef struct {
    uint32_t bus_error_count;
    uint32_t msgs_to_tx;
} twai_status_info_t;

#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY UINT32_MAX

#define TWAI_ALERT_TX_SUCCESS   (1u << 0)
#define TWAI_ALERT_TX_FAILED    (1u << 1)
#define TWAI_ALERT_RX_DATA      (1u << 2)
#define TWAI_ALERT_RX_QUEUE_FULL (1u << 3)
#define TWAI_ALERT_ERR_PASS     (1u << 4)
#define TWAI_ALERT_BUS_ERROR    (1u << 5)

#define TWAI_MSG_FLAG_NONE 0u

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t twai_driver_install(const twai_general_config_t *g_config,
                              const twai_timing_config_t *t_config,
                              const twai_filter_config_t *f_config);

esp_err_t twai_start(void);

esp_err_t twai_reconfigure_alerts(uint32_t alerts, uint32_t *old_alerts);

esp_err_t twai_read_alerts(uint32_t *alerts, TickType_t ticks_to_wait);

esp_err_t twai_get_status_info(twai_status_info_t *status_info);

esp_err_t twai_transmit(const twai_message_t *message, TickType_t ticks_to_wait);

esp_err_t twai_receive(twai_message_t *message, TickType_t ticks_to_wait);

#ifdef __cplusplus
}
#endif

