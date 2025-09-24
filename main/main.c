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
#include "ch422g.h"       // I2C expander for backlight/VCOM control
#include "io_extension.h"

#include "can.h"
#include "driver/gpio.h" // GPIO definitions for wake-up source
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_sleep.h"    // Light-sleep configuration
#include "esp_system.h"   // Reset reason API
#include "esp_task_wdt.h" // Watchdog timer API
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
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

static const char *TAG = "main"; // Tag for logging

static lv_timer_t *sleep_timer; // Inactivity timer handle
static bool sleep_enabled;      // Runtime sleep state
static bool backlight_ready;
static bool lvgl_ready;
static uint8_t backlight_percent = 100;

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

static void init_task(void *pvParameter);

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
    int cs_gpio = sd_get_cs_gpio();
    ESP_LOGI(TAG, "Ligne CS microSD pilotée directement par GPIO%d.", cs_gpio);
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
             "GPIO%d ne répond pas. Vérifiez le câblage direct de la CS microSD et "
             "la configuration menuconfig.",
             sd_get_cs_gpio());
  }

  ESP_LOGW(TAG,
           "Le firmware continuera sans carte SD tant que la ligne CS ne "
           "répond pas ou que le câblage direct n'est pas réparé.");
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

typedef struct {
  TaskHandle_t waiter;
  esp_err_t result;
} sd_mount_task_ctx_t;

typedef struct {
  TaskHandle_t idle_handle;
  int cpu_index;
  bool detached;
} idle_wdt_guard_t;

static idle_wdt_guard_t idle_wdt_guard_detach_for_current_core(void) {
  idle_wdt_guard_t guard = {
      .idle_handle = NULL,
      .cpu_index = 0,
      .detached = false,
  };

#if defined(INCLUDE_xTaskGetIdleTaskHandle) && (INCLUDE_xTaskGetIdleTaskHandle == 1)
#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
  BaseType_t core = xPortGetCoreID();
  guard.cpu_index = (int)core;
  guard.idle_handle = xTaskGetIdleTaskHandleForCPU(core);
#else
  guard.cpu_index = 0;
  guard.idle_handle = xTaskGetIdleTaskHandle();
#endif
#else
  return guard;
#endif

  if (!guard.idle_handle) {
    return guard;
  }

  esp_err_t status = esp_task_wdt_status(guard.idle_handle);
  if (status == ESP_OK) {
    esp_err_t ret = esp_task_wdt_delete(guard.idle_handle);
    if (ret == ESP_OK) {
      guard.detached = true;
    } else if (ret != ESP_ERR_INVALID_STATE && ret != ESP_ERR_NOT_FOUND) {
      ESP_LOGW(TAG,
               "Impossible de désinscrire l'idle task CPU%d du WDT: %s",
               guard.cpu_index, esp_err_to_name(ret));
    }
  } else if (status != ESP_ERR_INVALID_STATE && status != ESP_ERR_NOT_FOUND) {
    ESP_LOGW(TAG,
             "Statut inattendu du WDT pour l'idle task CPU%d: %s",
             guard.cpu_index, esp_err_to_name(status));
  }

  return guard;
}

static void idle_wdt_guard_restore(const idle_wdt_guard_t *guard) {
  if (!guard || !guard->detached || !guard->idle_handle) {
    return;
  }

#if defined(INCLUDE_xTaskGetIdleTaskHandle) && (INCLUDE_xTaskGetIdleTaskHandle == 1)
  esp_err_t ret = esp_task_wdt_add(guard->idle_handle);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE && ret != ESP_ERR_INVALID_ARG) {
    ESP_LOGW(TAG,
             "Impossible de réinscrire l'idle task CPU%d au WDT: %s",
             guard->cpu_index, esp_err_to_name(ret));
  }
#else
  (void)guard;
#endif
}

static void sd_mount_task(void *param) {
  sd_mount_task_ctx_t *ctx = (sd_mount_task_ctx_t *)param;
  if (!ctx) {
    vTaskDelete(NULL);
    return;
  }

  idle_wdt_guard_t idle_guard = idle_wdt_guard_detach_for_current_core();
  ctx->result = sd_mount();
  idle_wdt_guard_restore(&idle_guard);

  xTaskNotifyGive(ctx->waiter);
  vTaskDelete(NULL);
}

