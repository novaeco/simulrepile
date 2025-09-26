#include "compression_if.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "compression_if";

esp_err_t compression_if_init(void)
{
    ESP_LOGI(TAG, "Compression interface initialized (stub)");
    return ESP_OK;
}

esp_err_t compression_if_decompress(compression_codec_t codec, const uint8_t *input, size_t input_len, uint8_t *output, size_t output_len, size_t *consumed, size_t *produced)
{
    if (!output || !input) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t copy = input_len < output_len ? input_len : output_len;
    memcpy(output, input, copy);
    if (consumed) {
        *consumed = copy;
    }
    if (produced) {
        *produced = copy;
    }
    ESP_LOGD(TAG, "Passthrough decompression codec=%d", codec);
    return ESP_OK;
}
