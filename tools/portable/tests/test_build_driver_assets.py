import importlib.util
import pathlib
import struct
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location(
    "build_driver_assets", ROOT / "tools/portable/build_driver_assets.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class BuildDriverAssetsTests(unittest.TestCase):
    def test_finished_deceleration_requires_eight_nonpositive_values(self):
        rom = bytearray(MODULE.MOTION_END)
        for row in range(8):struct.pack_into("<8I",rom,0xEDC34+row*32,*range(8))
        struct.pack_into('<8h', rom, 0xEC7B0, -1, -2, -3, -4, -5, -6, -7, -8)
        self.assertEqual(MODULE._motion_payload(rom, 0)[-428:-412], struct.pack('<8h', *range(-1, -9, -1)))
        struct.pack_into('<h', rom, 0xEC7BE, 1)
        with self.assertRaisesRegex(ValueError, 'deceleration must be nonpositive'):
            MODULE._motion_payload(rom, 0)

    def test_bomb_bank_preserves_paired_views_and_detail_levels(self):
        rom = bytearray(0x400000)
        slots = bytes((i // 8) * 2 for i in range(64))
        rom[0xECD9C:0xECDDC] = slots
        rom[0x3F5800:0x400000] = bytes([0x53]) * 0xA800
        rom[0x345600:0x345800] = bytes([0x27]) * 512
        payload = MODULE.battle_bomb_graphics(rom)
        self.assertEqual(struct.unpack_from("<8I", payload),
                         (0x33524143, 0x8000, 256, 64, 64, 8, 16, 64))
        self.assertEqual(payload[32:96], slots)
        self.assertEqual(payload[96:-512], bytes([0x53]) * 0xA800)
        self.assertEqual(payload[-512:], bytes([0x27]) * 512)
        for invalid in (1, 16, 255):
            rom[0xECD9C] = invalid
            with self.assertRaises(ValueError):
                MODULE.battle_bomb_graphics(rom)
        with self.assertRaises(ValueError):
            MODULE.battle_bomb_graphics(rom[:-1])

    def test_builds_eight_source_mapped_drivers_deterministically(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            tiles = root / "tiles.bin"
            palettes = root / "palettes.bin"
            rom = root / "game.gba"
            tile_data = bytearray(MODULE.CHARACTER_COUNT * MODULE.BLOCK_BYTES)
            palette_data = bytearray(
                MODULE.CHARACTER_COUNT * MODULE.PALETTE_STRIDE)
            for driver in range(MODULE.CHARACTER_COUNT):
                start = driver * MODULE.BLOCK_BYTES
                tile_data[start:start + MODULE.FULL_FRAME_COUNT *
                          MODULE.FRAME_BYTES] = bytes([driver + 1]) * \
                    (MODULE.FULL_FRAME_COUNT * MODULE.FRAME_BYTES)
                start = driver * MODULE.PALETTE_STRIDE
                palette_data[start:start + MODULE.PALETTE_BYTES] = \
                    bytes([0x40 + driver]) * MODULE.PALETTE_BYTES
            tiles.write_bytes(tile_data)
            palettes.write_bytes(palette_data)
            rom_data = bytearray(max(
                MODULE.MOTION_END,
                MODULE.VIEW_SLOT_OFFSET + MODULE.VIEW_SLOT_COUNT,
                MODULE.ANGLE_TARGET_OFFSET + MODULE.ANGLE_TARGET_BYTES))
            rom_data[MODULE.VIEW_SLOT_OFFSET:MODULE.VIEW_SLOT_OFFSET +
                     MODULE.VIEW_SLOT_COUNT] = bytes(
                         min(24, index // 2)
                         for index in range(MODULE.VIEW_SLOT_COUNT))
            struct.pack_into("<8h",rom_data,0xEC7B0,-3,-7,-13,-21,-29,-43,-59,-79)
            struct.pack_into("<h",rom_data,0xEC7C4,160)
            for driver in range(MODULE.CHARACTER_COUNT):
                struct.pack_into("<i",rom_data,0xEBF58+driver*4,900+driver)
                for index in range(16):
                    struct.pack_into("<h", rom_data,
                                     MODULE.LOCAL_OFFSET + driver * 0x20 +
                                     index * 2, 10 + driver + index)
                for index in range(4):
                    struct.pack_into("<h", rom_data,
                                     MODULE.STEERING_RESPONSE_OFFSET +
                                     driver * 0x20 + index * 2,
                                     100 + driver * 10 + index)
            for row in range(8):struct.pack_into("<8I",rom_data,0xEDC34+row*32,*range(8))
            rom.write_bytes(rom_data)
            first, second = root / "first.pack", root / "second.pack"
            MODULE.build_driver_pack(tiles, palettes, rom, first)
            MODULE.build_driver_pack(tiles, palettes, rom, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())

            data = first.read_bytes()
            self.assertEqual(data[:8], b"MKSCPACK")
            section_count = struct.unpack_from("<I", data, 24)[0]
            self.assertEqual(section_count, 24)
            rows = [struct.unpack_from("<IIIIQQII", data,
                                       112 + index * 40)
                    for index in range(section_count)]
            self.assertEqual([(row[0], row[1]) for row in rows],
                             [(kind, driver) for kind in (2, 10, 13)
                              for driver in range(1, 9)])
            graphics_rows = rows[8:16]
            for expected_source, row in zip(MODULE.GRAPHICS_SOURCE_IDS,
                                            graphics_rows):
                offset, size = row[4], row[5]
                payload = data[offset:offset + size]
                self.assertEqual(payload[:4], b"CAR3")
                self.assertEqual(struct.unpack_from("<I", payload, 20)[0],
                                 expected_source)
                self.assertEqual(struct.unpack_from("<II", payload, 24),
                                 (MODULE.FULL_FRAME_COUNT,
                                  MODULE.VIEW_SLOT_COUNT))
                self.assertEqual(row[2], 3)
                self.assertEqual(struct.unpack_from("<I", payload, 8)[0], 256)
                self.assertEqual(payload[-MODULE.PALETTE_BYTES:],
                    palette_data[expected_source*MODULE.PALETTE_STRIDE:
                                 (expected_source+1)*MODULE.PALETTE_STRIDE])
            for driver, row in enumerate(rows[16:24]):
                source_class = MODULE.GRAPHICS_SOURCE_IDS[driver]
                offset, size = row[4], row[5]
                payload = data[offset:offset + size]
                self.assertEqual(payload[:4], b"MOT1")
                self.assertEqual(payload[-412:-284],rom_data[0xEBE28:0xEBEA8])
                self.assertEqual(payload[-284:-220],rom_data[0xEC1E8:0xEC228])
                self.assertEqual(payload[-220:-188],rom_data[0xEC228:0xEC248])
                self.assertEqual(row[2], 8)
                self.assertEqual(size, 4872 + MODULE.TRACK_ANGLE_BYTES +
                                 MODULE.ANGLE_TARGET_BYTES + MODULE.TIME_TRIAL_STATS_BYTES +
                                 3 * MODULE.GP_ENGINE_PROFILE_BYTES + 16 + 412)
                self.assertEqual(struct.unpack_from("<8h",payload,size-428),(-3,-7,-13,-21,-29,-43,-59,-79))
                payload=payload[:-428];size-=428
                for engine_class, adjustment in enumerate((-128, 0, 160)):
                    profile_offset = size - (3-engine_class)*MODULE.GP_ENGINE_PROFILE_BYTES
                    source_offset = MODULE.ANGLE_TARGET_OFFSET + engine_class*328
                    self.assertEqual(payload[profile_offset:profile_offset+416],
                                     rom_data[source_offset:source_offset+416])
                    engine_bonus = struct.unpack_from("<h",rom_data,
                                                      0xEC7C0+engine_class*2)[0]
                    self.assertEqual(struct.unpack_from("<i16h",payload,profile_offset+416),
                        ((900+source_class+engine_bonus)*65536,)+(-1,)*10+(adjustment,)*6)
                payload = payload[:-3*MODULE.GP_ENGINE_PROFILE_BYTES]
                size -= 3*MODULE.GP_ENGINE_PROFILE_BYTES
                self.assertEqual(struct.unpack_from("<i",payload,size-52)[0],
                                 (1060+source_class)*65536)
                self.assertEqual(struct.unpack_from("<16h",payload,size-48),
                                 (-1,)*10+(160,)*6)
                payload = payload[:-MODULE.TIME_TRIAL_STATS_BYTES]
                size -= MODULE.TIME_TRIAL_STATS_BYTES
                local_offset = 36 + 1024 * 4 + (64 + 8 + 8 + 8) * 2
                self.assertEqual(struct.unpack_from(
                    "<h", payload, local_offset + source_class * 32)[0],
                    (10 + source_class) * 16)
                self.assertEqual(struct.unpack_from(
                    "<h", payload, local_offset + 256 + source_class * 32)[0],
                    (10 + source_class) * 24)
                self.assertEqual(struct.unpack_from("<h", payload,
                    size - MODULE.ANGLE_TARGET_BYTES -
                    MODULE.TRACK_ANGLE_BYTES - 32)[0],
                    100 + source_class * 10)
                self.assertEqual(payload[-MODULE.ANGLE_TARGET_BYTES-
                                         MODULE.TRACK_ANGLE_BYTES:
                                         -MODULE.ANGLE_TARGET_BYTES],
                    rom_data[MODULE.TRACK_ANGLE_OFFSET:
                             MODULE.TRACK_ANGLE_OFFSET +
                             MODULE.TRACK_ANGLE_BYTES])
                self.assertEqual(payload[-MODULE.ANGLE_TARGET_BYTES:],
                    rom_data[MODULE.ANGLE_TARGET_OFFSET:
                             MODULE.ANGLE_TARGET_OFFSET +
                             MODULE.ANGLE_TARGET_BYTES])

    def test_rejects_truncated_sources(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            tiles, palettes, rom = root / "tiles", root / "palettes", root / "rom"
            tiles.write_bytes(b"bad")
            palettes.write_bytes(b"bad")
            rom.write_bytes(b"bad")
            with self.assertRaisesRegex(ValueError, "truncated"):
                MODULE.build_driver_pack(tiles, palettes, rom,
                                         root / "out.pack")


if __name__ == "__main__":
    unittest.main()
