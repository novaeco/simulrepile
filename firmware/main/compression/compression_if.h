#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int compression_compress(const uint8_t *input, size_t input_len, uint8_t **output, size_t *output_len);
int compression_decompress(const uint8_t *input, size_t input_len, uint8_t **output, size_t *output_len);

#ifdef __cplusplus
}
#endif
