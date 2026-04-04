#include <stdlib.h>
#include <stdint.h>

#include <zos_sys.h>
#include <zos_time.h>
#include <zos_video.h>
#include <zos_vfs.h>
#include <zos_keyboard.h>
#include <zvb_gfx.h>
#include <zvb_sound.h>
#include <zgdk.h>
#include <core.h>

#include "assets.h"
#include "layout_map.h"
#include "main.h"
#include "audio.h"
#include "splash.h"
#include "gameplay.h"
#include "render.h"

#if defined(CONFIG_VALIDATE) && CONFIG_VALIDATE
#define ZP_VALIDATE 1
#else
#define ZP_VALIDATE 0
#endif

#define CARD_REVEAL_DELAY 4
#define CARD_TILESET_MAX_GID 255
#define FONT_DIGIT_COUNT 10
#define FONT_ALPHA_COUNT 13
#define LAYOUT_SPACE_SAMPLE_X 2

/* Global graphics context used by ZVB drawing APIs. */
gfx_context vctx;

/* Current five cards on the table and working deck state. */
PokerCard cards[CARD_COUNT];
uint8_t deck[DECK_SIZE];
uint8_t deck_pos = 0;

/* High-level game flow: bet -> hold/draw -> result -> bet. */
GameState state = STATE_BET;

/* Persistent player/game values. */
uint16_t credits = INITIAL_CREDITS;
uint8_t bet = 1;
uint16_t win_amount = 0;

/* UI state flags. */
uint8_t show_win_banner = 0;
uint8_t show_card_faces = 0;
static uint8_t needs_redraw = 1;
static uint8_t needs_hud_redraw = 0;
char win_banner_text[36] = "YOU HAVE WON!";

/* Small entropy accumulator mixed into RNG seed values. */
uint16_t entropy = 1;
static uint8_t rng_seeded = 0;
static uint8_t reveal_slots[CARD_COUNT];
static uint8_t reveal_len = 0;
static uint8_t reveal_index = 0;
static uint8_t reveal_cooldown = 0;
static uint8_t reveal_active = 0;
/* Deferred game-over transition to splash/reset, executed safely in update(). */
uint8_t pending_bankrupt_reset = 0;
/* Reusable static scratch buffers to avoid stack-heavy local arrays on SDCC. */
uint16_t scratch_gid_grid[SRC_CARD_H][SRC_CARD_W];
static uint8_t draw_hand_cards_buf[CARD_COUNT];
static uint8_t draw_hand_slots_buf[CARD_COUNT];
char hud_num_buf[6];
uint8_t g_buf[SHARED_SCRATCH_BUF_SIZE];

/* Snapshot of key events for one update tick. */
typedef struct {
    uint8_t up;
    uint8_t down;
    uint8_t enter;
    uint8_t space;
    uint8_t quit;
    uint8_t toggle_audio_mode;
    uint8_t hold_toggle[CARD_COUNT];
} KeyEvents;

/* Debounce timer to avoid Enter/Space double-triggering state transitions. */
static uint8_t suppress_enter_ticks = 0;
/* Requires Enter/Space release before accepting next confirm action. */
static uint8_t confirm_armed = 0;

/* Cached splash background tile (layer 0) for space/fallback characters. */
static uint8_t splash_bg_tile = FONT_SPACE_TILE;
/* Layer1 clear tile (transparent/empty). */
static const uint8_t overlay_empty_tile = FONT_SPACE_TILE;

/* Position of each playable card slot (top-left tile of 3x4 card). */
const uint8_t slot_x[CARD_COUNT] = {5, 12, 19, 26, 33};
const uint8_t slot_y = 21;
static const uint8_t all_slots[CARD_COUNT] = {0, 1, 2, 3, 4};

/* Text anchor per card for "HOLD" labels in the bottom panel. */
const uint8_t hold_x[CARD_COUNT] = {4, 11, 18, 25, 32};
const uint8_t hold_y = 27;

/* HUD numeric fields in tiles. */
const uint8_t bet_x = 6;
const uint8_t bet_y = 17;
const uint8_t win_x = 19;
const uint8_t win_y = 17;
const uint8_t credit_x = 35;
const uint8_t credit_y = 17;

