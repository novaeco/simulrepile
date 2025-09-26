#include "persist/save_manager.h"
#include "unity.h"

extern int save_manager_internal_crc_validate(const uint8_t *data, size_t len, uint32_t expected);

void setUp(void) {}
void tearDown(void) {}

TEST_CASE("CRC validation detects mismatch", "[persist]")
{
    const uint8_t payload[] = {1, 2, 3};
    uint32_t wrong_crc = 0x12345678;
    TEST_ASSERT_NOT_EQUAL(0, save_manager_internal_crc_validate(payload, sizeof(payload), wrong_crc));
}
