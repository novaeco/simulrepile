#include "tts/tts_stub.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TTS_CHUNK_SAMPLES 256

static const char *TAG = "tts";
static bool s_initialized = false;
static bool s_enabled = false;

#if CONFIG_APP_ENABLE_TTS_SYNTH
static i2s_chan_handle_t s_i2s = NULL;
static SemaphoreHandle_t s_mutex = NULL;
static bool s_synth_ready = false;
static volatile bool s_abort_requested = false;
static float s_phase = 0.0f;
static float s_formant = 0.0f;
static bool s_stub_fallback = false;
#else
static const bool s_stub_fallback = true;
#endif

#if CONFIG_APP_ENABLE_TTS_SYNTH
static inline float tts_gain(void)
{
    return ((float)CONFIG_APP_TTS_SYNTH_GAIN_PERCENT) / 100.0f;
}

static float clampf(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static esp_err_t tts_synth_init(void)
{
    if (s_synth_ready) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_APP_TTS_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    chan_cfg.auto_clear = true;

    esp_err_t err = i2s_new_channel(&chan_cfg, &s_i2s, NULL);
    if (err != ESP_OK) {
        s_i2s = NULL;
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_APP_TTS_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_APP_TTS_I2S_BCK,
            .ws = CONFIG_APP_TTS_I2S_WS,
            .dout = CONFIG_APP_TTS_I2S_DATA,
            .din = I2S_GPIO_UNUSED,
        },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;

    err = i2s_channel_init_std_mode(s_i2s, &std_cfg);
    if (err != ESP_OK) {
        i2s_del_channel(s_i2s);
        s_i2s = NULL;
        return err;
    }

    err = i2s_channel_enable(s_i2s);
    if (err != ESP_OK) {
        i2s_del_channel(s_i2s);
        s_i2s = NULL;
        return err;
    }

    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex) {
            i2s_channel_disable(s_i2s);
            i2s_del_channel(s_i2s);
            s_i2s = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    s_synth_ready = true;
    s_phase = 0.0f;
    s_formant = 0.0f;
    return ESP_OK;
}

static bool tts_should_abort(void)
{
    return s_abort_requested;
}

static void tts_write(const int16_t *samples, size_t sample_count)
{
    if (!s_i2s || sample_count == 0) {
        return;
    }
    size_t bytes_total = sample_count * sizeof(int16_t);
    size_t offset = 0;
    while (offset < bytes_total && !tts_should_abort()) {
        size_t written = 0;
        (void)i2s_channel_write(s_i2s,
                                (const uint8_t *)samples + offset,
                                bytes_total - offset,
                                &written,
                                portMAX_DELAY);
        if (written == 0) {
            break;
        }
        offset += written;
    }
}

static void tts_render_silence(float duration_ms)
{
    if (duration_ms <= 0.0f) {
        return;
    }
    size_t total_samples = (size_t)((duration_ms / 1000.0f) * (float)CONFIG_APP_TTS_SAMPLE_RATE);
    int16_t buffer[TTS_CHUNK_SAMPLES] = {0};
    size_t remaining = total_samples;
    while (remaining > 0 && !tts_should_abort()) {
        size_t chunk = remaining > TTS_CHUNK_SAMPLES ? TTS_CHUNK_SAMPLES : remaining;
        tts_write(buffer, chunk);
        remaining -= chunk;
    }
}

static void tts_render_wave(float freq_hz, float duration_ms, float timbre)
{
    if (freq_hz <= 0.0f || duration_ms <= 0.0f) {
        return;
    }
    size_t total_samples = (size_t)((duration_ms / 1000.0f) * (float)CONFIG_APP_TTS_SAMPLE_RATE);
    if (total_samples == 0) {
        return;
    }

    float base_step = 2.0f * (float)M_PI * freq_hz / (float)CONFIG_APP_TTS_SAMPLE_RATE;
    float formant_step = 2.0f * (float)M_PI * freq_hz * (1.5f + timbre) /
                         (float)CONFIG_APP_TTS_SAMPLE_RATE;
    int16_t buffer[TTS_CHUNK_SAMPLES];
    size_t produced = 0;
    float gain = tts_gain();
    while (produced < total_samples && !tts_should_abort()) {
        size_t chunk = (total_samples - produced) > TTS_CHUNK_SAMPLES ? TTS_CHUNK_SAMPLES : (total_samples - produced);
        for (size_t i = 0; i < chunk; ++i) {
            float env = sinf((float)M_PI * ((float)(produced + i) / (float)total_samples));
            float sample = env * gain * (0.8f * sinf(s_phase) + 0.3f * sinf(s_formant));
            buffer[i] = (int16_t)(clampf(sample, -1.0f, 1.0f) * 32767.0f);
            s_phase += base_step;
            s_formant += formant_step;
            if (s_phase > 2.0f * (float)M_PI) {
                s_phase -= 2.0f * (float)M_PI;
            }
            if (s_formant > 2.0f * (float)M_PI) {
                s_formant -= 2.0f * (float)M_PI;
            }
        }
        tts_write(buffer, chunk);
        produced += chunk;
    }
}

static void tts_render_character(char symbol)
{
    if (tts_should_abort()) {
        return;
    }
    switch (symbol) {
    case ' ': tts_render_silence(90.0f); return;
    case '\n': tts_render_silence(150.0f); return;
    case '.': case '!': case '?': tts_render_silence(230.0f); return;
    case ',': case ';': case ':': tts_render_silence(120.0f); return;
    case '-': tts_render_silence(60.0f); return;
    case '\'': tts_render_silence(40.0f); return;
    default: break;
    }

    if (symbol >= '0' && symbol <= '9') {
        static const char *const k_digits[10] = {"ZERO","UN","DEUX","TROIS","QUATRE","CINQ","SIX","SEPT","HUIT","NEUF"};
        const char *word = k_digits[symbol - '0'];
        while (*word && !tts_should_abort()) {
            tts_render_character((char)*word++);
        }
        tts_render_silence(70.0f);
        return;
    }

    if (symbol >= 'A' && symbol <= 'Z') {
        static const float base_freq[] = {710.0f, 520.0f, 360.0f, 540.0f, 420.0f};
        float freq = 400.0f + ((symbol - 'A') % 8) * 45.0f;
        float timbre = 0.4f;
        switch (symbol) {
        case 'A': freq = base_freq[0]; timbre = 0.9f; break;
        case 'E': freq = base_freq[1]; timbre = 0.6f; break;
        case 'I': freq = base_freq[2]; timbre = 0.5f; break;
        case 'O': freq = base_freq[3]; timbre = 0.7f; break;
        case 'U': case 'Y': freq = base_freq[4]; timbre = 0.4f; break;
        default: break;
        }
        tts_render_wave(freq, 180.0f, timbre);
        tts_render_silence(30.0f);
        return;
    }

    tts_render_wave(460.0f, 140.0f, 0.3f);
    tts_render_silence(60.0f);
}

static void tts_render_text(const char *text)
{
    for (size_t i = 0; text[i] != '\0' && !tts_should_abort(); ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c >= 0x80U) {
            tts_render_silence(50.0f);
            continue;
        }
        if (c >= 'a' && c <= 'z') {
            c = (unsigned char)toupper(c);
        }
        tts_render_character((char)c);
    }
}
#endif

