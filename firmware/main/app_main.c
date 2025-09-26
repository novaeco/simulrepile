#include "app_config.h"
#include "asset_manager.h"
#include "bsp/waveshare_7b.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i18n/i18n.h"
#include "logging/log_manager.h"
#include "persist/save_manager.h"
#include "sim/sim_engine.h"
#include "sim/sim_presets.h"
#include "ui/ui_root.h"
#include "ui/ui_theme.h"
#include "update/update_manager.h"
#include "virtual_sensors.h"

static const char *TAG = "app_main";

static void simulation_task(void *arg)
{
    (void)arg;
    const TickType_t period = pdMS_TO_TICKS(500);
    while (1) {
        sim_engine_tick(500);
        vTaskDelay(period);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "SimulRepile starting, board=%s", bsp_board_revision_str());
    log_manager_init();
    asset_manager_init();
    save_manager_init();
    update_manager_init();
    i18n_init();

    bsp_display_init();
    bsp_touch_init();
    bsp_sdcard_init();

    ui_theme_init();

    sim_engine_init();

    size_t preset_count = 0;
    const sim_species_preset_t *presets = sim_presets_default(&preset_count);
    for (size_t i = 0; i < preset_count && i < APP_MAX_TERRARIUMS; ++i) {
        sim_engine_add_terrarium(&presets[i], presets[i].display_name);
    }

    lv_init();
    ui_root_init(lv_disp_get_default());

    xTaskCreate(simulation_task, "sim", 4096, NULL, 5, NULL);
}
