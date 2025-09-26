#pragma once

#include "app_config.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DOC_CATEGORY_REGULATORY = 0,
    DOC_CATEGORY_SPECIES,
    DOC_CATEGORY_GUIDES,
    DOC_CATEGORY_ALL,
} doc_category_t;

typedef struct {
    doc_category_t category;
    char name[64];
    char path[APP_SD_PATH_MAX_LEN];
} doc_entry_t;

size_t doc_reader_list(doc_category_t category, doc_entry_t *entries, size_t max_entries);
size_t doc_reader_search(const char *query, doc_entry_t *entries, size_t max_entries);
int doc_reader_load(const doc_entry_t *entry, char *buffer, size_t buffer_len);
const char *doc_reader_category_path(doc_category_t category);

#ifdef __cplusplus
}
#endif
