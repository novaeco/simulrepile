#include "persist/save_manager.h"
#include "sim/sim_models.h"
#include "unity.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

TEST_CASE("save manager serialisation", "[persist]")
{
    save_manager_init();
    sim_terrarium_state_t state = {0};
    strcpy(state.nickname, "UnitTest");
    strcpy(state.terrarium_id, "slot1");
    state.environment.day_temperature_target_c = 35.0f;
    state.environment.night_temperature_target_c = 25.0f;
    state.environment.humidity_target_percent = 55.0f;
    state.health.temperature_c = 28.5f;
    state.health.humidity_percent = 60.0f;
    strcpy(state.species.species_id, "test_species");
    TEST_ASSERT_EQUAL(0, save_manager_save_slot(0, &state));

    sim_terrarium_state_t restored = {0};
    TEST_ASSERT_EQUAL(0, save_manager_load_slot(0, &restored));
    TEST_ASSERT_EQUAL_FLOAT(state.health.humidity_percent, restored.health.humidity_percent);
    TEST_ASSERT_EQUAL_STRING(state.nickname, restored.nickname);
}
