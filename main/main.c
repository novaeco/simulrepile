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
#include "sdkconfig.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "esp_idf_version.h"
#include "esp_bit_defs.h"

static const char *TAG = "main"; // Tag for logging

#define STARTUP_WDT_TIMEOUT_MS 15000U

static lv_timer_t *sleep_timer; // Inactivity timer handle
static bool sleep_enabled;      // Runtime sleep state

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_touch_handle_t tp_handle = NULL;
static lv_obj_t *error_screen;
static lv_obj_t *prev_screen;
lv_obj_t *menu_screen;

static sdmmc_card_t *s_sd_card = NULL;
static bool s_sd_cs_ready = false;
static esp_err_t s_sd_cs_last_err = ESP_OK;
static char s_boot_error_msg[160];
static bool s_boot_error_pending = false;

#ifdef CONFIG_ESP_TASK_WDT_EN
static bool s_boot_wdt_registered = false;
#endif

static int64_t s_boot_time_origin = 0;

static void set_boot_error_message(const char *fmt, ...) {
  if (!fmt)
    return;

  va_list args;
  va_start(args, fmt);
  vsnprintf(s_boot_error_msg, sizeof(s_boot_error_msg), fmt, args);
  va_end(args);
  s_boot_error_pending = true;
}

static inline void boot_trace_event(const char *phase) {
  if (!phase)
    return;

  int64_t now = esp_timer_get_time();
  if (s_boot_time_origin == 0) {
    s_boot_time_origin = now;
  }
  uint32_t delta_ms = (uint32_t)((now - s_boot_time_origin) / 1000);
  ESP_LOGI(TAG, "[BOOT][%05" PRIu32 " ms] %s", delta_ms, phase);
#ifdef CONFIG_ESP_TASK_WDT_EN
  if (s_boot_wdt_registered) {
    esp_err_t wdt_ret = esp_task_wdt_reset();
    if (wdt_ret != ESP_OK) {
      ESP_LOGW(TAG, "esp_task_wdt_reset: %s", esp_err_to_name(wdt_ret));
    }
  }
#endif
}

#ifdef CONFIG_ESP_TASK_WDT_EN
static void configure_startup_wdt(void) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
  const esp_task_wdt_config_t cfg = {
      .timeout_ms = STARTUP_WDT_TIMEOUT_MS,
#if CONFIG_FREERTOS_UNICORE
      .idle_core_mask = BIT(0),
#else
      .idle_core_mask = BIT(0) | BIT(1),
#endif
      .trigger_panic = false,
  };
  esp_err_t cfg_ret = esp_task_wdt_reconfigure(&cfg);
  if (cfg_ret == ESP_ERR_INVALID_STATE) {
    cfg_ret = esp_task_wdt_init(&cfg);
  }
  if (cfg_ret != ESP_OK && cfg_ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "Configuration TWDT boot impossible: %s", esp_err_to_name(cfg_ret));
  }
#else
  esp_err_t cfg_ret = esp_task_wdt_init(STARTUP_WDT_TIMEOUT_MS / 1000, false);
  if (cfg_ret != ESP_OK && cfg_ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "Configuration TWDT boot impossible: %s", esp_err_to_name(cfg_ret));
  }
#endif

  esp_err_t add_ret = esp_task_wdt_add(NULL);
  if (add_ret == ESP_OK) {
    s_boot_wdt_registered = true;
  } else if (add_ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "esp_task_wdt_add: TWDT inactif");
  } else if (add_ret != ESP_OK) {
    ESP_LOGW(TAG, "esp_task_wdt_add: %s", esp_err_to_name(add_ret));
  }
}

