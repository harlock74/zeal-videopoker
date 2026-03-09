#!/usr/bin/env python3
import argparse
import re
from pathlib import Path


def parse_csv_tiles(tmx_text: str):
    m = re.search(r"<data encoding=\"csv\">(.*?)</data>", tmx_text, re.S)
    if not m:
        raise ValueError("Could not find CSV <data> section in TMX file.")
    values = re.findall(r"\b\d+\b", m.group(1))
    return [int(v) for v in values]


def parse_map_size(tmx_text: str):
    m = re.search(r"<map[^>]*\bwidth=\"(\d+)\"[^>]*\bheight=\"(\d+)\"", tmx_text)
    if not m:
        raise ValueError("Could not read width/height from <map> tag.")
    return int(m.group(1)), int(m.group(2))


def generate_header(width: int, height: int, gids):
    out = []
    out.append("#pragma once")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("")
    out.append(f"#define LAYOUT_W {width}")
    out.append(f"#define LAYOUT_H {height}")
    out.append("")
    out.append("static const uint16_t kLayoutGids[LAYOUT_W * LAYOUT_H] = {")
    for i in range(0, len(gids), 20):
        row = ", ".join(str(v) for v in gids[i:i + 20])
        out.append(f"    {row},")
    out.append("};")
    out.append("")
    return "\n".join(out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tmx", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    tmx_path = Path(args.tmx)
    out_path = Path(args.out)

    tmx_text = tmx_path.read_text(encoding="utf-8")
    width, height = parse_map_size(tmx_text)
    gids = parse_csv_tiles(tmx_text)

    expected = width * height
    if len(gids) != expected:
        raise ValueError(f"TMX tile count mismatch: got {len(gids)}, expected {expected}")

    out_path.write_text(generate_header(width, height, gids), encoding="utf-8")
    print(f"Wrote {out_path}")


if __name__ == "__main__":
    main()
