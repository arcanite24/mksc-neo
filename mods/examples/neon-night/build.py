#!/usr/bin/env python3
"""Build a local visual mod from the owner's imported pack; distribute no game data."""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/portable'))
from mod_kit import default_mods_directory
SPEC = importlib.util.spec_from_file_location("pack_builder", ROOT / "tools/portable/build_asset_pack.py")
BUILDER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILDER)


sections = BUILDER.read_sections

def prism(size):
    """Original indexed, tile-ordered 4bpp art, at each native sprite LOD."""
    out = bytearray(size * size // 2)
    for y in range(size):
        for x in range(size):
            dx, dy = abs(2 * x - size + 1), abs(2 * y - size + 1)
            color = 0
            if dx + dy < size * 3 // 4:
                color = 5 if dx + dy > size * 3 // 4 - max(2, size // 8) else (3 if x < size // 2 else 4)
            if size * 5 // 8 < y < size * 7 // 8 and size // 5 < x < size * 4 // 5:
                color = 6 if x < size // 3 or x > size * 2 // 3 else color
            offset = ((y // 8) * (size // 8) + x // 8) * 32 + (y % 8) * 4 + (x % 8) // 2
            out[offset] |= color << (4 * (x & 1))
    return out


def build(base, output, driver=1):
    output.mkdir(parents=True, exist_ok=True)
    entries = []
    driver_found = False
    for meta, data in sections(base):
        if meta["kind"] == 8:
            magic, tiles, colors = struct.unpack_from("<III", data)
            if magic != 0x31444152 or len(data) != 24 + tiles + colors * 2:
                raise ValueError("unsupported road graphics")
            for i in range(colors):
                color = struct.unpack_from("<H", data, 24 + tiles + i * 2)[0]
                r, g, b = color & 31, (color >> 5) & 31, (color >> 10) & 31
                # Dark violet road and cyan highlights, preserving distinct shades.
                struct.pack_into("<H", data, 24 + tiles + i * 2,
                                 min(31, 5 + r // 2) | (g // 3 << 5) | (min(31, 8 + b // 2) << 10))
        elif meta["kind"] == 10 and meta["content_id"] == driver:
            magic, tile_bytes, colors, width, height, _, frames, slots = struct.unpack_from("<8I", data)
            if magic != 0x33524143 or meta["version"] != 3 or (width, height) != (64, 64):
                raise ValueError("reimport the base pack for CAR3 driver art")
            start = 32 + slots
            if tile_bytes != frames * 2048 or len(data) != start + tile_bytes + 25 * (512 + 128) + colors * 2:
                raise ValueError("invalid driver frame layout")
            data[start:start + tile_bytes] = prism(64) * frames
            start += tile_bytes
            data[start:start + 25 * 512] = prism(32) * 25
            start += 25 * 512
            data[start:start + 25 * 128] = prism(16) * 25
            start += 25 * 128
            palette = [0, 0, 0x4210, 0x7FE0, 0x7C1F, 0x7FFF, 0x1808] + [0x7C1F] * 9
            for i in range(colors):
                struct.pack_into("<H", data, start + i * 2, palette[i % 16])
            driver_found = True
        else:
            continue
        name = f'{meta["kind"]}-{meta["content_id"]}.bin'
        (output / name).write_bytes(data)
        entries.append(dict(meta, path=name))
    if not driver_found or not any(e["kind"] == 8 for e in entries):
        raise ValueError("base pack lacks the requested driver or road graphics")
    (output / "pack.json").write_text(json.dumps({"sections": entries}, indent=2) + "\n")
    (output / "mod.json").write_bytes(Path(__file__).with_name("mod.json").read_bytes())
    BUILDER.build_pack(output / "pack.json", output / "content.pack")
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--driver", type=int, default=1)
    args = parser.parse_args()
    if args.output_dir is None:
        args.output_dir = default_mods_directory() / 'neon-night'
    entries = build(args.base, args.output_dir, args.driver)
    print(f"Built neon-night: {len(entries)} sections in {args.output_dir}")


if __name__ == "__main__":
    main()
