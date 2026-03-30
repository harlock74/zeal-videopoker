#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <zvb_gfx.h>
#include <zgdk.h>

#include "layout_map.h"
#include "main.h"
#include "app_state.h"
#include "assets.h"
#include "audio.h"
#include "render.h"

/* Restore one map cell from original TMX layout (used to erase overlays). */
static void restore_map_cell(uint8_t x, uint8_t y)
{
    uint16_t gid = kLayoutGids[(y * LAYOUT_W) + x];
    gfx_tilemap_place(&vctx, map_gid_to_tile(gid), TILEMAP_LAYER, x, y);
}

void draw_hold_frames(void)
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

void init_layout_tiles(void)
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

void place_gid_grid_at(uint8_t x0, uint8_t y0, const uint16_t grid[SRC_CARD_H][SRC_CARD_W])
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
    if (show_face) {
        assets_build_card_gid_grid(scratch_gid_grid, card);
    } else {
        assets_build_back_gid_grid(scratch_gid_grid);
    }
    place_gid_grid_at(slot_x[slot], slot_y, scratch_gid_grid);
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

void draw_hud_values(void)
{
    clear_hud_field(bet_x, bet_y, 4);
    clear_hud_field(win_x, win_y, 4);
    clear_hud_field(credit_x, credit_y, 4);
    restore_map_cell((uint8_t)(credit_x - 1), credit_y);

    /* Always print fixed-width 3 digits so HUD text does not jitter. */
    sprintf(hud_num_buf, "%03u", bet);
    nprint_string(&vctx, hud_num_buf, 3, bet_x, bet_y);
    sprintf(hud_num_buf, "%03u", win_amount);
    nprint_string(&vctx, hud_num_buf, 3, win_x, win_y);
    sprintf(hud_num_buf, "%03u", credits);
    nprint_string(&vctx, hud_num_buf, 3, credit_x, credit_y);
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
