#pragma once

#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

void i18n_init(void);
const char *i18n_translate(const char *key);
void i18n_set_language(app_lang_id_t lang);
app_lang_id_t i18n_get_language(void);

#ifdef __cplusplus
}
#endif
