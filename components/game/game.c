#include "game.h"
#include "economy.h"
#include "environment.h"
#include "esp_log.h"
#include "lvgl.h"
#include "reptiles.h"
#include "room.h"
#include "storage.h"
#include "terrarium.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define TAG "game"

/* Persistent game state --------------------------------------------------- */

#define MAX_TERRARIUM_ITEMS 16
#define ITEM_NAME_LEN 32
#define MAX_TERRARIUMS 25

typedef struct {
  size_t item_count;                              /* Number of items */
  char items[MAX_TERRARIUM_ITEMS][ITEM_NAME_LEN]; /* Item names      */
  char decor[ITEM_NAME_LEN];
  char substrate[ITEM_NAME_LEN];
  bool heater_on;
  bool light_on;
  bool mist_on;
  float temperature;                              /* Environment     */
  float humidity;
  float uv_index;
  env_profile_t profile;                          /* Day/night profile */
  float phase_offset;                             /* Local cycle offset */
} terrarium_state_t;

typedef struct {
  char species[32];
  float temperature;
  float humidity;
  float uv_index;
  float terrarium_min_size;
  reptile_cites_t cites;
  bool requires_authorisation;
  bool requires_cdc;
  bool requires_certificat;
  float growth;
  float health;
} reptile_state_t;

typedef struct {
  reptile_state_t reptile;     /* Stored reptile information */
  terrarium_state_t terrarium; /* Terrarium inventory and env */
} terrarium_slot_t;

typedef struct {
  size_t terrarium_count; /* Number of terrariums saved   */
  terrarium_slot_t terrariums[MAX_TERRARIUMS];
  economy_t economy; /* Global economy state         */
  float env_time_scale;                          /* Simulated hours/sec */
} game_state_t;

static lv_obj_t *main_menu;
static game_state_t game_state; /* In-RAM game state snapshot */

/* Default player regulatory context: adjust at runtime if needed */
static reptile_user_ctx_t user_ctx = {
    .cites_permit = REPTILE_CITES_NONE,
    .has_authorisation = false,
    .has_cdc = false,
    .has_certificat = false,
    .region = REPTILE_REGION_FR,
};

#define SAVE_PATH "/sdcard/simulrepile.sav"

static bool save_game(void) {
  for (size_t i = 0; i < game_state.terrarium_count; ++i) {
    terrarium_slot_t *slot = &game_state.terrariums[i];
    const reptile_info_t *info = reptiles_find(slot->reptile.species);
    if (!info || !reptiles_validate(info, &user_ctx)) {
      ESP_LOGW(TAG, "Cannot save: non-compliant reptile %s", slot->reptile.species);
      return false;
    }
    if (i == 0) {
      const terrarium_t *t = terrarium_get_state();
      slot->terrarium.item_count = t->item_count;
      for (size_t j = 0; j < t->item_count && j < MAX_TERRARIUM_ITEMS; ++j) {
        strncpy(slot->terrarium.items[j], t->items[j], ITEM_NAME_LEN - 1);
        slot->terrarium.items[j][ITEM_NAME_LEN - 1] = '\0';
      }
      strncpy(slot->terrarium.decor, t->decor, ITEM_NAME_LEN - 1);
      slot->terrarium.decor[ITEM_NAME_LEN - 1] = '\0';
      strncpy(slot->terrarium.substrate, t->substrate, ITEM_NAME_LEN - 1);
      slot->terrarium.substrate[ITEM_NAME_LEN - 1] = '\0';
      slot->terrarium.heater_on = t->heater_on;
      slot->terrarium.light_on = t->light_on;
      slot->terrarium.mist_on = t->mist_on;
      slot->terrarium.temperature = t->temperature;
      slot->terrarium.humidity = t->humidity;
      slot->terrarium.uv_index = t->uv_index;
    }
  }
  game_state.env_time_scale = environment_get_time_scale();
  return storage_save(SAVE_PATH, &game_state, sizeof(game_state));
}

