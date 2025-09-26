#include "i18n/i18n_manager.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "i18n";
static char s_root[128];
static i18n_language_t s_current = I18N_LANG_FR;

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

esp_err_t i18n_manager_init(const char *root_path)
{
    if (!root_path) {
        return ESP_ERR_INVALID_ARG;
    }
    strlcpy(s_root, root_path, sizeof(s_root));
    ESP_LOGI(TAG, "I18N root set to %s", s_root);
    return ESP_OK;
}

esp_err_t i18n_manager_set_language(i18n_language_t lang)
{
    s_current = lang;
    ESP_LOGI(TAG, "Language set to %s", lookup_language_path(lang));
    return ESP_OK;
}

const char *i18n_manager_get_string(const char *key)
{
    (void)key;
    switch (s_current) {
    case I18N_LANG_FR:
        return "Texte en français";
    case I18N_LANG_EN:
        return "Text in English";
    case I18N_LANG_DE:
        return "Text auf Deutsch";
    case I18N_LANG_ES:
        return "Texto en español";
    default:
        return key;
    }
}
