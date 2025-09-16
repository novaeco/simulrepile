#include "lvgl.h"
#include "lvgl_port.h"
#include "touch_gt911.h"
#include "storage.h"
#include "game.h"
#include "real_mode.h"
#include "dashboard.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "simulrepile";
static lv_obj_t *mode_selector_screen;

static void lv_tick_task(void *arg)
{
    (void)arg;
    lv_tick_inc(1);
}

static void gui_task(void *arg)
{
    (void)arg;
    while (1) {
        if (lvgl_port_lock(LVGL_PORT_LOCK_INFINITE)) {
            lv_timer_handler();
            lvgl_port_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void sim_btn_event_handler(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Switching to simulation mode");
    game_show_main_menu();
}

static void real_btn_event_handler(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Initializing real mode");
    real_mode_init();
    real_mode_detect_devices();
    dashboard_show();
    xTaskCreate(real_mode_loop, "real_mode", 4096, NULL, 1, NULL);
}

static lv_obj_t *create_mode_button(lv_obj_t *parent,
                                    const char *text,
                                    lv_event_cb_t event_cb,
                                    lv_coord_t y_offset)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_coord_t width = 320;
    lv_disp_t *disp = lv_disp_get_default();
    if (disp) {
        width = (lv_coord_t)((lv_disp_get_hor_res(disp) * 3) / 5);
    }
    lv_obj_set_width(btn, width);
    lv_obj_set_style_pad_all(btn, 16, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, y_offset);
    lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    return btn;
}

void show_mode_selector(void)
{
    if (mode_selector_screen == NULL) {
        mode_selector_screen = lv_obj_create(NULL);
        lv_obj_set_style_pad_all(mode_selector_screen, 32, LV_PART_MAIN);

        lv_obj_t *title = lv_label_create(mode_selector_screen);
        lv_label_set_text(title, "Sélection du mode");
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, -120);

        create_mode_button(mode_selector_screen, "Simulation", sim_btn_event_handler, -30);
        create_mode_button(mode_selector_screen, "Réel", real_btn_event_handler, 70);
    }

    lv_scr_load(mode_selector_screen);
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
