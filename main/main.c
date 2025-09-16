/*****************************************************************************
 * | File       :   main.c
 * | Author     :   Waveshare team
 * | Function   :   Main function
 * | Info       :
 * |                Ported LVGL 8.3.9 and display the official demo interface
 *----------------
 * | Version    :   V1.0
 * | Date       :   2024-12-06
 * | Info       :   Basic version
 *
 ******************************************************************************/

#include "gt911.h"        // Header for touch screen operations (GT911)
#include "rgb_lcd_port.h" // Header for Waveshare RGB LCD driver

#include "can.h"
#include "driver/gpio.h" // GPIO definitions for wake-up source
#include "driver/ledc.h" // LEDC for backlight PWM
#include "esp_lcd_panel_ops.h"
#include "esp_sleep.h"    // Light-sleep configuration
#include "esp_system.h"   // Reset reason API
#include "esp_task_wdt.h" // Watchdog timer API
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio.h" // Custom GPIO wrappers for reptile control
#include "sensors.h"      // Sensor initialization
#include "logging.h"
#include "lv_demos.h" // LVGL demo headers
#include "lvgl.h"
#include "lvgl_port.h"    // LVGL porting functions for integration
#include "nvs.h"          // NVS key-value API
#include "nvs_flash.h"    // NVS flash for persistent storage
#include "reptile_game.h" // Reptile game interface
#include "reptile_real.h" // Real-world mode interface
#include "sd.h"
#include "sleep.h" // Sleep control interface
#include "settings.h"     // Application settings
#include "game_mode.h"

static const char *TAG = "main"; // Tag for logging

static lv_timer_t *sleep_timer; // Inactivity timer handle
static bool sleep_enabled;      // Runtime sleep state

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_touch_handle_t tp_handle = NULL;
static lv_obj_t *error_screen;
static lv_obj_t *prev_screen;
lv_obj_t *menu_screen;

enum {
  APP_MODE_MENU = 0,
  APP_MODE_GAME = 1,
  APP_MODE_REAL = 2,
  APP_MODE_SETTINGS = 3,
  APP_MODE_MENU_OVERRIDE = 0xFF,
};

// GPIO used at boot to ignore the persisted last mode when held low
#define MODE_OVERRIDE_BTN GPIO_NUM_0

static void save_last_mode(uint8_t mode) {
  nvs_handle_t nvs;
  if (nvs_open("cfg", NVS_READWRITE, &nvs) == ESP_OK) {
    nvs_set_u8(nvs, "last_mode", mode);
    nvs_commit(nvs);
    nvs_close(nvs);
  }
}

// Public helper to force the menu on next boot via NVS flag
void reset_last_mode(void) { save_last_mode(APP_MODE_MENU_OVERRIDE); }

static void sleep_timer_cb(lv_timer_t *timer);

static void start_game_mode(void) {
  reptile_game_stop();
  reptile_game_init();
  reptile_game_start(panel_handle, tp_handle);
  logging_init(reptile_get_state);
  if (!sleep_timer)
    sleep_timer = lv_timer_create(sleep_timer_cb, 120000, NULL);
  settings_apply();
}

static void menu_btn_game_cb(lv_event_t *e) {
  (void)e;
  game_mode_set(GAME_MODE_SIMULATION);
  save_last_mode(APP_MODE_GAME);
  start_game_mode();
}

static void menu_btn_real_cb(lv_event_t *e) {
  (void)e;
  game_mode_set(GAME_MODE_REAL);
  reptile_game_stop();
  if (game_mode_get() == GAME_MODE_REAL) {
    esp_err_t err = sensors_init();
    if (err == ESP_ERR_NOT_FOUND) {
      lv_obj_t *mbox = lv_msgbox_create(NULL);
      lv_msgbox_add_title(mbox, "Erreur");
      lv_msgbox_add_text(mbox, "Capteur non connecté");
      lv_msgbox_add_close_button(mbox);
      lv_obj_center(mbox);
      return;
    }
    err = reptile_actuators_init();
    if (err == ESP_ERR_NOT_FOUND) {
      sensors_deinit();
      lv_obj_t *mbox = lv_msgbox_create(NULL);
      lv_msgbox_add_title(mbox, "Erreur");
      lv_msgbox_add_text(mbox, "Capteur non connecté");
      lv_msgbox_add_close_button(mbox);
      lv_obj_center(mbox);
      return;
    }
    save_last_mode(APP_MODE_REAL);
    reptile_real_start(panel_handle, tp_handle);
  }
}

static void menu_btn_settings_cb(lv_event_t *e) {
  (void)e;
  reptile_game_stop();
  save_last_mode(APP_MODE_SETTINGS);
  settings_screen_show();
}

