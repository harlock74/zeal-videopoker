#pragma once

#include <stdint.h>
#include <zos_errors.h>
#include <zvb_gfx.h>
#include <zgdk/sound/tracker.h>

gfx_error load_cards_palette(gfx_context* ctx);
gfx_error load_source_tile(gfx_context* ctx, uint16_t src_gid, uint16_t dst_from_byte);
gfx_error load_card_tiles(gfx_context* ctx, uint8_t card, uint16_t dst_from_byte);
gfx_error assets_validate_card_tables(uint16_t max_gid);
void assets_build_card_gid_grid(uint16_t grid[4][3], uint8_t card);
void assets_build_back_gid_grid(uint16_t grid[4][3]);
uint8_t assets_collect_component_gids(uint16_t* out_gids, uint8_t max_count);
zos_err_t load_zmt(track_t* track, uint8_t index);
void assets_shutdown(void);
