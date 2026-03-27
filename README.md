# zeal-videopoker

Video Poker for Zeal 8-bit Computer.

## Game Flow

1. **Bet phase (`DEAL`)**
   - Adjust bet with `UP` / `DOWN`.
   - Press `ENTER` or `SPACE` to start the hand.
   - Confirm actions are release-gated (fresh press required) to avoid accidental auto-deal.
   - Selected bet is subtracted from credits.
2. **Hold phase (`DRAW`)**
   - Five cards are dealt from a shuffled 52-card deck.
   - Toggle hold with `A/S/D/F/G` (cards 1 to 5).
   - Held cards are marked with `HOLD` and a visual frame.
   - Press `ENTER` to draw replacements.
   - Confirm actions are release-gated (fresh press required) to avoid accidental auto-draw.
3. **Result phase**
   - Only non-held cards are replaced.
   - Final hand is evaluated against the pay table.
   - `WIN` is computed as `multiplier * bet`.
   - `CREDIT` is updated and the banner shows the exact winning combo:
     - Example: `STRAIGHT X4: YOU HAVE WON!`
4. **Back to bet**
   - Press `ENTER` or `SPACE` to continue.
   - Card backs are shown again, ready for next hand.

If credits reach `0`, the game returns to the splash screen, waits for `ENTER/SPACE`,
then resets bankroll and re-enters the bet phase.

## Controls

- `UP` / `DOWN`: increase/decrease bet (bet phase)
- `A` / `S` / `D` / `F` / `G`: hold/unhold cards 1..5 (hold phase)
- `ENTER` or `SPACE`: deal, draw, continue
- `'`: quit

## Hand Ranking and Payout

- `250`: Royal Flush
- `50`: Straight Flush
- `25`: Four of a Kind
- `9`: Full House
- `6`: Flush
- `4`: Straight
- `3`: Three of a Kind
- `2`: Two Pair
- `1`: Pair

## Code Structure

Main gameplay is in `src/videopoker.c`.

- `init()`: input/video init, asset loading, map/font setup, startup checks, shared card tile init
- `validate_startup_tiles()`: validates critical UI/font/back GIDs at startup
- `init_card_component_tiles()`: preloads unique reusable card component GIDs once into VRAM shared pool (`184..255`)
- `verify_card_component_coverage()`: exhaustively checks that all GIDs used by all 52 face grids + back grid are preloaded
- `update()`: state machine and controls (`BET`, `HOLD`, `RESULT`)
- `draw()`: full or partial redraw synchronized with VBlank
- `start_new_round()`: deduct bet, reseed RNG, shuffle, deal
- `clamp_bet_to_credits()`: keeps bet valid (`1..MAX_BET` and never above credits)
- `deal_hand()`: initial 5-card deal
- `draw_hand()`: replace non-held cards, evaluate result, apply payout
- `evaluate_hand()`: rank detection and multiplier selection
- `render_layout()` / `render_cards()`: map/card/HUD/banner rendering
- `draw_card_slot_direct()`: draws one 3x4 card by mapping a GID grid directly to shared runtime tiles
- `map_card_gid_to_tile()`: shared GID -> runtime tile mapper with safe fallback diagnostics
- `shuffle_deck()` / `pop_deck()`: deck management (Fisher-Yates shuffle)
- `start_reveal_sequence()` / `update_reveal_sequence()`: one-by-one card reveal timing
- `play_card_place_sound()`: per-card SFX trigger during reveals
- `set_win_banner_from_result()`: builds combo-specific win banner text
- `mark_slot_dirty()` / `mark_all_slots_dirty()`: incremental slot redraw control

Supporting files:

- `src/assets.c` / `src/assets.h`: palette/tile loading helpers + runtime card compositor helpers
- `src/layout_map.h`: generated TMX map header
- `src/videopoker.h`: gameplay and rendering constants
- `CARD_TILE_MAPPING.md`: single mapping reference for `cards.gif` positions and runtime GIDs

