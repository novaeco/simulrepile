#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "reptiles.h"

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

#ifdef __cplusplus
}
#endif
