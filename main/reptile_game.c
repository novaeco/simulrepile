#include "reptile_game.h"
#include "can.h"
#include "image.h"
#include "lvgl_port.h"
#include "terrarium_manager.h"
#include "species_db.h"
#include "sleep.h"
#include "logging.h"
#include "esp_log.h"
#include "esp_err.h"
#include "game_mode.h"
#include "sd.h"
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

LV_FONT_DECLARE(lv_font_montserrat_24);

static lv_style_t style_font24;
static lv_style_t style_tile_selected;
static lv_obj_t *screen_main;
static lv_obj_t *screen_stats;
static lv_obj_t *terrarium_container;
static lv_obj_t *bar_faim;
static lv_obj_t *bar_eau;
static lv_obj_t *bar_temp;
static lv_obj_t *bar_humeur;
static lv_obj_t *bar_humidite;
static lv_obj_t *bar_uv;
static lv_obj_t *img_reptile;
static bool sprite_is_happy;
static bool s_game_active;
static lv_obj_t *label_stat_faim;
static lv_obj_t *label_stat_eau;
static lv_obj_t *label_stat_temp;
static lv_obj_t *label_stat_humeur;
static lv_obj_t *label_stat_humidite;
static lv_obj_t *label_stat_uv;
static lv_obj_t *lbl_sleep;
static lv_obj_t *label_terrarium_name;
static lv_obj_t *label_species_name;
static lv_obj_t *label_species_legal;
static lv_obj_t *label_species_cert;
static lv_obj_t *btn_species_select;
static terrarium_t *s_active_terrarium;

typedef struct {
  lv_obj_t *button;
  lv_obj_t *substrate_icon;
  lv_obj_t *decor_icon;
  lv_obj_t *name_label;
  lv_obj_t *status_label;
} terrarium_tile_ui_t;

static terrarium_tile_ui_t s_tiles[TERRARIUM_MANAGER_MAX_TERRARIUMS];
extern lv_obj_t *menu_screen;

#define REPTILE_UPDATE_PERIOD_MS 1000

static lv_timer_t *life_timer;
static lv_timer_t *action_timer;

static const char *TAG = "reptile_game";

#define REPTILE_SAVE_INDEX_FILE "save_index.cfg"
#define REPTILE_SAVE_PREFIX "reptile_save_"
#define REPTILE_SAVE_EXT ".bin"

typedef enum {
  REPTILE_START_AUTO = 0,
  REPTILE_START_NEW,
  REPTILE_START_RESUME,
} reptile_start_mode_t;

static reptile_start_mode_t s_start_mode = REPTILE_START_AUTO;
static bool s_slot_override_pending;
static char s_slot_override[REPTILE_SLOT_NAME_MAX];
static lv_obj_t *modal_species;
static lv_obj_t *list_species;
static lv_obj_t *label_species_details;
static lv_obj_t *btn_species_confirm;
static const species_db_entry_t *species_candidate;


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
static void back_btn_event_cb(lv_event_t *e);
static void action_btn_event_cb(lv_event_t *e);
static void sleep_btn_event_cb(lv_event_t *e);
static void menu_btn_event_cb(lv_event_t *e);
static void terrarium_tile_event_cb(lv_event_t *e);
static void ui_update_main(void);
static void ui_update_stats(void);
static void show_event_popup(reptile_event_t event);
static void set_generic_bar_color(lv_obj_t *bar, uint32_t value,
                                  uint32_t max);
static void set_range_bar_color(lv_obj_t *bar, uint32_t value,
                                uint32_t min, uint32_t max);
static void update_sprite(void);
static void show_action_sprite(action_type_t action);
static void revert_sprite_cb(lv_timer_t *t);
static reptile_t *active_reptile_ptr(void);
static void sync_active_runtime(void);
static void refresh_tile_styles(void);
static esp_err_t ensure_save_directory(bool simulation);
static esp_err_t allocate_new_save_slot(char *slot, size_t len);
static void update_species_labels(void);
static bool certificate_is_available(const species_db_entry_t *species);
static void show_species_selection_modal(bool forced);
static void hide_species_selection_modal(void);
static void species_btn_event_cb(lv_event_t *e);
static void species_select_event_cb(lv_event_t *e);
static void species_confirm_event_cb(lv_event_t *e);
static void species_cancel_event_cb(lv_event_t *e);
static bool terrarium_requires_species_selection(const terrarium_t *terrarium);
static void ensure_species_profile(void);

bool reptile_game_is_active(void) { return s_game_active; }

void reptile_game_init(void) {

  bool start_new = (s_start_mode == REPTILE_START_NEW);
  bool start_resume = (s_start_mode == REPTILE_START_RESUME);

  game_mode_set(GAME_MODE_SIMULATION);

  reptile_t seed = {0};
  reptile_init(&seed, true);

  esp_err_t mgr_err = terrarium_manager_init(true);
  if (mgr_err != ESP_OK) {
    ESP_LOGE(TAG,
             "Impossible d'initialiser le gestionnaire de terrariums (err=0x%x)",
             mgr_err);
  }

  s_active_terrarium = terrarium_manager_get_active();
  if (!s_active_terrarium) {
    ESP_LOGE(TAG, "Aucun terrarium actif disponible");
    s_start_mode = REPTILE_START_AUTO;
    return;
  }

  reptile_select_save(s_active_terrarium->config.reptile_slot, true);

  if (s_slot_override_pending) {
    esp_err_t slot_err =
        terrarium_manager_set_slot(s_active_terrarium, s_slot_override);
    if (slot_err != ESP_OK) {
      ESP_LOGW(TAG, "Sélection du slot %s impossible (err=0x%x)",
               s_slot_override, slot_err);
    } else {
      ESP_LOGI(TAG, "Slot de sauvegarde actif: %s", s_slot_override);
      terrarium_manager_reset_state(s_active_terrarium);
      reptile_select_save(s_active_terrarium->config.reptile_slot, true);
      reptile_save(&s_active_terrarium->reptile);
    }
    s_slot_override_pending = false;
    s_slot_override[0] = '\0';
  }

  if (!s_active_terrarium->state_loaded || start_resume) {
    esp_err_t load_res = reptile_load(&s_active_terrarium->reptile);
    if (load_res != ESP_OK) {
      if (start_resume) {
        ESP_LOGW(TAG,
                 "Sauvegarde introuvable, démarrage d'une nouvelle partie");
      }
      terrarium_manager_reset_state(s_active_terrarium);
      esp_err_t save_res = reptile_save(&s_active_terrarium->reptile);
      if (save_res != ESP_OK) {
        ESP_LOGW(TAG,
                 "Impossible de persister l'état initial (err=0x%x)", save_res);
      }
    } else {
      s_active_terrarium->state_loaded = true;
    }
  }

  if (start_new) {
    terrarium_manager_reset_state(s_active_terrarium);
    esp_err_t save_res = reptile_save(&s_active_terrarium->reptile);
    if (save_res != ESP_OK) {
      ESP_LOGW(TAG, "Impossible de sauvegarder le nouvel état (err=0x%x)",
               save_res);
    }
  }

  sync_active_runtime();
  sprite_is_happy = false;

  s_start_mode = REPTILE_START_AUTO;
}

