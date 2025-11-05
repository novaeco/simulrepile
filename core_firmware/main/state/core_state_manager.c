#include "state/core_state_manager.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#define CORE_STATE_TERRARIUM_COUNT 3

static const char *TAG = "core_state_mgr";

typedef struct {
    uint8_t id;
    const char *scientific_name;
    const char *common_name;
    float base_temp_day;
    float base_temp_night;
    float base_humidity_day;
    float base_humidity_night;
    float base_lux_day;
    float base_lux_night;
    float current_temp_day;
    float current_temp_night;
    float current_humidity_day;
    float current_humidity_night;
    float current_lux_day;
    float current_lux_night;
    float hydration_pct;
    float stress_pct;
    float health_pct;
    float activity_score;
    float cycle_speed;
    float phase_offset;
    float enrichment_factor;
    uint32_t last_feeding_timestamp;
} core_state_slot_t;

static core_state_slot_t s_slots[CORE_STATE_TERRARIUM_COUNT];

static inline float clampf(float value, float min, float max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static uint32_t current_epoch_seconds(void)
{
    uint64_t now_us = esp_timer_get_time();
    return (uint32_t)(CONFIG_CORE_APP_STATE_BASE_EPOCH + (now_us / 1000000ULL));
}

void core_state_manager_init(void)
{
    memset(s_slots, 0, sizeof(s_slots));

    s_slots[0] = (core_state_slot_t){
        .id = 0,
        .scientific_name = "Python regius",
        .common_name = "Python royal",
        .base_temp_day = 31.0f,
        .base_temp_night = 24.0f,
        .base_humidity_day = 60.0f,
        .base_humidity_night = 70.0f,
        .base_lux_day = 400.0f,
        .base_lux_night = 5.0f,
        .cycle_speed = 0.03f,
        .phase_offset = 0.0f,
        .enrichment_factor = 1.0f,
    };

    s_slots[1] = (core_state_slot_t){
        .id = 1,
        .scientific_name = "Pogona vitticeps",
        .common_name = "Dragon barbu",
        .base_temp_day = 35.0f,
        .base_temp_night = 22.0f,
        .base_humidity_day = 40.0f,
        .base_humidity_night = 50.0f,
        .base_lux_day = 650.0f,
        .base_lux_night = 10.0f,
        .cycle_speed = 0.045f,
        .phase_offset = 1.1f,
        .enrichment_factor = 1.3f,
    };

    s_slots[2] = (core_state_slot_t){
        .id = 2,
        .scientific_name = "Correlophus ciliatus",
        .common_name = "Gecko à crête",
        .base_temp_day = 27.0f,
        .base_temp_night = 21.0f,
        .base_humidity_day = 70.0f,
        .base_humidity_night = 85.0f,
        .base_lux_day = 220.0f,
        .base_lux_night = 3.0f,
        .cycle_speed = 0.038f,
        .phase_offset = 2.4f,
        .enrichment_factor = 0.8f,
    };

    uint32_t now_epoch = current_epoch_seconds();
    for (size_t i = 0; i < CORE_STATE_TERRARIUM_COUNT; ++i) {
        core_state_slot_t *slot = &s_slots[i];
        slot->current_temp_day = slot->base_temp_day;
        slot->current_temp_night = slot->base_temp_night;
        slot->current_humidity_day = slot->base_humidity_day;
        slot->current_humidity_night = slot->base_humidity_night;
        slot->current_lux_day = slot->base_lux_day;
        slot->current_lux_night = slot->base_lux_night;
        slot->hydration_pct = 88.0f - (float)i * 3.0f;
        slot->stress_pct = 15.0f + (float)i * 4.0f;
        slot->health_pct = 94.0f - (float)i * 2.0f;
        slot->activity_score = 0.5f;
        slot->last_feeding_timestamp = now_epoch - (uint32_t)(6 * 3600 * (i + 1));
    }

    ESP_LOGI(TAG, "Core state manager initialized (%d terrariums)", CORE_STATE_TERRARIUM_COUNT);
}

void core_state_manager_update(float delta_seconds)
{
    float time_s = (float)(esp_timer_get_time() / 1000000.0);
    for (size_t i = 0; i < CORE_STATE_TERRARIUM_COUNT; ++i) {
        core_state_slot_t *slot = &s_slots[i];
        float wave = sinf(time_s * slot->cycle_speed + slot->phase_offset);
        float wave_secondary = cosf(time_s * slot->cycle_speed * 0.7f + slot->phase_offset * 1.2f);

        slot->current_temp_day = slot->base_temp_day + wave * 1.8f;
        slot->current_temp_night = slot->base_temp_night + wave * 1.0f;
        slot->current_humidity_day = clampf(slot->base_humidity_day + wave_secondary * 6.0f, 30.0f, 95.0f);
        slot->current_humidity_night = clampf(slot->base_humidity_night + wave_secondary * 4.0f, 40.0f, 98.0f);
        slot->current_lux_day = clampf(slot->base_lux_day + wave * 80.0f, 50.0f, 900.0f);
        slot->current_lux_night = clampf(slot->base_lux_night + (wave_secondary + 1.0f) * 2.0f, 0.0f, 20.0f);

        slot->activity_score = clampf(0.45f + 0.4f * sinf(time_s * slot->cycle_speed * 1.3f + slot->phase_offset), 0.0f, 1.0f);

        float stress_trend = (30.0f - slot->activity_score * 45.0f + slot->enrichment_factor * 5.0f) * 0.015f;
        slot->stress_pct = clampf(slot->stress_pct + stress_trend * delta_seconds, 5.0f, 85.0f);

        float hydration_drain = slot->enrichment_factor * (0.20f + 0.05f * (1.0f - slot->activity_score));
        slot->hydration_pct = clampf(slot->hydration_pct - hydration_drain * delta_seconds, 45.0f, 100.0f);

        if (slot->hydration_pct < 55.0f) {
            slot->hydration_pct = 90.0f;
            slot->last_feeding_timestamp = current_epoch_seconds();
            slot->stress_pct = clampf(slot->stress_pct - 6.0f, 0.0f, 85.0f);
            slot->health_pct = clampf(slot->health_pct + 3.5f, 0.0f, 100.0f);
        }

        float hydration_penalty = (70.0f - slot->hydration_pct) * 0.01f;
        float stress_penalty = slot->stress_pct * 0.006f;
        slot->health_pct = clampf(slot->health_pct - (hydration_penalty + stress_penalty) * delta_seconds, 65.0f, 100.0f);
    }
}

static size_t resolve_slot_index(uint16_t x)
{
    const uint16_t width = 1024;
    uint16_t zone = width / CORE_STATE_TERRARIUM_COUNT;
    size_t idx = x / zone;
    if (idx >= CORE_STATE_TERRARIUM_COUNT) {
        idx = CORE_STATE_TERRARIUM_COUNT - 1;
    }
    return idx;
}

void core_state_manager_apply_touch(const core_link_touch_event_t *event)
{
    if (!event) {
        return;
    }

    size_t idx = resolve_slot_index(event->x);
    core_state_slot_t *slot = &s_slots[idx];

    if (event->type == CORE_LINK_TOUCH_DOWN) {
        slot->stress_pct = clampf(slot->stress_pct - (float)CONFIG_CORE_APP_TOUCH_RELIEF_DELTA, 0.0f, 80.0f);
        slot->activity_score = clampf(slot->activity_score + 0.1f, 0.0f, 1.0f);
    } else if (event->type == CORE_LINK_TOUCH_MOVE) {
        slot->activity_score = clampf(slot->activity_score + 0.02f, 0.0f, 1.0f);
    }
}

void core_state_manager_build_frame(core_link_state_frame_t *frame)
{
    if (!frame) {
        return;
    }

    memset(frame, 0, sizeof(*frame));
    frame->epoch_seconds = current_epoch_seconds();
    frame->terrarium_count = CORE_STATE_TERRARIUM_COUNT;

    for (uint8_t i = 0; i < CORE_STATE_TERRARIUM_COUNT; ++i) {
        core_link_terrarium_snapshot_t *snap = &frame->terrariums[i];
        const core_state_slot_t *slot = &s_slots[i];

        snap->terrarium_id = slot->id;
        strncpy(snap->scientific_name, slot->scientific_name, CORE_LINK_NAME_MAX_LEN);
        snap->scientific_name[CORE_LINK_NAME_MAX_LEN] = '\0';
        strncpy(snap->common_name, slot->common_name, CORE_LINK_NAME_MAX_LEN);
        snap->common_name[CORE_LINK_NAME_MAX_LEN] = '\0';

        snap->temp_day_c = slot->current_temp_day;
        snap->temp_night_c = slot->current_temp_night;
        snap->humidity_day_pct = slot->current_humidity_day;
        snap->humidity_night_pct = slot->current_humidity_night;
        snap->lux_day = slot->current_lux_day;
        snap->lux_night = slot->current_lux_night;
        snap->hydration_pct = slot->hydration_pct;
        snap->stress_pct = slot->stress_pct;
        snap->health_pct = slot->health_pct;
        snap->last_feeding_timestamp = slot->last_feeding_timestamp;
        snap->activity_score = slot->activity_score;
    }
}
