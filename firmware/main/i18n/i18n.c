#include "i18n.h"

#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "i18n";

static app_lang_id_t s_current_lang = APP_DEFAULT_LANGUAGE;

void i18n_init(void)
{
    ESP_LOGI(TAG, "Language initialised to %d", s_current_lang);
}

const char *i18n_translate(const char *key)
{
    (void)key;
    switch (s_current_lang) {
    case APP_LANG_ID_FR:
        return "(fr)";
    case APP_LANG_ID_EN:
        return "(en)";
    case APP_LANG_ID_DE:
        return "(de)";
    case APP_LANG_ID_ES:
        return "(es)";
    default:
        return "";
    }
}

void i18n_set_language(app_lang_id_t lang)
{
    s_current_lang = lang;
}

app_lang_id_t i18n_get_language(void)
{
    return s_current_lang;
}
