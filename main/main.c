/*****************************************************************************
 * | File       :   main.c
 * | Author     :   Waveshare team
 * | Function   :   Main function
 * | Info       :
 * |                Ported LVGL 9.x demos with compatibility wrappers
 *----------------
 * | Version    :   V1.0
 * | Date       :   2024-12-06
 * | Info       :   Basic version
 *
 ******************************************************************************/

#include "gt911.h"        // Header for touch screen operations (GT911)
#include "rgb_lcd_port.h" // Header for Waveshare RGB LCD driver

#include "can.h"
#include "ch422g.h"
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
#include "lvgl_compat.h"
#include "lvgl_port.h"    // LVGL porting functions for integration
#include "nvs.h"          // NVS key-value API
#include "nvs_flash.h"    // NVS flash for persistent storage
#include "reptile_game.h" // Reptile game interface
#include "reptile_real.h" // Real-world mode interface
#include "sd.h"
#include "sleep.h" // Sleep control interface
#include "settings.h"     // Application settings
#include "game_mode.h"
#include "sdkconfig.h"
#include "ui_theme.h"
#include "image.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_timer.h"

#ifndef CONFIG_CH422G_EXIO_SD_CS
#define CONFIG_CH422G_EXIO_SD_CS 4
#endif

static const char *TAG = "main"; // Tag for logging

static lv_timer_t *sleep_timer; // Inactivity timer handle
static bool sleep_enabled;      // Runtime sleep state

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_touch_handle_t tp_handle = NULL;
static lv_obj_t *error_screen;
static lv_obj_t *prev_screen;
lv_obj_t *menu_screen;
static lv_timer_t *menu_header_timer;
static lv_obj_t *menu_header_time_label;
static lv_obj_t *menu_header_sd_label;
static lv_obj_t *menu_header_sleep_label;
static lv_obj_t *menu_quick_hint_label;

static sdmmc_card_t *s_sd_card = NULL;
static bool s_sd_cs_ready = false;
static esp_err_t s_sd_cs_last_err = ESP_OK;

enum {
  APP_MODE_MENU = 0,
  APP_MODE_GAME = 1,
  APP_MODE_REAL = 2,
  APP_MODE_SETTINGS = 3,
  APP_MODE_MENU_OVERRIDE = 0xFF,
};

// Active-low GPIO sampled at boot to optionally fast-start the last mode
#define QUICK_START_BTN GPIO_NUM_0

static void save_last_mode(uint8_t mode) {
  nvs_handle_t nvs;
  uint8_t persisted = APP_MODE_MENU_OVERRIDE;

  if (mode == APP_MODE_GAME || mode == APP_MODE_REAL ||
      mode == APP_MODE_SETTINGS) {
    persisted = mode;
  } else if (mode == APP_MODE_MENU_OVERRIDE) {
    persisted = APP_MODE_MENU_OVERRIDE;
  }

  if (nvs_open("cfg", NVS_READWRITE, &nvs) == ESP_OK) {
    nvs_set_u8(nvs, "last_mode", persisted);
    nvs_commit(nvs);
    nvs_close(nvs);
  }
}

// Public helper to force the menu on next boot via NVS flag
void reset_last_mode(void) { save_last_mode(APP_MODE_MENU_OVERRIDE); }

static void sleep_timer_cb(lv_timer_t *timer);
static void menu_header_timer_cb(lv_timer_t *timer);
static void menu_header_update(void);
static void menu_hint_append(const char *message);

