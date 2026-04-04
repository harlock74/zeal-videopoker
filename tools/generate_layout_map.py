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
    out.append("extern const uint16_t kLayoutGids[LAYOUT_W * LAYOUT_H];")
    out.append("")
    return "\n".join(out)


def generate_source(gids):
    out = []
    out.append('#include "layout_map.h"')
    out.append("")
    out.append("const uint16_t kLayoutGids[LAYOUT_W * LAYOUT_H] = {")
    for i in range(0, len(gids), 20):
        row = ", ".join(str(v) for v in gids[i:i + 20])
        out.append(f"    {row},")
    out.append("};")
    out.append("")
    return "\n".join(out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tmx", required=True)
    parser.add_argument("--out", help="Header output path (legacy; same as --out-header)")
    parser.add_argument("--out-header", help="Header output path")
    parser.add_argument("--out-source", help="Source output path")
    args = parser.parse_args()

    tmx_path = Path(args.tmx)
    header_path = Path(args.out_header or args.out) if (args.out_header or args.out) else None
    if header_path is None:
        raise ValueError("Missing output path: use --out-header (or legacy --out)")

    source_path = Path(args.out_source) if args.out_source else header_path.with_suffix(".c")

    tmx_text = tmx_path.read_text(encoding="utf-8")
    width, height = parse_map_size(tmx_text)
    gids = parse_csv_tiles(tmx_text)

    expected = width * height
    if len(gids) != expected:
        raise ValueError(f"TMX tile count mismatch: got {len(gids)}, expected {expected}")

    header_path.write_text(generate_header(width, height, gids), encoding="utf-8")
    source_path.write_text(generate_source(gids), encoding="utf-8")
    print(f"Wrote {header_path}")
    print(f"Wrote {source_path}")


if __name__ == "__main__":
    main()
