#include "game.h"
#include "economy.h"
#include "environment.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "reptiles.h"
#include "room.h"
#include "terrarium_ui/ui.h"
#include "storage.h"
#include "terrarium.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define TAG "game"

/* Persistent game state --------------------------------------------------- */

#define MAX_TERRARIUM_ITEMS TERRARIUM_MAX_ITEMS
#define ITEM_NAME_LEN TERRARIUM_ITEM_NAME_LEN
#define MAX_TERRARIUMS GAME_MAX_TERRARIUMS

typedef struct {
  size_t item_count;                              /* Number of items */
  char items[MAX_TERRARIUM_ITEMS][ITEM_NAME_LEN]; /* Item names      */
  char name[ITEM_NAME_LEN];
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
  float max_health;
  bool mature;
  bool sick;
  bool alive;
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
static size_t current_slot;      /* Currently selected terrarium */
static float simulated_hours_accum; /* Accumulator for day/night cycle */

static void rebuild_environment_bindings(void);
static void environment_slot_cb(void *ctx, float temperature, float humidity,
                                float uv_index);
static void apply_terrarium_environment(terrarium_slot_t *slot,
                                        float temperature, float humidity,
                                        float uv_index);
static float compute_daily_revenue(void);
static void advance_simulated_day(void);
static void commit_slot_from_model(terrarium_slot_t *slot);
static void update_economy_from_health(void);

size_t game_get_terrarium_count(void) { return game_state.terrarium_count; }

size_t game_get_current_slot(void) { return current_slot; }

size_t game_add_terrarium(void) {
  if (game_state.terrarium_count >= MAX_TERRARIUMS)
    return SIZE_MAX;
  size_t idx = game_state.terrarium_count++;
  terrarium_slot_t *slot = &game_state.terrariums[idx];
  memset(slot, 0, sizeof(*slot));
  slot->reptile.alive = false;
  snprintf(slot->terrarium.name, sizeof(slot->terrarium.name),
           "Terrarium %u", (unsigned)(idx + 1));
  rebuild_environment_bindings();
  return idx;
}

bool game_select_terrarium(size_t index) {
  if (index >= game_state.terrarium_count)
    return false;
  current_slot = index;
  terrarium_slot_t *slot = &game_state.terrariums[index];
  const terrarium_t *cur = terrarium_get_state();
  memset((void *)cur, 0, sizeof(*cur));
  const reptile_info_t *info = reptiles_find(slot->reptile.species);
  if (info)
    terrarium_set_reptile(info);
  apply_terrarium_environment(slot, slot->terrarium.temperature,
                              slot->terrarium.humidity,
                              slot->terrarium.uv_index);
  terrarium_set_decor(slot->terrarium.decor);
  terrarium_set_substrate(slot->terrarium.substrate);
  terrarium_set_heater(slot->terrarium.heater_on);
  terrarium_set_light(slot->terrarium.light_on);
  terrarium_set_mist(slot->terrarium.mist_on);
  for (size_t j = 0; j < slot->terrarium.item_count; ++j)
    terrarium_add_item(slot->terrarium.items[j]);
  return true;
}

void game_remove_terrarium(size_t index) {
  if (index >= game_state.terrarium_count)
    return;
  for (size_t i = index + 1; i < game_state.terrarium_count; ++i)
    game_state.terrariums[i - 1] = game_state.terrariums[i];
  if (game_state.terrarium_count)
    game_state.terrarium_count--;
  if (current_slot >= game_state.terrarium_count)
    current_slot = game_state.terrarium_count ? game_state.terrarium_count - 1 : 0;
  rebuild_environment_bindings();
}

static void apply_terrarium_environment(terrarium_slot_t *slot,
                                        float temperature, float humidity,
                                        float uv_index)
{
  if (!slot)
    return;
  slot->terrarium.temperature = temperature;
  slot->terrarium.humidity = humidity;
  slot->terrarium.uv_index = uv_index;
  size_t slot_index = (size_t)(slot - game_state.terrariums);
  if (slot_index == current_slot && slot_index < game_state.terrarium_count) {
    terrarium_update_environment(temperature, humidity, uv_index);
  }
}

static void environment_slot_cb(void *ctx, float temperature, float humidity,
                                float uv_index)
{
  terrarium_slot_t *slot = (terrarium_slot_t *)ctx;
  apply_terrarium_environment(slot, temperature, humidity, uv_index);
}

static void rebuild_environment_bindings(void)
{
  environment_reset();
  for (size_t i = 0; i < game_state.terrarium_count; ++i) {
    terrarium_slot_t *slot = &game_state.terrariums[i];
    environment_register_terrarium(&slot->terrarium.profile,
                                   environment_slot_cb,
                                   slot,
                                   slot->terrarium.phase_offset);
  }
}

static void commit_slot_from_model(terrarium_slot_t *slot)
{
  if (!slot)
    return;
  const terrarium_t *t = terrarium_get_state();
  size_t count = t->item_count;
  if (count > MAX_TERRARIUM_ITEMS)
    count = MAX_TERRARIUM_ITEMS;
  slot->terrarium.item_count = count;
  for (size_t j = 0; j < count; ++j) {
    strncpy(slot->terrarium.items[j], t->items[j], ITEM_NAME_LEN - 1);
    slot->terrarium.items[j][ITEM_NAME_LEN - 1] = '\0';
  }
  for (size_t j = count; j < MAX_TERRARIUM_ITEMS; ++j) {
    slot->terrarium.items[j][0] = '\0';
  }
  strncpy(slot->terrarium.decor, t->decor, ITEM_NAME_LEN - 1);
  slot->terrarium.decor[ITEM_NAME_LEN - 1] = '\0';
  strncpy(slot->terrarium.substrate, t->substrate, ITEM_NAME_LEN - 1);
  slot->terrarium.substrate[ITEM_NAME_LEN - 1] = '\0';
  slot->terrarium.heater_on = t->heater_on;
  slot->terrarium.light_on = t->light_on;
  slot->terrarium.mist_on = t->mist_on;
  apply_terrarium_environment(slot, t->temperature, t->humidity, t->uv_index);
}

static float compute_daily_revenue(void)
{
  float revenue = 0.0f;
  for (size_t i = 0; i < game_state.terrarium_count; ++i) {
    const terrarium_slot_t *slot = &game_state.terrariums[i];
    const reptile_state_t *r = &slot->reptile;
    if (!r->alive || r->max_health <= 0.0f)
      continue;
    float health_ratio = r->health / r->max_health;
    if (health_ratio < 0.0f)
      health_ratio = 0.0f;
    if (health_ratio > 1.0f)
      health_ratio = 1.0f;
    float base = r->mature ? 25.0f : 12.0f;
    float exhibit_bonus = (float)slot->terrarium.item_count * 0.5f;
    revenue += (base + exhibit_bonus) * health_ratio;
  }
  return revenue;
}

static void update_economy_from_health(void)
{
  float total_ratio = 0.0f;
  size_t alive = 0;
  for (size_t i = 0; i < game_state.terrarium_count; ++i) {
    const reptile_state_t *r = &game_state.terrariums[i].reptile;
    if (!r->alive || r->max_health <= 0.0f)
      continue;
    float ratio = r->health / r->max_health;
    if (ratio < 0.0f)
      ratio = 0.0f;
    if (ratio > 1.0f)
      ratio = 1.0f;
    total_ratio += ratio;
    alive++;
  }
  if (alive == 0)
    return;
  float target = (total_ratio / (float)alive) * 100.0f;
  float diff = target - game_state.economy.wellbeing;
  game_state.economy.wellbeing += diff * 0.1f;
  if (game_state.economy.wellbeing > 100.0f)
    game_state.economy.wellbeing = 100.0f;
  if (game_state.economy.wellbeing < 0.0f)
    game_state.economy.wellbeing = 0.0f;
}

static void advance_simulated_day(void)
{
  float revenue = compute_daily_revenue();
  game_state.economy.budget += revenue;
  economy_next_day(&game_state.economy);
  update_economy_from_health();
  ESP_LOGI(TAG, "Day %u revenue=%.2f budget=%.2f wellbeing=%.1f",
           game_state.economy.day, revenue, game_state.economy.budget,
           game_state.economy.wellbeing);
}

void game_set_reptile(const reptile_info_t *info) {
  terrarium_set_reptile(info);
  if (!info)
    return;
  terrarium_slot_t *slot = &game_state.terrariums[current_slot];
  strncpy(slot->reptile.species, info->species,
          sizeof(slot->reptile.species) - 1);
  slot->reptile.species[sizeof(slot->reptile.species) - 1] = '\0';
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
  slot->reptile.max_health = info->needs.max_health;
  slot->reptile.mature = false;
  slot->reptile.sick = false;
  slot->reptile.alive = true;
  slot->terrarium.temperature = info->needs.temperature;
  slot->terrarium.humidity = info->needs.humidity;
  slot->terrarium.uv_index = info->needs.uv_index;
  slot->terrarium.profile.day_temp = info->needs.temperature;
  slot->terrarium.profile.night_temp = info->needs.temperature - 5.0f;
  slot->terrarium.profile.day_humidity = info->needs.humidity;
  slot->terrarium.profile.night_humidity = info->needs.humidity + 20.0f;
  slot->terrarium.profile.day_uv = info->needs.uv_index;
  if (slot->terrarium.name[0] == '\0') {
    strncpy(slot->terrarium.name, info->species,
            sizeof(slot->terrarium.name) - 1);
    slot->terrarium.name[sizeof(slot->terrarium.name) - 1] = '\0';
  }
  apply_terrarium_environment(slot, slot->terrarium.temperature,
                              slot->terrarium.humidity,
                              slot->terrarium.uv_index);
  environment_update_terrarium(slot, &slot->terrarium.profile,
                               slot->terrarium.phase_offset);
}

/* Periodic reptile simulation -------------------------------------------- */

#define REPTILE_UPDATE_PERIOD_US (1000 * 1000)
static esp_timer_handle_t reptile_timer;

static void reptile_tick(void *arg) {
  (void)arg;
  float delta_hours = environment_get_time_scale() *
                      (REPTILE_UPDATE_PERIOD_US / 1000000.0f);
  simulated_hours_accum += delta_hours;
  while (simulated_hours_accum >= 24.0f) {
    simulated_hours_accum -= 24.0f;
    advance_simulated_day();
  }

  float daily_factor = delta_hours > 0.0f ? delta_hours / 24.0f : 0.0f;

  for (size_t i = 0; i < game_state.terrarium_count; ++i) {
    terrarium_slot_t *slot = &game_state.terrariums[i];
    reptile_state_t *r = &slot->reptile;
    if (!r->alive)
      continue;

    const reptile_info_t *info = reptiles_find(r->species);
    if (!info)
      continue;

    float growth_step = info->needs.growth_rate * daily_factor;
    r->growth += growth_step;
    if (r->growth > REPTILE_GROWTH_MATURE)
      r->growth = REPTILE_GROWTH_MATURE;
    if (!r->mature && r->growth >= REPTILE_GROWTH_MATURE) {
      r->mature = true;
      ESP_LOGI(TAG, "%s reached maturity", r->species);
    }

    const terrarium_state_t *env = &slot->terrarium;
    float temp_diff = fabsf(env->temperature - r->temperature);
    float hum_diff = fabsf(env->humidity - r->humidity);
    float uv_diff = fabsf(env->uv_index - r->uv_index);

    float health_delta = 0.0f;
    float env_penalty = (temp_diff * 0.2f) + (hum_diff * 0.05f) + (uv_diff * 0.3f);
    health_delta -= env_penalty * info->needs.max_health * daily_factor;

    if (!env->heater_on)
      health_delta -= 3.0f * info->needs.max_health * daily_factor * 0.01f;
    if (!env->light_on)
      health_delta -= 2.0f * info->needs.max_health * daily_factor * 0.01f;
    if (!env->mist_on && r->humidity > env->humidity)
      health_delta -= 1.5f * info->needs.max_health * daily_factor * 0.01f;

    if (temp_diff < 1.0f && hum_diff < 5.0f && uv_diff < 0.5f &&
        env->heater_on && env->light_on) {
      health_delta += 1.5f * info->needs.max_health * daily_factor * 0.01f;
    }

    float wellbeing = game_state.economy.wellbeing;
    if (wellbeing > 75.0f) {
      health_delta += ((wellbeing - 75.0f) * 0.02f) * info->needs.max_health * daily_factor * 0.01f;
    } else if (wellbeing < 50.0f) {
      health_delta -= ((50.0f - wellbeing) * 0.03f) * info->needs.max_health * daily_factor * 0.01f;
    }

    r->health += health_delta;
    if (r->health > r->max_health)
      r->health = r->max_health;

    float sick_level = r->max_health * REPTILE_HEALTH_SICK_RATIO;
    if (!r->sick && r->health <= sick_level && r->health > REPTILE_HEALTH_DEAD) {
      r->sick = true;
      ESP_LOGW(TAG, "%s requires care", r->species);
    }
    if (r->sick && r->health > sick_level) {
      r->sick = false;
    }

    if (r->health <= REPTILE_HEALTH_DEAD) {
      r->health = REPTILE_HEALTH_DEAD;
      r->alive = false;
      ESP_LOGE(TAG, "%s died", r->species);
    }
  }
  update_economy_from_health();
}

static void start_reptile_timer(void) {
  if (reptile_timer)
    return;
  const esp_timer_create_args_t args = {
      .callback = &reptile_tick,
      .name = "reptile"};
  ESP_ERROR_CHECK(esp_timer_create(&args, &reptile_timer));
  ESP_ERROR_CHECK(
      esp_timer_start_periodic(reptile_timer, REPTILE_UPDATE_PERIOD_US));
}

/* Default player regulatory context: adjust at runtime if needed */
static reptile_user_ctx_t user_ctx = {
    .cites_permit = REPTILE_CITES_NONE,
    .has_authorisation = false,
    .has_cdc = false,
    .has_certificat = false,
    .region = REPTILE_REGION_FR,
};

#define SAVE_PATH "/sdcard/simulrepile.sav"

bool game_save(void) {
  terrarium_slot_t *slot = &game_state.terrariums[current_slot];
  commit_slot_from_model(slot);

  for (size_t i = 0; i < game_state.terrarium_count; ++i) {
    terrarium_slot_t *s = &game_state.terrariums[i];
    const reptile_info_t *info = reptiles_find(s->reptile.species);
    if (!info || !reptiles_validate(info, &user_ctx)) {
      ESP_LOGW(TAG, "Cannot save: non-compliant reptile %s", s->reptile.species);
      return false;
    }
    if (s->reptile.max_health <= 0.0f && info) {
      s->reptile.max_health = info->needs.max_health;
    }
    if (s->reptile.health < REPTILE_HEALTH_DEAD) {
      s->reptile.health = REPTILE_HEALTH_DEAD;
      s->reptile.alive = false;
    }
    if (s->reptile.growth >= REPTILE_GROWTH_MATURE) {
      s->reptile.mature = true;
    }
    if (s->terrarium.phase_offset < 0.0f)
      s->terrarium.phase_offset = 0.0f;
    if (s->terrarium.phase_offset > 24.0f)
      s->terrarium.phase_offset = fmodf(s->terrarium.phase_offset, 24.0f);
    environment_update_terrarium(s, &s->terrarium.profile,
                                 s->terrarium.phase_offset);
  }
  game_state.env_time_scale = environment_get_time_scale();
  return storage_save(SAVE_PATH, &game_state, sizeof(game_state));
}

bool game_load(void) {
  if (!storage_load(SAVE_PATH, &game_state, sizeof(game_state))) {
    return false;
  }
  if (game_state.terrarium_count == 0) {
    return false;
  }

  simulated_hours_accum = 0.0f;
  environment_set_time_scale(game_state.env_time_scale);

  for (size_t i = 0; i < game_state.terrarium_count; ++i) {
    terrarium_slot_t *slot = &game_state.terrariums[i];
    if (slot->reptile.health <= REPTILE_HEALTH_DEAD) {
      slot->reptile.health = REPTILE_HEALTH_DEAD;
      slot->reptile.alive = false;
    }
    if (slot->reptile.growth >= REPTILE_GROWTH_MATURE) {
      slot->reptile.mature = true;
    }
    const reptile_info_t *info = reptiles_find(slot->reptile.species);
    if (!reptiles_validate(info, &user_ctx)) {
      return false;
    }
    if (slot->reptile.max_health <= 0.0f && info) {
      slot->reptile.max_health = info->needs.max_health;
    }
    if (slot->terrarium.phase_offset < 0.0f)
      slot->terrarium.phase_offset = 0.0f;
    if (slot->terrarium.phase_offset > 24.0f)
      slot->terrarium.phase_offset = fmodf(slot->terrarium.phase_offset, 24.0f);
  }

  rebuild_environment_bindings();

  current_slot = 0;
  return game_select_terrarium(0);
}

bool game_get_terrarium_snapshot(size_t index, game_terrarium_snapshot_t *out)
{
  if (!out || index >= game_state.terrarium_count)
    return false;
  const terrarium_slot_t *slot = &game_state.terrariums[index];
  memset(out, 0, sizeof(*out));
  strncpy(out->name, slot->terrarium.name, sizeof(out->name) - 1);
  out->name[sizeof(out->name) - 1] = '\0';
  out->has_reptile = slot->reptile.species[0] != '\0';
  strncpy(out->species, slot->reptile.species, sizeof(out->species) - 1);
  out->species[sizeof(out->species) - 1] = '\0';
  out->target_temperature = slot->reptile.temperature;
  out->target_humidity = slot->reptile.humidity;
  out->target_uv = slot->reptile.uv_index;
  out->growth = slot->reptile.growth;
  out->health = slot->reptile.health;
  out->max_health = slot->reptile.max_health > 0.0f ? slot->reptile.max_health : 1.0f;
  out->mature = slot->reptile.mature;
  out->sick = slot->reptile.sick;
  out->alive = slot->reptile.alive;
  out->phase_offset = slot->terrarium.phase_offset;
  out->terrarium = slot->terrarium;
  return true;
}

void game_commit_current_terrarium(void)
{
  if (current_slot >= game_state.terrarium_count)
    return;
  terrarium_slot_t *slot = &game_state.terrariums[current_slot];
  commit_slot_from_model(slot);
}

void game_set_terrarium_name(const char *name)
{
  if (!name || current_slot >= game_state.terrarium_count)
    return;
  terrarium_slot_t *slot = &game_state.terrariums[current_slot];
  strncpy(slot->terrarium.name, name, sizeof(slot->terrarium.name) - 1);
  slot->terrarium.name[sizeof(slot->terrarium.name) - 1] = '\0';
  size_t len = strcspn(slot->terrarium.name, "\r\n");
  slot->terrarium.name[len] = '\0';
}

void game_set_terrarium_phase_offset(float hours)
{
  if (current_slot >= game_state.terrarium_count)
    return;
  if (hours < 0.0f)
    hours = 0.0f;
  while (hours >= 24.0f)
    hours -= 24.0f;
  terrarium_slot_t *slot = &game_state.terrariums[current_slot];
  slot->terrarium.phase_offset = hours;
  environment_update_terrarium(slot, &slot->terrarium.profile,
                               slot->terrarium.phase_offset);
}

const economy_t *game_get_economy(void)
{
  return &game_state.economy;
}

static void btn_new_game_event(lv_event_t *e) {
  ESP_LOGI(TAG, "Start new game");

  memset(&game_state, 0, sizeof(game_state));
  game_state.terrarium_count = 1;
  terrarium_slot_t *slot = &game_state.terrariums[0];
  current_slot = 0;
  simulated_hours_accum = 0.0f;

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
  slot->reptile.max_health = info->needs.max_health;
  slot->reptile.mature = false;
  slot->reptile.sick = false;
  slot->reptile.alive = true;

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
  snprintf(slot->terrarium.name, sizeof(slot->terrarium.name), "Terrarium %u", 1u);
  rebuild_environment_bindings();
  apply_terrarium_environment(slot, slot->terrarium.temperature,
                              slot->terrarium.humidity,
                              slot->terrarium.uv_index);

  /* Initialise economic state */
  economy_init(&game_state.economy, 100.0f, 100.0f);

  if (!game_save()) {
    ESP_LOGE(TAG, "Failed to save game");
  }

  start_reptile_timer();

  /* After creating a new game, open terrarium customisation UI */
  terrarium_ui_show();
}

static void btn_resume_event(lv_event_t *e) {
  ESP_LOGI(TAG, "Resume game");
  if (!game_load()) {
    ESP_LOGE(TAG, "No saved game");
    return;
  }

  terrarium_slot_t *slot = &game_state.terrariums[0];
  ESP_LOGI(TAG, "Loaded %s T=%.1f H=%.1f UV=%.1f budget=%.2f day=%u",
           slot->reptile.species, slot->terrarium.temperature,
           slot->terrarium.humidity, slot->terrarium.uv_index,
           game_state.economy.budget, game_state.economy.day);

  start_reptile_timer();

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
