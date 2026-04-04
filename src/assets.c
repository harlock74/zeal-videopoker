#include <stdint.h>

#include <zos_errors.h>
#include <zos_vfs.h>
#include <zvb_gfx.h>
#include <zgdk/sound/tracker.h>
#include <core.h>

#include "assets.h"
#include "app_state.h"

extern uint8_t _zmt_track1_start;
extern uint8_t _zmt_track1_end;
extern uint8_t _zmt_track2_start;
extern uint8_t _zmt_track2_end;
extern uint8_t _ztm_cards_start;
extern uint8_t _ztm_cards_end;

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
    uint8_t top;
    uint8_t mid;
    uint8_t bottom;
} FaceColumn;

typedef struct {
    uint8_t top;
    uint8_t mid_left;
    uint8_t mid_center;
    uint8_t mid_right;
    uint8_t bottom_left;
    uint8_t bottom_center;
    uint8_t bottom_right;
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

/* Core card composition tile IDs from cards.gif. */
static const uint8_t kWhiteCardTile = 11;

static const uint8_t kSuitTileBySuit[CARD_SUIT_COUNT] = {
    0,   /* Hearts */
    38,  /* Diamonds */
    114, /* Spades */
    76   /* Clubs */
};

/* Explicit suit->color mapping to avoid implicit dependency on suit ordering. */
static const uint8_t kSuitColorBySuit[CARD_SUIT_COUNT] = {
    COLOR_RED,   /* Hearts */
    COLOR_RED,   /* Diamonds */
    COLOR_BLACK, /* Spades */
    COLOR_BLACK, /* Clubs */
};

static const uint8_t kRankGlyphRed[CARD_RANK_COUNT] = {
    12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24
};

static const uint8_t kRankGlyphBlack[CARD_RANK_COUNT] = {
    50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62
};

/* J and K are center-column portraits (top/mid/bottom) with red/black variants. */
static const FaceColumn kJackFaceByColor[2] = {
    {1, 39, 77},  /* Red J */
    {2, 40, 78},  /* Black J */
};

static const FaceColumn kKingFaceByColor[2] = {
    {9, 47, 85},  /* Red K */
    {10, 48, 86}, /* Black K */
};

/* Q uses wider portrait fragments. */
static const QueenFace kQueenFaceByColor[2] = {
    {4, 41, 42, 43, 79, 80, 81}, /* Red Q */
    {7, 44, 45, 46, 82, 83, 84}, /* Black Q */
};

/* Fixed 3x4 red-back card layout from cards.gif/cards.tmx. */
static const uint8_t kBackCardTiles[CARD_TILE_H][CARD_TILE_W] = {
    {115, 116, 117},
    {118, 119, 120},
    {121, 122, 123},
    {124, 125, 126},
};

gfx_error load_cards_palette(gfx_context* ctx)
{
    uint8_t from_color = 0;
    zos_dev_t dev = open("assets/cards.ztp", O_RDONLY);
    if (dev < 0) {
        return GFX_FAILURE;
    }

    while (1) {
        uint16_t size = sizeof(g_buf);
        if (read(dev, g_buf, &size) != ERR_SUCCESS) {
            close(dev);
            return GFX_FAILURE;
        }
        if (size == 0) {
            break;
        }

        if (gfx_palette_load(ctx, g_buf, size, from_color) != GFX_SUCCESS) {
            close(dev);
            return GFX_FAILURE;
        }

        from_color = (uint8_t)(from_color + (size / 2));
    }

    close(dev);
    return GFX_SUCCESS;
}

gfx_error load_cards_tileset(gfx_context* ctx)
{
    uint16_t from_byte = 0;
    zos_dev_t dev = open("assets/cards.zts", O_RDONLY);
    if (dev < 0) {
        return GFX_FAILURE;
    }

    /* Stream 4-bit packed ZTS chunks and let ZVB decode to 8-bit tiles in VRAM. */
    while (1) {
        uint16_t size = sizeof(g_buf);
        gfx_tileset_options options = {
            .compression = TILESET_COMP_4BIT,
            .from_byte = from_byte,
            .pal_offset = 0,
            .opacity = 0,
        };

        if (read(dev, g_buf, &size) != ERR_SUCCESS) {
            close(dev);
            return GFX_FAILURE;
        }
        if (size == 0) {
            break;
        }

        if (gfx_tileset_load(ctx, g_buf, size, &options) != GFX_SUCCESS) {
            close(dev);
            return GFX_FAILURE;
        }
        from_byte = (uint16_t)(from_byte + (size * 2U));
    }

    close(dev);
    return GFX_SUCCESS;
}

static void init_card_grid(uint8_t grid[CARD_TILE_H][CARD_TILE_W], uint8_t tile)
{
    for (uint8_t r = 0; r < CARD_TILE_H; r++) {
        for (uint8_t c = 0; c < CARD_TILE_W; c++) {
            grid[r][c] = tile;
        }
    }
}

static void set_card_pos(uint8_t grid[CARD_TILE_H][CARD_TILE_W], CardPos pos, uint8_t tile)
{
    uint8_t row = (uint8_t)pos / CARD_TILE_W;
    uint8_t col = (uint8_t)pos % CARD_TILE_W;
    grid[row][col] = tile;
}

static void set_pips_for_rank(uint8_t grid[CARD_TILE_H][CARD_TILE_W], uint8_t rank, uint8_t suit_tile)
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
            grid[row][col] = suit_tile;
        }
    }
}

