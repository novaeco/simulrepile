#include "regulations.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "reptile_logic.h"

static const regulation_rule_t s_rules[] = {
    {
        .species_id = REPTILE_SPECIES_GECKO,
        .scientific_name = "Eublepharis macularius",
        .common_name = "Gecko léopard",
        .status = REGULATORY_STATUS_AUTHORIZED,
        .certificate_required = true,
        .certificate_text =
            "Certificat de cession + preuve d'origine (Annexe B - Règlement (CE) 338/97)",
        .register_required = true,
        .min_length_cm = 90.f,
        .min_width_cm = 45.f,
        .min_height_cm = 45.f,
        .day_temp_min = 28.f,
        .day_temp_max = 32.f,
        .night_temp_min = 24.f,
        .night_temp_max = 27.f,
        .humidity_min = 40.f,
        .humidity_max = 60.f,
        .uv_min = 2.f,
        .uv_max = 3.5f,
        .education_required = true,
        .education_text =
            "Affichage des conditions de détention et sensibilisation au prélèvement", 
        .legal_reference =
            "Arrêté du 8/10/2018 + Règlement (CE) 338/97",
    },
    {
        .species_id = REPTILE_SPECIES_PYTHON,
        .scientific_name = "Python regius",
        .common_name = "Python royal",
        .status = REGULATORY_STATUS_CONTROLLED,
        .certificate_required = true,
        .certificate_text =
            "Certificat intra-communautaire CITES B et registre Cerfa 12446*01",
        .register_required = true,
        .min_length_cm = 120.f,
        .min_width_cm = 60.f,
        .min_height_cm = 60.f,
        .day_temp_min = 30.f,
        .day_temp_max = 34.f,
        .night_temp_min = 26.f,
        .night_temp_max = 28.f,
        .humidity_min = 55.f,
        .humidity_max = 75.f,
        .uv_min = 2.5f,
        .uv_max = 4.0f,
        .education_required = true,
        .education_text =
            "Programme pédagogique sur la gestion des NAC soumis à autorisation",
        .legal_reference =
            "Code de l'environnement L413-2 et Arrêté du 8/10/2018",
    },
    {
        .species_id = REPTILE_SPECIES_TORTOISE,
        .scientific_name = "Testudo hermanni",
        .common_name = "Tortue d'Hermann",
        .status = REGULATORY_STATUS_CONTROLLED,
        .certificate_required = true,
        .certificate_text =
            "Certificat intra-communautaire (Annexe A) + marquage micro-puce",
        .register_required = true,
        .min_length_cm = 200.f,
        .min_width_cm = 100.f,
        .min_height_cm = 60.f,
        .day_temp_min = 27.f,
        .day_temp_max = 32.f,
        .night_temp_min = 20.f,
        .night_temp_max = 24.f,
        .humidity_min = 50.f,
        .humidity_max = 70.f,
        .uv_min = 3.f,
        .uv_max = 4.5f,
        .education_required = true,
        .education_text =
            "Panneau sur la protection de l'espèce et obligations de marquage",
        .legal_reference =
            "Règlement (CE) 338/97 + Arrêté du 8/10/2018",
    },
    {
        .species_id = REPTILE_SPECIES_CHAMELEON,
        .scientific_name = "Furcifer pardalis",
        .common_name = "Caméléon panthère",
        .status = REGULATORY_STATUS_CONTROLLED,
        .certificate_required = true,
        .certificate_text =
            "Certificat de cession CITES B et registre d'entrées/sorties",
        .register_required = true,
        .min_length_cm = 90.f,
        .min_width_cm = 60.f,
        .min_height_cm = 120.f,
        .day_temp_min = 29.f,
        .day_temp_max = 33.f,
        .night_temp_min = 22.f,
        .night_temp_max = 25.f,
        .humidity_min = 55.f,
        .humidity_max = 85.f,
        .uv_min = 4.f,
        .uv_max = 5.5f,
        .education_required = true,
        .education_text =
            "Sensibilisation à l'hygrométrie et à la gestion UV des caméléons",
        .legal_reference =
            "Arrêté du 8/10/2018 + Règlement (CE) 338/97",
    },
    {
        .species_id = REPTILE_SPECIES_CUSTOM,
        .scientific_name = "Profil personnalisé",
        .common_name = "Espèce non listée",
        .status = REGULATORY_STATUS_ASSESSMENT,
        .certificate_required = true,
        .certificate_text =
            "Validation préalable DDPP + pièces justificatives spécifiques",
        .register_required = true,
        .min_length_cm = 120.f,
        .min_width_cm = 60.f,
        .min_height_cm = 60.f,
        .day_temp_min = 26.f,
        .day_temp_max = 32.f,
        .night_temp_min = 22.f,
        .night_temp_max = 28.f,
        .humidity_min = 45.f,
        .humidity_max = 70.f,
        .uv_min = 2.5f,
        .uv_max = 5.5f,
        .education_required = true,
        .education_text =
            "Dossier pédagogique à construire selon l'espèce",
        .legal_reference =
            "Instruction préfectorale préalable + Code de l'environnement",
    },
};

