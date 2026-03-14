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
#include <zgdk.h>

#include "assets.h"
#include "layout_map.h"
#include "videopoker.h"

/* Global graphics context used by ZVB drawing APIs. */
gfx_context vctx;

/* Current five cards on the table and working deck state. */
static PokerCard cards[CARD_COUNT];
static uint8_t deck[DECK_SIZE];
static uint8_t deck_pos = 0;
/* If credits drop below this, game resets bankroll to this value. */
static const uint16_t MIN_CREDIT_RESTART = 15;

/* High-level game flow: bet -> hold/draw -> result -> bet. */
static GameState state = STATE_BET;

/* Persistent player/game values. */
static uint16_t credits = 15;
static uint8_t bet = 1;
static uint16_t win_amount = 0;

/* UI state flags. */
static uint8_t show_win_banner = 0;
static uint8_t show_card_faces = 0;
static uint8_t needs_redraw = 1;
static uint8_t needs_hud_redraw = 0;

/* Small entropy accumulator mixed into RNG seed values. */
static uint16_t entropy = 1;
static uint8_t rng_seeded = 0;

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

/* Map GID -> runtime tile ID remap generated from layout_map.h usage. */
static uint16_t mapped_gids[MAP_TILE_CAPACITY];
static uint8_t mapped_tiles[MAP_TILE_CAPACITY];
static uint8_t mapped_count = 0;

/* Position of each playable card slot (top-left tile of 3x4 card). */
static const uint8_t slot_x[CARD_COUNT] = {5, 12, 19, 26, 33};
static const uint8_t slot_y = 21;

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

static void restart_if_credit_low(void);
static void return_to_bet_phase(void);
static void reseed_rng_for_new_hand(void);

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

