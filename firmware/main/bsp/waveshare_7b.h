#pragma once

#include <driver/gpio.h>
#include <driver/i2c.h>
#include <driver/sdmmc_host.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_types.h>
#include <esp_lvgl_port.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_REV_A_ST7262 = 0,
    BOARD_REV_B_AXS15231B,
} board_revision_t;

const char *bsp_board_revision_str(void);
board_revision_t bsp_board_revision(void);

void bsp_display_init(void);
void bsp_touch_init(void);
void bsp_sdcard_init(void);

const esp_lcd_rgb_panel_config_t *bsp_display_rgb_config(void);
const esp_lvgl_port_cfg_t *bsp_lvgl_port_config(void);

#ifdef __cplusplus
}
#endif
