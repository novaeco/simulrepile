#include "persist/save_manager.h"
#include "sim/sim_models.h"
#include "unity.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

TEST_CASE("save manager serialisation", "[persist]")
{
    sim_terrarium_state_t state = {0};
    strcpy(state.nickname, "UnitTest");
    state.health.temperature_c = 28.5f;
    state.health.humidity_percent = 60.0f;
    TEST_ASSERT_EQUAL(0, save_manager_save_slot(0, &state));
}
