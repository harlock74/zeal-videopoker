#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <zos_vfs.h>
#include <zvb_gfx.h>

#include "assets.h"

#define ASSET_IO_CHUNK 128
#define TILE_BYTES 256
#define CARD_TILE_W 3
#define CARD_TILE_H 4
#define CARD_TILE_COUNT (CARD_TILE_W * CARD_TILE_H)
#define CARD_RANK_COUNT 13
#define CARD_SUIT_COUNT 4

enum {
    COLOR_RED = 0,
    COLOR_BLACK = 1,
};

enum {
    RANK_ACE = 0,
    RANK_JACK = 10,
    RANK_QUEEN = 11,
    RANK_KING = 12,
};

typedef struct {
    uint16_t top;
    uint16_t mid;
    uint16_t bottom;
} FaceColumn;

typedef struct {
    uint16_t top;
    uint16_t mid_left;
    uint16_t mid_center;
    uint16_t mid_right;
    uint16_t bottom_left;
    uint16_t bottom_center;
    uint16_t bottom_right;
} QueenFace;

/*
 * 3x4 card grid positions named to match the design PDF terminology:
 * Top, Middle1, Middle2, Bottom x Left/Centre/Right.
 */
typedef enum {
    TOP_LEFT = 0,
    TOP_CENTRE,
    TOP_RIGHT,
    MIDDLE1_LEFT,
    MIDDLE1_CENTRE,
    MIDDLE1_RIGHT,
    MIDDLE2_LEFT,
    MIDDLE2_CENTRE,
    MIDDLE2_RIGHT,
    BOTTOM_LEFT,
    BOTTOM_CENTRE,
    BOTTOM_RIGHT,
} CardPos;

#define POS_BIT(pos) ((uint16_t)(1U << (pos)))

/* Core card composition GIDs from cards.gif. */
static const uint16_t kWhiteCardTileGid = 12;

static const uint16_t kSuitGidBySuit[CARD_SUIT_COUNT] = {
    1,   /* Hearts */
    42,  /* Diamonds */
    124, /* Spades */
    83   /* Clubs */
};

/* Explicit suit->color mapping to avoid implicit dependency on suit ordering. */
static const uint8_t kSuitColorBySuit[CARD_SUIT_COUNT] = {
    COLOR_RED,   /* Hearts */
    COLOR_RED,   /* Diamonds */
    COLOR_BLACK, /* Spades */
    COLOR_BLACK, /* Clubs */
};

static const uint16_t kRankGlyphRed[CARD_RANK_COUNT] = {
    13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25
};

static const uint16_t kRankGlyphBlack[CARD_RANK_COUNT] = {
    54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66
};

/* J and K are center-column portraits (top/mid/bottom) with red/black variants. */
static const FaceColumn kJackFaceByColor[2] = {
    {2, 43, 84},  /* Red J */
    {3, 44, 85},  /* Black J */
};

static const FaceColumn kKingFaceByColor[2] = {
    {10, 51, 92}, /* Red K */
    {11, 52, 93}, /* Black K */
};

/* Q uses wider portrait fragments. */
static const QueenFace kQueenFaceByColor[2] = {
    {5, 45, 46, 47, 86, 87, 88}, /* Red Q */
    {8, 48, 49, 50, 89, 90, 91}, /* Black Q */
};

/* Fixed 3x4 red-back card layout from cards.gif/cards.tmx. */
static const uint16_t kBackCardGids[CARD_TILE_H][CARD_TILE_W] = {
    {39, 40, 41},
    {80, 81, 82},
    {121, 122, 123},
    {162, 163, 164},
};

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

static uint8_t validate_gid_range_local(uint16_t gid, uint16_t max_gid, const char* label)
{
    if (gid == 0 || gid > max_gid) {
        printf("Card table validation failed: %s GID %u out of 1..%u\n", label, gid, max_gid);
        return 0;
    }
    return 1;
}

