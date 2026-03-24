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

/*
 * Runtime tile allocation:
 *  - map tiles:   32..143
 *  - font tiles:  144..179
 *  - space tile:  180
 *  - hold frame:  181
 *  - shared card components: 184..255
 */
#define MAP_TILE_BASE 32
#define MAP_TILE_CAPACITY 112

#define FONT_DIGIT_TILE 144
#define FONT_ALPHA_A_TILE 154
#define FONT_ALPHA_N_TILE 167
#define FONT_SPACE_TILE 180
#define FONT_COLON_TILE 182
#define FONT_EXCL_TILE 183
#define HOLD_FRAME_TILE 181

#define TILEMAP_LAYER 0

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
