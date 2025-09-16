#include "compliance.h"
#include "lvgl.h"
#include <stdint.h>
#include <stdio.h>

LV_FONT_DECLARE(lv_font_montserrat_24);

typedef struct {
  const char *title;
  const char *question;
  const char *options[3];
  uint8_t option_count;
  uint8_t correct_index;
  const char *explanation;
  const char *legal_source;
} compliance_quiz_def_t;

static const compliance_quiz_def_t
    s_quizzes[COMPLIANCE_TOPIC_COUNT] = {
        [COMPLIANCE_TOPIC_TERRARIUM_SIZE] = {
            .title = "Dimensions minimales",
            .question =
                "Que doit faire l'éleveur si le terrarium est plus petit que les "
                "valeurs fixées par l'arrêté du 8 octobre 2018 ?",
            .options = {
                "Agrandir ou remplacer le terrarium pour respecter les dimensions "
                "réglementaires.",
                "Ignorer la règle, elle n'est qu'indicative.",
            },
            .option_count = 2,
            .correct_index = 0,
            .explanation =
                "L'annexe 2 de l'arrêté du 8 octobre 2018 impose des dimensions "
                "minimales par espèce ; il faut adapter l'installation avant toute "
                "détention.",
            .legal_source = "Arrêté du 8 octobre 2018 (JO 17/10/2018) - voir "
                            "docs/reglementation.md#dimensions-minimales",
        },
        [COMPLIANCE_TOPIC_CERTIFICATE] = {
            .title = "Certificat de capacité",
            .question =
                "Quelle autorisation est exigée pour détenir une espèce soumise "
                "à certificat de capacité ?",
            .options = {
                "Certificat de capacité et autorisation d'ouverture "
                "d'établissement (CDC/AOE) délivrés par la préfecture.",
                "Aucun document, une facture d'achat suffit.",
            },
            .option_count = 2,
            .correct_index = 0,
            .explanation =
                "Le Code de l'environnement (art. L413-2) et l'arrêté du 8 octobre "
                "2018 imposent un CDC complété d'une AOE pour les espèces non "
                "domestiques.",
            .legal_source =
                "Code de l'environnement art. L413-2 et arrêté du 8 octobre 2018 - "
                "voir docs/reglementation.md#certificat-capacite",
        },
        [COMPLIANCE_TOPIC_PROTECTED_SPECIES] = {
            .title = "Espèces protégées",
            .question =
                "Quelle pièce justificative est obligatoire pour un spécimen "
                "inscrit à l'annexe B du règlement (CE) n° 338/97 ?",
            .options = {
                "Un certificat ou permis CITES/UE attestant de l'origine légale "
                "et de la traçabilité.",
                "Aucune formalité : la détention est libre.",
            },
            .option_count = 2,
            .correct_index = 0,
            .explanation =
                "Le règlement (CE) n° 338/97 et son règlement d'application n° "
                "865/2006 imposent un certificat intra-UE (CITES) pour toute "
                "détention ou transfert d'espèces listées.",
            .legal_source =
                "Règlement (CE) n° 338/97 et règlement (CE) n° 865/2006 - voir "
                "docs/reglementation.md#especes-protegees",
        },
};

static lv_obj_t *s_modal;
static lv_obj_t *s_feedback_label;
static lv_obj_t *s_close_btn;
static compliance_topic_t s_active_topic;

static void compliance_close_event_cb(lv_event_t *e) {
  (void)e;
  compliance_dismiss();
}

static void compliance_option_event_cb(lv_event_t *e) {
  if (!s_modal) {
    return;
  }
  uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
  if (idx >= 3) {
    return;
  }
  const compliance_quiz_def_t *quiz = &s_quizzes[s_active_topic];
  bool correct = idx == quiz->correct_index;
  char message[512];
  snprintf(message, sizeof(message),
           "%s %s\nSource : %s",
           correct ? "✅ Bonne réponse." : "❌ Réponse incorrecte.",
           quiz->explanation, quiz->legal_source);
  if (s_feedback_label) {
    lv_label_set_text(s_feedback_label, message);
    lv_color_t color =
        correct ? lv_palette_main(LV_PALETTE_GREEN)
                : lv_palette_main(LV_PALETTE_RED);
    lv_obj_set_style_text_color(s_feedback_label, color, LV_PART_MAIN);
  }
  if (correct && s_close_btn) {
    lv_obj_clear_state(s_close_btn, LV_STATE_DISABLED);
  }
}

void compliance_show_quiz(compliance_topic_t topic) {
  if (topic >= COMPLIANCE_TOPIC_COUNT) {
    return;
  }
  const compliance_quiz_def_t *quiz = &s_quizzes[topic];
  if (!quiz->title) {
    return;
  }
  compliance_dismiss();

  s_active_topic = topic;
  s_modal = lv_obj_create(lv_scr_act());
  lv_obj_add_flag(s_modal, LV_OBJ_FLAG_MODAL);
  lv_obj_remove_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_modal, LV_PCT(85), LV_PCT(80));
  lv_obj_center(s_modal);
  lv_obj_set_style_pad_all(s_modal, 16, LV_PART_MAIN);
  lv_obj_set_style_pad_gap(s_modal, 12, LV_PART_MAIN);
  lv_obj_set_style_radius(s_modal, 12, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_modal, lv_palette_lighten(LV_PALETTE_GREY, 1),
                            LV_PART_MAIN);
  lv_obj_set_flex_flow(s_modal, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_modal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_START);

  lv_obj_t *title = lv_label_create(s_modal);
  lv_label_set_text(title, quiz->title);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);

  lv_obj_t *question = lv_label_create(s_modal);
  lv_label_set_text(question, quiz->question);
  lv_label_set_long_mode(question, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(question, LV_PCT(100));

  lv_obj_t *options = lv_obj_create(s_modal);
  lv_obj_remove_flag(options, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(options, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(options, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(options, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_gap(options, 10, LV_PART_MAIN);
  lv_obj_set_flex_flow(options, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_width(options, LV_PCT(100));

  for (uint8_t i = 0; i < quiz->option_count && i < 3; ++i) {
    lv_obj_t *btn = lv_btn_create(options);
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_add_event_cb(btn, compliance_option_event_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)i);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, quiz->options[i]);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(95));
    lv_obj_center(label);
  }

  s_feedback_label = lv_label_create(s_modal);
  lv_label_set_text(s_feedback_label,
                    "Sélectionnez la réponse conforme pour poursuivre.");
  lv_label_set_long_mode(s_feedback_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_feedback_label, LV_PCT(100));
  lv_obj_set_style_text_color(s_feedback_label,
                              lv_palette_darken(LV_PALETTE_GREY, 2),
                              LV_PART_MAIN);

  s_close_btn = lv_btn_create(s_modal);
  lv_obj_add_event_cb(s_close_btn, compliance_close_event_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_add_state(s_close_btn, LV_STATE_DISABLED);
  lv_obj_set_width(s_close_btn, 160);
  lv_obj_t *close_label = lv_label_create(s_close_btn);
  lv_label_set_text(close_label, "Fermer");
  lv_obj_center(close_label);
}

bool compliance_is_active(void) { return s_modal != NULL; }

void compliance_dismiss(void) {
  if (s_modal) {
    lv_obj_del(s_modal);
    s_modal = NULL;
    s_feedback_label = NULL;
    s_close_btn = NULL;
  }
}

const char *compliance_topic_reference(compliance_topic_t topic) {
  if (topic >= COMPLIANCE_TOPIC_COUNT) {
    return NULL;
  }
  return s_quizzes[topic].legal_source;
}
