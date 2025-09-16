#include "reptile_game.h"
#include "can.h"
#include "image.h"
#include "lvgl_port.h"
#include "sleep.h"
#include "logging.h"
#include "esp_log.h"
#include "game_mode.h"
#include <inttypes.h>
#include <stdbool.h>

LV_FONT_DECLARE(lv_font_montserrat_24);

static lv_style_t style_font24;
static lv_obj_t *screen_main;
static lv_obj_t *screen_stats;
static lv_obj_t *bar_faim;
static lv_obj_t *bar_eau;
static lv_obj_t *bar_temp;
static lv_obj_t *bar_humeur;
static lv_obj_t *bar_humidite;
static lv_obj_t *img_reptile;
static bool sprite_is_happy;
static bool s_game_active;
static lv_obj_t *label_stat_faim;
static lv_obj_t *label_stat_eau;
static lv_obj_t *label_stat_temp;
static lv_obj_t *label_stat_humeur;
static lv_obj_t *label_stat_humidite;
static lv_obj_t *lbl_sleep;
extern lv_obj_t *menu_screen;

#define REPTILE_UPDATE_PERIOD_MS 1000

static reptile_t reptile;
static uint32_t last_tick;
static lv_timer_t *life_timer;
static lv_timer_t *action_timer;
static uint32_t soothe_time_ms;
static uint32_t update_ms_accum;
static uint32_t soothe_ms_accum;

static const char *TAG = "reptile_game";


typedef enum {
  ACTION_FEED,
  ACTION_WATER,
  ACTION_HEAT,
  ACTION_SOOTHE,
} action_type_t;

static const lv_image_dsc_t *sprite_idle = &gImage_reptile_idle;
static const lv_image_dsc_t *sprite_manger = &gImage_reptile_manger;
static const lv_image_dsc_t *sprite_boire = &gImage_reptile_boire;
static const lv_image_dsc_t *sprite_chauffer = &gImage_reptile_chauffer;
static const lv_image_dsc_t *sprite_happy = &gImage_reptile_happy;
static const lv_image_dsc_t *sprite_sad = &gImage_reptile_sad;

static void warning_anim_cb(void *obj, int32_t v);
static void start_warning_anim(lv_obj_t *obj);
static void stats_btn_event_cb(lv_event_t *e);
static void back_btn_event_cb(lv_event_t *e);
static void action_btn_event_cb(lv_event_t *e);
static void sleep_btn_event_cb(lv_event_t *e);
static void menu_btn_event_cb(lv_event_t *e);
static void ui_update_main(void);
static void ui_update_stats(void);
static void show_event_popup(reptile_event_t event);
static void set_bar_color(lv_obj_t *bar, uint32_t value, uint32_t max);
static void update_sprite(void);
static void show_action_sprite(action_type_t action);
static void revert_sprite_cb(lv_timer_t *t);

bool reptile_game_is_active(void) { return s_game_active; }

void reptile_game_init(void) {

  game_mode_set(GAME_MODE_SIMULATION);
  reptile_init(&reptile, true);
  last_tick = lv_tick_get();
  update_ms_accum = 0;
  soothe_ms_accum = 0;
  if (reptile_load(&reptile) != ESP_OK) {
    reptile_save(&reptile);
  }
}

const reptile_t *reptile_get_state(void) { return &reptile; }

static void warning_anim_cb(void *obj, int32_t v) {
  lv_obj_set_style_bg_opa(obj, (lv_opa_t)v, LV_PART_MAIN);
}

