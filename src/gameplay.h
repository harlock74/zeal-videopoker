#pragma once

#include <stdint.h>

#include "videopoker.h"

/* Hand evaluation logic (pay table resolution). */
HandResult evaluate_hand(const uint8_t hand[CARD_COUNT]);

/* Deck management utilities. */
void shuffle_deck(void);
uint8_t pop_deck(void);
