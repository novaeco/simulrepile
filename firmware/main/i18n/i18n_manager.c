#include "i18n/i18n_manager.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"

typedef struct {
    bool loaded;
    cJSON *document;
    cJSON *strings;
} i18n_catalog_t;

static const char *TAG = "i18n";
static char s_root[128];
static i18n_language_t s_current = I18N_LANG_FR;
static i18n_catalog_t s_catalogs[I18N_LANG_COUNT];

static const char *lookup_language_path(i18n_language_t lang)
{
    switch (lang) {
    case I18N_LANG_FR:
        return "fr";
    case I18N_LANG_EN:
        return "en";
    case I18N_LANG_DE:
        return "de";
    case I18N_LANG_ES:
        return "es";
    default:
        return "fr";
    }
}

static void reset_catalogs(void)
{
    for (int i = 0; i < I18N_LANG_COUNT; ++i) {
        if (s_catalogs[i].document) {
            cJSON_Delete(s_catalogs[i].document);
        }
        s_catalogs[i].document = NULL;
        s_catalogs[i].strings = NULL;
        s_catalogs[i].loaded = false;
    }
}

static esp_err_t load_catalog_from_disk(i18n_language_t lang)
{
    if (lang < 0 || lang >= I18N_LANG_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[256];
    const char *lang_code = lookup_language_path(lang);
    int written = snprintf(path, sizeof(path), "%s/%s.json", s_root, lang_code);
    if (written <= 0 || written >= (int)sizeof(path)) {
        ESP_LOGE(TAG, "Language path overflow for %s", lang_code);
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open %s: errno=%d", path, errno);
        return ESP_ERR_NOT_FOUND;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        ESP_LOGE(TAG, "Failed to seek %s", path);
        return ESP_FAIL;
    }
    long length = ftell(file);
    if (length < 0) {
        fclose(file);
        ESP_LOGE(TAG, "Failed to determine length for %s", path);
        return ESP_FAIL;
    }
    rewind(file);

    char *buffer = malloc((size_t)length + 1);
    if (!buffer) {
        fclose(file);
        ESP_LOGE(TAG, "Out of memory loading %s", path);
        return ESP_ERR_NO_MEM;
    }

    size_t read = fread(buffer, 1, (size_t)length, file);
    fclose(file);
    if (read != (size_t)length) {
        free(buffer);
        ESP_LOGE(TAG, "Short read on %s (expected %ld, got %zu)", path, length, read);
        return ESP_ERR_INVALID_SIZE;
    }
    buffer[length] = '\0';

    cJSON *doc = cJSON_ParseWithLength(buffer, (size_t)length);
    free(buffer);
    if (!doc) {
        const char *err_ptr = cJSON_GetErrorPtr();
        ESP_LOGE(TAG, "JSON parse error in %s near %s", path, err_ptr ? err_ptr : "<unknown>");
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *strings = cJSON_GetObjectItemCaseSensitive(doc, "strings");
    if (!cJSON_IsObject(strings)) {
        cJSON_Delete(doc);
        ESP_LOGE(TAG, "Missing 'strings' object in %s", path);
        return ESP_ERR_INVALID_RESPONSE;
    }

    s_catalogs[lang].document = doc;
    s_catalogs[lang].strings = strings;
    s_catalogs[lang].loaded = true;
    ESP_LOGI(TAG, "Loaded language catalog %s (%ld bytes)", lang_code, length);
    return ESP_OK;
}

static esp_err_t ensure_catalog(i18n_language_t lang)
{
    if (lang < 0 || lang >= I18N_LANG_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_catalogs[lang].loaded) {
        return s_catalogs[lang].document ? ESP_OK : ESP_FAIL;
    }

    esp_err_t err = load_catalog_from_disk(lang);
    if (err != ESP_OK) {
        s_catalogs[lang].loaded = true;
        s_catalogs[lang].document = NULL;
        s_catalogs[lang].strings = NULL;
    }
    return err;
}

static const char *catalog_lookup(i18n_language_t lang, const char *key)
{
    if (lang < 0 || lang >= I18N_LANG_COUNT) {
        return NULL;
    }
    if (!s_catalogs[lang].document || !s_catalogs[lang].strings || !key) {
        return NULL;
    }

    const cJSON *entry = cJSON_GetObjectItemCaseSensitive(s_catalogs[lang].strings, key);
    if (!cJSON_IsString(entry) || !entry->valuestring) {
        return NULL;
    }
    return entry->valuestring;
}

esp_err_t i18n_manager_init(const char *root_path)
{
    if (!root_path) {
        return ESP_ERR_INVALID_ARG;
    }
    reset_catalogs();
    s_current = I18N_LANG_FR;
    strlcpy(s_root, root_path, sizeof(s_root));
    ESP_LOGI(TAG, "I18N root set to %s", s_root);

    esp_err_t err = ensure_catalog(s_current);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Default language preload failed: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

esp_err_t i18n_manager_set_language(i18n_language_t lang)
{
    esp_err_t err = ensure_catalog(lang);
    if (err == ESP_OK) {
        s_current = lang;
        ESP_LOGI(TAG, "Language set to %s", lookup_language_path(lang));
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Failed to load language %s (%s), keeping %s", lookup_language_path(lang),
             esp_err_to_name(err), lookup_language_path(s_current));

    if (lang != I18N_LANG_FR && s_current != I18N_LANG_FR) {
        if (ensure_catalog(I18N_LANG_FR) == ESP_OK) {
            s_current = I18N_LANG_FR;
            ESP_LOGW(TAG, "Fallback to default language FR");
        }
    }
    return err;
}

const char *i18n_manager_get_string(const char *key)
{
    if (!key || !key[0]) {
        return "";
    }

    if (ensure_catalog(s_current) != ESP_OK && s_current != I18N_LANG_FR) {
        if (ensure_catalog(I18N_LANG_FR) == ESP_OK) {
            s_current = I18N_LANG_FR;
        }
    }

    const char *value = catalog_lookup(s_current, key);
    if (!value && s_current != I18N_LANG_FR) {
        if (ensure_catalog(I18N_LANG_FR) == ESP_OK) {
            value = catalog_lookup(I18N_LANG_FR, key);
        }
    }
    return value ? value : key;
}
