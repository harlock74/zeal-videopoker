#include <stdint.h>

#include "rewards.h"

/*
 * Reward milestone configuration (easy to tweak gameplay goals):
 * stage 1..6 unlock when reward progression reaches these values.
 *
 * Progression model:
 * - +1 for every resolved hand
 * - +1 extra if that hand is a winning hand
 */
static const uint16_t kRewardProgressMilestones[REWARD_STAGE_COUNT] = {5, 10, 15, 20, 25, 30};

static uint16_t reward_progress = 0;
static uint8_t unlocked_reward_stage = 0;
/* Next stage to consume; acts as an ordered pending queue cursor. */
static uint8_t next_pending_stage = 1;

void rewards_reset(void)
{
    reward_progress = 0;
    unlocked_reward_stage = 0;
    next_pending_stage = 1;
}

void rewards_register_hand_result(uint16_t win_amount)
{
    /* +1 for any hand resolved, +1 extra for a winning hand. */
    reward_progress = (uint16_t)(reward_progress + 1U);
    if (win_amount > 0U) {
        reward_progress = (uint16_t)(reward_progress + 1U);
    }

    while (unlocked_reward_stage < REWARD_STAGE_COUNT &&
           reward_progress >= kRewardProgressMilestones[unlocked_reward_stage]) {
        unlocked_reward_stage++;
    }
}

uint8_t rewards_has_pending(void)
{
    return (next_pending_stage <= unlocked_reward_stage);
}

uint8_t rewards_pending_stage(void)
{
    if (next_pending_stage <= unlocked_reward_stage) {
        return next_pending_stage;
    }
    return 0;
}

uint8_t rewards_is_final_stage_pending(void)
{
    return (next_pending_stage == REWARD_STAGE_COUNT &&
            next_pending_stage <= unlocked_reward_stage);
}

uint8_t rewards_consume_pending_stage(void)
{
    uint8_t stage = rewards_pending_stage();
    if (stage > 0 && next_pending_stage <= REWARD_STAGE_COUNT) {
        next_pending_stage++;
    }
    return stage;
}

void rewards_clear_pending(void)
{
    next_pending_stage = (uint8_t)(unlocked_reward_stage + 1);
}