static void restore_runtime_wdt(void) {
  if (s_boot_wdt_registered) {
    esp_err_t reset_ret = esp_task_wdt_reset();
    if (reset_ret != ESP_OK) {
      ESP_LOGW(TAG, "esp_task_wdt_reset finale: %s", esp_err_to_name(reset_ret));
    }
  }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
  uint32_t runtime_timeout_ms = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000U;
  if (runtime_timeout_ms == 0) {
    runtime_timeout_ms = 5000U;
  }
  const esp_task_wdt_config_t cfg = {
      .timeout_ms = runtime_timeout_ms,
#if CONFIG_FREERTOS_UNICORE
      .idle_core_mask = BIT(0),
#else
      .idle_core_mask = BIT(0) | BIT(1),
#endif
      .trigger_panic = false,
  };
  esp_err_t cfg_ret = esp_task_wdt_reconfigure(&cfg);
  if (cfg_ret != ESP_OK) {
    ESP_LOGW(TAG, "Restauration TWDT runtime impossible: %s", esp_err_to_name(cfg_ret));
  }
#endif

  if (s_boot_wdt_registered) {
    esp_err_t del_ret = esp_task_wdt_delete(NULL);
    if (del_ret != ESP_OK && del_ret != ESP_ERR_NOT_FOUND) {
      ESP_LOGW(TAG, "esp_task_wdt_delete: %s", esp_err_to_name(del_ret));
    }
    if (del_ret == ESP_OK || del_ret == ESP_ERR_NOT_FOUND) {
      s_boot_wdt_registered = false;
    }
  }
}
#else
static inline void configure_startup_wdt(void) {}
static inline void restore_runtime_wdt(void) {}
#endif

