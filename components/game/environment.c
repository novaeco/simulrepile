#include "environment.h"
#include "terrarium.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <math.h>

#define TAG "environment"

/* Real-time acceleration: 1 second represents 1 hour of game time */
#define HOURS_PER_SEC 1

/* Profile for terrarium environment (day and night conditions) */
typedef struct {
    float day_temp;      /**< Daytime temperature in Celsius */
    float night_temp;    /**< Nighttime temperature in Celsius */
    float day_humidity;  /**< Daytime relative humidity in percent */
    float night_humidity;/**< Nighttime relative humidity in percent */
    float day_uv;        /**< Daytime UV index */
} env_profile_t;

static env_profile_t profiles[] = {
    {30.0f, 25.0f, 60.0f, 80.0f, 5.0f}, /* Default terrarium */
};

static esp_timer_handle_t env_timer;
static int64_t start_time;

/* Compute environment and synchronise with terrarium */
static void environment_tick(void *arg)
{
    (void)arg;
    int64_t now = esp_timer_get_time();
    float sim_hours = ((now - start_time) / 1000000.0f) * HOURS_PER_SEC; /* Simulated hours */
    float day_progress = fmodf(sim_hours, 24.0f);                      /* 0..24 */
    float ratio = 0.5f - 0.5f * cosf(day_progress / 24.0f * 2.0f * (float)M_PI); /* 0 night,1 noon */

    size_t count = sizeof(profiles) / sizeof(profiles[0]);
    for (size_t i = 0; i < count; ++i) {
        const env_profile_t *p = &profiles[i];
        float temp = p->night_temp + (p->day_temp - p->night_temp) * ratio;
        float humidity = p->night_humidity + (p->day_humidity - p->night_humidity) * ratio;
        float uv = p->day_uv * ratio; /* UV drops to 0 at night */
        terrarium_update_environment(temp, humidity, uv);
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
    ESP_ERROR_CHECK(esp_timer_start_periodic(env_timer, 1000 * 1000)); /* Update every second */
    ESP_LOGI(TAG, "Accelerated day/night cycle started");
}
