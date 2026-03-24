#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <zos_sys.h>
#include <zos_time.h>
#include <zos_video.h>
#include <zos_vfs.h>
#include <zos_keyboard.h>
#include <zvb_gfx.h>
#include <zvb_sound.h>
#include <zgdk.h>

#include "assets.h"
#include "layout_map.h"
#include "videopoker.h"

/* Sound slot used for card-placement SFX. */
#define CARD_SOUND 0
/* Tuned for a softer "card on poker desk" feel. */
#define CARD_SFX_BASE_FREQ 10
#define CARD_SFX_JITTER_MASK 0x03
#define CARD_SFX_DURATION 1
#define CARD_REVEAL_DELAY 4
#define CARD_SFX_WAVEFORM WAV_SAWTOOTH
#define CARD_TILESET_COLUMNS 41
#define CARD_TILESET_ROWS 5
#define CARD_TILESET_MAX_GID (CARD_TILESET_COLUMNS * CARD_TILESET_ROWS)
#define FONT_DIGIT_COUNT 10
#define FONT_ALPHA_COUNT 13
#define LAYOUT_SPACE_SAMPLE_X 2
#define CARD_SHARED_TILE_BASE 184
#define CARD_SHARED_TILE_CAPACITY ((uint8_t)(256 - CARD_SHARED_TILE_BASE))

/* Global graphics context used by ZVB drawing APIs. */
gfx_context vctx;

/* Current five cards on the table and working deck state. */
static PokerCard cards[CARD_COUNT];
static uint8_t deck[DECK_SIZE];
static uint8_t deck_pos = 0;

/* High-level game flow: bet -> hold/draw -> result -> bet. */
static GameState state = STATE_BET;

/* Persistent player/game values. */
static uint16_t credits = INITIAL_CREDITS;
static uint8_t bet = 1;
static uint16_t win_amount = 0;

/* UI state flags. */
static uint8_t show_win_banner = 0;
static uint8_t show_card_faces = 0;
static uint8_t needs_redraw = 1;
static uint8_t needs_hud_redraw = 0;
static char win_banner_text[36] = "YOU HAVE WON!";

/* Small entropy accumulator mixed into RNG seed values. */
static uint16_t entropy = 1;
static uint8_t rng_seeded = 0;
static uint8_t reveal_mask = 0x1F;
static uint8_t reveal_slots[CARD_COUNT];
static uint8_t reveal_len = 0;
static uint8_t reveal_index = 0;
static uint8_t reveal_cooldown = 0;
static uint8_t reveal_active = 0;
/* Per-slot one-shot SFX trigger, consumed in render_cards() when slot is drawn. */
static uint8_t reveal_sfx_pending_mask = 0;

/* Snapshot of key events for one update tick. */
typedef struct {
    uint8_t up;
    uint8_t down;
    uint8_t enter;
    uint8_t space;
    uint8_t quit;
    uint8_t hold_toggle[CARD_COUNT];
} KeyEvents;

/* Debounce timer to avoid Enter/Space double-triggering state transitions. */
static uint8_t suppress_enter_ticks = 0;
/* Requires Enter/Space release before accepting next confirm action. */
static uint8_t confirm_armed = 0;
/* Per-slot render invalidation mask for incremental redraws. */
static uint8_t dirty_slots[CARD_COUNT];
/* Forces one pass over all slots (startup/phase reset). */
static uint8_t full_redraw = 1;

/* Map GID -> runtime tile ID remap generated from layout_map.h usage. */
static uint16_t mapped_gids[MAP_TILE_CAPACITY];
static uint8_t mapped_tiles[MAP_TILE_CAPACITY];
static uint8_t mapped_count = 0;
/* Runtime mapping for shared card-component tiles (source GID -> runtime tile). */
static uint8_t card_gid_to_runtime[CARD_TILESET_MAX_GID + 1];

/* Position of each playable card slot (top-left tile of 3x4 card). */
static const uint8_t slot_x[CARD_COUNT] = {5, 12, 19, 26, 33};
static const uint8_t slot_y = 21;
static const uint8_t all_slots[CARD_COUNT] = {0, 1, 2, 3, 4};

/* Text anchor per card for "HOLD" labels in the bottom panel. */
static const uint8_t hold_x[CARD_COUNT] = {4, 11, 18, 25, 32};
static const uint8_t hold_y = 27;

/* HUD numeric fields in tiles. */
static const uint8_t bet_x = 6;
static const uint8_t bet_y = 17;
static const uint8_t win_x = 19;
static const uint8_t win_y = 17;
static const uint8_t credit_x = 34;
static const uint8_t credit_y = 17;

/* Critical source GIDs used outside TMX map rendering. */
static const uint16_t kHoldFrameSourceGid = 105;
static const uint16_t kDigitGidBase = 165;
static const uint16_t kAlphaAGidBase = 175;
static const uint16_t kAlphaNGidBase = 188;
static const uint16_t kColonGid = 201;
static const uint16_t kExclGid = 202;

