#!/usr/bin/env python3
"""Import Mario GP scoreboard and result-screen art directly from the US ROM."""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import struct
import sys
import tempfile

TOOLS = pathlib.Path(__file__).parent
sys.path.insert(0, str(TOOLS))
import build_asset_pack


MAGIC = 0x31525047
TILE_BYTES = 0x4000
PALETTE_BYTES = 0x200
DIGIT_OFFSET = 0xEE67E
DIGIT_COUNT = 20
HEADER_OFFSETS = (0xDB7A8, 0xDB798)
ROW_TABLE = 0xDB7B8
ROW_COUNT = 4
ROW_BYTES = 32
# Five-lap boards (sub_8022004 with more than three laps): L1..L5 and total.
COMPACT_ROW_TABLE = 0xDB7C8
COMPACT_ROW_COUNT = 6
# sub_8020770's 2123C time board map (Quick Run and VS).
TIME_BOARD_MAP_OFFSET = 0x9B4CC
MAP_BYTES = 0x800
FOLLOWUP_MAP_OFFSET = 0x9BE28
FOLLOWUP_MAP_BYTES = 0x500
SCROLL_MAP_OFFSET = 0x9BD7C
SCROLL_MAP_BYTES = 0x300
RECORD_PATTERN_TABLE = 0xDB7E0
RECORD_PATTERN_COUNT = 20
RECORD_PATTERN_CELLS = 9
OBJECT_TILES_OFFSET = 0x9B8D4
OBJECT_TILE_BYTES = 0x800
OBJECT_PALETTE_OFFSET = 0x9B8C0
OBJECT_PALETTE_BYTES = 0x14
MENU_PALETTE_OFFSET = 0xAAD7C
MENU_PALETTE_BYTES = 0x20
MENU_PULSE_BASE_PALETTE_OFFSET = 0xAAD3C
MENU_PULSE_BASE_PALETTE_BYTES = 0x20
SIN_TABLE_OFFSET = 0xE645C
SIN_TABLE_COUNT = 1024
MENU_PULSE_FACTOR_COUNT = 64
RANK_TILE_OFFSET = 0x6590C
RANK_COUNT = 8
RANK_TILE_BYTES = 0x160
RANK_PALETTE_ROM_OFFSET = 0x64C6C
FOLLOWUP_TILE_RANGES = (
    (0x0000, 0xAAE3C, 0x0800),
    (0x0800, 0xAB7FC, 0x01C0),
    (0x1000, 0x63834, 0x0400),
    (0x1400, 0xACAFC, 0x0C00),
    (0x2000, 0xABEFC, 0x0800),
)
FOLLOWUP_PALETTE_RANGES = (
    (0x180, 0x9B8A0, 0x20),
    (0x1A0, 0x627D4, 0x20),
    (0x1C0, 0xAACF0, 0x40),
)
ROM_RANGES = (
    (0x0000, 0x0E00, 0x62834),
    (0x0E00, 0x0E80, 0x6F53C),
    (0x0F00, 0x1400, 0x63734),
    (0x1400, 0x1700, 0x655AC),
    (0x1800, 0x1B00, 0x6524C),
    (0x1E00, 0x2000, 0x64634),
    (0x2800, 0x2A00, 0x66C0C),
)


def pattern(rom: bytes, offset: int) -> bytes:
    if offset + 4 > len(rom):
        raise ValueError("scoreboard pattern header outside ROM")
    width, height = struct.unpack_from("<HH", rom, offset)
    size = 4 + width * height * 2
    if width == 0 or height == 0 or offset + size > len(rom):
        raise ValueError("invalid scoreboard pattern")
    return rom[offset:offset + size]


def decode_lz77(source: bytes, offset: int) -> bytes:
    if offset + 4 > len(source) or source[offset] != 0x10:
        raise ValueError("invalid GP followup LZ77 header")
    size = int.from_bytes(source[offset + 1:offset + 4], "little")
    cursor = offset + 4
    out = bytearray()
    while len(out) < size:
        if cursor >= len(source):
            raise ValueError("truncated GP followup LZ77 flags")
        flags = source[cursor]
        cursor += 1
        for bit in range(7, -1, -1):
            if len(out) >= size:
                break
            if flags & (1 << bit):
                if cursor + 2 > len(source):
                    raise ValueError("truncated GP followup LZ77 match")
                pair = (source[cursor] << 8) | source[cursor + 1]
                cursor += 2
                length = (pair >> 12) + 3
                distance = (pair & 0xFFF) + 1
                if distance > len(out):
                    raise ValueError("invalid GP followup LZ77 distance")
                for _ in range(length):
                    out.append(out[-distance])
                    if len(out) == size:
                        break
            else:
                if cursor >= len(source):
                    raise ValueError("truncated GP followup LZ77 literal")
                out.append(source[cursor])
                cursor += 1
    return bytes(out)


