#ifndef SPECIES_DB_H
#define SPECIES_DB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPECIES_DB_ID_MAX_LEN 32
#define SPECIES_DB_NAME_MAX_LEN 64
#define SPECIES_DB_LEGAL_REF_MAX_LEN 128
#define SPECIES_DB_CERT_CODE_MAX_LEN 48

typedef struct {
  uint16_t length_cm;
  uint16_t width_cm;
  uint16_t height_cm;
} species_dimension_requirements_t;

typedef struct {
  uint16_t temperature_min_c;
  uint16_t temperature_max_c;
  uint8_t humidity_min_pct;
  uint8_t humidity_max_pct;
  uint8_t uv_index_min;
  uint8_t uv_index_max;
} species_environment_profile_t;

typedef struct {
  const char *id;
  const char *common_name;
  const char *scientific_name;
  species_dimension_requirements_t terrarium_min;
  species_environment_profile_t environment;
  bool certificate_required;
  const char *certificate_code;
  const char *legal_reference;
} species_db_entry_t;

size_t species_db_count(void);
const species_db_entry_t *species_db_get(size_t index);
const species_db_entry_t *species_db_get_by_id(const char *id);
bool species_db_dimensions_satisfied(const species_db_entry_t *entry,
                                      uint16_t length_cm, uint16_t width_cm,
                                      uint16_t height_cm);
size_t species_db_filter_by_dimensions(uint16_t length_cm, uint16_t width_cm,
                                       uint16_t height_cm,
                                       const species_db_entry_t **entries,
                                       size_t max_entries);
const char *species_db_get_catalog_json(size_t *size);
const char *species_db_get_catalog_csv(size_t *size);

#ifdef __cplusplus
}
#endif

#endif /* SPECIES_DB_H */
