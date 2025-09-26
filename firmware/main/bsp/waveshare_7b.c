#include "waveshare_7b.h"

#include "app_config.h"
#include "esp_log.h"
#include "lvgl.h"

#ifdef CONFIG_APP_BOARD_REV_A
#include "pins_rev_a.h"
#else
#include "pins_rev_b.h"
#endif

static const char *TAG = "bsp";

static esp_lcd_rgb_panel_config_t s_rgb_config = {
    .data_width = 16,
    .psram_trans_align = 64,
    .hsync_pulse_width = 10,
    .hsync_back_porch = 20,
    .hsync_front_porch = 20,
    .vsync_pulse_width = 10,
    .vsync_back_porch = 10,
    .vsync_front_porch = 10,
    .pclk_hz = 16000000,
    .de_gpio_num = LCD_DE_PIN,
    .pclk_gpio_num = LCD_PCLK_PIN,
    .vsync_gpio_num = LCD_VSYNC_PIN,
    .hsync_gpio_num = LCD_HSYNC_PIN,
    .disp_gpio_num = LCD_BACKLIGHT_PIN,
    .data_gpio_nums = {
        LCD_DATA0_PIN, LCD_DATA1_PIN, LCD_DATA2_PIN, LCD_DATA3_PIN,
        LCD_DATA4_PIN, LCD_DATA5_PIN, LCD_DATA6_PIN, LCD_DATA7_PIN,
        LCD_DATA8_PIN, LCD_DATA9_PIN, LCD_DATA10_PIN, LCD_DATA11_PIN,
        LCD_DATA12_PIN, LCD_DATA13_PIN, LCD_DATA14_PIN, LCD_DATA15_PIN,
    },
    .flags = {
        .fb_in_psram = 1,
        .disp_active_low = 0,
        .relax_on_idle = 1,
    },
    .timings = {
        .h_res = 1024,
        .v_res = 600,
    },
};

static esp_lvgl_port_cfg_t s_lvgl_cfg = {
    .task_priority = 5,
    .task_stack = 8192,
    .task_affinity = 0,
    .task_max_sleep_ms = 10,
    .timer_period_ms = 5,
};

const char *bsp_board_revision_str(void)
{
#if CONFIG_APP_BOARD_REV_A
    return "BOARD_REV_A_ST7262";
#elif CONFIG_APP_BOARD_REV_B
    return "BOARD_REV_B_AXS15231B";
#else
    return "BOARD_REV_UNKNOWN";
#endif
}

board_revision_t bsp_board_revision(void)
{
#if CONFIG_APP_BOARD_REV_A
    return BOARD_REV_A_ST7262;
#else
    return BOARD_REV_B_AXS15231B;
#endif
}

void bsp_display_init(void)
{
    ESP_LOGI(TAG, "Initialising RGB display");
}

void bsp_touch_init(void)
{
    ESP_LOGI(TAG, "Initialising GT911 touch (stub)");
}

void bsp_sdcard_init(void)
{
    ESP_LOGI(TAG, "Initialising SD card (stub)");
}

const esp_lcd_rgb_panel_config_t *bsp_display_rgb_config(void)
{
    return &s_rgb_config;
}

const esp_lvgl_port_cfg_t *bsp_lvgl_port_config(void)
{
    return &s_lvgl_cfg;
}
