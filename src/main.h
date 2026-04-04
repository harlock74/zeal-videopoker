#pragma once

#include <stdint.h>
#include <zvb_gfx.h>

#define true 1
#define false 0

#define SCREEN_TILE_W 40
#define SCREEN_TILE_H 30

#define CARD_COUNT 5
#define DECK_SIZE 52
#define MAX_BET 5

/* Game economy config */
#define INITIAL_CREDITS 5
#define RESET_CREDITS 5

#define SRC_CARD_W 3
#define SRC_CARD_H 4
#define SHARED_SCRATCH_BUF_SIZE 1024

/* Direct tileset IDs from cards.gif current font/overlay layout. */
#define FONT_DIGIT_TILE 152
#define FONT_ALPHA_A_TILE 162
#define FONT_ALPHA_N_TILE 175
/* Blank tile used as layer1 clear/space tile. */
#define FONT_SPACE_TILE 3
#define FONT_COLON_TILE 188
#define FONT_EXCL_TILE 189
#define HOLD_FRAME_TILE 119

#define EMPTY_TILE FONT_SPACE_TILE

#define TILEMAP_LAYER 0
#define UI_LAYER 1
#define CARD_LAYER 1

typedef enum {
    STATE_BET = 0,
    STATE_HOLD,
    STATE_RESULT
} GameState;

typedef struct {
    uint8_t card;
    uint8_t held;
} PokerCard;

typedef struct {
    uint8_t multiplier;
    const char* name;
} HandResult;

void init(void);
void deinit(void);
void update(void);
void draw(void);
void deal_hand(void);
void draw_hand(void);
void render_table(void);
void render_cards(void);
HandResult evaluate_hand(const uint8_t hand[CARD_COUNT]);
