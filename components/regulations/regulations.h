#ifndef REGULATIONS_H
#define REGULATIONS_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  REGULATORY_STATUS_FORBIDDEN = 0,
  REGULATORY_STATUS_CONTROLLED,
  REGULATORY_STATUS_AUTHORIZED,
  REGULATORY_STATUS_ASSESSMENT,
} regulatory_status_t;

typedef struct {
  int species_id;
  const char *scientific_name;
  const char *common_name;
  regulatory_status_t status;
  bool certificate_required;
  const char *certificate_text;
  bool register_required;
  float min_length_cm;
  float min_width_cm;
  float min_height_cm;
  float day_temp_min;
  float day_temp_max;
  float night_temp_min;
  float night_temp_max;
  float humidity_min;
  float humidity_max;
  float uv_min;
  float uv_max;
  bool education_required;
  const char *education_text;
  const char *legal_reference;
} regulation_rule_t;

typedef struct {
  float length_cm;
  float width_cm;
  float height_cm;
  float temperature_c;
  float humidity_pct;
  float uv_index;
  bool is_daytime;
  unsigned int certificate_count;
  bool certificate_valid;
  bool certificate_expired;
  bool register_present;
  bool education_present;
} regulations_compliance_input_t;

typedef struct {
  bool allowed;
  bool certificate_ok;
  bool register_ok;
  bool dimensions_ok;
  bool education_ok;
  bool environment_ok;
  bool blocking;
} regulations_compliance_report_t;

size_t regulations_get_rules(const regulation_rule_t **rules_out);
const regulation_rule_t *regulations_get_rule(int species_id);
const char *regulations_status_to_string(regulatory_status_t status);
esp_err_t regulations_validate_species(int species_id, char *reason,
                                       size_t reason_len);
esp_err_t regulations_evaluate(const regulation_rule_t *rule,
                               const regulations_compliance_input_t *input,
                               regulations_compliance_report_t *report);

#ifdef __cplusplus
}
#endif

#endif /* REGULATIONS_H */
