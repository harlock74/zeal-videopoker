#pragma once

#include <stdint.h>

/*
 * Reward progression API:
 * - Tracks cumulative won credits.
 * - Unlocks staged reward reveals at configurable milestones.
 */

#define REWARD_STAGE_COUNT 6

void rewards_reset(void);
void rewards_register_hand_result(uint16_t win_amount);
uint8_t rewards_has_pending(void);
uint8_t rewards_pending_stage(void);
void rewards_clear_pending(void);