/* Critical source GIDs used outside TMX map rendering. */
static const uint16_t kHoldFrameSourceGid = 120;
static const uint16_t kDigitGidBase = 153;
static const uint16_t kAlphaAGidBase = 163;
static const uint16_t kAlphaNGidBase = 176;
static const uint16_t kColonGid = 189;
static const uint16_t kExclGid = 190;
/* Splash border fiche 2x2 source GIDs (+1 from Tiled local tile IDs). */
static const uint16_t kSplashChipTL = 100; /* Tiled ID 99 */
static const uint16_t kSplashChipTR = 101; /* Tiled ID 100 */
static const uint16_t kSplashChipBL = 138; /* Tiled ID 137 */
static const uint16_t kSplashChipBR = 139; /* Tiled ID 138 */
/* Splash text content and placement. */
static const char kSplashTitle[] = "ZEAL VIDEO POKER";
static const char kSplashPrompt[] = "PRESS ENTER TO PLAY!";
static const uint8_t kSplashTitleY = 10;
static const uint8_t kSplashPromptY = 13;

static void restart_if_credit_low(void);
static void return_to_bet_phase(void);
static void reseed_rng_for_new_hand(void);
static void start_reveal_sequence(const uint8_t* slots, uint8_t len, uint8_t initial_mask);
static void update_reveal_sequence(void);
static void set_win_banner_from_result(const HandResult* result);
static void perform_bankrupt_reset_with_splash(void);
static void clamp_bet_to_credits(void);
#if ZP_VALIDATE
static uint8_t validate_startup_tiles(void);
#endif
static void clear_overlay_layer1(void);
static void draw_splash_prompt(uint8_t visible);
static void draw_splash_chip_block(uint8_t x, uint8_t y, uint8_t chip_tl, uint8_t chip_tr, uint8_t chip_bl, uint8_t chip_br);
static void draw_splash_border(void);
static void render_splash_screen(void);
static void poll_keys(KeyEvents* ev);
static void print_error_u16(const char* prefix, uint16_t value);

/* Translate TMX GID to the runtime tile ID loaded into VRAM. */
uint8_t map_gid_to_tile(uint16_t gid)
{
    if (gid == 0 || gid > CARD_TILESET_MAX_GID) {
        return FONT_SPACE_TILE;
    }
    return (uint8_t)(gid - 1U);
}

