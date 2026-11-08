#!/usr/bin/env python3
"""Build deterministic source-backed graphics/manifests for all eight drivers."""

import argparse
import importlib.util
import json
import pathlib
import struct
import tempfile

_PACK_SCRIPT = pathlib.Path(__file__).with_name("build_asset_pack.py")
_PACK_SPEC = importlib.util.spec_from_file_location(
    "build_asset_pack", _PACK_SCRIPT)
build_asset_pack = importlib.util.module_from_spec(_PACK_SPEC)
_PACK_SPEC.loader.exec_module(build_asset_pack)


CHARACTER_COUNT = 8
# `_080D9790`, consumed by `spm_menuCharIdxToCharId`: gameplay/handling
# order (Mario..Bowser) to characterTiles.bin storage order.
GRAPHICS_SOURCE_IDS = (0, 1, 3, 6, 7, 4, 5, 2)
BLOCK_BYTES = 0x16000
FRAME_BYTES = 0x800
FRAME_SLOT = 0x18
FRAME_COUNT = 29
FULL_FRAME_COUNT = 34
VIEW_SLOT_COUNT = 64
LOD_FRAME_COUNT = 25
LOD32_FRAME_BYTES = 0x200
LOD16_FRAME_BYTES = 0x80
LOD32_OFFSET = 0xE800 + 20 * LOD32_FRAME_BYTES
LOD16_OFFSET = 0x14800
PALETTE_STRIDE = 0x200
PALETTE_BYTES = PALETTE_STRIDE
SINE_OFFSET = 0x0E645C
AUTO_OFFSET = 0x0EBEC8
BRAKE_OFFSET = 0x0EBEA8
BRAKE_ACCELERATE_OFFSET = 0x0EBEB8
FORWARD_RESPONSE_OFFSET = 0x0EC1D8
LOCAL_OFFSET = 0x0EBF78
STEERING_DELTA_OFFSET = 0x0EC71C
STEERING_RESPONSE_OFFSET = 0x0EC0D8
TRACK_ANGLE_OFFSET = 0x0DE3FC
TRACK_ANGLE_BYTES = 0x8000
ANGLE_TARGET_OFFSET = 0x0EC300
ANGLE_TARGET_BYTES = 0x1A0
TIME_TRIAL_STATS_BYTES = 52
GP_ENGINE_PROFILE_BYTES = ANGLE_TARGET_BYTES + 36
VIEW_SLOT_OFFSET = 0x0ECD5C
MOTION_END = 0xF2C24


def _signed16(value):
    return value if value < 0x8000 else value - 0x10000


def _pack_i16(values):
    return struct.pack("<" + "h" * len(values), *values)


