#include "sim/sim_engine.h"
#include "sim/sim_presets.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

TEST_CASE("simulation tick updates hydration", "[sim]")
{
    sim_engine_init();
    size_t count;
    const sim_species_preset_t *presets = sim_presets_default(&count);
    TEST_ASSERT_GREATER_THAN(0, count);
    int idx = sim_engine_add_terrarium(&presets[0], "Test");
    TEST_ASSERT_GREATER_OR_EQUAL(0, idx);
    const sim_terrarium_state_t *state = sim_engine_get_state((size_t)idx);
    float hydration_before = state->health.hydration_level;
    sim_engine_tick(1000);
    state = sim_engine_get_state((size_t)idx);
    TEST_ASSERT_NOT_EQUAL(hydration_before, state->health.hydration_level);
}
