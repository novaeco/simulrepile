#include "game.h"
#include "lvgl.h"
#include "storage.h"
#include "esp_log.h"
#include "room.h"
#include "terrarium.h"
#include <string.h>
#include <stdlib.h>

#define TAG "game"

typedef struct {
    char species[32];
    float temperature;
    float humidity;
    float uv_index;
} reptile_t;

static lv_obj_t *main_menu;
static reptile_t *game_state;

#define SAVE_PATH "/sdcard/game.dat"

static void btn_new_game_event(lv_event_t *e)
{
    ESP_LOGI(TAG, "Start new game");
    if (game_state) {
        free(game_state);
    }
    game_state = calloc(1, sizeof(reptile_t));
    if (!game_state) {
        ESP_LOGE(TAG, "Allocation failed");
        return;
    }
    strncpy(game_state->species, "Serpent", sizeof(game_state->species) - 1);
    game_state->temperature = 28.0f;
    game_state->humidity = 60.0f;
    game_state->uv_index = 3.0f;
    terrarium_update_environment(game_state->temperature, game_state->humidity,
                                 game_state->uv_index);
    if (!storage_save(SAVE_PATH, game_state, sizeof(reptile_t))) {
        ESP_LOGE(TAG, "Failed to save game");
    }

    /* After creating a new game, open the room selection view */
    room_show();
}

static void btn_resume_event(lv_event_t *e)
{
    ESP_LOGI(TAG, "Resume game");
    if (!game_state) {
        game_state = malloc(sizeof(reptile_t));
        if (!game_state) {
            ESP_LOGE(TAG, "Allocation failed");
            return;
        }
    }
    if (!storage_load(SAVE_PATH, game_state, sizeof(reptile_t))) {
        ESP_LOGE(TAG, "No saved game");
        return;
    }
    terrarium_update_environment(game_state->temperature, game_state->humidity,
                                 game_state->uv_index);
    ESP_LOGI(TAG, "Loaded %s T=%.1f H=%.1f UV=%.1f", game_state->species,
             game_state->temperature, game_state->humidity, game_state->uv_index);
}

static void btn_settings_event(lv_event_t *e)
{
    ESP_LOGI(TAG, "Open settings");
    lv_obj_t *settings = lv_obj_create(NULL);
    lv_obj_t *label = lv_label_create(settings);
    lv_label_set_text(label, "Param\xC3\xA8tres");
    lv_obj_center(label);
    lv_scr_load(settings);
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
