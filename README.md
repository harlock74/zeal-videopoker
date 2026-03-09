# zeal-videopoker

Video Poker for Zeal 8-bit Computer.

## Controls

- `UP` / `DOWN`: change bet on bet screen
- `A`: toggle hold on selected card
- `B` or `START`: deal / draw / continue
- `SELECT`: quit

## Assets used

- `assets/1.2 Poker cards_modified.gif`: source file you added
- `assets/cards_modified.gif`: single source used to generate shared `.ztp/.zts` palette+tiles
- `assets/cards.tmx`: layout source used for placeholder coordinates and decorative tiles

## Rendering model

- Each dealt card is rendered as `3x4` tiles.
- Five card slots are mapped to the 5 placeholder regions from `cards.tmx`.
- Fisher-Yates shuffling is used on a 52-card deck each deal.
- Palette and tiles are streamed at runtime from `assets/cards_modified.ztp/.zts` in small chunks with `gfx_palette_load` / `gfx_tileset_load`.

## Accreditation
Author: Zingot Games
Licence: CC-BY 4.0
https://opengameart.org/content/bitmap-font-pack
www.zingot.com
A few changes to the original assets have been made including the color palette.

Author: (Pixel) Poker Cards
Licence: CC-BY 4.0
https://ivoryred.itch.io/pixel-poker-cards
A few changes to the original assets have been made including the color palette.
