#include "lvgl.h"
#include "lvgl_port.h"
#include "touch_gt911.h"
#include "storage.h"
#include "game.h"
#include "real_terrarium.h"
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

static void sim_btn_event_handler(lv_event_t *e)
{
    (void)e;
    game_show_main_menu();
}

static void real_btn_event_handler(lv_event_t *e)
{
    (void)e;
    real_terrarium_init();
    real_terrarium_show_main_screen();
}

void show_mode_selector(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);

    lv_obj_t *btn_sim = lv_btn_create(scr);
    lv_obj_align(btn_sim, LV_ALIGN_CENTER, 0, -40);
    lv_obj_t *label_sim = lv_label_create(btn_sim);
    lv_label_set_text(label_sim, "Simulation");
    lv_obj_center(label_sim);
    lv_obj_add_event_cb(btn_sim, sim_btn_event_handler, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_real = lv_btn_create(scr);
    lv_obj_align(btn_real, LV_ALIGN_CENTER, 0, 40);
    lv_obj_t *label_real = lv_label_create(btn_real);
    lv_label_set_text(label_real, "Réel");
    lv_obj_center(label_real);
    lv_obj_add_event_cb(btn_real, real_btn_event_handler, LV_EVENT_CLICKED, NULL);

    lv_scr_load(scr);
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

    show_mode_selector();
}
