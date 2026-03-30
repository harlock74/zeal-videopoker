#pragma once

#include <stdint.h>
#include "main.h"

void init_layout_tiles(void);
void place_gid_grid_at(uint8_t x0, uint8_t y0, const uint16_t grid[SRC_CARD_H][SRC_CARD_W]);
void draw_hold_frames(void);
void draw_hold_labels(void);
void draw_hud_values(void);
