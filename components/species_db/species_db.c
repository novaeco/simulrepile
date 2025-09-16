#include "species_db.h"
#include <string.h>

static const species_db_entry_t s_species_db[] = {
    {
        .id = "pogona_vitticeps",
        .common_name = "Agame barbu central",
        .scientific_name = "Pogona vitticeps",
        .terrarium_min = {.length_cm = 120, .width_cm = 60, .height_cm = 60},
        .environment = {.temperature_min_c = 28,
                        .temperature_max_c = 38,
                        .humidity_min_pct = 30,
                        .humidity_max_pct = 45,
                        .uv_index_min = 4,
                        .uv_index_max = 6},
        .certificate_required = false,
        .certificate_code = "",
        .legal_reference =
            "Arrêté du 8 octobre 2018 - annexe 2 (Espèces domestiques)",
        .is_protected = false,
        .protected_reference = "",
    },
    {
        .id = "python_regius",
        .common_name = "Python royal",
        .scientific_name = "Python regius",
        .terrarium_min = {.length_cm = 120, .width_cm = 60, .height_cm = 60},
        .environment = {.temperature_min_c = 26,
                        .temperature_max_c = 32,
                        .humidity_min_pct = 50,
                        .humidity_max_pct = 65,
                        .uv_index_min = 2,
                        .uv_index_max = 3},
        .certificate_required = true,
        .certificate_code = "CDC_APA_2022_123",
        .legal_reference =
            "Arrêté du 8 octobre 2018 - annexe 2 (Espèces soumises à CDC)",
        .is_protected = true,
        .protected_reference =
            "Règlement (CE) n° 338/97 annexe B - certificat intra-UE CITES",
    },
    {
        .id = "eublepharis_macularius",
        .common_name = "Gecko léopard",
        .scientific_name = "Eublepharis macularius",
        .terrarium_min = {.length_cm = 90, .width_cm = 45, .height_cm = 45},
        .environment = {.temperature_min_c = 24,
                        .temperature_max_c = 32,
                        .humidity_min_pct = 30,
                        .humidity_max_pct = 45,
                        .uv_index_min = 2,
                        .uv_index_max = 4},
        .certificate_required = false,
        .certificate_code = "",
        .legal_reference =
            "Arrêté du 10 août 2004 modifié (animaux domestiques)",
        .is_protected = false,
        .protected_reference = "",
    },
};

static const size_t s_species_db_count =
    sizeof(s_species_db) / sizeof(s_species_db[0]);

extern const uint8_t species_catalogue_json_start[] asm(
    "_binary_species_catalogue_json_start");
extern const uint8_t species_catalogue_json_end[] asm(
    "_binary_species_catalogue_json_end");
extern const uint8_t species_catalogue_csv_start[] asm(
    "_binary_species_catalogue_csv_start");
extern const uint8_t species_catalogue_csv_end[] asm(
    "_binary_species_catalogue_csv_end");

size_t species_db_count(void) { return s_species_db_count; }

const species_db_entry_t *species_db_get(size_t index) {
  if (index >= s_species_db_count) {
    return NULL;
  }
  return &s_species_db[index];
}

const species_db_entry_t *species_db_get_by_id(const char *id) {
  if (!id || id[0] == '\0') {
    return NULL;
  }
  for (size_t i = 0; i < s_species_db_count; ++i) {
    if (strcmp(s_species_db[i].id, id) == 0) {
      return &s_species_db[i];
    }
  }
  return NULL;
}

bool species_db_dimensions_satisfied(const species_db_entry_t *entry,
                                      uint16_t length_cm, uint16_t width_cm,
                                      uint16_t height_cm) {
  if (!entry) {
    return false;
  }
  const species_dimension_requirements_t *req = &entry->terrarium_min;
  return length_cm >= req->length_cm && width_cm >= req->width_cm &&
         height_cm >= req->height_cm;
}

size_t species_db_filter_by_dimensions(uint16_t length_cm, uint16_t width_cm,
                                       uint16_t height_cm,
                                       const species_db_entry_t **entries,
                                       size_t max_entries) {
  if (!entries || max_entries == 0) {
    return 0;
  }
  size_t count = 0;
  for (size_t i = 0; i < s_species_db_count && count < max_entries; ++i) {
    if (species_db_dimensions_satisfied(&s_species_db[i], length_cm, width_cm,
                                        height_cm)) {
      entries[count++] = &s_species_db[i];
    }
  }
  return count;
}

const char *species_db_get_catalog_json(size_t *size) {
  if (size) {
    *size = (size_t)(species_catalogue_json_end -
                     species_catalogue_json_start);
  }
  return (const char *)species_catalogue_json_start;
}

const char *species_db_get_catalog_csv(size_t *size) {
  if (size) {
    *size =
        (size_t)(species_catalogue_csv_end - species_catalogue_csv_start);
  }
  return (const char *)species_catalogue_csv_start;
}
