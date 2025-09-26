#include "compression/compression_if.h"
#include "unity.h"
#include <stdlib.h>

void setUp(void) {}
void tearDown(void) {}

TEST_CASE("compression roundtrip", "[compression]")
{
    const uint8_t payload[] = {1, 2, 3, 4};
    uint8_t *compressed = NULL;
    size_t compressed_len = 0;
    TEST_ASSERT_EQUAL(0, compression_compress(payload, sizeof(payload), &compressed, &compressed_len));
    TEST_ASSERT_NOT_NULL(compressed);
    TEST_ASSERT_EQUAL(sizeof(payload), compressed_len);
    uint8_t *decompressed = NULL;
    size_t decompressed_len = 0;
    TEST_ASSERT_EQUAL(0, compression_decompress(compressed, compressed_len, &decompressed, &decompressed_len));
    TEST_ASSERT_EQUAL_MEMORY(payload, decompressed, sizeof(payload));
    free(compressed);
    free(decompressed);
}