gfx_error assets_validate_card_tables(uint16_t max_gid)
{
    uint8_t ok = 1;

    ok &= validate_gid_range_local(kWhiteCardTileGid, max_gid, "white_bg");

    for (uint8_t i = 0; i < CARD_SUIT_COUNT; i++) {
        ok &= validate_gid_range_local(kSuitGidBySuit[i], max_gid, "suit_gid");
    }

    for (uint8_t i = 0; i < CARD_RANK_COUNT; i++) {
        ok &= validate_gid_range_local(kRankGlyphRed[i], max_gid, "rank_red");
        ok &= validate_gid_range_local(kRankGlyphBlack[i], max_gid, "rank_black");
    }

    for (uint8_t c = 0; c < 2; c++) {
        ok &= validate_gid_range_local(kJackFaceByColor[c].top, max_gid, "jack_top");
        ok &= validate_gid_range_local(kJackFaceByColor[c].mid, max_gid, "jack_mid");
        ok &= validate_gid_range_local(kJackFaceByColor[c].bottom, max_gid, "jack_bottom");

        ok &= validate_gid_range_local(kKingFaceByColor[c].top, max_gid, "king_top");
        ok &= validate_gid_range_local(kKingFaceByColor[c].mid, max_gid, "king_mid");
        ok &= validate_gid_range_local(kKingFaceByColor[c].bottom, max_gid, "king_bottom");

        ok &= validate_gid_range_local(kQueenFaceByColor[c].top, max_gid, "queen_top");
        ok &= validate_gid_range_local(kQueenFaceByColor[c].mid_left, max_gid, "queen_mid_l");
        ok &= validate_gid_range_local(kQueenFaceByColor[c].mid_center, max_gid, "queen_mid_c");
        ok &= validate_gid_range_local(kQueenFaceByColor[c].mid_right, max_gid, "queen_mid_r");
        ok &= validate_gid_range_local(kQueenFaceByColor[c].bottom_left, max_gid, "queen_bot_l");
        ok &= validate_gid_range_local(kQueenFaceByColor[c].bottom_center, max_gid, "queen_bot_c");
        ok &= validate_gid_range_local(kQueenFaceByColor[c].bottom_right, max_gid, "queen_bot_r");
    }

    return ok ? GFX_SUCCESS : GFX_FAILURE;
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

static void set_card_pos(uint16_t grid[CARD_TILE_H][CARD_TILE_W], CardPos pos, uint16_t gid)
{
    uint8_t row = (uint8_t)pos / CARD_TILE_W;
    uint8_t col = (uint8_t)pos % CARD_TILE_W;
    grid[row][col] = gid;
}

static void set_pips_for_rank(uint16_t grid[CARD_TILE_H][CARD_TILE_W], uint8_t rank, uint16_t suit_gid)
{
    /*
     * Pip layouts for A..10 as 12-bit masks over the 3x4 grid:
     * indices:  0  1  2
     *           3  4  5
     *           6  7  8
     *           9 10 11
     */
    static const uint16_t kPipMaskByRank[10] = {
        /* A  */ POS_BIT(MIDDLE2_CENTRE),
        /* 2  */ POS_BIT(MIDDLE1_CENTRE) | POS_BIT(BOTTOM_CENTRE),
        /* 3  */ POS_BIT(MIDDLE1_CENTRE) | POS_BIT(MIDDLE2_CENTRE) | POS_BIT(BOTTOM_CENTRE),
        /* 4  */ POS_BIT(MIDDLE1_LEFT) | POS_BIT(MIDDLE1_RIGHT) | POS_BIT(BOTTOM_LEFT) | POS_BIT(BOTTOM_RIGHT),
        /* 5  */ POS_BIT(MIDDLE1_LEFT) | POS_BIT(MIDDLE1_RIGHT) | POS_BIT(MIDDLE2_CENTRE) | POS_BIT(BOTTOM_LEFT) | POS_BIT(BOTTOM_RIGHT),
        /* 6  */ POS_BIT(MIDDLE1_LEFT) | POS_BIT(MIDDLE1_RIGHT) | POS_BIT(MIDDLE2_LEFT) | POS_BIT(MIDDLE2_RIGHT) | POS_BIT(BOTTOM_LEFT) | POS_BIT(BOTTOM_RIGHT),
        /* 7  */ POS_BIT(MIDDLE1_LEFT) | POS_BIT(MIDDLE1_RIGHT) | POS_BIT(MIDDLE2_LEFT) | POS_BIT(MIDDLE2_CENTRE) | POS_BIT(MIDDLE2_RIGHT) | POS_BIT(BOTTOM_LEFT) | POS_BIT(BOTTOM_RIGHT),
        /* 8  */ POS_BIT(MIDDLE1_LEFT) | POS_BIT(MIDDLE1_RIGHT) | POS_BIT(MIDDLE2_LEFT) | POS_BIT(MIDDLE2_CENTRE) | POS_BIT(MIDDLE2_RIGHT) | POS_BIT(BOTTOM_LEFT) | POS_BIT(BOTTOM_CENTRE) | POS_BIT(BOTTOM_RIGHT),
        /* 9  */ POS_BIT(MIDDLE1_LEFT) | POS_BIT(MIDDLE1_CENTRE) | POS_BIT(MIDDLE1_RIGHT) | POS_BIT(MIDDLE2_LEFT) | POS_BIT(MIDDLE2_CENTRE) | POS_BIT(MIDDLE2_RIGHT) | POS_BIT(BOTTOM_LEFT) | POS_BIT(BOTTOM_CENTRE) | POS_BIT(BOTTOM_RIGHT),
        /* 10 */ POS_BIT(TOP_CENTRE) | POS_BIT(MIDDLE1_LEFT) | POS_BIT(MIDDLE1_CENTRE) | POS_BIT(MIDDLE1_RIGHT) | POS_BIT(MIDDLE2_LEFT) | POS_BIT(MIDDLE2_CENTRE) | POS_BIT(MIDDLE2_RIGHT) | POS_BIT(BOTTOM_LEFT) | POS_BIT(BOTTOM_CENTRE) | POS_BIT(BOTTOM_RIGHT),
    };

    uint16_t mask;
    uint8_t pos;
    uint8_t row;
    uint8_t col;

    if (rank >= 10U) {
        return;
    }

    mask = kPipMaskByRank[rank];
    for (pos = 0; pos < CARD_TILE_COUNT; pos++) {
        if (mask & (uint16_t)(1U << pos)) {
            row = (uint8_t)(pos / CARD_TILE_W);
            col = (uint8_t)(pos % CARD_TILE_W);
            grid[row][col] = suit_gid;
        }
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
    set_card_pos(grid, MIDDLE1_LEFT, suit_gid);

    uint8_t color_idx = black ? COLOR_BLACK : COLOR_RED;

    if (rank == RANK_JACK) {
        const FaceColumn* face = &kJackFaceByColor[color_idx];
        set_card_pos(grid, MIDDLE1_CENTRE, face->top);
        set_card_pos(grid, MIDDLE2_CENTRE, face->mid);
        set_card_pos(grid, BOTTOM_CENTRE, face->bottom);
    } else if (rank == RANK_QUEEN) {
        const QueenFace* face = &kQueenFaceByColor[color_idx];
        set_card_pos(grid, MIDDLE1_CENTRE, face->top);
        set_card_pos(grid, MIDDLE2_LEFT, face->mid_left);
        set_card_pos(grid, MIDDLE2_CENTRE, face->mid_center);
        set_card_pos(grid, MIDDLE2_RIGHT, face->mid_right);
        set_card_pos(grid, BOTTOM_LEFT, face->bottom_left);
        set_card_pos(grid, BOTTOM_CENTRE, face->bottom_center);
        set_card_pos(grid, BOTTOM_RIGHT, face->bottom_right);
    } else if (rank == RANK_KING) {
        const FaceColumn* face = &kKingFaceByColor[color_idx];
        set_card_pos(grid, MIDDLE1_CENTRE, face->top);
        set_card_pos(grid, MIDDLE2_CENTRE, face->mid);
        set_card_pos(grid, BOTTOM_CENTRE, face->bottom);
    }
}

static void build_card_gid_grid_internal(uint16_t grid[CARD_TILE_H][CARD_TILE_W], uint8_t card)
{
    uint8_t rank = (uint8_t)(card % 13U);
    uint8_t suit = (uint8_t)((card / 13U) % CARD_SUIT_COUNT);
    uint8_t color = kSuitColorBySuit[suit];
    uint8_t black = (color == COLOR_BLACK);

    init_card_grid(grid, kWhiteCardTileGid);
    set_card_pos(grid, TOP_LEFT, black ? kRankGlyphBlack[rank] : kRankGlyphRed[rank]);

    if (rank < RANK_JACK) {
        set_pips_for_rank(grid, rank, kSuitGidBySuit[suit]);
    } else {
        set_face_figure(grid, rank, black, kSuitGidBySuit[suit]);
    }
}

void assets_build_card_gid_grid(uint16_t grid[4][3], uint8_t card)
{
    build_card_gid_grid_internal(grid, card);
}

void assets_build_back_gid_grid(uint16_t grid[4][3])
{
    for (uint8_t row = 0; row < CARD_TILE_H; row++) {
        for (uint8_t col = 0; col < CARD_TILE_W; col++) {
            grid[row][col] = kBackCardGids[row][col];
        }
    }
}

static void append_unique_gid(uint16_t gid, uint16_t* out_gids, uint8_t max_count, uint8_t* count, uint8_t* overflow)
{
    if (*count >= max_count) {
        *overflow = 1;
        return;
    }

    for (uint8_t i = 0; i < *count; i++) {
        if (out_gids[i] == gid) {
            return;
        }
    }

    out_gids[*count] = gid;
    (*count)++;
}

uint8_t assets_collect_component_gids(uint16_t* out_gids, uint8_t max_count)
{
    uint8_t count = 0;
    uint8_t overflow = 0;

    append_unique_gid(kWhiteCardTileGid, out_gids, max_count, &count, &overflow);

    for (uint8_t i = 0; i < CARD_SUIT_COUNT; i++) {
        append_unique_gid(kSuitGidBySuit[i], out_gids, max_count, &count, &overflow);
    }

    for (uint8_t i = 0; i < CARD_RANK_COUNT; i++) {
        append_unique_gid(kRankGlyphRed[i], out_gids, max_count, &count, &overflow);
        append_unique_gid(kRankGlyphBlack[i], out_gids, max_count, &count, &overflow);
    }

    for (uint8_t c = 0; c < 2; c++) {
        append_unique_gid(kJackFaceByColor[c].top, out_gids, max_count, &count, &overflow);
        append_unique_gid(kJackFaceByColor[c].mid, out_gids, max_count, &count, &overflow);
        append_unique_gid(kJackFaceByColor[c].bottom, out_gids, max_count, &count, &overflow);

        append_unique_gid(kKingFaceByColor[c].top, out_gids, max_count, &count, &overflow);
        append_unique_gid(kKingFaceByColor[c].mid, out_gids, max_count, &count, &overflow);
        append_unique_gid(kKingFaceByColor[c].bottom, out_gids, max_count, &count, &overflow);

        append_unique_gid(kQueenFaceByColor[c].top, out_gids, max_count, &count, &overflow);
        append_unique_gid(kQueenFaceByColor[c].mid_left, out_gids, max_count, &count, &overflow);
        append_unique_gid(kQueenFaceByColor[c].mid_center, out_gids, max_count, &count, &overflow);
        append_unique_gid(kQueenFaceByColor[c].mid_right, out_gids, max_count, &count, &overflow);
        append_unique_gid(kQueenFaceByColor[c].bottom_left, out_gids, max_count, &count, &overflow);
        append_unique_gid(kQueenFaceByColor[c].bottom_center, out_gids, max_count, &count, &overflow);
        append_unique_gid(kQueenFaceByColor[c].bottom_right, out_gids, max_count, &count, &overflow);
    }

    for (uint8_t row = 0; row < CARD_TILE_H; row++) {
        for (uint8_t col = 0; col < CARD_TILE_W; col++) {
            append_unique_gid(kBackCardGids[row][col], out_gids, max_count, &count, &overflow);
        }
    }

    return overflow ? 0 : count;
}

gfx_error load_card_tiles(gfx_context* ctx, uint8_t card, uint16_t dst_from_byte)
{
    /*
     * cards.gif now stores reusable components (rank glyphs/suit pips/figure parts),
     * so each 3x4 card face is composed tile-by-tile at load time.
     */
    uint16_t grid[CARD_TILE_H][CARD_TILE_W];

    build_card_gid_grid_internal(grid, card);

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
