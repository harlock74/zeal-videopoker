# zeal-videopoker

Video Poker for Zeal 8-bit Computer.

It runs at **320×240 @8bpp**.

## Controls

- `UP` / `DOWN`: increase/decrease bet (bet phase)
- `A` / `S` / `D` / `F` / `G`: hold/unhold cards 1..5 (hold phase)
- `ENTER` or `SPACE`: deal, draw, continue after winning a poker hand
- `P`: toggle gameplay audio mode
  - `music-only` (default): gameplay tracker music on, card placement SFX muted
  - `card-SFX-only`: gameplay tracker music paused, card placement SFX enabled
- `'`: quit

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

If credits reach `0`, the bottom banner shows:
`CREDIT OVER! PRESS ENTER TO START!`
After `ENTER/SPACE`, the game returns to the splash screen, then resets bankroll
and re-enters the bet phase.

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

## Accreditation

Author: Zingot Games  
License: CC-BY 4.0  
https://opengameart.org/content/bitmap-font-pack  
A few changes to the original assets have been made including the color palette.

Author: (Pixel) Poker Cards  
License: CC-BY 4.0  
https://ivoryred.itch.io/pixel-poker-cards  
A few changes to the original assets have been made including the color palette.