static void set_face_figure(
    uint8_t grid[CARD_TILE_H][CARD_TILE_W],
    uint8_t rank,
    uint8_t black,
    uint8_t suit_tile)
{
    /*
     * Figure cards (J/Q/K) are assembled from dedicated portrait components.
     * Suit marker must be in row 1, col 0 (left side), matching the template.
     */
    set_card_pos(grid, MIDDLE1_LEFT, suit_tile);

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

static void build_card_tile_grid_internal(uint8_t grid[CARD_TILE_H][CARD_TILE_W], uint8_t card)
{
    uint8_t rank = (uint8_t)(card % 13U);
    uint8_t suit = (uint8_t)((card / 13U) % CARD_SUIT_COUNT);
    uint8_t color = kSuitColorBySuit[suit];
    uint8_t black = (color == COLOR_BLACK);

    init_card_grid(grid, kWhiteCardTile);
    set_card_pos(grid, TOP_LEFT, black ? kRankGlyphBlack[rank] : kRankGlyphRed[rank]);

    if (rank < RANK_JACK) {
        set_pips_for_rank(grid, rank, kSuitTileBySuit[suit]);
    } else {
        set_face_figure(grid, rank, black, kSuitTileBySuit[suit]);
    }
}

void assets_build_card_tile_grid(uint8_t grid[4][3], uint8_t card)
{
    build_card_tile_grid_internal(grid, card);
}

void assets_build_back_tile_grid(uint8_t grid[4][3])
{
    for (uint8_t row = 0; row < CARD_TILE_H; row++) {
        for (uint8_t col = 0; col < CARD_TILE_W; col++) {
            grid[row][col] = kBackCardTiles[row][col];
        }
    }
}

uint8_t assets_get_layout_tile(uint8_t x, uint8_t y)
{
    if (x >= SCREEN_TILE_W || y >= SCREEN_TILE_H) {
        return EMPTY_TILE;
    }
    return ((uint8_t*)&_ztm_cards_start)[((uint16_t)y * SCREEN_TILE_W) + x];
}

zos_err_t load_zmt(track_t* track, uint8_t index)
{
    zos_err_t err = ERR_INVALID_OFFSET;
    track->title[0] = (char)ZMT_INDEX_NONE;

    zmt_reset(VOL_50);
    switch (index) {
        case ZMT_INDEX_SPLASH: {
            const size_t size = &_zmt_track1_end - &_zmt_track1_start;
            err = zmt_rom_load(track, &_zmt_track1_start, size);
        } break;
        case ZMT_INDEX_GAME: {
            const size_t size = &_zmt_track2_end - &_zmt_track2_start;
            err = zmt_rom_load(track, &_zmt_track2_start, size);
        } break;
    }
    if (err == ERR_SUCCESS) {
        track->title[0] = (char)index;
    } else {
        put_s("Warning: Failed to load track: ");
        put_c((char)index);
        put_c('\n');
    }
    return err;
}

void __assets__(void) __naked
{
    __asm__(
        "__ztm_cards_start:\n"
        "    .incbin \"assets/cards.ztm\"\n"
        "__ztm_cards_end:\n"

        "__zmt_track1_start:\n"
        "    .incbin \"assets/splash.zmt\"\n"
        "__zmt_track1_end:\n"

        "__zmt_track2_start:\n"
        "    .incbin \"assets/music.zmt\"\n"
        "__zmt_track2_end:\n");
}
