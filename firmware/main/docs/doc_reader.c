#include "docs/doc_reader.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#define MAX_DOCS 32

static const char *TAG = "doc_reader";
static char s_root[128];

static const char *category_to_path(doc_category_t category)
{
    switch (category) {
    case DOC_CATEGORY_REGLEMENTAIRES:
        return "/reglementaires";
    case DOC_CATEGORY_SPECIES:
        return "/species";
    case DOC_CATEGORY_GUIDES:
        return "/guides";
    default:
        return "/unknown";
    }
}

esp_err_t doc_reader_init(const char *root_path)
{
    if (!root_path) {
        return ESP_ERR_INVALID_ARG;
    }
    strlcpy(s_root, root_path, sizeof(s_root));
    ESP_LOGI(TAG, "Document root set to %s", s_root);
    return ESP_OK;
}

esp_err_t doc_reader_list(doc_category_t category, doc_descriptor_t *out_array, int max_items, int *out_count)
{
    if (!out_array || max_items <= 0 || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    // Stub: expose fixed entries to ensure UI integration tests can proceed.
    if (max_items < 1) {
        return ESP_OK;
    }
    out_array[0].category = category;
    switch (category) {
    case DOC_CATEGORY_REGLEMENTAIRES:
        out_array[0].path = "disclaimer_fr.txt";
        break;
    case DOC_CATEGORY_SPECIES:
        out_array[0].path = "python_regius.txt";
        break;
    case DOC_CATEGORY_GUIDES:
        out_array[0].path = "maintenance_hebdo.txt";
        break;
    }
    *out_count = 1;
    return ESP_OK;
}

esp_err_t doc_reader_load(const doc_descriptor_t *doc, char *buffer, int buffer_len, int *out_len)
{
    if (!doc || !buffer || buffer_len <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s%s/%s", s_root, category_to_path(doc->category), doc->path);
    ESP_LOGI(TAG, "Loading doc %s", full_path);
    int written = snprintf(buffer, buffer_len, "[STUB]%s", doc->path);
    if (out_len) {
        *out_len = written;
    }
    return ESP_OK;
}