static void task_wdt_feed_if_registered(bool *registered, const char *context) {
  if (!registered || !*registered) {
    return;
  }

  esp_err_t reset_ret = esp_task_wdt_reset();
  if (reset_ret == ESP_OK) {
    return;
  }

  if (reset_ret == ESP_ERR_NOT_FOUND || reset_ret == ESP_ERR_INVALID_STATE) {
    *registered = false;
    ESP_LOGW(TAG,
             "%s: tâche non inscrite auprès du WDT (%s) – keepalive suspendu",
             context ? context : "WDT", esp_err_to_name(reset_ret));
    return;
  }

  ESP_LOGW(TAG, "%s: rafraîchissement WDT impossible (%s)",
           context ? context : "WDT", esp_err_to_name(reset_ret));
}

static esp_err_t sd_mount_with_watchdog(bool *wdt_registered) {
  sd_mount_task_ctx_t ctx = {
      .waiter = xTaskGetCurrentTaskHandle(),
      .result = ESP_FAIL,
  };

#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
  const BaseType_t task_core = 1;
#else
  const BaseType_t task_core = tskNO_AFFINITY;
#endif

  BaseType_t rc = xTaskCreatePinnedToCore(sd_mount_task, "sd_mount", 4096, &ctx,
                                          tskIDLE_PRIORITY + 1, NULL, task_core);
  if (rc != pdPASS) {
    ESP_LOGE(TAG,
             "Impossible de créer la tâche sd_mount (rc=%ld)", (long)rc);
    return ESP_ERR_NO_MEM;
  }

  const TickType_t wait_ticks = pdMS_TO_TICKS(500);
  while (ulTaskNotifyTake(pdTRUE, wait_ticks) == 0) {
    task_wdt_feed_if_registered(wdt_registered, "sd_mount");
  }

  task_wdt_feed_if_registered(wdt_registered, "sd_mount");

  return ctx.result;
}

