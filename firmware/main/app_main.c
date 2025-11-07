#include "app_main.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "assets/asset_cache.h"
#include "bsp/waveshare_7b.h"
#include "docs/doc_reader.h"
#include "i18n/i18n_manager.h"
#include "link/core_link.h"
#include "lvgl_port.h"
#include "persist/save_manager.h"
#include "persist/save_service.h"
#include "sim/sim_engine.h"
#include "tts/tts_stub.h"
#include "ui/ui_root.h"
#include "ui/ui_settings.h"
#include "updates/updates_manager.h"

#include "sdkconfig.h"
#include "esp_system.h"

static const char *TAG = "simulrepile";
static bool s_resync_banner_active = false;

static void ui_loop_task(void *ctx);
static void handle_core_state(const core_link_state_frame_t *frame, void *ctx);
static void handle_core_link_status(bool connected, void *ctx);
static void handle_boot_updates(void);
static void handle_command_ack(core_link_command_opcode_t opcode, esp_err_t status, uint8_t terrarium_count, void *ctx);

void app_main(void)
{
    ESP_LOGI(TAG, "Boot sequence start");
    ESP_ERROR_CHECK(bsp_init());
    ESP_ERROR_CHECK(asset_cache_init());
    ESP_ERROR_CHECK(save_manager_init("/sdcard/saves"));
    ESP_ERROR_CHECK(i18n_manager_init("/sdcard/i18n"));
    ESP_ERROR_CHECK(doc_reader_init("/sdcard/docs"));
    ESP_ERROR_CHECK(tts_stub_init());
    ESP_ERROR_CHECK(save_service_init());

    handle_boot_updates();

    core_link_config_t link_cfg = {
        .uart_port = CONFIG_APP_CORE_LINK_UART_PORT,
        .tx_gpio = CONFIG_APP_CORE_LINK_UART_TX_PIN,
        .rx_gpio = CONFIG_APP_CORE_LINK_UART_RX_PIN,
        .baud_rate = CONFIG_APP_CORE_LINK_UART_BAUD,
        .task_stack_size = 4096,
        .task_priority = 6,
        .handshake_timeout_ticks = pdMS_TO_TICKS(CONFIG_APP_CORE_LINK_HANDSHAKE_TIMEOUT_MS),
    };

    ESP_ERROR_CHECK(core_link_init(&link_cfg));
    ESP_ERROR_CHECK(core_link_register_state_callback(handle_core_state, NULL));
    ESP_ERROR_CHECK(core_link_register_status_callback(handle_core_link_status, NULL));
    ESP_ERROR_CHECK(core_link_register_command_ack_callback(handle_command_ack, NULL));
    ESP_ERROR_CHECK(core_link_start());

    esp_err_t wait_err = core_link_wait_for_handshake(link_cfg.handshake_timeout_ticks);
    if (wait_err == ESP_OK) {
        ESP_LOGI(TAG, "Core link handshake established (peer v%u)", core_link_get_peer_version());
        ESP_ERROR_CHECK(core_link_request_state_sync());
    } else {
        ESP_LOGW(TAG, "Core link handshake timed out");
    }

    ESP_ERROR_CHECK(lvgl_port_init());

    sim_engine_init();
    (void)sim_engine_handle_link_status(core_link_is_ready());
    ui_root_init();
    ui_root_show_boot_splash();
    ui_root_show_disclaimer();

    if (core_link_is_ready()) {
        ESP_ERROR_CHECK(core_link_send_display_ready());
    }

    xTaskCreatePinnedToCore(ui_loop_task, "ui_loop", 4096, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "Initialization complete");
}

static void handle_boot_updates(void)
{
    esp_err_t err = updates_finalize_boot_state();
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "OTA finalize reported %s", esp_err_to_name(err));
    }

    updates_manifest_info_t manifest;
    err = updates_check_available(&manifest);
    if (err == ESP_OK) {
        const char *version = manifest.version[0] ? manifest.version : "?";
        ESP_LOGI(TAG,
                 "SD update detected (v%s, %u KiB) → flashing before UI",
                 version,
                 (unsigned)((manifest.size_bytes + 1023) / 1024));
        esp_err_t apply_err = updates_apply(&manifest);
        if (apply_err == ESP_OK) {
            ESP_LOGI(TAG, "Update copied to OTA slot. Rebooting...");
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        } else {
            ESP_LOGE(TAG, "SD update apply failed: %s", esp_err_to_name(apply_err));
        }
    } else if (err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "SD update manifest check failed: %s", esp_err_to_name(err));
    }
}

static void ui_loop_task(void *ctx)
{
    const TickType_t period = pdMS_TO_TICKS(1000 / 30); // 30 Hz loop
    while (true) {
        ui_root_update();
        asset_cache_tick();
        vTaskDelay(period);
    }
}

static void handle_core_state(const core_link_state_frame_t *frame, void *ctx)
{
    (void)ctx;
    if (!frame) {
        return;
    }
    esp_err_t err = sim_engine_apply_remote_snapshot(frame);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply remote snapshot: %s", esp_err_to_name(err));
    } else if (s_resync_banner_active) {
        ui_root_set_link_alert(false, NULL);
        s_resync_banner_active = false;
    }
}

static void handle_core_link_status(bool connected, void *ctx)
{
    (void)ctx;
    const char *alert = sim_engine_handle_link_status(connected);
    if (connected) {
        ESP_LOGI(TAG, "Core link watchdog cleared: DevKitC reachable");
        if (alert && alert[0] != '\0') {
            ui_root_set_link_alert(true, alert);
            s_resync_banner_active = true;
        } else {
            ui_root_set_link_alert(false, alert);
            s_resync_banner_active = false;
        }
        if (core_link_is_ready()) {
            esp_err_t err = core_link_send_display_ready();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to notify display ready: %s", esp_err_to_name(err));
            }
        }
    } else {
        ESP_LOGW(TAG, "Core link watchdog tripped: falling back to local simulation");
        ui_root_set_link_alert(true, alert);
        s_resync_banner_active = false;
    }
}

static void handle_command_ack(core_link_command_opcode_t opcode, esp_err_t status, uint8_t terrarium_count, void *ctx)
{
    (void)ctx;

    if (opcode != CORE_LINK_CMD_RELOAD_PROFILES) {
        return;
    }

    ui_settings_on_profiles_reload(status, terrarium_count);

    if (status == ESP_OK || status == ESP_ERR_NOT_FOUND) {
        sim_engine_hint_remote_count(terrarium_count);
        if (core_link_is_ready()) {
            esp_err_t err = core_link_request_state_sync();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Échec requête sync après reload: %s", esp_err_to_name(err));
            }
        }
    } else {
        ESP_LOGW(TAG, "Reload profils rejeté: %s", esp_err_to_name(status));
    }
}
