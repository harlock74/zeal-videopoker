[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)

# zeal-videopoker

zeal-videopoker is a Video Poker game for the Zeal 8-bit Computer.

It runs at **640×480 @8bpp**.

zeal-videopoker contains sound effects and background music composed with Zeal Music Track (ZMT). There is one dedicated music track for the splash screen and one dedicated music track for gameplay. Feel free to create your own tracks and include them in the game. In `/alternative music tracks/gameplay tracks` you will find "Vivaldi" and "Beethoven" style alternatives to get you started.

## Screenshots

| Splash Screen  | Gameplay |
| - | - |
| ![image](https://github.com/harlock74/zeal-videopoker/blob/main/docs/screenshots/1.png) | ![image](https://github.com/harlock74/zeal-videopoker/blob/main/docs/screenshots/2.png) |

## Controls

- `UP` / `DOWN`: increase/decrease bet (bet phase)
- `LEFT` / `RIGHT`: move between cards during hold selection
- `SPACE` / `Z` or SNES `B`: deal, draw, continue after winning a poker hand
- `X` or SNES `A`: hold/unhold the selected card
- `ENTER` or SNES `START`: start/restart the game
- `S` or SNES `X`: toggle gameplay audio mode
  - `music-only` (default): gameplay tracker music on, card placement SFX muted
  - `card-SFX-only`: gameplay tracker music paused, card placement SFX enabled
- `R SHIFT` or SNES `SELECT`: quit

## Game Flow

1. **Bet phase (`DEAL`)**
   - Adjust bet with `UP` / `DOWN`.
   - Press `SPACE` / `Z` or SNES `B` to deal.
   - Selected bet is subtracted from credits.
2. **Hold phase (`DRAW`)**
   - Five cards are dealt from a shuffled 52-card deck.
   - Move the selection with `LEFT` / `RIGHT`.
   - Toggle hold on the selected card with `X` or SNES `A`.
   - Held cards are marked with `HOLD`; the frame is only the current selector.
   - Press `SPACE` / `Z` or SNES `B` to draw replacements.
3. **Result phase**
   - Only non-held cards are replaced.
   - Final hand is evaluated against the pay table.
   - `WIN` is computed as `multiplier * bet`.
   - `CREDIT` is updated and the banner shows the exact winning combo:
     - Example: `STRAIGHT X4: YOU HAVE WON!`
4. **Back to bet**
   - Press `SPACE` / `Z` or SNES `B` to continue.
   - Card backs are shown again, ready for next hand.
5. **Rewards phase**
   - You may be rewarded with a well-deserved surprise as you progress through the game, even if you are not too lucky with poker hands. If you do win a few hands, rewards may appear sooner.

If credits reach `0`, the bottom banner shows:
`CREDIT OVER! PRESS ENTER TO START!`
After `ENTER` or SNES `START`, the game returns to the splash screen, then resets bankroll
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

## Assets

The main larger cards were made by me. Feel free to create your own cards by editing the provided `cards.aseprite`. All you need to do is change the tiles used for each suit pip. You can also be creative with the Jack, Queen, and King tiles. Have fun if you find my cards boring at some point.

## Attribution

Author: Zingot Games  
License: CC-BY 4.0  
https://opengameart.org/content/bitmap-font-pack  
A few changes to the original assets have been made including the color palette.

Author: (Pixel) Poker Cards  
License: CC-BY 4.0  
https://ivoryred.itch.io/pixel-poker-cards  
A few changes to the original assets have been made including the color palette.
