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

#define SRC_CARD_W 3
#define SRC_CARD_H 4
#define DST_TILE_BASE 192

#define FONT_DIGIT_TILE 48
#define FONT_ALPHA_A_TILE 64
#define FONT_ALPHA_N_TILE 80
#define FONT_SPACE_TILE 96

#define MAP_TILE_BASE 104
#define MAP_TILE_CAPACITY 88
#define HOLD_FRAME_TILE 252

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