#if ZP_VALIDATE
static uint8_t validate_gid_range(uint16_t gid, const char* name)
{
    if (gid == 0 || gid > CARD_TILESET_MAX_GID) {
        put_s("Tile self-check failed: ");
        put_s(name);
        put_s(" GID ");
        put_u16(gid);
        put_s(" out of 1..");
        put_u16(CARD_TILESET_MAX_GID);
        put_c('\n');
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
    uint16_t space_gid = kLayoutGids[(hold_y * LAYOUT_W) + LAYOUT_SPACE_SAMPLE_X];

    if (!validate_gid_range(kHoldFrameSourceGid, "hold_frame")) {
        return 0;
    }
    if (!validate_gid_range(space_gid, "space_bg")) {
        return 0;
    }
    if (!validate_gid_range(kColonGid, "font_colon")) {
        return 0;
    }
    if (!validate_gid_range(kExclGid, "font_excl")) {
        return 0;
    }
    if (!validate_gid_range((uint16_t)(kDigitGidBase + (FONT_DIGIT_COUNT - 1)), "font_digit_9")) {
        return 0;
    }
    if (!validate_gid_range((uint16_t)(kAlphaAGidBase + (FONT_ALPHA_COUNT - 1)), "font_alpha_M")) {
        return 0;
    }
    if (!validate_gid_range((uint16_t)(kAlphaNGidBase + (FONT_ALPHA_COUNT - 1)), "font_alpha_Z")) {
        return 0;
    }

    return 1;
}
#endif

uint8_t map_card_gid_to_tile(uint16_t gid)
{
    return map_gid_to_tile(gid);
}

static void load_ui_font_tiles(void)
{
    /*
     * nprint_string writes to layer 1; use transparent/empty tile for spaces
     * so clearing text does not leave blue artifacts on splash/game screens.
     */
    ascii_map(' ', 1, overlay_empty_tile);
    ascii_map('0', 10, map_gid_to_tile(kDigitGidBase));    // 0-9
    ascii_map('A', 13, map_gid_to_tile(kAlphaAGidBase));   // A-M
    ascii_map('a', 13, map_gid_to_tile(kAlphaAGidBase));   // A-M
    ascii_map('N', 13, map_gid_to_tile(kAlphaNGidBase));   // N-Z
    ascii_map('n', 13, map_gid_to_tile(kAlphaNGidBase));   // N-Z
    ascii_map(':', 1, map_gid_to_tile(kColonGid));
    ascii_map('!', 1, map_gid_to_tile(kExclGid));
}

static void clear_overlay_layer1(void)
{
    /* Clear layer1 (usually has garbage at init / after transitions). */
    tilemap_fill(&vctx, LAYER1, overlay_empty_tile, 0, 0, SCREEN_TILE_W, SCREEN_TILE_H);
}

static uint8_t splash_char_tile(char c)
{
    if (c >= '0' && c <= '9') {
        return (uint8_t)(FONT_DIGIT_TILE + (c - '0'));
    }
    if (c >= 'A' && c <= 'M') {
        return (uint8_t)(FONT_ALPHA_A_TILE + (c - 'A'));
    }
    if (c >= 'N' && c <= 'Z') {
        return (uint8_t)(FONT_ALPHA_N_TILE + (c - 'N'));
    }
    if (c >= 'a' && c <= 'm') {
        return (uint8_t)(FONT_ALPHA_A_TILE + (c - 'a'));
    }
    if (c >= 'n' && c <= 'z') {
        return (uint8_t)(FONT_ALPHA_N_TILE + (c - 'n'));
    }
    if (c == ':') {
        return FONT_COLON_TILE;
    }
    if (c == '!') {
        return FONT_EXCL_TILE;
    }
    return splash_bg_tile;
}

static void draw_splash_text_layer0(const char* text, uint8_t x, uint8_t y)
{
    uint8_t len = (uint8_t)str_len(text);
    for (uint8_t i = 0; i < len; i++) {
        uint8_t tile = splash_char_tile(text[i]);
        gfx_tilemap_place(&vctx, tile, TILEMAP_LAYER, (uint8_t)(x + i), y);
    }
}

static void draw_splash_prompt(uint8_t visible)
{
    uint8_t len = (uint8_t)str_len(kSplashPrompt);
    uint8_t x = (uint8_t)((SCREEN_TILE_W - len) / 2);

    if (visible) {
        draw_splash_text_layer0(kSplashPrompt, x, kSplashPromptY);
        return;
    }

    for (uint8_t i = 0; i < len; i++) {
        gfx_tilemap_place(&vctx, splash_bg_tile, TILEMAP_LAYER, (uint8_t)(x + i), kSplashPromptY);
    }
}

static void draw_splash_chip_block(uint8_t x, uint8_t y, uint8_t chip_tl, uint8_t chip_tr, uint8_t chip_bl, uint8_t chip_br)
{
    gfx_tilemap_place(&vctx, chip_tl, TILEMAP_LAYER, x, y);
    gfx_tilemap_place(&vctx, chip_tr, TILEMAP_LAYER, (uint8_t)(x + 1), y);
    gfx_tilemap_place(&vctx, chip_bl, TILEMAP_LAYER, x, (uint8_t)(y + 1));
    gfx_tilemap_place(&vctx, chip_br, TILEMAP_LAYER, (uint8_t)(x + 1), (uint8_t)(y + 1));
}

static void draw_splash_border(void)
{
    const uint8_t border_off_x = 2;
    const uint8_t border_off_y = 2;
    const uint8_t x_first = border_off_x;
    const uint8_t y_first = border_off_y;
    const uint8_t x_last = (uint8_t)(SCREEN_TILE_W - border_off_x - 2);
    /* Bottom row is one tile lower than before, as requested. */
    const uint8_t y_last = (uint8_t)(SCREEN_TILE_H - border_off_y - 2);
    const uint8_t chip_tl = map_gid_to_tile(kSplashChipTL);
    const uint8_t chip_tr = map_gid_to_tile(kSplashChipTR);
    const uint8_t chip_bl = map_gid_to_tile(kSplashChipBL);
    const uint8_t chip_br = map_gid_to_tile(kSplashChipBR);

    for (uint8_t x = x_first; x <= x_last; x = (uint8_t)(x + 2)) {
        draw_splash_chip_block(x, y_first, chip_tl, chip_tr, chip_bl, chip_br);
        draw_splash_chip_block(x, y_last, chip_tl, chip_tr, chip_bl, chip_br);
    }

    for (uint8_t y = y_first; y <= (uint8_t)(y_last - 2); y = (uint8_t)(y + 2)) {
        draw_splash_chip_block(x_first, y, chip_tl, chip_tr, chip_bl, chip_br);
        draw_splash_chip_block(x_last, y, chip_tl, chip_tr, chip_bl, chip_br);
    }
}

static void render_splash_screen(void)
{
    static const uint8_t showcase_cards[5] = {
        9,  /* 10 of hearts */
        10, /* J of hearts */
        11, /* Q of hearts */
        12, /* K of hearts */
        0   /* A of hearts */
    };
    static const uint8_t showcase_x[5] = {7, 12, 18, 24, 30};
    static const uint8_t showcase_y = 18;
    uint16_t space_gid = kLayoutGids[(hold_y * LAYOUT_W) + LAYOUT_SPACE_SAMPLE_X];
    uint8_t bg_tile = map_gid_to_tile(space_gid);
    uint8_t title_x = (uint8_t)((SCREEN_TILE_W - (uint8_t)str_len(kSplashTitle)) / 2);
    splash_bg_tile = bg_tile;

    clear_overlay_layer1();

    for (uint8_t y = 0; y < SCREEN_TILE_H; y++) {
        for (uint8_t x = 0; x < SCREEN_TILE_W; x++) {
            gfx_tilemap_place(&vctx, bg_tile, TILEMAP_LAYER, x, y);
        }
    }

    draw_splash_text_layer0(kSplashTitle, title_x, kSplashTitleY);
    draw_splash_prompt(1);

    /* Showcase hand centered in splash screen. */
    for (uint8_t i = 0; i < 5; i++) {
        assets_build_card_gid_grid(scratch_gid_grid, showcase_cards[i]);
        place_gid_grid_at(showcase_x[i], showcase_y, scratch_gid_grid);
    }

    /* Draw border last so splash cards/text cannot overwrite chips. */
    draw_splash_border();
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

    if (combo == NULL) {
        str_cpy(win_banner_text, "YOU HAVE WON!");
        return;
    }

    itoa(result->multiplier, g_buf, 10, 'A');
    str_cpy(win_banner_text, combo);
    str_cat(win_banner_text, " X");
    str_cat(win_banner_text, g_buf);
    str_cat(win_banner_text, ": YOU HAVE WON!");
}

void deal_hand(void)
{
    /* Deal five fresh cards and move to hold selection phase. */
    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        cards[i].card = pop_deck();
        cards[i].held = false;
        render_mark_slot_dirty(i);
    }

    show_card_faces = 1;
    state = STATE_HOLD;
    start_reveal_sequence(all_slots, CARD_COUNT, 0);
    needs_redraw = 1;
}