static void start_warning_anim(lv_obj_t *obj) {
  lv_obj_set_style_bg_color(obj, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
  lv_anim_set_time(&a, 400);
  lv_anim_set_playback_time(&a, 400);
  lv_anim_set_repeat_count(&a, 2);
  lv_anim_set_exec_cb(&a, warning_anim_cb);
  lv_anim_start(&a);
}

static void show_event_popup(reptile_event_t event) {
  const char *msg = NULL;
  switch (event) {
  case REPTILE_EVENT_MALADIE:
    msg = "Le reptile est malade!";
    break;
  case REPTILE_EVENT_CROISSANCE:
    msg = "Le reptile grandit!";
    break;
  default:
    return;
  }
  lv_obj_t *mbox = lv_msgbox_create(NULL);
  lv_msgbox_add_title(mbox, "Évènement");
  lv_msgbox_add_text(mbox, msg);
  lv_msgbox_add_close_button(mbox);
  lv_obj_center(mbox);
}

static void set_bar_color(lv_obj_t *bar, uint32_t value, uint32_t max) {
  lv_color_t palette_color;

  if (bar == bar_temp) {
    if (value < REPTILE_TEMP_THRESHOLD_LOW ||
        value > REPTILE_TEMP_THRESHOLD_HIGH) {
      palette_color = lv_palette_main(LV_PALETTE_RED);
    } else if (value <= REPTILE_TEMP_THRESHOLD_LOW + 1 ||
               value >= REPTILE_TEMP_THRESHOLD_HIGH - 1) {
      palette_color = lv_palette_main(LV_PALETTE_YELLOW);
    } else {
      palette_color = lv_palette_main(LV_PALETTE_GREEN);
    }
  } else {
    uint32_t pct = (value * 100) / max;
    if (pct > 70) {
      palette_color = lv_palette_main(LV_PALETTE_GREEN);
    } else if (pct > 30) {
      palette_color = lv_palette_main(LV_PALETTE_YELLOW);
    } else {
      palette_color = lv_palette_main(LV_PALETTE_RED);
    }
  }

  lv_obj_set_style_bg_color(bar, palette_color, LV_PART_INDICATOR);
}

static void sprite_anim_exec_cb(void *obj, int32_t v) {
  lv_obj_set_y((lv_obj_t *)obj, v);
}

static void set_sprite_anim(bool happy) {
  lv_anim_del(img_reptile, sprite_anim_exec_cb);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, img_reptile);
  if (happy) {
    lv_anim_set_values(&a, -5, 5);
  } else {
    lv_anim_set_values(&a, 0, 5);
  }
  lv_anim_set_time(&a, 500);
  lv_anim_set_playback_time(&a, 500);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_exec_cb(&a, sprite_anim_exec_cb);
  lv_anim_start(&a);
}

static void update_sprite(void) {
  if (action_timer)
    return;
  bool happy = reptile.humeur >= 50;
  if (happy != sprite_is_happy) {
    sprite_is_happy = happy;
    lv_img_set_src(img_reptile, happy ? sprite_happy : sprite_sad);
    set_sprite_anim(happy);
  }
}

static void revert_sprite_cb(lv_timer_t *t) {
  (void)t;
  if (action_timer) {
    lv_timer_del(action_timer);
    action_timer = NULL;
  }
  update_sprite();
}

static void show_action_sprite(action_type_t action) {
  const lv_image_dsc_t *src = sprite_idle;
  switch (action) {
  case ACTION_FEED:
    src = sprite_manger;
    break;
  case ACTION_WATER:
    src = sprite_boire;
    break;
  case ACTION_HEAT:
    src = sprite_chauffer;
    break;
  case ACTION_SOOTHE:
    src = sprite_idle;
    break;
  }
  lv_img_set_src(img_reptile, src);
  set_sprite_anim(true);
  if (action_timer)
    lv_timer_del(action_timer);
  action_timer = lv_timer_create(revert_sprite_cb, 1000, NULL);
}

