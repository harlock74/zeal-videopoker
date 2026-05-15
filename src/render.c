#include <stdint.h>

#include <zvb_gfx.h>
#include <zgdk.h>
#include <core.h>

#include "main.h"
#include "app_state.h"
#include "assets.h"
#include "audio.h"
#include "render.h"

/* Render/reveal internals owned by this module. */
static uint8_t reveal_mask = 0x1F;
static uint8_t reveal_sfx_pending_mask = 0;
static uint8_t dirty_slots[CARD_COUNT];
static uint8_t full_redraw = 1;

static void clear_overlay_cell(uint8_t x, uint8_t y)
{
    gfx_tilemap_place(&vctx, EMPTY_TILE, CARD_LAYER, x, y);
}

static void clear_overlay_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height)
{
    tilemap_fill(&vctx, CARD_LAYER, EMPTY_TILE, x, y, width, height);
}

void draw_selector_frame(void)
{
    /* Draw/remove the 1-tile cursor border around the selected card only. */
    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        uint8_t show_frame = (state == STATE_HOLD) && (i == selected_hold_slot);
        uint8_t x0 = (uint8_t)(slot_x[i] - 1);
        uint8_t y0 = (uint8_t)(slot_y - 1);
        uint8_t x1 = (uint8_t)(slot_x[i] + SRC_CARD_W);
        uint8_t y1 = (uint8_t)(slot_y + SRC_CARD_H);

        for (uint8_t x = x0; x <= x1; x++) {
            if (show_frame) {
                gfx_tilemap_place(&vctx, SELECTOR_FRAME_TILE, CARD_LAYER, x, y0);
                gfx_tilemap_place(&vctx, SELECTOR_FRAME_TILE, CARD_LAYER, x, y1);
            } else {
                clear_overlay_cell(x, y0);
                clear_overlay_cell(x, y1);
            }
        }

        for (uint8_t y = (uint8_t)(y0 + 1); y < y1; y++) {
            if (show_frame) {
                gfx_tilemap_place(&vctx, SELECTOR_FRAME_TILE, CARD_LAYER, x0, y);
                gfx_tilemap_place(&vctx, SELECTOR_FRAME_TILE, CARD_LAYER, x1, y);
            } else {
                clear_overlay_cell(x0, y);
                clear_overlay_cell(x1, y);
            }
        }
    }
}

static void render_layout(void)
{
    /* Paint the full static background UI from tiled2zeal-generated map data. */
    for (uint8_t y = 0; y < SCREEN_TILE_H; y++) {
        for (uint8_t x = 0; x < SCREEN_TILE_W; x++) {
            gfx_tilemap_place(&vctx, assets_get_layout_tile(x, y), TILEMAP_LAYER, x, y);
            gfx_tilemap_place(&vctx, assets_get_layout_overlay_tile(x, y), UI_LAYER, x, y);
        }
    }
}

void place_tile_grid_at(uint8_t x0, uint8_t y0, const uint8_t grid[SRC_CARD_H][SRC_CARD_W])
{
    for (uint8_t row = 0; row < SRC_CARD_H; row++) {
        for (uint8_t col = 0; col < SRC_CARD_W; col++) {
            gfx_tilemap_place(&vctx, grid[row][col], CARD_LAYER, (uint8_t)(x0 + col), (uint8_t)(y0 + row));
        }
    }
}

static void draw_card_slot_direct(uint8_t slot, uint8_t show_face, uint8_t card)
{
    if (show_face) {
        assets_build_card_tile_grid(scratch_tile_grid, card);
    } else {
        assets_build_back_tile_grid(scratch_tile_grid);
    }
    place_tile_grid_at(slot_x[slot], slot_y, scratch_tile_grid);
}

static void clear_card_slot(uint8_t slot)
{
    clear_overlay_rect(slot_x[slot], slot_y, SRC_CARD_W, SRC_CARD_H);
}

static void clear_bottom_row(void)
{
    /* Clears the action banner/hold row on overlay layer. */
    clear_overlay_rect(2, hold_y, 36, 1);
}

void draw_hold_labels(void)
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
            uint8_t msg_len = (uint8_t)str_len(win_banner_text);
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

void draw_hud_values(void)
{
    /* Always print fixed-width 3 digits so HUD text does not jitter. */
    itoa_pad(bet, hud_num_buf, 10, 'A', '0', 3);
    nprint_string(&vctx, hud_num_buf, 3, bet_x, bet_y);
    itoa_pad(win_amount, hud_num_buf, 10, 'A', '0', 3);
    nprint_string(&vctx, hud_num_buf, 3, win_x, win_y);
    itoa_pad(credits, hud_num_buf, 10, 'A', '0', 3);
    nprint_string(&vctx, hud_num_buf, 3, credit_x, credit_y);
}

void render_mark_all_slots_dirty(void)
{
    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        dirty_slots[i] = 1;
    }
    full_redraw = 1;
}

void render_mark_slot_dirty(uint8_t slot)
{
    if (slot < CARD_COUNT) {
        dirty_slots[slot] = 1;
    }
}

void render_begin_reveal(uint8_t initial_mask)
{
    reveal_mask = initial_mask;
    reveal_sfx_pending_mask = 0;
    /* Mask changes can affect all visible slots. */
    render_mark_all_slots_dirty();
}

void render_reveal_slot(uint8_t slot, uint8_t queue_sfx)
{
    if (slot >= CARD_COUNT) {
        return;
    }
    reveal_mask |= (uint8_t)(1U << slot);
    render_mark_slot_dirty(slot);
    if (queue_sfx) {
        reveal_sfx_pending_mask |= (uint8_t)(1U << slot);
    }
}

void render_table(void)
{
    /* Static background render (called once at init). */
    render_layout();
}

void render_refresh_overlays(void)
{
    draw_hud_values();
    draw_selector_frame();
    draw_hold_labels();
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
    render_refresh_overlays();
}
