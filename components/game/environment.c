#include "environment.h"
#include "terrarium.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <math.h>

#define TAG "environment"

#define ENV_UPDATE_PERIOD_US (1000 * 1000)
#define MAX_TERRARIUMS 8

typedef struct {
    env_profile_t profile;              /**< Day/night profile */
    environment_update_cb_t callback;   /**< Update callback  */
    float phase_offset;                 /**< Local time offset in hours */
} terrarium_env_t;

static terrarium_env_t terrariums[MAX_TERRARIUMS];
static size_t terrarium_count;
static esp_timer_handle_t env_timer;
static int64_t start_time;
static float hours_per_sec = 1.0f;      /**< Simulated hours per real second */

void environment_set_time_scale(float hours_per_second)
{
    if (hours_per_second > 0.0f) {
        hours_per_sec = hours_per_second;
    }
}

float environment_get_time_scale(void)
{
    return hours_per_sec;
}

bool environment_register_terrarium(const env_profile_t *profile,
                                    environment_update_cb_t cb,
                                    float phase_offset)
{
    if (!profile || !cb || terrarium_count >= MAX_TERRARIUMS) {
        return false;
    }
    terrariums[terrarium_count].profile = *profile;
    terrariums[terrarium_count].callback = cb;
    terrariums[terrarium_count].phase_offset = phase_offset;
    terrarium_count++;
    return true;
}

/* Compute environment and synchronise with each registered terrarium */
static void environment_tick(void *arg)
{
    (void)arg;
    int64_t now = esp_timer_get_time();
    float sim_hours = ((now - start_time) / 1000000.0f) * hours_per_sec; /* Simulated hours */

    for (size_t i = 0; i < terrarium_count; ++i) {
        const env_profile_t *p = &terrariums[i].profile;
        float local = fmodf(sim_hours + terrariums[i].phase_offset, 24.0f);
        float ratio = 0.5f - 0.5f * cosf(local / 24.0f * 2.0f * (float)M_PI); /* 0 night,1 noon */

        float temp = p->night_temp + (p->day_temp - p->night_temp) * ratio;
        float humidity = p->night_humidity + (p->day_humidity - p->night_humidity) * ratio;
        float uv = p->day_uv * ratio; /* UV drops to 0 at night */
        terrariums[i].callback(temp, humidity, uv);
    }
}

void environment_init(void)
{
    start_time = esp_timer_get_time();

    const esp_timer_create_args_t args = {
        .callback = &environment_tick,
        .name = "env"
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &env_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(env_timer, ENV_UPDATE_PERIOD_US));

    ESP_LOGI(TAG, "Accelerated day/night cycle started");
}