def _motion_payload(rom: bytes, source_id: int):
    counts = (1024, 64, 8, 8, 8, 256, 10, 16)
    local = []
    # sub_803C1BC uses the unscaled row for GP selector 0/1 and the
    # boosted row for the other selectors. Keep both profiles for every class.
    for profile in range(2):
        for character_class in range(8):
            for index in range(16):
                raw = struct.unpack_from(
                    "<h", rom, LOCAL_OFFSET + character_class * 0x20 +
                    index * 2)[0]
                value = raw * 16
                if profile:
                    value += value // 2
                    if value <= 0x31:
                        value = 0xE0
                local.append(_signed16(value & 0xFFFF))
    steering = struct.unpack_from(
        "<16h", rom, STEERING_RESPONSE_OFFSET + source_id * 0x20)
    # C1BC: Time Trial uses engine-class row 2 and the boosted local profile.
    base = struct.unpack_from("<i", rom, 0xEBF58 + source_id * 4)[0]
    engine = struct.unpack_from("<h", rom, 0xEC7C0 + 2 * 2)[0]
    limits = [-1] * 10 + [value * 16 + 0xA0 for value in
        struct.unpack_from("<6h", rom, 0xEC078 + source_id * 12)]
    stats = (struct.pack("<i16h", (base + engine) * 65536, *limits) +
             rom[0xEBF48:0xEBF58])
    deceleration = struct.unpack_from("<8h", rom, 0xEC7B0)
    if any(value > 0 for value in deceleration):
        raise ValueError("finished-driver deceleration must be nonpositive")
    response_rows = struct.unpack_from('<64h', rom, 0xEBE28)
    selector_rows = struct.unpack_from('<16i', rom, 0xEC1E8)
    offsets = struct.unpack_from('<8i', rom, 0xEC228)
    if (any(value > 0 if index % 8 == 0 else value < 0
            for index, value in enumerate(response_rows)) or
        any(not 0 <= value <= 7 for value in selector_rows) or
        any(not -7 <= value <= 7 for value in offsets) or
        any(base + offset > 7 for base in selector_rows for offset in offsets)):
        raise ValueError('invalid driver response profile')
    rivals=struct.unpack_from('<64I',rom,0xEDC34)
    if any(sorted(rivals[row*8:row*8+8])!=list(range(8)) for row in range(8)):
        raise ValueError('invalid rival class row')
    contact=struct.unpack_from('<8I',rom,0xEC6DC)
    recovery=struct.unpack_from('<8i',rom,0xEC248)
    if any(not -32768<=v<=0 for v in recovery):
        raise ValueError('invalid contact recovery acceleration')
    roster=bytes(rivals)+struct.pack('<8H',*(v&65535 for v in contact))+struct.pack('<8i',*recovery)
    profiles = bytearray()
    for engine_class, adjustment in enumerate((-128, 0, 160)):
        # BB48 permits a four-bit row index: retain the source's entire
        # addressable window, including bytes after the 328-byte CPU row.
        offset = ANGLE_TARGET_OFFSET + engine_class * 328
        profiles += rom[offset:offset + ANGLE_TARGET_BYTES]
        engine = struct.unpack_from("<h", rom, 0xEC7C0 + engine_class * 2)[0]
        limits = [-1] * 10 + [value * 16 + adjustment for value in
            struct.unpack_from("<6h", rom, 0xEC078 + source_id * 12)]
        profiles += struct.pack("<i16h", (base + engine) * 65536, *limits)
    return (struct.pack("<9I", 0x31544F4D, *counts) +
            rom[SINE_OFFSET:SINE_OFFSET + 1024 * 4] +
            rom[AUTO_OFFSET:AUTO_OFFSET + 64 * 2] +
            rom[BRAKE_OFFSET:BRAKE_OFFSET + 8 * 2] +
            rom[BRAKE_ACCELERATE_OFFSET:BRAKE_ACCELERATE_OFFSET + 8 * 2] +
            rom[FORWARD_RESPONSE_OFFSET:FORWARD_RESPONSE_OFFSET + 8 * 2] +
            _pack_i16(local) +
            rom[STEERING_DELTA_OFFSET:STEERING_DELTA_OFFSET + 10 * 2] +
            _pack_i16(steering) +
            rom[TRACK_ANGLE_OFFSET:TRACK_ANGLE_OFFSET + TRACK_ANGLE_BYTES] +
            rom[ANGLE_TARGET_OFFSET:ANGLE_TARGET_OFFSET + ANGLE_TARGET_BYTES] + stats + profiles +
            rom[0xEC7B0:0xEC7C0] + rom[0xEBE28:0xEBEA8] +
            rom[0xEC1E8:0xEC228] + rom[0xEC228:0xEC248] + roster + rom[0xECCA8:0xECCE4] + rom[0xF2C14:0xF2C24])


def driver_sections(rom):
    """Normalize every driver directly from the verified ROM's data ranges."""
    return _driver_sections(rom[0x345800:0x3FFF00], rom[0x344600:0x345800], rom) + [
        (10, 9, battle_bomb_graphics(rom), 4)]