static void restart_if_credit_low(void);
static void return_to_bet_phase(void);
static void reseed_rng_for_new_hand(void);
static void start_reveal_sequence(const uint8_t* slots, uint8_t len, uint8_t initial_mask);
static void update_reveal_sequence(void);
static void play_card_place_sound(void);
static void set_win_banner_from_result(const HandResult* result);
static void mark_all_slots_dirty(void);
static void mark_slot_dirty(uint8_t slot);
static uint8_t validate_startup_tiles(void);
static uint8_t init_card_component_tiles(void);

/* Card IDs are 0..51, grouped by suits in blocks of 13. */
static uint8_t card_rank(uint8_t card)
{
    return card % 13;
}

/* Suit index: 0..3 from card ID. */
static uint8_t card_suit(uint8_t card)
{
    return card / 13;
}

/* Translate TMX GID to the runtime tile ID loaded into VRAM. */
static uint8_t map_gid_to_tile(uint16_t gid)
{
    for (uint8_t i = 0; i < mapped_count; i++) {
        if (mapped_gids[i] == gid) {
            return mapped_tiles[i];
        }
    }

    return FONT_SPACE_TILE;
}

static uint8_t is_gid_mapped(uint16_t gid)
{
    for (uint8_t i = 0; i < mapped_count; i++) {
        if (mapped_gids[i] == gid) {
            return 1;
        }
    }
    return 0;
}

static uint8_t validate_gid_range(uint16_t gid, const char* name)
{
    if (gid == 0 || gid > CARD_TILESET_MAX_GID) {
        printf("Tile self-check failed: %s GID %u out of 1..%u\n", name, gid, CARD_TILESET_MAX_GID);
        return 0;
    }
    return 1;
}

static uint8_t validate_startup_tiles(void)
{
    /*
     * Early guard against cards.gif/cards.tsx drift.
     * Validate only critical hardcoded GIDs used by font/back/hold paths.
     */
    uint8_t ok = 1;
    uint16_t space_gid = kLayoutGids[(hold_y * LAYOUT_W) + LAYOUT_SPACE_SAMPLE_X];

    ok &= validate_gid_range(kHoldFrameSourceGid, "hold_frame");
    ok &= validate_gid_range(space_gid, "space_bg");
    ok &= validate_gid_range(kColonGid, "font_colon");
    ok &= validate_gid_range(kExclGid, "font_excl");
    ok &= validate_gid_range((uint16_t)(kDigitGidBase + (FONT_DIGIT_COUNT - 1)), "font_digit_9");
    ok &= validate_gid_range((uint16_t)(kAlphaAGidBase + (FONT_ALPHA_COUNT - 1)), "font_alpha_M");
    ok &= validate_gid_range((uint16_t)(kAlphaNGidBase + (FONT_ALPHA_COUNT - 1)), "font_alpha_Z");

    if (!is_gid_mapped(space_gid)) {
        printf("Tile self-check failed: space_bg GID %u missing from map remap\n", space_gid);
        ok = 0;
    }

    return ok;
}

static uint8_t map_card_gid_to_tile(uint16_t gid)
{
    if (gid == 0 || gid > CARD_TILESET_MAX_GID) {
        return FONT_SPACE_TILE;
    }

    if (card_gid_to_runtime[gid] == 0) {
        return FONT_SPACE_TILE;
    }

    return card_gid_to_runtime[gid];
}

static uint8_t init_card_component_tiles(void)
{
    uint16_t gids[CARD_SHARED_TILE_CAPACITY];
    uint8_t count = assets_collect_component_gids(gids, CARD_SHARED_TILE_CAPACITY);

    memset(card_gid_to_runtime, 0, sizeof(card_gid_to_runtime));

    if (count == 0) {
        printf("Card tile init failed: no component GIDs collected\n");
        return 0;
    }

    if (count > CARD_SHARED_TILE_CAPACITY) {
        printf("Card tile init failed: component count %u exceeds capacity %u\n", count, CARD_SHARED_TILE_CAPACITY);
        return 0;
    }

    for (uint8_t i = 0; i < count; i++) {
        uint16_t gid = gids[i];
        uint8_t runtime_tile = (uint8_t)(CARD_SHARED_TILE_BASE + i);

        if (!validate_gid_range(gid, "card_component")) {
            return 0;
        }

        if (load_source_tile(&vctx, gid, (uint16_t)runtime_tile * TILE_SIZE) != GFX_SUCCESS) {
            printf("Card tile init failed: load GID %u\n", gid);
            return 0;
        }

        card_gid_to_runtime[gid] = runtime_tile;
    }

    return 1;
}

