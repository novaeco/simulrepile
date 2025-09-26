#include "doc_reader.h"

#include "esp_log.h"

#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>

#define DOC_MAX_ENTRIES 128

static const char *TAG = "doc_reader";

static const char *strcasestr_local(const char *haystack, const char *needle)
{
    if (!haystack || !needle || *needle == '\0') {
        return haystack;
    }
    size_t needle_len = strlen(needle);
    for (const char *p = haystack; *p; ++p) {
        size_t i = 0;
        while (i < needle_len && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            ++i;
        }
        if (i == needle_len) {
            return p;
        }
    }
    return NULL;
}

static const char *category_root(doc_category_t category)
{
    switch (category) {
    case DOC_CATEGORY_REGULATORY:
        return APP_DOCS_REGULATORY_DIR;
    case DOC_CATEGORY_SPECIES:
        return APP_DOCS_SPECIES_DIR;
    case DOC_CATEGORY_GUIDES:
        return APP_DOCS_GUIDES_DIR;
    case DOC_CATEGORY_ALL:
    default:
        return APP_SD_DOCS_DIR;
    }
}

const char *doc_reader_category_path(doc_category_t category)
{
    return category_root(category);
}

static bool match_extension(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) {
        return false;
    }
    return strcasecmp(dot, ".txt") == 0 || strcasecmp(dot, ".html") == 0 || strcasecmp(dot, ".htm") == 0;
}

static size_t scan_directory(const char *root, doc_category_t category, doc_entry_t *entries, size_t max_entries)
{
    DIR *dir = opendir(root);
    if (!dir) {
        ESP_LOGW(TAG, "Directory %s unavailable", root);
        return 0;
    }
    struct dirent *entry;
    size_t count = 0;
    while ((entry = readdir(dir)) != NULL && count < max_entries) {
        if (entry->d_type != DT_REG) {
            continue;
        }
        if (!match_extension(entry->d_name)) {
            continue;
        }
        doc_entry_t *out = &entries[count++];
        out->category = category;
        snprintf(out->name, sizeof(out->name), "%s", entry->d_name);
        snprintf(out->path, sizeof(out->path), "%s/%s", root, entry->d_name);
    }
    closedir(dir);
    return count;
}

size_t doc_reader_list(doc_category_t category, doc_entry_t *entries, size_t max_entries)
{
    if (!entries || max_entries == 0) {
        return 0;
    }
    if (category == DOC_CATEGORY_ALL) {
        size_t total = 0;
        total += scan_directory(APP_DOCS_REGULATORY_DIR, DOC_CATEGORY_REGULATORY, entries + total,
                                total < max_entries ? max_entries - total : 0);
        total += scan_directory(APP_DOCS_SPECIES_DIR, DOC_CATEGORY_SPECIES, entries + total,
                                total < max_entries ? max_entries - total : 0);
        total += scan_directory(APP_DOCS_GUIDES_DIR, DOC_CATEGORY_GUIDES, entries + total,
                                total < max_entries ? max_entries - total : 0);
        return total;
    }
    return scan_directory(category_root(category), category, entries, max_entries);
}

size_t doc_reader_search(const char *query, doc_entry_t *entries, size_t max_entries)
{
    if (!query || !entries || max_entries == 0) {
        return 0;
    }
    doc_entry_t temp[DOC_MAX_ENTRIES];
    size_t total = doc_reader_list(DOC_CATEGORY_ALL, temp, DOC_MAX_ENTRIES);
    size_t match_count = 0;
    for (size_t i = 0; i < total && match_count < max_entries; ++i) {
        if (strcasestr_local(temp[i].name, query)) {
            entries[match_count++] = temp[i];
        }
    }
    return match_count;
}

int doc_reader_load(const doc_entry_t *entry, char *buffer, size_t buffer_len)
{
    if (!entry || !buffer || buffer_len == 0) {
        return -1;
    }
    FILE *f = fopen(entry->path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s", entry->path);
        return -1;
    }
    size_t read = fread(buffer, 1, buffer_len - 1, f);
    buffer[read] = '\0';
    fclose(f);
    return (int)read;
}