void sleep_set_enabled(bool enabled) {
  sleep_enabled = enabled;
  if (!sleep_timer)
    return;
  if (enabled) {
    lv_timer_set_period(sleep_timer, 120000);
    lv_timer_resume(sleep_timer);
    lv_timer_reset(sleep_timer);
  } else {
    lv_timer_pause(sleep_timer);
  }
}

bool sleep_is_enabled(void) { return sleep_enabled; }

static void show_error_screen(const char *msg) {
  if (!lvgl_port_lock(-1))
    return;
  if (!error_screen) {
    prev_screen = lv_scr_act();
    error_screen = lv_obj_create(NULL);
    lv_obj_t *label = lv_label_create(error_screen);
    lv_label_set_text(label, msg);
    lv_obj_center(label);
  }
  lv_disp_load_scr(error_screen);
  lvgl_port_unlock();
}

static void hide_error_screen(void) {
  if (!lvgl_port_lock(-1))
    return;
  if (error_screen) {
    lv_disp_load_scr(prev_screen);
    lv_obj_del(error_screen);
    error_screen = NULL;
  }
  lvgl_port_unlock();
}

static void wait_for_sd_card(void) {
  esp_err_t err;
  int attempts = 0;
  const int max_attempts = 10;

  ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

  while (true) {
    esp_task_wdt_reset();
    err = sd_mmc_init();
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
      hide_error_screen();
      return;
    }

    ESP_LOGE(TAG, "Carte SD absente ou illisible (%s)", esp_err_to_name(err));
    show_error_screen("Insérer une carte SD valide");
    vTaskDelay(pdMS_TO_TICKS(500));
    if (++attempts >= max_attempts) {
      show_error_screen("Carte SD absente - redémarrage");
      vTaskDelay(pdMS_TO_TICKS(2000));
      esp_restart();
    }
    // Attendre indéfiniment jusqu'à insertion d'une carte valide
  }
}

#define BL_PIN GPIO_NUM_16
#define BL_LEDC_TIMER LEDC_TIMER_0
#define BL_LEDC_CHANNEL LEDC_CHANNEL_0
#define BL_LEDC_MODE LEDC_LOW_SPEED_MODE
#define BL_LEDC_FREQ_HZ 5000
#define BL_LEDC_DUTY_RES LEDC_TIMER_13_BIT
#define BL_DUTY_MAX ((1 << BL_LEDC_DUTY_RES) - 1)

static uint32_t bl_duty = BL_DUTY_MAX;

static void backlight_init(void) {
  ledc_timer_config_t timer_cfg = {
      .speed_mode = BL_LEDC_MODE,
      .duty_resolution = BL_LEDC_DUTY_RES,
      .timer_num = BL_LEDC_TIMER,
      .freq_hz = BL_LEDC_FREQ_HZ,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

  ledc_channel_config_t ch_cfg = {
      .gpio_num = BL_PIN,
      .speed_mode = BL_LEDC_MODE,
      .channel = BL_LEDC_CHANNEL,
      .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = BL_LEDC_TIMER,
      .duty = 0,
      .hpoint = 0,
  };
  ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

  ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, bl_duty);
  ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
}

static void sleep_timer_cb(lv_timer_t *timer) {
  (void)timer;
  lv_timer_t *t = lv_timer_get_next(NULL);
  while (t) {
    lv_timer_pause(t);
    t = lv_timer_get_next(t);
  }

  esp_lcd_panel_disp_on_off(panel_handle, false);
  ledc_stop(BL_LEDC_MODE, BL_LEDC_CHANNEL, 0);
  gpio_set_level(BL_PIN, 0);

  esp_sleep_wakeup_cause_t cause = ESP_SLEEP_WAKEUP_UNDEFINED;
  logging_pause();
  esp_err_t err = sd_mmc_unmount();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "D\u00e9montage SD: %s", esp_err_to_name(err));
    goto cleanup;
  }
  // Use ANY_LOW to ensure compatibility with ESP32-S3 and avoid deprecated
  // ALL_LOW
  esp_sleep_enable_ext1_wakeup((1ULL << GPIO_NUM_4), ESP_EXT1_WAKEUP_ANY_LOW);
  gpio_pulldown_en(
      GPIO_NUM_4); // ensure defined level; use external pull-up if needed
  esp_light_sleep_start();
  cause = esp_sleep_get_wakeup_cause();
  ESP_LOGI(TAG, "Wakeup cause: %d", cause);

cleanup:
  esp_lcd_panel_disp_on_off(panel_handle, true);
  ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, bl_duty);
  ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);

  if (cause == ESP_SLEEP_WAKEUP_EXT1) {
    wait_for_sd_card();
  }

  logging_resume();

  reptile_game_init();
  reptile_tick(NULL);

  t = lv_timer_get_next(NULL);
  while (t) {
    lv_timer_resume(t);
    t = lv_timer_get_next(t);
  }
  lv_timer_reset(timer);
}

