#include "compression_if.h"

#include <stdlib.h>
#include <string.h>

int compression_compress(const uint8_t *input, size_t input_len, uint8_t **output, size_t *output_len)
{
    if (!input || !output || !output_len) {
        return -1;
    }
    *output = malloc(input_len);
    if (!*output) {
        return -1;
    }
    memcpy(*output, input, input_len);
    *output_len = input_len;
    return 0;
}

int compression_decompress(const uint8_t *input, size_t input_len, uint8_t **output, size_t *output_len)
{
    if (!input || !output || !output_len) {
        return -1;
    }
    *output = malloc(input_len);
    if (!*output) {
        return -1;
    }
    memcpy(*output, input, input_len);
    *output_len = input_len;
    return 0;
}
