#include "lvgl.h"
#include "lvgl_port.h"
#include "touch_gt911.h"
#include "storage.h"
#include "game.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "simulrepile";

static void lv_tick_task(void *arg)
{
    (void)arg;
    lv_tick_inc(1);
}

static void gui_task(void *arg)
{
    (void)arg;
    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing system");
    lvgl_port_init();
    touch_gt911_init();
    storage_init();
    game_init();

    xTaskCreate(gui_task, "gui", 4096, NULL, 1, NULL);

    const esp_timer_create_args_t tick_args = {
        .callback = &lv_tick_task,
        .name = "lv_tick"
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 1000));

    game_show_main_menu();
}