static void load_ui_font_tiles(void)
{
    uint16_t space_gid = kLayoutGids[(hold_y * LAYOUT_W) + LAYOUT_SPACE_SAMPLE_X];

    /* Frame tile used to highlight held cards (green suit-symbol tile). */
    load_source_tile(&vctx, kHoldFrameSourceGid, (uint16_t)HOLD_FRAME_TILE * TILE_SIZE);

    /* Space/background tile used to clear text areas cleanly. */
    load_source_tile(&vctx, space_gid, (uint16_t)FONT_SPACE_TILE * TILE_SIZE);

    /* Digits and A-Z are loaded from your font strip row in cards.gif. */
    for (uint8_t i = 0; i < FONT_DIGIT_COUNT; i++) {
        load_source_tile(&vctx, (uint16_t)(kDigitGidBase + i), (uint16_t)(FONT_DIGIT_TILE + i) * TILE_SIZE);
    }

    for (uint8_t i = 0; i < FONT_ALPHA_COUNT; i++) {
        load_source_tile(&vctx, (uint16_t)(kAlphaAGidBase + i), (uint16_t)(FONT_ALPHA_A_TILE + i) * TILE_SIZE);
    }

    for (uint8_t i = 0; i < FONT_ALPHA_COUNT; i++) {
        load_source_tile(&vctx, (uint16_t)(kAlphaNGidBase + i), (uint16_t)(FONT_ALPHA_N_TILE + i) * TILE_SIZE);
    }

    /* Punctuation tiles follow Z in the custom font strip. */
    load_source_tile(&vctx, kColonGid, (uint16_t)FONT_COLON_TILE * TILE_SIZE);
    load_source_tile(&vctx, kExclGid, (uint16_t)FONT_EXCL_TILE * TILE_SIZE);

    ascii_map(' ', 1, FONT_SPACE_TILE);
    ascii_map('0', 10, FONT_DIGIT_TILE);    // 0-9
    ascii_map('A', 13, FONT_ALPHA_A_TILE);  // A-M
    ascii_map('a', 13, FONT_ALPHA_A_TILE);  // A-M
    ascii_map('N', 13, FONT_ALPHA_N_TILE);  // N-Z
    ascii_map('n', 13, FONT_ALPHA_N_TILE);  // N-Z
    ascii_map(':', 1, FONT_COLON_TILE);
    ascii_map('!', 1, FONT_EXCL_TILE);
}

/* Restore one map cell from original TMX layout (used to erase overlays). */
static void restore_map_cell(uint8_t x, uint8_t y)
{
    uint16_t gid = kLayoutGids[(y * LAYOUT_W) + x];
    gfx_tilemap_place(&vctx, map_gid_to_tile(gid), TILEMAP_LAYER, x, y);
}

static void draw_hold_frames(void)
{
    /* Draw/remove a 1-tile border around each card slot based on hold flag. */
    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        uint8_t show_frame = (state == STATE_HOLD) && cards[i].held;
        uint8_t x0 = (uint8_t)(slot_x[i] - 1);
        uint8_t y0 = (uint8_t)(slot_y - 1);
        uint8_t x1 = (uint8_t)(slot_x[i] + SRC_CARD_W);
        uint8_t y1 = (uint8_t)(slot_y + SRC_CARD_H);

        for (uint8_t x = x0; x <= x1; x++) {
            if (show_frame) {
                gfx_tilemap_place(&vctx, HOLD_FRAME_TILE, TILEMAP_LAYER, x, y0);
                gfx_tilemap_place(&vctx, HOLD_FRAME_TILE, TILEMAP_LAYER, x, y1);
            } else {
                restore_map_cell(x, y0);
                restore_map_cell(x, y1);
            }
        }

        for (uint8_t y = (uint8_t)(y0 + 1); y < y1; y++) {
            if (show_frame) {
                gfx_tilemap_place(&vctx, HOLD_FRAME_TILE, TILEMAP_LAYER, x0, y);
                gfx_tilemap_place(&vctx, HOLD_FRAME_TILE, TILEMAP_LAYER, x1, y);
            } else {
                restore_map_cell(x0, y);
                restore_map_cell(x1, y);
            }
        }
    }
}

static void init_layout_tiles(void)
{
    /* Load each unique GID from the TMX map exactly once into VRAM tile slots. */
    mapped_count = 0;

    for (uint16_t i = 0; i < (LAYOUT_W * LAYOUT_H); i++) {
        uint16_t gid = kLayoutGids[i];
        uint8_t found = 0;

        for (uint8_t j = 0; j < mapped_count; j++) {
            if (mapped_gids[j] == gid) {
                found = 1;
                break;
            }
        }

        if (found) {
            continue;
        }

        if (mapped_count >= MAP_TILE_CAPACITY) {
            break;
        }

        mapped_gids[mapped_count] = gid;
        mapped_tiles[mapped_count] = (uint8_t)(MAP_TILE_BASE + mapped_count);

        load_source_tile(&vctx, gid, (uint16_t)mapped_tiles[mapped_count] * TILE_SIZE);
        mapped_count++;
    }
}

