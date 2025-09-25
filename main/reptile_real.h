#ifndef REPTILE_REAL_H
#define REPTILE_REAL_H

#include "esp_lcd_panel_ops.h"
#include "touch.h"

/**
 * @brief Launch the real-mode screen displaying sensors and actuator states
 *        with manual control buttons.
 */
void reptile_real_start(esp_lcd_panel_handle_t panel, esp_lcd_touch_handle_t tp);

#endif // REPTILE_REAL_H
