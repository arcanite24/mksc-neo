#!/usr/bin/env python3
"""Build source-owned Mario GP standings assets for one MKSC course."""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import struct


MAGIC = 0x31534750
HEADER_SIZE = 32
TILE_BYTES = 0x3000
PALETTE_COUNT = 256
MAP_COUNT = 1024
DRIVER_COUNT = 8
DRIVER_CELLS = 18
FOOTER_CELLS = 9
CUPS_OFFSET = 0xE7464
TRACK_TABLE_OFFSET = 0xE7FEC
CUP_GRAPHICS_TABLE = 0xDB254
DRIVER_PATTERN_TABLE = 0xDB3DC
DRIVER_DISPLAY_TABLE = 0xD97B0
STATIC_TILE_RANGES = (
    (0x1400, 0xACAFC, 0x0C00),
    (0x2000, 0xABEFC, 0x0800),
    (0x0000, 0x9AA2C, 0x0400),
    # 1DAA4 uploads the portrait bank last: its border tiles A0..A3
    # replace the first four tiles of the name bank at 0x1400.
    (0x1000, 0x9AE2C, 0x0480),
)


def decode_lz77(source: bytes, offset: int) -> bytes:
    if offset + 4 > len(source) or source[offset] != 0x10:
        raise ValueError(f"invalid LZ77 header at 0x{offset:x}")
    size = int.from_bytes(source[offset + 1:offset + 4], "little")
    cursor = offset + 4
    output = bytearray()
    while len(output) < size:
        if cursor >= len(source):
            raise ValueError("truncated LZ77 flags")
        flags = source[cursor]
        cursor += 1
        for bit in range(7, -1, -1):
            if len(output) >= size:
                break
            if flags & (1 << bit):
                if cursor + 2 > len(source):
                    raise ValueError("truncated LZ77 match")
                pair = (source[cursor] << 8) | source[cursor + 1]
                cursor += 2
                count = (pair >> 12) + 3
                distance = (pair & 0xFFF) + 1
                if distance > len(output):
                    raise ValueError("invalid LZ77 distance")
                for _ in range(count):
                    output.append(output[-distance])
                    if len(output) == size:
                        break
            else:
                if cursor >= len(source):
                    raise ValueError("truncated LZ77 literal")
                output.append(source[cursor])
                cursor += 1
    return bytes(output)


def pointer(rom: bytes, offset: int) -> int:
    if offset + 4 > len(rom):
        raise ValueError("pointer outside ROM")
    value = struct.unpack_from("<I", rom, offset)[0]
    if not 0x08000000 <= value < 0x08000000 + len(rom):
        raise ValueError(f"invalid ROM pointer 0x{value:08x}")
    return value - 0x08000000


def payload(rom: bytes, course_id: int, source_header: int, track_set: int = 0) -> bytes:
    # Track sets: 0 GBA cups, 1 Extra, 2 the Battle arena table.
    if not 0 <= source_header < (4 if track_set == 2 else 20) or course_id <= 0 or track_set not in (0, 1, 2):
        raise ValueError("unsupported course identity")
    if len(rom) < TRACK_TABLE_OFFSET + 24 * 4:
        raise ValueError("ROM is too small")
    track_index = struct.unpack_from("<I", rom,
                                     (CUPS_OFFSET, 0xE74C4, 0xE7524)[track_set] + source_header * 4)[0]
    track_def = pointer(rom, TRACK_TABLE_OFFSET + track_index * 4)
    track_name = decode_lz77(rom, pointer(rom, track_def + 0x30))
    cup_index = source_header // 4
    cup_name = decode_lz77(rom, pointer(
        rom, CUP_GRAPHICS_TABLE + cup_index * 4))
    if len(track_name) > 0x400 or len(cup_name) > 0x800:
        raise ValueError("standings course/cup graphics exceed source slots")

    tiles = bytearray(TILE_BYTES)
    for destination, source, size in STATIC_TILE_RANGES:
        data = rom[source:source + size]
        if len(data) != size:
            raise ValueError("standings tile range outside ROM")
        tiles[destination:destination + size] = data
    tiles[0x400:0x400 + len(track_name)] = track_name
    tiles[0x800:0x800 + len(cup_name)] = cup_name

    palette = bytearray(PALETTE_COUNT * 2)
    palette[0x180:0x200] = rom[0x9A9AC:0x9AA2C]
    palette[0x1C0:0x200] = rom[0xAACF0:0xAAD30]
    if len(palette) != PALETTE_COUNT * 2:
        raise ValueError("standings palette range outside ROM")

    background = bytearray(decode_lz77(rom, 0x9B2AC))
    if len(background) != MAP_COUNT * 2:
        raise ValueError("wrong standings background map size")
    for column in range(3):
        source_x = cup_index * 3 + column
        for source_y, destination_y in ((28, 1), (29, 2)):
            source = (source_y * 32 + source_x) * 2
            destination = (destination_y * 32 + 15 + column) * 2
            background[destination:destination + 2] = \
                background[source:source + 2]

    driver_cells = bytearray()
    display = struct.unpack_from("<8I", rom, DRIVER_DISPLAY_TABLE)
    if sorted(display) != list(range(DRIVER_COUNT)):
        raise ValueError("driver display table is not a permutation")
    for driver_class in range(DRIVER_COUNT):
        display_index = display[driver_class]
        for half in range(2):
            name = pointer(rom, DRIVER_PATTERN_TABLE +
                           (display_index * 2 + half) * 4)
            cells = rom[name:name + (DRIVER_CELLS // 2) * 2]
            if len(cells) != (DRIVER_CELLS // 2) * 2:
                raise ValueError("driver pattern outside ROM")
            driver_cells += cells
    footer = pointer(rom, DRIVER_PATTERN_TABLE + DRIVER_COUNT * 2 * 4)
    footer_cells = rom[footer:footer + FOOTER_CELLS * 2]
    if len(footer_cells) != FOOTER_CELLS * 2:
        raise ValueError("standings footer pattern outside ROM")

    header = struct.pack(
        "<8I", MAGIC, TILE_BYTES, PALETTE_COUNT, MAP_COUNT,
        DRIVER_COUNT, DRIVER_CELLS, course_id, source_header)
    return header + tiles + palette + background + driver_cells + footer_cells


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--course-id", type=int, default=24)
    parser.add_argument("--source-header", type=int, default=4)
    args = parser.parse_args()
    try:
        data = payload(args.rom.read_bytes(), args.course_id,
                       args.source_header)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(data)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(json.dumps({"bytes": len(data),
                      "sha256": hashlib.sha256(data).hexdigest(),
                      "course_id": args.course_id,
                      "source_header": args.source_header},
                     sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
