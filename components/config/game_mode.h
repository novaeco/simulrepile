#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GAME_MODE_SIMULATION = 0
} game_mode_t;

void game_mode_set(game_mode_t mode);
game_mode_t game_mode_get(void);

#ifdef __cplusplus
}
#endif