static void render_layout(void)
{
    /* Paint the full static background UI from TMX-derived GID data. */
    for (uint8_t y = 0; y < LAYOUT_H; y++) {
        for (uint8_t x = 0; x < LAYOUT_W; x++) {
            uint16_t gid = kLayoutGids[(y * LAYOUT_W) + x];
            uint8_t tile = map_gid_to_tile(gid);
            gfx_tilemap_place(&vctx, tile, TILEMAP_LAYER, x, y);
        }
    }
}

static void mark_all_slots_dirty(void)
{
    /* Used when phase/layout changes can affect all card slots at once. */
    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        dirty_slots[i] = 1;
    }
    full_redraw = 1;
}

static void mark_slot_dirty(uint8_t slot)
{
    if (slot < CARD_COUNT) {
        dirty_slots[slot] = 1;
    }
}

static void place_gid_grid_at(uint8_t x0, uint8_t y0, const uint16_t grid[SRC_CARD_H][SRC_CARD_W])
{
    for (uint8_t row = 0; row < SRC_CARD_H; row++) {
        for (uint8_t col = 0; col < SRC_CARD_W; col++) {
            uint16_t gid = grid[row][col];
            uint8_t tile = map_card_gid_to_tile(gid);
            gfx_tilemap_place(&vctx, tile, TILEMAP_LAYER, (uint8_t)(x0 + col), (uint8_t)(y0 + row));
        }
    }
}

static void draw_card_slot_direct(uint8_t slot, uint8_t show_face, uint8_t card)
{
    uint16_t grid[SRC_CARD_H][SRC_CARD_W];
    if (show_face) {
        assets_build_card_gid_grid(grid, card);
    } else {
        assets_build_back_gid_grid(grid);
    }
    place_gid_grid_at(slot_x[slot], slot_y, grid);
}

static void clear_card_slot(uint8_t slot)
{
    uint8_t x0 = slot_x[slot];
    for (uint8_t row = 0; row < SRC_CARD_H; row++) {
        for (uint8_t col = 0; col < SRC_CARD_W; col++) {
            restore_map_cell((uint8_t)(x0 + col), (uint8_t)(slot_y + row));
        }
    }
}

static void restore_hold_background(uint8_t slot)
{
    /* Keep helper in case per-slot text clears are needed again. */
    for (uint8_t col = 0; col < 4; col++) {
        uint8_t x = (uint8_t)(hold_x[slot] + col);
        restore_map_cell(x, hold_y);
    }
}

static void clear_bottom_row(void)
{
    /* Clears the action banner/hold row on tilemap layer 0. */
    for (uint8_t x = 2; x < 38; x++) {
        restore_map_cell(x, hold_y);
    }
}

static void clear_hud_field(uint8_t x, uint8_t y, uint8_t width)
{
    /* Clears one numeric HUD field before printing a new value. */
    for (uint8_t i = 0; i < width; i++) {
        restore_map_cell((uint8_t)(x + i), y);
    }
}

static void draw_hold_labels(void)
{
    static const char hold_text[] = "HOLD";
    static const char deal_text[] = "DEAL";
    static const char draw_text[] = "DRAW";
    static const char clear_row[] = "                                    ";
    uint8_t any_held = 0;

    clear_bottom_row();
    /* nprint_string draws on layer 1, so clear that row explicitly too. */
    nprint_string(&vctx, clear_row, 36, 2, hold_y);

    /*
     * Banner policy:
     * - BET phase: show DEAL
     * - HOLD phase: show DRAW until at least one hold is set, then show HOLD labels
     * - RESULT phase: show YOU HAVE WON! only for winning hands
     */
    if (state != STATE_HOLD) {
        if (show_win_banner) {
            uint8_t msg_len = (uint8_t)strlen(win_banner_text);
            uint8_t msg_x = (uint8_t)(2 + ((36 - msg_len) / 2));
            nprint_string(&vctx, win_banner_text, msg_len, msg_x, hold_y);
        } else if (state == STATE_BET || state == STATE_RESULT) {
            nprint_string(&vctx, deal_text, 4, 18, hold_y);
        }
        return;
    }
    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        if (cards[i].held) {
            any_held = 1;
            break;
        }
    }
    if (!any_held) {
        nprint_string(&vctx, draw_text, 4, 18, hold_y);
    }

    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        if (cards[i].held) {
            nprint_string(&vctx, hold_text, 4, hold_x[i], hold_y);
        }
    }
}

