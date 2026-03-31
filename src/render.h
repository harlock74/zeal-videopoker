#pragma once

#include <stdint.h>
#include "main.h"

void place_gid_grid_at(uint8_t x0, uint8_t y0, const uint16_t grid[SRC_CARD_H][SRC_CARD_W]);
void draw_hold_frames(void);
void draw_hold_labels(void);
void draw_hud_values(void);
void render_mark_all_slots_dirty(void);
void render_mark_slot_dirty(uint8_t slot);
void render_begin_reveal(uint8_t initial_mask);
void render_reveal_slot(uint8_t slot, uint8_t queue_sfx);
void render_refresh_overlays(void);