void reptile_tick(lv_timer_t *timer) {
  (void)timer;
  uint32_t now = lv_tick_get();
  uint32_t elapsed = now - last_tick;
  last_tick = now;

  update_ms_accum += elapsed;
  uint32_t process_ms = update_ms_accum - (update_ms_accum % 1000U);
  reptile_update(&reptile, process_ms);
  update_ms_accum -= process_ms;
  bool dirty = process_ms > 0;

  if (soothe_time_ms > 0) {
    soothe_time_ms = (soothe_time_ms > elapsed) ? (soothe_time_ms - elapsed) : 0;
    soothe_ms_accum += elapsed;
    uint32_t mood_sec = soothe_ms_accum / 1000U;
    if (mood_sec > 0) {
      uint32_t inc = mood_sec * 2U;
      reptile.humeur =
          (reptile.humeur + inc > 100) ? 100 : reptile.humeur + inc;
      soothe_ms_accum -= mood_sec * 1000U;
      dirty = true;
    }
  } else {
    soothe_ms_accum = 0;
  }

  reptile_event_t prev_evt = reptile.event;
  reptile_event_t evt = reptile_check_events(&reptile);
  if (evt != prev_evt && evt != REPTILE_EVENT_NONE) {
    show_event_popup(evt);
  }
  if (dirty) {
    reptile_save(&reptile);
  }

  ui_update_main();
  ui_update_stats();

  // Broadcast reptile state over CAN bus
  can_message_t msg = {
      .identifier = 0x100,
      .data_length_code = 8,
      .flags = TWAI_MSG_FLAG_NONE,
  };
  msg.data[0] = (uint8_t)(reptile.faim & 0xFF);
  msg.data[1] = (uint8_t)((reptile.faim >> 8) & 0xFF);
  msg.data[2] = (uint8_t)(reptile.eau & 0xFF);
  msg.data[3] = (uint8_t)((reptile.eau >> 8) & 0xFF);
  msg.data[4] = (uint8_t)(reptile.temperature & 0xFF);
  msg.data[5] = (uint8_t)((reptile.temperature >> 8) & 0xFF);
  msg.data[6] = (uint8_t)(reptile.humeur & 0xFF);
  msg.data[7] = (uint8_t)((reptile.humeur >> 8) & 0xFF);
  if (can_is_active()) {
    esp_err_t err = can_write_Byte(msg);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "CAN write failed: %s", esp_err_to_name(err));
    }
  }

  if (reptile.faim <= REPTILE_FAMINE_THRESHOLD) {
    start_warning_anim(bar_faim);
  }
  if (reptile.eau <= REPTILE_EAU_THRESHOLD) {
    start_warning_anim(bar_eau);
  }
  if (reptile.temperature <= REPTILE_TEMP_THRESHOLD_LOW ||
      reptile.temperature >= REPTILE_TEMP_THRESHOLD_HIGH) {
    start_warning_anim(bar_temp);
  }
}

static void stats_btn_event_cb(lv_event_t *e) {
  (void)e;
  if (lvgl_port_lock(-1)) {
    lv_scr_load(screen_stats);
    lvgl_port_unlock();
  }
}

static void back_btn_event_cb(lv_event_t *e) {
  (void)e;
  if (lvgl_port_lock(-1)) {
    lv_scr_load(screen_main);
    lvgl_port_unlock();
  }
}

static void action_btn_event_cb(lv_event_t *e) {
  action_type_t action = (action_type_t)(uintptr_t)lv_event_get_user_data(e);
  if (lvgl_port_lock(-1)) {
    switch (action) {
    case ACTION_FEED:
      reptile_feed(&reptile);
      break;
    case ACTION_WATER:
      reptile_give_water(&reptile);
      break;
    case ACTION_HEAT:
      reptile_heat(&reptile);
      break;
    case ACTION_SOOTHE:
      reptile_soothe(&reptile);
      soothe_time_ms = 5000;
      break;
    }
    show_action_sprite(action);
    ui_update_main();
    ui_update_stats();
    lvgl_port_unlock();
  }
}

static void sleep_btn_event_cb(lv_event_t *e) {
  (void)e;
  bool enabled = sleep_is_enabled();
  sleep_set_enabled(!enabled);
  lv_label_set_text(lbl_sleep, sleep_is_enabled() ? "Veille ON" : "Veille OFF");
}

void reptile_game_stop(void) {
  s_game_active = false;
  logging_pause();
  sleep_set_enabled(false);
  soothe_time_ms = 0;
  if (life_timer) {
    lv_timer_del(life_timer);
    life_timer = NULL;
  }
  if (action_timer) {
    lv_timer_del(action_timer);
    action_timer = NULL;
  }
  if (screen_main) {
    lv_obj_del(screen_main);
    screen_main = NULL;
  }
  if (screen_stats) {
    lv_obj_del(screen_stats);
    screen_stats = NULL;
  }
  lv_style_reset(&style_font24);
  update_ms_accum = 0;
  soothe_ms_accum = 0;
}