static uint8_t is_straight(const uint8_t ranks[13])
{
    /*
     * ranks[0] is Ace.
     * Supports:
     * - normal 5-card runs
     * - A-2-3-4-5
     * - 10-J-Q-K-A
     */
    uint8_t run = 0;

    for (uint8_t r = 0; r < 13; r++) {
        if (ranks[r]) {
            run++;
            if (run == 5) {
                return true;
            }
        } else {
            run = 0;
        }
    }

    if (ranks[0] && ranks[1] && ranks[2] && ranks[3] && ranks[4]) {
        return true;
    }

    if (ranks[0] && ranks[9] && ranks[10] && ranks[11] && ranks[12]) {
        return true;
    }

    return false;
}

static uint8_t is_royal(const uint8_t ranks[13])
{
    /* Royal uses 10/J/Q/K/A ranks; suit is checked separately by flush logic. */
    return ranks[0] && ranks[9] && ranks[10] && ranks[11] && ranks[12];
}

static void shuffle_deck(void)
{
    /* Fisher-Yates shuffle over full 52-card deck. */
    for (uint8_t i = 0; i < DECK_SIZE; i++) {
        deck[i] = i;
    }

    for (uint8_t i = DECK_SIZE - 1; i > 0; i--) {
        uint8_t j = rand8_quick() % (i + 1);
        uint8_t tmp = deck[i];
        deck[i] = deck[j];
        deck[j] = tmp;
    }

    deck_pos = 0;
}

static uint8_t pop_deck(void)
{
    /* Safety fallback if deck is exhausted unexpectedly. */
    if (deck_pos >= DECK_SIZE) {
        shuffle_deck();
    }
    return deck[deck_pos++];
}

static void draw_hud_values(void)
{
    char bet_buf[6];
    char win_buf[6];
    char credit_buf[6];
    /* Always print fixed-width 3 digits so HUD text does not jitter. */
    sprintf(bet_buf, "%03u", bet);
    sprintf(win_buf, "%03u", win_amount);
    sprintf(credit_buf, "%03u", credits);

    clear_hud_field(bet_x, bet_y, 4);
    clear_hud_field(win_x, win_y, 4);
    clear_hud_field(credit_x, credit_y, 4);
    restore_map_cell((uint8_t)(credit_x - 1), credit_y);

    nprint_string(&vctx, bet_buf, 3, bet_x, bet_y);
    nprint_string(&vctx, win_buf, 3, win_x, win_y);
    nprint_string(&vctx, credit_buf, 3, credit_x, credit_y);
}

HandResult evaluate_hand(const uint8_t hand[CARD_COUNT])
{
    uint8_t rank_counts[13] = {0};
    uint8_t suit_counts[4] = {0};

    uint8_t pairs = 0;
    uint8_t trips = 0;
    uint8_t quads = 0;

    /* Histogram ranks/suits once, then derive all categories from counts. */
    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        rank_counts[card_rank(hand[i])]++;
        suit_counts[card_suit(hand[i])]++;
    }

    uint8_t flush = false;
    for (uint8_t s = 0; s < 4; s++) {
        if (suit_counts[s] == 5) {
            flush = true;
            break;
        }
    }

    uint8_t straight = is_straight(rank_counts);

    for (uint8_t r = 0; r < 13; r++) {
        if (rank_counts[r] == 2) {
            pairs++;
        } else if (rank_counts[r] == 3) {
            trips++;
        } else if (rank_counts[r] == 4) {
            quads++;
        }
    }

    /*
     * Ranking priority follows the pay table from highest to lowest.
     * First match returns immediately.
     */
    if (straight && flush && is_royal(rank_counts)) {
        HandResult result = {250, "ROYAL"};
        return result;
    }
    if (straight && flush) {
        HandResult result = {50, "STRFL"};
        return result;
    }
    if (quads) {
        HandResult result = {25, "4KIND"};
        return result;
    }
    if (trips && pairs) {
        HandResult result = {9, "FULLHS"};
        return result;
    }
    if (flush) {
        HandResult result = {6, "FLUSH"};
        return result;
    }
    if (straight) {
        HandResult result = {4, "STRAIT"};
        return result;
    }
    if (trips) {
        HandResult result = {3, "3KIND"};
        return result;
    }
    if (pairs == 2) {
        HandResult result = {2, "2PAIR"};
        return result;
    }

    if (pairs == 1) {
        HandResult result = {1, "PAIR"};
        return result;
    }

    HandResult result = {0, "NOWIN"};
    return result;
}

static void set_win_banner_from_result(const HandResult* result)
{
    const char* combo = NULL;

    switch (result->multiplier) {
        case 250: combo = "ROYAL FLUSH"; break;
        case 50:  combo = "STRAIGHT FLUSH"; break;
        case 25:  combo = "FOUR OF A KIND"; break;
        case 9:   combo = "FULL HOUSE"; break;
        case 6:   combo = "FLUSH"; break;
        case 4:   combo = "STRAIGHT"; break;
        case 3:   combo = "THREE OF A KIND"; break;
        case 2:   combo = "TWO PAIR"; break;
        case 1:   combo = "PAIR"; break;
        default:  combo = NULL; break;
    }

    if (combo != NULL) {
        sprintf(win_banner_text, "%s X%u: YOU HAVE WON!", combo, result->multiplier);
    } else {
        strcpy(win_banner_text, "YOU HAVE WON!");
    }
}

