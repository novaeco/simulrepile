#ifndef ECONOMY_H
#define ECONOMY_H

#include <stdint.h>

/**
 * @brief Game economy state.
 */
typedef struct {
    uint32_t day;      /**< Current in-game day counter. */
    float budget;      /**< Player budget in credits. */
    float wellbeing;   /**< Reptile wellbeing score [0,100]. */
} economy_t;

/**
 * @brief Initialise the economy system.
 *
 * @param eco Economy instance to initialise.
 * @param initial_budget Starting credits for the player.
 * @param initial_wellbeing Initial wellbeing score.
 */
void economy_init(economy_t *eco, float initial_budget, float initial_wellbeing);

/**
 * @brief Advance the simulation by one day.
 *
 * Handles weekly income, daily expenses and adjusts wellbeing according to
 * the remaining budget.
 *
 * @param eco Economy instance to update.
 */
void economy_next_day(economy_t *eco);

#endif // ECONOMY_H
