#ifndef REPTILE_GAME_H
#define REPTILE_GAME_H

#include "lvgl.h"
#include "touch.h"
#include <esp_lcd_panel_ops.h>
#include <stdint.h>
#include "reptile_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

void reptile_game_init(void);
void reptile_tick(lv_timer_t *timer);
const reptile_t *reptile_get_state(void);
void reptile_game_stop(void);
bool reptile_game_is_active(void);

void reptile_game_start(esp_lcd_panel_handle_t panel,
                        esp_lcd_touch_handle_t touch);

#ifdef __cplusplus
}
#endif

#endif /* REPTILE_GAME_H */