static reptile_t *active_reptile_ptr(void) {
  return s_active_terrarium ? &s_active_terrarium->reptile : NULL;
}

static void sync_active_runtime(void) {
  if (!s_active_terrarium) {
    return;
  }
  s_active_terrarium->last_tick_ms = lv_tick_get();
  s_active_terrarium->update_ms_accum = 0;
  s_active_terrarium->soothe_ms_accum = 0;
  s_active_terrarium->soothe_time_ms = 0;
}

static void refresh_tile_styles(void) {
  size_t active_idx = terrarium_manager_get_active_index();
  for (size_t i = 0; i < TERRARIUM_MANAGER_MAX_TERRARIUMS; ++i) {
    lv_obj_t *btn = s_tiles[i].button;
    if (!btn) {
      continue;
    }
    if (i == active_idx) {
      lv_obj_add_style(btn, &style_tile_selected, LV_PART_MAIN);
    } else {
      lv_obj_remove_style(btn, &style_tile_selected, LV_PART_MAIN);
    }
  }
}

const reptile_t *reptile_get_state(void) {
  terrarium_t *terrarium = terrarium_manager_get_active();
  return terrarium ? &terrarium->reptile : NULL;
}

static esp_err_t ensure_save_directory(bool simulation) {
  char dir[96];
  int written = snprintf(dir, sizeof(dir), "%s/%s", MOUNT_POINT,
                         simulation ? "sim" : "real");
  if (written < 0 || (size_t)written >= sizeof(dir)) {
    return ESP_ERR_INVALID_SIZE;
  }
  if (mkdir(dir, 0777) != 0) {
    if (errno != EEXIST) {
      ESP_LOGW(TAG, "Création du répertoire %s échouée (errno=%d)", dir, errno);
      return ESP_FAIL;
    }
  }
  return ESP_OK;
}

static esp_err_t allocate_new_save_slot(char *slot, size_t len) {
  if (!slot || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t err = ensure_save_directory(true);
  if (err != ESP_OK) {
    return err;
  }

  char index_path[128];
  int written = snprintf(index_path, sizeof(index_path), "%s/sim/%s", MOUNT_POINT,
                         REPTILE_SAVE_INDEX_FILE);
  if (written < 0 || (size_t)written >= sizeof(index_path)) {
    return ESP_ERR_INVALID_SIZE;
  }

  uint32_t index = 0;
  FILE *f = fopen(index_path, "r");
  if (f) {
    if (fscanf(f, "%u", &index) != 1) {
      index = 0;
    }
    fclose(f);
  }

  index++;
  f = fopen(index_path, "w");
  if (!f) {
    ESP_LOGW(TAG, "Impossible d'ouvrir %s pour écriture", index_path);
    return ESP_FAIL;
  }
  if (fprintf(f, "%u\n", index) < 0) {
    ESP_LOGW(TAG, "Impossible d'écrire l'index de sauvegarde dans %s",
             index_path);
    fclose(f);
    return ESP_FAIL;
  }
  fclose(f);

  int needed = snprintf(slot, len, REPTILE_SAVE_PREFIX "%04u" REPTILE_SAVE_EXT,
                        index);
  if (needed < 0 || (size_t)needed >= len) {
    if (len > 0) {
      slot[len - 1] = '\0';
    }
    return ESP_ERR_INVALID_SIZE;
  }
  return ESP_OK;
}

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

static void set_generic_bar_color(lv_obj_t *bar, uint32_t value, uint32_t max) {
  if (!bar || max == 0U) {
    return;
  }
  uint32_t pct = (value * 100U) / max;
  lv_color_t palette_color;
  if (pct > 70U) {
    palette_color = lv_palette_main(LV_PALETTE_GREEN);
  } else if (pct > 30U) {
    palette_color = lv_palette_main(LV_PALETTE_YELLOW);
  } else {
    palette_color = lv_palette_main(LV_PALETTE_RED);
  }
  lv_obj_set_style_bg_color(bar, palette_color, LV_PART_INDICATOR);
}

static void set_range_bar_color(lv_obj_t *bar, uint32_t value, uint32_t min,
                                uint32_t max) {
  if (!bar) {
    return;
  }
  lv_color_t palette_color;
  if (max <= min) {
    palette_color = (value == min) ? lv_palette_main(LV_PALETTE_GREEN)
                                   : lv_palette_main(LV_PALETTE_RED);
  } else if (value < min || value > max) {
    palette_color = lv_palette_main(LV_PALETTE_RED);
  } else {
    uint32_t span = max - min;
    uint32_t offset = value - min;
    uint32_t margin = (span >= 4U) ? (span / 4U) : 1U;
    if (offset <= margin || (max - value) <= margin) {
      palette_color = lv_palette_main(LV_PALETTE_YELLOW);
    } else {
      palette_color = lv_palette_main(LV_PALETTE_GREEN);
    }
  }
  lv_obj_set_style_bg_color(bar, palette_color, LV_PART_INDICATOR);
}

static bool certificate_is_available(const species_db_entry_t *species) {
  if (!species) {
    return true;
  }
  if (!species->certificate_required) {
    return true;
  }
  if (!species->certificate_code || species->certificate_code[0] == '\0') {
    return false;
  }
  char path[256];
  static const char *exts[] = {".pdf", ".crt", ".cer"};
  for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); ++i) {
    int written = snprintf(path, sizeof(path), "%s/certificates/%s%s", MOUNT_POINT,
                           species->certificate_code, exts[i]);
    if (written <= 0 || (size_t)written >= sizeof(path)) {
      continue;
    }
    struct stat st = {0};
    if (stat(path, &st) == 0) {
      return true;
    }
  }
  return false;
}