def battle_bomb_graphics(rom):
    """435C8/43708: class8 bank, sixteen walking frames and both LODs."""
    if len(rom) < 0x400000:
        raise ValueError('truncated Battle bomb graphics')
    slots=rom[0xECD9C:0xECDDC]
    if len(slots)!=64 or any(value>14 or value%2 for value in slots):
        raise ValueError('invalid Battle bomb view table')
    return (struct.pack('<8I',0x33524143,16*FRAME_BYTES,256,64,64,8,16,64) +
            slots + rom[0x3F5800:0x400000] + rom[0x345600:0x345800])


def _driver_sections(tiles, palettes, rom):
    required_tiles = (CHARACTER_COUNT - 1) * BLOCK_BYTES + max(
        FULL_FRAME_COUNT * FRAME_BYTES,
        LOD32_OFFSET + LOD_FRAME_COUNT * LOD32_FRAME_BYTES,
        LOD16_OFFSET + LOD_FRAME_COUNT * LOD16_FRAME_BYTES)
    if (len(tiles) < required_tiles or
            len(palettes) < (CHARACTER_COUNT - 1) * PALETTE_STRIDE + PALETTE_BYTES or
            len(rom) < max(MOTION_END, VIEW_SLOT_OFFSET + VIEW_SLOT_COUNT,
                           ANGLE_TARGET_OFFSET + ANGLE_TARGET_BYTES)):
        raise ValueError("character source assets are truncated")
    sections = []
    for roster_index, source_class in enumerate(GRAPHICS_SOURCE_IDS):
        content_id = roster_index + 1
        tile_offset = source_class * BLOCK_BYTES
        palette_offset = source_class * PALETTE_STRIDE
        graphic = struct.pack(
            "<8I", 0x33524143, FULL_FRAME_COUNT * FRAME_BYTES,
            256, 64, 64, source_class, FULL_FRAME_COUNT, VIEW_SLOT_COUNT)
        graphic += rom[VIEW_SLOT_OFFSET:VIEW_SLOT_OFFSET + VIEW_SLOT_COUNT]
        graphic += tiles[tile_offset:tile_offset + FULL_FRAME_COUNT * FRAME_BYTES]
        graphic += tiles[tile_offset + LOD32_OFFSET:
                         tile_offset + LOD32_OFFSET + LOD_FRAME_COUNT * LOD32_FRAME_BYTES]
        graphic += tiles[tile_offset + LOD16_OFFSET:
                         tile_offset + LOD16_OFFSET + LOD_FRAME_COUNT * LOD16_FRAME_BYTES]
        graphic += palettes[palette_offset:palette_offset + PALETTE_BYTES]
        manifest = struct.pack("<6I", 0x31524843, content_id, 1,
                               content_id, content_id, source_class)
        sections.extend(((2, content_id, manifest, 1),
                         (10, content_id, graphic, 3),
                         (13, content_id, _motion_payload(rom, source_class), 8)))
    return sections


def build_driver_pack(tiles_path, palettes_path, rom_path, output):
    payloads = _driver_sections(tiles_path.read_bytes(), palettes_path.read_bytes(),
                               rom_path.read_bytes())
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mksc-drivers-", dir=output.parent) as temporary:
        root = pathlib.Path(temporary)
        sections = []
        for kind, content_id, payload, version in payloads:
            name = f"{kind}-{content_id}.bin"
            (root / name).write_bytes(payload)
            sections.append({"kind": kind, "content_id": content_id,
                "version": version, "flags": 3, "path": name})
        manifest = root / "manifest.json"
        manifest.write_text(json.dumps({"sections": sections}))
        return build_asset_pack.build_pack(manifest, output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--character-tiles", type=pathlib.Path, required=True)
    parser.add_argument("--character-palettes", type=pathlib.Path,
                        required=True)
    parser.add_argument("--rom", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    digest, deterministic_digest = build_driver_pack(
        args.character_tiles, args.character_palettes, args.rom, args.output)
    print(f"pack_sha256={digest}")
    print(f"deterministic_sha256={deterministic_digest}")
    print("drivers=8 roster=0,1,2,3,4,5,6,7 "
          "source_classes=0,1,3,6,7,4,5,2 frames=0..33 view-slots=64")


if __name__ == "__main__":
    main()
