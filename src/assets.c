#include <stdint.h>

#include <zos_errors.h>
#include <zos_vfs.h>
#include <zvb_gfx.h>
#include <zgdk/sound/tracker.h>
#include <core.h>

#include "assets.h"
#include "app_state.h"

extern uint8_t _ztm_cards0000_start;
extern uint8_t _ztm_cards0000_end;
extern uint8_t _ztm_cards0001_start;
extern uint8_t _ztm_cards0001_end;

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

/*
 * Pack four 3-bit rows into one 12-bit pip mask.
 * Bit 0 = TOP_LEFT, bit 11 = BOTTOM_RIGHT.
 */
#define PIPS(r0, r1, r2, r3) \
    ((uint16_t)((r0) | ((r1) << 3) | ((r2) << 6) | ((r3) << 9)))

/* Core card composition tile IDs from cards.gif. */
static const uint8_t kWhiteCardTile = 16;

static const uint8_t kSuitTileBySuit[CARD_SUIT_COUNT] = {
    3,   /* Hearts */
    5,  /* Diamonds */
    6, /* Spades */
    8   /* Clubs */
};

/* Explicit suit->color mapping to avoid implicit dependency on suit ordering. */
static const uint8_t kSuitColorBySuit[CARD_SUIT_COUNT] = {
    COLOR_RED,   /* Hearts */
    COLOR_RED,   /* Diamonds */
    COLOR_BLACK, /* Spades */
    COLOR_BLACK, /* Clubs */
};

static const uint8_t kRankGlyphRed[CARD_RANK_COUNT] = {
    /* A,2,3,4,5,6,7,8,9,10,J,Q,K */
    48,49,50,51,52,53,54,55,56,57,58,59,60
};

static const uint8_t kRankGlyphBlack[CARD_RANK_COUNT] = {
    /* A,2,3,4,5,6,7,8,9,10,J,Q,K */
    64,65,66,67,68,69,70,71,72,73,74,75,76
};

/* J and K are center-column portraits (top/mid/bottom) with red/black variants. */
static const FaceColumn kJackFaceByColor[2] = {
    {
        1,   /* top -> MIDDLE1_CENTRE */
        17,  /* mid -> MIDDLE2_CENTRE */
        33   /* bottom -> BOTTOM_CENTRE */
    },       /* Red J */
    {
        2,   /* top -> MIDDLE1_CENTRE */
        18,  /* mid -> MIDDLE2_CENTRE */
        34   /* bottom -> BOTTOM_CENTRE */
    },       /* Black J */
};

static const FaceColumn kKingFaceByColor[2] = {
    {
        9,   /* top -> MIDDLE1_CENTRE */
        25,  /* mid -> MIDDLE2_CENTRE */
        41   /* bottom -> BOTTOM_CENTRE */
    },       /* Red K */
    {
        10,  /* top -> MIDDLE1_CENTRE */
        26,  /* mid -> MIDDLE2_CENTRE */
        42   /* bottom -> BOTTOM_CENTRE */
    },       /* Black K */
};

/* Q uses wider portrait fragments. */
static const QueenFace kQueenFaceByColor[2] = {
    {
        4,   /* top -> MIDDLE1_CENTRE */
        19,  /* mid_left -> MIDDLE2_LEFT */
        20,  /* mid_center -> MIDDLE2_CENTRE */
        21,  /* mid_right -> MIDDLE2_RIGHT */
        35,  /* bottom_left -> BOTTOM_LEFT */
        36,  /* bottom_center -> BOTTOM_CENTRE */
        37   /* bottom_right -> BOTTOM_RIGHT */
    },       /* Red Q */
    {
        7,   /* top -> MIDDLE1_CENTRE */
        22,  /* mid_left -> MIDDLE2_LEFT */
        23,  /* mid_center -> MIDDLE2_CENTRE */
        24,  /* mid_right -> MIDDLE2_RIGHT */
        38,  /* bottom_left -> BOTTOM_LEFT */
        39,  /* bottom_center -> BOTTOM_CENTRE */
        40   /* bottom_right -> BOTTOM_RIGHT */
    },       /* Black Q */
};

/* Fixed 3x4 red-back card layout from cards.gif/cards.tmx. */
static const uint8_t kBackCardTiles[CARD_TILE_H][CARD_TILE_W] = {
    {80, 81, 82},
    {83, 84, 85},
    {86, 87, 88},
    {89, 90, 91},
};