void render_table(void)
{
    /* Static background render (called once at init). */
    render_layout();
}

void render_cards(void)
{
    /*
     * Draw only changed slots:
     * - avoids unnecessary tilemap writes
     * - card visuals are composed directly from shared runtime tiles
     */
    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        if (!full_redraw && !dirty_slots[i]) {
            continue;
        }

        if (reveal_mask & (uint8_t)(1U << i)) {
            draw_card_slot_direct(i, show_card_faces, cards[i].card);
            /* Play card SFX exactly when the card is visually placed (sync fix). */
            if (reveal_sfx_pending_mask & (uint8_t)(1U << i)) {
                play_card_place_sound();
                reveal_sfx_pending_mask &= (uint8_t)~(1U << i);
            }
        } else {
            clear_card_slot(i);
        }

        dirty_slots[i] = 0;
    }
    full_redraw = 0;

    /* Dynamic overlays are redrawn every time cards/HUD state changes. */
    draw_hold_frames();
    draw_hold_labels();
    draw_hud_values();
}

void deal_hand(void)
{
    /* Deal five fresh cards and move to hold selection phase. */
    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        cards[i].card = pop_deck();
        cards[i].held = false;
        mark_slot_dirty(i);
    }

    show_card_faces = 1;
    state = STATE_HOLD;
    start_reveal_sequence(all_slots, CARD_COUNT, 0);
    needs_redraw = 1;
}

void draw_hand(void)
{
    HandResult result;
    uint8_t hand[CARD_COUNT];
    uint8_t slots[CARD_COUNT];
    uint8_t slot_count = 0;
    uint8_t keep_mask = 0;

    /* Replace only non-held cards. Held cards remain unchanged. */
    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        if (!cards[i].held) {
            cards[i].card = pop_deck();
            slots[slot_count++] = i;
            mark_slot_dirty(i);
        } else {
            keep_mask |= (uint8_t)(1U << i);
        }
        hand[i] = cards[i].card;
    }

    /* Score result and credit winnings (multiplier * current bet). */
    result = evaluate_hand(hand);
    win_amount = (uint16_t)result.multiplier * bet;
    credits += win_amount;
    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        cards[i].held = false;
    }
    /*
     * Keep final hand visible in RESULT phase so player can inspect outcome.
     * Next Enter/Space returns to BET and shows backs again.
     */
    show_card_faces = 1;
    show_win_banner = (win_amount > 0);
    if (show_win_banner) {
        set_win_banner_from_result(&result);
    }
    state = STATE_RESULT;
    start_reveal_sequence(slots, slot_count, keep_mask);
    suppress_enter_ticks = 8;
    needs_redraw = 1;
    restart_if_credit_low();
}

static void start_new_round(void)
{
    /* Ensure RNG is valid, then begin a new paid hand. */
    if (!rng_seeded) {
        uint16_t seed = (uint16_t)(entropy | 1);
        rand8_seed(seed);
        rng_seeded = 1;
    }

    if (credits == 0) {
        return;
    }

    /* At hand start: take bet, clear previous win, reseed + reshuffle, deal. */
    credits -= bet;
    win_amount = 0;
    show_win_banner = 0;
    reseed_rng_for_new_hand();
    shuffle_deck();
    deal_hand();
    suppress_enter_ticks = 8;
}

static void seed_rng_from_time(void)
{
    /* Initial seeding at startup from system time and entropy accumulator. */
    zos_time_t now;
    uint16_t seed = 1;

    if (gettime(0, &now) == ERR_SUCCESS) {
        seed = (uint16_t)(now.t_millis ^ entropy);
    } else {
        seed = entropy;
    }

    if ((seed & 1U) == 0) {
        seed++;
    }

    rand8_seed(seed);
    rng_seeded = 1;
}

static void reseed_rng_for_new_hand(void)
{
    /*
     * Per-hand reseed to avoid repeating sequences between hands.
     * Mixes entropy, bankroll, bet, previous win, and current clock millis.
     */
    zos_time_t now;
    uint16_t seed = (uint16_t)(entropy ^ ((uint16_t)credits << 3) ^ ((uint16_t)bet << 9) ^ win_amount);

    if (gettime(0, &now) == ERR_SUCCESS) {
        seed ^= now.t_millis;
    }

    if ((seed & 1U) == 0) {
        seed++;
    }

    rand8_seed(seed);
    rng_seeded = 1;
}

