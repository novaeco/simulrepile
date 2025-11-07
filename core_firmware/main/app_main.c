#include "app_main.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "link/core_host_link.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "state/core_state_manager.h"

static const char *TAG = "simulrepile_core";

static void handshake_task(void *ctx);
static void state_update_task(void *ctx);
static void state_publish_task(void *ctx);
static void handle_display_ready(const core_host_display_info_t *info, void *ctx);
static void handle_state_request(void *ctx);
static void handle_touch_event(const core_link_touch_event_t *event, void *ctx);
static esp_err_t handle_command(core_link_command_opcode_t opcode, const char *argument, uint8_t *out_count, void *ctx);
static void publish_snapshot(void);

esp_err_t app_initialize(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs init failed");

    core_state_manager_init();

    core_host_link_config_t link_cfg = {
        .uart_port = CONFIG_CORE_APP_LINK_UART_PORT,
        .tx_gpio = CONFIG_CORE_APP_LINK_UART_TX_PIN,
        .rx_gpio = CONFIG_CORE_APP_LINK_UART_RX_PIN,
        .baud_rate = CONFIG_CORE_APP_LINK_UART_BAUD,
        .task_stack_size = 4096,
        .task_priority = 6,
        .handshake_timeout_ticks = pdMS_TO_TICKS(CONFIG_CORE_APP_HANDSHAKE_TIMEOUT_MS),
    };

    ESP_ERROR_CHECK(core_host_link_init(&link_cfg));
    ESP_ERROR_CHECK(core_host_link_register_display_ready_cb(handle_display_ready, NULL));
    ESP_ERROR_CHECK(core_host_link_register_request_cb(handle_state_request, NULL));
    ESP_ERROR_CHECK(core_host_link_register_touch_cb(handle_touch_event, NULL));
    ESP_ERROR_CHECK(core_host_link_register_command_cb(handle_command, NULL));
    ESP_ERROR_CHECK(core_host_link_start());

    xTaskCreatePinnedToCore(handshake_task, "core_handshake", 3072, NULL, 7, NULL, 0);
    xTaskCreatePinnedToCore(state_update_task, "core_state_update", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(state_publish_task, "core_state_publish", 4096, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "Core firmware initialized");
    return ESP_OK;
}

void app_main(void)
{
    ESP_ERROR_CHECK(app_initialize());
}

static void handshake_task(void *ctx)
{
    (void)ctx;
    while (!core_host_link_is_handshake_complete()) {
        ESP_ERROR_CHECK(core_host_link_send_hello());
        vTaskDelay(pdMS_TO_TICKS(CONFIG_CORE_APP_HANDSHAKE_RETRY_MS));
    }
    ESP_LOGI(TAG, "Handshake complete (peer protocol v%u)", core_host_link_get_peer_version());
    if (core_host_link_wait_for_display_ready(pdMS_TO_TICKS(CONFIG_CORE_APP_HANDSHAKE_TIMEOUT_MS)) != ESP_OK) {
        ESP_LOGW(TAG, "Display ready timeout");
    }
    publish_snapshot();
    vTaskDelete(NULL);
}

static void state_update_task(void *ctx)
{
    (void)ctx;
    const TickType_t period = pdMS_TO_TICKS(100);
    while (true) {
        core_state_manager_update(0.1f);
        vTaskDelay(period);
    }
}

static void state_publish_task(void *ctx)
{
    (void)ctx;
    const TickType_t period = pdMS_TO_TICKS(CONFIG_CORE_APP_STATE_PUBLISH_INTERVAL_MS);
    while (true) {
        if (core_host_link_is_display_ready()) {
            publish_snapshot();
        }
        vTaskDelay(period);
    }
}

static void handle_display_ready(const core_host_display_info_t *info, void *ctx)
{
    (void)ctx;
    if (info) {
        ESP_LOGI(TAG, "Display ready at %ux%u (protocol v%u)", info->width, info->height, info->protocol_version);
    }
    publish_snapshot();
}

static void handle_state_request(void *ctx)
{
    (void)ctx;
    publish_snapshot();
}

static void handle_touch_event(const core_link_touch_event_t *event, void *ctx)
{
    (void)ctx;
    core_state_manager_apply_touch(event);
}

static esp_err_t handle_command(core_link_command_opcode_t opcode, const char *argument, uint8_t *out_count, void *ctx)
{
    (void)ctx;
    esp_err_t status = ESP_ERR_NOT_SUPPORTED;

    switch (opcode) {
        case CORE_LINK_CMD_RELOAD_PROFILES: {
            const char *path = (argument && argument[0] != '\0') ? argument : NULL;
            status = core_state_manager_reload_profiles(path);
            if (status == ESP_OK || status == ESP_ERR_NOT_FOUND) {
                publish_snapshot();
            }
            break;
        }
        default:
            ESP_LOGW(TAG, "Unhandled command opcode 0x%02X", opcode);
            break;
    }

    if (out_count) {
        *out_count = (uint8_t)core_state_manager_get_terrarium_count();
    }

    return status;
}

static void publish_snapshot(void)
{
    core_link_state_frame_t frame = {0};
    core_state_manager_build_frame(&frame);
    esp_err_t err = core_host_link_send_state(&frame);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send state frame: %s", esp_err_to_name(err));
    }
}
