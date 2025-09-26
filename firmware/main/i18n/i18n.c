#include "i18n.h"

#include "app_config.h"
#include "esp_log.h"

#include <cJSON.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define I18N_MAX_ENTRIES 128

typedef struct {
    char key[64];
    char value[160];
} translation_entry_t;

static const char *TAG = "i18n";
static translation_entry_t s_entries[I18N_MAX_ENTRIES];
static size_t s_entry_count = 0;
static app_lang_id_t s_current_lang = APP_DEFAULT_LANGUAGE;

static const char *lang_code(app_lang_id_t lang)
{
    switch (lang) {
    case APP_LANG_ID_FR:
        return "fr";
    case APP_LANG_ID_EN:
        return "en";
    case APP_LANG_ID_DE:
        return "de";
    case APP_LANG_ID_ES:
        return "es";
    default:
        return APP_DEFAULT_LANGUAGE_CODE;
    }
}

static int load_translations_from_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Unable to open language file %s (%d)", path, errno);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buffer = malloc((size_t)size + 1);
    if (!buffer) {
        fclose(f);
        return -1;
    }
    fread(buffer, 1, (size_t)size, f);
    buffer[size] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buffer);
    free(buffer);
    if (!root) {
        ESP_LOGE(TAG, "Invalid JSON in %s", path);
        return -1;
    }

    memset(s_entries, 0, sizeof(s_entries));
    s_entry_count = 0;
    cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, root)
    {
        if (!cJSON_IsString(entry) || s_entry_count >= I18N_MAX_ENTRIES) {
            continue;
        }
        snprintf(s_entries[s_entry_count].key, sizeof(s_entries[s_entry_count].key), "%s", entry->string);
        snprintf(s_entries[s_entry_count].value, sizeof(s_entries[s_entry_count].value), "%s", entry->valuestring);
        ++s_entry_count;
    }
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Loaded %zu translations from %s", s_entry_count, path);
    return 0;
}

static void build_language_path(app_lang_id_t lang, char *buffer, size_t len)
{
    snprintf(buffer, len, "%s/%s.json", APP_SD_I18N_DIR, lang_code(lang));
}

void i18n_init(void)
{
    char path[APP_SD_PATH_MAX_LEN];
    build_language_path(APP_DEFAULT_LANGUAGE, path, sizeof(path));
    if (load_translations_from_file(path) != 0) {
        ESP_LOGW(TAG, "Falling back to builtin key display");
        s_entry_count = 0;
    }
    s_current_lang = APP_DEFAULT_LANGUAGE;
}

const char *i18n_translate(const char *key)
{
    if (!key) {
        return "";
    }
    for (size_t i = 0; i < s_entry_count; ++i) {
        if (strcmp(s_entries[i].key, key) == 0) {
            return s_entries[i].value;
        }
    }
    return key;
}

void i18n_set_language(app_lang_id_t lang)
{
    char path[APP_SD_PATH_MAX_LEN];
    build_language_path(lang, path, sizeof(path));
    if (load_translations_from_file(path) == 0) {
        s_current_lang = lang;
    } else {
        ESP_LOGE(TAG, "Language switch failed for %s", path);
    }
}

app_lang_id_t i18n_get_language(void)
{
    return s_current_lang;
}
