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

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/param.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(lv_font_montserrat_20);
LV_FONT_DECLARE(lv_font_montserrat_16);

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
static lv_obj_t *menu_status_label;
static lv_obj_t *table_terrariums;
static lv_obj_t *label_cash;
static lv_obj_t *label_cycle;
static lv_obj_t *label_alerts;
static lv_obj_t *label_inventory;
static lv_obj_t *sleep_switch;
static lv_obj_t *overview_status_icon;
static lv_obj_t *detail_title;
static lv_obj_t *detail_env_table;
static lv_obj_t *detail_status_label;
static lv_obj_t *detail_status_icon;
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

static lv_style_t style_title;
static lv_style_t style_table_header;
static lv_style_t style_cell_selected;
static lv_style_t style_value;
static lv_style_t style_overview_cell;

static lv_timer_t *facility_timer;
static uint32_t last_tick_ms;
static uint32_t autosave_ms;
static int64_t prev_income_snapshot;
static int64_t prev_expense_snapshot;
static uint32_t selected_terrarium;
static bool s_game_active;

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

static const lv_image_dsc_t *icon_currency = &gImage_currency_card;

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
static void table_event_cb(lv_event_t *e);
static void nav_button_event_cb(lv_event_t *e);
static void species_dropdown_event_cb(lv_event_t *e);
static void config_dropdown_event_cb(lv_event_t *e);
static void add_certificate_event_cb(lv_event_t *e);
static void inventory_button_event_cb(lv_event_t *e);
static void save_slot_event_cb(lv_event_t *e);
static void save_action_event_cb(lv_event_t *e);
static void menu_button_event_cb(lv_event_t *e);
static void simulation_new_game_event_cb(lv_event_t *e);
static void simulation_resume_event_cb(lv_event_t *e);
static void simulation_settings_event_cb(lv_event_t *e);
static void sleep_switch_event_cb(lv_event_t *e);
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

static void init_styles(void) {
  lv_style_init(&style_title);
  lv_style_set_text_font(&style_title, &lv_font_montserrat_24);
  lv_style_set_text_color(&style_title, lv_color_hex(0x2E3A59));

  lv_style_init(&style_table_header);
  lv_style_set_bg_color(&style_table_header, lv_palette_lighten(LV_PALETTE_GREY, 1));
  lv_style_set_border_color(&style_table_header, lv_palette_main(LV_PALETTE_GREY));
  lv_style_set_border_width(&style_table_header, 1);
  lv_style_set_text_font(&style_table_header, &lv_font_montserrat_20);
  lv_style_set_pad_all(&style_table_header, 6);

  lv_style_init(&style_cell_selected);
  lv_style_set_bg_color(&style_cell_selected, lv_palette_main(LV_PALETTE_BLUE));
  lv_style_set_text_color(&style_cell_selected, lv_color_white());

  lv_style_init(&style_value);
  lv_style_set_text_font(&style_value, &lv_font_montserrat_20);

  lv_style_init(&style_overview_cell);
  lv_style_set_text_font(&style_overview_cell, &lv_font_montserrat_16);
  lv_style_set_pad_all(&style_overview_cell, 4);
  lv_style_set_text_line_space(&style_overview_cell, 2);
  lv_style_set_text_align(&style_overview_cell, LV_TEXT_ALIGN_CENTER);
}

static void destroy_styles(void) {
  lv_style_reset(&style_title);
  lv_style_reset(&style_table_header);
  lv_style_reset(&style_cell_selected);
  lv_style_reset(&style_value);
  lv_style_reset(&style_overview_cell);
}

