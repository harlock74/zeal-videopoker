#!/usr/bin/env python3
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"

TILE = 16
COLS = 13
ROWS = 8

RANKS = ["A", "2", "3", "4", "5", "6", "7", "8", "9", "T", "J", "Q", "K"]
SUITS = ["S", "H", "D", "C"]  # Spades, Hearts, Diamonds, Clubs
RED_SUITS = {"H", "D"}
HUD_CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:+-"


def draw_diamond(draw, cx, cy, c):
    draw.polygon([(cx, cy - 3), (cx + 3, cy), (cx, cy + 3), (cx - 3, cy)], fill=c)


def draw_heart(draw, cx, cy, c):
    draw.ellipse((cx - 4, cy - 4, cx, cy), fill=c)
    draw.ellipse((cx, cy - 4, cx + 4, cy), fill=c)
    draw.polygon([(cx - 4, cy - 1), (cx + 4, cy - 1), (cx, cy + 5)], fill=c)


def draw_club(draw, cx, cy, c):
    draw.ellipse((cx - 4, cy - 4, cx, cy), fill=c)
    draw.ellipse((cx, cy - 4, cx + 4, cy), fill=c)
    draw.ellipse((cx - 2, cy - 7, cx + 2, cy - 3), fill=c)
    draw.rectangle((cx - 1, cy, cx + 1, cy + 4), fill=c)


def draw_spade(draw, cx, cy, c):
    draw.polygon([(cx, cy - 6), (cx + 5, cy + 1), (cx - 5, cy + 1)], fill=c)
    draw.ellipse((cx - 4, cy - 2, cx, cy + 2), fill=c)
    draw.ellipse((cx, cy - 2, cx + 4, cy + 2), fill=c)
    draw.rectangle((cx - 1, cy + 1, cx + 1, cy + 5), fill=c)


def draw_suit(draw, suit, cx, cy, color):
    if suit == "S":
        draw_spade(draw, cx, cy, color)
    elif suit == "H":
        draw_heart(draw, cx, cy, color)
    elif suit == "D":
        draw_diamond(draw, cx, cy, color)
    else:
        draw_club(draw, cx, cy, color)


def main():
    img = Image.new("RGB", (COLS * TILE, ROWS * TILE), (0, 120, 0))
    draw = ImageDraw.Draw(img)
    font = ImageFont.load_default()

    for suit_index, suit in enumerate(SUITS):
        for rank_index, rank in enumerate(RANKS):
            x0 = rank_index * TILE
            y0 = suit_index * TILE

            is_red = suit in RED_SUITS
            color = (208, 16, 16) if is_red else (8, 8, 8)

            draw.rectangle((x0 + 1, y0 + 1, x0 + TILE - 2, y0 + TILE - 2), fill=(244, 244, 238), outline=(16, 16, 16))

            draw.text((x0 + 3, y0 + 1), rank, fill=color, font=font)
            draw_suit(draw, suit, x0 + 8, y0 + 9, color)

            draw.text((x0 + 10, y0 + 9), rank, fill=color, font=font)

    # Draw HUD font glyphs after the 52 card tiles.
    # Tile order maps directly to HUD_CHARS for code-side lookup.
    glyph_start = len(RANKS) * len(SUITS)
    for i, ch in enumerate(HUD_CHARS):
        tile = glyph_start + i
        x0 = (tile % COLS) * TILE
        y0 = (tile // COLS) * TILE

        draw.rectangle((x0, y0, x0 + TILE - 1, y0 + TILE - 1), fill=(220, 222, 214))
        draw.rectangle((x0 + 1, y0 + 1, x0 + TILE - 2, y0 + TILE - 2), outline=(188, 192, 180))

        if ch != " ":
            draw.text((x0 + 4, y0 + 4), ch, fill=(20, 20, 20), font=font)

    png_path = ASSETS / "cards.png"
    gif_path = ASSETS / "cards.gif"

    img.save(png_path)
    img.save(gif_path, format="GIF", optimize=False)

    print(f"Wrote {png_path}")
    print(f"Wrote {gif_path}")


if __name__ == "__main__":
    main()