static void sd_cs_selftest(void) {
  s_sd_cs_ready = false;
  s_sd_cs_last_err = sd_spi_cs_selftest();
  if (s_sd_cs_last_err == ESP_OK) {
    s_sd_cs_ready = true;
    bool uses_direct_cs = false;
#if CONFIG_STORAGE_SD_USE_GPIO_CS || CONFIG_STORAGE_SD_GPIO_FALLBACK
    uses_direct_cs = sd_uses_direct_cs();
    if (uses_direct_cs) {
      ESP_LOGI(TAG,
               "Ligne CS microSD pilotée directement par GPIO%d.",
               CONFIG_STORAGE_SD_GPIO_CS_NUM);
#if !CONFIG_STORAGE_SD_USE_GPIO_CS
      ESP_LOGW(TAG,
               "Fallback GPIO activé : CH422G indisponible au boot. Les accès SD "
               "utiliseront la liaison directe jusqu'à réparation.");
#endif
    } else {
#ifdef CONFIG_CH422G_EXIO_SD_CS
      ESP_LOGI(TAG, "Ligne CS microSD pilotée via CH422G EXIO%d.",
               CONFIG_CH422G_EXIO_SD_CS);
#else
      ESP_LOGI(TAG, "Ligne CS microSD pilotée via CH422G.");
#endif
    }
#endif
    if (!uses_direct_cs) {
      ESP_LOGI(TAG,
               "CH422G détecté sur 0x%02X (SDA=%d SCL=%d).",
               ch422g_get_address(), CONFIG_I2C_MASTER_SDA_GPIO,
               CONFIG_I2C_MASTER_SCL_GPIO);
    }
    menu_header_update();
    return;
  }

  ESP_LOGE(TAG, "Autotest ligne CS SD impossible: %s",
           esp_err_to_name(s_sd_cs_last_err));
  int sda_level = gpio_get_level(CONFIG_I2C_MASTER_SDA_GPIO);
  int scl_level = gpio_get_level(CONFIG_I2C_MASTER_SCL_GPIO);
  ESP_LOGW(TAG, "Bus levels: SDA=%d SCL=%d (0=bas, 1=haut).", sda_level,
           scl_level);
  if (s_sd_cs_last_err == ESP_ERR_NOT_FOUND) {
    ESP_LOGE(TAG,
             "CH422G absent ou injoignable. Vérifiez VCC=3V3, SDA=GPIO%d, "
             "SCL=GPIO%d et les résistances de tirage 2.2–4.7 kΩ.",
             CONFIG_I2C_MASTER_SDA_GPIO, CONFIG_I2C_MASTER_SCL_GPIO);
#if CONFIG_STORAGE_SD_USE_GPIO_CS || CONFIG_STORAGE_SD_GPIO_FALLBACK
  } else if (s_sd_cs_last_err == ESP_ERR_INVALID_STATE &&
             sd_fallback_due_to_ch422g()) {
    ESP_LOGW(TAG,
             "Fallback CS direct GPIO%d actif sans liaison détectée. Reliez EXIO%u "
             "à GPIO%d puis activez Component config → Storage / SD card → "
             "Automatically mount the fallback CS pour autoriser le montage "
             "automatique.",
             CONFIG_STORAGE_SD_GPIO_CS_NUM, CONFIG_CH422G_EXIO_SD_CS,
             CONFIG_STORAGE_SD_GPIO_CS_NUM);
#endif
  } else if (s_sd_cs_last_err == ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG,
             "Bus I2C instable : lecture NACK pendant la configuration de la "
             "ligne CS. Inspectez les pull-ups et le câblage CH422G.");
  }

#if CONFIG_STORAGE_SD_USE_GPIO_CS
  ESP_LOGW(TAG, "Vérifiez la configuration GPIO CS (%d) et l'état du câblage.",
           CONFIG_STORAGE_SD_GPIO_CS_NUM);
#elif CONFIG_STORAGE_SD_GPIO_FALLBACK
  ESP_LOGW(TAG,
           "Fallback GPIO%d configuré : connectez le fil CS direct ou rétablissez "
           "le CH422G pour retrouver la microSD.",
           CONFIG_STORAGE_SD_GPIO_CS_NUM);
#else
  ESP_LOGW(TAG,
           "Le firmware continuera sans carte SD tant que le bus CH422G ne "
           "répond pas ou qu'aucun fallback GPIO n'est configuré.");
#endif
  menu_header_update();
}