def payload(rom: bytes) -> bytes:
    if len(rom) < 0xF0000:
        raise ValueError("ROM is too small for GP result art")
    # sub_804EC30 installs the shared HUD glyphs and three palette banks.
    # Subsequent result uploads replace the ranges listed above. Dynamic HUD
    # tiles/maps are intentionally absent: times, coins and rank come from
    # native race state, never from a captured race.
    tiles = bytearray(TILE_BYTES)
    tiles[:0x2000] = rom[0x62834:0x64834]
    for start, end, source in ROM_RANGES:
        tiles[start:end] = rom[source:source + end-start]
    palette = bytearray(PALETTE_BYTES)
    palette[0x140:0x200] = rom[0x62774:0x62834]
    headers = [pattern(rom, offset) for offset in HEADER_OFFSETS]
    if any(len(value) != 16 for value in headers):
        raise ValueError("wrong scoreboard header pattern size")
    rows = []
    lengths = []
    for index in range(ROW_COUNT):
        pointer = struct.unpack_from("<I", rom, ROW_TABLE + index * 4)[0]
        if not 0x08000000 <= pointer < 0x09000000:
            raise ValueError("invalid scoreboard row pointer")
        value = pattern(rom, pointer - 0x08000000)
        if len(value) > ROW_BYTES:
            raise ValueError("scoreboard row pattern too large")
        lengths.append(len(value))
        rows.append(value.ljust(ROW_BYTES, b"\0"))
    compact_rows = []
    compact_lengths = []
    for index in range(COMPACT_ROW_COUNT):
        pointer = struct.unpack_from("<I", rom, COMPACT_ROW_TABLE + index * 4)[0]
        if not 0x08000000 <= pointer < 0x09000000:
            raise ValueError("invalid scoreboard row pointer")
        value = pattern(rom, pointer - 0x08000000)
        if len(value) > ROW_BYTES:
            raise ValueError("scoreboard row pattern too large")
        compact_lengths.append(len(value))
        compact_rows.append(value.ljust(ROW_BYTES, b"\0"))
    time_board_map = decode_lz77(rom, TIME_BOARD_MAP_OFFSET)
    if len(time_board_map) != MAP_BYTES:
        raise ValueError("wrong time board map size")
    digits = rom[DIGIT_OFFSET:DIGIT_OFFSET + DIGIT_COUNT * 2]
    if len(digits) != DIGIT_COUNT * 2:
        raise ValueError("scoreboard digit table outside ROM")
    background_map = bytes(MAP_BYTES)
    rank_tiles = rom[RANK_TILE_OFFSET:RANK_TILE_OFFSET +
                     RANK_COUNT * RANK_TILE_BYTES]
    if len(rank_tiles) != RANK_COUNT * RANK_TILE_BYTES:
        raise ValueError("GP rank tiles outside ROM")
    rank_palette = bytearray(palette[0x180:0x1a0])
    rank_palette[0x12:0x1a] = rom[
        RANK_PALETTE_ROM_OFFSET:RANK_PALETTE_ROM_OFFSET + 8]
    if len(rank_palette) != 32:
        raise ValueError("GP rank palette outside ROM")
    followup_tiles = bytearray(tiles)
    for destination, source, size in FOLLOWUP_TILE_RANGES:
        value = rom[source:source + size]
        if len(value) != size:
            raise ValueError("GP followup tile range outside ROM")
        followup_tiles[destination:destination + size] = value
    followup_palette = bytearray(palette[:PALETTE_BYTES])
    for destination, source, size in FOLLOWUP_PALETTE_RANGES:
        value = rom[source:source + size]
        if len(value) != size:
            raise ValueError("GP followup palette range outside ROM")
        followup_palette[destination:destination + size] = value
    followup_map = decode_lz77(rom, FOLLOWUP_MAP_OFFSET)
    scroll_map = decode_lz77(rom, SCROLL_MAP_OFFSET)
    if len(followup_map) != FOLLOWUP_MAP_BYTES or len(scroll_map) != SCROLL_MAP_BYTES:
        raise ValueError("wrong GP followup map size")
    record_patterns = bytearray()
    for index in range(RECORD_PATTERN_COUNT):
        pointer = struct.unpack_from("<I", rom,
                                     RECORD_PATTERN_TABLE + index * 4)[0]
        if not 0x08000000 <= pointer < 0x09000000:
            raise ValueError("invalid GP record-pattern pointer")
        offset = pointer - 0x08000000
        value = rom[offset:offset + RECORD_PATTERN_CELLS * 2]
        if len(value) != RECORD_PATTERN_CELLS * 2:
            raise ValueError("GP record pattern outside ROM")
        record_patterns += value
    object_tiles = decode_lz77(rom, OBJECT_TILES_OFFSET)
    object_palette = rom[OBJECT_PALETTE_OFFSET:
                         OBJECT_PALETTE_OFFSET + OBJECT_PALETTE_BYTES]
    menu_palette = rom[MENU_PALETTE_OFFSET:
                       MENU_PALETTE_OFFSET + MENU_PALETTE_BYTES]
    menu_pulse_base_palette = rom[
        MENU_PULSE_BASE_PALETTE_OFFSET:
        MENU_PULSE_BASE_PALETTE_OFFSET + MENU_PULSE_BASE_PALETTE_BYTES]
    if SIN_TABLE_OFFSET + SIN_TABLE_COUNT * 4 > len(rom):
        raise ValueError("GP results sine table outside ROM")
    sine = struct.unpack_from(f"<{SIN_TABLE_COUNT}i", rom, SIN_TABLE_OFFSET)
    pulse_factors = []
    for phase in range(MENU_PULSE_FACTOR_COUNT):
        signed_phase = phase if phase < 32 else phase - 64
        index = (256 - signed_phase * 16) & 0x3ff
        value = 0x8000 - sine[index]
        pulse_factors.append((value + (1 if value < 0 else 0)) >> 1)
    if len(object_tiles) != OBJECT_TILE_BYTES or \
            len(object_palette) != OBJECT_PALETTE_BYTES or \
            len(menu_palette) != MENU_PALETTE_BYTES:
        raise ValueError("wrong GP results object asset size")
    if len(menu_pulse_base_palette) != MENU_PULSE_BASE_PALETTE_BYTES:
        raise ValueError("wrong GP results pulse palette size")
    header = struct.pack("<16I", MAGIC, TILE_BYTES, PALETTE_BYTES // 2,
                         DIGIT_COUNT, len(headers), len(rows),
                         MAP_BYTES // 2, FOLLOWUP_MAP_BYTES // 2,
                         SCROLL_MAP_BYTES // 2, RECORD_PATTERN_COUNT,
                         OBJECT_TILE_BYTES, OBJECT_PALETTE_BYTES // 2,
                         MENU_PALETTE_BYTES // 2,
                         MENU_PULSE_BASE_PALETTE_BYTES // 2,
                         MENU_PULSE_FACTOR_COUNT, RANK_COUNT)
    record_values = struct.unpack_from("<8I", rom, 0xD97B0)
    if any(value > 7 for value in record_values):
        raise ValueError("invalid record identity mapping")
    record_identities = bytes(record_values)
    menu_option_codes = rom[0xDB6E0:0xDB6E5]
    if len(menu_option_codes)!=5 or any(value>4 for value in menu_option_codes):
        raise ValueError("invalid results menu option mapping")
    return (header + tiles + palette + digits +
            b"".join(headers) + struct.pack("<4H", *lengths) +
            b"".join(rows) + background_map + followup_tiles +
            followup_palette + followup_map + scroll_map + record_patterns +
            object_tiles + object_palette + menu_palette +
            menu_pulse_base_palette +
            struct.pack("<64H", *pulse_factors) + rank_tiles + rank_palette +
            struct.pack("<6H", *compact_lengths) + b"".join(compact_rows) +
            time_board_map + record_identities + menu_option_codes)


def build(rom_path: pathlib.Path, output: pathlib.Path):
    if output.exists():
        raise ValueError(f"output already exists: {output}")
    rom = rom_path.read_bytes()
    if hashlib.sha1(rom).hexdigest() != "9d327c030c3e2d9007990518594f70c3340ac56f":
        raise ValueError("GP result import requires an unmodified US ROM")
    data = payload(rom)
    version = 8
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="gp-results-", dir=output.parent) as tmp:
        root = pathlib.Path(tmp)
        (root / "gp-results.bin").write_bytes(data)
        (root / "manifest.json").write_text(json.dumps({"sections": [{
            "kind": 22, "content_id": 1, "version": version, "flags": 3,
            "semantic_revision": version, "path": "gp-results.bin"}]},
            separators=(",", ":")) + "\n")
        pack_digest, deterministic = build_asset_pack.build_pack(
            root / "manifest.json", output)
    return hashlib.sha256(data).hexdigest(), pack_digest, deterministic


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    try:
        payload_digest, pack_digest, deterministic = build(
            args.rom, args.output)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f"payload_sha256={payload_digest}")
    print(f"pack_sha256={pack_digest}")
    print(f"deterministic_sha256={deterministic}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
