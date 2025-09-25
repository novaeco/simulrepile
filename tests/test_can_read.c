#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "can.h"

static esp_err_t g_stub_receive_status = ESP_ERR_TIMEOUT;
static twai_message_t g_stub_receive_message;
static bool g_stub_receive_has_message = false;

static void twai_stub_set_receive_result(esp_err_t status,
                                         const twai_message_t *message)
{
    g_stub_receive_status = status;
    if (message)
    {
        g_stub_receive_message = *message;
        g_stub_receive_has_message = true;
    }
    else
    {
        memset(&g_stub_receive_message, 0, sizeof(g_stub_receive_message));
        g_stub_receive_has_message = false;
    }
}

esp_err_t twai_driver_install(const twai_general_config_t *g_config,
                              const twai_timing_config_t *t_config,
                              const twai_filter_config_t *f_config)
{
    (void)g_config;
    (void)t_config;
    (void)f_config;
    return ESP_OK;
}

esp_err_t twai_start(void)
{
    return ESP_OK;
}

esp_err_t twai_reconfigure_alerts(uint32_t alerts, uint32_t *old_alerts)
{
    if (old_alerts)
    {
        *old_alerts = alerts;
    }
    return ESP_OK;
}

esp_err_t twai_read_alerts(uint32_t *alerts, TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
    if (alerts)
    {
        *alerts = 0;
    }
    return ESP_OK;
}

esp_err_t twai_get_status_info(twai_status_info_t *status_info)
{
    if (status_info)
    {
        status_info->bus_error_count = 0;
        status_info->msgs_to_tx = 0;
    }
    return ESP_OK;
}

esp_err_t twai_transmit(const twai_message_t *message, TickType_t ticks_to_wait)
{
    (void)message;
    (void)ticks_to_wait;
    return ESP_OK;
}

esp_err_t twai_receive(twai_message_t *message, TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
    if ((g_stub_receive_status == ESP_OK) && message && g_stub_receive_has_message)
    {
        *message = g_stub_receive_message;
        g_stub_receive_has_message = false;
    }
    return g_stub_receive_status;
}

static void test_no_frame_keeps_callers_buffer_pristine(void)
{
    can_message_t msg;
    memset(&msg, 0xA5, sizeof(msg));
    uint8_t snapshot[sizeof(msg)];
    memcpy(snapshot, &msg, sizeof(msg));

    twai_stub_set_receive_result(ESP_ERR_TIMEOUT, NULL);

    esp_err_t status = can_read_Byte(&msg);
    assert(status == ESP_ERR_TIMEOUT);
    assert(memcmp(snapshot, &msg, sizeof(msg)) == 0);
}

static void test_frame_copied_once_and_preserved_on_timeout(void)
{
    can_message_t msg;
    memset(&msg, 0, sizeof(msg));

    twai_message_t frame = {
        .identifier = 0x123,
        .data_length_code = 3,
        .data = {0xDE, 0xAD, 0xBE},
        .flags = TWAI_MSG_FLAG_NONE,
        .extd = false,
        .rtr = false,
    };

    twai_stub_set_receive_result(ESP_OK, &frame);

    esp_err_t status = can_read_Byte(&msg);
    assert(status == ESP_OK);
    assert(msg.identifier == frame.identifier);
    assert(msg.data_length_code == frame.data_length_code);
    assert(memcmp(msg.data, frame.data, frame.data_length_code) == 0);

    can_message_t preserved = msg;

    twai_stub_set_receive_result(ESP_ERR_TIMEOUT, NULL);

    status = can_read_Byte(&msg);
    assert(status == ESP_ERR_TIMEOUT);
    assert(memcmp(&msg, &preserved, sizeof(msg)) == 0);
}

int main(void)
{
    test_no_frame_keeps_callers_buffer_pristine();
    test_frame_copied_once_and_preserved_on_timeout();
    puts("can_read_Byte tests passed");
    return 0;
}