static void menu_header_update_locked(void) {
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
    char cs_hint[32];
    lv_color_t sd_color = lv_color_hex(0x2F4F43);
    snprintf(cs_hint, sizeof(cs_hint), " \u00b7 CS=GPIO%d", sd_get_cs_gpio());
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
      sd_color = lv_color_hex(0x2F4F43);
    } else {
      snprintf(sd_text, sizeof(sd_text), LV_SYMBOL_SD_CARD " microSD en attente%s",
               cs_hint);
      sd_color = lv_color_hex(0xA46A2D);
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

static void menu_header_update(void) {
  if (!lvgl_ready) {
    return;
  }
  if (!lvgl_port_lock(100)) {
    ESP_LOGW(TAG, "LVGL busy, skipping menu header refresh");
    return;
  }
  menu_header_update_locked();
  lvgl_port_unlock();
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
    if (err != ESP_OK) {
      lv_obj_t *mbox = lv_msgbox_create(NULL);
      lv_msgbox_add_title(mbox, "Erreur");
      char msg[96];
      snprintf(msg,
               sizeof(msg),
               "Initialisation capteurs échouée (%s)",
               esp_err_to_name(err));
      lv_msgbox_add_text(mbox, msg);
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
    if (err != ESP_OK) {
      sensors_deinit();
      lv_obj_t *mbox = lv_msgbox_create(NULL);
      lv_msgbox_add_title(mbox, "Erreur");
      char msg[96];
      snprintf(msg,
               sizeof(msg),
               "Initialisation actionneurs échouée (%s)",
               esp_err_to_name(err));
      lv_msgbox_add_text(mbox, msg);
      lv_msgbox_add_close_button(mbox);
      lv_obj_center(mbox);
      return;
    }
    save_last_mode(APP_MODE_REAL);
    reptile_real_start(panel_handle, tp_handle);
    if (sensors_is_using_simulation_fallback()) {
      lv_obj_t *warn = lv_msgbox_create(NULL);
      lv_msgbox_add_title(warn, "Attention");
      lv_msgbox_add_text(warn,
                         "Aucun capteur physique détecté.\n"
                         "Lecture en mode simulation.");
      lv_msgbox_add_close_button(warn);
      lv_obj_center(warn);
    }
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

static void menu_hint_append_locked(const char *message) {
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

static void menu_hint_append(const char *message) {
  if (!message || message[0] == '\0') {
    return;
  }

  if (!lvgl_ready) {
    return;
  }

  if (!lvgl_port_lock(100)) {
    ESP_LOGW(TAG, "LVGL busy, skipping hint update");
    return;
  }

  menu_hint_append_locked(message);
  lvgl_port_unlock();
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
  bool wdt_added_here = false;
  bool restart_required = false;

  if (sd_is_mounted()) {
    return;
  }

  if (!s_sd_cs_ready) {
    ESP_LOGE(TAG,
             "Attente SD annulée : autotest CS échoué (%s). Réparez le bus direct GPIO%d.",
             esp_err_to_name(s_sd_cs_last_err), sd_get_cs_gpio());
    show_error_screen("CS microSD directe indisponible\nVérifier le câblage GPIO");
    menu_header_update();
    return;
  }

  esp_err_t wdt_status = esp_task_wdt_status(NULL);
  if (wdt_status == ESP_OK) {
    wdt_registered = true;
  } else if (wdt_status == ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG,
             "wait_for_sd: WDT tâche non initialisé (%s) – pas de supervision",
             esp_err_to_name(wdt_status));
  } else {
    esp_err_t wdt_ret = esp_task_wdt_add(NULL);
    if (wdt_ret == ESP_OK) {
      wdt_registered = true;
      wdt_added_here = true;
    } else if (wdt_ret == ESP_ERR_INVALID_ARG) {
      wdt_registered = true;
    } else if (wdt_ret == ESP_ERR_INVALID_STATE) {
      ESP_LOGW(TAG,
               "wait_for_sd: WDT tâche non initialisé (%s) – pas de supervision",
               esp_err_to_name(wdt_ret));
    } else {
      ESP_LOGW(TAG, "Impossible d'enregistrer le WDT tâche: %s",
               esp_err_to_name(wdt_ret));
    }
  }

  while (true) {
    task_wdt_feed_if_registered(&wdt_registered, "wait_for_sd");
    err = sd_mount_with_watchdog(&wdt_registered);
    if (err == ESP_OK) {
      s_sd_card = sd_get_card();
      hide_error_screen();
      sd_write_selftest();
      task_wdt_feed_if_registered(&wdt_registered, "wait_for_sd");
      if (wdt_added_here) {
        esp_err_t del_ret = esp_task_wdt_delete(NULL);
        if (del_ret == ESP_OK || del_ret == ESP_ERR_NOT_FOUND) {
          wdt_registered = false;
          wdt_added_here = false;
        } else if (del_ret == ESP_ERR_INVALID_STATE) {
          ESP_LOGW(TAG,
                   "wait_for_sd: WDT tâche non initialisé (%s) – désinscription implicite",
                   esp_err_to_name(del_ret));
          wdt_registered = false;
          wdt_added_here = false;
        } else {
          ESP_LOGW(TAG, "Impossible de se désinscrire du WDT tâche: %s",
                   esp_err_to_name(del_ret));
        }
        if (!wdt_registered) {
          task_wdt_feed_if_registered(&wdt_registered, "wait_for_sd");
        }
      }
      menu_header_update();
      return;
    }

    s_sd_card = NULL;
    ESP_LOGE(TAG, "Carte SD absente ou illisible (%s)", esp_err_to_name(err));
    show_error_screen("Insérer une carte SD valide");
    menu_header_update();
    vTaskDelay(pdMS_TO_TICKS(500));
    task_wdt_feed_if_registered(&wdt_registered, "wait_for_sd");
    if (++attempts >= max_attempts) {
      restart_required = true;
      show_error_screen("Carte SD absente - redémarrage");
      vTaskDelay(pdMS_TO_TICKS(2000));
      break;
    }
    // Attendre indéfiniment jusqu'à insertion d'une carte valide
  }

  if (wdt_registered && wdt_added_here) {
    esp_err_t del_ret = esp_task_wdt_delete(NULL);
    if (del_ret == ESP_OK || del_ret == ESP_ERR_NOT_FOUND) {
      wdt_registered = false;
      wdt_added_here = false;
    } else if (del_ret == ESP_ERR_INVALID_STATE) {
      ESP_LOGW(TAG,
               "wait_for_sd: WDT tâche non initialisé (%s) – désinscription implicite",
               esp_err_to_name(del_ret));
      wdt_registered = false;
      wdt_added_here = false;
    } else {
      ESP_LOGW(TAG, "Impossible de se désinscrire du WDT tâche: %s",
               esp_err_to_name(del_ret));
    }
  }
  menu_header_update();
  if (restart_required) {
    esp_restart();
  }
}

static void backlight_apply(uint8_t percent)
{
  if (!backlight_ready) {
    return;
  }

  if (percent > 100) {
    percent = 100;
  }

  esp_err_t ret = IO_EXTENSION_Output(IO_EXTENSION_IO_2, percent > 0);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Backlight gate update failed: %s", esp_err_to_name(ret));
  }

  ret = IO_EXTENSION_Pwm_Output(percent);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Backlight PWM update failed: %s", esp_err_to_name(ret));
  }
}

static void backlight_init(void)
{
  esp_err_t ret = IO_EXTENSION_Init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "IO extension init failed for backlight: %s", esp_err_to_name(ret));
    backlight_ready = false;
    return;
  }

  backlight_ready = true;
  backlight_apply(backlight_percent);
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
  if (backlight_ready) {
    esp_err_t ret = IO_EXTENSION_Pwm_Output(0);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to stop backlight PWM: %s", esp_err_to_name(ret));
    }
    ret = IO_EXTENSION_Output(IO_EXTENSION_IO_2, 0);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to gate backlight: %s", esp_err_to_name(ret));
    }
  }

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
  if (backlight_ready) {
    backlight_apply(backlight_percent);
  }

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
void app_main(void) {
  esp_reset_reason_t rr = esp_reset_reason();
  ESP_LOGI(TAG, "Reset reason: %d", rr);

  BaseType_t rc = xTaskCreatePinnedToCore(init_task, "init_task", 16384, NULL,
                                          tskIDLE_PRIORITY + 1, NULL, 0);
  if (rc != pdPASS) {
    ESP_LOGE(TAG, "Failed to create init_task");
    esp_system_abort("init_task");
  }
}