static void start_reveal_sequence(const uint8_t* slots, uint8_t len, uint8_t initial_mask)
{
    reveal_mask = initial_mask;
    reveal_len = (len > CARD_COUNT) ? CARD_COUNT : len;
    reveal_index = 0;
    reveal_cooldown = 0;
    reveal_active = (reveal_len > 0);
    reveal_sfx_pending_mask = 0;
    /* Visibility masks changed; redraw card region conservatively. */
    mark_all_slots_dirty();

    for (uint8_t i = 0; i < reveal_len; i++) {
        reveal_slots[i] = slots[i];
    }
}

static void update_reveal_sequence(void)
{
    if (!reveal_active) {
        return;
    }

    if (reveal_cooldown > 0) {
        reveal_cooldown--;
        return;
    }

    uint8_t slot = reveal_slots[reveal_index];
    reveal_mask |= (uint8_t)(1U << slot);
    /* Reveal animation updates one slot at a time. */
    mark_slot_dirty(slot);
    /* Defer SFX until render pass actually places this card on screen. */
    reveal_sfx_pending_mask |= (uint8_t)(1U << slot);

    reveal_index++;
    needs_redraw = 1;

    if (reveal_index >= reveal_len) {
        reveal_active = 0;
    } else {
        reveal_cooldown = CARD_REVEAL_DELAY;
    }
}

static void play_card_place_sound(void)
{
    uint8_t jitter = (uint8_t)(rand8_quick() & CARD_SFX_JITTER_MASK);
    Sound* tap = sound_get(CARD_SOUND);
    if (tap != NULL) {
        tap->waveform = CARD_SFX_WAVEFORM;
    }

    tap = sound_play(CARD_SOUND, (uint16_t)(CARD_SFX_BASE_FREQ + jitter), CARD_SFX_DURATION);
    if (tap != NULL) {
        tap->waveform = CARD_SFX_WAVEFORM;
    }
}

static void restart_if_credit_low(void)
{
    /* Restart only when bankroll is exhausted, not after ordinary losses. */
    if (credits > 0) {
        return;
    }

    credits = RESET_CREDITS;
    bet = 1;
    win_amount = 0;
    show_win_banner = 0;
    show_card_faces = 0;
    reveal_active = 0;
    reveal_mask = 0;
    start_reveal_sequence(all_slots, CARD_COUNT, 0);
    mark_all_slots_dirty();
    suppress_enter_ticks = 8;

    shuffle_deck();
    state = STATE_BET;
    needs_redraw = 1;
    needs_hud_redraw = 0;
}

static void return_to_bet_phase(void)
{
    /* Return from RESULT to BET UI: clear holds/banner and show red backs. */
    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        cards[i].held = false;
    }

    show_win_banner = 0;
    show_card_faces = 0;
    reveal_active = 0;
    reveal_mask = 0;
    start_reveal_sequence(all_slots, CARD_COUNT, 0);
    mark_all_slots_dirty();
    state = STATE_BET;
    suppress_enter_ticks = 8;
    needs_redraw = 1;
    needs_hud_redraw = 0;
}

static void poll_keys(KeyEvents* ev)
{
    /*
     * Read all pending keyboard bytes this tick and convert to one-shot events.
     * KB_RELEASED marker is skipped so hold toggles happen on press only.
     */
    uint8_t buf[32];
    uint8_t released = 0;

    memset(ev, 0, sizeof(*ev));

    while (1) {
        uint16_t size = sizeof(buf);
        if (read(DEV_STDIN, buf, &size) != ERR_SUCCESS || size == 0) {
            break;
        }

        for (uint16_t i = 0; i < size; i++) {
            uint8_t key = buf[i];
            if (key == KB_RELEASED) {
                released = 1;
                continue;
            }
            if (released) {
                released = 0;
                continue;
            }

            switch (key) {
                case KB_UP_ARROW: ev->up = 1; break;
                case KB_DOWN_ARROW: ev->down = 1; break;
                case KB_KEY_ENTER: ev->enter = 1; break;
                case KB_KEY_SPACE: ev->space = 1; break;
                case KB_KEY_A: ev->hold_toggle[0] = 1; break;
                case KB_KEY_S: ev->hold_toggle[1] = 1; break;
                case KB_KEY_D: ev->hold_toggle[2] = 1; break;
                case KB_KEY_F: ev->hold_toggle[3] = 1; break;
                case KB_KEY_G: ev->hold_toggle[4] = 1; break;
                case KB_RIGHT_SHIFT:
                case KB_KEY_QUOTE:
                    ev->quit = 1;
                    break;
                default:
                    break;
            }
        }
    }
}