static void load_ui_font_tiles(void)
{
    /* Frame tile used to highlight held cards */
    load_source_tile(&vctx, 1007, (uint16_t)HOLD_FRAME_TILE * TILE_SIZE);

    /* Space/background tile used to clear text areas cleanly. */
    load_source_tile(&vctx, 981, (uint16_t)FONT_SPACE_TILE * TILE_SIZE);

    /* Digits and A-Z are loaded from your font area in the tileset. */
    for (uint8_t i = 0; i < 10; i++) {
        load_source_tile(&vctx, (uint16_t)(1181 + i), (uint16_t)(FONT_DIGIT_TILE + i) * TILE_SIZE);
    }

    for (uint8_t i = 0; i < 13; i++) {
        load_source_tile(&vctx, (uint16_t)(1191 + i), (uint16_t)(FONT_ALPHA_A_TILE + i) * TILE_SIZE);
    }

    for (uint8_t i = 0; i < 13; i++) {
        load_source_tile(&vctx, (uint16_t)(1204 + i), (uint16_t)(FONT_ALPHA_N_TILE + i) * TILE_SIZE);
    }

    ascii_map(' ', 1, FONT_SPACE_TILE);
    ascii_map('0', 10, 48); // 0-9
    ascii_map('A', 13, 64); // A-M
    ascii_map('a', 13, 64); // A-M
    ascii_map('N', 13, 80); // N-Z
    ascii_map('n', 13, 80); // N-Z
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
        uint8_t x0 = (uint8_t)(slot_x[i] - 1);
        uint8_t y0 = (uint8_t)(slot_y - 1);
        uint8_t x1 = (uint8_t)(slot_x[i] + SRC_CARD_W);
        uint8_t y1 = (uint8_t)(slot_y + SRC_CARD_H);

        for (uint8_t x = x0; x <= x1; x++) {
            if (cards[i].held) {
                gfx_tilemap_place(&vctx, HOLD_FRAME_TILE, TILEMAP_LAYER, x, y0);
                gfx_tilemap_place(&vctx, HOLD_FRAME_TILE, TILEMAP_LAYER, x, y1);
            } else {
                restore_map_cell(x, y0);
                restore_map_cell(x, y1);
            }
        }

        for (uint8_t y = (uint8_t)(y0 + 1); y < y1; y++) {
            if (cards[i].held) {
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

static void load_card_tiles_to_slot(uint8_t slot, uint8_t card)
{
    /* Each card is 3x4 tiles and each slot has its own destination tile block. */
    uint8_t dst_base_tile = (uint8_t)(DST_TILE_BASE + (slot * (SRC_CARD_W * SRC_CARD_H)));
    load_card_tiles(&vctx, card, (uint16_t)dst_base_tile * TILE_SIZE);
}

static void load_back_tiles_to_slot(uint8_t slot)
{
    /* Fixed 3x4 red-back card from your tileset (GIDs from cards.tmx layout). */
    static const uint16_t back_gids[SRC_CARD_H][SRC_CARD_W] = {
        {945, 946, 947},
        {1004, 1005, 1006},
        {1063, 1064, 1065},
        {1122, 1123, 1124},
    };
    uint8_t dst_base_tile = (uint8_t)(DST_TILE_BASE + (slot * (SRC_CARD_W * SRC_CARD_H)));

    for (uint8_t row = 0; row < SRC_CARD_H; row++) {
        for (uint8_t col = 0; col < SRC_CARD_W; col++) {
            uint8_t dst_tile = (uint8_t)(dst_base_tile + (row * SRC_CARD_W) + col);
            load_source_tile(&vctx, back_gids[row][col], (uint16_t)dst_tile * TILE_SIZE);
        }
    }
}

static void place_card_slot(uint8_t slot)
{
    /* Blit the 3x4 tile block for one card to its table position. */
    uint8_t x0 = slot_x[slot];
    uint8_t dst_base_tile = (uint8_t)(DST_TILE_BASE + (slot * (SRC_CARD_W * SRC_CARD_H)));

    for (uint8_t row = 0; row < SRC_CARD_H; row++) {
        for (uint8_t col = 0; col < SRC_CARD_W; col++) {
            uint8_t tile = (uint8_t)(dst_base_tile + (row * SRC_CARD_W) + col);
            gfx_tilemap_place(&vctx, tile, TILEMAP_LAYER, (uint8_t)(x0 + col), (uint8_t)(slot_y + row));
        }
    }
}

static void restore_hold_background(uint8_t slot)
{
    /* Keep helper in case per-slot text clears are needed again. */
    uint8_t bg = map_gid_to_tile(981);
    for (uint8_t col = 0; col < 4; col++) {
        uint8_t x = (uint8_t)(hold_x[slot] + col);
        gfx_tilemap_place(&vctx, bg, TILEMAP_LAYER, x, hold_y);
    }
}

static void clear_bottom_row(void)
{
    /* Clears the action banner/hold row on tilemap layer 0. */
    uint8_t bg = map_gid_to_tile(981);
    for (uint8_t x = 2; x < 38; x++) {
        gfx_tilemap_place(&vctx, bg, TILEMAP_LAYER, x, hold_y);
    }
}

static void clear_hud_field(uint8_t x, uint8_t y, uint8_t width)
{
    /* Clears one numeric HUD field before printing a new value. */
    uint8_t bg = map_gid_to_tile(981);
    for (uint8_t i = 0; i < width; i++) {
        gfx_tilemap_place(&vctx, bg, TILEMAP_LAYER, (uint8_t)(x + i), y);
    }
}

static void draw_hold_labels(void)
{
    static const char hold_text[] = "HOLD";
    static const char won_text[] = "YOU HAVE WON!";
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
            nprint_string(&vctx, won_text, 13, 13, hold_y);
        } else if (state == STATE_BET) {
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
    uint8_t bg = map_gid_to_tile(981);

    /* Always print fixed-width 3 digits so HUD text does not jitter. */
    sprintf(bet_buf, "%03u ", bet);
    sprintf(win_buf, "%03u ", win_amount);
    sprintf(credit_buf, "%03u ", credits);

    clear_hud_field(bet_x, bet_y, 4);
    clear_hud_field(win_x, win_y, 4);
    clear_hud_field(credit_x, credit_y, 4);
    gfx_tilemap_place(&vctx, bg, TILEMAP_LAYER, (uint8_t)(credit_x - 1), credit_y);

    nprint_string(&vctx, bet_buf, 4, bet_x, bet_y);
    nprint_string(&vctx, win_buf, 4, win_x, win_y);
    nprint_string(&vctx, credit_buf, 4, credit_x, credit_y);
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
        HandResult result = {5, "STRAIT"};
        return result;
    }
    if (trips) {
        HandResult result = {4, "3KIND"};
        return result;
    }
    if (pairs == 2) {
        HandResult result = {3, "2PAIR"};
        return result;
    }

    if (pairs == 1) {
        HandResult result = {2, "PAIR"};
        return result;
    }

    HandResult result = {0, "NOWIN"};
    return result;
}

void render_table(void)
{
    /* Static background render (called once at init). */
    render_layout();
}

void render_cards(void)
{
    /* Draw either face cards or red backs depending on phase. */
    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        if (show_card_faces) {
            load_card_tiles_to_slot(i, cards[i].card);
        } else {
            load_back_tiles_to_slot(i);
        }
        place_card_slot(i);
    }

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
    }

    show_card_faces = 1;
    state = STATE_HOLD;
    needs_redraw = 1;
}