static void menu_btn_event_cb(lv_event_t *e) {
  (void)e;
  if (lvgl_port_lock(-1)) {
    reptile_game_stop();
    lv_scr_load(menu_screen);
    lvgl_port_unlock();
  }
}

static void ui_update_main(void) {
  lv_bar_set_value(bar_faim, reptile.faim, LV_ANIM_ON);
  lv_bar_set_value(bar_eau, reptile.eau, LV_ANIM_ON);
  lv_bar_set_value(bar_humeur, reptile.humeur, LV_ANIM_ON);
  set_bar_color(bar_faim, reptile.faim, 100);
  set_bar_color(bar_eau, reptile.eau, 100);
  set_bar_color(bar_humeur, reptile.humeur, 100);
  lv_bar_set_value(bar_temp, reptile.temperature, LV_ANIM_ON);
  lv_bar_set_value(bar_humidite, reptile.humidite, LV_ANIM_ON);
  set_bar_color(bar_temp, reptile.temperature, 50);
  set_bar_color(bar_humidite, reptile.humidite, 100);
  update_sprite();
}

static void ui_update_stats(void) {
  lv_label_set_text_fmt(label_stat_faim, "Faim: %" PRIu32, reptile.faim);
  lv_label_set_text_fmt(label_stat_eau, "Eau: %" PRIu32, reptile.eau);
  lv_label_set_text_fmt(label_stat_temp, "Température: %" PRIu32,
                        reptile.temperature);
  lv_label_set_text_fmt(label_stat_humidite, "Humidité: %" PRIu32,
                        reptile.humidite);
  lv_label_set_text_fmt(label_stat_humeur, "Humeur: %" PRIu32, reptile.humeur);
}

