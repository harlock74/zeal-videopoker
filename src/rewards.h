#pragma once

#include <stdint.h>

/*
 * Reward progression API:
 * - Tracks reward progression points (not won credits).
 * - Progression model:
 *   +1 for each resolved hand,
 *   +1 extra when the hand is a winning hand.
 * - Unlocks staged reward reveals at configurable progression milestones.
 */

#define REWARD_STAGE_COUNT 6

void rewards_reset(void);
void rewards_register_hand_result(uint16_t win_amount);
uint8_t rewards_has_pending(void);
uint8_t rewards_pending_stage(void);
uint8_t rewards_is_final_stage_pending(void);
uint8_t rewards_consume_pending_stage(void);
void rewards_clear_pending(void);
