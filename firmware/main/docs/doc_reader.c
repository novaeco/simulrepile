#include "docs/doc_reader.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "assets/asset_cache.h"
#include "esp_check.h"
#include "esp_log.h"

#define DOC_READER_MAX_DOCS 32
#define DOC_READER_MAX_NAME_LEN 96
#define DOC_READER_MAX_PATH_LEN 256

static const char *TAG = "doc_reader";
static char s_root[128];
static char s_cached_names[DOC_READER_MAX_DOCS][DOC_READER_MAX_NAME_LEN];

static bool has_extension(const char *name, const char *ext)
{
    const char *dot = strrchr(name, '.');
    if (!dot || dot[1] == '\0') {
        return false;
    }
    return strcasecmp(dot + 1, ext) == 0;
}

static bool is_supported_extension(const char *name)
{
    if (!name) {
        return false;
    }
    return has_extension(name, "txt") || has_extension(name, "html") || has_extension(name, "htm");
}

static esp_err_t build_category_path(doc_category_t category, char *out_path, size_t len)
{
    const char *suffix = NULL;
    switch (category) {
    case DOC_CATEGORY_REGLEMENTAIRES:
        suffix = "/reglementaires";
        break;
    case DOC_CATEGORY_SPECIES:
        suffix = "/species";
        break;
    case DOC_CATEGORY_GUIDES:
        suffix = "/guides";
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    int written = snprintf(out_path, len, "%s%s", s_root, suffix);
    if (written <= 0 || written >= (int)len) {
        ESP_LOGE(TAG, "Category path overflow (%d/%zu)", written, len);
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static int compare_strings(const void *lhs, const void *rhs)
{
    const char *const *a = lhs;
    const char *const *b = rhs;
    return strcmp(*a, *b);
}

esp_err_t doc_reader_init(const char *root_path)
{
    if (!root_path) {
        return ESP_ERR_INVALID_ARG;
    }
    strlcpy(s_root, root_path, sizeof(s_root));
    ESP_LOGI(TAG, "Document root set to %s", s_root);

    static const char *k_categories[] = {
        "/reglementaires",
        "/species",
        "/guides",
    };
    for (size_t i = 0; i < sizeof(k_categories) / sizeof(k_categories[0]); ++i) {
        char path[DOC_READER_MAX_PATH_LEN];
        int written = snprintf(path, sizeof(path), "%s%s", s_root, k_categories[i]);
        if (written <= 0 || written >= (int)sizeof(path)) {
            ESP_LOGW(TAG, "Category path overflow for %s", k_categories[i]);
            continue;
        }
        struct stat st = {0};
        if (stat(path, &st) != 0) {
            ESP_LOGW(TAG, "Document category missing: %s (errno=%d)", path, errno);
        } else if (!S_ISDIR(st.st_mode)) {
            ESP_LOGW(TAG, "Document category path is not a directory: %s", path);
        }
    }
    return ESP_OK;
}

esp_err_t doc_reader_list(doc_category_t category, doc_descriptor_t *out_array, int max_items, int *out_count)
{
    if (!out_array || max_items <= 0 || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    if (max_items > DOC_READER_MAX_DOCS) {
        max_items = DOC_READER_MAX_DOCS;
    }

    char category_path[DOC_READER_MAX_PATH_LEN];
    ESP_RETURN_ON_ERROR(build_category_path(category, category_path, sizeof(category_path)), TAG, "Invalid category path");

    DIR *dir = opendir(category_path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open %s (errno=%d)", category_path, errno);
        return ESP_ERR_NOT_FOUND;
    }

    char local_names[DOC_READER_MAX_DOCS][DOC_READER_MAX_NAME_LEN];
    const char *name_ptrs[DOC_READER_MAX_DOCS];
    int local_count = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        if (!is_supported_extension(entry->d_name)) {
            continue;
        }
        if (local_count >= DOC_READER_MAX_DOCS) {
            ESP_LOGW(TAG, "Category %d reached maximum cached entries", category);
            break;
        }

        char entry_path[DOC_READER_MAX_PATH_LEN];
        int written = snprintf(entry_path, sizeof(entry_path), "%s/%s", category_path, entry->d_name);
        if (written <= 0 || written >= (int)sizeof(entry_path)) {
            ESP_LOGW(TAG, "Entry path overflow for %s", entry->d_name);
            continue;
        }

        struct stat st = {0};
        if (stat(entry_path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }

        strlcpy(local_names[local_count], entry->d_name, sizeof(local_names[local_count]));
        name_ptrs[local_count] = local_names[local_count];
        ++local_count;
    }
    closedir(dir);

    if (local_count == 0) {
        return ESP_OK;
    }

    qsort(name_ptrs, local_count, sizeof(name_ptrs[0]), compare_strings);

    int to_copy = local_count < max_items ? local_count : max_items;
    for (int i = 0; i < to_copy; ++i) {
        strlcpy(s_cached_names[i], name_ptrs[i], sizeof(s_cached_names[i]));
        out_array[i].category = category;
        out_array[i].path = s_cached_names[i];
    }
    *out_count = to_copy;

    if (local_count > max_items) {
        ESP_LOGW(TAG, "Doc list truncated: %d available, %d returned", local_count, max_items);
    }
    return ESP_OK;
}

esp_err_t doc_reader_load(const doc_descriptor_t *doc, char *buffer, int buffer_len, int *out_len)
{
    if (!doc || !buffer || buffer_len <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    char category_path[DOC_READER_MAX_PATH_LEN];
    ESP_RETURN_ON_ERROR(build_category_path(doc->category, category_path, sizeof(category_path)), TAG, "Invalid descriptor");

    char full_path[DOC_READER_MAX_PATH_LEN];
    int written = snprintf(full_path, sizeof(full_path), "%s/%s", category_path, doc->path);
    if (written <= 0 || written >= (int)sizeof(full_path)) {
        ESP_LOGE(TAG, "Full path overflow for %s", doc->path ? doc->path : "<null>");
        return ESP_ERR_INVALID_SIZE;
    }

    asset_handle_t handle;
    esp_err_t err = asset_cache_get(full_path, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Document %s missing", full_path);
        } else {
            ESP_LOGE(TAG, "Asset cache error %s for %s", esp_err_to_name(err), full_path);
        }
        return err;
    }

    size_t available = handle.size;
    size_t to_copy = available;
    bool truncated = false;
    if (buffer_len <= 1) {
        to_copy = 0;
        truncated = available > 0;
    } else if (to_copy > (size_t)(buffer_len - 1)) {
        to_copy = (size_t)(buffer_len - 1);
        truncated = true;
    }

    if (to_copy > 0 && handle.data) {
        memcpy(buffer, handle.data, to_copy);
    }
    buffer[to_copy] = '\0';
    if (out_len) {
        *out_len = (int)to_copy;
    }

    if (truncated) {
        ESP_LOGW(TAG, "Document %s truncated to %zu/%zu bytes", full_path, to_copy, available);
    }

    asset_cache_release(&handle);
    return truncated ? ESP_ERR_INVALID_SIZE : ESP_OK;
}
