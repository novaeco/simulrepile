#include "economy.h"

#include <stddef.h>

/* Economic parameters */
#define WEEKLY_CREDIT       100.0f
#define FOOD_COST_PER_DAY   10.0f
#define ELECTRICITY_COST    5.0f
#define VETERINARY_COST     2.0f
#define EQUIPMENT_COST      1.0f

/* Wellbeing adjustment thresholds */
#define WELLBEING_MAX       100.0f
#define WELLBEING_MIN       0.0f

/**
 * @brief Apply the weekly income when a new week starts.
 */
static void economy_apply_weekly_credit(economy_t *eco)
{
    if (!eco) {
        return;
    }

    if (eco->day % 7 == 1) {
        eco->budget += WEEKLY_CREDIT;
    }
}

/**
 * @brief Deduct mandatory daily expenses from the player budget.
 */
static void economy_apply_daily_expenses(economy_t *eco)
{
    if (!eco) {
        return;
    }

    eco->budget -= FOOD_COST_PER_DAY;
    eco->budget -= ELECTRICITY_COST;
    eco->budget -= VETERINARY_COST;
    eco->budget -= EQUIPMENT_COST;
}

/**
 * @brief Adjust the reptile wellbeing according to the available budget.
 */
static void economy_apply_wellbeing(economy_t *eco)
{
    if (!eco) {
        return;
    }

    if (eco->budget < 0.0f) {
        eco->wellbeing -= 5.0f;
    } else if (eco->budget < 50.0f) {
        eco->wellbeing -= 1.0f;
    } else if (eco->budget > 200.0f) {
        eco->wellbeing += 1.0f;
    }

    if (eco->wellbeing > WELLBEING_MAX) {
        eco->wellbeing = WELLBEING_MAX;
    } else if (eco->wellbeing < WELLBEING_MIN) {
        eco->wellbeing = WELLBEING_MIN;
    }
}

void economy_init(economy_t *eco, float initial_budget, float initial_wellbeing)
{
    if (!eco) {
        return;
    }

    eco->day = 0;
    eco->budget = initial_budget;
    eco->wellbeing = initial_wellbeing;
}

void economy_next_day(economy_t *eco)
{
    if (!eco) {
        return;
    }

    eco->day++;

    economy_apply_weekly_credit(eco);
    economy_apply_daily_expenses(eco);
    economy_apply_wellbeing(eco);
}
