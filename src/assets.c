#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <zos_vfs.h>
#include <zvb_gfx.h>

#include "assets.h"

#define ASSET_IO_CHUNK 128
#define TILE_BYTES 256
#define CARD_TILE_COLUMNS 41
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

static void init_card_grid(uint16_t grid[CARD_TILE_H][CARD_TILE_W], uint16_t gid)
{
    for (uint8_t r = 0; r < CARD_TILE_H; r++) {
        for (uint8_t c = 0; c < CARD_TILE_W; c++) {
            grid[r][c] = gid;
        }
    }
}

static void set_pips_for_rank(uint16_t grid[CARD_TILE_H][CARD_TILE_W], uint8_t rank, uint16_t suit_gid)
{
    /*
     * Pip layout in 3x4 card cells:
     * row 0: top, row 3: bottom, col 1: center.
     */
    switch (rank) {
        case 0: /* A */
            grid[2][1] = suit_gid;
            break;
        case 1: /* 2 */
            grid[1][1] = suit_gid;
            grid[3][1] = suit_gid;
            break;
        case 2: /* 3 */
            grid[1][1] = suit_gid;
            grid[2][1] = suit_gid;
            grid[3][1] = suit_gid;
            break;
        case 3: /* 4 */
            grid[1][0] = suit_gid;
            grid[1][2] = suit_gid;
            grid[3][0] = suit_gid;
            grid[3][2] = suit_gid;
            break;
        case 4: /* 5 */
            grid[1][0] = suit_gid;
            grid[1][2] = suit_gid;
            grid[2][1] = suit_gid;
            grid[3][0] = suit_gid;
            grid[3][2] = suit_gid;
            break;
        case 5: /* 6 */
            grid[1][0] = suit_gid;
            grid[1][2] = suit_gid;
            grid[2][0] = suit_gid;
            grid[2][2] = suit_gid;
            grid[3][0] = suit_gid;
            grid[3][2] = suit_gid;
            break;
        case 6: /* 7 */
            /* Middle1: left+right, Middle2: left+center+right, Bottom: left+right */
            grid[1][0] = suit_gid;
            grid[1][2] = suit_gid;
            grid[2][0] = suit_gid;
            grid[2][1] = suit_gid;
            grid[2][2] = suit_gid;
            grid[3][0] = suit_gid;
            grid[3][2] = suit_gid;
            break;
        case 7: /* 8 */
            /* Middle1: left+right, Middle2: left+center+right, Bottom: left+center+right */
            grid[1][0] = suit_gid;
            grid[1][2] = suit_gid;
            grid[2][0] = suit_gid;
            grid[2][1] = suit_gid;
            grid[2][2] = suit_gid;
            grid[3][0] = suit_gid;
            grid[3][1] = suit_gid;
            grid[3][2] = suit_gid;
            break;
        case 8: /* 9 */
            grid[1][0] = suit_gid;
            grid[1][1] = suit_gid;
            grid[1][2] = suit_gid;
            grid[2][0] = suit_gid;
            grid[2][1] = suit_gid;
            grid[2][2] = suit_gid;
            grid[3][0] = suit_gid;
            grid[3][1] = suit_gid;
            grid[3][2] = suit_gid;
            break;
        case 9: /* 10 */
            grid[0][1] = suit_gid;
            grid[1][0] = suit_gid;
            grid[1][1] = suit_gid;
            grid[1][2] = suit_gid;
            grid[2][0] = suit_gid;
            grid[2][1] = suit_gid;
            grid[2][2] = suit_gid;
            grid[3][0] = suit_gid;
            grid[3][1] = suit_gid;
            grid[3][2] = suit_gid;
            break;
        default:
            break;
    }
}

static void set_face_figure(
    uint16_t grid[CARD_TILE_H][CARD_TILE_W],
    uint8_t rank,
    uint8_t black,
    uint16_t suit_gid)
{
    /*
     * Figure cards (J/Q/K) are assembled from dedicated portrait components.
     * Suit marker must be in row 1, col 0 (left side), matching the template.
     */
    grid[1][0] = suit_gid;

    if (rank == 10) { /* J */
        grid[1][1] = black ? 3 : 2;
        grid[2][1] = black ? 44 : 43;
        grid[3][1] = black ? 85 : 84;
    } else if (rank == 11) { /* Q */
        grid[1][1] = black ? 8 : 5;
        grid[2][0] = black ? 48 : 45;
        grid[2][1] = black ? 49 : 46;
        grid[2][2] = black ? 50 : 47;
        grid[3][0] = black ? 89 : 86;
        grid[3][1] = black ? 90 : 87;
        grid[3][2] = black ? 91 : 88;
    } else if (rank == 12) { /* K */
        grid[1][1] = black ? 11 : 10;
        grid[2][1] = black ? 52 : 51;
        grid[3][1] = black ? 93 : 92;
    }
}

gfx_error load_card_tiles(gfx_context* ctx, uint8_t card, uint16_t dst_from_byte)
{
    /*
     * cards.gif now stores reusable components (rank glyphs/suit pips/figure parts),
     * so each 3x4 card face is composed tile-by-tile at load time.
     */
    static const uint16_t suit_gid_by_suit[4] = {
        1,   /* Hearts */
        42,  /* Diamonds */
        124, /* Spades */
        83   /* Clubs */
    };
    static const uint16_t rank_glyph_red[13] = {
        13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25
    };
    static const uint16_t rank_glyph_black[13] = {
        54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66
    };
    const uint16_t white_bg_gid = 12;

    uint8_t rank = (uint8_t)(card % 13U);
    uint8_t suit = (uint8_t)((card / 13U) % 4U);
    uint8_t black = (suit >= 2U);
    uint16_t grid[CARD_TILE_H][CARD_TILE_W];

    init_card_grid(grid, white_bg_gid);

    /* Rank glyph in top-left cell, color by suit family. */
    grid[0][0] = black ? rank_glyph_black[rank] : rank_glyph_red[rank];

    if (rank < 10U) {
        set_pips_for_rank(grid, rank, suit_gid_by_suit[suit]);
    } else {
        set_face_figure(grid, rank, black, suit_gid_by_suit[suit]);
    }

    for (uint8_t row = 0; row < CARD_TILE_H; row++) {
        for (uint8_t col = 0; col < CARD_TILE_W; col++) {
            uint16_t dst = (uint16_t)(dst_from_byte + ((uint16_t)row * CARD_TILE_W + col) * TILE_BYTES);
            if (load_source_tile(ctx, grid[row][col], dst) != GFX_SUCCESS) {
                return GFX_FAILURE;
            }
        }
    }

    return GFX_SUCCESS;
}
