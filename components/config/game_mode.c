#include "game_mode.h"

static game_mode_t s_game_mode = GAME_MODE_REAL;

void game_mode_set(game_mode_t mode)
{
    s_game_mode = mode;
}

game_mode_t game_mode_get(void)
{
    return s_game_mode;
}