static void halt_with_error(void) {
  restore_runtime_wdt();
  while (true) {
#ifdef CONFIG_ESP_TASK_WDT_EN
    if (s_boot_wdt_registered) {
      esp_task_wdt_reset();
    }
#endif
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

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

static void sd_cs_selftest(void) {
  s_sd_cs_ready = false;
  s_sd_cs_last_err = sd_spi_cs_selftest();
  if (s_sd_cs_last_err == ESP_OK) {
    s_sd_cs_ready = true;
    return;
  }

  ESP_LOGE(TAG, "Autotest ligne CS SD impossible: %s",
           esp_err_to_name(s_sd_cs_last_err));
  set_boot_error_message(
      "Autotest CS SD échoué (%s)\nVérifier CH422G / câblage CS",
      esp_err_to_name(s_sd_cs_last_err));
  if (s_sd_cs_last_err == ESP_ERR_NOT_FOUND) {
    ESP_LOGE(TAG,
             "CH422G absent ou injoignable. Vérifiez VCC=3V3, SDA=GPIO%d, "
             "SCL=GPIO%d et les résistances de tirage 2.2–4.7 kΩ.",
             CONFIG_I2C_MASTER_SDA_GPIO, CONFIG_I2C_MASTER_SCL_GPIO);
  } else if (s_sd_cs_last_err == ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG,
             "Bus I2C instable : lecture NACK pendant la configuration de la "
             "ligne CS. Inspectez les pull-ups et le câblage CH422G.");
  } else if (s_sd_cs_last_err == ESP_ERR_NOT_SUPPORTED) {
#if CONFIG_STORAGE_SD_USE_GPIO_CS
    ESP_LOGE(TAG,
             "La broche GPIO%d est réservée par la PSRAM octale : CS direct "
             "inutilisable. Sélectionnez un GPIO libre via "
             "CONFIG_STORAGE_SD_GPIO_CS_NUM ou désactivez le fallback.",
             CONFIG_STORAGE_SD_GPIO_CS_NUM);
    set_boot_error_message(
        "GPIO%d indisponible pour la CS microSD\nChoisir un GPIO hors plage 26-37 ou "
        "désactiver CONFIG_STORAGE_SD_USE_GPIO_CS",
        CONFIG_STORAGE_SD_GPIO_CS_NUM);
#else
    ESP_LOGE(TAG,
             "CS SD direct non supporté : %s",
             esp_err_to_name(s_sd_cs_last_err));
#endif
  }

#if !CONFIG_STORAGE_SD_USE_GPIO_CS
  ESP_LOGW(TAG,
           "Le firmware continuera sans carte SD tant que le bus CH422G ne "
           "répond pas ou qu'un fallback GPIO n'est pas configuré.");
#else
  if (s_sd_cs_last_err == ESP_ERR_NOT_SUPPORTED) {
    ESP_LOGW(TAG,
             "Sélectionner une broche de CS hors plage GPIO26–GPIO37 pour "
             "éviter les conflits avec la PSRAM octale.");
  } else {
    ESP_LOGW(TAG, "Vérifiez la configuration GPIO CS (%d) et l'état du câblage.",
             CONFIG_STORAGE_SD_GPIO_CS_NUM);
  }
#endif
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

void sleep_timer_arm(bool arm) {
  if (!sleep_timer)
    return;

  if (!sleep_enabled || !arm || !reptile_game_is_active()) {
    lv_timer_pause(sleep_timer);
    return;
  }

  lv_timer_resume(sleep_timer);
  lv_timer_reset(sleep_timer);
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

void sleep_set_enabled(bool enabled) {
  sleep_enabled = enabled;
  if (!sleep_timer)
    return;
  if (enabled) {
    lv_timer_set_period(sleep_timer, 120000);
  }
  sleep_timer_arm(enabled);
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

  if (sd_is_mounted()) {
    return;
  }

  if (!s_sd_cs_ready) {
    ESP_LOGE(TAG,
             "Attente SD annulée : autotest CS échoué (%s). Réparez le bus "
             "CH422G ou activez le fallback GPIO dans menuconfig.",
             esp_err_to_name(s_sd_cs_last_err));
    set_boot_error_message("Erreur bus CH422G / CS SD\nVérifier câblage I2C");
    return;
  }

  while (true) {
#ifdef CONFIG_ESP_TASK_WDT_EN
    if (s_boot_wdt_registered) {
      esp_task_wdt_reset();
    }
#endif
    err = sd_mount(&s_sd_card);
    if (err == ESP_OK) {
      hide_error_screen();
      sd_write_selftest();
      return;
    }

    s_sd_card = NULL;
    ESP_LOGE(TAG, "Carte SD absente ou illisible (%s)", esp_err_to_name(err));
    show_error_screen("Insérer une carte SD valide");
    vTaskDelay(pdMS_TO_TICKS(500));
    if (++attempts >= max_attempts) {
      show_error_screen("Carte SD absente - redémarrage");
      vTaskDelay(pdMS_TO_TICKS(2000));
      esp_restart();
      return;
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
  configure_startup_wdt();
  boot_trace_event("Séquence d'initialisation démarrée");

  esp_reset_reason_t rr = esp_reset_reason();
  ESP_LOGI(TAG, "Reset reason: %d", rr);

  ESP_LOGI(TAG, "Initialisation NVS flash");
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  boot_trace_event("NVS initialisée");

  ESP_LOGI(TAG, "Chargement des paramètres persistants");
  settings_init();
  boot_trace_event("Paramètres chargés");

  ESP_LOGI(TAG, "Autotest ligne CS SD");
  sd_cs_selftest();
  boot_trace_event("Autotest CS SD terminé");
  if (s_sd_cs_ready) {
    esp_err_t sd_ret = sd_mount(&s_sd_card);
    if (sd_ret == ESP_OK) {
      boot_trace_event("Carte SD montée (boot)");
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

  ESP_LOGI(TAG, "Initialisation contrôleur tactile GT911");
  bool touch_ready = true;
  esp_err_t tp_ret = touch_gt911_init(&tp_handle);
  if (tp_ret != ESP_OK) {
    touch_ready = false;
    ESP_LOGE(TAG, "GT911 injoignable: %s", esp_err_to_name(tp_ret));
    set_boot_error_message("Contrôleur tactile GT911 indisponible\n"
                           "Vérifier câblage SDA/SCL/INT/RST");
  }
  boot_trace_event(touch_ready ? "GT911 initialisé" : "GT911 indisponible");

  ESP_LOGI(TAG, "Initialisation panneau RGB");
  panel_handle = waveshare_esp32_s3_rgb_lcd_init();
  boot_trace_event("Panneau RGB initialisé");

  ESP_LOGI(TAG, "Initialisation rétroéclairage");
  backlight_init();
  boot_trace_event("PWM rétroéclairage active");

  ESP_LOGI(TAG, "Configuration sorties terrarium");
  DEV_GPIO_Mode(SERVO_FEED_PIN, GPIO_MODE_OUTPUT);
  DEV_Digital_Write(SERVO_FEED_PIN, 0);
  DEV_GPIO_Mode(WATER_PUMP_PIN, GPIO_MODE_OUTPUT);
  DEV_Digital_Write(WATER_PUMP_PIN, 0);
  DEV_GPIO_Mode(HEAT_RES_PIN, GPIO_MODE_OUTPUT);
  DEV_Digital_Write(HEAT_RES_PIN, 0);
  boot_trace_event("Sorties terrarium initialisées");

  ESP_LOGI(TAG, "Initialisation bus CAN");
  const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_125KBITS();
  const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  const twai_general_config_t g_config =
      TWAI_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, TWAI_MODE_NORMAL);
  if (can_init(t_config, f_config, g_config) != ESP_OK) {
    ESP_LOGW(TAG, "CAN indisponible – fonctionnalité désactivée");
  }
  boot_trace_event("Bus CAN configuré");

  ESP_LOGI(TAG, "Initialisation LVGL");
  esp_err_t lvgl_ret = lvgl_port_init(panel_handle, touch_ready ? tp_handle : NULL);
  if (lvgl_ret != ESP_OK) {
    ESP_LOGE(TAG, "LVGL init failed: %s", esp_err_to_name(lvgl_ret));
    set_boot_error_message("Initialisation LVGL échouée (%s)",
                           esp_err_to_name(lvgl_ret));
    if (s_boot_error_pending) {
      show_error_screen(s_boot_error_msg);
    }
    halt_with_error();
    return;
  }
  boot_trace_event("LVGL initialisé");

  if (s_boot_error_pending) {
    show_error_screen(s_boot_error_msg);
  }

  if (!touch_ready) {
    halt_with_error();
    return;
  }

  boot_trace_event("Attente carte SD");
  wait_for_sd_card();
  boot_trace_event(sd_is_mounted() ? "Carte SD prête" : "Carte SD indisponible");

  ESP_LOGI(TAG, "Display LVGL demos");

  if (lvgl_port_lock(-1)) {
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
    lv_label_set_text(label, "Mode Réel");
    lv_obj_center(label);

    lv_obj_t *btn_settings = lv_btn_create(menu_screen);
    lv_obj_set_size(btn_settings, 200, 50);
    lv_obj_align(btn_settings, LV_ALIGN_CENTER, 0, 60);
    lv_obj_add_event_cb(btn_settings, menu_btn_settings_cb, LV_EVENT_CLICKED,
                        NULL);
    label = lv_label_create(btn_settings);
    lv_label_set_text(label, "Paramètres");
    lv_obj_center(label);

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

    if (has_persisted_mode) {
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

      lv_obj_t *hint_label = lv_label_create(menu_screen);
      lv_label_set_long_mode(hint_label, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(hint_label, 300);
      lv_label_set_text_fmt(hint_label,
                            "Dernier mode sélectionné : %s\n"
                            "(maintenir le bouton physique au démarrage pour relancer)",
                            last_mode_text);
      lv_obj_align(hint_label, LV_ALIGN_CENTER, 0, 140);
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

  boot_trace_event("Interface LVGL prête");
  restore_runtime_wdt();
}