static void sd_write_selftest(void) {
  const char *path = SD_MOUNT_POINT "/selftest.txt";
  FILE *f = fopen(path, "w");
  if (!f) {
    ESP_LOGE(TAG, "Impossible de créer %s: %s", path, strerror(errno));
    return;
  }

  unsigned long now_us = (unsigned long)esp_timer_get_time();
  if (fprintf(f, "OK %lu\n", now_us) < 0) {
    ESP_LOGE(TAG, "Écriture selftest échouée: %s", strerror(errno));
    fclose(f);
    return;
  }

  if (fclose(f) != 0) {
    ESP_LOGE(TAG, "Fermeture selftest.txt échouée: %s", strerror(errno));
    return;
  }

  ESP_LOGI(TAG, "SD selftest.txt written");
}

static void menu_header_update(void) {
  if (menu_header_time_label) {
    time_t now = time(NULL);
    struct tm info;
    char buffer[32];
    if (now != (time_t)-1 && localtime_r(&now, &info)) {
      if (strftime(buffer, sizeof(buffer), "%H:%M", &info) == 0) {
        snprintf(buffer, sizeof(buffer), "--:--");
      }
    } else {
      snprintf(buffer, sizeof(buffer), "--:--");
    }
    lv_label_set_text(menu_header_time_label, buffer);
  }

  if (menu_header_sd_label) {
    char sd_text[96];
    lv_color_t sd_color = lv_color_hex(0x2F4F43);
    const char *cs_hint = "";
    bool forced_fallback = false;
#if CONFIG_STORAGE_SD_USE_GPIO_CS || CONFIG_STORAGE_SD_GPIO_FALLBACK
    if (sd_uses_direct_cs()) {
      if (sd_fallback_due_to_ch422g()) {
#if CONFIG_STORAGE_SD_USE_GPIO_CS
        cs_hint = " \u00b7 GPIO (!)";
#else
        cs_hint = " \u00b7 GPIO fallback (!)";
#endif
        forced_fallback = true;
      } else {
#if CONFIG_STORAGE_SD_USE_GPIO_CS
        cs_hint = " \u00b7 GPIO";
#else
        cs_hint = " \u00b7 GPIO fallback";
#endif
      }
    } else {
      cs_hint = " \u00b7 CH422G";
    }
#endif
    if (!s_sd_cs_ready) {
      const char *err = (s_sd_cs_last_err != ESP_OK)
                            ? esp_err_to_name(s_sd_cs_last_err)
                            : "bus";
      snprintf(sd_text, sizeof(sd_text),
               LV_SYMBOL_WARNING " microSD indisponible (%s)%s", err, cs_hint);
      sd_color = lv_color_hex(0xB54B3A);
    } else if (sd_is_mounted()) {
      snprintf(sd_text, sizeof(sd_text), LV_SYMBOL_SD_CARD " microSD prête%s",
               cs_hint);
      sd_color = forced_fallback ? lv_color_hex(0xB27B16)
                                 : lv_color_hex(0x2F4F43);
    } else {
      snprintf(sd_text, sizeof(sd_text), LV_SYMBOL_SD_CARD " microSD en attente%s",
               cs_hint);
      sd_color = forced_fallback ? lv_color_hex(0xB27B16)
                                 : lv_color_hex(0xA46A2D);
    }
    lv_label_set_text(menu_header_sd_label, sd_text);
    lv_obj_set_style_text_color(menu_header_sd_label, sd_color, 0);
  }

  if (menu_header_sleep_label) {
    bool enabled = sleep_is_enabled();
    const char *text = enabled ? LV_SYMBOL_POWER " Veille auto: ON"
                               : LV_SYMBOL_POWER " Veille auto: OFF";
    lv_color_t color = enabled ? lv_color_hex(0x2F4F43)
                               : lv_color_hex(0x1F7A70);
    lv_label_set_text(menu_header_sleep_label, text);
    lv_obj_set_style_text_color(menu_header_sleep_label, color, 0);
  }
}

static void menu_header_timer_cb(lv_timer_t *timer) {
  (void)timer;
  menu_header_update();
}

