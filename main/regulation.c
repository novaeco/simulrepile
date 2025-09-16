#include "regulation.h"
#include "lvgl.h"

LV_FONT_DECLARE(lv_font_montserrat_24);

extern lv_obj_t *menu_screen;

static lv_obj_t *s_regulation_screen;

static void regulation_back_event_cb(lv_event_t *e) {
  (void)e;
  if (menu_screen) {
    lv_scr_load(menu_screen);
  }
  if (s_regulation_screen) {
    lv_obj_del_async(s_regulation_screen);
    s_regulation_screen = NULL;
  }
}

void regulation_screen_show(void) {
  if (s_regulation_screen) {
    lv_scr_load(s_regulation_screen);
    return;
  }

  s_regulation_screen = lv_obj_create(NULL);
  lv_obj_set_style_pad_all(s_regulation_screen, 24, LV_PART_MAIN);
  lv_obj_set_style_pad_gap(s_regulation_screen, 18, LV_PART_MAIN);
  lv_obj_set_flex_flow(s_regulation_screen, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_regulation_screen, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

  lv_obj_t *title = lv_label_create(s_regulation_screen);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
  lv_label_set_text(title, "Règlementation");

  lv_obj_t *body = lv_label_create(s_regulation_screen);
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(body, LV_PCT(100));
  lv_label_set_text(body,
                    "• Dimensions minimales : respecter les annexes de l'arrêté du "
                    "8 octobre 2018 (JO 17/10/2018). Voir "
                    "docs/reglementation.md#dimensions-minimales.\n"
                    "• CDC/AOE obligatoires : Code de l'environnement art. L413-2 et "
                    "arrêté du 8 octobre 2018 pour les espèces non domestiques. Voir "
                    "docs/reglementation.md#certificat-capacite.\n"
                    "• Espèces protégées : Règlement (CE) n° 338/97 et règlement (CE) "
                    "n° 865/2006 imposent un permis CITES intra-UE. Voir "
                    "docs/reglementation.md#especes-protegees.");

  lv_obj_t *note = lv_label_create(s_regulation_screen);
  lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(note, LV_PCT(100));
  lv_label_set_text(note,
                    "Pour le détail des obligations, consulter le dossier "
                    "docs/reglementation.md disponible sur la carte SD ou dans le "
                    "répertoire du projet.");

  lv_obj_t *back_btn = lv_btn_create(s_regulation_screen);
  lv_obj_add_event_cb(back_btn, regulation_back_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_set_width(back_btn, 160);
  lv_obj_t *back_lbl = lv_label_create(back_btn);
  lv_label_set_text(back_lbl, "Retour");
  lv_obj_center(back_lbl);
}
