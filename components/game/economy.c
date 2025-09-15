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

/* Wellbeing penalty when expenses cannot be covered */
#define DEFICIT_PENALTY_BASE 5.0f
#define DEFICIT_PENALTY_RATE 0.1f

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
 *
 * @return Amount of credits missing to cover the expenses.
 */
static float economy_apply_daily_expenses(economy_t *eco)
{
    if (!eco) {
        return 0.0f;
    }

    const float expenses = FOOD_COST_PER_DAY + ELECTRICITY_COST +
                           VETERINARY_COST + EQUIPMENT_COST;
    const float available = eco->budget > 0.0f ? eco->budget : 0.0f;

    eco->budget -= expenses;

    float deficit = expenses - available;
    return deficit > 0.0f ? deficit : 0.0f;
}

/**
 * @brief Adjust the reptile wellbeing according to the budget status.
 *
 * @param deficit Unpaid portion of today's expenses.
 */
static void economy_apply_wellbeing(economy_t *eco, float deficit)
{
    if (!eco) {
        return;
    }

    if (deficit > 0.0f) {
        eco->wellbeing -= DEFICIT_PENALTY_BASE +
                          deficit * DEFICIT_PENALTY_RATE;
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
    float deficit = economy_apply_daily_expenses(eco);
    economy_apply_wellbeing(eco, deficit);
}