void sleep_timer_arm(bool arm) {
  if (!sleep_timer) {
    menu_header_update();
    return;
  }

  if (!sleep_enabled || !arm || !reptile_game_is_active()) {
    lv_timer_pause(sleep_timer);
    menu_header_update();
    return;
  }

  lv_timer_resume(sleep_timer);
  lv_timer_reset(sleep_timer);
  menu_header_update();
}

static void start_game_mode(void) {
  reptile_game_stop();
  reptile_game_init();
  reptile_game_start(panel_handle, tp_handle);
  logging_init(reptile_get_state);
  if (!sleep_timer)
    sleep_timer = lv_timer_create(sleep_timer_cb, 120000, NULL);
  lv_timer_pause(sleep_timer);
  settings_apply();
  sleep_timer_arm(true);
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
  sleep_timer_arm(false);
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
  sleep_timer_arm(false);
  save_last_mode(APP_MODE_SETTINGS);
  settings_screen_show();
}

static void menu_btn_wake_cb(lv_event_t *e) {
  (void)e;
  ESP_LOGI(TAG, "Désactivation manuelle de la veille automatique");
  sleep_set_enabled(false);
  sleep_timer_arm(false);
  menu_hint_append("Veille automatique désactivée pour cette session.");
  menu_header_update();
}

void sleep_set_enabled(bool enabled) {
  sleep_enabled = enabled;
  if (sleep_timer) {
    if (enabled) {
      lv_timer_set_period(sleep_timer, 120000);
    }
    sleep_timer_arm(enabled);
  }
  menu_header_update();
}

bool sleep_is_enabled(void) { return sleep_enabled; }