static void update_species_labels(void) {
  if (!label_species_name || !label_species_legal || !label_species_cert) {
    return;
  }
  const species_db_entry_t *species =
      s_active_terrarium ? terrarium_manager_get_species(s_active_terrarium)
                         : NULL;
  if (!species) {
    lv_label_set_text(label_species_name, "Espèce : aucune");
    lv_label_set_text(label_species_legal,
                      "Réf.: non définie\nDimensions min : --");
    lv_label_set_text(label_species_cert, "Certificat : non requis");
    return;
  }
  lv_label_set_text_fmt(label_species_name, "Espèce : %s (%s)",
                        species->common_name, species->scientific_name);
  lv_label_set_text_fmt(label_species_legal,
                        "Réf.: %s\nMin : %ucm x %ucm x %ucm",
                        species->legal_reference, species->terrarium_min.length_cm,
                        species->terrarium_min.width_cm,
                        species->terrarium_min.height_cm);
  bool cert_ok = certificate_is_available(species);
  if (species->certificate_required) {
    lv_label_set_text_fmt(label_species_cert, "Certificat %s : %s",
                          species->certificate_code,
                          cert_ok ? "✅ disponible" : "⚠️ absent");
  } else {
    lv_label_set_text(label_species_cert, "Certificat : non requis");
  }
}

static void populate_species_details(const species_db_entry_t *species) {
  if (!label_species_details) {
    return;
  }
  if (!species) {
    lv_label_set_text(label_species_details,
                      "Sélectionnez une espèce pour voir le détail.");
    return;
  }
  bool cert_ok = certificate_is_available(species);
  char buf[320];
  snprintf(buf, sizeof(buf),
           "%s (%s)\nTerrarium min : %ucm x %ucm x %ucm\nTemp : %u-%u °C\nHumidité : %u-%u %%\nUV : %u-%u\nRéférence : %s\nCertificat : %s",
           species->common_name, species->scientific_name,
           species->terrarium_min.length_cm, species->terrarium_min.width_cm,
           species->terrarium_min.height_cm, species->environment.temperature_min_c,
           species->environment.temperature_max_c,
           species->environment.humidity_min_pct,
           species->environment.humidity_max_pct, species->environment.uv_index_min,
           species->environment.uv_index_max,
           species->legal_reference,
           species->certificate_required
               ? (cert_ok ? "Disponible" : "Absent")
               : "Non requis");
  lv_label_set_text(label_species_details, buf);
}

static void hide_species_selection_modal(void) {
  if (modal_species) {
    lv_obj_del(modal_species);
    modal_species = NULL;
    list_species = NULL;
    label_species_details = NULL;
    btn_species_confirm = NULL;
    species_candidate = NULL;
  }
}

