#pragma once

#include <stdint.h>

#include <zvb_gfx.h>

#include "videopoker.h"

typedef struct {
    gfx_context* vctx;
    PokerCard* cards;
    GameState* state;
    uint16_t* credits;
    uint8_t* bet;
    uint16_t* win_amount;
    uint8_t* show_win_banner;
    uint8_t* show_card_faces;
    uint8_t* dirty_slots;
    uint8_t* full_redraw;
    uint8_t* reveal_mask;
    uint8_t* reveal_sfx_pending_mask;
    char* win_banner_text;
    char* hud_num_buf;
    uint16_t (*scratch_gid_grid)[SRC_CARD_W];
    uint16_t* mapped_gids;
    uint8_t* mapped_tiles;
    uint8_t* mapped_count;
    uint8_t* card_gid_to_runtime;
    const uint8_t* slot_x;
    const uint8_t* slot_y;
    const uint8_t* hold_x;
    const uint8_t* hold_y;
    const uint8_t* bet_x;
    const uint8_t* bet_y;
    const uint8_t* win_x;
    const uint8_t* win_y;
    const uint8_t* credit_x;
    const uint8_t* credit_y;
    uint8_t (*map_gid_to_tile_fn)(uint16_t gid);
    uint8_t (*map_card_gid_to_tile_fn)(uint16_t gid);
} RenderBindings;

void render_bind(const RenderBindings* bindings);

void init_layout_tiles(void);
void place_gid_grid_at(uint8_t x0, uint8_t y0, const uint16_t grid[SRC_CARD_H][SRC_CARD_W]);
void draw_hold_frames(void);
void draw_hold_labels(void);
void draw_hud_values(void);
