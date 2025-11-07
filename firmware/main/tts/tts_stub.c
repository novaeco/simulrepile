#include "tts/tts_stub.h"

#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "tts_stub";
static bool s_initialized = false;
static bool s_enabled = false;

esp_err_t tts_stub_init(void)
{
#if !CONFIG_APP_ENABLE_TTS_STUB
    ESP_LOGI(TAG, "TTS stub disabled via Kconfig");
    s_initialized = true;
    s_enabled = false;
    return ESP_OK;
#else
    if (s_initialized) {
        return ESP_OK;
    }
    s_initialized = true;
    s_enabled = true;
    ESP_LOGI(TAG, "TTS stub initialized (logging mode)");
    return ESP_OK;
#endif
}

void tts_stub_enable(bool enable)
{
#if CONFIG_APP_ENABLE_TTS_STUB
    if (!s_initialized) {
        (void)tts_stub_init();
    }
    s_enabled = enable;
    ESP_LOGI(TAG, "TTS %s", enable ? "enabled" : "disabled");
#else
    (void)enable;
#endif
}

bool tts_stub_is_enabled(void)
{
#if CONFIG_APP_ENABLE_TTS_STUB
    if (!s_initialized) {
        (void)tts_stub_init();
    }
    return s_enabled;
#else
    return false;
#endif
}

void tts_stub_speak(const char *text, bool interrupt)
{
#if CONFIG_APP_ENABLE_TTS_STUB
    if (!tts_stub_is_enabled() || !text || text[0] == '\0') {
        return;
    }
    ESP_LOGI(TAG, "[TTS:%s] %s", interrupt ? "INT" : "QUEUED", text);
#else
    (void)text;
    (void)interrupt;
#endif
}
