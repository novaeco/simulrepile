#include "reptile_game.h"
#include "can.h"
#include "image.h"
#include "lvgl_port.h"
#include "sleep.h"
#include "logging.h"
#include "game_mode.h"
#include "settings.h"
#include "esp_log.h"
#include "regulations.h"
#include "sd.h"
#include "ui_theme.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/param.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#define TERRARIUM_GRID_SIZE 5U
#define FACILITY_UPDATE_PERIOD_MS 1000U
#define AUTOSAVE_PERIOD_MS 60000U
#define ECONOMY_CHART_POINTS 64U

typedef enum {
  CONFIG_SUBSTRATE = 0,
  CONFIG_HEATING,
  CONFIG_DECOR,
  CONFIG_UV,
  CONFIG_SIZE,
} config_field_t;

typedef enum {
  INVENTORY_ADD_FEED = 0,
  INVENTORY_ADD_WATER,
  INVENTORY_ADD_SUBSTRATE,
  INVENTORY_ADD_UV,
  INVENTORY_ADD_DECOR,
} inventory_action_t;

typedef enum {
  SAVE_ACTION_SAVE = 0,
  SAVE_ACTION_LOAD,
  SAVE_ACTION_RESET_STATS,
} save_action_t;

static reptile_facility_t g_facility;
static char s_active_slot[sizeof(g_facility.slot)] = "slot_a";
static lv_obj_t *screen_simulation_menu;
static lv_obj_t *screen_overview;
static lv_obj_t *screen_detail;
static lv_obj_t *screen_economy;
static lv_obj_t *screen_save;
static lv_obj_t *screen_regulations;
static lv_obj_t *menu_slot_dropdown;
static lv_obj_t *menu_summary_card;
static lv_obj_t *menu_summary_slot_label;
static lv_obj_t *menu_summary_sd_label;
static lv_obj_t *menu_summary_event_label;
static lv_obj_t *menu_summary_event_icon;
static lv_obj_t *terrarium_grid;
static terrarium_card_widgets_t terrarium_cards[REPTILE_MAX_TERRARIUMS];
static lv_obj_t *cash_label;
static lv_obj_t *cash_badge;
static lv_obj_t *cash_bar;
static lv_obj_t *cycle_label;
static lv_obj_t *cycle_arc;
static lv_obj_t *cycle_badge;
static lv_obj_t *stock_label;
static lv_obj_t *stock_bar;
static lv_obj_t *stock_badge;
static lv_obj_t *sensor_badge;
static lv_obj_t *incident_badge;
static lv_obj_t *occupancy_badge;
static lv_obj_t *sleep_switch;
static lv_obj_t *overview_context_overlay;
static lv_obj_t *overview_context_menu;
static lv_obj_t *detail_title;
static lv_obj_t *detail_alert_badge;
static lv_obj_t *detail_status_label;
static lv_obj_t *detail_status_icon;
static lv_obj_t *detail_temp_label;
static lv_obj_t *detail_humidity_label;
static lv_obj_t *detail_growth_label;
static lv_obj_t *detail_uv_label;
static lv_obj_t *detail_satiety_label;
static lv_obj_t *detail_hydration_label;
static lv_obj_t *detail_weight_label;
static lv_obj_t *detail_stage_badge;
static lv_obj_t *detail_pathology_badge;
static lv_obj_t *detail_incident_badge;
static lv_obj_t *dropdown_species;
static lv_obj_t *dropdown_substrate;
static lv_obj_t *dropdown_heating;
static lv_obj_t *dropdown_decor;
static lv_obj_t *dropdown_uv;
static lv_obj_t *dropdown_size;
static lv_obj_t *detail_cert_table;
static lv_obj_t *education_switch_detail;
static lv_obj_t *detail_register_label;
static lv_obj_t *detail_compliance_label;
static lv_obj_t *register_button;
static lv_obj_t *detail_humidity_arc;
static lv_obj_t *detail_growth_bar;
static lv_meter_indicator_t *detail_temp_indicator;
static lv_meter_indicator_t *detail_temp_range_indicator;
static lv_obj_t *detail_temp_meter;
static lv_obj_t *economy_chart;
static lv_chart_series_t *series_income;
static lv_chart_series_t *series_expenses;
static lv_obj_t *economy_table;
static lv_obj_t *economy_summary_label;
static lv_obj_t *save_slot_dropdown;
static lv_obj_t *save_status_label;
static lv_obj_t *regulations_table;
static lv_obj_t *regulations_alert_table;
static lv_obj_t *regulations_summary_label;
static lv_obj_t *regulations_export_label;

static lv_timer_t *facility_timer;
static uint32_t last_tick_ms;
static uint32_t autosave_ms;
static int64_t prev_income_snapshot;
static int64_t prev_expense_snapshot;
static uint32_t selected_terrarium;
static bool s_game_active;

typedef struct {
  lv_obj_t *card;
  lv_obj_t *icon;
  lv_obj_t *title;
  lv_obj_t *name;
  lv_obj_t *stage;
  lv_obj_t *warning;
  lv_obj_t *badge;
} terrarium_card_widgets_t;

typedef struct {
  lv_obj_t *content;
  lv_obj_t *arrow;
} detail_accordion_panel_t;

typedef enum {
  DETAIL_PANEL_SPECIES = 0,
  DETAIL_PANEL_MATERIAL,
  DETAIL_PANEL_COUNT,
} detail_panel_id_t;

static detail_accordion_panel_t detail_accordion_panels[DETAIL_PANEL_COUNT];

typedef enum {
  TERRARIUM_CONTEXT_ACTION_DETAIL = 0x01U,
  TERRARIUM_CONTEXT_ACTION_HISTORY = 0x02U,
  TERRARIUM_CONTEXT_ACTION_CLOSE = 0xFFU,
} terrarium_context_action_t;

static char
    species_options_buffer[REPTILE_SPECIES_COUNT * (REPTILE_NAME_MAX_LEN + 1U)];
static reptile_species_id_t species_option_ids[REPTILE_SPECIES_COUNT];
static uint32_t species_option_count;

static const char *TAG = "reptile_game";

extern lv_obj_t *menu_screen;

static const char *substrate_options =
    "Terreau tropical\nSable désertique\nFibre coco\nTourbe horticole\n"
    "Forest floor";
static const char *heating_options =
    "Câble 25W\nTapis 40W\nLampe céramique 60W\n"
    "Radiant panel";
static const char *decor_options =
    "Branches + cachettes\nFond 3D roche\nPlantes vivantes\n"
    "Empilement d'ardoises";
static const char *uv_options =
    "UVB T5 5%\nUVB T5 10%\nArcadia ProT5 12%\n"
    "LED UVB hybride";
static const char *size_options =
    "90x45x45 cm\n120x60x60 cm\n180x90x60 cm\n200x100x60 cm";

static const char *slot_options = "slot_a\nslot_b\nslot_c\nslot_d";

static void build_simulation_menu_screen(void);
static void build_overview_screen(void);
static void build_detail_screen(void);
static void build_economy_screen(void);
static void build_save_screen(void);
static void build_regulation_screen(void);
static void ensure_game_screens(void);
static void simulation_enter_overview(void);
static void simulation_apply_active_slot(const char *slot);
static void simulation_get_selected_slot(char *slot, size_t len);
static void simulation_sync_slot_dropdowns(void);
static void simulation_set_status(const char *fmt, ...);
static void update_overview_screen(void);
static void update_detail_screen(void);
static void update_economy_screen(void);
static void update_certificate_table(void);
static void update_regulation_screen(void);
static void facility_timer_cb(lv_timer_t *timer);
static void publish_can_frame(void);
static void terrarium_card_event_cb(lv_event_t *e);
static void terrarium_context_button_event_cb(lv_event_t *e);
static void terrarium_context_overlay_event_cb(lv_event_t *e);
static void nav_button_event_cb(lv_event_t *e);
static void species_dropdown_event_cb(lv_event_t *e);
static void config_dropdown_event_cb(lv_event_t *e);
static void add_certificate_event_cb(lv_event_t *e);
static void scan_certificate_event_cb(lv_event_t *e);
static void inventory_button_event_cb(lv_event_t *e);
static void save_slot_event_cb(lv_event_t *e);
static void save_action_event_cb(lv_event_t *e);
static void menu_button_event_cb(lv_event_t *e);
static void simulation_new_game_event_cb(lv_event_t *e);
static void simulation_resume_event_cb(lv_event_t *e);
static void simulation_settings_event_cb(lv_event_t *e);
static void sleep_switch_event_cb(lv_event_t *e);
static lv_obj_t *accordion_panel_create(lv_obj_t *parent, const char *title,
                                        const char *icon, bool expanded,
                                        detail_panel_id_t id);
static void accordion_header_event_cb(lv_event_t *e);
static lv_obj_t *ui_config_dropdown_create(lv_obj_t *list, const char *icon,
                                           const char *title, const char *options,
                                           lv_event_cb_t event_cb, void *user_data,
                                           const char *tooltip);
static ui_theme_badge_kind_t pathology_badge_kind(reptile_pathology_t pathology);
static ui_theme_badge_kind_t incident_badge_kind(reptile_incident_t incident);
static void education_switch_event_cb(lv_event_t *e);
static void register_button_event_cb(lv_event_t *e);
static void export_report_event_cb(lv_event_t *e);
static const char *growth_stage_to_string(reptile_growth_stage_t stage);
static const char *pathology_to_string(reptile_pathology_t pathology);
static const char *incident_to_string(reptile_incident_t incident);
static int find_option_index(const char *options, const char *value);
static void dropdown_select_index(lv_obj_t *dd, uint32_t idx);
static void dropdown_select_none(lv_obj_t *dd);
static void load_dropdown_value(lv_obj_t *dd, const char *options,
                                const char *value);
static void update_chart_series(int64_t income_cents, int64_t expense_cents);
static int find_size_option(float length_cm, float width_cm, float height_cm);
static void populate_species_options(void);
static int find_species_option_index(reptile_species_id_t id);
static void simulation_summary_update_slot(const char *slot);
static void simulation_summary_update_sd(void);
static void simulation_summary_update_event(const char *text);
static bool simulation_message_is_error(const char *text);
static void close_terrarium_context_menu(void);
static void show_terrarium_context_menu(uint32_t index);
static void show_terrarium_history_dialog(uint32_t index);
static void update_terrarium_card(uint32_t index, lv_coord_t card_width);
static lv_coord_t determine_card_width(uint32_t terrarium_count);
static float compute_stock_health(const reptile_inventory_t *inventory,
                                  uint32_t terrarium_count,
                                  const char **lowest_label);

bool reptile_game_is_active(void) { return s_game_active; }

void reptile_game_init(void) {
  if (g_facility.slot[0] != '\0') {
    snprintf(s_active_slot, sizeof(s_active_slot), "%s", g_facility.slot);
  }
  game_mode_set(GAME_MODE_SIMULATION);
  reptile_facility_init(&g_facility, true, s_active_slot, game_mode_get());
  snprintf(s_active_slot, sizeof(s_active_slot), "%s", g_facility.slot);
  selected_terrarium = 0;
  last_tick_ms = 0;
  autosave_ms = 0;
  prev_income_snapshot = g_facility.economy.daily_income_cents;
  prev_expense_snapshot = g_facility.economy.daily_expenses_cents;
}

const reptile_facility_t *reptile_get_state(void) { return &g_facility; }

static void simulation_summary_update_slot(const char *slot) {
  if (!menu_summary_slot_label)
    return;
  const char *active = (slot && slot[0] != '\0') ? slot : "--";
  lv_label_set_text_fmt(menu_summary_slot_label, "Slot actif : %s", active);
}

static void simulation_summary_update_sd(void) {
  if (!menu_summary_sd_label)
    return;
  bool mounted = sd_is_mounted();
  const char *text = mounted ? "microSD prête" : "microSD indisponible";
  lv_color_t color = mounted ? lv_color_hex(0x2F4F43)
                             : lv_color_hex(0xB54B3A);
  lv_label_set_text(menu_summary_sd_label, text);
  lv_obj_set_style_text_color(menu_summary_sd_label, color, 0);
}

static bool simulation_message_is_error(const char *text) {
  if (!text)
    return false;
  static const char *keywords[] = {"échoué",   "échec",    "impossible",
                                   "Erreur",  "erreur",   "Échec",
                                   "Échoué",  "critique", NULL};
  for (const char **kw = keywords; *kw; ++kw) {
    if (strstr(text, *kw)) {
      return true;
    }
  }
  return false;
}

static void simulation_summary_update_event(const char *text) {
  if (!menu_summary_event_label || !text)
    return;
  bool is_error = simulation_message_is_error(text);
  lv_color_t color = is_error ? lv_color_hex(0xB54B3A)
                              : lv_color_hex(0x2F4F43);
  lv_label_set_text(menu_summary_event_label, text);
  lv_obj_set_style_text_color(menu_summary_event_label, color, 0);
  if (menu_summary_event_icon) {
    lv_label_set_text(menu_summary_event_icon,
                      is_error ? LV_SYMBOL_WARNING : LV_SYMBOL_OK);
    lv_obj_set_style_text_color(menu_summary_event_icon,
                                is_error ? lv_color_hex(0xB54B3A)
                                         : lv_color_hex(0x3A7D60),
                                0);
  }
}

static void simulation_set_status(const char *fmt, ...) {
  if (!fmt)
    return;
  char buffer[160];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  simulation_summary_update_event(buffer);
  simulation_summary_update_sd();
}

static void simulation_apply_active_slot(const char *slot) {
  const char *effective = (slot && slot[0] != '\0') ? slot : "slot_a";
  snprintf(g_facility.slot, sizeof(g_facility.slot), "%s", effective);
  snprintf(s_active_slot, sizeof(s_active_slot), "%s", g_facility.slot);
  simulation_sync_slot_dropdowns();
  simulation_summary_update_slot(g_facility.slot);
}

static void simulation_get_selected_slot(char *slot, size_t len) {
  if (!slot || len == 0)
    return;
  if (menu_slot_dropdown) {
    lv_dropdown_get_selected_str(menu_slot_dropdown, slot, len);
  } else {
    slot[0] = '\0';
  }
  if (slot[0] == '\0') {
    const char *fallback = (s_active_slot[0] != '\0') ? s_active_slot : "slot_a";
    snprintf(slot, len, "%s", fallback);
  }
}

static void simulation_sync_slot_dropdowns(void) {
  if (menu_slot_dropdown) {
    lv_dropdown_set_options(menu_slot_dropdown, slot_options);
    load_dropdown_value(menu_slot_dropdown, slot_options, g_facility.slot);
  }
  if (save_slot_dropdown) {
    lv_dropdown_set_options(save_slot_dropdown, slot_options);
    load_dropdown_value(save_slot_dropdown, slot_options, g_facility.slot);
  }
  simulation_summary_update_slot(g_facility.slot);
}

static void ensure_game_screens(void) {
  if (!screen_detail) {
    build_detail_screen();
  }
  if (!screen_economy) {
    build_economy_screen();
  }
  if (!screen_save) {
    build_save_screen();
  }
  if (!screen_regulations) {
    build_regulation_screen();
  }
  if (!screen_overview) {
    build_overview_screen();
  }
}

static void simulation_enter_overview(void) {
  ensure_game_screens();
  simulation_sync_slot_dropdowns();
  if (save_status_label) {
    lv_label_set_text_fmt(save_status_label, "Slot actif: %s", g_facility.slot);
  }
  update_overview_screen();
  update_detail_screen();
  update_economy_screen();
  update_regulation_screen();
  lv_scr_load(screen_overview);
}

