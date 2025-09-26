#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    COMPRESSION_CODEC_NONE = 0,
    COMPRESSION_CODEC_LZ4,
    COMPRESSION_CODEC_HEATSHRINK,
    COMPRESSION_CODEC_MINIZ,
} compression_codec_t;

esp_err_t compression_if_init(void);
esp_err_t compression_if_decompress(compression_codec_t codec, const uint8_t *input, size_t input_len, uint8_t *output, size_t output_len, size_t *consumed, size_t *produced);

#ifdef __cplusplus
}
#endif