## Rendering Notes

- Video mode: `ZVB_CTRL_VID_MODE_GFX_640_8BIT`
- Each card is rendered as `3x4` tiles
- UI layout is read from `cards.tmx` and generated into `layout_map.h`
- Number card layouts (A..10) are table-driven via `kPipMaskByRank`
- Pip/face placement uses PDF-style named positions (`TOP_LEFT`, `MIDDLE1_CENTRE`, etc.)
- Suit color is explicit via `kSuitColorBySuit[]` (no implicit suit-order assumption)
- Cards are not uploaded per-slot anymore: card components are shared in VRAM and tilemap-composed per draw

## Audio Implementation

The game uses ZGDK/ZVB sound APIs and plays a short SFX every time a card is
placed (both card backs and face cards) during reveal animations.

Audio lifecycle in code:

- `sound_init()` in `init()`
- `sound_loop()` once per frame in `update()`
- `sound_play(...)` in `play_card_place_sound()`
- `sound_stop_all()` + `sound_deinit()` in `deinit()`

Audio sync fix:

- Reveal logic marks a per-slot pending SFX flag.
- The SFX is triggered in `render_cards()` exactly when the corresponding slot is actually placed.
- This keeps audio and visual card placement aligned on real hardware timing.

Current hardware-tuned defaults:

- `CARD_SFX_WAVEFORM = WAV_SAWTOOTH`
- `CARD_SFX_BASE_FREQ = 10`
- `CARD_SFX_JITTER_MASK = 0x03`
- `CARD_SFX_DURATION = 1`
- `CARD_REVEAL_DELAY = 4`

## Performance Optimizations (Real Hardware)

1. **Shared card-component tile pool**
   - Unique card component GIDs are preloaded once at startup.
   - During gameplay, cards are drawn by tilemap placement only.
   - Eliminates repeated per-slot 12-tile uploads.

2. **Per-slot dirty rendering**
   - `dirty_slots[]` + `full_redraw` allow incremental card redraw.
   - Only changed slots are processed during reveal/deal/draw transitions.

3. **Persistent asset stream reuse (`assets.c`)**
   - `cards.zts` stream is reused and cursor-tracked.
   - Avoids repeated open/close/seek overhead for source tile loading.

## Safety Guards

- Startup tile self-check validates:
  - critical UI/font/back GIDs are in valid atlas range
  - map-derived background tile is present in runtime remap
- Card compositor tables are validated at startup:
  - suit tiles
  - red/black rank glyph strips
  - J/Q/K face fragment tables
  - white card tile and back-card matrix
- Shared-card preload coverage is validated at startup:
  - all 52 generated face card grids
  - back-card grid
  - startup aborts if any required GID is missing from `card_gid_to_runtime[]`
- Bet underflow guard:
  - round start is blocked unless `credits >= bet`
  - bet is clamped when returning to BET phase, preventing `uint16_t` wraparound

## Splash Screen Notes

- Splash is blocking (`ENTER/SPACE` to continue).
- Border chips use dedicated splash GIDs and are drawn last to avoid overwrite artifacts.
- On game-over (`credits == 0`), splash is shown again before bankroll reset.
- Layer1 (text overlay) is explicitly cleared at init and at splash render time.
- Space character mapping for `nprint_string()` uses an empty overlay tile, so text clears do not leave blue bars/garbage tiles.
- Previous per-field splash text clear helper was removed as redundant after full Layer1 clear.

## Accreditation

Author: Zingot Games  
License: CC-BY 4.0  
https://opengameart.org/content/bitmap-font-pack  
A few changes to the original assets have been made including the color palette.

Author: (Pixel) Poker Cards  
License: CC-BY 4.0  
https://ivoryred.itch.io/pixel-poker-cards  
A few changes to the original assets have been made including the color palette.
