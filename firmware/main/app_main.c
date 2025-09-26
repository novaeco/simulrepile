#include "app_config.h"
#include "asset_manager.h"
#include "bsp/waveshare_7b.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
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
static TimerHandle_t s_autosave_timer;

static void autosave_callback(TimerHandle_t timer)
{
    (void)timer;
    for (size_t i = 0; i < sim_engine_terrarium_count(); ++i) {
        const sim_terrarium_state_t *state = sim_engine_get_state(i);
        if (state) {
            save_manager_save_slot(i, state);
        }
    }
    log_manager_info("Autosave completed");
}

static void simulation_task(void *arg)
{
    (void)arg;
    const TickType_t period = pdMS_TO_TICKS(500);
    while (1) {
        sim_engine_tick(500);
        vTaskDelay(period);
    }
}

static bool load_existing_saves(void)
{
    bool loaded = false;
    sim_terrarium_state_t snapshot;
    for (size_t slot = 0; slot < SAVE_SLOT_COUNT; ++slot) {
        if (save_manager_load_slot(slot, &snapshot) == 0) {
            if (sim_engine_attach_state(&snapshot) >= 0) {
                loaded = true;
            }
        }
    }
    return loaded;
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
    update_manager_check_sd();

    ui_theme_init();

    sim_engine_init();

    bool restored = load_existing_saves();
    if (!restored) {
        size_t preset_count = 0;
        const sim_species_preset_t *presets = sim_presets_default(&preset_count);
        for (size_t i = 0; i < preset_count && i < APP_MAX_TERRARIUMS; ++i) {
            sim_engine_add_terrarium(&presets[i], presets[i].display_name);
        }
    }

    lv_init();
    ui_root_init(lv_disp_get_default());

    xTaskCreate(simulation_task, "sim", 4096, NULL, 5, NULL);

    s_autosave_timer = xTimerCreate("autosave",
                                     pdMS_TO_TICKS(APP_AUTOSAVE_INTERVAL_S * 1000),
                                     pdTRUE,
                                     NULL,
                                     autosave_callback);
    if (s_autosave_timer) {
        xTimerStart(s_autosave_timer, 0);
    }
}