static void show_species_selection_modal(bool forced) {
  if (modal_species || !s_active_terrarium) {
    return;
  }
  species_candidate = NULL;

  modal_species = lv_obj_create(lv_scr_act());
  lv_obj_set_size(modal_species, LV_PCT(80), LV_PCT(80));
  lv_obj_center(modal_species);
  lv_obj_add_flag(modal_species, LV_OBJ_FLAG_MODAL);
  lv_obj_set_style_pad_all(modal_species, 12, 0);
  lv_obj_set_style_pad_gap(modal_species, 12, 0);
  lv_obj_set_style_bg_color(modal_species, lv_palette_lighten(LV_PALETTE_GREY, 1),
                            LV_PART_MAIN);
  lv_obj_set_flex_flow(modal_species, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *title = lv_label_create(modal_species);
  lv_obj_add_style(title, &style_font24, 0);
  lv_label_set_text(title, forced ? "Sélection d'espèce requise"
                                  : "Choisir une espèce autorisée");

  list_species = lv_list_create(modal_species);
  lv_obj_set_size(list_species, LV_PCT(100), LV_PCT(50));

  const terrarium_config_t *cfg = &s_active_terrarium->config;
  bool has_option = false;
  for (size_t i = 0; i < species_db_count(); ++i) {
    const species_db_entry_t *species = species_db_get(i);
    if (!species_db_dimensions_satisfied(species, cfg->dimensions.length_cm,
                                         cfg->dimensions.width_cm,
                                         cfg->dimensions.height_cm)) {
      continue;
    }
    bool cert_ok = certificate_is_available(species);
    char item[160];
    snprintf(item, sizeof(item), "%s (%s)%s", species->common_name,
             species->scientific_name,
             (!cert_ok && species->certificate_required) ? " ⚠️" : "");
    lv_obj_t *btn = lv_list_add_btn(list_species, NULL, item);
    lv_obj_add_event_cb(btn, species_select_event_cb, LV_EVENT_CLICKED,
                        (void *)species);
    has_option = true;
  }

  label_species_details = lv_label_create(modal_species);
  lv_obj_align(label_species_details, LV_ALIGN_TOP_LEFT, 0, 0);
  populate_species_details(NULL);

  lv_obj_t *actions = lv_obj_create(modal_species);
  lv_obj_remove_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_gap(actions, 12, 0);

  btn_species_confirm = lv_btn_create(actions);
  lv_obj_set_width(btn_species_confirm, 150);
  lv_obj_add_event_cb(btn_species_confirm, species_confirm_event_cb,
                      LV_EVENT_CLICKED, NULL);
  lv_obj_add_state(btn_species_confirm, LV_STATE_DISABLED);
  lv_obj_t *lbl_confirm = lv_label_create(btn_species_confirm);
  lv_obj_add_style(lbl_confirm, &style_font24, 0);
  lv_label_set_text(lbl_confirm, "Valider");
  lv_obj_center(lbl_confirm);

  lv_obj_t *btn_cancel = lv_btn_create(actions);
  lv_obj_set_width(btn_cancel, 150);
  lv_obj_add_event_cb(btn_cancel, species_cancel_event_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
  lv_obj_add_style(lbl_cancel, &style_font24, 0);
  lv_label_set_text(lbl_cancel, forced ? "Plus tard" : "Annuler");
  lv_obj_center(lbl_cancel);

  if (!has_option) {
    lv_label_set_text(label_species_details,
                      "Aucune espèce conforme aux dimensions actuelles.");
    lv_obj_add_state(btn_species_confirm, LV_STATE_DISABLED);
  }
}

static void species_btn_event_cb(lv_event_t *e) {
  (void)e;
  if (lvgl_port_lock(-1)) {
    show_species_selection_modal(false);
    lvgl_port_unlock();
  }
}

static void species_select_event_cb(lv_event_t *e) {
  const species_db_entry_t *species =
      (const species_db_entry_t *)lv_event_get_user_data(e);
  species_candidate = species;
  populate_species_details(species);
  if (!btn_species_confirm) {
    return;
  }
  if (species && certificate_is_available(species)) {
    lv_obj_clear_state(btn_species_confirm, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(btn_species_confirm, LV_STATE_DISABLED);
  }
}

static void species_confirm_event_cb(lv_event_t *e) {
  (void)e;
  if (!species_candidate || !s_active_terrarium) {
    return;
  }
  if (!certificate_is_available(species_candidate) &&
      species_candidate->certificate_required) {
    return;
  }
  esp_err_t err = terrarium_manager_set_species(s_active_terrarium,
                                                species_candidate);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Impossible d'appliquer l'espèce (%s) err=0x%x",
             species_candidate->id, err);
    return;
  }
  update_species_labels();
  hide_species_selection_modal();
  ui_update_main();
  ui_update_stats();
  refresh_tile_styles();
}

static void species_cancel_event_cb(lv_event_t *e) {
  (void)e;
  hide_species_selection_modal();
}

static bool terrarium_requires_species_selection(const terrarium_t *terrarium) {
  if (!terrarium) {
    return false;
  }
  const species_db_entry_t *species = terrarium_manager_get_species(terrarium);
  if (!species) {
    return true;
  }
  const terrarium_config_t *cfg = &terrarium->config;
  if (!species_db_dimensions_satisfied(species, cfg->dimensions.length_cm,
                                       cfg->dimensions.width_cm,
                                       cfg->dimensions.height_cm)) {
    return true;
  }
  if (species->certificate_required && !certificate_is_available(species)) {
    return true;
  }
  return false;
}

static void ensure_species_profile(void) {
  update_species_labels();
  if (terrarium_requires_species_selection(s_active_terrarium)) {
    show_species_selection_modal(true);
  }
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
  reptile_t *reptile = active_reptile_ptr();
  if (!reptile) {
    return;
  }
  bool happy = reptile->humeur >= 50;
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
  terrarium_t *terrarium = s_active_terrarium;
  reptile_t *reptile = active_reptile_ptr();
  if (!terrarium || !reptile) {
    return;
  }

  uint32_t now = lv_tick_get();
  if (terrarium->last_tick_ms == 0) {
    terrarium->last_tick_ms = now;
    return;
  }

  uint32_t elapsed = now - terrarium->last_tick_ms;
  terrarium->last_tick_ms = now;

  terrarium->update_ms_accum += elapsed;
  uint32_t process_ms =
      terrarium->update_ms_accum - (terrarium->update_ms_accum % 1000U);
  reptile_update(reptile, process_ms);
  terrarium->update_ms_accum -= process_ms;
  bool dirty = process_ms > 0;

  if (terrarium->soothe_time_ms > 0) {
    terrarium->soothe_time_ms =
        (terrarium->soothe_time_ms > elapsed) ?
            (terrarium->soothe_time_ms - elapsed) :
            0;
    terrarium->soothe_ms_accum += elapsed;
    uint32_t mood_sec = terrarium->soothe_ms_accum / 1000U;
    if (mood_sec > 0) {
      uint32_t inc = mood_sec * 2U;
      reptile->humeur =
          (reptile->humeur + inc > 100) ? 100 : reptile->humeur + inc;
      terrarium->soothe_ms_accum -= mood_sec * 1000U;
      dirty = true;
    }
  } else {
    terrarium->soothe_ms_accum = 0;
  }

  reptile_event_t prev_evt = reptile->event;
  reptile_event_t evt = reptile_check_events(reptile);
  if (evt != prev_evt && evt != REPTILE_EVENT_NONE) {
    show_event_popup(evt);
  }
  if (dirty) {
    reptile_save(reptile);
  }

  ui_update_main();
  ui_update_stats();

  // Broadcast reptile state over CAN bus
  can_message_t msg = {
      .identifier = 0x100,
      .data_length_code = 8,
      .flags = TWAI_MSG_FLAG_NONE,
  };
  msg.data[0] = (uint8_t)(reptile->faim & 0xFF);
  msg.data[1] = (uint8_t)((reptile->faim >> 8) & 0xFF);
  msg.data[2] = (uint8_t)(reptile->eau & 0xFF);
  msg.data[3] = (uint8_t)((reptile->eau >> 8) & 0xFF);
  msg.data[4] = (uint8_t)(reptile->temperature & 0xFF);
  msg.data[5] = (uint8_t)((reptile->temperature >> 8) & 0xFF);
  msg.data[6] = (uint8_t)(reptile->humeur & 0xFF);
  msg.data[7] = (uint8_t)((reptile->humeur >> 8) & 0xFF);
  if (can_is_active()) {
    esp_err_t err = can_write_Byte(msg);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "CAN write failed: %s", esp_err_to_name(err));
    }
  }

  if (reptile->faim <= REPTILE_FAMINE_THRESHOLD) {
    start_warning_anim(bar_faim);
  }
  if (reptile->eau <= REPTILE_EAU_THRESHOLD) {
    start_warning_anim(bar_eau);
  }
  reptile_environment_thresholds_t thresholds;
  reptile_get_thresholds(reptile, &thresholds);
  if (reptile->temperature < thresholds.temperature_min_c ||
      reptile->temperature > thresholds.temperature_max_c) {
    start_warning_anim(bar_temp);
  }
  if (reptile->humidite < thresholds.humidity_min_pct ||
      reptile->humidite > thresholds.humidity_max_pct) {
    start_warning_anim(bar_humidite);
  }
  if (reptile->uv_index < thresholds.uv_index_min ||
      reptile->uv_index > thresholds.uv_index_max) {
    start_warning_anim(bar_uv);
  }
}

static void back_btn_event_cb(lv_event_t *e) {
  (void)e;
  if (lvgl_port_lock(-1)) {
    lv_scr_load(screen_main);
    refresh_tile_styles();
    lvgl_port_unlock();
  }
}

