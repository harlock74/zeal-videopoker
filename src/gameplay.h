#pragma once

#include <stdint.h>

#include "videopoker.h"

typedef struct {
    uint8_t* deck;
    uint8_t* deck_pos;
} GameplayBindings;

void gameplay_bind(const GameplayBindings* bindings);

/* Hand evaluation logic (pay table resolution). */
HandResult evaluate_hand(const uint8_t hand[CARD_COUNT]);

/* Deck management utilities. */
void shuffle_deck(void);
uint8_t pop_deck(void);
