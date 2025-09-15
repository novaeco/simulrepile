#include "environment.h"
#include "terrarium.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <math.h>

#define TAG "environment"

/* Real-time acceleration: 1 second represents 1 hour of game time */
#define HOURS_PER_SEC 1
#define ENV_UPDATE_PERIOD_US (1000 * 1000)
#define MAX_TERRARIUMS 8

typedef struct {
    env_profile_t profile;              /**< Day/night profile */
    environment_update_cb_t callback;   /**< Update callback  */
} terrarium_env_t;

static terrarium_env_t terrariums[MAX_TERRARIUMS];
static size_t terrarium_count;
static esp_timer_handle_t env_timer;
static int64_t start_time;

bool environment_register_terrarium(const env_profile_t *profile,
                                    environment_update_cb_t cb)
{
    if (!profile || !cb || terrarium_count >= MAX_TERRARIUMS) {
        return false;
    }
    terrariums[terrarium_count].profile = *profile;
    terrariums[terrarium_count].callback = cb;
    terrarium_count++;
    return true;
}

/* Compute environment and synchronise with each registered terrarium */
static void environment_tick(void *arg)
{
    (void)arg;
    int64_t now = esp_timer_get_time();
    float sim_hours = ((now - start_time) / 1000000.0f) * HOURS_PER_SEC; /* Simulated hours */
    float day_progress = fmodf(sim_hours, 24.0f);                        /* 0..24 */
    float ratio = 0.5f - 0.5f * cosf(day_progress / 24.0f * 2.0f * (float)M_PI); /* 0 night,1 noon */

    for (size_t i = 0; i < terrarium_count; ++i) {
        const env_profile_t *p = &terrariums[i].profile;
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

    /* Register default terrarium profile */
    env_profile_t default_profile = {30.0f, 25.0f, 60.0f, 80.0f, 5.0f};
    environment_register_terrarium(&default_profile, terrarium_update_environment);

    ESP_LOGI(TAG, "Accelerated day/night cycle started");
}
