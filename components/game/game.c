#include "game.h"
#include "lvgl.h"
#include "storage.h"
#include "esp_log.h"
#include <string.h>

#define TAG "game"

typedef struct {
    char species[32];
    float temperature;
    float humidity;
} reptile_t;

static lv_obj_t *main_menu;

static void btn_new_game_event(lv_event_t *e)
{
    ESP_LOGI(TAG, "Start new game");
    // TODO: implement new game logic
}

static void btn_resume_event(lv_event_t *e)
{
    ESP_LOGI(TAG, "Resume game");
    // TODO: load saved game from storage
}

static void btn_settings_event(lv_event_t *e)
{
    ESP_LOGI(TAG, "Open settings");
    // TODO: implement settings UI
}

void game_init(void)
{
    // Placeholder for game state initialization
    ESP_LOGI(TAG, "Game initialized");
}

void game_show_main_menu(void)
{
    main_menu = lv_obj_create(NULL);
    lv_scr_load(main_menu);

    lv_obj_t *btn_new = lv_btn_create(main_menu);
    lv_obj_set_size(btn_new, 250, 80);
    lv_obj_align(btn_new, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_t *label_new = lv_label_create(btn_new);
    lv_label_set_text(label_new, "Nouvelle partie");
    lv_obj_center(label_new);
    lv_obj_add_event_cb(btn_new, btn_new_game_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_resume = lv_btn_create(main_menu);
    lv_obj_set_size(btn_resume, 250, 80);
    lv_obj_align(btn_resume, LV_ALIGN_TOP_MID, 0, 160);
    lv_obj_t *label_resume = lv_label_create(btn_resume);
    lv_label_set_text(label_resume, "Reprendre");
    lv_obj_center(label_resume);
    lv_obj_add_event_cb(btn_resume, btn_resume_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_settings = lv_btn_create(main_menu);
    lv_obj_set_size(btn_settings, 250, 80);
    lv_obj_align(btn_settings, LV_ALIGN_TOP_MID, 0, 280);
    lv_obj_t *label_settings = lv_label_create(btn_settings);
    lv_label_set_text(label_settings, "Param\xC3\xA8tres");
    lv_obj_center(label_settings);
    lv_obj_add_event_cb(btn_settings, btn_settings_event, LV_EVENT_CLICKED, NULL);
}
