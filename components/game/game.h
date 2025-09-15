#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "economy.h"
#include "reptiles.h"
#include "terrarium/terrarium.h"

#define GAME_MAX_TERRARIUMS 25

typedef struct {
    char name[TERRARIUM_ITEM_NAME_LEN];
    bool has_reptile;
    char species[32];
    float target_temperature;
    float target_humidity;
    float target_uv;
    float growth;
    float health;
    float max_health;
    bool mature;
    bool sick;
    bool alive;
    float phase_offset;
    terrarium_t terrarium;
} game_terrarium_snapshot_t;

#ifdef __cplusplus
extern "C" {
#endif

void game_init(void);
void game_show_main_menu(void);

size_t game_add_terrarium(void);
bool game_select_terrarium(size_t index);
void game_remove_terrarium(size_t index);
size_t game_get_terrarium_count(void);
size_t game_get_current_slot(void);
void game_set_reptile(const reptile_info_t *info);
bool game_save(void);
bool game_load(void);
bool game_get_terrarium_snapshot(size_t index, game_terrarium_snapshot_t *out);
void game_commit_current_terrarium(void);
void game_set_terrarium_name(const char *name);
void game_set_terrarium_phase_offset(float hours);
const economy_t *game_get_economy(void);

#ifdef __cplusplus
}
#endif