void draw_hand(void)
{
    HandResult result;
    uint8_t slot_count = 0;
    uint8_t keep_mask = 0;

    /* Replace only non-held cards. Held cards remain unchanged. */
    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        if (!cards[i].held) {
            cards[i].card = pop_deck();
            draw_hand_slots_buf[slot_count++] = i;
            render_mark_slot_dirty(i);
        } else {
            keep_mask |= (uint8_t)(1U << i);
        }
        draw_hand_cards_buf[i] = cards[i].card;
    }

    /* Score result and credit winnings (multiplier * current bet). */
    result = evaluate_hand(draw_hand_cards_buf);
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
    start_reveal_sequence(draw_hand_slots_buf, slot_count, keep_mask);
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

    if (credits == 0 || credits < bet) {
        clamp_bet_to_credits();
        needs_hud_redraw = 1;
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
    render_begin_reveal(initial_mask);
    reveal_len = (len > CARD_COUNT) ? CARD_COUNT : len;
    reveal_index = 0;
    reveal_cooldown = 0;
    reveal_active = (reveal_len > 0);

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
    render_reveal_slot(slot, 1);

    reveal_index++;
    needs_redraw = 1;

    if (reveal_index >= reveal_len) {
        reveal_active = 0;
    } else {
        reveal_cooldown = CARD_REVEAL_DELAY;
    }
}

static void restart_if_credit_low(void)
{
    /*
     * Credit exhausted: show explicit banner and wait for Enter/Space before
     * returning to splash. Keep the current hand visible meanwhile.
     */
    if (credits > 0) {
        return;
    }

    str_cpy(win_banner_text, "CREDIT OVER! PRESS ENTER TO START!");
    show_win_banner = 1;
    needs_hud_redraw = 1;
    pending_bankrupt_reset = 1;
}