static void simulation_set_status(const char *fmt, ...) {
  if (!menu_status_label || !fmt)
    return;
  char buffer[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  lv_label_set_text(menu_status_label, buffer);
}

static void simulation_apply_active_slot(const char *slot) {
  const char *effective = (slot && slot[0] != '\0') ? slot : "slot_a";
  snprintf(g_facility.slot, sizeof(g_facility.slot), "%s", effective);
  snprintf(s_active_slot, sizeof(s_active_slot), "%s", g_facility.slot);
  simulation_sync_slot_dropdowns();
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
  lv_obj_clear_flag(screen_simulation_menu, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(screen_simulation_menu);
  lv_obj_add_style(title, &style_title, 0);
  lv_label_set_text(title, "Simulation reptiles");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

  lv_obj_t *slot_label = lv_label_create(screen_simulation_menu);
  lv_obj_add_style(slot_label, &style_value, 0);
  lv_label_set_text(slot_label, "Slot de sauvegarde");
  lv_obj_align(slot_label, LV_ALIGN_TOP_MID, 0, 80);

  menu_slot_dropdown = lv_dropdown_create(screen_simulation_menu);
  lv_dropdown_set_options(menu_slot_dropdown, slot_options);
  lv_obj_set_width(menu_slot_dropdown, 220);
  lv_obj_align(menu_slot_dropdown, LV_ALIGN_TOP_MID, 0, 120);

  lv_obj_t *btn_new = lv_btn_create(screen_simulation_menu);
  lv_obj_set_size(btn_new, 240, 54);
  lv_obj_align(btn_new, LV_ALIGN_CENTER, 0, -40);
  lv_obj_add_event_cb(btn_new, simulation_new_game_event_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_t *lbl_new = lv_label_create(btn_new);
  lv_label_set_text(lbl_new, "Nouvelle partie");
  lv_obj_center(lbl_new);

  lv_obj_t *btn_resume = lv_btn_create(screen_simulation_menu);
  lv_obj_set_size(btn_resume, 240, 54);
  lv_obj_align(btn_resume, LV_ALIGN_CENTER, 0, 30);
  lv_obj_add_event_cb(btn_resume, simulation_resume_event_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_t *lbl_resume = lv_label_create(btn_resume);
  lv_label_set_text(lbl_resume, "Reprendre");
  lv_obj_center(lbl_resume);

  lv_obj_t *btn_settings = lv_btn_create(screen_simulation_menu);
  lv_obj_set_size(btn_settings, 220, 48);
  lv_obj_align(btn_settings, LV_ALIGN_CENTER, 0, 100);
  lv_obj_add_event_cb(btn_settings, simulation_settings_event_cb,
                      LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_settings = lv_label_create(btn_settings);
  lv_label_set_text(lbl_settings, "Paramètres");
  lv_obj_center(lbl_settings);

  lv_obj_t *btn_main_menu = lv_btn_create(screen_simulation_menu);
  lv_obj_set_size(btn_main_menu, 220, 48);
  lv_obj_align(btn_main_menu, LV_ALIGN_BOTTOM_LEFT, 20, -20);
  lv_obj_add_event_cb(btn_main_menu, menu_button_event_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_t *lbl_main_menu = lv_label_create(btn_main_menu);
  lv_label_set_text(lbl_main_menu, "Menu principal");
  lv_obj_center(lbl_main_menu);

  menu_status_label = lv_label_create(screen_simulation_menu);
  lv_obj_add_style(menu_status_label, &style_value, 0);
  lv_obj_align(menu_status_label, LV_ALIGN_BOTTOM_RIGHT, -20, -20);

  simulation_sync_slot_dropdowns();
  simulation_set_status("Slot actif: %s", g_facility.slot);
}

static void build_overview_screen(void) {
  screen_overview = lv_obj_create(NULL);
  lv_obj_clear_flag(screen_overview, LV_OBJ_FLAG_SCROLLABLE);

  table_terrariums = lv_table_create(screen_overview);
  lv_obj_set_size(table_terrariums, 600, 360);
  lv_obj_align(table_terrariums, LV_ALIGN_TOP_LEFT, 10, 10);
  lv_table_set_column_count(table_terrariums, TERRARIUM_GRID_SIZE);
  lv_table_set_row_count(table_terrariums, TERRARIUM_GRID_SIZE);
  lv_obj_add_style(table_terrariums, &style_table_header,
                   LV_PART_ITEMS | LV_STATE_DEFAULT);
  lv_obj_add_style(table_terrariums, &style_overview_cell,
                   LV_PART_ITEMS | LV_STATE_DEFAULT);
  lv_obj_add_style(table_terrariums, &style_cell_selected,
                   LV_PART_ITEMS | LV_STATE_USER_1);
  lv_obj_add_event_cb(table_terrariums, table_event_cb, LV_EVENT_VALUE_CHANGED,
                      NULL);

  for (uint32_t col = 0; col < TERRARIUM_GRID_SIZE; ++col) {
    lv_table_set_col_width(table_terrariums, col, 120);
  }

  lv_obj_t *icon = lv_img_create(screen_overview);
  lv_img_set_src(icon, icon_currency);
  lv_obj_align(icon, LV_ALIGN_TOP_RIGHT, -20, 10);

  label_cash = lv_label_create(screen_overview);
  lv_obj_add_style(label_cash, &style_title, 0);
  lv_obj_align_to(label_cash, icon, LV_ALIGN_OUT_BOTTOM_RIGHT, -40, 10);

  label_cycle = lv_label_create(screen_overview);
  lv_obj_add_style(label_cycle, &style_value, 0);
  lv_obj_align(label_cycle, LV_ALIGN_TOP_RIGHT, -20, 120);

  label_alerts = lv_label_create(screen_overview);
  lv_obj_add_style(label_alerts, &style_value, 0);
  lv_obj_align(label_alerts, LV_ALIGN_TOP_RIGHT, -20, 170);

  overview_status_icon = lv_img_create(screen_overview);
  lv_img_set_src(overview_status_icon, &gImage_terrarium_ok);
  lv_obj_align_to(overview_status_icon, label_alerts, LV_ALIGN_OUT_LEFT_MID, -10,
                  0);

  label_inventory = lv_label_create(screen_overview);
  lv_obj_add_style(label_inventory, &style_value, 0);
  lv_obj_align(label_inventory, LV_ALIGN_TOP_RIGHT, -20, 220);

  lv_obj_t *btn_detail = lv_btn_create(screen_overview);
  lv_obj_set_size(btn_detail, 180, 48);
  lv_obj_align(btn_detail, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  lv_obj_add_event_cb(btn_detail, nav_button_event_cb, LV_EVENT_CLICKED,
                      screen_detail);
  lv_obj_t *lbl_detail = lv_label_create(btn_detail);
  lv_label_set_text(lbl_detail, "Détails terrarium");
  lv_obj_center(lbl_detail);

  lv_obj_t *btn_economy = lv_btn_create(screen_overview);
  lv_obj_set_size(btn_economy, 180, 48);
  lv_obj_align(btn_economy, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_add_event_cb(btn_economy, nav_button_event_cb, LV_EVENT_CLICKED,
                      screen_economy);
  lv_obj_t *lbl_economy = lv_label_create(btn_economy);
  lv_label_set_text(lbl_economy, "Économie");
  lv_obj_center(lbl_economy);

  lv_obj_t *btn_save = lv_btn_create(screen_overview);
  lv_obj_set_size(btn_save, 180, 48);
  lv_obj_align(btn_save, LV_ALIGN_BOTTOM_RIGHT, -210, -10);
  lv_obj_add_event_cb(btn_save, nav_button_event_cb, LV_EVENT_CLICKED,
                      screen_save);
  lv_obj_t *lbl_save = lv_label_create(btn_save);
  lv_label_set_text(lbl_save, "Sauvegardes");
  lv_obj_center(lbl_save);

  lv_obj_t *btn_reg = lv_btn_create(screen_overview);
  lv_obj_set_size(btn_reg, 180, 48);
  lv_obj_align(btn_reg, LV_ALIGN_BOTTOM_RIGHT, -410, -10);
  lv_obj_add_event_cb(btn_reg, nav_button_event_cb, LV_EVENT_CLICKED,
                      screen_regulations);
  lv_obj_t *lbl_reg = lv_label_create(btn_reg);
  lv_label_set_text(lbl_reg, "Obligations");
  lv_obj_center(lbl_reg);

  lv_obj_t *btn_menu = lv_btn_create(screen_overview);
  lv_obj_set_size(btn_menu, 180, 48);
  lv_obj_align(btn_menu, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
  lv_obj_add_event_cb(btn_menu, nav_button_event_cb, LV_EVENT_CLICKED,
                      screen_simulation_menu);
  lv_obj_t *lbl_menu = lv_label_create(btn_menu);
  lv_label_set_text(lbl_menu, "Menu Simulation");
  lv_obj_center(lbl_menu);

  sleep_switch = lv_switch_create(screen_overview);
  lv_obj_align(sleep_switch, LV_ALIGN_BOTTOM_RIGHT, -20, -80);
  if (sleep_is_enabled()) {
    lv_obj_add_state(sleep_switch, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(sleep_switch, sleep_switch_event_cb, LV_EVENT_VALUE_CHANGED,
                      NULL);
  lv_obj_t *lbl_sleep = lv_label_create(screen_overview);
  lv_label_set_text(lbl_sleep, "Veille automatique");
  lv_obj_align_to(lbl_sleep, sleep_switch, LV_ALIGN_OUT_LEFT_MID, -10, 0);
}

static void build_detail_screen(void) {
  screen_detail = lv_obj_create(NULL);
  lv_obj_clear_flag(screen_detail, LV_OBJ_FLAG_SCROLLABLE);

  detail_title = lv_label_create(screen_detail);
  lv_obj_add_style(detail_title, &style_title, 0);
  lv_obj_align(detail_title, LV_ALIGN_TOP_LEFT, 10, 10);

  detail_env_table = lv_table_create(screen_detail);
  lv_obj_set_size(detail_env_table, 620, 260);
  lv_obj_align(detail_env_table, LV_ALIGN_TOP_LEFT, 10, 60);
  lv_table_set_column_count(detail_env_table, 2);
  lv_table_set_row_count(detail_env_table, 12);
  lv_obj_add_style(detail_env_table, &style_table_header,
                   LV_PART_ITEMS | LV_STATE_DEFAULT);

  detail_status_label = lv_label_create(screen_detail);
  lv_obj_add_style(detail_status_label, &style_value, 0);
  lv_obj_align(detail_status_label, LV_ALIGN_TOP_LEFT, 10, 330);

  detail_status_icon = lv_img_create(screen_detail);
  lv_img_set_src(detail_status_icon, &gImage_terrarium_ok);
  lv_obj_align_to(detail_status_icon, detail_status_label,
                  LV_ALIGN_OUT_LEFT_MID, -10, 0);

  populate_species_options();

  dropdown_species = lv_dropdown_create(screen_detail);
  lv_obj_set_width(dropdown_species, 260);
  lv_obj_align(dropdown_species, LV_ALIGN_TOP_RIGHT, -10, 10);
  if (species_options_buffer[0] != '\0') {
    lv_dropdown_set_options(dropdown_species, species_options_buffer);
  }
  lv_dropdown_set_text(dropdown_species, "Choisir espèce");
  lv_obj_add_event_cb(dropdown_species, species_dropdown_event_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);

  dropdown_substrate = lv_dropdown_create(screen_detail);
  lv_dropdown_set_options(dropdown_substrate, substrate_options);
  lv_obj_align(dropdown_substrate, LV_ALIGN_TOP_RIGHT, -10, 60);
  lv_obj_add_event_cb(dropdown_substrate, config_dropdown_event_cb,
                      LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)CONFIG_SUBSTRATE);

  dropdown_heating = lv_dropdown_create(screen_detail);
  lv_dropdown_set_options(dropdown_heating, heating_options);
  lv_obj_align(dropdown_heating, LV_ALIGN_TOP_RIGHT, -10, 120);
  lv_obj_add_event_cb(dropdown_heating, config_dropdown_event_cb,
                      LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)CONFIG_HEATING);

  dropdown_decor = lv_dropdown_create(screen_detail);
  lv_dropdown_set_options(dropdown_decor, decor_options);
  lv_obj_align(dropdown_decor, LV_ALIGN_TOP_RIGHT, -10, 180);
  lv_obj_add_event_cb(dropdown_decor, config_dropdown_event_cb,
                      LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)CONFIG_DECOR);

  dropdown_uv = lv_dropdown_create(screen_detail);
  lv_dropdown_set_options(dropdown_uv, uv_options);
  lv_obj_align(dropdown_uv, LV_ALIGN_TOP_RIGHT, -10, 240);
  lv_obj_add_event_cb(dropdown_uv, config_dropdown_event_cb,
                      LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)CONFIG_UV);

  dropdown_size = lv_dropdown_create(screen_detail);
  lv_dropdown_set_options(dropdown_size, size_options);
  lv_obj_align(dropdown_size, LV_ALIGN_TOP_RIGHT, -10, 300);
  lv_obj_add_event_cb(dropdown_size, config_dropdown_event_cb,
                      LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)CONFIG_SIZE);

  lv_obj_t *btn_add_cert = lv_btn_create(screen_detail);
  lv_obj_set_size(btn_add_cert, 220, 44);
  lv_obj_align(btn_add_cert, LV_ALIGN_TOP_RIGHT, -10, 360);
  lv_obj_add_event_cb(btn_add_cert, add_certificate_event_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_t *lbl_cert = lv_label_create(btn_add_cert);
  lv_label_set_text(lbl_cert, "Ajouter certificat");
  lv_obj_center(lbl_cert);

  education_switch_detail = lv_switch_create(screen_detail);
  lv_obj_align(education_switch_detail, LV_ALIGN_TOP_RIGHT, -10, 420);
  lv_obj_add_event_cb(education_switch_detail, education_switch_event_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_t *edu_label = lv_label_create(screen_detail);
  lv_label_set_text(edu_label, "Affichage pédagogique");
  lv_obj_align_to(edu_label, education_switch_detail, LV_ALIGN_OUT_LEFT_MID, -10,
                  0);

  detail_register_label = lv_label_create(screen_detail);
  lv_obj_add_style(detail_register_label, &style_value, 0);
  lv_obj_align(detail_register_label, LV_ALIGN_TOP_LEFT, 10, 360);

  register_button = lv_btn_create(screen_detail);
  lv_obj_set_size(register_button, 220, 44);
  lv_obj_align(register_button, LV_ALIGN_TOP_LEFT, 10, 400);
  lv_obj_add_event_cb(register_button, register_button_event_cb,
                      LV_EVENT_CLICKED, NULL);
  lv_obj_t *register_label = lv_label_create(register_button);
  lv_label_set_text(register_label, "Consigner la cession");
  lv_obj_center(register_label);

  detail_compliance_label = lv_label_create(screen_detail);
  lv_obj_add_style(detail_compliance_label, &style_value, 0);
  lv_obj_align(detail_compliance_label, LV_ALIGN_TOP_LEFT, 10, 440);

  detail_cert_table = lv_table_create(screen_detail);
  lv_obj_set_size(detail_cert_table, 460, 120);
  lv_obj_align(detail_cert_table, LV_ALIGN_BOTTOM_LEFT, 10, -150);
  lv_table_set_column_count(detail_cert_table, 2);
  lv_table_set_row_count(detail_cert_table, 6);
  lv_obj_add_style(detail_cert_table, &style_table_header,
                   LV_PART_ITEMS | LV_STATE_DEFAULT);

  lv_obj_t *btn_feed_stock = lv_btn_create(screen_detail);
  lv_obj_set_size(btn_feed_stock, 180, 44);
  lv_obj_align(btn_feed_stock, LV_ALIGN_BOTTOM_RIGHT, -10, -180);
  lv_obj_add_event_cb(btn_feed_stock, inventory_button_event_cb,
                      LV_EVENT_CLICKED, (void *)(uintptr_t)INVENTORY_ADD_FEED);
  lv_obj_t *lbl_feed_stock = lv_label_create(btn_feed_stock);
  lv_label_set_text(lbl_feed_stock, "+10 proies");
  lv_obj_center(lbl_feed_stock);

  lv_obj_t *btn_water_stock = lv_btn_create(screen_detail);
  lv_obj_set_size(btn_water_stock, 180, 44);
  lv_obj_align(btn_water_stock, LV_ALIGN_BOTTOM_RIGHT, -10, -130);
  lv_obj_add_event_cb(btn_water_stock, inventory_button_event_cb,
                      LV_EVENT_CLICKED, (void *)(uintptr_t)INVENTORY_ADD_WATER);
  lv_obj_t *lbl_water_stock = lv_label_create(btn_water_stock);
  lv_label_set_text(lbl_water_stock, "+20 L eau");
  lv_obj_center(lbl_water_stock);

  lv_obj_t *btn_substrate_stock = lv_btn_create(screen_detail);
  lv_obj_set_size(btn_substrate_stock, 180, 44);
  lv_obj_align(btn_substrate_stock, LV_ALIGN_BOTTOM_RIGHT, -10, -80);
  lv_obj_add_event_cb(btn_substrate_stock, inventory_button_event_cb,
                      LV_EVENT_CLICKED,
                      (void *)(uintptr_t)INVENTORY_ADD_SUBSTRATE);
  lv_obj_t *lbl_substrate = lv_label_create(btn_substrate_stock);
  lv_label_set_text(lbl_substrate, "+2 substrats");
  lv_obj_center(lbl_substrate);

  lv_obj_t *btn_uv_stock = lv_btn_create(screen_detail);
  lv_obj_set_size(btn_uv_stock, 180, 44);
  lv_obj_align(btn_uv_stock, LV_ALIGN_BOTTOM_RIGHT, -10, -30);
  lv_obj_add_event_cb(btn_uv_stock, inventory_button_event_cb, LV_EVENT_CLICKED,
                      (void *)(uintptr_t)INVENTORY_ADD_UV);
  lv_obj_t *lbl_uv = lv_label_create(btn_uv_stock);
  lv_label_set_text(lbl_uv, "+1 UV");
  lv_obj_center(lbl_uv);

  lv_obj_t *btn_decor_stock = lv_btn_create(screen_detail);
  lv_obj_set_size(btn_decor_stock, 180, 44);
  lv_obj_align(btn_decor_stock, LV_ALIGN_BOTTOM_RIGHT, -200, -30);
  lv_obj_add_event_cb(btn_decor_stock, inventory_button_event_cb,
                      LV_EVENT_CLICKED, (void *)(uintptr_t)INVENTORY_ADD_DECOR);
  lv_obj_t *lbl_decor = lv_label_create(btn_decor_stock);
  lv_label_set_text(lbl_decor, "+1 décor");
  lv_obj_center(lbl_decor);

  lv_obj_t *btn_back = lv_btn_create(screen_detail);
  lv_obj_set_size(btn_back, 160, 44);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  lv_obj_add_event_cb(btn_back, nav_button_event_cb, LV_EVENT_CLICKED,
                      screen_overview);
  lv_obj_t *lbl_back = lv_label_create(btn_back);
  lv_label_set_text(lbl_back, "Retour");
  lv_obj_center(lbl_back);

  lv_obj_t *btn_menu = lv_btn_create(screen_detail);
  lv_obj_set_size(btn_menu, 180, 44);
  lv_obj_align(btn_menu, LV_ALIGN_BOTTOM_LEFT, 190, -10);
  lv_obj_add_event_cb(btn_menu, nav_button_event_cb, LV_EVENT_CLICKED,
                      screen_simulation_menu);
  lv_obj_t *lbl_menu = lv_label_create(btn_menu);
  lv_label_set_text(lbl_menu, "Menu Simulation");
  lv_obj_center(lbl_menu);
}

static void build_economy_screen(void) {
  screen_economy = lv_obj_create(NULL);
  lv_obj_clear_flag(screen_economy, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(screen_economy);
  lv_obj_add_style(title, &style_title, 0);
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
  lv_obj_add_style(economy_table, &style_table_header,
                   LV_PART_ITEMS | LV_STATE_DEFAULT);

  economy_summary_label = lv_label_create(screen_economy);
  lv_obj_add_style(economy_summary_label, &style_value, 0);
  lv_obj_align(economy_summary_label, LV_ALIGN_BOTTOM_LEFT, 10, -10);

  lv_obj_t *btn_menu = lv_btn_create(screen_economy);
  lv_obj_set_size(btn_menu, 180, 44);
  lv_obj_align(btn_menu, LV_ALIGN_BOTTOM_LEFT, 10, -60);
  lv_obj_add_event_cb(btn_menu, nav_button_event_cb, LV_EVENT_CLICKED,
                      screen_simulation_menu);
  lv_obj_t *lbl_menu = lv_label_create(btn_menu);
  lv_label_set_text(lbl_menu, "Menu Simulation");
  lv_obj_center(lbl_menu);

  lv_obj_t *btn_back = lv_btn_create(screen_economy);
  lv_obj_set_size(btn_back, 160, 44);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
  lv_obj_add_event_cb(btn_back, nav_button_event_cb, LV_EVENT_CLICKED,
                      screen_overview);
  lv_obj_t *lbl_back = lv_label_create(btn_back);
  lv_label_set_text(lbl_back, "Retour");
  lv_obj_center(lbl_back);
}

static void build_save_screen(void) {
  screen_save = lv_obj_create(NULL);
  lv_obj_clear_flag(screen_save, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(screen_save);
  lv_obj_add_style(title, &style_title, 0);
  lv_label_set_text(title, "Gestion des sauvegardes");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 10);

  save_slot_dropdown = lv_dropdown_create(screen_save);
  lv_dropdown_set_options(save_slot_dropdown, slot_options);
  lv_obj_align(save_slot_dropdown, LV_ALIGN_TOP_LEFT, 10, 60);
  lv_obj_add_event_cb(save_slot_dropdown, save_slot_event_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);

  save_status_label = lv_label_create(screen_save);
  lv_obj_add_style(save_status_label, &style_value, 0);
  lv_obj_align(save_status_label, LV_ALIGN_TOP_LEFT, 10, 110);

  lv_obj_t *btn_save = lv_btn_create(screen_save);
  lv_obj_set_size(btn_save, 200, 48);
  lv_obj_align(btn_save, LV_ALIGN_TOP_LEFT, 10, 160);
  lv_obj_add_event_cb(btn_save, save_action_event_cb, LV_EVENT_CLICKED,
                      (void *)(uintptr_t)SAVE_ACTION_SAVE);
  lv_obj_t *lbl_save = lv_label_create(btn_save);
  lv_label_set_text(lbl_save, "Sauvegarder maintenant");
  lv_obj_center(lbl_save);

  lv_obj_t *btn_load = lv_btn_create(screen_save);
  lv_obj_set_size(btn_load, 200, 48);
  lv_obj_align(btn_load, LV_ALIGN_TOP_LEFT, 10, 220);
  lv_obj_add_event_cb(btn_load, save_action_event_cb, LV_EVENT_CLICKED,
                      (void *)(uintptr_t)SAVE_ACTION_LOAD);
  lv_obj_t *lbl_load = lv_label_create(btn_load);
  lv_label_set_text(lbl_load, "Charger le slot");
  lv_obj_center(lbl_load);

  lv_obj_t *btn_reset = lv_btn_create(screen_save);
  lv_obj_set_size(btn_reset, 220, 48);
  lv_obj_align(btn_reset, LV_ALIGN_TOP_LEFT, 10, 280);
  lv_obj_add_event_cb(btn_reset, save_action_event_cb, LV_EVENT_CLICKED,
                      (void *)(uintptr_t)SAVE_ACTION_RESET_STATS);
  lv_obj_t *lbl_reset = lv_label_create(btn_reset);
  lv_label_set_text(lbl_reset, "Réinitialiser les compteurs");
  lv_obj_center(lbl_reset);

  lv_obj_t *btn_menu = lv_btn_create(screen_save);
  lv_obj_set_size(btn_menu, 180, 48);
  lv_obj_align(btn_menu, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  lv_obj_add_event_cb(btn_menu, nav_button_event_cb, LV_EVENT_CLICKED,
                      screen_simulation_menu);
  lv_obj_t *lbl_menu = lv_label_create(btn_menu);
  lv_label_set_text(lbl_menu, "Menu Simulation");
  lv_obj_center(lbl_menu);

  lv_obj_t *btn_back = lv_btn_create(screen_save);
  lv_obj_set_size(btn_back, 160, 48);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
  lv_obj_add_event_cb(btn_back, nav_button_event_cb, LV_EVENT_CLICKED,
                      screen_overview);
  lv_obj_t *lbl_back = lv_label_create(btn_back);
  lv_label_set_text(lbl_back, "Retour");
  lv_obj_center(lbl_back);
}

static void build_regulation_screen(void) {
  screen_regulations = lv_obj_create(NULL);
  lv_obj_clear_flag(screen_regulations, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(screen_regulations);
  lv_obj_add_style(title, &style_title, 0);
  lv_label_set_text(title, "Référentiel réglementaire");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 10);

  regulations_table = lv_table_create(screen_regulations);
  lv_obj_set_size(regulations_table, 700, 220);
  lv_obj_align(regulations_table, LV_ALIGN_TOP_LEFT, 10, 60);
  lv_table_set_column_count(regulations_table, 4);
  lv_table_set_row_count(regulations_table, 1);
  lv_obj_add_style(regulations_table, &style_table_header,
                   LV_PART_ITEMS | LV_STATE_DEFAULT);

  regulations_alert_table = lv_table_create(screen_regulations);
  lv_obj_set_size(regulations_alert_table, 700, 160);
  lv_obj_align(regulations_alert_table, LV_ALIGN_TOP_LEFT, 10, 300);
  lv_table_set_column_count(regulations_alert_table, 3);
  lv_table_set_row_count(regulations_alert_table, 1);
  lv_obj_add_style(regulations_alert_table, &style_table_header,
                   LV_PART_ITEMS | LV_STATE_DEFAULT);

  regulations_summary_label = lv_label_create(screen_regulations);
  lv_obj_add_style(regulations_summary_label, &style_value, 0);
  lv_obj_align(regulations_summary_label, LV_ALIGN_BOTTOM_LEFT, 10, -80);

  lv_obj_t *btn_export = lv_btn_create(screen_regulations);
  lv_obj_set_size(btn_export, 240, 48);
  lv_obj_align(btn_export, LV_ALIGN_BOTTOM_LEFT, 10, -30);
  lv_obj_add_event_cb(btn_export, export_report_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_export = lv_label_create(btn_export);
  lv_label_set_text(lbl_export, "Exporter rapport microSD");
  lv_obj_center(lbl_export);

  regulations_export_label = lv_label_create(screen_regulations);
  lv_obj_add_style(regulations_export_label, &style_value, 0);
  lv_label_set_text(regulations_export_label, "Aucun export réalisé");
  lv_obj_align(regulations_export_label, LV_ALIGN_BOTTOM_LEFT, 260, -30);

  lv_obj_t *btn_menu = lv_btn_create(screen_regulations);
  lv_obj_set_size(btn_menu, 180, 48);
  lv_obj_align(btn_menu, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  lv_obj_add_event_cb(btn_menu, nav_button_event_cb, LV_EVENT_CLICKED,
                      screen_simulation_menu);
  lv_obj_t *lbl_menu = lv_label_create(btn_menu);
  lv_label_set_text(lbl_menu, "Menu Simulation");
  lv_obj_center(lbl_menu);

  lv_obj_t *btn_back = lv_btn_create(screen_regulations);
  lv_obj_set_size(btn_back, 160, 48);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
  lv_obj_add_event_cb(btn_back, nav_button_event_cb, LV_EVENT_CLICKED,
                      screen_overview);
  lv_obj_t *lbl_back = lv_label_create(btn_back);
  lv_label_set_text(lbl_back, "Retour");
  lv_obj_center(lbl_back);
}

void reptile_game_start(esp_lcd_panel_handle_t panel,
                        esp_lcd_touch_handle_t touch) {
  (void)panel;
  (void)touch;
  s_game_active = true;
  init_styles();

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
  menu_status_label = NULL;
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
  destroy_styles();
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

static void update_table_cell(uint32_t index, uint32_t row, uint32_t col) {
  char buffer[96];
  if (index >= g_facility.terrarium_count) {
    snprintf(buffer, sizeof(buffer), "T%02" PRIu32 "\n--\n--\n%s",
             (uint32_t)(index + 1U), LV_SYMBOL_MINUS);
    lv_table_set_cell_value(table_terrariums, row, col, buffer);
    return;
  }
  const terrarium_t *terrarium =
      reptile_facility_get_terrarium_const(&g_facility, (uint8_t)index);
  if (!terrarium || !terrarium->occupied) {
    snprintf(buffer, sizeof(buffer), "T%02" PRIu32 "\nDisponible\nLibre\n%s",
             (uint32_t)(index + 1U), LV_SYMBOL_MINUS);
    lv_table_set_cell_value(table_terrariums, row, col, buffer);
    return;
  }

  const char *stage = growth_stage_to_string(terrarium->stage);
  const char *alert =
      (terrarium->pathology != REPTILE_PATHOLOGY_NONE ||
       terrarium->incident != REPTILE_INCIDENT_NONE)
          ? LV_SYMBOL_WARNING
          : LV_SYMBOL_OK;
  snprintf(buffer, sizeof(buffer), "T%02" PRIu32 "\n%s\n%s\n%s",
           (uint32_t)(index + 1U), terrarium->species.name, stage, alert);
  lv_table_set_cell_value(table_terrariums, row, col, buffer);
}

static void set_overview_cell_ctrl(uint32_t row, uint32_t col, bool selected) {
#if LVGL_VERSION_MAJOR > 9 || LV_VERSION_CHECK(9, 4, 0)
  lv_table_set_cell_ctrl(table_terrariums, row, col,
                         LV_TABLE_CELL_CTRL_CUSTOM_1, selected);
#else
  if (selected) {
    lv_table_set_cell_ctrl(table_terrariums, row, col,
                           LV_TABLE_CELL_CTRL_CUSTOM_1);
  } else {
    lv_table_clear_cell_ctrl(table_terrariums, row, col,
                             LV_TABLE_CELL_CTRL_CUSTOM_1);
  }
#endif
}

static void update_overview_screen(void) {
  if (!table_terrariums)
    return;

  for (uint32_t row = 0; row < TERRARIUM_GRID_SIZE; ++row) {
    for (uint32_t col = 0; col < TERRARIUM_GRID_SIZE; ++col) {
      uint32_t index = row * TERRARIUM_GRID_SIZE + col;
      update_table_cell(index, row, col);
      set_overview_cell_ctrl(row, col, index == selected_terrarium);
    }
  }

  lv_label_set_text_fmt(label_cash, "Trésorerie: %.2f €",
                        (double)g_facility.economy.cash_cents / 100.0);
  const reptile_day_cycle_t *cycle = &g_facility.cycle;
  uint32_t elapsed_ms = cycle->elapsed_in_phase_ms;
  lv_label_set_text_fmt(label_cycle,
                        "%s %02u:%02u | Jour %" PRIu32,
                        cycle->is_daytime ? "Jour" : "Nuit",
                        (unsigned)(elapsed_ms / 60000U),
                        (unsigned)((elapsed_ms / 1000U) % 60U),
                        g_facility.economy.days_elapsed);
  lv_label_set_text_fmt(label_alerts,
                        "Alertes: %" PRIu32 " (pathologies %" PRIu32
                        " / conformité %" PRIu32 ")",
                        g_facility.alerts_active, g_facility.pathology_active,
                        g_facility.compliance_alerts);
  if (overview_status_icon) {
    lv_img_set_src(overview_status_icon, g_facility.alerts_active
                                            ? &gImage_terrarium_alert
                                            : &gImage_terrarium_ok);
  }
  lv_label_set_text_fmt(
      label_inventory,
      "Stocks - Proies:%" PRIu32 " | Eau:%" PRIu32 " L | Substrat:%" PRIu32
      " | UV:%" PRIu32 " | Décor:%" PRIu32,
      g_facility.inventory.feeders, g_facility.inventory.water_reserve_l,
      g_facility.inventory.substrate_bags, g_facility.inventory.uv_bulbs,
      g_facility.inventory.decor_kits);

  if (sleep_switch) {
    if (sleep_is_enabled()) {
      lv_obj_add_state(sleep_switch, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(sleep_switch, LV_STATE_CHECKED);
    }
  }
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
    lv_label_set_text(detail_title, "Terrarium disponible");
    lv_label_set_text(detail_status_label,
                      "Attribuer une espèce pour configurer ce terrarium");
    if (detail_status_icon) {
      lv_img_set_src(detail_status_icon, &gImage_terrarium_ok);
    }
    if (detail_env_table) {
      static const char *kLabels[12] = {
          "Température",    "Humidité",   "Index UV",      "Satiété",
          "Hydratation",    "Croissance", "Poids",        "Stade",
          "Pathologie",     "Incident",   "Dimensions",    "Obligations",
      };
      for (uint32_t row = 0; row < 12U; ++row) {
        lv_table_set_cell_value(detail_env_table, row, 0, kLabels[row]);
        lv_table_set_cell_value(detail_env_table, row, 1, "-");
      }
    }
    if (detail_cert_table) {
      lv_table_set_row_count(detail_cert_table, 2);
      lv_table_set_cell_value(detail_cert_table, 0, 0, "Identifiant");
      lv_table_set_cell_value(detail_cert_table, 0, 1, "Échéance");
      lv_table_set_cell_value(detail_cert_table, 1, 0, "-");
      lv_table_set_cell_value(detail_cert_table, 1, 1,
                              "Aucun certificat enregistré");
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
    return;
  }
  const species_profile_t *profile = &terrarium->species;
  lv_label_set_text_fmt(detail_title, "T%02" PRIu32 " - %s (%s)",
                        (uint32_t)(selected_terrarium + 1U),
                        terrarium->nickname, profile->name);

  lv_table_set_cell_value(detail_env_table, 0, 0, "Température");
  lv_table_set_cell_value_fmt(detail_env_table, 0, 1, "%.1f °C (%.1f-%.1f)",
                              terrarium->temperature_c, profile->day_temp_min,
                              profile->day_temp_max);
  lv_table_set_cell_value(detail_env_table, 1, 0, "Humidité");
  lv_table_set_cell_value_fmt(detail_env_table, 1, 1, "%.0f %% (%.0f-%.0f)",
                              terrarium->humidity_pct, profile->humidity_min,
                              profile->humidity_max);
  lv_table_set_cell_value(detail_env_table, 2, 0, "Index UV");
  lv_table_set_cell_value_fmt(detail_env_table, 2, 1, "%.2f (%.1f-%.1f)",
                              terrarium->uv_index, profile->uv_min,
                              profile->uv_max);
  lv_table_set_cell_value(detail_env_table, 3, 0, "Satiété");
  lv_table_set_cell_value_fmt(detail_env_table, 3, 1, "%.0f %%",
                              terrarium->satiety * 100.0f);
  lv_table_set_cell_value(detail_env_table, 4, 0, "Hydratation");
  lv_table_set_cell_value_fmt(detail_env_table, 4, 1, "%.0f %%",
                              terrarium->hydration * 100.0f);
  lv_table_set_cell_value(detail_env_table, 5, 0, "Croissance");
  lv_table_set_cell_value_fmt(detail_env_table, 5, 1, "%.0f %%",
                              terrarium->growth * 100.0f);
  lv_table_set_cell_value(detail_env_table, 6, 0, "Poids");
  lv_table_set_cell_value_fmt(detail_env_table, 6, 1, "%.0f g",
                              terrarium->weight_g);
  lv_table_set_cell_value(detail_env_table, 7, 0, "Stade");
  lv_table_set_cell_value(detail_env_table, 7, 1,
                          growth_stage_to_string(terrarium->stage));
  lv_table_set_cell_value(detail_env_table, 8, 0, "Pathologie");
  lv_table_set_cell_value(detail_env_table, 8, 1,
                          pathology_to_string(terrarium->pathology));
  lv_table_set_cell_value(detail_env_table, 9, 0, "Incident");
  lv_table_set_cell_value(detail_env_table, 9, 1,
                          incident_to_string(terrarium->incident));
  const regulation_rule_t *rule =
      regulations_get_rule((int)terrarium->species.id);
  lv_table_set_cell_value(detail_env_table, 10, 0, "Dimensions");
  if (rule) {
    lv_table_set_cell_value_fmt(detail_env_table, 10, 1,
                                "%.0fx%.0fx%.0f cm / min %.0fx%.0fx%.0f cm",
                                terrarium->config.length_cm,
                                terrarium->config.width_cm,
                                terrarium->config.height_cm, rule->min_length_cm,
                                rule->min_width_cm, rule->min_height_cm);
  } else {
    lv_table_set_cell_value_fmt(detail_env_table, 10, 1,
                                "%.0fx%.0fx%.0f cm", terrarium->config.length_cm,
                                terrarium->config.width_cm,
                                terrarium->config.height_cm);
  }
  lv_table_set_cell_value(detail_env_table, 11, 0, "Obligations");
  const char *cert_status = "Certificat absent";
  if (terrarium->incident == REPTILE_INCIDENT_CERTIFICATE_EXPIRED) {
    cert_status = "Certificat expiré";
  } else if (terrarium->incident == REPTILE_INCIDENT_CERTIFICATE_MISSING) {
    cert_status = "Certificat manquant";
  } else if (terrarium->certificate_count > 0) {
    cert_status = "Certificat enregistré";
  }
  const char *register_status =
      terrarium->config.register_completed ? "Registre OK" : "Registre à consigner";
  lv_table_set_cell_value_fmt(detail_env_table, 11, 1, "%s | %s", cert_status,
                              register_status);

  lv_label_set_text_fmt(
      detail_status_label,
      "Substrat: %s | Chauffage: %s | Décor: %s | UV: %s",
      terrarium->config.substrate, terrarium->config.heating,
      terrarium->config.decor, terrarium->config.uv_setup);
  if (detail_status_icon) {
    bool warn = terrarium->pathology != REPTILE_PATHOLOGY_NONE ||
                terrarium->incident != REPTILE_INCIDENT_NONE;
    lv_img_set_src(detail_status_icon,
                   warn ? &gImage_terrarium_alert : &gImage_terrarium_ok);
  }

  load_dropdown_value(dropdown_substrate, substrate_options,
                      terrarium->config.substrate);
  load_dropdown_value(dropdown_heating, heating_options,
                      terrarium->config.heating);
  load_dropdown_value(dropdown_decor, decor_options, terrarium->config.decor);
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
      lv_label_set_text(detail_register_label, "Registre non renseigné");
    }
  }
  if (register_button) {
    lv_obj_t *label = lv_obj_get_child(register_button, 0);
    if (label) {
      lv_label_set_text(label, terrarium->config.register_completed
                                   ? "Annuler la cession"
                                   : "Consigner la cession");
    }
  }
  if (detail_compliance_label) {
    lv_label_set_text(detail_compliance_label,
                      terrarium->compliance_message[0] != '\0'
                          ? terrarium->compliance_message
                          : "Aucune remarque");
  }

  update_certificate_table();
}

static void update_certificate_table(void) {
  if (!detail_cert_table)
    return;
  const terrarium_t *terrarium =
      reptile_facility_get_terrarium_const(&g_facility,
                                           (uint8_t)selected_terrarium);
  if (!terrarium || !terrarium->occupied) {
    return;
  }
  lv_table_set_row_count(detail_cert_table,
                       MAX(2U, terrarium->certificate_count + 1U));
  lv_table_set_cell_value(detail_cert_table, 0, 0, "Identifiant");
  lv_table_set_cell_value(detail_cert_table, 0, 1, "Échéance");
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

static void table_event_cb(lv_event_t *e) {
  lv_obj_t *table = lv_event_get_target(e);
  uint32_t row = LV_TABLE_CELL_NONE;
  uint32_t col = LV_TABLE_CELL_NONE;
  lv_table_get_selected_cell(table, &row, &col);
  if (row == LV_TABLE_CELL_NONE || col == LV_TABLE_CELL_NONE)
    return;

  uint32_t index = row * TERRARIUM_GRID_SIZE + col;
  if (index >= g_facility.terrarium_count)
    return;

  if (index != selected_terrarium) {
    selected_terrarium = index;
    update_overview_screen();
    update_detail_screen();
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
