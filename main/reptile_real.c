#include "reptile_real.h"
#include "env_control.h"
#include "gpio.h"
#include "sensors.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "settings.h"
#include <math.h>

static void feed_task(void *arg);
static void env_state_cb(const reptile_env_state_t *state, void *ctx);

static lv_obj_t *screen;
static lv_obj_t *label_temp;
static lv_obj_t *label_hum;
static lv_obj_t *label_pump;
static lv_obj_t *label_heat;
static lv_obj_t *label_feed;
static volatile bool feed_running;
static TaskHandle_t feed_task_handle;
static reptile_env_state_t s_env_state;

extern lv_obj_t *menu_screen;

static void update_status_labels(void) {
  if (isnan(s_env_state.temperature))
    lv_label_set_text(label_temp, "Temp\u00e9rature: Non connect\u00e9");
  else
    lv_label_set_text_fmt(label_temp, "Temp\u00e9rature: %.1f \u00b0C", s_env_state.temperature);
  if (isnan(s_env_state.humidity))
    lv_label_set_text(label_hum, "Humidit\u00e9: Non connect\u00e9");
  else
    lv_label_set_text_fmt(label_hum, "Humidit\u00e9: %.1f %%", s_env_state.humidity);
  lv_label_set_text(label_pump, s_env_state.pumping ? "Pompe: ON" : "Pompe: OFF");
  lv_label_set_text(label_heat, s_env_state.heating ? "Chauffage: ON" : "Chauffage: OFF");
  lv_label_set_text(label_feed, feed_running ? "Nourrissage: ON" : "Nourrissage: OFF");
}

static void env_state_cb(const reptile_env_state_t *state, void *ctx) {
  (void)ctx;
  s_env_state = *state;
  if (lvgl_port_lock(-1)) {
    update_status_labels();
    lvgl_port_unlock();
  }
}


static void feed_task(void *arg) {
  (void)arg;
  feed_running = true;
  if (lvgl_port_lock(-1)) {
    update_status_labels();
    lvgl_port_unlock();
  }
  reptile_feed_gpio();
  feed_running = false;
  if (lvgl_port_lock(-1)) {
    update_status_labels();
    lvgl_port_unlock();
  }
  feed_task_handle = NULL;
  vTaskDelete(NULL);
}

static void pump_btn_cb(lv_event_t *e) {
  (void)e;
  reptile_env_manual_pump();
}

static void heat_btn_cb(lv_event_t *e) {
  (void)e;
  reptile_env_manual_heat();
}

static void feed_btn_cb(lv_event_t *e) {
  (void)e;
  if (!feed_running)
    xTaskCreate(feed_task, "feed_task", 2048, NULL, 5, &feed_task_handle);
}

static void menu_btn_cb(lv_event_t *e) {
  (void)e;
  reptile_env_stop();
  sensors_deinit();
  if (feed_task_handle) {
    vTaskDelete(feed_task_handle);
    feed_task_handle = NULL;
  }
  feed_running = false;
  reptile_actuators_deinit();

  if (lvgl_port_lock(-1)) {
    lv_scr_load(menu_screen);
    if (screen) {
      lv_obj_del(screen);
      screen = NULL;
    }
    lvgl_port_unlock();
  }
}

void reptile_real_start(esp_lcd_panel_handle_t panel, esp_lcd_touch_handle_t tp) {
  (void)panel;
  (void)tp;

  if (!lvgl_port_lock(-1))
    return;

  screen = lv_obj_create(NULL);

  label_temp = lv_label_create(screen);
  lv_obj_align(label_temp, LV_ALIGN_TOP_LEFT, 10, 10);

  label_hum = lv_label_create(screen);
  lv_obj_align(label_hum, LV_ALIGN_TOP_LEFT, 10, 40);

  label_pump = lv_label_create(screen);
  lv_obj_align(label_pump, LV_ALIGN_TOP_LEFT, 10, 80);
  lv_obj_t *btn_pump = lv_btn_create(screen);
  lv_obj_align(btn_pump, LV_ALIGN_TOP_RIGHT, -10, 75);
  lv_obj_t *lbl = lv_label_create(btn_pump);
  lv_label_set_text(lbl, "Pompe");
  lv_obj_center(lbl);
  lv_obj_add_event_cb(btn_pump, pump_btn_cb, LV_EVENT_CLICKED, NULL);

  label_heat = lv_label_create(screen);
  lv_obj_align(label_heat, LV_ALIGN_TOP_LEFT, 10, 120);
  lv_obj_t *btn_heat = lv_btn_create(screen);
  lv_obj_align(btn_heat, LV_ALIGN_TOP_RIGHT, -10, 115);
  lbl = lv_label_create(btn_heat);
  lv_label_set_text(lbl, "Chauffage");
  lv_obj_center(lbl);
  lv_obj_add_event_cb(btn_heat, heat_btn_cb, LV_EVENT_CLICKED, NULL);

  label_feed = lv_label_create(screen);
  lv_obj_align(label_feed, LV_ALIGN_TOP_LEFT, 10, 160);
  lv_obj_t *btn_feed = lv_btn_create(screen);
  lv_obj_align(btn_feed, LV_ALIGN_TOP_RIGHT, -10, 155);
  lbl = lv_label_create(btn_feed);
  lv_label_set_text(lbl, "Nourrir");
  lv_obj_center(lbl);
  lv_obj_add_event_cb(btn_feed, feed_btn_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *btn_menu = lv_btn_create(screen);
  lv_obj_align(btn_menu, LV_ALIGN_BOTTOM_MID, 0, -10);
  lbl = lv_label_create(btn_menu);
  lv_label_set_text(lbl, "Menu");
  lv_obj_center(lbl);
  lv_obj_add_event_cb(btn_menu, menu_btn_cb, LV_EVENT_CLICKED, NULL);

  s_env_state.temperature = NAN;
  s_env_state.humidity = NAN;
  s_env_state.heating = false;
  s_env_state.pumping = false;
  feed_running = false;
  update_status_labels();
  lv_disp_load_scr(screen);
  lvgl_port_unlock();

  reptile_env_thresholds_t thr = {
      .temp_setpoint = g_settings.temp_threshold,
      .humidity_setpoint = g_settings.humidity_threshold,
  };
  reptile_env_start(&thr, env_state_cb, NULL);
}

