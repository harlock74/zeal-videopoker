#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <zos_vfs.h>
#include <zvb_gfx.h>

#include "assets.h"

#define ASSET_IO_CHUNK 128
#define TILE_BYTES 256
#define CARD_TILE_COLUMNS 39
#define CARD_TILE_W 3
#define CARD_TILE_H 4

/*
 * Reusable stream state to avoid repeated open/close/seek churn.
 * This is especially important on real hardware where storage I/O latency dominates.
 */
static zos_dev_t g_stream_dev = -1;
static char g_stream_name[32];
static int32_t g_stream_offset = 0;
static uint8_t g_stream_valid = 0;

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

static void close_asset_stream(void)
{
    /* Safe to call repeatedly; used during errors and shutdown. */
    if (g_stream_dev >= 0) {
        close(g_stream_dev);
    }
    g_stream_dev = -1;
    g_stream_name[0] = '\0';
    g_stream_offset = 0;
    g_stream_valid = 0;
}

static zos_dev_t ensure_asset_stream(const char* name)
{
    /* Fast path: reuse already-open stream for same asset file. */
    if (g_stream_valid && (strcmp(g_stream_name, name) == 0)) {
        return g_stream_dev;
    }

    /* Slow path: switch to a different source file. */
    close_asset_stream();
    g_stream_dev = open_asset_with_fallback(name);
    if (g_stream_dev < 0) {
        return g_stream_dev;
    }

    strncpy(g_stream_name, name, sizeof(g_stream_name) - 1);
    g_stream_name[sizeof(g_stream_name) - 1] = '\0';
    g_stream_offset = 0;
    g_stream_valid = 1;
    return g_stream_dev;
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

    /*
     * Reuse stream across many tile loads:
     * repeated calls from card reveals can then avoid open/close overhead.
     */
    zos_dev_t dev = ensure_asset_stream(filename);
    if (dev < 0) {
        return GFX_FAILURE;
    }

    if (src_offset != g_stream_offset) {
        /* Only seek when requested source offset differs from tracked stream cursor. */
        int32_t off = src_offset;
        if (seek(dev, &off, SEEK_SET) != ERR_SUCCESS) {
            close_asset_stream();
            return GFX_FAILURE;
        }
        g_stream_offset = src_offset;
    }

    while (loaded < byte_count) {
        uint16_t chunk = (uint16_t)(byte_count - loaded);
        if (chunk > ASSET_IO_CHUNK) {
            chunk = ASSET_IO_CHUNK;
        }

        uint16_t size = chunk;
        if (read(dev, buf, &size) != ERR_SUCCESS || size != chunk) {
            close_asset_stream();
            return GFX_FAILURE;
        }
        /* Keep a local cursor so later reads can stay sequential when possible. */
        g_stream_offset += size;

        gfx_tileset_options options = {
            .compression = TILESET_COMP_NONE,
            .from_byte = (uint16_t)(dst_from_byte + loaded),
            .pal_offset = 0,
            .opacity = 0,
        };

        if (gfx_tileset_load(ctx, buf, size, &options) != GFX_SUCCESS) {
            close_asset_stream();
            return GFX_FAILURE;
        }

        loaded = (uint16_t)(loaded + size);
    }

    return GFX_SUCCESS;
}

gfx_error load_cards_palette(gfx_context* ctx)
{
    uint8_t buf[ASSET_IO_CHUNK];
    uint8_t from_color = 0;

    /* Palette is loaded once; close any tile stream to release handle first. */
    close_asset_stream();
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

void assets_shutdown(void)
{
    /* Called from game deinit() to release persistent stream handle. */
    close_asset_stream();
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

    /*
     * Load 3 contiguous tiles per row (4 rows total) for one card face.
     * This keeps I/O in larger sequential reads instead of many tiny calls.
     */
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