static void build_simulation_menu_screen(void) {
  screen_simulation_menu = lv_obj_create(NULL);
  ui_theme_apply_screen(screen_simulation_menu);
  lv_obj_set_style_pad_all(screen_simulation_menu, 24, 0);

  lv_obj_t *title = lv_label_create(screen_simulation_menu);
  ui_theme_apply_title(title);
  lv_label_set_text(title, "Simulation reptiles");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

  lv_obj_t *card = ui_theme_create_card(screen_simulation_menu);
  lv_obj_set_width(card, 380);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 20);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(card, 18, 0);

  lv_obj_t *slot_label = lv_label_create(card);
  ui_theme_apply_body(slot_label);
  lv_label_set_text(slot_label, "Slot de sauvegarde");
  lv_obj_set_width(slot_label, LV_PCT(100));

  menu_slot_dropdown = lv_dropdown_create(card);
  lv_dropdown_set_options(menu_slot_dropdown, slot_options);
  ui_theme_apply_dropdown(menu_slot_dropdown);
  lv_obj_set_width(menu_slot_dropdown, LV_PCT(100));

  lv_obj_t *btn_new = ui_theme_create_button(
      card, "Nouvelle partie", UI_THEME_BUTTON_PRIMARY,
      simulation_new_game_event_cb, NULL);
  lv_obj_set_width(btn_new, LV_PCT(100));

  lv_obj_t *btn_resume = ui_theme_create_button(
      card, "Reprendre", UI_THEME_BUTTON_PRIMARY, simulation_resume_event_cb,
      NULL);
  lv_obj_set_width(btn_resume, LV_PCT(100));

  lv_obj_t *btn_settings = ui_theme_create_button(
      card, "Paramètres", UI_THEME_BUTTON_SECONDARY,
      simulation_settings_event_cb, NULL);
  lv_obj_set_width(btn_settings, LV_PCT(100));

  lv_obj_t *btn_main_menu = ui_theme_create_button(
      screen_simulation_menu, "Menu principal", UI_THEME_BUTTON_SECONDARY,
      menu_button_event_cb, NULL);
  lv_obj_set_width(btn_main_menu, 220);
  lv_obj_align(btn_main_menu, LV_ALIGN_BOTTOM_LEFT, 24, -20);

  menu_summary_card = ui_theme_create_card(screen_simulation_menu);
  lv_obj_set_width(menu_summary_card, 360);
  lv_obj_align(menu_summary_card, LV_ALIGN_BOTTOM_RIGHT, -24, -20);
  lv_obj_set_flex_flow(menu_summary_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(menu_summary_card, 12, 0);

  lv_obj_t *summary_title = lv_label_create(menu_summary_card);
  ui_theme_apply_title(summary_title);
  lv_label_set_text(summary_title, "Résumé session");

  lv_obj_t *slot_row = lv_obj_create(menu_summary_card);
  lv_obj_remove_style_all(slot_row);
  lv_obj_set_flex_flow(slot_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(slot_row, 10, 0);
  lv_obj_set_scrollbar_mode(slot_row, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t *slot_icon = lv_label_create(slot_row);
  ui_theme_apply_caption(slot_icon);
  lv_label_set_text(slot_icon, LV_SYMBOL_SAVE);

  menu_summary_slot_label = lv_label_create(slot_row);
  ui_theme_apply_body(menu_summary_slot_label);
  lv_obj_set_width(menu_summary_slot_label, LV_PCT(100));
  lv_label_set_long_mode(menu_summary_slot_label, LV_LABEL_LONG_WRAP);

  lv_obj_t *sd_row = lv_obj_create(menu_summary_card);
  lv_obj_remove_style_all(sd_row);
  lv_obj_set_flex_flow(sd_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(sd_row, 10, 0);
  lv_obj_set_scrollbar_mode(sd_row, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t *sd_icon = lv_label_create(sd_row);
  ui_theme_apply_caption(sd_icon);
  lv_label_set_text(sd_icon, LV_SYMBOL_SD_CARD);

  menu_summary_sd_label = lv_label_create(sd_row);
  ui_theme_apply_body(menu_summary_sd_label);
  lv_obj_set_width(menu_summary_sd_label, LV_PCT(100));
  lv_label_set_long_mode(menu_summary_sd_label, LV_LABEL_LONG_WRAP);

  lv_obj_t *event_row = lv_obj_create(menu_summary_card);
  lv_obj_remove_style_all(event_row);
  lv_obj_set_flex_flow(event_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(event_row, 10, 0);
  lv_obj_set_scrollbar_mode(event_row, LV_SCROLLBAR_MODE_OFF);

  menu_summary_event_icon = lv_label_create(event_row);
  ui_theme_apply_caption(menu_summary_event_icon);
  lv_label_set_text(menu_summary_event_icon, LV_SYMBOL_OK);

  menu_summary_event_label = lv_label_create(event_row);
  ui_theme_apply_body(menu_summary_event_label);
  lv_obj_set_width(menu_summary_event_label, LV_PCT(100));
  lv_label_set_long_mode(menu_summary_event_label, LV_LABEL_LONG_WRAP);

  simulation_sync_slot_dropdowns();
  simulation_summary_update_slot(g_facility.slot);
  simulation_summary_update_sd();
  simulation_set_status("Slot actif: %s", g_facility.slot);
}

static void build_overview_screen(void) {
  screen_overview = lv_obj_create(NULL);
  ui_theme_apply_screen(screen_overview);

  lv_obj_t *grid_card = ui_theme_create_card(screen_overview);
  lv_obj_set_size(grid_card, 660, 420);
  lv_obj_align(grid_card, LV_ALIGN_TOP_LEFT, 16, 16);
  lv_obj_set_flex_flow(grid_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(grid_card, 12, 0);

  lv_obj_t *grid_title = lv_label_create(grid_card);
  ui_theme_apply_title(grid_title);
  lv_label_set_text(grid_title, "Terrariums");
  lv_obj_set_width(grid_title, LV_PCT(100));

  terrarium_grid = lv_obj_create(grid_card);
  lv_obj_remove_style_all(terrarium_grid);
  lv_obj_set_size(terrarium_grid, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(terrarium_grid, 0, 0);
  lv_obj_set_style_pad_gap(terrarium_grid, 16, 0);
  lv_obj_set_style_bg_opa(terrarium_grid, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(terrarium_grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(terrarium_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_STRETCH);
  lv_obj_set_scroll_dir(terrarium_grid, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(terrarium_grid, LV_SCROLLBAR_MODE_AUTO);

  for (uint32_t i = 0; i < REPTILE_MAX_TERRARIUMS; ++i) {
    terrarium_card_widgets_t *widgets = &terrarium_cards[i];
    widgets->card = ui_theme_create_card(terrarium_grid);
    lv_obj_set_style_pad_all(widgets->card, 16, 0);
    lv_obj_set_style_pad_gap(widgets->card, 10, 0);
    lv_obj_set_style_min_width(widgets->card, 200, 0);
    lv_obj_set_style_max_width(widgets->card, 240, 0);
    lv_obj_set_style_min_height(widgets->card, 150, 0);
    lv_obj_set_flex_flow(widgets->card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(widgets->card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(widgets->card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(widgets->card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(widgets->card, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(widgets->card, terrarium_card_event_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)i);
    lv_obj_add_event_cb(widgets->card, terrarium_card_event_cb,
                        LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)i);

    lv_obj_t *header = lv_obj_create(widgets->card);
    lv_obj_remove_style_all(header);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_pad_gap(header, 12, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);

    widgets->icon = lv_img_create(header);
    lv_img_set_src(widgets->icon, ui_theme_get_icon(UI_THEME_ICON_TERRARIUM_OK));

    widgets->title = lv_label_create(header);
    ui_theme_apply_body(widgets->title);
    lv_obj_set_flex_grow(widgets->title, 1);
    lv_label_set_text(widgets->title, "T00");

    widgets->badge = ui_theme_create_badge(header, UI_THEME_BADGE_INFO, "Libre");
    lv_obj_set_style_align_self(widgets->badge, LV_ALIGN_END, 0);

    widgets->name = lv_label_create(widgets->card);
    ui_theme_apply_title(widgets->name);
    lv_obj_set_width(widgets->name, LV_PCT(100));
    lv_label_set_long_mode(widgets->name, LV_LABEL_LONG_WRAP);
    lv_label_set_text(widgets->name, "Disponible");

    widgets->stage = lv_label_create(widgets->card);
    ui_theme_apply_body(widgets->stage);
    lv_obj_set_width(widgets->stage, LV_PCT(100));
    lv_label_set_long_mode(widgets->stage, LV_LABEL_LONG_WRAP);
    lv_label_set_text(widgets->stage, "Aucun occupant");

    widgets->warning = lv_label_create(widgets->card);
    ui_theme_apply_caption(widgets->warning);
    lv_obj_set_width(widgets->warning, LV_PCT(100));
    lv_label_set_long_mode(widgets->warning, LV_LABEL_LONG_WRAP);
    lv_label_set_text(widgets->warning, "Touchez pour configurer");
  }

  lv_obj_t *metrics_card = ui_theme_create_card(screen_overview);
  lv_obj_set_size(metrics_card, 320, 420);
  lv_obj_align(metrics_card, LV_ALIGN_TOP_RIGHT, -16, 16);
  lv_obj_set_flex_flow(metrics_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(metrics_card, 16, 0);

  lv_obj_t *metrics_title = lv_label_create(metrics_card);
  ui_theme_apply_title(metrics_title);
  lv_label_set_text(metrics_title, "Indicateurs");
  lv_obj_set_width(metrics_title, LV_PCT(100));

  cash_badge = ui_theme_create_badge(metrics_card, UI_THEME_BADGE_INFO, "0 €");
  lv_obj_set_style_align_self(cash_badge, LV_ALIGN_START, 0);

  cash_bar = lv_bar_create(metrics_card);
  lv_bar_set_range(cash_bar, -100, 100);
  lv_bar_set_value(cash_bar, 0, LV_ANIM_OFF);
  lv_obj_set_width(cash_bar, LV_PCT(100));
  lv_obj_clear_flag(cash_bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(cash_bar, lv_color_hex(0xE1F2E1), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(cash_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(cash_bar, lv_color_hex(0x3A7D60),
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(cash_bar, LV_OPA_COVER, LV_PART_INDICATOR);

  cash_label = lv_label_create(metrics_card);
  ui_theme_apply_body(cash_label);
  lv_obj_set_width(cash_label, LV_PCT(100));
  lv_label_set_long_mode(cash_label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(cash_label, "Trésorerie stabilisée");

  lv_obj_t *cycle_row = lv_obj_create(metrics_card);
  lv_obj_remove_style_all(cycle_row);
  lv_obj_set_width(cycle_row, LV_PCT(100));
  lv_obj_set_flex_flow(cycle_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(cycle_row, 16, 0);
  lv_obj_set_style_bg_opa(cycle_row, LV_OPA_TRANSP, 0);

  cycle_arc = lv_arc_create(cycle_row);
  lv_obj_set_size(cycle_arc, 110, 110);
  lv_arc_set_bg_angles(cycle_arc, 135, 45);
  lv_arc_set_range(cycle_arc, 0, 1000);
  lv_arc_set_value(cycle_arc, 0);
  lv_arc_set_rotation(cycle_arc, 270);
  lv_obj_clear_flag(cycle_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_color(cycle_arc, lv_color_hex(0xD7EDDE), LV_PART_MAIN);
  lv_obj_set_style_arc_width(cycle_arc, 12, LV_PART_MAIN);
  lv_obj_set_style_arc_color(cycle_arc, lv_color_hex(0x3A7D60),
                             LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(cycle_arc, 14, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(cycle_arc, LV_OPA_TRANSP, LV_PART_KNOB);

  lv_obj_t *cycle_info = lv_obj_create(cycle_row);
  lv_obj_remove_style_all(cycle_info);
  lv_obj_set_flex_flow(cycle_info, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(cycle_info, 8, 0);
  lv_obj_set_style_bg_opa(cycle_info, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_grow(cycle_info, 1);

  cycle_badge = ui_theme_create_badge(cycle_info, UI_THEME_BADGE_INFO, "Cycle");
  lv_obj_set_style_align_self(cycle_badge, LV_ALIGN_START, 0);

  cycle_label = lv_label_create(cycle_info);
  ui_theme_apply_body(cycle_label);
  lv_obj_set_width(cycle_label, LV_PCT(100));
  lv_label_set_long_mode(cycle_label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(cycle_label, "Nuit 00:00");

  stock_badge = ui_theme_create_badge(metrics_card, UI_THEME_BADGE_INFO,
                                      "Stocks stabilisés");
  lv_obj_set_style_align_self(stock_badge, LV_ALIGN_START, 0);

  stock_bar = lv_bar_create(metrics_card);
  lv_bar_set_range(stock_bar, 0, 1000);
  lv_bar_set_value(stock_bar, 0, LV_ANIM_OFF);
  lv_obj_set_width(stock_bar, LV_PCT(100));
  lv_obj_clear_flag(stock_bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(stock_bar, lv_color_hex(0xEDF7ED), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(stock_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(stock_bar, lv_color_hex(0x7CB38B),
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(stock_bar, LV_OPA_COVER, LV_PART_INDICATOR);

  stock_label = lv_label_create(metrics_card);
  ui_theme_apply_body(stock_label);
  lv_obj_set_width(stock_label, LV_PCT(100));
  lv_label_set_long_mode(stock_label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(stock_label, "Stocks non évalués");

  lv_obj_t *status_row = lv_obj_create(metrics_card);
  lv_obj_remove_style_all(status_row);
  lv_obj_set_width(status_row, LV_PCT(100));
  lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(status_row, 10, 0);
  lv_obj_set_style_bg_opa(status_row, LV_OPA_TRANSP, 0);

  sensor_badge =
      ui_theme_create_badge(status_row, UI_THEME_BADGE_INFO, "Capteurs");
  incident_badge =
      ui_theme_create_badge(status_row, UI_THEME_BADGE_INFO, "Incidents 0");
  occupancy_badge = ui_theme_create_badge(status_row, UI_THEME_BADGE_INFO,
                                          "Occupés 0/0");

  sleep_switch = lv_switch_create(metrics_card);
  lv_obj_set_style_align_self(sleep_switch, LV_ALIGN_END, 0);
  if (sleep_is_enabled()) {
    lv_obj_add_state(sleep_switch, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(sleep_switch, sleep_switch_event_cb, LV_EVENT_VALUE_CHANGED,
                      NULL);
  lv_obj_t *lbl_sleep = lv_label_create(metrics_card);
  ui_theme_apply_caption(lbl_sleep);
  lv_label_set_text(lbl_sleep, "Veille automatique");

  lv_obj_t *btn_detail = ui_theme_create_button(
      screen_overview, "Détails terrarium", UI_THEME_BUTTON_PRIMARY,
      nav_button_event_cb, screen_detail);
  lv_obj_set_width(btn_detail, 200);
  lv_obj_align(btn_detail, LV_ALIGN_BOTTOM_LEFT, 16, -18);

  lv_obj_t *btn_economy = ui_theme_create_button(
      screen_overview, "Économie", UI_THEME_BUTTON_SECONDARY,
      nav_button_event_cb, screen_economy);
  lv_obj_set_width(btn_economy, 180);
  lv_obj_align(btn_economy, LV_ALIGN_BOTTOM_MID, 0, -18);

  lv_obj_t *btn_save = ui_theme_create_button(screen_overview, "Sauvegardes",
                                              UI_THEME_BUTTON_SECONDARY,
                                              nav_button_event_cb, screen_save);
  lv_obj_set_width(btn_save, 180);
  lv_obj_align(btn_save, LV_ALIGN_BOTTOM_RIGHT, -220, -18);

  lv_obj_t *btn_reg = ui_theme_create_button(
      screen_overview, "Obligations", UI_THEME_BUTTON_SECONDARY,
      nav_button_event_cb, screen_regulations);
  lv_obj_set_width(btn_reg, 180);
  lv_obj_align(btn_reg, LV_ALIGN_BOTTOM_RIGHT, -410, -18);

  lv_obj_t *btn_menu = ui_theme_create_button(
      screen_overview, "Menu Simulation", UI_THEME_BUTTON_SECONDARY,
      nav_button_event_cb, screen_simulation_menu);
  lv_obj_set_width(btn_menu, 200);
  lv_obj_align(btn_menu, LV_ALIGN_BOTTOM_RIGHT, -16, -18);
}

static ui_theme_badge_kind_t pathology_badge_kind(reptile_pathology_t pathology) {
  switch (pathology) {
  case REPTILE_PATHOLOGY_NONE:
    return UI_THEME_BADGE_SUCCESS;
  case REPTILE_PATHOLOGY_RESPIRATORY:
  case REPTILE_PATHOLOGY_PARASITIC:
    return UI_THEME_BADGE_WARNING;
  case REPTILE_PATHOLOGY_METABOLIC:
    return UI_THEME_BADGE_CRITICAL;
  default:
    return UI_THEME_BADGE_INFO;
  }
}

static ui_theme_badge_kind_t incident_badge_kind(reptile_incident_t incident) {
  switch (incident) {
  case REPTILE_INCIDENT_NONE:
    return UI_THEME_BADGE_SUCCESS;
  case REPTILE_INCIDENT_CERTIFICATE_EXPIRED:
  case REPTILE_INCIDENT_DIMENSION_NON_CONFORM:
  case REPTILE_INCIDENT_AUDIT_LOCK:
    return UI_THEME_BADGE_CRITICAL;
  case REPTILE_INCIDENT_ENVIRONMENT_OUT_OF_RANGE:
  case REPTILE_INCIDENT_REGISTER_MISSING:
  case REPTILE_INCIDENT_CERTIFICATE_MISSING:
  case REPTILE_INCIDENT_EDUCATION_MISSING:
    return UI_THEME_BADGE_WARNING;
  default:
    return UI_THEME_BADGE_INFO;
  }
}

static void accordion_header_event_cb(lv_event_t *e) {
  detail_accordion_panel_t *panel =
      (detail_accordion_panel_t *)lv_event_get_user_data(e);
  if (!panel)
    return;
  lv_obj_t *header = lv_event_get_target(e);
  bool expanded = lv_obj_has_state(header, LV_STATE_CHECKED);
  if (panel->content) {
    if (expanded) {
      lv_obj_clear_flag(panel->content, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(panel->content, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (panel->arrow) {
    lv_label_set_text(panel->arrow, expanded ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
  }
}

static lv_obj_t *accordion_panel_create(lv_obj_t *parent, const char *title,
                                        const char *icon, bool expanded,
                                        detail_panel_id_t id) {
  if (!parent || id >= DETAIL_PANEL_COUNT)
    return NULL;

  detail_accordion_panel_t *panel = &detail_accordion_panels[id];
  panel->content = NULL;
  panel->arrow = NULL;

  lv_obj_t *container = lv_obj_create(parent);
  lv_obj_remove_style_all(container);
  lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(container, LV_PCT(100));
  lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(container, 0, 0);
  lv_obj_set_style_pad_gap(container, 10, 0);
  lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *header = lv_btn_create(container);
  lv_obj_remove_style_all(header);
  lv_obj_add_flag(header, LV_OBJ_FLAG_CHECKABLE);
  if (expanded) {
    lv_obj_add_state(header, LV_STATE_CHECKED);
  }
  lv_obj_set_width(header, LV_PCT(100));
  lv_obj_set_style_bg_color(header, lv_color_hex(0xE4F2E8), 0);
  lv_obj_set_style_bg_grad_color(header, lv_color_hex(0xD1E7DA), 0);
  lv_obj_set_style_bg_grad_dir(header, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(header, 14, 0);
  lv_obj_set_style_border_width(header, 1, 0);
  lv_obj_set_style_border_color(header, lv_color_hex(0xB7D3C2), 0);
  lv_obj_set_style_pad_hor(header, 18, 0);
  lv_obj_set_style_pad_ver(header, 12, 0);
  lv_obj_set_style_pad_gap(header, 12, 0);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *icon_label = lv_label_create(header);
  ui_theme_apply_body(icon_label);
  lv_label_set_text(icon_label, icon ? icon : LV_SYMBOL_SETTINGS);

  lv_obj_t *title_label = lv_label_create(header);
  ui_theme_apply_body(title_label);
  lv_label_set_text(title_label, title ? title : "");
  lv_obj_set_flex_grow(title_label, 1);

  lv_obj_t *arrow = lv_label_create(header);
  ui_theme_apply_caption(arrow);
  lv_label_set_text(arrow, expanded ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);

  lv_obj_t *content = lv_obj_create(container);
  lv_obj_remove_style_all(content);
  lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(content, LV_PCT(100));
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(content, 0, 0);
  lv_obj_set_style_pad_gap(content, 14, 0);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

  if (!expanded) {
    lv_obj_add_flag(content, LV_OBJ_FLAG_HIDDEN);
  }

  panel->content = content;
  panel->arrow = arrow;
  lv_obj_add_event_cb(header, accordion_header_event_cb, LV_EVENT_VALUE_CHANGED,
                      panel);

  return content;
}

static lv_obj_t *ui_config_dropdown_create(lv_obj_t *list, const char *icon,
                                           const char *title, const char *options,
                                           lv_event_cb_t event_cb, void *user_data,
                                           const char *tooltip) {
  if (!list)
    return NULL;

  lv_obj_t *item = lv_obj_create(list);
  lv_obj_remove_style_all(item);
  lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(item, LV_PCT(100));
  lv_obj_set_style_bg_color(item, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(item, 16, 0);
  lv_obj_set_style_border_width(item, 1, 0);
  lv_obj_set_style_border_color(item, lv_color_hex(0xC7DDCB), 0);
  lv_obj_set_style_shadow_width(item, 8, 0);
  lv_obj_set_style_shadow_ofs_y(item, 2, 0);
  lv_obj_set_style_shadow_color(item, lv_color_hex(0xBBDCCB), 0);
  lv_obj_set_style_pad_all(item, 14, 0);
  lv_obj_set_style_pad_gap(item, 10, 0);
  lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *header = lv_obj_create(item);
  lv_obj_remove_style_all(header);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(header, 0, 0);
  lv_obj_set_style_pad_gap(header, 10, 0);
  lv_obj_set_width(header, LV_PCT(100));
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *icon_label = lv_label_create(header);
  ui_theme_apply_caption(icon_label);
  lv_label_set_text(icon_label, icon ? icon : LV_SYMBOL_SETTINGS);

  lv_obj_t *title_label = lv_label_create(header);
  ui_theme_apply_body(title_label);
  lv_label_set_text(title_label, title ? title : "");
  lv_obj_set_flex_grow(title_label, 1);

  lv_obj_t *dd = lv_dropdown_create(item);
  ui_theme_apply_dropdown(dd);
  lv_obj_set_width(dd, LV_PCT(100));
  if (options) {
    lv_dropdown_set_options(dd, options);
  }
  if (event_cb) {
    lv_obj_add_event_cb(dd, event_cb, LV_EVENT_VALUE_CHANGED, user_data);
  }
  if (tooltip) {
    lv_obj_set_tooltip_text(dd, tooltip);
  }

  return dd;
}

static void build_detail_screen(void) {
  screen_detail = lv_obj_create(NULL);
  ui_theme_apply_screen(screen_detail);
  lv_obj_set_style_pad_all(screen_detail, 16, 0);
  lv_obj_set_style_pad_gap(screen_detail, 16, 0);
  lv_obj_set_flex_flow(screen_detail, LV_FLEX_FLOW_COLUMN);

  detail_title = lv_label_create(screen_detail);
  ui_theme_apply_title(detail_title);
  lv_label_set_text(detail_title, "Terrarium");
  lv_obj_set_width(detail_title, LV_PCT(100));

  lv_obj_t *content = lv_obj_create(screen_detail);
  lv_obj_remove_style_all(content);
  lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(content, LV_PCT(100));
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(content, 0, 0);
  lv_obj_set_style_pad_gap(content, 16, 0);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);

  /* Colonne A : monitoring */
  lv_obj_t *col_monitor = lv_obj_create(content);
  lv_obj_remove_style_all(col_monitor);
  lv_obj_clear_flag(col_monitor, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(col_monitor, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(col_monitor, 0, 0);
  lv_obj_set_style_pad_gap(col_monitor, 16, 0);
  lv_obj_set_flex_flow(col_monitor, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_min_width(col_monitor, 300, 0);
  lv_obj_set_flex_grow(col_monitor, 1);

  lv_obj_t *monitor_card = ui_theme_create_card(col_monitor);
  lv_obj_set_width(monitor_card, LV_PCT(100));
  lv_obj_set_flex_flow(monitor_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(monitor_card, 14, 0);

  detail_alert_badge =
      ui_theme_create_badge(monitor_card, UI_THEME_BADGE_INFO,
                            "Surveillance inactive");
  lv_obj_set_style_align_self(detail_alert_badge, LV_ALIGN_START, 0);

  lv_obj_t *status_row = lv_obj_create(monitor_card);
  lv_obj_remove_style_all(status_row);
  lv_obj_clear_flag(status_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(status_row, 12, 0);
  lv_obj_set_style_bg_opa(status_row, LV_OPA_TRANSP, 0);
  lv_obj_set_width(status_row, LV_PCT(100));

  detail_status_icon = lv_img_create(status_row);
  lv_img_set_src(detail_status_icon,
                 ui_theme_get_icon(UI_THEME_ICON_TERRARIUM_OK));

  detail_status_label = lv_label_create(status_row);
  ui_theme_apply_body(detail_status_label);
  lv_obj_set_flex_grow(detail_status_label, 1);
  lv_label_set_long_mode(detail_status_label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(detail_status_label,
                    "Attribuer une espèce pour configurer ce terrarium");

  lv_obj_t *gauge_row = lv_obj_create(monitor_card);
  lv_obj_remove_style_all(gauge_row);
  lv_obj_clear_flag(gauge_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(gauge_row, LV_PCT(100));
  lv_obj_set_style_bg_opa(gauge_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_gap(gauge_row, 18, 0);
  lv_obj_set_flex_flow(gauge_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(gauge_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);

  lv_obj_t *temp_column = lv_obj_create(gauge_row);
  lv_obj_remove_style_all(temp_column);
  lv_obj_clear_flag(temp_column, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(temp_column, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(temp_column, 0, 0);
  lv_obj_set_style_pad_gap(temp_column, 8, 0);
  lv_obj_set_flex_flow(temp_column, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_min_width(temp_column, 160, 0);

  detail_temp_meter = lv_meter_create(temp_column);
  lv_obj_set_size(detail_temp_meter, 160, 160);
  lv_meter_scale_t *temp_scale = lv_meter_add_scale(detail_temp_meter);
  lv_meter_set_scale_ticks(detail_temp_meter, temp_scale, 51, 5, 10,
                           lv_palette_main(LV_PALETTE_GREY));
  lv_meter_set_scale_major_ticks(detail_temp_meter, temp_scale, 10, 4, 20,
                                 lv_palette_main(LV_PALETTE_BLUE), 12);
  lv_meter_set_scale_range(detail_temp_meter, temp_scale, 0, 500, 270, 135);
  detail_temp_range_indicator =
      lv_meter_add_arc(detail_temp_meter, temp_scale, 12,
                       lv_palette_main(LV_PALETTE_GREEN), 0);
  detail_temp_indicator = lv_meter_add_needle_line(
      detail_temp_meter, temp_scale, 6, lv_palette_main(LV_PALETTE_RED), -10);

  detail_temp_label = lv_label_create(temp_column);
  ui_theme_apply_caption(detail_temp_label);
  lv_label_set_long_mode(detail_temp_label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(detail_temp_label, "Température: --");

  lv_obj_t *humidity_column = lv_obj_create(gauge_row);
  lv_obj_remove_style_all(humidity_column);
  lv_obj_clear_flag(humidity_column, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(humidity_column, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(humidity_column, 0, 0);
  lv_obj_set_style_pad_gap(humidity_column, 8, 0);
  lv_obj_set_flex_flow(humidity_column, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_min_width(humidity_column, 160, 0);

  detail_humidity_arc = lv_arc_create(humidity_column);
  lv_obj_set_size(detail_humidity_arc, 160, 160);
  lv_arc_set_rotation(detail_humidity_arc, 270);
  lv_arc_set_bg_angles(detail_humidity_arc, 135, 45);
  lv_arc_set_range(detail_humidity_arc, 0, 1000);
  lv_arc_set_value(detail_humidity_arc, 0);
  lv_obj_clear_flag(detail_humidity_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(detail_humidity_arc, 14, LV_PART_MAIN);
  lv_obj_set_style_arc_color(detail_humidity_arc, lv_color_hex(0xD7EDDE),
                             LV_PART_MAIN);
  lv_obj_set_style_arc_width(detail_humidity_arc, 18, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(detail_humidity_arc,
                             lv_palette_main(LV_PALETTE_TEAL),
                             LV_PART_INDICATOR);

  detail_humidity_label = lv_label_create(humidity_column);
  ui_theme_apply_caption(detail_humidity_label);
  lv_label_set_long_mode(detail_humidity_label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(detail_humidity_label, "Humidité: --");

  lv_obj_t *growth_column = lv_obj_create(gauge_row);
  lv_obj_remove_style_all(growth_column);
  lv_obj_clear_flag(growth_column, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(growth_column, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(growth_column, 0, 0);
  lv_obj_set_style_pad_gap(growth_column, 10, 0);
  lv_obj_set_flex_flow(growth_column, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_grow(growth_column, 1);
  lv_obj_set_style_min_width(growth_column, 180, 0);

  detail_growth_label = lv_label_create(growth_column);
  ui_theme_apply_caption(detail_growth_label);
  lv_label_set_long_mode(detail_growth_label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(detail_growth_label, "Croissance: --");

  detail_growth_bar = lv_bar_create(growth_column);
  lv_bar_set_range(detail_growth_bar, 0, 1000);
  lv_bar_set_value(detail_growth_bar, 0, LV_ANIM_OFF);
  lv_obj_set_width(detail_growth_bar, LV_PCT(100));
  lv_obj_clear_flag(detail_growth_bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(detail_growth_bar, lv_color_hex(0xEDF7ED),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(detail_growth_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(detail_growth_bar,
                            lv_palette_main(LV_PALETTE_GREEN),
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(detail_growth_bar, LV_OPA_COVER, LV_PART_INDICATOR);

  lv_obj_t *badge_row = lv_obj_create(monitor_card);
  lv_obj_remove_style_all(badge_row);
  lv_obj_clear_flag(badge_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(badge_row, LV_PCT(100));
  lv_obj_set_style_bg_opa(badge_row, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(badge_row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(badge_row, 10, 0);

  detail_stage_badge = ui_theme_create_badge(badge_row, UI_THEME_BADGE_INFO,
                                             "Stade: --");
  detail_pathology_badge = ui_theme_create_badge(badge_row, UI_THEME_BADGE_INFO,
                                                 "Pathologie: --");
  detail_incident_badge = ui_theme_create_badge(badge_row, UI_THEME_BADGE_INFO,
                                                "Incident: --");

  lv_obj_t *metrics_list = lv_obj_create(monitor_card);
  lv_obj_remove_style_all(metrics_list);
  lv_obj_clear_flag(metrics_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(metrics_list, LV_PCT(100));
  lv_obj_set_style_bg_opa(metrics_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(metrics_list, 0, 0);
  lv_obj_set_style_pad_gap(metrics_list, 6, 0);
  lv_obj_set_flex_flow(metrics_list, LV_FLEX_FLOW_COLUMN);

  detail_uv_label = lv_label_create(metrics_list);
  ui_theme_apply_caption(detail_uv_label);
  lv_label_set_long_mode(detail_uv_label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(detail_uv_label, "UV: --");

  detail_satiety_label = lv_label_create(metrics_list);
  ui_theme_apply_caption(detail_satiety_label);
  lv_label_set_text(detail_satiety_label, "Satiété: --");

  detail_hydration_label = lv_label_create(metrics_list);
  ui_theme_apply_caption(detail_hydration_label);
  lv_label_set_text(detail_hydration_label, "Hydratation: --");

  detail_weight_label = lv_label_create(metrics_list);
  ui_theme_apply_caption(detail_weight_label);
  lv_label_set_text(detail_weight_label, "Poids: --");

  /* Colonne B : configuration */
  lv_obj_t *col_config = lv_obj_create(content);
  lv_obj_remove_style_all(col_config);
  lv_obj_clear_flag(col_config, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(col_config, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(col_config, 0, 0);
  lv_obj_set_style_pad_gap(col_config, 16, 0);
  lv_obj_set_flex_flow(col_config, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_min_width(col_config, 320, 0);
  lv_obj_set_flex_grow(col_config, 1);

  lv_obj_t *config_card = ui_theme_create_card(col_config);
  lv_obj_set_width(config_card, LV_PCT(100));
  lv_obj_set_flex_flow(config_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(config_card, 16, 0);

  lv_obj_t *config_title = lv_label_create(config_card);
  ui_theme_apply_body(config_title);
  lv_label_set_text(config_title, "Configuration terrarium");
  lv_obj_set_width(config_title, LV_PCT(100));

  populate_species_options();

  lv_obj_t *species_panel =
      accordion_panel_create(config_card, "Espèce", LV_SYMBOL_LIST, true,
                             DETAIL_PANEL_SPECIES);
  lv_obj_t *species_list = lv_list_create(species_panel);
  lv_obj_set_width(species_list, LV_PCT(100));
  lv_obj_set_style_bg_opa(species_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(species_list, 0, 0);
  lv_obj_set_style_pad_gap(species_list, 12, 0);
  lv_obj_set_scroll_dir(species_list, LV_DIR_VER);

  dropdown_species = ui_config_dropdown_create(
      species_list, LV_SYMBOL_LIST, "Occupant",
      species_options_buffer[0] != '\0' ? species_options_buffer : NULL,
      species_dropdown_event_cb, NULL,
      "Sélectionner l'espèce maintenue");
  lv_dropdown_set_text(dropdown_species, "Choisir espèce");

  lv_obj_t *material_panel = accordion_panel_create(
      config_card, "Matériel & environnement", LV_SYMBOL_SETTINGS, true,
      DETAIL_PANEL_MATERIAL);
  lv_obj_t *material_list = lv_list_create(material_panel);
  lv_obj_set_width(material_list, LV_PCT(100));
  lv_obj_set_style_bg_opa(material_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(material_list, 0, 0);
  lv_obj_set_style_pad_gap(material_list, 12, 0);
  lv_obj_set_scroll_dir(material_list, LV_DIR_VER);

  dropdown_substrate = ui_config_dropdown_create(
      material_list, LV_SYMBOL_HOME, "Substrat", substrate_options,
      config_dropdown_event_cb, (void *)(uintptr_t)CONFIG_SUBSTRATE,
      "Choisir le substrat principal conforme au biotope");

  dropdown_heating = ui_config_dropdown_create(
      material_list, LV_SYMBOL_POWER, "Chauffage", heating_options,
      config_dropdown_event_cb, (void *)(uintptr_t)CONFIG_HEATING,
      "Sélectionner le dispositif de chauffage installé");

  dropdown_decor = ui_config_dropdown_create(
      material_list, LV_SYMBOL_DOWNLOAD, "Décor", decor_options,
      config_dropdown_event_cb, (void *)(uintptr_t)CONFIG_DECOR,
      "Définir les aménagements intérieurs principaux");

  dropdown_uv = ui_config_dropdown_create(
      material_list, LV_SYMBOL_EYE_OPEN, "Éclairage UV", uv_options,
      config_dropdown_event_cb, (void *)(uintptr_t)CONFIG_UV,
      "Type de rampe UVB installée");

  dropdown_size = ui_config_dropdown_create(
      material_list, LV_SYMBOL_DRIVE, "Dimensions", size_options,
      config_dropdown_event_cb, (void *)(uintptr_t)CONFIG_SIZE,
      "Gabarit standard du terrarium (L x l x h)");

  /* Colonne C : conformité et documentation */
  lv_obj_t *col_compliance = lv_obj_create(content);
  lv_obj_remove_style_all(col_compliance);
  lv_obj_clear_flag(col_compliance, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(col_compliance, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(col_compliance, 0, 0);
  lv_obj_set_style_pad_gap(col_compliance, 16, 0);
  lv_obj_set_flex_flow(col_compliance, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_min_width(col_compliance, 320, 0);
  lv_obj_set_flex_grow(col_compliance, 1);

  lv_obj_t *compliance_card = ui_theme_create_card(col_compliance);
  lv_obj_set_width(compliance_card, LV_PCT(100));
  lv_obj_set_flex_flow(compliance_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(compliance_card, 14, 0);

  lv_obj_t *compliance_title = lv_label_create(compliance_card);
  ui_theme_apply_body(compliance_title);
  lv_label_set_text(compliance_title, "Conformité & documentation");
  lv_obj_set_width(compliance_title, LV_PCT(100));

  detail_compliance_label = lv_label_create(compliance_card);
  ui_theme_apply_body(detail_compliance_label);
  lv_label_set_long_mode(detail_compliance_label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(detail_compliance_label,
                    "Aucune conformité requise sans espèce");

  lv_obj_t *register_row = lv_obj_create(compliance_card);
  lv_obj_remove_style_all(register_row);
  lv_obj_clear_flag(register_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(register_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(register_row, 0, 0);
  lv_obj_set_style_pad_gap(register_row, 12, 0);
  lv_obj_set_flex_flow(register_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(register_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  detail_register_label = lv_label_create(register_row);
  ui_theme_apply_body(detail_register_label);
  lv_label_set_text(detail_register_label, "Registre non renseigné");
  lv_obj_set_flex_grow(detail_register_label, 1);

  register_button = ui_theme_create_button(register_row,
                                           "Consigner la cession",
                                           UI_THEME_BUTTON_PRIMARY,
                                           register_button_event_cb, NULL);
  lv_obj_set_width(register_button, 220);

  lv_obj_t *education_row = lv_obj_create(compliance_card);
  lv_obj_remove_style_all(education_row);
  lv_obj_clear_flag(education_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(education_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(education_row, 0, 0);
  lv_obj_set_style_pad_gap(education_row, 12, 0);
  lv_obj_set_flex_flow(education_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(education_row, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *edu_label = lv_label_create(education_row);
  ui_theme_apply_caption(edu_label);
  lv_label_set_text(edu_label, "Affichage pédagogique");

  education_switch_detail = lv_switch_create(education_row);
  lv_obj_add_event_cb(education_switch_detail, education_switch_event_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);

  lv_obj_t *cert_container = lv_obj_create(compliance_card);
  lv_obj_remove_style_all(cert_container);
  lv_obj_set_width(cert_container, LV_PCT(100));
  lv_obj_set_style_bg_color(cert_container, lv_color_hex(0xF6FBF6), 0);
  lv_obj_set_style_bg_opa(cert_container, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(cert_container, 14, 0);
  lv_obj_set_style_border_width(cert_container, 1, 0);
  lv_obj_set_style_border_color(cert_container, lv_color_hex(0xC7DDCB), 0);
  lv_obj_set_style_pad_all(cert_container, 8, 0);
  lv_obj_set_style_pad_gap(cert_container, 8, 0);
  lv_obj_set_scroll_dir(cert_container, LV_DIR_VER);
  lv_obj_set_style_max_height(cert_container, 220, 0);

  detail_cert_table = lv_table_create(cert_container);
  lv_obj_set_width(detail_cert_table, LV_PCT(100));
  lv_table_set_column_count(detail_cert_table, 2);
  lv_table_set_row_count(detail_cert_table, 6);
  ui_theme_apply_table(detail_cert_table, UI_THEME_TABLE_DEFAULT);

  lv_obj_t *cert_action_row = lv_obj_create(compliance_card);
  lv_obj_remove_style_all(cert_action_row);
  lv_obj_clear_flag(cert_action_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(cert_action_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(cert_action_row, 0, 0);
  lv_obj_set_style_pad_gap(cert_action_row, 12, 0);
  lv_obj_set_flex_flow(cert_action_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(cert_action_row, LV_FLEX_ALIGN_END,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *btn_add_cert = ui_theme_create_button(
      cert_action_row, "Ajouter certificat", UI_THEME_BUTTON_PRIMARY,
      add_certificate_event_cb, NULL);
  lv_obj_set_width(btn_add_cert, 220);

  lv_obj_t *btn_scan_cert = ui_theme_create_button(
      cert_action_row, "Scanner certificat", UI_THEME_BUTTON_SECONDARY,
      scan_certificate_event_cb, NULL);
  lv_obj_set_width(btn_scan_cert, 220);

  /* Pied de page : navigation + stocks */
  lv_obj_t *footer = lv_obj_create(screen_detail);
  lv_obj_remove_style_all(footer);
  lv_obj_set_width(footer, LV_PCT(100));
  lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(footer, 0, 0);
  lv_obj_set_style_pad_gap(footer, 12, 0);
  lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *nav_group = lv_obj_create(footer);
  lv_obj_remove_style_all(nav_group);
  lv_obj_set_style_bg_opa(nav_group, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(nav_group, 0, 0);
  lv_obj_set_style_pad_gap(nav_group, 12, 0);
  lv_obj_set_flex_flow(nav_group, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(nav_group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *btn_back = ui_theme_create_button(nav_group, "Retour",
                                              UI_THEME_BUTTON_SECONDARY,
                                              nav_button_event_cb,
                                              screen_overview);
  lv_obj_set_width(btn_back, 180);

  lv_obj_t *btn_menu = ui_theme_create_button(
      nav_group, "Menu Simulation", UI_THEME_BUTTON_SECONDARY,
      nav_button_event_cb, screen_simulation_menu);
  lv_obj_set_width(btn_menu, 200);

  lv_obj_t *inventory_group = lv_obj_create(footer);
  lv_obj_remove_style_all(inventory_group);
  lv_obj_set_style_bg_opa(inventory_group, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(inventory_group, 0, 0);
  lv_obj_set_style_pad_gap(inventory_group, 10, 0);
  lv_obj_set_flex_flow(inventory_group, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(inventory_group, LV_FLEX_ALIGN_END,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_flex_grow(inventory_group, 1);

  lv_obj_t *btn_feed_stock = ui_theme_create_button(
      inventory_group, "+10 proies", UI_THEME_BUTTON_SECONDARY,
      inventory_button_event_cb, (void *)(uintptr_t)INVENTORY_ADD_FEED);
  lv_obj_set_width(btn_feed_stock, 180);

  lv_obj_t *btn_water_stock = ui_theme_create_button(
      inventory_group, "+20 L eau", UI_THEME_BUTTON_SECONDARY,
      inventory_button_event_cb, (void *)(uintptr_t)INVENTORY_ADD_WATER);
  lv_obj_set_width(btn_water_stock, 180);

  lv_obj_t *btn_substrate_stock = ui_theme_create_button(
      inventory_group, "+2 substrats", UI_THEME_BUTTON_SECONDARY,
      inventory_button_event_cb,
      (void *)(uintptr_t)INVENTORY_ADD_SUBSTRATE);
  lv_obj_set_width(btn_substrate_stock, 180);

  lv_obj_t *btn_uv_stock = ui_theme_create_button(
      inventory_group, "+1 UV", UI_THEME_BUTTON_SECONDARY,
      inventory_button_event_cb, (void *)(uintptr_t)INVENTORY_ADD_UV);
  lv_obj_set_width(btn_uv_stock, 180);

  lv_obj_t *btn_decor_stock = ui_theme_create_button(
      inventory_group, "+1 décor", UI_THEME_BUTTON_SECONDARY,
      inventory_button_event_cb, (void *)(uintptr_t)INVENTORY_ADD_DECOR);
  lv_obj_set_width(btn_decor_stock, 180);
}

static void build_economy_screen(void) {
  screen_economy = lv_obj_create(NULL);
  ui_theme_apply_screen(screen_economy);
  lv_obj_set_style_pad_all(screen_economy, 16, 0);

  lv_obj_t *title = lv_label_create(screen_economy);
  ui_theme_apply_title(title);
  lv_label_set_text(title, "Synthèse économique");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 10);

  economy_chart = lv_chart_create(screen_economy);
  lv_obj_set_size(economy_chart, 640, 200);
  lv_obj_align(economy_chart, LV_ALIGN_TOP_LEFT, 10, 60);
  lv_chart_set_point_count(economy_chart, ECONOMY_CHART_POINTS);
  lv_chart_set_type(economy_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_div_line_count(economy_chart, 5, 6);
  lv_chart_set_range(economy_chart, LV_CHART_AXIS_PRIMARY_Y, -200, 600);
  series_income = lv_chart_add_series(economy_chart,
                                      lv_palette_main(LV_PALETTE_GREEN),
                                      LV_CHART_AXIS_PRIMARY_Y);
  series_expenses = lv_chart_add_series(economy_chart,
                                        lv_palette_main(LV_PALETTE_RED),
                                        LV_CHART_AXIS_PRIMARY_Y);
  for (uint32_t i = 0; i < ECONOMY_CHART_POINTS; ++i) {
    lv_chart_set_next_value(economy_chart, series_income, 0);
    lv_chart_set_next_value(economy_chart, series_expenses, 0);
  }

  economy_table = lv_table_create(screen_economy);
  lv_obj_set_size(economy_table, 640, 220);
  lv_obj_align(economy_table, LV_ALIGN_BOTTOM_LEFT, 10, -70);
  lv_table_set_column_count(economy_table, 4);
  lv_table_set_row_count(economy_table, 6);
  ui_theme_apply_table(economy_table, UI_THEME_TABLE_DEFAULT);

  economy_summary_label = lv_label_create(screen_economy);
  ui_theme_apply_body(economy_summary_label);
  lv_obj_align(economy_summary_label, LV_ALIGN_BOTTOM_LEFT, 10, -10);

  lv_obj_t *btn_menu = ui_theme_create_button(
      screen_economy, "Menu Simulation", UI_THEME_BUTTON_SECONDARY,
      nav_button_event_cb, screen_simulation_menu);
  lv_obj_set_width(btn_menu, 200);
  lv_obj_align(btn_menu, LV_ALIGN_BOTTOM_LEFT, 10, -60);

  lv_obj_t *btn_back = ui_theme_create_button(
      screen_economy, "Retour", UI_THEME_BUTTON_SECONDARY, nav_button_event_cb,
      screen_overview);
  lv_obj_set_width(btn_back, 180);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
}

static void build_save_screen(void) {
  screen_save = lv_obj_create(NULL);
  ui_theme_apply_screen(screen_save);
  lv_obj_set_style_pad_all(screen_save, 16, 0);

  lv_obj_t *title = lv_label_create(screen_save);
  ui_theme_apply_title(title);
  lv_label_set_text(title, "Gestion des sauvegardes");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 10);

  save_slot_dropdown = lv_dropdown_create(screen_save);
  lv_dropdown_set_options(save_slot_dropdown, slot_options);
  ui_theme_apply_dropdown(save_slot_dropdown);
  lv_obj_align(save_slot_dropdown, LV_ALIGN_TOP_LEFT, 10, 60);
  lv_obj_add_event_cb(save_slot_dropdown, save_slot_event_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);

  save_status_label = lv_label_create(screen_save);
  ui_theme_apply_body(save_status_label);
  lv_obj_align(save_status_label, LV_ALIGN_TOP_LEFT, 10, 110);

  lv_obj_t *btn_save = ui_theme_create_button(
      screen_save, "Sauvegarder maintenant", UI_THEME_BUTTON_PRIMARY,
      save_action_event_cb, (void *)(uintptr_t)SAVE_ACTION_SAVE);
  lv_obj_set_width(btn_save, 240);
  lv_obj_align(btn_save, LV_ALIGN_TOP_LEFT, 10, 160);

  lv_obj_t *btn_load = ui_theme_create_button(
      screen_save, "Charger le slot", UI_THEME_BUTTON_SECONDARY,
      save_action_event_cb, (void *)(uintptr_t)SAVE_ACTION_LOAD);
  lv_obj_set_width(btn_load, 240);
  lv_obj_align(btn_load, LV_ALIGN_TOP_LEFT, 10, 220);

  lv_obj_t *btn_reset = ui_theme_create_button(
      screen_save, "Réinitialiser les compteurs", UI_THEME_BUTTON_SECONDARY,
      save_action_event_cb, (void *)(uintptr_t)SAVE_ACTION_RESET_STATS);
  lv_obj_set_width(btn_reset, 260);
  lv_obj_align(btn_reset, LV_ALIGN_TOP_LEFT, 10, 280);

  lv_obj_t *btn_menu = ui_theme_create_button(
      screen_save, "Menu Simulation", UI_THEME_BUTTON_SECONDARY,
      nav_button_event_cb, screen_simulation_menu);
  lv_obj_set_width(btn_menu, 200);
  lv_obj_align(btn_menu, LV_ALIGN_BOTTOM_LEFT, 10, -10);

  lv_obj_t *btn_back = ui_theme_create_button(
      screen_save, "Retour", UI_THEME_BUTTON_SECONDARY, nav_button_event_cb,
      screen_overview);
  lv_obj_set_width(btn_back, 180);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
}

static void build_regulation_screen(void) {
  screen_regulations = lv_obj_create(NULL);
  ui_theme_apply_screen(screen_regulations);
  lv_obj_set_style_pad_all(screen_regulations, 16, 0);

  lv_obj_t *title = lv_label_create(screen_regulations);
  ui_theme_apply_title(title);
  lv_label_set_text(title, "Référentiel réglementaire");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 10);

  regulations_table = lv_table_create(screen_regulations);
  lv_obj_set_size(regulations_table, 700, 220);
  lv_obj_align(regulations_table, LV_ALIGN_TOP_LEFT, 10, 60);
  lv_table_set_column_count(regulations_table, 4);
  lv_table_set_row_count(regulations_table, 1);
  ui_theme_apply_table(regulations_table, UI_THEME_TABLE_DEFAULT);

  regulations_alert_table = lv_table_create(screen_regulations);
  lv_obj_set_size(regulations_alert_table, 700, 160);
  lv_obj_align(regulations_alert_table, LV_ALIGN_TOP_LEFT, 10, 300);
  lv_table_set_column_count(regulations_alert_table, 3);
  lv_table_set_row_count(regulations_alert_table, 1);
  ui_theme_apply_table(regulations_alert_table, UI_THEME_TABLE_DEFAULT);

  regulations_summary_label = lv_label_create(screen_regulations);
  ui_theme_apply_body(regulations_summary_label);
  lv_obj_align(regulations_summary_label, LV_ALIGN_BOTTOM_LEFT, 10, -80);

  regulations_export_label = lv_label_create(screen_regulations);
  ui_theme_apply_body(regulations_export_label);
  lv_label_set_text(regulations_export_label, "Aucun export réalisé");
  lv_obj_align(regulations_export_label, LV_ALIGN_BOTTOM_LEFT, 260, -30);

  lv_obj_t *btn_export = ui_theme_create_button(
      screen_regulations, "Exporter rapport microSD",
      UI_THEME_BUTTON_PRIMARY, export_report_event_cb, NULL);
  lv_obj_set_width(btn_export, 260);
  lv_obj_align(btn_export, LV_ALIGN_BOTTOM_LEFT, 10, -30);

  lv_obj_t *btn_menu = ui_theme_create_button(
      screen_regulations, "Menu Simulation", UI_THEME_BUTTON_SECONDARY,
      nav_button_event_cb, screen_simulation_menu);
  lv_obj_set_width(btn_menu, 200);
  lv_obj_align(btn_menu, LV_ALIGN_BOTTOM_LEFT, 10, -10);

  lv_obj_t *btn_back = ui_theme_create_button(
      screen_regulations, "Retour", UI_THEME_BUTTON_SECONDARY,
      nav_button_event_cb, screen_overview);
  lv_obj_set_width(btn_back, 180);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
}

void reptile_game_start(esp_lcd_panel_handle_t panel,
                        esp_lcd_touch_handle_t touch) {
  (void)panel;
  (void)touch;
  s_game_active = true;

  build_simulation_menu_screen();

  facility_timer = lv_timer_create(facility_timer_cb, FACILITY_UPDATE_PERIOD_MS,
                                   NULL);
  last_tick_ms = lv_tick_get();
  autosave_ms = 0;
  prev_income_snapshot = g_facility.economy.daily_income_cents;
  prev_expense_snapshot = g_facility.economy.daily_expenses_cents;

  lv_scr_load(screen_simulation_menu);
}

void reptile_game_stop(void) {
  s_game_active = false;
  sleep_timer_arm(false);
  if (facility_timer) {
    lv_timer_del(facility_timer);
    facility_timer = NULL;
  }
  if (screen_simulation_menu) {
    lv_obj_del(screen_simulation_menu);
    screen_simulation_menu = NULL;
  }
  menu_slot_dropdown = NULL;
  menu_summary_card = NULL;
  menu_summary_slot_label = NULL;
  menu_summary_sd_label = NULL;
  menu_summary_event_label = NULL;
  menu_summary_event_icon = NULL;
  if (screen_overview) {
    lv_obj_del(screen_overview);
    screen_overview = NULL;
  }
  if (screen_detail) {
    lv_obj_del(screen_detail);
    screen_detail = NULL;
  }
  if (screen_economy) {
    lv_obj_del(screen_economy);
    screen_economy = NULL;
  }
  if (screen_save) {
    lv_obj_del(screen_save);
    screen_save = NULL;
  }
  if (screen_regulations) {
    lv_obj_del(screen_regulations);
    screen_regulations = NULL;
  }
}

void reptile_tick(lv_timer_t *timer) { facility_timer_cb(timer); }

static void facility_timer_cb(lv_timer_t *timer) {
  (void)timer;
  uint32_t now = lv_tick_get();
  uint32_t elapsed = (last_tick_ms == 0) ? FACILITY_UPDATE_PERIOD_MS
                                         : (now - last_tick_ms);
  if (elapsed == 0) {
    elapsed = FACILITY_UPDATE_PERIOD_MS;
  }
  last_tick_ms = now;

  reptile_facility_tick(&g_facility, elapsed);
  autosave_ms += elapsed;
  if (autosave_ms >= AUTOSAVE_PERIOD_MS) {
    if (reptile_facility_save(&g_facility) == ESP_OK) {
      autosave_ms = 0;
    }
  }

  update_overview_screen();
  update_detail_screen();
  update_economy_screen();
  update_regulation_screen();
  update_chart_series(g_facility.economy.daily_income_cents,
                      g_facility.economy.daily_expenses_cents);
  publish_can_frame();
}

static void publish_can_frame(void) {
  reptile_facility_metrics_t metrics;
  reptile_facility_compute_metrics(&g_facility, &metrics);

  can_message_t msg = {
      .identifier = 0x101,
      .data_length_code = 8,
      .flags = TWAI_MSG_FLAG_NONE,
  };

  uint16_t avg_growth = (uint16_t)
      (g_facility.average_growth < 0.f
           ? 0
           : (g_facility.average_growth > 1.f
                  ? 100
                  : (uint16_t)lrintf(g_facility.average_growth * 100.0f)));

  int16_t cash_k = (int16_t)MAX(
      MIN(g_facility.economy.cash_cents / 1000, (int64_t)INT16_MAX),
      (int64_t)INT16_MIN);

  msg.data[0] = (uint8_t)(metrics.occupied & 0xFF);
  msg.data[1] = (uint8_t)(g_facility.alerts_active & 0xFF);
  msg.data[2] = (uint8_t)(cash_k & 0xFF);
  msg.data[3] = (uint8_t)((cash_k >> 8) & 0xFF);
  msg.data[4] = (uint8_t)(avg_growth & 0xFF);
  msg.data[5] = (uint8_t)((avg_growth >> 8) & 0xFF);
  msg.data[6] = (uint8_t)(g_facility.pathology_active & 0xFF);
  msg.data[7] = (uint8_t)(g_facility.compliance_alerts & 0xFF);

  if (can_is_active()) {
    esp_err_t err = can_write_Byte(msg);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "CAN write failed: %s", esp_err_to_name(err));
    }
  }
}

static lv_coord_t determine_card_width(uint32_t terrarium_count) {
  if (terrarium_count >= 20U)
    return 150;
  if (terrarium_count >= 12U)
    return 180;
  if (terrarium_count >= 6U)
    return 210;
  return 240;
}

static float compute_stock_health(const reptile_inventory_t *inventory,
                                  uint32_t terrarium_count,
                                  const char **lowest_label) {
  if (lowest_label)
    *lowest_label = "Proies";
  if (!inventory)
    return 0.0f;

  uint32_t base = terrarium_count > 0U ? terrarium_count : 1U;
  float targets[5] = {(float)base * 6.0f, (float)base * 40.0f,
                      (float)base * 2.0f, (float)base * 1.0f,
                      (float)base * 1.0f};
  float values[5] = {(float)inventory->feeders,
                     (float)inventory->water_reserve_l,
                     (float)inventory->substrate_bags,
                     (float)inventory->uv_bulbs,
                     (float)inventory->decor_kits};
  const char *labels[5] = {"Proies", "Eau", "Substrat", "UV", "Décor"};

  float min_ratio = 1.0f;
  for (size_t i = 0; i < 5; ++i) {
    float target = targets[i];
    if (target <= 0.0f)
      target = 1.0f;
    float ratio = values[i] / target;
    if (ratio > 1.0f)
      ratio = 1.0f;
    if (ratio < min_ratio) {
      min_ratio = ratio;
      if (lowest_label)
        *lowest_label = labels[i];
    }
  }
  if (min_ratio < 0.0f)
    min_ratio = 0.0f;
  return min_ratio;
}

static void update_terrarium_card(uint32_t index, lv_coord_t card_width) {
  if (index >= REPTILE_MAX_TERRARIUMS)
    return;

  terrarium_card_widgets_t *widgets = &terrarium_cards[index];
  if (!widgets->card)
    return;

  if (index >= g_facility.terrarium_count) {
    lv_obj_add_flag(widgets->card, LV_OBJ_FLAG_HIDDEN);
    ui_theme_set_card_selected(widgets->card, false);
    return;
  }

  lv_obj_clear_flag(widgets->card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_min_width(widgets->card, card_width, 0);
  lv_obj_set_style_max_width(widgets->card, card_width, 0);

  if (widgets->title) {
    lv_label_set_text_fmt(widgets->title, "T%02" PRIu32,
                          (uint32_t)(index + 1U));
  }

  const terrarium_t *terrarium =
      reptile_facility_get_terrarium_const(&g_facility, (uint8_t)index);

  bool is_selected = (index == selected_terrarium);
  ui_theme_set_card_selected(widgets->card, is_selected);
  if (is_selected) {
    lv_obj_scroll_to_view(widgets->card, LV_ANIM_OFF);
  }

  if (!terrarium || !terrarium->occupied) {
    if (widgets->icon) {
      lv_img_set_src(widgets->icon, ui_theme_get_icon(UI_THEME_ICON_TERRARIUM_OK));
    }
    if (widgets->name) {
      lv_label_set_text(widgets->name, "Disponible");
    }
    if (widgets->stage) {
      lv_label_set_text(widgets->stage, "Aucun occupant assigné");
    }
    if (widgets->warning) {
      lv_label_set_text(widgets->warning,
                        "Touchez pour configurer ce terrarium");
      lv_obj_set_style_text_color(widgets->warning, lv_color_hex(0x4C6F52), 0);
    }
    if (widgets->badge) {
      lv_label_set_text(widgets->badge, "Libre");
      ui_theme_badge_set_kind(widgets->badge, UI_THEME_BADGE_INFO);
    }
    return;
  }

  const char *display_name =
      (terrarium->nickname[0] != '\0') ? terrarium->nickname
                                        : terrarium->species.name;
  if (widgets->name) {
    char name_buffer[96];
    if (strcmp(display_name, terrarium->species.name) == 0) {
      snprintf(name_buffer, sizeof(name_buffer), "%s", display_name);
    } else {
      snprintf(name_buffer, sizeof(name_buffer), "%s (%s)", display_name,
               terrarium->species.name);
    }
    lv_label_set_text(widgets->name, name_buffer);
  }

  if (widgets->stage) {
    const char *stage = growth_stage_to_string(terrarium->stage);
    lv_label_set_text_fmt(widgets->stage, "%s • %.1f kg • %" PRIu32 " j",
                          stage, terrarium->weight_g / 1000.0f,
                          terrarium->age_days);
  }

  bool maintenance_alert = terrarium->needs_maintenance;
  bool sensor_fault = !g_facility.sensors_available;
  const bool has_pathology = terrarium->pathology != REPTILE_PATHOLOGY_NONE;
  const bool has_incident = terrarium->incident != REPTILE_INCIDENT_NONE;

  char warn_buffer[160];
  if (has_pathology) {
    snprintf(warn_buffer, sizeof(warn_buffer), "Pathologie: %s",
             pathology_to_string(terrarium->pathology));
  } else if (has_incident) {
    snprintf(warn_buffer, sizeof(warn_buffer), "Incident: %s",
             incident_to_string(terrarium->incident));
  } else if (maintenance_alert) {
    snprintf(warn_buffer, sizeof(warn_buffer),
             "Maintenance à planifier (%" PRIu32 " h)",
             terrarium->maintenance_hours);
  } else if (sensor_fault) {
    snprintf(warn_buffer, sizeof(warn_buffer),
             "Capteurs hors-ligne • Sat %.0f%% Hyd %.0f%%",
             terrarium->satiety * 100.0f, terrarium->hydration * 100.0f);
  } else {
    snprintf(warn_buffer, sizeof(warn_buffer),
             "Conditions stables • Sat %.0f%% Hyd %.0f%%",
             terrarium->satiety * 100.0f, terrarium->hydration * 100.0f);
  }

  if (widgets->warning) {
    lv_label_set_text(widgets->warning, warn_buffer);
    lv_color_t color = lv_color_hex(0x4C6F52);
    if (has_pathology || has_incident) {
      color = lv_color_hex(0xC44536);
    } else if (maintenance_alert || sensor_fault) {
      color = lv_color_hex(0xC7763B);
    }
    lv_obj_set_style_text_color(widgets->warning, color, 0);
  }

  if (widgets->badge) {
    if (has_pathology || has_incident) {
      ui_theme_badge_set_kind(widgets->badge, UI_THEME_BADGE_WARNING);
      lv_label_set_text(widgets->badge, "Alerte");
    } else if (maintenance_alert || sensor_fault) {
      ui_theme_badge_set_kind(widgets->badge, UI_THEME_BADGE_WARNING);
      lv_label_set_text(widgets->badge,
                        maintenance_alert ? "Maintenance" : "Capteurs");
    } else if (terrarium->stage >= REPTILE_GROWTH_ADULT) {
      ui_theme_badge_set_kind(widgets->badge, UI_THEME_BADGE_SUCCESS);
      lv_label_set_text(widgets->badge, "Mature");
    } else {
      ui_theme_badge_set_kind(widgets->badge, UI_THEME_BADGE_SUCCESS);
      lv_label_set_text(widgets->badge, "OK");
    }
  }

  if (widgets->icon) {
    ui_theme_icon_t icon =
        (has_pathology || has_incident || maintenance_alert || sensor_fault)
            ? UI_THEME_ICON_TERRARIUM_ALERT
            : UI_THEME_ICON_TERRARIUM_OK;
    lv_img_set_src(widgets->icon, ui_theme_get_icon(icon));
  }
}

static void update_overview_screen(void) {
  if (!terrarium_grid)
    return;

  lv_coord_t card_width = determine_card_width(g_facility.terrarium_count);
  for (uint32_t i = 0; i < REPTILE_MAX_TERRARIUMS; ++i) {
    update_terrarium_card(i, card_width);
  }

  reptile_facility_metrics_t metrics;
  reptile_facility_compute_metrics(&g_facility, &metrics);

  if (cash_label || cash_badge || cash_bar) {
    double cash_euros = (double)g_facility.economy.cash_cents / 100.0;
    double net_euros =
        (double)(g_facility.economy.daily_income_cents -
                 g_facility.economy.daily_expenses_cents) /
        100.0;
    if (cash_label) {
      lv_label_set_text_fmt(
          cash_label, "Trésorerie: %s%.2f € | Flux %+0.2f €/j",
          (cash_euros < 0.0) ? "-" : "", fabs(cash_euros), net_euros);
    }
    if (cash_badge) {
      char badge_text[32];
      double cash_keuros = (double)g_facility.economy.cash_cents / 100000.0;
      snprintf(badge_text, sizeof(badge_text), "%+0.1fk€", cash_keuros);
      lv_label_set_text(cash_badge, badge_text);
      ui_theme_badge_set_kind(cash_badge, cash_euros >= 0.0
                                             ? UI_THEME_BADGE_SUCCESS
                                             : UI_THEME_BADGE_WARNING);
    }
    if (cash_bar) {
      int64_t cash_thousands64 = g_facility.economy.cash_cents / 1000;
      if (cash_thousands64 > 32000)
        cash_thousands64 = 32000;
      if (cash_thousands64 < -32000)
        cash_thousands64 = -32000;
      lv_coord_t cash_thousands = (lv_coord_t)cash_thousands64;
      int32_t span = (int32_t)llabs(cash_thousands64) + 500;
      if (span < 1500)
        span = 1500;
      if (span > 32000)
        span = 32000;
      lv_bar_set_range(cash_bar, (lv_coord_t)-span, (lv_coord_t)span);
      lv_bar_set_value(cash_bar, cash_thousands, LV_ANIM_OFF);
    }
  }

  if (cycle_badge || cycle_label || cycle_arc) {
    const reptile_day_cycle_t *cycle = &g_facility.cycle;
    uint32_t phase_ms = cycle->is_daytime ? cycle->day_ms : cycle->night_ms;
    if (phase_ms == 0U)
      phase_ms = 1U;
    float progress =
        (float)cycle->elapsed_in_phase_ms / (float)phase_ms;
    if (progress > 1.0f)
      progress = 1.0f;
    if (cycle_arc) {
      lv_arc_set_value(cycle_arc,
                       (lv_coord_t)lrintf(progress * 1000.0f));
    }
    if (cycle_label) {
      uint32_t minutes = cycle->elapsed_in_phase_ms / 60000U;
      uint32_t seconds = (cycle->elapsed_in_phase_ms / 1000U) % 60U;
      lv_label_set_text_fmt(
          cycle_label, "%s %02u:%02u | Jour %" PRIu32,
          cycle->is_daytime ? "Jour" : "Nuit", (unsigned)minutes,
          (unsigned)seconds, g_facility.economy.days_elapsed);
    }
    if (cycle_badge) {
      if (cycle->is_daytime) {
        ui_theme_badge_set_kind(cycle_badge, UI_THEME_BADGE_SUCCESS);
        lv_label_set_text(cycle_badge, "Jour");
      } else {
        ui_theme_badge_set_kind(cycle_badge, UI_THEME_BADGE_INFO);
        lv_label_set_text(cycle_badge, "Nuit");
      }
    }
  }

  const char *lowest_stock = "Proies";
  float stock_ratio = compute_stock_health(&g_facility.inventory,
                                           g_facility.terrarium_count,
                                           &lowest_stock);
  float clamped_stock = stock_ratio;
  if (clamped_stock > 1.0f)
    clamped_stock = 1.0f;
  if (clamped_stock < 0.0f)
    clamped_stock = 0.0f;

  if (stock_bar) {
    lv_bar_set_value(stock_bar,
                     (lv_coord_t)lrintf(clamped_stock * 1000.0f), LV_ANIM_OFF);
  }
  if (stock_badge) {
    char stock_badge_text[32];
    float percent = clamped_stock * 100.0f;
    snprintf(stock_badge_text, sizeof(stock_badge_text), "%s %.0f%%",
             lowest_stock, percent);
    lv_label_set_text(stock_badge, stock_badge_text);
    if (stock_ratio >= 0.75f) {
      ui_theme_badge_set_kind(stock_badge, UI_THEME_BADGE_SUCCESS);
    } else if (stock_ratio >= 0.45f) {
      ui_theme_badge_set_kind(stock_badge, UI_THEME_BADGE_WARNING);
    } else {
      ui_theme_badge_set_kind(stock_badge, UI_THEME_BADGE_CRITICAL);
    }
  }
  if (stock_label) {
    lv_label_set_text_fmt(
        stock_label,
        "Proies:%" PRIu32 " | Eau:%" PRIu32 " L | Substrat:%" PRIu32
        " | UV:%" PRIu32 " | Décor:%" PRIu32,
        g_facility.inventory.feeders, g_facility.inventory.water_reserve_l,
        g_facility.inventory.substrate_bags, g_facility.inventory.uv_bulbs,
        g_facility.inventory.decor_kits);
  }

  if (sensor_badge) {
    if (g_facility.sensors_available) {
      ui_theme_badge_set_kind(sensor_badge, UI_THEME_BADGE_SUCCESS);
      lv_label_set_text(sensor_badge, "Capteurs OK");
    } else {
      ui_theme_badge_set_kind(sensor_badge, UI_THEME_BADGE_WARNING);
      lv_label_set_text(sensor_badge, "Capteurs hors-ligne");
    }
  }

  if (incident_badge) {
    lv_label_set_text_fmt(incident_badge, "Incidents %" PRIu32
                                           " | Patho %" PRIu32,
                          g_facility.alerts_active,
                          g_facility.pathology_active);
    if (g_facility.alerts_active > 0U) {
      ui_theme_badge_set_kind(incident_badge, UI_THEME_BADGE_WARNING);
    } else {
      ui_theme_badge_set_kind(incident_badge, UI_THEME_BADGE_SUCCESS);
    }
  }

  if (occupancy_badge) {
    lv_label_set_text_fmt(occupancy_badge,
                          "Occupés %" PRIu32 "/%" PRIu32 " • Mat.%" PRIu32,
                          metrics.occupied, g_facility.terrarium_count,
                          metrics.mature);
    if (metrics.free_slots == 0U && g_facility.terrarium_count > 0U) {
      ui_theme_badge_set_kind(occupancy_badge, UI_THEME_BADGE_WARNING);
    } else if (metrics.occupied > 0U) {
      ui_theme_badge_set_kind(occupancy_badge, UI_THEME_BADGE_SUCCESS);
    } else {
      ui_theme_badge_set_kind(occupancy_badge, UI_THEME_BADGE_INFO);
    }
  }

  if (sleep_switch) {
    if (sleep_is_enabled()) {
      lv_obj_add_state(sleep_switch, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(sleep_switch, LV_STATE_CHECKED);
    }
  }
}

static void close_terrarium_context_menu(void) {
  if (overview_context_overlay) {
    lv_obj_del(overview_context_overlay);
    overview_context_overlay = NULL;
    overview_context_menu = NULL;
  }
}

static void terrarium_context_overlay_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_DELETE) {
    overview_context_overlay = NULL;
    overview_context_menu = NULL;
    return;
  }
  if (code == LV_EVENT_CLICKED || code == LV_EVENT_CANCEL) {
    close_terrarium_context_menu();
  }
}

static void show_terrarium_history_dialog(uint32_t index) {
  if (index >= REPTILE_MAX_TERRARIUMS)
    return;
  const terrarium_t *terrarium =
      reptile_facility_get_terrarium_const(&g_facility, (uint8_t)index);

  char title[96];
  if (terrarium && terrarium->occupied) {
    const char *display_name =
        (terrarium->nickname[0] != '\0') ? terrarium->nickname
                                          : terrarium->species.name;
    snprintf(title, sizeof(title), "T%02" PRIu32 " — %s",
             (uint32_t)(index + 1U), display_name);
  } else {
    snprintf(title, sizeof(title), "Terrarium T%02" PRIu32,
             (uint32_t)(index + 1U));
  }

  char body[384];
  if (terrarium && terrarium->occupied) {
    const char *stage = growth_stage_to_string(terrarium->stage);
    const char *pathology = pathology_to_string(terrarium->pathology);
    const char *incident = incident_to_string(terrarium->incident);
    const char *compliance =
        (terrarium->compliance_message[0] != '\0')
            ? terrarium->compliance_message
            : "Conformité à jour";
    snprintf(body, sizeof(body),
             "Espèce: %s\nStade: %s\nPathologie: %s\nIncident: %s\n"
             "Conformité: %s",
             terrarium->species.name, stage, pathology, incident, compliance);
  } else {
    snprintf(body, sizeof(body),
             "Ce terrarium est libre. Touchez une carte pour assigner une espèce.");
  }

  lv_obj_t *mbox = lv_msgbox_create(NULL);
  lv_msgbox_add_title(mbox, title);
  lv_msgbox_add_text(mbox, body);
  lv_msgbox_add_close_button(mbox);
  lv_obj_center(mbox);
}

static void show_terrarium_context_menu(uint32_t index) {
  if (index >= REPTILE_MAX_TERRARIUMS)
    return;
  close_terrarium_context_menu();

  overview_context_overlay = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(overview_context_overlay);
  lv_obj_set_style_bg_color(overview_context_overlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(overview_context_overlay, LV_OPA_40, 0);
  lv_obj_set_size(overview_context_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_add_flag(overview_context_overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(overview_context_overlay, terrarium_context_overlay_event_cb,
                      LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(overview_context_overlay, terrarium_context_overlay_event_cb,
                      LV_EVENT_DELETE, NULL);

  overview_context_menu = ui_theme_create_card(overview_context_overlay);
  lv_obj_set_style_pad_all(overview_context_menu, 18, 0);
  lv_obj_set_style_pad_gap(overview_context_menu, 12, 0);
  lv_obj_set_style_min_width(overview_context_menu, 280, 0);
  lv_obj_center(overview_context_menu);

  const terrarium_t *terrarium =
      reptile_facility_get_terrarium_const(&g_facility, (uint8_t)index);

  char header_text[96];
  if (terrarium && terrarium->occupied) {
    const char *display_name =
        (terrarium->nickname[0] != '\0') ? terrarium->nickname
                                          : terrarium->species.name;
    snprintf(header_text, sizeof(header_text), "T%02" PRIu32 " — %s",
             (uint32_t)(index + 1U), display_name);
  } else {
    snprintf(header_text, sizeof(header_text), "Terrarium T%02" PRIu32,
             (uint32_t)(index + 1U));
  }

  lv_obj_t *title = lv_label_create(overview_context_menu);
  ui_theme_apply_title(title);
  lv_label_set_text(title, header_text);
  lv_obj_set_width(title, LV_PCT(100));

  lv_obj_t *subtitle = lv_label_create(overview_context_menu);
  ui_theme_apply_body(subtitle);
  lv_obj_set_width(subtitle, LV_PCT(100));
  lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);
  if (terrarium && terrarium->occupied) {
    const char *stage = growth_stage_to_string(terrarium->stage);
    lv_label_set_text_fmt(subtitle, "%s • %s • %.1f kg",
                          terrarium->species.name, stage,
                          terrarium->weight_g / 1000.0f);
  } else {
    lv_label_set_text(subtitle, "Disponible pour un nouvel occupant.");
  }

  lv_obj_t *btn_detail = ui_theme_create_button(
      overview_context_menu, "Ouvrir détail", UI_THEME_BUTTON_PRIMARY,
      terrarium_context_button_event_cb,
      (void *)(uintptr_t)(((uint32_t)index << 8) |
                          (uint32_t)TERRARIUM_CONTEXT_ACTION_DETAIL));
  lv_obj_set_width(btn_detail, LV_PCT(100));

  lv_obj_t *btn_history = ui_theme_create_button(
      overview_context_menu, "Synthèse incidents", UI_THEME_BUTTON_SECONDARY,
      terrarium_context_button_event_cb,
      (void *)(uintptr_t)(((uint32_t)index << 8) |
                          (uint32_t)TERRARIUM_CONTEXT_ACTION_HISTORY));
  lv_obj_set_width(btn_history, LV_PCT(100));

  lv_obj_t *btn_close = ui_theme_create_button(
      overview_context_menu, "Fermer", UI_THEME_BUTTON_SECONDARY,
      terrarium_context_button_event_cb,
      (void *)(uintptr_t)(((uint32_t)index << 8) |
                          (uint32_t)TERRARIUM_CONTEXT_ACTION_CLOSE));
  lv_obj_set_width(btn_close, LV_PCT(100));
}

static void update_detail_screen(void) {
  if (!screen_detail)
    return;

  if (species_option_count == 0U && species_options_buffer[0] == '\0') {
    populate_species_options();
    if (dropdown_species && species_options_buffer[0] != '\0') {
      lv_dropdown_set_options(dropdown_species, species_options_buffer);
    }
  }

  const terrarium_t *terrarium =
      reptile_facility_get_terrarium_const(&g_facility,
                                           (uint8_t)selected_terrarium);

  if (dropdown_species) {
    if (!terrarium) {
      lv_obj_add_state(dropdown_species, LV_STATE_DISABLED);
      dropdown_select_none(dropdown_species);
      lv_dropdown_set_text(dropdown_species, "Terrarium indisponible");
    } else {
      lv_obj_clear_state(dropdown_species, LV_STATE_DISABLED);
      if (!terrarium->occupied) {
        dropdown_select_none(dropdown_species);
        lv_dropdown_set_text(dropdown_species, "Choisir espèce");
      } else {
        int idx = find_species_option_index(terrarium->species.id);
        if (idx >= 0) {
          dropdown_select_index(dropdown_species, (uint32_t)idx);
        } else {
          lv_dropdown_set_text(dropdown_species, terrarium->species.name);
        }
      }
    }
  }

  if (!terrarium || !terrarium->occupied) {
    if (detail_title) {
      lv_label_set_text(detail_title, "Terrarium disponible");
    }
    if (detail_alert_badge) {
      lv_label_set_text(detail_alert_badge, "Terrarium libre");
      ui_theme_badge_set_kind(detail_alert_badge, UI_THEME_BADGE_INFO);
    }
    if (detail_status_label) {
      lv_label_set_text(detail_status_label,
                        "Attribuer une espèce pour configurer ce terrarium");
    }
    if (detail_status_icon) {
      lv_img_set_src(detail_status_icon,
                     ui_theme_get_icon(UI_THEME_ICON_TERRARIUM_OK));
    }
    if (detail_temp_label) {
      lv_label_set_text(detail_temp_label, "Température: --");
    }
    if (detail_temp_meter && detail_temp_indicator) {
      lv_meter_set_indicator_value(detail_temp_meter, detail_temp_indicator, 0);
      if (detail_temp_range_indicator) {
        lv_meter_set_indicator_start_value(detail_temp_meter,
                                           detail_temp_range_indicator, 0);
        lv_meter_set_indicator_end_value(detail_temp_meter,
                                         detail_temp_range_indicator, 0);
      }
    }
    if (detail_humidity_arc) {
      lv_arc_set_value(detail_humidity_arc, 0);
    }
    if (detail_humidity_label) {
      lv_label_set_text(detail_humidity_label, "Humidité: --");
    }
    if (detail_growth_bar) {
      lv_bar_set_value(detail_growth_bar, 0, LV_ANIM_OFF);
    }
    if (detail_growth_label) {
      lv_label_set_text(detail_growth_label, "Croissance: --");
    }
    if (detail_uv_label) {
      lv_label_set_text(detail_uv_label, "UV: --");
    }
    if (detail_satiety_label) {
      lv_label_set_text(detail_satiety_label, "Satiété: --");
    }
    if (detail_hydration_label) {
      lv_label_set_text(detail_hydration_label, "Hydratation: --");
    }
    if (detail_weight_label) {
      lv_label_set_text(detail_weight_label, "Poids: --");
    }
    if (detail_stage_badge) {
      lv_label_set_text(detail_stage_badge, "Stade: --");
      ui_theme_badge_set_kind(detail_stage_badge, UI_THEME_BADGE_INFO);
    }
    if (detail_pathology_badge) {
      lv_label_set_text(detail_pathology_badge, "Pathologie: --");
      ui_theme_badge_set_kind(detail_pathology_badge, UI_THEME_BADGE_INFO);
    }
    if (detail_incident_badge) {
      lv_label_set_text(detail_incident_badge, "Incident: --");
      ui_theme_badge_set_kind(detail_incident_badge, UI_THEME_BADGE_INFO);
    }
    if (detail_compliance_label) {
      lv_label_set_text(detail_compliance_label,
                        "Aucune conformité requise sans espèce");
    }
    dropdown_select_none(dropdown_substrate);
    dropdown_select_none(dropdown_heating);
    dropdown_select_none(dropdown_decor);
    dropdown_select_none(dropdown_uv);
    dropdown_select_none(dropdown_size);
    if (education_switch_detail) {
      lv_obj_clear_state(education_switch_detail, LV_STATE_CHECKED);
    }
    if (detail_register_label) {
      lv_label_set_text(detail_register_label, "Registre non renseigné");
    }
    if (register_button) {
      lv_obj_t *label = lv_obj_get_child(register_button, 0);
      if (label) {
        lv_label_set_text(label, "Consigner la cession");
      }
    }
    update_certificate_table();
    return;
  }

  const species_profile_t *profile = &terrarium->species;
  char title[128];
  const char *display_name =
      (terrarium->nickname[0] != '\0') ? terrarium->nickname
                                        : terrarium->species.name;
  snprintf(title, sizeof(title), "T%02" PRIu32 " - %s (%s)",
           (uint32_t)(selected_terrarium + 1U), display_name, profile->name);
  if (detail_title) {
    lv_label_set_text(detail_title, title);
  }

  bool warn = terrarium->pathology != REPTILE_PATHOLOGY_NONE ||
              terrarium->incident != REPTILE_INCIDENT_NONE;
  if (detail_status_icon) {
    lv_img_set_src(detail_status_icon,
                   ui_theme_get_icon(warn ? UI_THEME_ICON_TERRARIUM_ALERT
                                          : UI_THEME_ICON_TERRARIUM_OK));
  }
  if (detail_status_label) {
    lv_label_set_text_fmt(
        detail_status_label,
        "Substrat: %s | Chauffage: %s | Décor: %s | UV: %s",
        terrarium->config.substrate, terrarium->config.heating,
        terrarium->config.decor, terrarium->config.uv_setup);
  }

  if (detail_alert_badge) {
    if (terrarium->incident != REPTILE_INCIDENT_NONE) {
      ui_theme_badge_set_kind(detail_alert_badge,
                              incident_badge_kind(terrarium->incident));
      lv_label_set_text_fmt(detail_alert_badge, "Incident: %s",
                            incident_to_string(terrarium->incident));
    } else if (terrarium->pathology != REPTILE_PATHOLOGY_NONE) {
      ui_theme_badge_set_kind(detail_alert_badge,
                              pathology_badge_kind(terrarium->pathology));
      lv_label_set_text_fmt(detail_alert_badge, "Surveillance: %s",
                            pathology_to_string(terrarium->pathology));
    } else {
      ui_theme_badge_set_kind(detail_alert_badge, UI_THEME_BADGE_SUCCESS);
      lv_label_set_text(detail_alert_badge, "Paramètres conformes");
    }
  }

  bool is_daytime = g_facility.cycle.is_daytime;
  float temp_min = is_daytime ? profile->day_temp_min : profile->night_temp_min;
  float temp_max = is_daytime ? profile->day_temp_max : profile->night_temp_max;
  if (detail_temp_meter && detail_temp_indicator) {
    int32_t temp_value = (int32_t)lrintf(terrarium->temperature_c * 10.0f);
    lv_meter_set_indicator_value(detail_temp_meter, detail_temp_indicator,
                                 temp_value);
    if (detail_temp_range_indicator) {
      lv_meter_set_indicator_start_value(
          detail_temp_meter, detail_temp_range_indicator,
          (int32_t)lrintf(temp_min * 10.0f));
      lv_meter_set_indicator_end_value(
          detail_temp_meter, detail_temp_range_indicator,
          (int32_t)lrintf(temp_max * 10.0f));
    }
  }
  if (detail_temp_label) {
    lv_label_set_text_fmt(detail_temp_label,
                          "Température: %.1f °C (%.1f-%.1f °C %s)",
                          terrarium->temperature_c, temp_min, temp_max,
                          is_daytime ? "jour" : "nuit");
  }

  if (detail_humidity_arc) {
    lv_arc_set_value(detail_humidity_arc,
                     (int32_t)lrintf(terrarium->humidity_pct * 10.0f));
  }
  if (detail_humidity_label) {
    lv_label_set_text_fmt(detail_humidity_label,
                          "Humidité: %.0f %% (%.0f-%.0f %%)",
                          terrarium->humidity_pct, profile->humidity_min,
                          profile->humidity_max);
  }

  if (detail_growth_bar) {
    lv_bar_set_value(detail_growth_bar,
                     (int32_t)lrintf(terrarium->growth * 1000.0f),
                     LV_ANIM_OFF);
  }
  if (detail_growth_label) {
    lv_label_set_text_fmt(detail_growth_label, "Croissance: %.0f %%",
                          terrarium->growth * 100.0f);
  }

  if (detail_uv_label) {
    lv_label_set_text_fmt(detail_uv_label, "UV: %.2f (%.1f-%.1f)",
                          terrarium->uv_index, profile->uv_min,
                          profile->uv_max);
  }
  if (detail_satiety_label) {
    lv_label_set_text_fmt(detail_satiety_label, "Satiété: %.0f %%",
                          terrarium->satiety * 100.0f);
  }
  if (detail_hydration_label) {
    lv_label_set_text_fmt(detail_hydration_label, "Hydratation: %.0f %%",
                          terrarium->hydration * 100.0f);
  }
  if (detail_weight_label) {
    lv_label_set_text_fmt(detail_weight_label, "Poids: %.0f g / %.0f g",
                          terrarium->weight_g, profile->adult_weight_g);
  }

  if (detail_stage_badge) {
    ui_theme_badge_set_kind(detail_stage_badge, UI_THEME_BADGE_INFO);
    lv_label_set_text_fmt(detail_stage_badge, "Stade: %s",
                          growth_stage_to_string(terrarium->stage));
  }
  if (detail_pathology_badge) {
    ui_theme_badge_set_kind(detail_pathology_badge,
                            pathology_badge_kind(terrarium->pathology));
    lv_label_set_text_fmt(detail_pathology_badge, "Pathologie: %s",
                          pathology_to_string(terrarium->pathology));
  }
  if (detail_incident_badge) {
    ui_theme_badge_set_kind(detail_incident_badge,
                            incident_badge_kind(terrarium->incident));
    lv_label_set_text_fmt(detail_incident_badge, "Incident: %s",
                          incident_to_string(terrarium->incident));
  }

  if (detail_compliance_label) {
    if (terrarium->compliance_message[0] != '\0') {
      lv_label_set_text(detail_compliance_label,
                        terrarium->compliance_message);
    } else if (terrarium->incident != REPTILE_INCIDENT_NONE) {
      lv_label_set_text_fmt(detail_compliance_label,
                            "Obligation non respectée: %s",
                            incident_to_string(terrarium->incident));
    } else {
      lv_label_set_text(detail_compliance_label,
                        "Obligations réglementaires satisfaites");
    }
  }

  load_dropdown_value(dropdown_substrate, substrate_options,
                      terrarium->config.substrate);
  load_dropdown_value(dropdown_heating, heating_options,
                      terrarium->config.heating);
  load_dropdown_value(dropdown_decor, decor_options,
                      terrarium->config.decor);
  load_dropdown_value(dropdown_uv, uv_options, terrarium->config.uv_setup);
  if (dropdown_size) {
    int size_index = find_size_option(terrarium->config.length_cm,
                                      terrarium->config.width_cm,
                                      terrarium->config.height_cm);
    if (size_index >= 0) {
      dropdown_select_index(dropdown_size, (uint32_t)size_index);
    } else {
      dropdown_select_none(dropdown_size);
    }
  }

  if (education_switch_detail) {
    if (terrarium->config.educational_panel_present) {
      lv_obj_add_state(education_switch_detail, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(education_switch_detail, LV_STATE_CHECKED);
    }
  }

  if (detail_register_label) {
    if (terrarium->config.register_completed &&
        terrarium->config.register_reference[0] != '\0') {
      lv_label_set_text_fmt(detail_register_label, "Registre: %s",
                            terrarium->config.register_reference);
    } else {
      lv_label_set_text(detail_register_label, "Registre à compléter");
    }
  }

  if (register_button) {
    lv_obj_t *label = lv_obj_get_child(register_button, 0);
    if (label) {
      lv_label_set_text(label,
                        terrarium->config.register_completed
                            ? "Annuler la cession"
                            : "Consigner la cession");
    }
  }

  update_certificate_table();
}

static void update_certificate_table(void) {
  if (!detail_cert_table)
    return;
  const terrarium_t *terrarium =
      reptile_facility_get_terrarium_const(&g_facility,
                                           (uint8_t)selected_terrarium);

  uint32_t row_count = 2U;
  if (terrarium && terrarium->occupied &&
      terrarium->certificate_count + 1U > row_count) {
    row_count = terrarium->certificate_count + 1U;
  }
  lv_table_set_row_count(detail_cert_table, row_count);
  lv_table_set_cell_value(detail_cert_table, 0, 0, "Identifiant");
  lv_table_set_cell_value(detail_cert_table, 0, 1, "Échéance");

  if (!terrarium || !terrarium->occupied ||
      terrarium->certificate_count == 0U) {
    lv_table_set_cell_value(detail_cert_table, 1, 0,
                            "Aucun certificat enregistré");
    lv_table_set_cell_value(detail_cert_table, 1, 1, "–");
    for (uint32_t row = 2U; row < row_count; ++row) {
      lv_table_set_cell_value(detail_cert_table, row, 0, "");
      lv_table_set_cell_value(detail_cert_table, row, 1, "");
    }
    return;
  }

  for (uint32_t i = 0; i < terrarium->certificate_count; ++i) {
    const reptile_certificate_t *cert = &terrarium->certificates[i];
    lv_table_set_cell_value(detail_cert_table, i + 1U, 0, cert->id);
    if (cert->expiry_date == 0) {
      lv_table_set_cell_value(detail_cert_table, i + 1U, 1, "Illimitée");
    } else {
      struct tm tm_buf;
      localtime_r(&cert->expiry_date, &tm_buf);
      char buf[32];
      strftime(buf, sizeof(buf), "%d/%m/%Y", &tm_buf);
      lv_table_set_cell_value(detail_cert_table, i + 1U, 1, buf);
    }
  }

  for (uint32_t row = terrarium->certificate_count + 1U; row < row_count;
       ++row) {
    lv_table_set_cell_value(detail_cert_table, row, 0, "");
    lv_table_set_cell_value(detail_cert_table, row, 1, "");
  }
}

static void update_regulation_screen(void) {
  if (!regulations_table || !regulations_alert_table)
    return;

  const regulation_rule_t *rules = NULL;
  size_t rule_count = regulations_get_rules(&rules);
  lv_table_set_row_count(regulations_table, rule_count + 1U);
  lv_table_set_cell_value(regulations_table, 0, 0, "Espèce");
  lv_table_set_cell_value(regulations_table, 0, 1, "Statut");
  lv_table_set_cell_value(regulations_table, 0, 2, "Certificat");
  lv_table_set_cell_value(regulations_table, 0, 3, "Dimensions min");
  for (size_t i = 0; i < rule_count; ++i) {
    const regulation_rule_t *rule = &rules[i];
    lv_table_set_cell_value(regulations_table, i + 1U, 0,
                            rule->common_name ? rule->common_name : "N/D");
    lv_table_set_cell_value(regulations_table, i + 1U, 1,
                            regulations_status_to_string(rule->status));
    lv_table_set_cell_value(regulations_table, i + 1U, 2,
                            rule->certificate_text ? rule->certificate_text
                                                   : "N/A");
    char dim_buf[48];
    snprintf(dim_buf, sizeof(dim_buf), "%.0fx%.0fx%.0f cm", rule->min_length_cm,
             rule->min_width_cm, rule->min_height_cm);
    lv_table_set_cell_value(regulations_table, i + 1U, 3, dim_buf);
  }

  lv_table_set_row_count(regulations_alert_table, 1);
  lv_table_set_cell_value(regulations_alert_table, 0, 0, "Terrarium");
  lv_table_set_cell_value(regulations_alert_table, 0, 1, "Incident");
  lv_table_set_cell_value(regulations_alert_table, 0, 2, "Message");

  uint32_t row = 1;
  for (uint32_t i = 0; i < g_facility.terrarium_count; ++i) {
    const terrarium_t *terrarium =
        reptile_facility_get_terrarium_const(&g_facility, (uint8_t)i);
    if (!terrarium || !terrarium->occupied) {
      continue;
    }
    const regulation_rule_t *rule =
        regulations_get_rule((int)terrarium->species.id);
    bool expired = (terrarium->incident == REPTILE_INCIDENT_CERTIFICATE_EXPIRED);
    bool cert_ok = terrarium->certificate_count > 0 && !expired &&
                   terrarium->incident != REPTILE_INCIDENT_CERTIFICATE_MISSING;
    regulations_compliance_input_t input = {
        .length_cm = terrarium->config.length_cm,
        .width_cm = terrarium->config.width_cm,
        .height_cm = terrarium->config.height_cm,
        .temperature_c = terrarium->temperature_c,
        .humidity_pct = terrarium->humidity_pct,
        .uv_index = terrarium->uv_index,
        .is_daytime = g_facility.cycle.is_daytime,
        .certificate_count = terrarium->certificate_count,
        .certificate_valid = cert_ok,
        .certificate_expired = expired,
        .register_present = terrarium->config.register_completed,
        .education_present = terrarium->config.educational_panel_present,
    };
    regulations_compliance_report_t report = {0};
    bool compliance_issue = false;
    if (rule) {
      regulations_evaluate(rule, &input, &report);
      compliance_issue = !report.allowed || !report.dimensions_ok ||
                        !report.certificate_ok || !report.register_ok ||
                        !report.education_ok;
    } else {
      compliance_issue = terrarium->incident != REPTILE_INCIDENT_NONE;
    }
    if (!compliance_issue && terrarium->incident == REPTILE_INCIDENT_NONE) {
      continue;
    }
    lv_table_set_row_count(regulations_alert_table, row + 1U);
    char terrarium_id[8];
    snprintf(terrarium_id, sizeof(terrarium_id), "T%02" PRIu32,
             (uint32_t)(i + 1U));
    lv_table_set_cell_value(regulations_alert_table, row, 0, terrarium_id);
    lv_table_set_cell_value(regulations_alert_table, row, 1,
                            incident_to_string(terrarium->incident));
    lv_table_set_cell_value(regulations_alert_table, row, 2,
                            terrarium->compliance_message);
    row++;
  }
  if (row == 1U) {
    lv_table_set_row_count(regulations_alert_table, 2);
    lv_table_set_cell_value(regulations_alert_table, 1, 0, "-");
    lv_table_set_cell_value(regulations_alert_table, 1, 1, "Aucun");
    lv_table_set_cell_value(regulations_alert_table, 1, 2,
                            "Tous les terrariums sont conformes");
  }

  if (regulations_summary_label) {
    lv_label_set_text_fmt(
        regulations_summary_label,
        "Alertes conformité: %" PRIu32 " | Incidents actifs: %" PRIu32,
        g_facility.compliance_alerts, g_facility.alerts_active);
  }
}

static void update_economy_screen(void) {
  if (!economy_table)
    return;
  lv_table_set_cell_value(economy_table, 0, 0, "Terrarium");
  lv_table_set_cell_value(economy_table, 0, 1, "Recettes €/j");
  lv_table_set_cell_value(economy_table, 0, 2, "Coûts €/j");
  lv_table_set_cell_value(economy_table, 0, 3, "Statut");

  uint32_t row = 1;
  for (uint32_t i = 0; i < g_facility.terrarium_count && row < 6U; ++i) {
    const terrarium_t *terrarium =
        reptile_facility_get_terrarium_const(&g_facility, (uint8_t)i);
    if (!terrarium || !terrarium->occupied) {
      continue;
    }
    lv_table_set_cell_value_fmt(economy_table, row, 0, "T%02" PRIu32,
                                (uint32_t)(i + 1U));
    lv_table_set_cell_value_fmt(economy_table, row, 1, "%.2f",
                                terrarium->revenue_cents_per_day / 100.0f);
    lv_table_set_cell_value_fmt(economy_table, row, 2, "%.2f",
                                terrarium->operating_cost_cents_per_day /
                                    100.0f);
    const char *status =
        (terrarium->pathology != REPTILE_PATHOLOGY_NONE)
            ? "Soins"
            : ((terrarium->incident != REPTILE_INCIDENT_NONE) ? "Audit" :
                                                             "OK");
    lv_table_set_cell_value(economy_table, row, 3, status);
    row++;
  }
  for (; row < 6U; ++row) {
    lv_table_set_cell_value(economy_table, row, 0, "");
    lv_table_set_cell_value(economy_table, row, 1, "");
    lv_table_set_cell_value(economy_table, row, 2, "");
    lv_table_set_cell_value(economy_table, row, 3, "");
  }

  lv_label_set_text_fmt(
      economy_summary_label,
      "Jour %" PRIu32
      " | Revenu hebdo: %.2f € | Revenu d'exploitation: %.2f € | Dépenses: %.2f € |"
      " Amendes cumulées: %.2f €",
      g_facility.economy.days_elapsed,
      g_facility.economy.weekly_subsidy_cents / 100.0,
      g_facility.economy.daily_income_cents / 100.0,
      g_facility.economy.daily_expenses_cents / 100.0,
      g_facility.economy.fines_cents / 100.0);
}

static void update_chart_series(int64_t income_cents, int64_t expense_cents) {
  if (!economy_chart)
    return;
  int64_t delta_income = income_cents - prev_income_snapshot;
  int64_t delta_expense = expense_cents - prev_expense_snapshot;
  prev_income_snapshot = income_cents;
  prev_expense_snapshot = expense_cents;

  lv_coord_t income_val = (lv_coord_t)(delta_income / 100);
  lv_coord_t expense_val = (lv_coord_t)(delta_expense / 100);
  lv_chart_set_next_value(economy_chart, series_income, income_val);
  lv_chart_set_next_value(economy_chart, series_expenses, -expense_val);
  lv_chart_refresh(economy_chart);
}

static void terrarium_card_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  uint32_t index = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

  if (code == LV_EVENT_CLICKED) {
    if (index >= g_facility.terrarium_count) {
      close_terrarium_context_menu();
      return;
    }
    if (selected_terrarium != index) {
      selected_terrarium = index;
      update_overview_screen();
      update_detail_screen();
    }
    ensure_game_screens();
    if (screen_detail) {
      lv_scr_load(screen_detail);
    }
    close_terrarium_context_menu();
  } else if (code == LV_EVENT_LONG_PRESSED) {
    if (index < g_facility.terrarium_count) {
      show_terrarium_context_menu(index);
    }
  }
}

static void terrarium_context_button_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    return;

  uintptr_t encoded = (uintptr_t)lv_event_get_user_data(e);
  uint32_t index = (uint32_t)((encoded >> 8) & 0xFFFFFFU);
  terrarium_context_action_t action =
      (terrarium_context_action_t)(encoded & 0xFFU);

  if (index >= REPTILE_MAX_TERRARIUMS) {
    close_terrarium_context_menu();
    return;
  }

  switch (action) {
  case TERRARIUM_CONTEXT_ACTION_DETAIL:
    if (index < g_facility.terrarium_count) {
      selected_terrarium = index;
      ensure_game_screens();
      update_overview_screen();
      update_detail_screen();
      if (screen_detail) {
        lv_scr_load(screen_detail);
      }
    }
    close_terrarium_context_menu();
    break;
  case TERRARIUM_CONTEXT_ACTION_HISTORY:
    close_terrarium_context_menu();
    show_terrarium_history_dialog(index);
    break;
  default:
    close_terrarium_context_menu();
    break;
  }
}

static void nav_button_event_cb(lv_event_t *e) {
  lv_obj_t *target = lv_event_get_user_data(e);
  if (!target)
    return;
  lv_scr_load(target);
}

static void species_dropdown_event_cb(lv_event_t *e) {
  lv_obj_t *dd = lv_event_get_target(e);
  if (!dd)
    return;
  uint16_t selected = lv_dropdown_get_selected(dd);
#if defined(LV_DROPDOWN_SELECTED_NONE)
  if (selected == LV_DROPDOWN_SELECTED_NONE)
    return;
#endif
  if (selected >= species_option_count)
    return;
  const species_profile_t *profile =
      reptile_species_get(species_option_ids[selected]);
  if (!profile)
    return;
  terrarium_t *terrarium =
      reptile_facility_get_terrarium(&g_facility, (uint8_t)selected_terrarium);
  if (!terrarium)
    return;
  char nickname[REPTILE_NAME_MAX_LEN];
  const char *nickname_ptr = NULL;
  if (terrarium->nickname[0] != '\0') {
    snprintf(nickname, sizeof(nickname), "%s", terrarium->nickname);
    nickname_ptr = nickname;
  }
  esp_err_t err = reptile_terrarium_set_species(terrarium, profile, nickname_ptr);
  if (err == ESP_OK) {
    reptile_facility_save(&g_facility);
  } else if (detail_compliance_label) {
    lv_label_set_text(detail_compliance_label,
                      terrarium->compliance_message[0] != '\0'
                          ? terrarium->compliance_message
                          : "Profil refusé");
  }
  update_detail_screen();
  update_overview_screen();
  update_economy_screen();
  update_regulation_screen();
}

static void config_dropdown_event_cb(lv_event_t *e) {
  lv_obj_t *dd = lv_event_get_target(e);
  config_field_t field =
      (config_field_t)(uintptr_t)lv_event_get_user_data(e);
  char sel[64];
  lv_dropdown_get_selected_str(dd, sel, sizeof(sel));
  terrarium_t *terrarium =
      reptile_facility_get_terrarium(&g_facility, (uint8_t)selected_terrarium);
  if (!terrarium || !terrarium->occupied)
    return;
  esp_err_t err = ESP_OK;
  switch (field) {
  case CONFIG_SUBSTRATE:
    err = reptile_terrarium_set_substrate(terrarium, sel);
    break;
  case CONFIG_HEATING:
    err = reptile_terrarium_set_heating(terrarium, sel);
    break;
  case CONFIG_DECOR:
    err = reptile_terrarium_set_decor(terrarium, sel);
    break;
  case CONFIG_UV:
    err = reptile_terrarium_set_uv(terrarium, sel);
    break;
  case CONFIG_SIZE: {
    static const struct {
      float length_cm;
      float width_cm;
      float height_cm;
    } kSizes[] = {
        {90.f, 45.f, 45.f},
        {120.f, 60.f, 60.f},
        {180.f, 90.f, 60.f},
        {200.f, 100.f, 60.f},
    };
    uint16_t index = lv_dropdown_get_selected(dd);
    if (index < (sizeof(kSizes) / sizeof(kSizes[0]))) {
      err = reptile_terrarium_set_dimensions(terrarium, kSizes[index].length_cm,
                                             kSizes[index].width_cm,
                                             kSizes[index].height_cm);
    }
    break;
  }
  }
  if (err == ESP_OK) {
    reptile_facility_save(&g_facility);
  } else if (detail_compliance_label) {
    lv_label_set_text(detail_compliance_label,
                      "Configuration refusée (non conforme)");
  }
  update_detail_screen();
}

static void scan_certificate_event_cb(lv_event_t *e) {
  (void)e;
  terrarium_t *terrarium =
      reptile_facility_get_terrarium(&g_facility, (uint8_t)selected_terrarium);
  if (!terrarium || !terrarium->occupied) {
    if (detail_compliance_label) {
      lv_label_set_text(detail_compliance_label,
                        "Scanner disponible après assignation d'une espèce");
    }
    return;
  }
  ESP_LOGI(TAG, "Demande de scan certificat pour T%02" PRIu32,
           (uint32_t)(selected_terrarium + 1U));
  if (detail_compliance_label) {
    lv_label_set_text(detail_compliance_label,
                      "Scan certificat en attente de lecture NFC/QR...");
  }
}

static void add_certificate_event_cb(lv_event_t *e) {
  (void)e;
  terrarium_t *terrarium =
      reptile_facility_get_terrarium(&g_facility, (uint8_t)selected_terrarium);
  if (!terrarium || !terrarium->occupied)
    return;
  reptile_certificate_t cert = {0};
  time_t now = time(NULL);
  snprintf(cert.id, sizeof(cert.id), "CERT-%02" PRIu32 "-%ld",
           (uint32_t)(selected_terrarium + 1U), (long)(now % 100000));
  snprintf(cert.authority, sizeof(cert.authority), "DDPP");
  cert.issue_date = now;
  cert.expiry_date = now + 365L * 24L * 3600L;
  cert.valid = true;
  if (reptile_terrarium_add_certificate(terrarium, &cert) == ESP_OK) {
    reptile_facility_save(&g_facility);
    update_certificate_table();
  }
}

static void inventory_button_event_cb(lv_event_t *e) {
  inventory_action_t action =
      (inventory_action_t)(uintptr_t)lv_event_get_user_data(e);
  switch (action) {
  case INVENTORY_ADD_FEED:
    reptile_inventory_add_feed(&g_facility, 10);
    break;
  case INVENTORY_ADD_WATER:
    reptile_inventory_add_water(&g_facility, 20);
    break;
  case INVENTORY_ADD_SUBSTRATE:
    reptile_inventory_add_substrate(&g_facility, 2);
    break;
  case INVENTORY_ADD_UV:
    reptile_inventory_add_uv_bulbs(&g_facility, 1);
    break;
  case INVENTORY_ADD_DECOR:
    reptile_inventory_add_decor(&g_facility, 1);
    break;
  }
  reptile_facility_save(&g_facility);
  update_overview_screen();
}

static void education_switch_event_cb(lv_event_t *e) {
  lv_obj_t *sw = lv_event_get_target(e);
  terrarium_t *terrarium =
      reptile_facility_get_terrarium(&g_facility, (uint8_t)selected_terrarium);
  if (!terrarium || !terrarium->occupied)
    return;
  bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
  reptile_terrarium_set_education(terrarium, enabled);
  reptile_facility_save(&g_facility);
  update_detail_screen();
  update_regulation_screen();
}

static void register_button_event_cb(lv_event_t *e) {
  (void)e;
  terrarium_t *terrarium =
      reptile_facility_get_terrarium(&g_facility, (uint8_t)selected_terrarium);
  if (!terrarium || !terrarium->occupied)
    return;
  if (terrarium->config.register_completed) {
    reptile_terrarium_set_register(terrarium, false, NULL);
  } else {
    char ref[REPTILE_CERT_ID_LEN];
    time_t now = time(NULL);
    snprintf(ref, sizeof(ref), "REG-%02" PRIu32 "-%ld",
             (uint32_t)(selected_terrarium + 1U), (long)(now % 100000));
    reptile_terrarium_set_register(terrarium, true, ref);
  }
  reptile_facility_save(&g_facility);
  update_detail_screen();
  update_regulation_screen();
}

static void export_report_event_cb(lv_event_t *e) {
  (void)e;
  char filename[64];
  time_t now = time(NULL);
  struct tm tm_info;
  localtime_r(&now, &tm_info);
  strftime(filename, sizeof(filename), "rapport_%Y%m%d_%H%M%S.csv", &tm_info);
  esp_err_t err =
      reptile_facility_export_regulation_report(&g_facility, filename);
  if (regulations_export_label) {
    if (err == ESP_OK) {
      char path[160];
      snprintf(path, sizeof(path), "%s/reports/%s", MOUNT_POINT, filename);
      lv_label_set_text_fmt(regulations_export_label, "Exporté: %s", path);
    } else {
      lv_label_set_text(regulations_export_label,
                        "Échec export (microSD indisponible)");
    }
  }
}

static void save_slot_event_cb(lv_event_t *e) {
  char slot[16];
  lv_dropdown_get_selected_str(lv_event_get_target(e), slot, sizeof(slot));
  if (reptile_facility_set_slot(&g_facility, slot) == ESP_OK) {
    simulation_apply_active_slot(g_facility.slot);
    selected_terrarium = 0;
    prev_income_snapshot = g_facility.economy.daily_income_cents;
    prev_expense_snapshot = g_facility.economy.daily_expenses_cents;
    if (save_status_label) {
      lv_label_set_text_fmt(save_status_label, "Slot actif: %s",
                           g_facility.slot);
    }
    simulation_set_status("Slot actif: %s", g_facility.slot);
    update_overview_screen();
    update_detail_screen();
    update_economy_screen();
    update_regulation_screen();
  } else {
    lv_label_set_text_fmt(save_status_label, "Échec chargement slot %s", slot);
  }
}

static void save_action_event_cb(lv_event_t *e) {
  save_action_t action =
      (save_action_t)(uintptr_t)lv_event_get_user_data(e);
  esp_err_t err = ESP_OK;
  switch (action) {
  case SAVE_ACTION_SAVE:
    err = reptile_facility_save(&g_facility);
    lv_label_set_text(save_status_label,
                      (err == ESP_OK) ? "Sauvegarde effectuée"
                                       : "Échec de la sauvegarde");
    break;
  case SAVE_ACTION_LOAD:
    err = reptile_facility_load(&g_facility);
    if (err == ESP_OK) {
      lv_label_set_text(save_status_label, "Chargement réussi");
      update_overview_screen();
      update_detail_screen();
      update_economy_screen();
    } else {
      lv_label_set_text(save_status_label, "Chargement impossible");
    }
    break;
  case SAVE_ACTION_RESET_STATS:
    reptile_facility_reset_statistics(&g_facility);
    lv_label_set_text(save_status_label, "Compteurs journaliers remis à zéro");
    break;
  }
}

static void simulation_new_game_event_cb(lv_event_t *e) {
  (void)e;
  char slot[sizeof(g_facility.slot)];
  simulation_get_selected_slot(slot, sizeof(slot));
  simulation_apply_active_slot(slot);

  reptile_facility_reset_state(&g_facility);
  esp_err_t err = reptile_facility_save(&g_facility);

  selected_terrarium = 0;
  autosave_ms = 0;
  last_tick_ms = lv_tick_get();
  prev_income_snapshot = g_facility.economy.daily_income_cents;
  prev_expense_snapshot = g_facility.economy.daily_expenses_cents;

  if (err == ESP_OK) {
    simulation_set_status("Nouvelle partie sur %s", g_facility.slot);
  } else {
    simulation_set_status("Slot %s: sauvegarde impossible", g_facility.slot);
  }

  simulation_enter_overview();
}

static void simulation_resume_event_cb(lv_event_t *e) {
  (void)e;
  char slot[sizeof(g_facility.slot)];
  simulation_get_selected_slot(slot, sizeof(slot));
  char previous_slot[sizeof(g_facility.slot)];
  snprintf(previous_slot, sizeof(previous_slot), "%s", g_facility.slot);
  simulation_apply_active_slot(slot);

  esp_err_t err = reptile_facility_load(&g_facility);
  if (err != ESP_OK) {
    simulation_set_status("Chargement échoué (%s)", g_facility.slot);
    simulation_apply_active_slot(previous_slot);
    return;
  }

  selected_terrarium = 0;
  autosave_ms = 0;
  last_tick_ms = lv_tick_get();
  prev_income_snapshot = g_facility.economy.daily_income_cents;
  prev_expense_snapshot = g_facility.economy.daily_expenses_cents;

  simulation_set_status("Slot chargé: %s", g_facility.slot);
  simulation_enter_overview();
}

static void simulation_settings_event_cb(lv_event_t *e) {
  (void)e;
  settings_screen_show();
  simulation_set_status("Paramètres ouverts");
}

static void menu_button_event_cb(lv_event_t *e) {
  (void)e;
  reptile_game_stop();
  if (menu_screen) {
    lv_scr_load(menu_screen);
  }
}

static void sleep_switch_event_cb(lv_event_t *e) {
  bool checked = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  sleep_set_enabled(checked);
}

static const char *growth_stage_to_string(reptile_growth_stage_t stage) {
  switch (stage) {
  case REPTILE_GROWTH_HATCHLING:
    return "Nouveau-né";
  case REPTILE_GROWTH_JUVENILE:
    return "Juvénile";
  case REPTILE_GROWTH_ADULT:
    return "Adulte";
  case REPTILE_GROWTH_SENIOR:
    return "Sénior";
  default:
    return "Inconnu";
  }
}

static const char *pathology_to_string(reptile_pathology_t pathology) {
  switch (pathology) {
  case REPTILE_PATHOLOGY_NONE:
    return "Aucune";
  case REPTILE_PATHOLOGY_RESPIRATORY:
    return "Affection respiratoire";
  case REPTILE_PATHOLOGY_PARASITIC:
    return "Parasitoses";
  case REPTILE_PATHOLOGY_METABOLIC:
    return "Syndrome métabolique";
  default:
    return "N/C";
  }
}

static const char *incident_to_string(reptile_incident_t incident) {
  switch (incident) {
  case REPTILE_INCIDENT_NONE:
    return "Aucun";
  case REPTILE_INCIDENT_CERTIFICATE_MISSING:
    return "Certificat manquant";
  case REPTILE_INCIDENT_CERTIFICATE_EXPIRED:
    return "Certificat expiré";
  case REPTILE_INCIDENT_ENVIRONMENT_OUT_OF_RANGE:
    return "Non-conformité climatique";
  case REPTILE_INCIDENT_REGISTER_MISSING:
    return "Registre absent";
  case REPTILE_INCIDENT_DIMENSION_NON_CONFORM:
    return "Dimensions insuffisantes";
  case REPTILE_INCIDENT_EDUCATION_MISSING:
    return "Pédagogie manquante";
  case REPTILE_INCIDENT_AUDIT_LOCK:
    return "Blocage administratif";
  default:
    return "N/C";
  }
}

static void populate_species_options(void) {
  size_t offset = 0U;
  species_option_count = 0U;
  species_options_buffer[0] = '\0';
  for (int id = 0; id < REPTILE_SPECIES_COUNT; ++id) {
    const species_profile_t *profile =
        reptile_species_get((reptile_species_id_t)id);
    if (!profile || profile->name[0] == '\0') {
      continue;
    }
    if (species_option_count >= REPTILE_SPECIES_COUNT) {
      break;
    }
    size_t name_len = strnlen(profile->name, sizeof(profile->name));
    if (name_len == 0U) {
      continue;
    }
    if (offset != 0U) {
      if (offset >= sizeof(species_options_buffer) - 1U) {
        break;
      }
      species_options_buffer[offset++] = '\n';
    }
    size_t remaining = sizeof(species_options_buffer) - offset - 1U;
    if (name_len > remaining) {
      name_len = remaining;
    }
    memcpy(&species_options_buffer[offset], profile->name, name_len);
    offset += name_len;
    species_options_buffer[offset] = '\0';
    species_option_ids[species_option_count++] = (reptile_species_id_t)id;
  }
}

static int find_species_option_index(reptile_species_id_t id) {
  for (uint32_t i = 0; i < species_option_count; ++i) {
    if (species_option_ids[i] == id) {
      return (int)i;
    }
  }
  return -1;
}

static int find_option_index(const char *options, const char *value) {
  if (!options || !value)
    return -1;
  int index = 0;
  const char *start = options;
  while (*start) {
    const char *end = strchr(start, '\n');
    size_t len = end ? (size_t)(end - start) : strlen(start);
    if (strncmp(start, value, len) == 0 && strlen(value) == len) {
      return index;
    }
    index++;
    if (!end)
      break;
    start = end + 1;
  }
  return -1;
}

static void dropdown_select_index(lv_obj_t *dd, uint32_t idx) {
  if (!dd)
    return;
  lv_dropdown_set_selected_highlight(dd, true);
  lv_dropdown_set_text(dd, NULL);
  lv_dropdown_set_selected(dd, idx);
}

static void dropdown_select_none(lv_obj_t *dd) {
  if (!dd)
    return;
  lv_dropdown_set_selected_highlight(dd, false);
  lv_dropdown_set_text(dd, "");
#if defined(LV_DROPDOWN_SELECTED_NONE)
  lv_dropdown_set_selected(dd, LV_DROPDOWN_SELECTED_NONE);
#endif
}

static int find_size_option(float length_cm, float width_cm, float height_cm) {
  static const struct {
    float l;
    float w;
    float h;
  } kSizes[] = {
      {90.f, 45.f, 45.f},
      {120.f, 60.f, 60.f},
      {180.f, 90.f, 60.f},
      {200.f, 100.f, 60.f},
  };
  for (int i = 0; i < (int)(sizeof(kSizes) / sizeof(kSizes[0])); ++i) {
    if (fabsf(length_cm - kSizes[i].l) < 1.0f &&
        fabsf(width_cm - kSizes[i].w) < 1.0f &&
        fabsf(height_cm - kSizes[i].h) < 1.0f) {
      return i;
    }
  }
  return -1;
}

static void load_dropdown_value(lv_obj_t *dd, const char *options,
                                const char *value) {
  if (!dd)
    return;
  int idx = find_option_index(options, value);
  if (idx >= 0) {
    dropdown_select_index(dd, (uint32_t)idx);
  } else {
    dropdown_select_none(dd);
  }
}