// Main application function
void app_main() {
  esp_reset_reason_t rr = esp_reset_reason();
  ESP_LOGI(TAG, "Reset reason: %d", rr);

  // Initialize NVS flash storage with error handling for page issues
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Load persisted application settings
  settings_init();

  // Initialize SD card at boot for early log availability
  esp_err_t sd_ret = sd_mmc_init();
  if (sd_ret != ESP_OK) {
    ESP_LOGW(TAG, "Initial SD init failed: %s", esp_err_to_name(sd_ret));
  }

  // Initialize the GT911 touch screen controller
  esp_err_t tp_ret = touch_gt911_init(&tp_handle);
  if (tp_ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize GT911 touch controller: %s",
             esp_err_to_name(tp_ret));
    return;
  }

  // Initialize the Waveshare ESP32-S3 RGB LCD hardware
  panel_handle = waveshare_esp32_s3_rgb_lcd_init();

  backlight_init();

  /* Configure reptile control outputs */
  DEV_GPIO_Mode(SERVO_FEED_PIN, GPIO_MODE_OUTPUT);
  DEV_Digital_Write(SERVO_FEED_PIN, 0);
  DEV_GPIO_Mode(WATER_PUMP_PIN, GPIO_MODE_OUTPUT);
  DEV_Digital_Write(WATER_PUMP_PIN, 0);
  DEV_GPIO_Mode(HEAT_RES_PIN, GPIO_MODE_OUTPUT);
  DEV_Digital_Write(HEAT_RES_PIN, 0);

  // Initialize CAN bus (125 kbps)
  const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_125KBITS();
  const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  const twai_general_config_t g_config =
      TWAI_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, TWAI_MODE_NORMAL);
  if (can_init(t_config, f_config, g_config) != ESP_OK) {
    ESP_LOGW(TAG, "CAN indisponible – fonctionnalité désactivée");
  }

  // Initialize LVGL with the panel and touch handles
  ESP_ERROR_CHECK(lvgl_port_init(panel_handle, tp_handle));

  // Initialize SD card (retry until available)
  wait_for_sd_card();

  ESP_LOGI(TAG, "Display LVGL demos");

  // Lock the mutex because LVGL APIs are not thread-safe
  if (lvgl_port_lock(-1)) {
    // Create main menu screen
    menu_screen = lv_obj_create(NULL);

    lv_obj_t *btn_game = lv_btn_create(menu_screen);
    lv_obj_set_size(btn_game, 200, 50);
    lv_obj_align(btn_game, LV_ALIGN_CENTER, 0, -60);
    lv_obj_add_event_cb(btn_game, menu_btn_game_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(btn_game);
    lv_label_set_text(label, "Mode Jeu");
    lv_obj_center(label);

    lv_obj_t *btn_real = lv_btn_create(menu_screen);
    lv_obj_set_size(btn_real, 200, 50);
    lv_obj_align(btn_real, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn_real, menu_btn_real_cb, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(btn_real);
    lv_label_set_text(label, "Mode R\u00e9el");
    lv_obj_center(label);

    lv_obj_t *btn_settings = lv_btn_create(menu_screen);
    lv_obj_set_size(btn_settings, 200, 50);
    lv_obj_align(btn_settings, LV_ALIGN_CENTER, 0, 60);
    lv_obj_add_event_cb(btn_settings, menu_btn_settings_cb, LV_EVENT_CLICKED,
                        NULL);
    label = lv_label_create(btn_settings);
    lv_label_set_text(label, "Param\u00e8tres");
    lv_obj_center(label);

    uint8_t last_mode = APP_MODE_MENU;
    nvs_handle_t nvs;
    if (nvs_open("cfg", NVS_READWRITE, &nvs) == ESP_OK) {
      nvs_get_u8(nvs, "last_mode", &last_mode);
      nvs_close(nvs);
    }

    bool force_menu = (last_mode == APP_MODE_MENU_OVERRIDE);

    // If override button is held during boot, ignore stored mode
    gpio_reset_pin(MODE_OVERRIDE_BTN);
    gpio_set_direction(MODE_OVERRIDE_BTN, GPIO_MODE_INPUT);
    gpio_pullup_en(MODE_OVERRIDE_BTN);
    if (gpio_get_level(MODE_OVERRIDE_BTN) == 0) {
      force_menu = true;
    }

    if (force_menu) {
      last_mode = APP_MODE_MENU;
    }

    switch (last_mode) {
    case APP_MODE_GAME:
      start_game_mode();
      break;
    case APP_MODE_REAL:
      game_mode_set(GAME_MODE_REAL);
      if (game_mode_get() == GAME_MODE_REAL) {
        reptile_real_start(panel_handle, tp_handle);
      }
      break;
    case APP_MODE_SETTINGS:
      settings_screen_show();
      break;
    default:
      lv_scr_load(menu_screen);
      break;
    }

    lvgl_port_unlock();
  }

  esp_task_wdt_delete(NULL);
}