void init(void)
{
    /* Initialize input, graphics mode, assets, and initial BET screen state. */
    zos_err_t err = input_init(true);
    if (err != ERR_SUCCESS) {
        printf("Input init failed: %d\n", err);
        exit(1);
    }

    gfx_enable_screen(0);

    err = gfx_initialize(ZVB_CTRL_VID_MODE_GFX_640_8BIT, &vctx);
    if (err != ERR_SUCCESS) {
        printf("GFX init failed: %d\n", err);
        exit(1);
    }

    err = load_cards_palette(&vctx);
    if (err != GFX_SUCCESS) {
        printf("Palette load failed: %d\n", err);
        exit(1);
    }

    sound_init();
    Sound* card_sound = sound_get(CARD_SOUND);
    if (card_sound != NULL) {
        card_sound->waveform = CARD_SFX_WAVEFORM;
    }

    init_layout_tiles();
    if (!validate_startup_tiles()) {
        printf("Critical tile validation failed. Check cards.gif/cards.tsx/cards.tmx.\n");
        exit(1);
    }
    if (assets_validate_card_tables(CARD_TILESET_MAX_GID) != GFX_SUCCESS) {
        printf("Card composition tile validation failed. Check cards.gif tile mappings.\n");
        exit(1);
    }
    if (!init_card_component_tiles()) {
        printf("Card component tile preload failed.\n");
        exit(1);
    }
    load_ui_font_tiles();
    render_table();
    mark_all_slots_dirty();

    seed_rng_from_time();
    shuffle_deck();
    show_card_faces = 0;
    start_reveal_sequence(all_slots, CARD_COUNT, 0);
    state = STATE_BET;
    suppress_enter_ticks = 8;
    confirm_armed = 0;
    render_cards();
    needs_redraw = 0;
    needs_hud_redraw = 0;

    gfx_enable_screen(1);
}

void deinit(void)
{
    /* Restore text screen before exiting back to shell/system. */
    sound_stop_all();
    sound_deinit();
    /* Close persistent asset streams opened by assets.c optimization. */
    assets_shutdown();
    ioctl(DEV_STDOUT, CMD_RESET_SCREEN, NULL);
}

void update(void)
{
    /* Game logic tick: input/state transitions only (no VRAM drawing here). */
    KeyEvents ev;
    sound_loop();
    update_reveal_sequence();
    poll_keys(&ev);
    entropy++;
    if (suppress_enter_ticks > 0) {
        suppress_enter_ticks--;
    }
    /* Re-arm confirm only after Enter/Space are fully released. */
    if (!ev.enter && !ev.space) {
        confirm_armed = 1;
    }

    if (ev.quit) {
        deinit();
        exit(0);
    }

    if (state == STATE_HOLD) {
        if (reveal_active) {
            return;
        }
        /* Toggle holds with A/S/D/F/G; Enter performs draw. */
        for (uint8_t i = 0; i < CARD_COUNT; i++) {
            if (ev.hold_toggle[i]) {
                cards[i].held ^= 1;
                needs_hud_redraw = 1;
            }
        }
        if (ev.enter && suppress_enter_ticks == 0 && confirm_armed) {
            confirm_armed = 0;
            draw_hand();
        }
        return;
    }

    if (state == STATE_RESULT) {
        /* RESULT waits for confirmation before returning to BET phase. */
        if (suppress_enter_ticks == 0) {
            if (show_win_banner && (ev.up || ev.down || ev.enter || ev.space)) {
                show_win_banner = 0;
                needs_hud_redraw = 1;
            }
            if ((ev.enter || ev.space) && confirm_armed) {
                confirm_armed = 0;
                return_to_bet_phase();
            }
        }
        return;
    }

    if (state == STATE_BET) {
        /* BET phase: adjust bet with arrows, then Enter/Space to deal. */
        if (show_win_banner && (ev.up || ev.down || ev.enter || ev.space)) {
            show_win_banner = 0;
            needs_hud_redraw = 1;
        }

        if (ev.up && bet < MAX_BET && bet < credits) {
            bet++;
            needs_hud_redraw = 1;
        }
        if (ev.down && bet > 1) {
            bet--;
            needs_hud_redraw = 1;
        }
        if ((ev.enter || ev.space) && suppress_enter_ticks == 0 && confirm_armed) {
            confirm_armed = 0;
            start_new_round();
        }
    }
}

void draw(void)
{
    /* Render tick: full redraw on major changes, partial redraw for HUD/labels. */
    if (needs_redraw) {
        render_cards();
        needs_redraw = 0;
        needs_hud_redraw = 0;
    } else if (needs_hud_redraw) {
        draw_hud_values();
        draw_hold_frames();
        draw_hold_labels();
        needs_hud_redraw = 0;
    }
}

int main(void)
{
    /* Classic fixed game loop synced to VBlank for stable visual updates. */
    init();

    while (1) {
        update();

        gfx_wait_vblank(&vctx);
        draw();
        gfx_wait_end_vblank(&vctx);
    }

    return 0;
}