void draw_hand(void)
{
    HandResult result;
    uint8_t hand[CARD_COUNT];

    /* Replace only non-held cards. Held cards remain unchanged. */
    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        if (!cards[i].held) {
            cards[i].card = pop_deck();
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
    state = STATE_RESULT;
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

static void restart_if_credit_low(void)
{
    /* Auto-reset bankroll when dropping below minimum threshold. */
    if (credits >= MIN_CREDIT_RESTART) {
        return;
    }

    credits = MIN_CREDIT_RESTART;
    bet = 1;
    win_amount = 0;
    show_win_banner = 0;
    show_card_faces = 0;
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

    init_layout_tiles();
    load_ui_font_tiles();
    render_table();

    seed_rng_from_time();
    shuffle_deck();
    show_card_faces = 0;
    state = STATE_BET;
    render_cards();
    needs_redraw = 0;
    needs_hud_redraw = 0;

    gfx_enable_screen(1);
}

void deinit(void)
{
    /* Restore text screen before exiting back to shell/system. */
    ioctl(DEV_STDOUT, CMD_RESET_SCREEN, NULL);
}

void update(void)
{
    /* Game logic tick: input/state transitions only (no VRAM drawing here). */
    KeyEvents ev;
    poll_keys(&ev);
    entropy++;
    if (suppress_enter_ticks > 0) {
        suppress_enter_ticks--;
    }

    if (ev.quit) {
        deinit();
        exit(0);
    }

    if (state == STATE_HOLD) {
        /* Toggle holds with A/S/D/F/G; Enter performs draw. */
        for (uint8_t i = 0; i < CARD_COUNT; i++) {
            if (ev.hold_toggle[i]) {
                cards[i].held ^= 1;
                needs_hud_redraw = 1;
            }
        }
        if (ev.enter && suppress_enter_ticks == 0) {
            draw_hand();
        }
        return;
    }

    if (state == STATE_RESULT) {
        /* RESULT waits for confirmation before returning to BET phase. */
        if (show_win_banner && (ev.up || ev.down || ev.enter || ev.space)) {
            show_win_banner = 0;
            needs_hud_redraw = 1;
        }
        if ((ev.enter || ev.space) && suppress_enter_ticks == 0) {
            return_to_bet_phase();
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
        if ((ev.enter || ev.space) && suppress_enter_ticks == 0) {
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