static void terrarium_tile_event_cb(lv_event_t *e) {
  size_t index = (size_t)lv_event_get_user_data(e);
  if (lvgl_port_lock(-1)) {
    esp_err_t err = terrarium_manager_select(index);
    if (err == ESP_OK) {
      s_active_terrarium = terrarium_manager_get_active();
      reptile_select_save(s_active_terrarium->config.reptile_slot, true);
      sync_active_runtime();
      sprite_is_happy = false;
      update_sprite();
      ui_update_main();
      ui_update_stats();
      refresh_tile_styles();
      lv_scr_load(screen_stats);
      ensure_species_profile();
    } else {
      ESP_LOGW(TAG, "Sélection du terrarium %u impossible (err=0x%x)",
               (unsigned)(index + 1), err);
    }
    lvgl_port_unlock();
  }
}

static void action_btn_event_cb(lv_event_t *e) {
  action_type_t action = (action_type_t)(uintptr_t)lv_event_get_user_data(e);
  if (lvgl_port_lock(-1)) {
    reptile_t *reptile = active_reptile_ptr();
    if (!reptile) {
      lvgl_port_unlock();
      return;
    }
    switch (action) {
    case ACTION_FEED:
      reptile_feed(reptile);
      break;
    case ACTION_WATER:
      reptile_give_water(reptile);
      break;
    case ACTION_HEAT:
      reptile_heat(reptile);
      break;
    case ACTION_SOOTHE:
      reptile_soothe(reptile);
      if (s_active_terrarium) {
        s_active_terrarium->soothe_time_ms = 5000;
        s_active_terrarium->soothe_ms_accum = 0;
      }
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

void reptile_game_prepare_new_game(void) {
  s_start_mode = REPTILE_START_NEW;
  char new_slot[REPTILE_SLOT_NAME_MAX] = {0};
  esp_err_t err = allocate_new_save_slot(new_slot, sizeof(new_slot));
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Allocation d'un nouveau slot échouée (err=0x%x)", err);
    strncpy(s_slot_override, "reptile_state.bin", sizeof(s_slot_override) - 1);
    s_slot_override[sizeof(s_slot_override) - 1] = '\0';
    s_slot_override_pending = true;
    return;
  } else {
    ESP_LOGI(TAG, "Création d'un nouveau slot de sauvegarde: %s", new_slot);
  }
  if (terrarium_manager_is_initialized()) {
    terrarium_t *terrarium = terrarium_manager_get_active();
    if (terrarium) {
      if (terrarium_manager_set_slot(terrarium, new_slot) == ESP_OK) {
        terrarium_manager_reset_state(terrarium);
        reptile_select_save(terrarium->config.reptile_slot, true);
        reptile_save(&terrarium->reptile);
        s_slot_override_pending = false;
        s_slot_override[0] = '\0';
        return;
      }
    }
  }
  strncpy(s_slot_override, new_slot, sizeof(s_slot_override) - 1);
  s_slot_override[sizeof(s_slot_override) - 1] = '\0';
  s_slot_override_pending = true;
}

void reptile_game_prepare_resume(void) {
  s_start_mode = REPTILE_START_RESUME;
  if (terrarium_manager_is_initialized()) {
    terrarium_t *terrarium = terrarium_manager_get_active();
    if (terrarium) {
      terrarium->state_loaded = false;
    }
  } else {
    s_slot_override_pending = false;
    s_slot_override[0] = '\0';
  }
}

void reptile_game_stop(void) {
  s_game_active = false;
  logging_pause();
  sleep_set_enabled(false);
  hide_species_selection_modal();
  if (s_active_terrarium) {
    s_active_terrarium->soothe_time_ms = 0;
    s_active_terrarium->soothe_ms_accum = 0;
    s_active_terrarium->update_ms_accum = 0;
  }
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
  lv_style_reset(&style_tile_selected);
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
  size_t count = terrarium_manager_count();
  size_t active_idx = terrarium_manager_get_active_index();
  for (size_t i = 0; i < count; ++i) {
    terrarium_t *terrarium = terrarium_manager_get(i);
    terrarium_tile_ui_t *tile = &s_tiles[i];
    if (!terrarium || !tile->button) {
      continue;
    }
    if (tile->name_label) {
      lv_label_set_text(tile->name_label, terrarium->config.name);
    }
    if (tile->substrate_icon) {
      lv_image_set_src(tile->substrate_icon,
                       terrarium_manager_get_substrate_icon(
                           terrarium->config.substrate));
    }
    if (tile->decor_icon) {
      lv_image_set_src(tile->decor_icon,
                       terrarium_manager_get_decor_icon(terrarium->config.decor));
    }

    const reptile_t *r = terrarium->state_loaded ? &terrarium->reptile : NULL;
    if (tile->status_label) {
      if (!r) {
        lv_label_set_text(tile->status_label, "Non initialisé");
      } else {
        const species_db_entry_t *species =
            terrarium_manager_get_species(terrarium);
        const char *species_name = species ? species->common_name : "Aucune espèce";
        bool dims_ok = species ? species_db_dimensions_satisfied(
                                     species, terrarium->config.dimensions.length_cm,
                                     terrarium->config.dimensions.width_cm,
                                     terrarium->config.dimensions.height_cm)
                               : false;
        bool cert_ok = certificate_is_available(species);
        char status[256];
        int written = snprintf(status, sizeof(status),
                               "%s\nF:%3" PRIu32 " Eau:%3" PRIu32
                               "\nTemp:%2" PRIu32 "°C Hum:%2" PRIu32 "%% UV:%2" PRIu32,
                               species_name, r->faim, r->eau, r->temperature,
                               r->humidite, r->uv_index);
        if (species && written > 0 && (size_t)written < sizeof(status)) {
          if (!dims_ok) {
            written += snprintf(status + written, sizeof(status) - (size_t)written,
                                "\n⚠️ Dimensions insuffisantes");
          }
          if (species->certificate_required && !cert_ok &&
              written > 0 && (size_t)written < sizeof(status)) {
            snprintf(status + written, sizeof(status) - (size_t)written,
                     "\n⚠️ Certificat manquant");
          }
        }
        lv_label_set_text(tile->status_label, status);
      }
    }

    bool warning = false;
    if (r) {
      reptile_environment_thresholds_t thresholds;
      reptile_get_thresholds(r, &thresholds);
      bool temp_bad = (r->temperature < thresholds.temperature_min_c) ||
                      (r->temperature > thresholds.temperature_max_c);
      bool hum_bad = (r->humidite < thresholds.humidity_min_pct) ||
                     (r->humidite > thresholds.humidity_max_pct);
      bool uv_bad = (r->uv_index < thresholds.uv_index_min) ||
                    (r->uv_index > thresholds.uv_index_max);
      bool cert_bad = false;
      const species_db_entry_t *species =
          terrarium_manager_get_species(terrarium);
      if (species) {
        bool dims_ok = species_db_dimensions_satisfied(
            species, terrarium->config.dimensions.length_cm,
            terrarium->config.dimensions.width_cm,
            terrarium->config.dimensions.height_cm);
        bool cert_ok = certificate_is_available(species);
        cert_bad = (!dims_ok) || (species->certificate_required && !cert_ok);
      }
      warning = (r->faim <= REPTILE_FAMINE_THRESHOLD ||
                 r->eau <= REPTILE_EAU_THRESHOLD || temp_bad || hum_bad ||
                 uv_bad || cert_bad);
    }
    lv_color_t base_color = warning ? lv_palette_lighten(LV_PALETTE_RED, 2)
                                    : lv_palette_lighten(LV_PALETTE_GREY, 3);
    lv_obj_set_style_bg_color(tile->button, base_color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile->button, LV_OPA_COVER, LV_PART_MAIN);
    if (i == active_idx) {
      lv_obj_set_style_bg_color(tile->button,
                                warning ? lv_palette_main(LV_PALETTE_DEEP_ORANGE)
                                        : lv_palette_lighten(LV_PALETTE_BLUE, 3),
                                LV_PART_MAIN);
    }
  }
  refresh_tile_styles();
}

static void ui_update_stats(void) {
  reptile_t *reptile = active_reptile_ptr();
  if (!reptile) {
    return;
  }
  if (label_terrarium_name && s_active_terrarium) {
    lv_label_set_text(label_terrarium_name, s_active_terrarium->config.name);
  }
  reptile_environment_thresholds_t thresholds;
  reptile_get_thresholds(reptile, &thresholds);

  lv_bar_set_value(bar_faim, reptile->faim, LV_ANIM_ON);
  lv_bar_set_value(bar_eau, reptile->eau, LV_ANIM_ON);
  lv_bar_set_value(bar_humeur, reptile->humeur, LV_ANIM_ON);
  set_generic_bar_color(bar_faim, reptile->faim, 100);
  set_generic_bar_color(bar_eau, reptile->eau, 100);
  set_generic_bar_color(bar_humeur, reptile->humeur, 100);

  uint32_t temp_bar_max = thresholds.temperature_max_c + 10U;
  if (temp_bar_max < thresholds.temperature_max_c) {
    temp_bar_max = thresholds.temperature_max_c;
  }
  lv_bar_set_range(bar_temp, 0, temp_bar_max);
  lv_bar_set_value(bar_temp, reptile->temperature, LV_ANIM_ON);
  set_range_bar_color(bar_temp, reptile->temperature,
                      thresholds.temperature_min_c,
                      thresholds.temperature_max_c);

  lv_bar_set_value(bar_humidite, reptile->humidite, LV_ANIM_ON);
  set_range_bar_color(bar_humidite, reptile->humidite,
                      thresholds.humidity_min_pct,
                      thresholds.humidity_max_pct);

  uint32_t uv_bar_max = thresholds.uv_index_max + 4U;
  if (uv_bar_max < thresholds.uv_index_max) {
    uv_bar_max = thresholds.uv_index_max;
  }
  lv_bar_set_range(bar_uv, 0, uv_bar_max);
  lv_bar_set_value(bar_uv, reptile->uv_index, LV_ANIM_ON);
  set_range_bar_color(bar_uv, reptile->uv_index, thresholds.uv_index_min,
                      thresholds.uv_index_max);
  lv_label_set_text_fmt(label_stat_faim, "Faim: %" PRIu32, reptile->faim);
  lv_label_set_text_fmt(label_stat_eau, "Eau: %" PRIu32, reptile->eau);
  lv_label_set_text_fmt(label_stat_temp, "Température: %" PRIu32,
                        reptile->temperature);
  lv_label_set_text_fmt(label_stat_humidite, "Humidité: %" PRIu32,
                        reptile->humidite);
  lv_label_set_text_fmt(label_stat_humeur, "Humeur: %" PRIu32,
                        reptile->humeur);
  if (label_stat_uv) {
    lv_label_set_text_fmt(label_stat_uv, "UV: %" PRIu32, reptile->uv_index);
  }
  update_species_labels();
  update_sprite();
}

void reptile_game_start(esp_lcd_panel_handle_t panel,
                        esp_lcd_touch_handle_t touch) {
  (void)panel;
  (void)touch;
  s_game_active = true;
  lv_style_init(&style_font24);
  lv_style_set_text_font(&style_font24, &lv_font_montserrat_24);

  lv_style_init(&style_tile_selected);
  lv_style_set_border_width(&style_tile_selected, 4);
  lv_style_set_border_color(&style_tile_selected,
                            lv_palette_main(LV_PALETTE_BLUE));
  lv_style_set_border_opa(&style_tile_selected, LV_OPA_COVER);
  lv_style_set_outline_width(&style_tile_selected, 0);

  memset(s_tiles, 0, sizeof(s_tiles));

  screen_main = lv_obj_create(NULL);
  lv_obj_set_style_pad_all(screen_main, 12, 0);
  lv_obj_set_style_pad_gap(screen_main, 12, 0);
  lv_obj_set_flex_flow(screen_main, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *title = lv_label_create(screen_main);
  lv_obj_add_style(title, &style_font24, 0);
  lv_label_set_text(title, "Sélection des terrariums");
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

  terrarium_container = lv_obj_create(screen_main);
  lv_obj_set_size(terrarium_container, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(terrarium_container, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(terrarium_container, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_gap(terrarium_container, 12, 0);
  lv_obj_set_style_pad_all(terrarium_container, 6, 0);
  lv_obj_set_style_bg_opa(terrarium_container, LV_OPA_TRANSP, LV_PART_MAIN);

  size_t terrarium_count = terrarium_manager_count();
  for (size_t i = 0; i < terrarium_count; ++i) {
    lv_obj_t *btn = lv_btn_create(terrarium_container);
    lv_obj_set_size(btn, 150, 150);
    lv_obj_set_style_radius(btn, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(btn, terrarium_tile_event_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)i);

    lv_obj_t *icon_row = lv_obj_create(btn);
    lv_obj_remove_flag(icon_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(icon_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(icon_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(icon_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(icon_row, 6, LV_PART_MAIN);

    s_tiles[i].button = btn;
    s_tiles[i].substrate_icon = lv_image_create(icon_row);
    s_tiles[i].decor_icon = lv_image_create(icon_row);

    s_tiles[i].name_label = lv_label_create(btn);
    lv_obj_add_style(s_tiles[i].name_label, &style_font24, 0);
    lv_obj_set_style_text_align(s_tiles[i].name_label, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);

    s_tiles[i].status_label = lv_label_create(btn);
    lv_obj_set_style_text_align(s_tiles[i].status_label, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
  }

  screen_stats = lv_obj_create(NULL);
  lv_obj_set_style_pad_all(screen_stats, 12, 0);

  label_terrarium_name = lv_label_create(screen_stats);
  lv_obj_add_style(label_terrarium_name, &style_font24, 0);
  lv_obj_align(label_terrarium_name, LV_ALIGN_TOP_MID, 0, 10);

  btn_species_select = lv_btn_create(screen_stats);
  lv_obj_set_size(btn_species_select, 200, 40);
  lv_obj_align(btn_species_select, LV_ALIGN_TOP_LEFT, 10, 10);
  lv_obj_add_event_cb(btn_species_select, species_btn_event_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_t *lbl_species_btn = lv_label_create(btn_species_select);
  lv_obj_add_style(lbl_species_btn, &style_font24, 0);
  lv_label_set_text(lbl_species_btn, "Choisir espèce");
  lv_obj_center(lbl_species_btn);

  label_species_name = lv_label_create(screen_stats);
  lv_obj_add_style(label_species_name, &style_font24, 0);
  lv_obj_align(label_species_name, LV_ALIGN_TOP_LEFT, 10, 60);

  label_species_legal = lv_label_create(screen_stats);
  lv_obj_align_to(label_species_legal, label_species_name, LV_ALIGN_OUT_BOTTOM_LEFT,
                  0, 10);

  label_species_cert = lv_label_create(screen_stats);
  lv_obj_align_to(label_species_cert, label_species_legal,
                  LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

  img_reptile = lv_img_create(screen_stats);
  lv_img_set_src(img_reptile, sprite_idle);
  lv_obj_align(img_reptile, LV_ALIGN_TOP_LEFT, 10, 150);

  bar_faim = lv_bar_create(screen_stats);
  lv_bar_set_range(bar_faim, 0, 100);
  lv_obj_set_size(bar_faim, 220, 20);
  lv_obj_align(bar_faim, LV_ALIGN_TOP_LEFT, 180, 60);
  lv_bar_set_value(bar_faim, 100, LV_ANIM_OFF);
  lv_obj_t *label_faim = lv_label_create(screen_stats);
  lv_obj_add_style(label_faim, &style_font24, 0);
  lv_label_set_text(label_faim, "Faim");
  lv_obj_align_to(label_faim, bar_faim, LV_ALIGN_OUT_TOP_LEFT, 0, -5);

  bar_eau = lv_bar_create(screen_stats);
  lv_bar_set_range(bar_eau, 0, 100);
  lv_obj_set_size(bar_eau, 220, 20);
  lv_obj_align_to(bar_eau, bar_faim, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 30);
  lv_bar_set_value(bar_eau, 100, LV_ANIM_OFF);
  lv_obj_t *label_eau = lv_label_create(screen_stats);
  lv_obj_add_style(label_eau, &style_font24, 0);
  lv_label_set_text(label_eau, "Eau");
  lv_obj_align_to(label_eau, bar_eau, LV_ALIGN_OUT_TOP_LEFT, 0, -5);

  bar_temp = lv_bar_create(screen_stats);
  lv_bar_set_range(bar_temp, 0, 50);
  lv_obj_set_size(bar_temp, 220, 20);
  lv_obj_align_to(bar_temp, bar_eau, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 30);
  lv_bar_set_value(bar_temp, 30, LV_ANIM_OFF);
  lv_obj_t *label_temp = lv_label_create(screen_stats);
  lv_obj_add_style(label_temp, &style_font24, 0);
  lv_label_set_text(label_temp, "Température");
  lv_obj_align_to(label_temp, bar_temp, LV_ALIGN_OUT_TOP_LEFT, 0, -5);

  bar_humidite = lv_bar_create(screen_stats);
  lv_bar_set_range(bar_humidite, 0, 100);
  lv_obj_set_size(bar_humidite, 220, 20);
  lv_obj_align_to(bar_humidite, bar_temp, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 30);
  lv_bar_set_value(bar_humidite, 50, LV_ANIM_OFF);
  lv_obj_t *label_humidite = lv_label_create(screen_stats);
  lv_obj_add_style(label_humidite, &style_font24, 0);
  lv_label_set_text(label_humidite, "Humidité");
  lv_obj_align_to(label_humidite, bar_humidite, LV_ALIGN_OUT_TOP_LEFT, 0, -5);

  bar_uv = lv_bar_create(screen_stats);
  lv_bar_set_range(bar_uv, 0, 12);
  lv_obj_set_size(bar_uv, 220, 20);
  lv_obj_align_to(bar_uv, bar_humidite, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 30);
  lv_bar_set_value(bar_uv, 5, LV_ANIM_OFF);
  lv_obj_t *label_uv = lv_label_create(screen_stats);
  lv_obj_add_style(label_uv, &style_font24, 0);
  lv_label_set_text(label_uv, "UV");
  lv_obj_align_to(label_uv, bar_uv, LV_ALIGN_OUT_TOP_LEFT, 0, -5);

  bar_humeur = lv_bar_create(screen_stats);
  lv_bar_set_range(bar_humeur, 0, 100);
  lv_obj_set_size(bar_humeur, 220, 20);
  lv_obj_align_to(bar_humeur, bar_uv, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 30);
  lv_bar_set_value(bar_humeur, 100, LV_ANIM_OFF);
  lv_obj_t *label_humeur = lv_label_create(screen_stats);
  lv_obj_add_style(label_humeur, &style_font24, 0);
  lv_label_set_text(label_humeur, "Humeur");
  lv_obj_align_to(label_humeur, bar_humeur, LV_ALIGN_OUT_TOP_LEFT, 0, -5);

  label_stat_faim = lv_label_create(screen_stats);
  lv_obj_add_style(label_stat_faim, &style_font24, 0);
  lv_obj_align(label_stat_faim, LV_ALIGN_TOP_RIGHT, -10, 120);

  label_stat_eau = lv_label_create(screen_stats);
  lv_obj_add_style(label_stat_eau, &style_font24, 0);
  lv_obj_align_to(label_stat_eau, label_stat_faim, LV_ALIGN_OUT_BOTTOM_RIGHT, 0,
                  10);

  label_stat_temp = lv_label_create(screen_stats);
  lv_obj_add_style(label_stat_temp, &style_font24, 0);
  lv_obj_align_to(label_stat_temp, label_stat_eau, LV_ALIGN_OUT_BOTTOM_RIGHT, 0,
                  10);

  label_stat_humidite = lv_label_create(screen_stats);
  lv_obj_add_style(label_stat_humidite, &style_font24, 0);
  lv_obj_align_to(label_stat_humidite, label_stat_temp, LV_ALIGN_OUT_BOTTOM_RIGHT,
                  0, 10);

  label_stat_uv = lv_label_create(screen_stats);
  lv_obj_add_style(label_stat_uv, &style_font24, 0);
  lv_obj_align_to(label_stat_uv, label_stat_humidite, LV_ALIGN_OUT_BOTTOM_RIGHT,
                  0, 10);

  label_stat_humeur = lv_label_create(screen_stats);
  lv_obj_add_style(label_stat_humeur, &style_font24, 0);
  lv_obj_align_to(label_stat_humeur, label_stat_uv,
                  LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 10);

  lv_obj_t *btn_feed = lv_btn_create(screen_stats);
  lv_obj_set_size(btn_feed, 140, 40);
  lv_obj_align(btn_feed, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  lv_obj_add_event_cb(btn_feed, action_btn_event_cb, LV_EVENT_CLICKED,
                      (void *)(uintptr_t)ACTION_FEED);
  lv_obj_t *lbl_feed = lv_label_create(btn_feed);
  lv_obj_add_style(lbl_feed, &style_font24, 0);
  lv_label_set_text(lbl_feed, "Nourrir");
  lv_obj_center(lbl_feed);

  lv_obj_t *btn_water = lv_btn_create(screen_stats);
  lv_obj_set_size(btn_water, 140, 40);
  lv_obj_align(btn_water, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_add_event_cb(btn_water, action_btn_event_cb, LV_EVENT_CLICKED,
                      (void *)(uintptr_t)ACTION_WATER);
  lv_obj_t *lbl_water = lv_label_create(btn_water);
  lv_obj_add_style(lbl_water, &style_font24, 0);
  lv_label_set_text(lbl_water, "Hydrater");
  lv_obj_center(lbl_water);

  lv_obj_t *btn_heat = lv_btn_create(screen_stats);
  lv_obj_set_size(btn_heat, 140, 40);
  lv_obj_align(btn_heat, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
  lv_obj_add_event_cb(btn_heat, action_btn_event_cb, LV_EVENT_CLICKED,
                      (void *)(uintptr_t)ACTION_HEAT);
  lv_obj_t *lbl_heat = lv_label_create(btn_heat);
  lv_obj_add_style(lbl_heat, &style_font24, 0);
  lv_label_set_text(lbl_heat, "Chauffer");
  lv_obj_center(lbl_heat);

  lv_obj_t *btn_soothe = lv_btn_create(screen_stats);
  lv_obj_set_size(btn_soothe, 140, 40);
  lv_obj_align(btn_soothe, LV_ALIGN_BOTTOM_RIGHT, -10, -60);
  lv_obj_add_event_cb(btn_soothe, action_btn_event_cb, LV_EVENT_CLICKED,
                      (void *)(uintptr_t)ACTION_SOOTHE);
  lv_obj_t *lbl_soothe = lv_label_create(btn_soothe);
  lv_obj_add_style(lbl_soothe, &style_font24, 0);
  lv_label_set_text(lbl_soothe, "Caresser");
  lv_obj_center(lbl_soothe);

  lv_obj_t *btn_back = lv_btn_create(screen_stats);
  lv_obj_set_size(btn_back, 140, 40);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -60);
  lv_obj_add_event_cb(btn_back, back_btn_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_back = lv_label_create(btn_back);
  lv_obj_add_style(lbl_back, &style_font24, 0);
  lv_label_set_text(lbl_back, "Retour");
  lv_obj_center(lbl_back);

  lv_obj_t *btn_sleep = lv_btn_create(screen_stats);
  lv_obj_set_size(btn_sleep, 160, 40);
  lv_obj_align(btn_sleep, LV_ALIGN_TOP_RIGHT, -10, 60);
  lv_obj_add_event_cb(btn_sleep, sleep_btn_event_cb, LV_EVENT_CLICKED, NULL);
  lbl_sleep = lv_label_create(btn_sleep);
  lv_obj_add_style(lbl_sleep, &style_font24, 0);
  lv_label_set_text(lbl_sleep, sleep_is_enabled() ? "Veille ON" : "Veille OFF");
  lv_obj_center(lbl_sleep);

  lv_obj_t *btn_menu = lv_btn_create(screen_stats);
  lv_obj_set_size(btn_menu, 160, 40);
  lv_obj_align(btn_menu, LV_ALIGN_TOP_RIGHT, -10, 10);
  lv_obj_add_event_cb(btn_menu, menu_btn_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_menu = lv_label_create(btn_menu);
  lv_obj_add_style(lbl_menu, &style_font24, 0);
  lv_label_set_text(lbl_menu, "Menu");
  lv_obj_center(lbl_menu);

  ui_update_main();
  ui_update_stats();
  refresh_tile_styles();

  sync_active_runtime();

  if (!life_timer) {
    life_timer = lv_timer_create(reptile_tick, REPTILE_UPDATE_PERIOD_MS, NULL);
  }

  lv_scr_load(screen_main);
  ensure_species_profile();
}

  screen_main = lv_obj_create(NULL);
  screen_stats = lv_obj_create(NULL);