static gfx_error load_palette_from_file(gfx_context* ctx, const char* path)
{
    uint8_t from_color = 0;
    zos_dev_t dev = open(path, O_RDONLY);
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

static gfx_error load_tileset_from_file(gfx_context* ctx, const char* path, uint8_t compression_mode, uint16_t vram_scale)
{
    uint16_t from_byte = 0;
    zos_dev_t dev = open(path, O_RDONLY);
    if (dev < 0) {
        return GFX_FAILURE;
    }

    while (1) {
        uint16_t size = sizeof(g_buf);
        gfx_tileset_options options = {
            .compression = compression_mode,
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
        from_byte = (uint16_t)(from_byte + (size * vram_scale));
    }

    close(dev);
    return GFX_SUCCESS;
}

gfx_error load_cards_palette(gfx_context* ctx)
{
    return load_palette_from_file(ctx, "assets/cards.ztp");
}

gfx_error load_cards_tileset(gfx_context* ctx)
{
    if (load_tileset_from_file(ctx, "assets/cards.zts", TILESET_COMP_4BIT, 2) != GFX_SUCCESS) {
        return GFX_FAILURE;
    }

    /*
     * Reserve tile 255 as a guaranteed transparent tile (palette index 0),
     * independent of artwork ordering inside cards.zts.
     */
    if (gfx_tileset_add_color_tile(ctx, 255, 0) != GFX_SUCCESS) {
        return GFX_FAILURE;
    }

    return GFX_SUCCESS;
}

gfx_error load_rewards_palette(gfx_context* ctx)
{
    return load_palette_from_file(ctx, "assets/rewards.ztp");
}

gfx_error load_rewards_bitmap_256(gfx_context* ctx, uint8_t reveal_stage)
{
    /*
     * rewards.zts is exported as a tileset (tile-major), not linear scanlines.
     * For bitmap mode, VRAM expects linear 8bpp pixel indices:
     *   byte offset = y * 256 + x
     *
     * gif2zeal outputs 16x16 tiles for ZVB assets.
     * In 2bpp, each tile row is 16 pixels packed into 4 bytes:
     *   [p0 p1 p2 p3] [p4 p5 p6 p7] [p8 p9 p10 p11] [p12 p13 p14 p15]
     * where each p is a 2-bit palette index.
     */
    enum {
        BMP_W = 256,
        BMP_H = 240,
        TILE_W = 16,
        TILE_H = 16,
        TILES_X = BMP_W / TILE_W,
        TILES_Y = BMP_H / TILE_H,
        TILE_BYTES_2BPP = 64
    };

    static uint8_t tile_data[TILE_BYTES_2BPP];
    static uint8_t row_bytes[4];
    static uint8_t row8[TILE_W];
    uint8_t stage = reveal_stage;

    if (stage < 1U) {
        stage = 1U;
    } else if (stage > 4U) {
        stage = 4U;
    }

    zos_dev_t dev = open("assets/rewards.zts", O_RDONLY);
    if (dev < 0) {
        return GFX_FAILURE;
    }

    for (uint16_t tile_idx = 0; tile_idx < (uint16_t)(TILES_X * TILES_Y); tile_idx++) {
        uint16_t size = TILE_BYTES_2BPP;
        uint8_t tile_x = (uint8_t)(tile_idx % TILES_X);
        uint8_t tile_y = (uint8_t)(tile_idx / TILES_X);

        if (read(dev, tile_data, &size) != ERR_SUCCESS || size != TILE_BYTES_2BPP) {
            close(dev);
            return GFX_FAILURE;
        }

        for (uint8_t row = 0; row < TILE_H; row++) {
            row_bytes[0] = tile_data[(uint8_t)(row * 4)];
            row_bytes[1] = tile_data[(uint8_t)(row * 4 + 1)];
            row_bytes[2] = tile_data[(uint8_t)(row * 4 + 2)];
            row_bytes[3] = tile_data[(uint8_t)(row * 4 + 3)];
            gfx_tileset_options options = {
                .compression = TILESET_COMP_NONE,
                .from_byte = (uint16_t)(((uint16_t)tile_y * TILE_H + row) * BMP_W + ((uint16_t)tile_x * TILE_W)),
                .pal_offset = 0,
                .opacity = 0,
            };

            row8[0]  = (uint8_t)((row_bytes[0] >> 6) & 0x03);
            row8[1]  = (uint8_t)((row_bytes[0] >> 4) & 0x03);
            row8[2]  = (uint8_t)((row_bytes[0] >> 2) & 0x03);
            row8[3]  = (uint8_t)(row_bytes[0] & 0x03);
            row8[4]  = (uint8_t)((row_bytes[1] >> 6) & 0x03);
            row8[5]  = (uint8_t)((row_bytes[1] >> 4) & 0x03);
            row8[6]  = (uint8_t)((row_bytes[1] >> 2) & 0x03);
            row8[7]  = (uint8_t)(row_bytes[1] & 0x03);
            row8[8]  = (uint8_t)((row_bytes[2] >> 6) & 0x03);
            row8[9]  = (uint8_t)((row_bytes[2] >> 4) & 0x03);
            row8[10] = (uint8_t)((row_bytes[2] >> 2) & 0x03);
            row8[11] = (uint8_t)(row_bytes[2] & 0x03);
            row8[12] = (uint8_t)((row_bytes[3] >> 6) & 0x03);
            row8[13] = (uint8_t)((row_bytes[3] >> 4) & 0x03);
            row8[14] = (uint8_t)((row_bytes[3] >> 2) & 0x03);
            row8[15] = (uint8_t)(row_bytes[3] & 0x03);

            if (stage < 4U) {
                /*
                 * Progressive reveal:
                 * - stage 1 -> show only pixels where (index % 4) == 0
                 * - stage 2 -> show indices %4 in {0,1}
                 * - stage 3 -> show indices %4 in {0,1,2}
                 * - stage 4 -> show full image
                 */
                uint16_t y = (uint16_t)tile_y * TILE_H + row;
                uint16_t x_base = (uint16_t)tile_x * TILE_W;
                uint16_t base = (uint16_t)(y * BMP_W);

                for (uint8_t i = 0; i < TILE_W; i++) {
                    uint16_t linear_index = (uint16_t)(base + x_base + i);
                    if ((linear_index & 0x03U) >= stage) {
                        row8[i] = 0;
                    }
                }
            }

            if (gfx_tileset_load(ctx, row8, TILE_W, &options) != GFX_SUCCESS) {
                close(dev);
                return GFX_FAILURE;
            }
        }
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
    if (rank == RANK_ACE) {
        /*
         * card % 13 gives: A=0, 2=1, ..., 10=9, J=10, Q=11, K=12.
         * We keep Ace (0) out of the table and handle it directly.
         * Then the pip table is only 2..10 and indexing is simple:
         *   table_index = rank - 1   // rank 1..9 -> index 0..8
         */
        set_card_pos(grid, MIDDLE2_CENTRE, suit_tile);
        return;
    }

    /* Pip layouts for ranks 2..10 as four visual rows (top to bottom). */
    static const uint16_t kPipMaskByRank[9] = {
        /* 2
           000
           010
           000
           010 */
        PIPS(0b000, 0b010, 0b000, 0b010),

        /* 3
           000
           010
           010
           010 */
        PIPS(0b000, 0b010, 0b010, 0b010),

        /* 4
           000
           101
           000
           101 */
        PIPS(0b000, 0b101, 0b000, 0b101),

        /* 5
           000
           101
           010
           101 */
        PIPS(0b000, 0b101, 0b010, 0b101),

        /* 6
           000
           101
           101
           101 */
        PIPS(0b000, 0b101, 0b101, 0b101),

        /* 7
           000
           101
           111
           101 */
        PIPS(0b000, 0b101, 0b111, 0b101),

        /* 8
           000
           101
           111
           111 */
        PIPS(0b000, 0b101, 0b111, 0b111),

        /* 9
           000
           111
           111
           111 */
        PIPS(0b000, 0b111, 0b111, 0b111),

        /* 10
           010
           111
           111
           111 */
        PIPS(0b010, 0b111, 0b111, 0b111),
    };

    uint16_t mask;
    uint8_t pos;
    uint8_t row;
    uint8_t col;

    if (rank < 1U || rank >= 10U) {
        return;
    }

    mask = kPipMaskByRank[rank - 1U];
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

static uint8_t assets_get_layout_tile_from(const uint8_t* map, uint8_t x, uint8_t y)
{
    if (x >= SCREEN_TILE_W || y >= SCREEN_TILE_H) {
        return EMPTY_TILE;
    }
    return map[((uint16_t)y * SCREEN_TILE_W) + x];
}

uint8_t assets_get_layout_tile(uint8_t x, uint8_t y)
{
    return assets_get_layout_tile_from((const uint8_t*)&_ztm_cards0000_start, x, y);
}

uint8_t assets_get_layout_overlay_tile(uint8_t x, uint8_t y)
{
    return assets_get_layout_tile_from((const uint8_t*)&_ztm_cards0001_start, x, y);
}

zos_err_t load_zmt(track_t* track, uint8_t index)
{
    zos_err_t err = ERR_INVALID_OFFSET;
    track->title[0] = (char)ZMT_INDEX_NONE;

    zmt_reset(VOL_50);
    switch (index) {
        case ZMT_INDEX_SPLASH: {
            err = zmt_file_load(track, "assets/splash.zmt");
        } break;
        case ZMT_INDEX_GAME: {
            err = zmt_file_load(track, "assets/music.zmt");
        } break;
    }
    if (err == ERR_SUCCESS) {
        track->title[0] = (char)index;
    }
    return err;
}

void __assets__(void) __naked
{
    __asm__(
        "__ztm_cards0000_start:\n"
        "    .incbin \"assets/cards0000.ztm\"\n"
        "__ztm_cards0000_end:\n"

        "__ztm_cards0001_start:\n"
        "    .incbin \"assets/cards0001.ztm\"\n"
        "__ztm_cards0001_end:\n");
}