esp_err_t tts_stub_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    s_initialized = true;

#if CONFIG_APP_ENABLE_TTS_SYNTH
    esp_err_t err = tts_synth_init();
    if (err == ESP_OK) {
        s_enabled = true;
        s_stub_fallback = false;
        ESP_LOGI(TAG,
                 "Embedded TTS synthesizer initialised (%d Hz, gain %d%%)",
                 CONFIG_APP_TTS_SAMPLE_RATE,
                 CONFIG_APP_TTS_SYNTH_GAIN_PERCENT);
        return ESP_OK;
    }
    s_stub_fallback = true;
    ESP_LOGE(TAG, "Synthesizer initialisation failed: %s", esp_err_to_name(err));
#if !CONFIG_APP_ENABLE_TTS_STUB
    return err;
#endif
#endif

#if CONFIG_APP_ENABLE_TTS_STUB
    s_enabled = true;
    ESP_LOGI(TAG, "TTS logging stub active");
    return ESP_OK;
#else
    s_enabled = false;
    ESP_LOGW(TAG, "TTS disabled: no backend enabled");
    return ESP_OK;
#endif
}

void tts_stub_enable(bool enable)
{
    if (!s_initialized) {
        if (tts_stub_init() != ESP_OK) {
            return;
        }
    }
#if CONFIG_APP_ENABLE_TTS_SYNTH
    if (!s_stub_fallback) {
        s_enabled = enable;
        return;
    }
#endif
#if CONFIG_APP_ENABLE_TTS_STUB
    s_enabled = enable;
    ESP_LOGI(TAG, "TTS stub %s", enable ? "enabled" : "disabled");
#else
    (void)enable;
#endif
}

bool tts_stub_is_enabled(void)
{
    if (!s_initialized) {
        (void)tts_stub_init();
    }
    return s_enabled;
}

void tts_stub_speak(const char *text, bool interrupt)
{
    if (!text || text[0] == '\0') {
        return;
    }
    if (!s_initialized) {
        if (tts_stub_init() != ESP_OK) {
            return;
        }
    }
    if (!s_enabled) {
        return;
    }
#if CONFIG_APP_ENABLE_TTS_SYNTH
    if (!s_stub_fallback && s_mutex) {
        if (interrupt) {
            s_abort_requested = true;
        }
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
            return;
        }
        s_abort_requested = false;
        tts_render_text(text);
        xSemaphoreGive(s_mutex);
        return;
    }
#endif
#if CONFIG_APP_ENABLE_TTS_STUB
    ESP_LOGI(TAG, "[TTS:%s] %s", interrupt ? "INT" : "QUEUED", text);
#else
    (void)interrupt;
#endif
}
