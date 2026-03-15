#include <stdio.h>
#include <stdint.h>

#include <zos_vfs.h>
#include <zvb_gfx.h>

#include "assets.h"

#define ASSET_IO_CHUNK 128
#define TILE_BYTES 256
#define CARD_TILE_COLUMNS 59
#define CARD_TILE_W 3
#define CARD_TILE_H 4

static zos_dev_t open_asset_with_fallback(const char* name)
{
    char path[PATH_MAX];
    zos_dev_t dev;

    sprintf(path, "assets/%s", name);
    dev = open(path, O_RDONLY);
    if (dev >= 0) return dev;

    sprintf(path, "/assets/%s", name);
    dev = open(path, O_RDONLY);
    if (dev >= 0) return dev;

    sprintf(path, "A:/assets/%s", name);
    return open(path, O_RDONLY);
}

static gfx_error load_file_chunked_to_vram(
    gfx_context* ctx,
    const char* filename,
    int32_t src_offset,
    uint16_t byte_count,
    uint16_t dst_from_byte)
{
    uint8_t buf[ASSET_IO_CHUNK];
    uint16_t loaded = 0;

    zos_dev_t dev = open_asset_with_fallback(filename);
    if (dev < 0) {
        return GFX_FAILURE;
    }

    if (src_offset > 0) {
        int32_t off = src_offset;
        if (seek(dev, &off, SEEK_SET) != ERR_SUCCESS) {
            close(dev);
            return GFX_FAILURE;
        }
    }

    while (loaded < byte_count) {
        uint16_t chunk = (uint16_t)(byte_count - loaded);
        if (chunk > ASSET_IO_CHUNK) {
            chunk = ASSET_IO_CHUNK;
        }

        uint16_t size = chunk;
        if (read(dev, buf, &size) != ERR_SUCCESS || size != chunk) {
            close(dev);
            return GFX_FAILURE;
        }

        gfx_tileset_options options = {
            .compression = TILESET_COMP_NONE,
            .from_byte = (uint16_t)(dst_from_byte + loaded),
            .pal_offset = 0,
            .opacity = 0,
        };

        if (gfx_tileset_load(ctx, buf, size, &options) != GFX_SUCCESS) {
            close(dev);
            return GFX_FAILURE;
        }

        loaded = (uint16_t)(loaded + size);
    }

    close(dev);
    return GFX_SUCCESS;
}

gfx_error load_cards_palette(gfx_context* ctx)
{
    uint8_t buf[ASSET_IO_CHUNK];
    uint8_t from_color = 0;

    zos_dev_t dev = open_asset_with_fallback("cards.ztp");
    if (dev < 0) {
        return GFX_FAILURE;
    }

    while (1) {
        uint16_t size = sizeof(buf);
        if (read(dev, buf, &size) != ERR_SUCCESS) {
            close(dev);
            return GFX_FAILURE;
        }
        if (size == 0) {
            break;
        }

        if (gfx_palette_load(ctx, buf, size, from_color) != GFX_SUCCESS) {
            close(dev);
            return GFX_FAILURE;
        }

        from_color = (uint8_t)(from_color + (size / 2));
    }

    close(dev);
    return GFX_SUCCESS;
}

gfx_error load_source_tile(gfx_context* ctx, uint16_t src_gid, uint16_t dst_from_byte)
{
    uint16_t src_index = (uint16_t)(src_gid - 1U);
    int32_t src_offset = (int32_t)src_index * TILE_BYTES;

    return load_file_chunked_to_vram(ctx, "cards.zts", src_offset, TILE_BYTES, dst_from_byte);
}

gfx_error load_card_tiles(gfx_context* ctx, uint8_t card, uint16_t dst_from_byte)
{
    /* Source sheet rows are: 0=Hearts,1=Diamonds,2=Spades,3=Clubs */
    static const uint8_t suit_row_map[4] = {0, 1, 2, 3};

    uint8_t rank = card % 13U;
    uint8_t suit = (card / 13U) % 4U;

    uint8_t src_card_x = (uint8_t)(rank * CARD_TILE_W);
    uint8_t src_card_y = (uint8_t)(suit_row_map[suit] * CARD_TILE_H);

    for (uint8_t row = 0; row < CARD_TILE_H; row++) {
        uint16_t src_tile_index = (uint16_t)(src_card_y + row) * CARD_TILE_COLUMNS + src_card_x;
        int32_t src_offset = (int32_t)src_tile_index * TILE_BYTES;
        uint16_t row_bytes = (uint16_t)(CARD_TILE_W * TILE_BYTES);
        uint16_t row_dst = (uint16_t)(dst_from_byte + (row * CARD_TILE_W * TILE_BYTES));

        if (load_file_chunked_to_vram(ctx, "cards.zts", src_offset, row_bytes, row_dst) != GFX_SUCCESS) {
            return GFX_FAILURE;
        }
    }

    return GFX_SUCCESS;
}
