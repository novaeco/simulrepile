#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DOC_CATEGORY_REGLEMENTAIRES,
    DOC_CATEGORY_SPECIES,
    DOC_CATEGORY_GUIDES,
} doc_category_t;

typedef struct {
    doc_category_t category;
    const char *path;
} doc_descriptor_t;

esp_err_t doc_reader_init(const char *root_path);
esp_err_t doc_reader_list(doc_category_t category, doc_descriptor_t *out_array, int max_items, int *out_count);
esp_err_t doc_reader_load(const doc_descriptor_t *doc, char *buffer, int buffer_len, int *out_len);

#ifdef __cplusplus
}
#endif