static void init_task(void *pvParameter) {
  (void)pvParameter;

  ESP_LOGI(TAG, "T0 init_task start");
  vTaskDelay(pdMS_TO_TICKS(100));

  bool wdt_registered = false;
  esp_err_t wdt_err = esp_task_wdt_add(NULL);
  if (wdt_err == ESP_OK) {
    wdt_registered = true;
  } else if (wdt_err == ESP_ERR_INVALID_ARG) {
    /* La tâche est déjà inscrite auprès du WDT (CONFIG_ESP_TASK_WDT_INIT). */
    wdt_registered = true;
    ESP_LOGD(TAG, "init_task déjà enregistré auprès du WDT");
  } else if (wdt_err == ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG,
             "init_task: le WDT tâche n'est pas initialisé (%s) – keepalive désactivé",
             esp_err_to_name(wdt_err));
  } else {
    ESP_LOGW(TAG, "init_task: impossible d'ajouter la tâche au WDT (%s)",
             esp_err_to_name(wdt_err));
  }

#define INIT_TASK_WDT_FEED()                                                  \
  do {                                                                        \
    task_wdt_feed_if_registered(&wdt_registered, "init_task");               \
  } while (0)

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  INIT_TASK_WDT_FEED();

  settings_init();
  INIT_TASK_WDT_FEED();

  ESP_LOGI(TAG, "T1 LCD init start");
  panel_handle = waveshare_esp32_s3_rgb_lcd_init();
  if (!panel_handle) {
    ESP_LOGE(TAG, "Failed to initialize RGB LCD panel");
    goto exit;
  }
  backlight_init();
  esp_err_t lvgl_ret = lvgl_port_init(panel_handle, NULL);
  lvgl_ready = (lvgl_ret == ESP_OK);
  if (lvgl_ret != ESP_OK) {
    ESP_LOGE(TAG, "LVGL port init failed: %s", esp_err_to_name(lvgl_ret));
  }
  ESP_LOGI(TAG, "T1 LCD init done");
  INIT_TASK_WDT_FEED();
  vTaskDelay(pdMS_TO_TICKS(10));
  if (lvgl_ret != ESP_OK) {
    goto exit;
  }

  ESP_LOGI(TAG, "T2 GT911 init start");
  esp_err_t tp_ret = touch_gt911_init(&tp_handle);
  if (tp_ret == ESP_OK) {
    esp_err_t attach_ret = lvgl_port_attach_touch(tp_handle);
    if (attach_ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to attach GT911 touch to LVGL: %s", esp_err_to_name(attach_ret));
    }
  } else {
    ESP_LOGE(TAG, "Failed to initialize GT911 touch controller: %s", esp_err_to_name(tp_ret));
  }
  ESP_LOGI(TAG, "T2 GT911 init done (status=%s)", esp_err_to_name(tp_ret));
  INIT_TASK_WDT_FEED();
  vTaskDelay(pdMS_TO_TICKS(10));
  if (tp_ret != ESP_OK) {
    goto exit;
  }

  ESP_LOGI(TAG, "T3 CH422G init start");
  esp_err_t ch_ret = ch422g_init();
  if (ch_ret != ESP_OK) {
    ESP_LOGE(TAG, "CH422G init failed: %s", esp_err_to_name(ch_ret));
  }
  ESP_LOGI(TAG, "T3 CH422G init done (status=%s)", esp_err_to_name(ch_ret));
  INIT_TASK_WDT_FEED();
  vTaskDelay(pdMS_TO_TICKS(10));

  ESP_LOGI(TAG, "T4 SD init start");
  sd_cs_selftest();
  INIT_TASK_WDT_FEED();