static void clamp_bet_to_credits(void)
{
    /*
     * Keep bet valid for bankroll and configured limits:
     * 1 <= bet <= MAX_BET and bet <= credits when credits > 0.
     */
    if (credits == 0) {
        bet = 1;
        return;
    }

    if (bet < 1) {
        bet = 1;
    }
    if (bet > MAX_BET) {
        bet = MAX_BET;
    }
    if (bet > credits) {
        bet = (uint8_t)credits;
    }
}

static void perform_bankrupt_reset_with_splash(void)
{
    render_splash_screen();
    splash_run_blocking(draw_splash_prompt);
    render_table();
    start_game_music();

    credits = RESET_CREDITS;
    bet = 1;
    win_amount = 0;
    show_win_banner = 0;
    show_card_faces = 0;
    reveal_active = 0;
    start_reveal_sequence(all_slots, CARD_COUNT, 0);
    suppress_enter_ticks = 8;
    confirm_armed = 0;

    shuffle_deck();
    state = STATE_BET;
    render_cards();
    needs_redraw = 0;
    needs_hud_redraw = 0;
    pending_bankrupt_reset = 0;
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
    start_reveal_sequence(all_slots, CARD_COUNT, 0);
    state = STATE_BET;
    clamp_bet_to_credits();
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
    uint8_t* buf = g_buf;
    uint8_t released = 0;

    mem_set(ev, 0, sizeof(*ev));

    while (1) {
        uint16_t size = 8;
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
                case KB_KEY_P: ev->toggle_audio_mode = 1; break;
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
    /* Initialize input, graphics mode, assets, then block on splash screen. */
    zos_err_t err = input_init(true);
    if (err != ERR_SUCCESS) {
        print_error_u16("Input init failed: ", err);
        exit(1);
    }

    gfx_enable_screen(0);

    err = gfx_initialize(ZVB_CTRL_VID_MODE_GFX_640_8BIT, &vctx);
    if (err != ERR_SUCCESS) {
        print_error_u16("GFX init failed: ", err);
        exit(1);
    }

    clear_overlay_layer1();

    err = load_cards_palette(&vctx);
    if (err != GFX_SUCCESS) {
        print_error_u16("Palette load failed: ", err);
        exit(1);
    }

    sound_init();
    audio_init_tracks();

    err = load_cards_tileset(&vctx);
    if (err != GFX_SUCCESS) {
        print_error_u16("Tileset load failed: ", err);
        exit(1);
    }
#if ZP_VALIDATE
    if (!validate_startup_tiles()) {
        put_s("Critical tile validation failed. Check cards.gif/cards.tsx/cards.tmx.\n");
        exit(1);
    }
    if (assets_validate_card_tables(CARD_TILESET_MAX_GID) != GFX_SUCCESS) {
        put_s("Card composition tile validation failed. Check cards.gif tile mappings.\n");
        exit(1);
    }
#endif
    load_ui_font_tiles();
    render_splash_screen();
    gfx_enable_screen(1);
    splash_run_blocking(draw_splash_prompt);
    start_game_music();

    render_table();

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
}

void deinit(void)
{
    /* Restore text screen before exiting back to shell/system. */
    stop_current_music();
    sound_stop_all();
    sound_deinit();
    ioctl(DEV_STDOUT, CMD_RESET_SCREEN, NULL);
}

static void print_error_u16(const char* prefix, uint16_t value)
{
    put_s(prefix);
    put_u16(value);
    put_c('\n');
}

void update(void)
{
    /* Game logic tick: input/state transitions only (no VRAM drawing here). */
    KeyEvents ev;
    sound_loop();
    tick_current_music();
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

    if (pending_bankrupt_reset) {
        /*
         * Game-over confirmation gate:
         * only Enter/Space moves back to splash screen and restarts bankroll.
         */
        if ((ev.enter || ev.space) && suppress_enter_ticks == 0 && confirm_armed) {
            confirm_armed = 0;
            perform_bankrupt_reset_with_splash();
        }
        return;
    }

    if (ev.toggle_audio_mode) {
        audio_toggle_game_audio_mode();
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
            if (!pending_bankrupt_reset && show_win_banner && (ev.up || ev.down || ev.enter || ev.space)) {
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
        if ((ev.enter || ev.space) && suppress_enter_ticks == 0 && confirm_armed && credits >= bet) {
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
        render_refresh_overlays();
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
}
