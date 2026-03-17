# zeal-videopoker

Video Poker for Zeal 8-bit Computer.

## Game Flow

1. **Bet phase (`DEAL`)**
   - Adjust bet with `UP` / `DOWN`.
   - Press `ENTER` or `SPACE` to start the hand.
   - Selected bet is subtracted from credits.
2. **Hold phase (`DRAW`)**
   - Five cards are dealt from a shuffled 52-card deck.
   - Toggle hold with `A/S/D/F/G` (cards 1 to 5).
   - Held cards are marked with `HOLD` and a visual frame.
   - Press `ENTER` to draw replacements.
3. **Result phase**
   - Only non-held cards are replaced.
   - Final hand is evaluated against the pay table.
   - `WIN` is computed as `multiplier * bet`.
   - `CREDIT` is updated and the banner shows the exact winning combo:
     - Example: `STRAIGHT X4: YOU HAVE WON!`
4. **Back to bet**
   - Press `ENTER` or `SPACE` to continue.
   - Card backs are shown again, ready for next hand.

If credits reach `0`, the game resets bankroll and returns to the bet phase.

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

- `init()`: input/video init, asset loading, map/font setup, first screen
- `update()`: state machine and controls (`BET`, `HOLD`, `RESULT`)
- `draw()`: full or partial redraw synchronized with VBlank
- `start_new_round()`: deduct bet, reseed RNG, shuffle, deal
- `deal_hand()`: initial 5-card deal
- `draw_hand()`: replace non-held cards, evaluate result, apply payout
- `evaluate_hand()`: rank detection and multiplier selection
- `render_layout()` / `render_cards()`: map/card/HUD/banner rendering
- `shuffle_deck()` / `pop_deck()`: deck management (Fisher-Yates shuffle)
- `start_reveal_sequence()` / `update_reveal_sequence()`: one-by-one card reveal timing
- `play_card_place_sound()`: per-card SFX trigger during reveals
- `set_win_banner_from_result()`: builds combo-specific win banner text
- `ensure_slot_tiles()`: uploads slot graphics only when card/back content changes
- `mark_slot_dirty()` / `mark_all_slots_dirty()`: incremental slot redraw control

Supporting files:

- `src/assets.c` / `src/assets.h`: palette/tile loading helpers
- `src/layout_map.h`: generated TMX map header
- `src/videopoker.h`: gameplay and rendering constants

## Rendering Notes

- Video mode: `ZVB_CTRL_VID_MODE_GFX_640_8BIT`
- Each card is rendered as `3x4` tiles
- UI layout is read from `cards.tmx` and generated into `layout_map.h`

## Audio Implementation

The game uses ZGDK/ZVB sound APIs and plays a short SFX every time a card is
placed (both card backs and face cards) during reveal animations.

Audio lifecycle in code:

- `sound_init()` in `init()`
- `sound_loop()` once per frame in `update()`
- `sound_play(...)` in `play_card_place_sound()`
- `sound_stop_all()` + `sound_deinit()` in `deinit()`

Audio sync fix:

- Reveal logic now marks a per-slot pending SFX flag.
- The SFX is triggered in `render_cards()` exactly when the corresponding slot is actually placed.
- This keeps audio and visual card placement aligned on real hardware timing.

Current hardware-tuned defaults:

- `CARD_SFX_WAVEFORM = WAV_SAWTOOTH`
- `CARD_SFX_BASE_FREQ = 148`
- `CARD_SFX_JITTER_MASK = 0x03`
- `CARD_SFX_DURATION = 1`
- `CARD_REVEAL_DELAY = 4`

### Current tunable audio parameters

Defined near the top of `src/videopoker.c`:

- `CARD_SFX_WAVEFORM`: waveform (`WAV_SQUARE`, `WAV_TRIANGLE`, `WAV_SAWTOOTH`, `WAV_NOISE`)
- `CARD_SFX_BASE_FREQ`: base pitch/frequency passed to `sound_play`
- `CARD_SFX_JITTER_MASK`: random variation mask for humanized repeated taps
- `CARD_SFX_DURATION`: sound length
- `CARD_REVEAL_DELAY`: timing between consecutive cards in reveal sequence

In practice:

- Lower `CARD_SFX_BASE_FREQ` and slightly longer `CARD_SFX_DURATION` feel more like a soft desk tap.
- Higher `CARD_REVEAL_DELAY` makes dealing slower and more deliberate.
- Larger `CARD_SFX_JITTER_MASK` adds more variation between card hits.

## Performance Optimizations (Real Hardware)

The game now includes three targeted optimizations for Zeal hardware:

1. **Slot visual caching (Step 1)**
   - `SlotVisualCache` tracks what is already loaded in each card slot.
   - `ensure_slot_tiles()` prevents redundant re-upload of unchanged face/back graphics.

2. **Per-slot dirty rendering (Step 2)**
   - `dirty_slots[]` + `full_redraw` allow incremental card redraw.
   - Only changed slots are processed during reveal/deal/draw transitions.

3. **Persistent asset stream reuse (Step 3)**
   - `assets.c` keeps `cards.zts` stream open and tracks current offset.
   - This avoids repeated `open/close/seek` overhead for each tile load.
   - `assets_shutdown()` closes the stream in `deinit()`.

## Accreditation

Author: Zingot Games  
License: CC-BY 4.0  
https://opengameart.org/content/bitmap-font-pack  
A few changes to the original assets have been made including the color palette.

Author: (Pixel) Poker Cards  
License: CC-BY 4.0  
https://ivoryred.itch.io/pixel-poker-cards  
A few changes to the original assets have been made including the color palette.