size_t regulations_get_rules(const regulation_rule_t **rules_out) {
  if (rules_out) {
    *rules_out = s_rules;
  }
  return sizeof(s_rules) / sizeof(s_rules[0]);
}

const regulation_rule_t *regulations_get_rule(int species_id) {
  size_t count = sizeof(s_rules) / sizeof(s_rules[0]);
  for (size_t i = 0; i < count; ++i) {
    if (s_rules[i].species_id == species_id) {
      return &s_rules[i];
    }
  }
  return NULL;
}

const char *regulations_status_to_string(regulatory_status_t status) {
  switch (status) {
  case REGULATORY_STATUS_FORBIDDEN:
    return "Interdite";
  case REGULATORY_STATUS_CONTROLLED:
    return "Soumise à autorisation";
  case REGULATORY_STATUS_AUTHORIZED:
    return "Autorisée";
  case REGULATORY_STATUS_ASSESSMENT:
  default:
    return "Évaluation requise";
  }
}

static void set_reason(char *reason, size_t reason_len, const char *fmt,
                       const char *detail) {
  if (!reason || reason_len == 0 || !fmt) {
    return;
  }
  snprintf(reason, reason_len, fmt, detail ? detail : "");
}

esp_err_t regulations_validate_species(int species_id, char *reason,
                                       size_t reason_len) {
  const regulation_rule_t *rule = regulations_get_rule(species_id);
  if (!rule) {
    set_reason(reason, reason_len, "Espèce inconnue (%s)", "catalogue");
    return ESP_ERR_NOT_FOUND;
  }
  switch (rule->status) {
  case REGULATORY_STATUS_FORBIDDEN:
    set_reason(reason, reason_len, "Espèce interdite d'introduction: %s",
               rule->legal_reference);
    return ESP_ERR_INVALID_STATE;
  case REGULATORY_STATUS_ASSESSMENT:
    set_reason(reason, reason_len,
               "Validation administrative requise avant introduction (%s)",
               rule->legal_reference);
    return ESP_ERR_INVALID_STATE;
  default:
    break;
  }
  return ESP_OK;
}

static bool value_in_range(float value, float min_v, float max_v) {
  if (max_v <= min_v) {
    return true;
  }
  return value >= min_v && value <= max_v;
}

esp_err_t regulations_evaluate(const regulation_rule_t *rule,
                               const regulations_compliance_input_t *input,
                               regulations_compliance_report_t *report) {
  if (!rule || !input || !report) {
    return ESP_ERR_INVALID_ARG;
  }

  memset(report, 0, sizeof(*report));

  bool allowed = rule->status != REGULATORY_STATUS_FORBIDDEN &&
                 rule->status != REGULATORY_STATUS_ASSESSMENT;
  report->allowed = allowed;

  bool certificate_ok = true;
  if (rule->certificate_required) {
    certificate_ok = input->certificate_count > 0 && input->certificate_valid &&
                     !input->certificate_expired;
  }
  report->certificate_ok = certificate_ok;

  bool register_ok = true;
  if (rule->register_required) {
    register_ok = input->register_present;
  }
  report->register_ok = register_ok;

  bool dimensions_ok = input->length_cm >= rule->min_length_cm &&
                       input->width_cm >= rule->min_width_cm &&
                       input->height_cm >= rule->min_height_cm;
  report->dimensions_ok = dimensions_ok;

  bool education_ok = true;
  if (rule->education_required) {
    education_ok = input->education_present;
  }
  report->education_ok = education_ok;

  float temp_min = input->is_daytime ? rule->day_temp_min : rule->night_temp_min;
  float temp_max = input->is_daytime ? rule->day_temp_max : rule->night_temp_max;
  bool temp_ok = value_in_range(input->temperature_c, temp_min, temp_max);
  bool humidity_ok = value_in_range(input->humidity_pct, rule->humidity_min,
                                    rule->humidity_max);
  bool uv_ok = value_in_range(input->uv_index, rule->uv_min, rule->uv_max);
  report->environment_ok = temp_ok && humidity_ok && uv_ok;

  report->blocking = !report->allowed || !report->dimensions_ok ||
                     !report->certificate_ok || !report->register_ok;

  return ESP_OK;
}