#if CONFIG_SD_AUTOMOUNT
  if (s_sd_cs_ready) {
    INIT_TASK_WDT_FEED();
    esp_err_t sd_ret = sd_mount_with_watchdog(&wdt_registered);
    if (sd_ret == ESP_OK) {
      s_sd_card = sd_get_card();
      sd_write_selftest();
      INIT_TASK_WDT_FEED();
    } else {
      s_sd_card = NULL;
      ESP_LOGW(TAG, "Initial SD init failed: %s", esp_err_to_name(sd_ret));
    }
  } else {
    ESP_LOGW(TAG,
             "Initial SD init skipped: autotest CS échoué (%s)",
             esp_err_to_name(s_sd_cs_last_err));
  }
#else
  if (!s_sd_cs_ready) {
    ESP_LOGW(TAG,
             "Initial SD init skipped: autotest CS échoué (%s)",
             esp_err_to_name(s_sd_cs_last_err));
  }
#endif
  ESP_LOGI(TAG, "T4 SD init done (mounted=%d)", sd_is_mounted());
  INIT_TASK_WDT_FEED();
  vTaskDelay(pdMS_TO_TICKS(10));

  const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_125KBITS();
  const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  const twai_general_config_t g_config =
      TWAI_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, TWAI_MODE_NORMAL);
  if (can_init(t_config, f_config, g_config) != ESP_OK) {
    ESP_LOGW(TAG, "CAN indisponible – fonctionnalité désactivée");
  }
  INIT_TASK_WDT_FEED();

  ui_theme_init();
  INIT_TASK_WDT_FEED();

  wait_for_sd_card();
  INIT_TASK_WDT_FEED();

  ESP_LOGI(TAG, "Display LVGL demos");

  if (lvgl_port_lock(-1)) {
    INIT_TASK_WDT_FEED();
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
    INIT_TASK_WDT_FEED();

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
    INIT_TASK_WDT_FEED();

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
    INIT_TASK_WDT_FEED();

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
    INIT_TASK_WDT_FEED();
  }

exit:
  if (wdt_registered) {
    esp_err_t del_err = esp_task_wdt_delete(NULL);
    if (del_err != ESP_OK && del_err != ESP_ERR_INVALID_STATE) {
      ESP_LOGW(TAG, "init_task: impossible de se retirer du WDT (%s)",
               esp_err_to_name(del_err));
    }
  }
#undef INIT_TASK_WDT_FEED
  ESP_LOGI(TAG, "T9 init_task done");
  vTaskDelay(pdMS_TO_TICKS(1));
  vTaskDelete(NULL);
}
