#pragma once

#include <stdint.h>
#include <zvb_gfx.h>

gfx_error load_cards_palette(gfx_context* ctx);
gfx_error load_source_tile(gfx_context* ctx, uint16_t src_gid, uint16_t dst_from_byte);
gfx_error load_card_tiles(gfx_context* ctx, uint8_t card, uint16_t dst_from_byte);