void reptile_game_start(esp_lcd_panel_handle_t panel,
                        esp_lcd_touch_handle_t touch) {
  (void)panel;
  (void)touch;
  s_game_active = true;
  lv_style_init(&style_font24);
  lv_style_set_text_font(&style_font24, &lv_font_montserrat_24);

  screen_main = lv_obj_create(NULL);
  screen_stats = lv_obj_create(NULL);

  /* Reptile sprite */
  img_reptile = lv_img_create(screen_main);
  lv_img_set_src(img_reptile, sprite_idle);
  lv_obj_align(img_reptile, LV_ALIGN_TOP_MID, 0, 0);

  /* Hunger bar */
  bar_faim = lv_bar_create(screen_main);
  lv_bar_set_range(bar_faim, 0, 100);
  lv_obj_set_size(bar_faim, 200, 20);
  lv_obj_align(bar_faim, LV_ALIGN_LEFT_MID, 10, -40);
  lv_bar_set_value(bar_faim, 100, LV_ANIM_OFF);
  lv_obj_t *label_faim = lv_label_create(screen_main);
  lv_obj_add_style(label_faim, &style_font24, 0);
  lv_label_set_text(label_faim, "Faim");
  lv_obj_align_to(label_faim, bar_faim, LV_ALIGN_OUT_TOP_LEFT, 0, -5);

  /* Water bar */
  bar_eau = lv_bar_create(screen_main);
  lv_bar_set_range(bar_eau, 0, 100);
  lv_obj_set_size(bar_eau, 200, 20);
  lv_obj_align(bar_eau, LV_ALIGN_LEFT_MID, 10, 0);
  lv_bar_set_value(bar_eau, 100, LV_ANIM_OFF);
  lv_obj_t *label_eau = lv_label_create(screen_main);
  lv_obj_add_style(label_eau, &style_font24, 0);
  lv_label_set_text(label_eau, "Eau");
  lv_obj_align_to(label_eau, bar_eau, LV_ALIGN_OUT_TOP_LEFT, 0, -5);

  /* Temperature bar */
  bar_temp = lv_bar_create(screen_main);
  lv_bar_set_range(bar_temp, 0, 50);
  lv_obj_set_size(bar_temp, 200, 20);
  lv_obj_align(bar_temp, LV_ALIGN_LEFT_MID, 10, 40);
  lv_bar_set_value(bar_temp, 30, LV_ANIM_OFF);
  lv_obj_t *label_temp = lv_label_create(screen_main);
  lv_obj_add_style(label_temp, &style_font24, 0);
  lv_label_set_text(label_temp, "Température");
  lv_obj_align_to(label_temp, bar_temp, LV_ALIGN_OUT_TOP_LEFT, 0, -5);

  /* Humidity bar */
  bar_humidite = lv_bar_create(screen_main);
  lv_bar_set_range(bar_humidite, 0, 100);
  lv_obj_set_size(bar_humidite, 200, 20);
  lv_obj_align(bar_humidite, LV_ALIGN_LEFT_MID, 10, 80);
  lv_bar_set_value(bar_humidite, 50, LV_ANIM_OFF);
  lv_obj_t *label_humidite = lv_label_create(screen_main);
  lv_obj_add_style(label_humidite, &style_font24, 0);
  lv_label_set_text(label_humidite, "Humidité");
  lv_obj_align_to(label_humidite, bar_humidite, LV_ALIGN_OUT_TOP_LEFT, 0, -5);

  /* Mood bar */
  bar_humeur = lv_bar_create(screen_main);
  lv_bar_set_range(bar_humeur, 0, 100);
  lv_obj_set_size(bar_humeur, 200, 20);
  lv_obj_align(bar_humeur, LV_ALIGN_LEFT_MID, 10, 120);
  lv_bar_set_value(bar_humeur, 100, LV_ANIM_OFF);
  lv_obj_t *label_humeur = lv_label_create(screen_main);
  lv_obj_add_style(label_humeur, &style_font24, 0);
  lv_label_set_text(label_humeur, "Humeur");
  lv_obj_align_to(label_humeur, bar_humeur, LV_ALIGN_OUT_TOP_LEFT, 0, -5);

  /* Action buttons */
  lv_obj_t *btn_feed = lv_btn_create(screen_main);
  lv_obj_set_size(btn_feed, 120, 40);
  lv_obj_align(btn_feed, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  lv_obj_add_event_cb(btn_feed, action_btn_event_cb, LV_EVENT_CLICKED,
                      (void *)(uintptr_t)ACTION_FEED);
  lv_obj_t *lbl_feed = lv_label_create(btn_feed);
  lv_obj_add_style(lbl_feed, &style_font24, 0);
  lv_label_set_text(lbl_feed, "Nourrir");
  lv_obj_center(lbl_feed);

  lv_obj_t *btn_water = lv_btn_create(screen_main);
  lv_obj_set_size(btn_water, 120, 40);
  lv_obj_align(btn_water, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_add_event_cb(btn_water, action_btn_event_cb, LV_EVENT_CLICKED,
                      (void *)(uintptr_t)ACTION_WATER);
  lv_obj_t *lbl_water = lv_label_create(btn_water);
  lv_obj_add_style(lbl_water, &style_font24, 0);
  lv_label_set_text(lbl_water, "Hydrater");
  lv_obj_center(lbl_water);

  lv_obj_t *btn_heat = lv_btn_create(screen_main);
  lv_obj_set_size(btn_heat, 120, 40);
  lv_obj_align(btn_heat, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
  lv_obj_add_event_cb(btn_heat, action_btn_event_cb, LV_EVENT_CLICKED,
                      (void *)(uintptr_t)ACTION_HEAT);
  lv_obj_t *lbl_heat = lv_label_create(btn_heat);
  lv_obj_add_style(lbl_heat, &style_font24, 0);
  lv_label_set_text(lbl_heat, "Chauffer");
  lv_obj_center(lbl_heat);

  lv_obj_t *btn_soothe = lv_btn_create(screen_main);
  lv_obj_set_size(btn_soothe, 120, 40);
  lv_obj_align(btn_soothe, LV_ALIGN_BOTTOM_RIGHT, -10, -60);
  lv_obj_add_event_cb(btn_soothe, action_btn_event_cb, LV_EVENT_CLICKED,
                      (void *)(uintptr_t)ACTION_SOOTHE);
  lv_obj_t *lbl_soothe = lv_label_create(btn_soothe);
  lv_obj_add_style(lbl_soothe, &style_font24, 0);
  lv_label_set_text(lbl_soothe, "Caresser");
  lv_obj_center(lbl_soothe);

  /* Sleep toggle button */
  lv_obj_t *btn_sleep = lv_btn_create(screen_main);
  lv_obj_set_size(btn_sleep, 160, 40);
  lv_obj_align(btn_sleep, LV_ALIGN_TOP_RIGHT, -10, 60);
  lv_obj_add_event_cb(btn_sleep, sleep_btn_event_cb, LV_EVENT_CLICKED, NULL);
  lbl_sleep = lv_label_create(btn_sleep);
  lv_obj_add_style(lbl_sleep, &style_font24, 0);
  lv_label_set_text(lbl_sleep, sleep_is_enabled() ? "Veille ON" : "Veille OFF");
  lv_obj_center(lbl_sleep);

  /* Stats button */
  lv_obj_t *btn_stats = lv_btn_create(screen_main);
  lv_obj_set_size(btn_stats, 160, 40);
  lv_obj_align(btn_stats, LV_ALIGN_TOP_RIGHT, -10, 10);
  lv_obj_add_event_cb(btn_stats, stats_btn_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_stats = lv_label_create(btn_stats);
  lv_obj_add_style(lbl_stats, &style_font24, 0);
  lv_label_set_text(lbl_stats, "Statistiques");
  lv_obj_center(lbl_stats);

  /* Menu button */
  lv_obj_t *btn_menu = lv_btn_create(screen_main);
  lv_obj_set_size(btn_menu, 160, 40);
  lv_obj_align(btn_menu, LV_ALIGN_TOP_RIGHT, -10, 110);
  lv_obj_add_event_cb(btn_menu, menu_btn_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_menu = lv_label_create(btn_menu);
  lv_obj_add_style(lbl_menu, &style_font24, 0);
  lv_label_set_text(lbl_menu, "Menu");
  lv_obj_center(lbl_menu);

  /* Stats screen content */
  label_stat_faim = lv_label_create(screen_stats);
  lv_obj_add_style(label_stat_faim, &style_font24, 0);
  lv_obj_align(label_stat_faim, LV_ALIGN_TOP_LEFT, 10, 10);

  label_stat_eau = lv_label_create(screen_stats);
  lv_obj_add_style(label_stat_eau, &style_font24, 0);
  lv_obj_align(label_stat_eau, LV_ALIGN_TOP_LEFT, 10, 50);

  label_stat_temp = lv_label_create(screen_stats);
  lv_obj_add_style(label_stat_temp, &style_font24, 0);
  lv_obj_align(label_stat_temp, LV_ALIGN_TOP_LEFT, 10, 90);
  label_stat_humidite = lv_label_create(screen_stats);
  lv_obj_add_style(label_stat_humidite, &style_font24, 0);
  lv_obj_align(label_stat_humidite, LV_ALIGN_TOP_LEFT, 10, 130);

  label_stat_humeur = lv_label_create(screen_stats);
  lv_obj_add_style(label_stat_humeur, &style_font24, 0);
  lv_obj_align(label_stat_humeur, LV_ALIGN_TOP_LEFT, 10, 170);

  lv_obj_t *btn_back = lv_btn_create(screen_stats);
  lv_obj_set_size(btn_back, 160, 40);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_add_event_cb(btn_back, back_btn_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_back = lv_label_create(btn_back);
  lv_obj_add_style(lbl_back, &style_font24, 0);
  lv_label_set_text(lbl_back, "Retour");
  lv_obj_center(lbl_back);

  ui_update_main();
  ui_update_stats();
  life_timer = lv_timer_create(reptile_tick, REPTILE_UPDATE_PERIOD_MS, NULL);

  lv_scr_load(screen_main);
}
