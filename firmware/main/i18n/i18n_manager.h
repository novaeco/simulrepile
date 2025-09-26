#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    I18N_LANG_FR,
    I18N_LANG_EN,
    I18N_LANG_DE,
    I18N_LANG_ES,
    I18N_LANG_COUNT,
} i18n_language_t;

esp_err_t i18n_manager_init(const char *root_path);
esp_err_t i18n_manager_set_language(i18n_language_t lang);
const char *i18n_manager_get_string(const char *key);

#ifdef __cplusplus
}
#endif
