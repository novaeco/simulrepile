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
#include "lvgl_port.h"
#include "persist/save_manager.h"
#include "sim/sim_engine.h"
#include "ui/ui_root.h"

static const char *TAG = "simulrepile";

static void simulation_task(void *ctx);

void app_main(void)
{
    ESP_LOGI(TAG, "Boot sequence start");
    ESP_ERROR_CHECK(bsp_init());
    ESP_ERROR_CHECK(asset_cache_init());
    ESP_ERROR_CHECK(save_manager_init("/sdcard/saves"));
    ESP_ERROR_CHECK(i18n_manager_init("/sdcard/i18n"));
    ESP_ERROR_CHECK(doc_reader_init("/sdcard/docs"));
    ESP_ERROR_CHECK(lvgl_port_init());

    ui_root_init();
    ui_root_show_boot_splash();
    ui_root_show_disclaimer();

    sim_engine_init();
    xTaskCreatePinnedToCore(simulation_task, "sim_engine", 4096, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "Initialization complete");
}

static void simulation_task(void *ctx)
{
    const TickType_t period = pdMS_TO_TICKS(1000 / 30); // 30 Hz loop
    while (true) {
        sim_engine_step(1.0f / 30.0f);
        ui_root_update();
        vTaskDelay(period);
    }
}
