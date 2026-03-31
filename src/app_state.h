#pragma once

#include <stdint.h>

#include <zvb_gfx.h>

#include "main.h"

/*
 * Cross-module app state declarations.
 * Definitions live in src/main.c.
 */

extern gfx_context vctx;

extern PokerCard cards[CARD_COUNT];
extern uint8_t deck[DECK_SIZE];
extern uint8_t deck_pos;
extern GameState state;

extern uint16_t credits;
extern uint8_t bet;
extern uint16_t win_amount;

extern uint8_t show_win_banner;
extern uint8_t show_card_faces;
extern uint8_t pending_bankrupt_reset;

extern uint16_t entropy;

extern char win_banner_text[36];
extern char hud_num_buf[6];

extern uint16_t scratch_gid_grid[SRC_CARD_H][SRC_CARD_W];
extern uint16_t mapped_gids[MAP_TILE_CAPACITY];
extern uint8_t mapped_tiles[MAP_TILE_CAPACITY];
extern uint8_t mapped_count;

extern const uint8_t slot_x[CARD_COUNT];
extern const uint8_t slot_y;
extern const uint8_t hold_x[CARD_COUNT];
extern const uint8_t hold_y;
extern const uint8_t bet_x;
extern const uint8_t bet_y;
extern const uint8_t win_x;
extern const uint8_t win_y;
extern const uint8_t credit_x;
extern const uint8_t credit_y;

extern uint8_t map_gid_to_tile(uint16_t gid);
extern uint8_t map_card_gid_to_tile(uint16_t gid);
