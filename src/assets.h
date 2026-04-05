#pragma once

#include <stdint.h>
#include <zos_errors.h>
#include <zvb_gfx.h>
#include <zgdk/sound/tracker.h>

#define ZMT_INDEX_SPLASH 'a'
#define ZMT_INDEX_GAME 'b'
#define ZMT_INDEX_NONE 0xFF

gfx_error load_cards_palette(gfx_context* ctx);
gfx_error load_cards_tileset(gfx_context* ctx);
void assets_build_card_tile_grid(uint8_t grid[4][3], uint8_t card);
void assets_build_back_tile_grid(uint8_t grid[4][3]);
uint8_t assets_get_layout_tile(uint8_t x, uint8_t y);
uint8_t assets_get_layout_overlay_tile(uint8_t x, uint8_t y);
zos_err_t load_zmt(track_t* track, uint8_t index);