static bool load_game(void) {
  if (!storage_load(SAVE_PATH, &game_state, sizeof(game_state))) {
    return false;
  }
  if (game_state.terrarium_count == 0) {
    return false;
  }

  environment_set_time_scale(game_state.env_time_scale);

  for (size_t i = 0; i < game_state.terrarium_count; ++i) {
    terrarium_slot_t *slot = &game_state.terrariums[i];
    const reptile_info_t *info = reptiles_find(slot->reptile.species);
    if (!reptiles_validate(info, &user_ctx)) {
      return false;
    }
    environment_register_terrarium(&slot->terrarium.profile,
                                    terrarium_update_environment,
                                    slot->terrarium.phase_offset);
    if (i == 0) {
      terrarium_set_reptile(info);
      terrarium_update_environment(slot->terrarium.temperature,
                                   slot->terrarium.humidity,
                                   slot->terrarium.uv_index);
      terrarium_set_decor(slot->terrarium.decor);
      terrarium_set_substrate(slot->terrarium.substrate);
      terrarium_set_heater(slot->terrarium.heater_on);
      terrarium_set_light(slot->terrarium.light_on);
      terrarium_set_mist(slot->terrarium.mist_on);
      for (size_t j = 0; j < slot->terrarium.item_count; ++j) {
        terrarium_add_item(slot->terrarium.items[j]);
      }
    }
  }

  return true;
}

static void btn_new_game_event(lv_event_t *e) {
  ESP_LOGI(TAG, "Start new game");

  memset(&game_state, 0, sizeof(game_state));
  game_state.terrarium_count = 1;
  terrarium_slot_t *slot = &game_state.terrariums[0];

  const reptile_info_t *info = reptiles_find("Python regius");
  if (!reptiles_validate(info, &user_ctx)) {
    ESP_LOGE(TAG, "Invalid reptile data");
    return;
  }

  /* Populate reptile state */
  strncpy(slot->reptile.species, info->species,
          sizeof(slot->reptile.species) - 1);
  slot->reptile.temperature = info->needs.temperature;
  slot->reptile.humidity = info->needs.humidity;
  slot->reptile.uv_index = info->needs.uv_index;
  slot->reptile.terrarium_min_size = info->needs.terrarium_min_size;
  slot->reptile.cites = info->legal.cites;
  slot->reptile.requires_authorisation = info->legal.requires_authorisation;
  slot->reptile.requires_cdc = info->legal.requires_cdc;
  slot->reptile.requires_certificat = info->legal.requires_certificat;
  slot->reptile.growth = 0.0f;
  slot->reptile.health = info->needs.max_health;

  /* Host reptile and mirror environment requirements */
  terrarium_set_reptile(info);
  slot->terrarium.temperature = info->needs.temperature;
  slot->terrarium.humidity = info->needs.humidity;
  slot->terrarium.uv_index = info->needs.uv_index;
  slot->terrarium.profile.day_temp = info->needs.temperature;
  slot->terrarium.profile.night_temp = info->needs.temperature - 5.0f;
  slot->terrarium.profile.day_humidity = info->needs.humidity;
  slot->terrarium.profile.night_humidity = info->needs.humidity + 20.0f;
  slot->terrarium.profile.day_uv = info->needs.uv_index;
  slot->terrarium.phase_offset = 0.0f;
  environment_register_terrarium(&slot->terrarium.profile,
                                 terrarium_update_environment,
                                 slot->terrarium.phase_offset);

  /* Initialise economic state */
  economy_init(&game_state.economy, 100.0f, 100.0f);

  if (!save_game()) {
    ESP_LOGE(TAG, "Failed to save game");
  }

  /* After creating a new game, open the room selection view */
  room_show();
}

static void btn_resume_event(lv_event_t *e) {
  ESP_LOGI(TAG, "Resume game");
  if (!load_game()) {
    ESP_LOGE(TAG, "No saved game");
    return;
  }

  terrarium_slot_t *slot = &game_state.terrariums[0];
  ESP_LOGI(TAG, "Loaded %s T=%.1f H=%.1f UV=%.1f budget=%.2f day=%u",
           slot->reptile.species, slot->terrarium.temperature,
           slot->terrarium.humidity, slot->terrarium.uv_index,
           game_state.economy.budget, game_state.economy.day);

  /* Continue to room view */
  room_show();
}

static void btn_settings_event(lv_event_t *e) {
  ESP_LOGI(TAG, "Open settings");
  lv_obj_t *settings = lv_obj_create(NULL);
  lv_obj_t *label = lv_label_create(settings);
  lv_label_set_text(label, "Param\xC3\xA8tres");
  lv_obj_center(label);
  lv_scr_load(settings);
}

void game_init(void) {
  if (!reptiles_load()) {
    ESP_LOGE(TAG, "Failed to load reptile data");
  }
  environment_init();
  ESP_LOGI(TAG, "Game initialized");
}

void game_show_main_menu(void) {
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
