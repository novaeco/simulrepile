#include "game.h"
#include "lvgl.h"
#include "storage.h"
#include "esp_log.h"
#include "room.h"
#include "terrarium.h"
#include "reptiles.h"
#include "environment.h"
#include "economy.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>

#define TAG "game"

/* Persistent game state --------------------------------------------------- */

#define MAX_TERRARIUM_ITEMS 16
#define ITEM_NAME_LEN       32

typedef struct {
    size_t item_count;                                  /* Number of items */
    char items[MAX_TERRARIUM_ITEMS][ITEM_NAME_LEN];     /* Item names      */
    float temperature;                                  /* Environment     */
    float humidity;
    float uv_index;
} terrarium_state_t;

typedef struct {
    char species[32];
    float temperature;
    float humidity;
    float uv_index;
    float terrarium_min_size;
    bool requires_authorisation;
    bool requires_certificat;
} reptile_state_t;

typedef struct {
    reptile_state_t reptile;    /* Stored reptile information */
    terrarium_state_t terrarium;/* Terrarium inventory and env */
    economy_t economy;          /* Economy state               */
} game_state_t;

static lv_obj_t *main_menu;
static game_state_t game_state; /* In-RAM game state snapshot */

#define SAVE_PATH "/sdcard/simulrepile.sav"

static bool save_game(void)
{
    return storage_save(SAVE_PATH, &game_state, sizeof(game_state));
}

static bool load_game(void)
{
    return storage_load(SAVE_PATH, &game_state, sizeof(game_state));
}

static void btn_new_game_event(lv_event_t *e)
{
    ESP_LOGI(TAG, "Start new game");

    memset(&game_state, 0, sizeof(game_state));

    const reptile_info_t *info = reptiles_find("Python regius");
    if (!reptiles_validate(info)) {
        ESP_LOGE(TAG, "Invalid reptile data");
        return;
    }

    /* Populate reptile state */
    strncpy(game_state.reptile.species, info->species, sizeof(game_state.reptile.species) - 1);
    game_state.reptile.temperature = info->temperature;
    game_state.reptile.humidity = info->humidity;
    game_state.reptile.uv_index = info->uv_index;
    game_state.reptile.terrarium_min_size = info->terrarium_min_size;
    game_state.reptile.requires_authorisation = info->requires_authorisation;
    game_state.reptile.requires_certificat = info->requires_certificat;

    /* Initial terrarium environment mirrors reptile requirements */
    game_state.terrarium.temperature = info->temperature;
    game_state.terrarium.humidity = info->humidity;
    game_state.terrarium.uv_index = info->uv_index;
    terrarium_update_environment(info->temperature, info->humidity, info->uv_index);

    /* Initialise economic state */
    economy_init(&game_state.economy, 100.0f, 100.0f);

    if (!save_game()) {
        ESP_LOGE(TAG, "Failed to save game");
    }

    /* After creating a new game, open the room selection view */
    room_show();
}

static void btn_resume_event(lv_event_t *e)
{
    ESP_LOGI(TAG, "Resume game");

    if (!load_game()) {
        ESP_LOGE(TAG, "No saved game");
        return;
    }

    /* Restore terrarium environment */
    terrarium_update_environment(game_state.terrarium.temperature,
                                 game_state.terrarium.humidity,
                                 game_state.terrarium.uv_index);

    /* Restore terrarium inventory */
    for (size_t i = 0; i < game_state.terrarium.item_count; ++i) {
        terrarium_add_item(game_state.terrarium.items[i]);
    }

    ESP_LOGI(TAG,
             "Loaded %s T=%.1f H=%.1f UV=%.1f budget=%.2f day=%u",
             game_state.reptile.species,
             game_state.terrarium.temperature,
             game_state.terrarium.humidity,
             game_state.terrarium.uv_index,
             game_state.economy.budget,
             game_state.economy.day);

    /* Continue to room view */
    room_show();
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
    if (!reptiles_load()) {
        ESP_LOGE(TAG, "Failed to load reptile data");
    }
    environment_init();
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