static void menu_hint_append(const char *message) {
  if (!menu_quick_hint_label || !message || message[0] == '\0') {
    return;
  }

  const char *existing = lv_label_get_text(menu_quick_hint_label);
  if (existing && existing[0] != '\0') {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s\n%s", existing, message);
    lv_label_set_text(menu_quick_hint_label, buffer);
  } else {
    lv_label_set_text(menu_quick_hint_label, message);
  }
  lv_obj_clear_flag(menu_quick_hint_label, LV_OBJ_FLAG_HIDDEN);
}

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
  bool wdt_registered = false;
  bool restart_required = false;

  if (sd_is_mounted()) {
    return;
  }

  if (!s_sd_cs_ready) {
#if CONFIG_STORAGE_SD_USE_GPIO_CS || CONFIG_STORAGE_SD_GPIO_FALLBACK
    if (sd_fallback_due_to_ch422g() && sd_uses_direct_cs()) {
      ESP_LOGE(TAG,
               "Attente SD annulée : fallback GPIO%d inactif tant que le pont EXIO%u→GPIO%d "
               "n'est pas câblé (%s).",
               CONFIG_STORAGE_SD_GPIO_CS_NUM, CONFIG_CH422G_EXIO_SD_CS,
               CONFIG_STORAGE_SD_GPIO_CS_NUM, esp_err_to_name(s_sd_cs_last_err));
      char screen_msg[192];
      snprintf(screen_msg, sizeof(screen_msg),
               "Fallback GPIO%d requis\nRelier EXIO%u→GPIO%d puis activer\n"
               "l'auto-mount dans menuconfig.",
               CONFIG_STORAGE_SD_GPIO_CS_NUM, CONFIG_CH422G_EXIO_SD_CS,
               CONFIG_STORAGE_SD_GPIO_CS_NUM);
      show_error_screen(screen_msg);
      if (lvgl_port_lock(-1)) {
        char hint[192];
        snprintf(hint, sizeof(hint),
                 "CS direct sur GPIO%d inactif. Relier EXIO%u→GPIO%d et activer"
                 " l'option d'auto-mount du fallback.",
                 CONFIG_STORAGE_SD_GPIO_CS_NUM, CONFIG_CH422G_EXIO_SD_CS,
                 CONFIG_STORAGE_SD_GPIO_CS_NUM);
        menu_hint_append(hint);
        lvgl_port_unlock();
      }
      menu_header_update();
      return;
    }
#endif
    ESP_LOGE(TAG,
             "Attente SD annulée : autotest CS échoué (%s). Réparez le bus "
             "CH422G ou activez le fallback GPIO dans menuconfig.",
             esp_err_to_name(s_sd_cs_last_err));
    show_error_screen("Erreur bus CH422G / CS SD\nVérifier câblage I2C");
    menu_header_update();
    return;
  }

  esp_err_t wdt_ret = esp_task_wdt_add(NULL);
  if (wdt_ret == ESP_OK) {
    wdt_registered = true;
  } else {
    ESP_LOGW(TAG, "Impossible d'enregistrer le WDT tâche: %s",
             esp_err_to_name(wdt_ret));
  }

  while (true) {
    if (wdt_registered) {
      esp_task_wdt_reset();
    }
    err = sd_mount(&s_sd_card);
    if (err == ESP_OK) {
      hide_error_screen();
      sd_write_selftest();
      if (wdt_registered) {
        esp_err_t del_ret = esp_task_wdt_delete(NULL);
        if (del_ret != ESP_OK) {
          ESP_LOGW(TAG, "Impossible de se désinscrire du WDT tâche: %s",
                   esp_err_to_name(del_ret));
        }
        wdt_registered = false;
      }
      menu_header_update();
      return;
    }

    s_sd_card = NULL;
    ESP_LOGE(TAG, "Carte SD absente ou illisible (%s)", esp_err_to_name(err));
    show_error_screen("Insérer une carte SD valide");
    menu_header_update();
    vTaskDelay(pdMS_TO_TICKS(500));
    if (++attempts >= max_attempts) {
      restart_required = true;
#if CONFIG_STORAGE_SD_USE_GPIO_CS || CONFIG_STORAGE_SD_GPIO_FALLBACK
      if (sd_uses_direct_cs() && sd_fallback_due_to_ch422g()) {
        restart_required = false;
        ESP_LOGE(TAG,
                 "Fallback GPIO%d actif sans câblage détecté. Relier EXIO%u (SD_CS) à "
                 "GPIO%d puis activer Component config → Storage / SD card → "
                 "Automatically mount the fallback CS, ou laisser l'option "
                 "désactivée pour éviter les WDT.",
                 CONFIG_STORAGE_SD_GPIO_CS_NUM, CONFIG_CH422G_EXIO_SD_CS,
                 CONFIG_STORAGE_SD_GPIO_CS_NUM);
        s_sd_cs_ready = false;
        s_sd_cs_last_err = err;
        if (lvgl_port_lock(-1)) {
          char hint[192];
          snprintf(hint, sizeof(hint),
                   "Fallback CS direct sur GPIO%d.\nRelier EXIO%u→GPIO%d puis "
                   "activer l'option d'auto-mount dans menuconfig.",
                   CONFIG_STORAGE_SD_GPIO_CS_NUM, CONFIG_CH422G_EXIO_SD_CS,
                   CONFIG_STORAGE_SD_GPIO_CS_NUM);
          menu_hint_append(hint);
          lvgl_port_unlock();
        }
        char screen_msg[192];
        snprintf(screen_msg, sizeof(screen_msg),
                 "Fallback GPIO%d actif\nCâbler EXIO%u→GPIO%d puis activer\n"
                 "l'auto-mount dans menuconfig.",
                 CONFIG_STORAGE_SD_GPIO_CS_NUM, CONFIG_CH422G_EXIO_SD_CS,
                 CONFIG_STORAGE_SD_GPIO_CS_NUM);
        show_error_screen(screen_msg);
        break;
      }
#endif
      show_error_screen("Carte SD absente - redémarrage");
      vTaskDelay(pdMS_TO_TICKS(2000));
      break;
    }
    // Attendre indéfiniment jusqu'à insertion d'une carte valide
  }

  if (wdt_registered) {
    esp_err_t del_ret = esp_task_wdt_delete(NULL);
    if (del_ret != ESP_OK) {
      ESP_LOGW(TAG, "Impossible de se désinscrire du WDT tâche: %s",
               esp_err_to_name(del_ret));
    }
  }
  menu_header_update();
  if (restart_required) {
    esp_restart();
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
  if (!reptile_game_is_active())
    return;
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
  esp_err_t err = ESP_OK;
  if (sd_is_mounted()) {
    err = sd_unmount();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "D\u00e9montage SD: %s", esp_err_to_name(err));
      goto cleanup;
    }
    s_sd_card = NULL;
  }
  menu_header_update();
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
  sleep_timer_arm(true);
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
  sd_cs_selftest();
  if (s_sd_cs_ready) {
    esp_err_t sd_ret = sd_mount(&s_sd_card);
    if (sd_ret == ESP_OK) {
      sd_write_selftest();
    } else {
      ESP_LOGW(TAG, "Initial SD init failed: %s", esp_err_to_name(sd_ret));
      s_sd_card = NULL;
    }
  } else {
    ESP_LOGW(TAG,
             "Initial SD init skipped: autotest CS échoué (%s)",
             esp_err_to_name(s_sd_cs_last_err));
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
  ui_theme_init();

  // Initialize SD card (retry until available)
  wait_for_sd_card();

  ESP_LOGI(TAG, "Display LVGL demos");

  // Lock the mutex because LVGL APIs are not thread-safe
  if (lvgl_port_lock(-1)) {
    // Create main menu screen
    menu_screen = lv_obj_create(NULL);
    ui_theme_apply_screen(menu_screen);
    lv_obj_set_style_pad_all(menu_screen, 32, 0);
    lv_obj_set_style_pad_gap(menu_screen, 24, 0);
    lv_obj_set_flex_flow(menu_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(menu_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);

    lv_obj_t *header = ui_theme_create_card(menu_screen);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(header, 24, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(header, 20, LV_PART_MAIN);

    lv_obj_t *brand_box = lv_obj_create(header);
    lv_obj_remove_style_all(brand_box);
    lv_obj_set_flex_flow(brand_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(brand_box, 20, 0);
    lv_obj_set_scrollbar_mode(brand_box, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *logo = lv_img_create(brand_box);
    lv_img_set_src(logo, &gImage_reptile_happy);
    lv_img_set_zoom(logo, 160);

    lv_obj_t *brand_text = lv_obj_create(brand_box);
    lv_obj_remove_style_all(brand_text);
    lv_obj_set_flex_flow(brand_text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(brand_text, 6, 0);
    lv_obj_set_scrollbar_mode(brand_text, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *brand_title = lv_label_create(brand_text);
    ui_theme_apply_title(brand_title);
    lv_label_set_text(brand_title, "SimulRepile Control");

    lv_obj_t *brand_caption = lv_label_create(brand_text);
    ui_theme_apply_caption(brand_caption);
    lv_label_set_text(brand_caption,
                      "Gestion multi-terrariums & conformité CITES");

    lv_obj_t *status_box = lv_obj_create(header);
    lv_obj_remove_style_all(status_box);
    lv_obj_set_flex_flow(status_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(status_box, 6, 0);
    lv_obj_set_scrollbar_mode(status_box, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_align_self(status_box, LV_ALIGN_END, 0);

    menu_header_time_label = lv_label_create(status_box);
    ui_theme_apply_title(menu_header_time_label);
    lv_obj_set_style_text_align(menu_header_time_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(menu_header_time_label, "--:--");

    menu_header_sd_label = lv_label_create(status_box);
    ui_theme_apply_body(menu_header_sd_label);
    lv_obj_set_style_text_align(menu_header_sd_label, LV_TEXT_ALIGN_RIGHT, 0);

    menu_header_sleep_label = lv_label_create(status_box);
    ui_theme_apply_caption(menu_header_sleep_label);
    lv_obj_set_style_text_align(menu_header_sleep_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *nav_grid = lv_obj_create(menu_screen);
    lv_obj_remove_style_all(nav_grid);
    lv_obj_set_width(nav_grid, LV_PCT(100));
    lv_obj_set_flex_flow(nav_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_gap(nav_grid, 24, 0);
    lv_obj_set_style_pad_all(nav_grid, 4, 0);
    lv_obj_set_scrollbar_mode(nav_grid, LV_SCROLLBAR_MODE_OFF);

    ui_theme_create_nav_card(nav_grid, "Mode Jeu",
                             "Simulation avancée, IA et sauvegardes multislot",
                             LV_SYMBOL_PLAY, UI_THEME_NAV_ICON_SYMBOL,
                             menu_btn_game_cb, NULL);

    const lv_image_dsc_t *real_icon =
        ui_theme_get_icon(UI_THEME_ICON_TERRARIUM_OK);
    ui_theme_create_nav_card(nav_grid, "Mode Réel",
                             "Capteurs physiques, automation CH422G et microSD",
                             real_icon, UI_THEME_NAV_ICON_IMAGE, menu_btn_real_cb,
                             NULL);

    ui_theme_create_nav_card(nav_grid, "Paramètres",
                             "Profils terrariums, calendriers et calibrations",
                             LV_SYMBOL_SETTINGS, UI_THEME_NAV_ICON_SYMBOL,
                             menu_btn_settings_cb, NULL);

    menu_quick_hint_label = lv_label_create(menu_screen);
    ui_theme_apply_caption(menu_quick_hint_label);
    lv_label_set_long_mode(menu_quick_hint_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(menu_quick_hint_label, LV_PCT(100));
    lv_obj_set_style_text_align(menu_quick_hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(menu_quick_hint_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *wake_btn = ui_theme_create_button(
        menu_screen, "Quitter veille", UI_THEME_BUTTON_SECONDARY,
        menu_btn_wake_cb, NULL);
    lv_obj_set_width(wake_btn, 260);
    lv_obj_set_style_align_self(wake_btn, LV_ALIGN_CENTER, 0);

    uint8_t last_mode = APP_MODE_MENU_OVERRIDE;
    bool has_persisted_mode = false;
    nvs_handle_t nvs;
    if (nvs_open("cfg", NVS_READWRITE, &nvs) == ESP_OK) {
      esp_err_t nvs_ret = nvs_get_u8(nvs, "last_mode", &last_mode);
      nvs_close(nvs);

      if (nvs_ret == ESP_OK &&
          (last_mode == APP_MODE_GAME || last_mode == APP_MODE_REAL ||
           last_mode == APP_MODE_SETTINGS)) {
        has_persisted_mode = true;
      } else {
        last_mode = APP_MODE_MENU_OVERRIDE;
      }
    }

    gpio_reset_pin(QUICK_START_BTN);
    gpio_set_direction(QUICK_START_BTN, GPIO_MODE_INPUT);
    gpio_pullup_en(QUICK_START_BTN);

    bool quick_start_requested = (gpio_get_level(QUICK_START_BTN) == 0);

    if (has_persisted_mode && menu_quick_hint_label) {
      const char *last_mode_text = NULL;
      switch (last_mode) {
      case APP_MODE_GAME:
        last_mode_text = "Mode Jeu";
        break;
      case APP_MODE_REAL:
        last_mode_text = "Mode Réel";
        break;
      case APP_MODE_SETTINGS:
        last_mode_text = "Paramètres";
        break;
      default:
        last_mode_text = "Menu";
        break;
      }

      lv_label_set_text_fmt(
          menu_quick_hint_label,
          "Dernier mode sélectionné : %s\n"
          "(maintenir le bouton physique au démarrage pour relancer)",
          last_mode_text);
      lv_obj_clear_flag(menu_quick_hint_label, LV_OBJ_FLAG_HIDDEN);
    }

    menu_header_update();
    if (!menu_header_timer) {
      menu_header_timer = lv_timer_create(menu_header_timer_cb, 1000, NULL);
    }

    lv_scr_load(menu_screen);

    if (quick_start_requested && has_persisted_mode) {
      ESP_LOGI(TAG, "Démarrage rapide demandé");
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
        break;
      }
    } else if (quick_start_requested && !has_persisted_mode) {
      ESP_LOGW(TAG,
               "Bouton de démarrage rapide actif mais aucun mode persistant valide");
    }

    lvgl_port_unlock();
  }
}
