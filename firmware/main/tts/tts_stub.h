#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t tts_stub_init(void);
void tts_stub_enable(bool enable);
bool tts_stub_is_enabled(void);
void tts_stub_speak(const char *text, bool interrupt);

#ifdef __cplusplus
}
#endif
