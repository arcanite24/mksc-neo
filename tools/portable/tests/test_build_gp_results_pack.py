import importlib.util
import pathlib
import struct
import unittest


PATH = pathlib.Path(__file__).parents[1] / "build_gp_results_pack.py"
SPEC = importlib.util.spec_from_file_location("build_gp_results_pack", PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class BuildGpResultsPackTests(unittest.TestCase):
    @staticmethod
    def zero_lz77(size):
        tokens = [(False, b"\0")]
        remaining = size - 1
        while remaining:
            if remaining >= 3:
                length = min(18, remaining)
                pair = (length - 3) << 12
                tokens.append((True, pair.to_bytes(2, "big")))
                remaining -= length
            else:
                tokens.append((False, b"\0"))
                remaining -= 1
        out = bytearray(b"\x10" + size.to_bytes(3, "little"))
        for start in range(0, len(tokens), 8):
            group = tokens[start:start + 8]
            flags = sum((1 << (7 - index)) for index, token in enumerate(group)
                        if token[0])
            out.append(flags)
            for _, value in group:
                out += value
        return bytes(out)

    def fixture(self):
        rom = bytearray(0xF0000)
        rom[0x62774:0x62834] = bytes((index*7)&0xff for index in range(0xc0))
        for index, (_, source, size) in enumerate(MODULE.FOLLOWUP_TILE_RANGES):
            rom[source:source + size] = bytes([0x40 + index]) * size
        for index, (_, source, size) in enumerate(MODULE.FOLLOWUP_PALETTE_RANGES):
            rom[source:source + size] = bytes([0x60 + index]) * size
        for index, (start, end, source) in enumerate(MODULE.ROM_RANGES):
            value = bytes([index + 1]) * (end - start)
            rom[source:source + len(value)] = value
        scroll = self.zero_lz77(MODULE.SCROLL_MAP_BYTES)
        followup = self.zero_lz77(MODULE.FOLLOWUP_MAP_BYTES)
        objects = self.zero_lz77(MODULE.OBJECT_TILE_BYTES)
        rom[MODULE.OBJECT_PALETTE_OFFSET:
            MODULE.OBJECT_PALETTE_OFFSET + MODULE.OBJECT_PALETTE_BYTES] = \
            bytes(range(MODULE.OBJECT_PALETTE_BYTES))
        rom[MODULE.MENU_PALETTE_OFFSET:
            MODULE.MENU_PALETTE_OFFSET + MODULE.MENU_PALETTE_BYTES] = \
            bytes(range(MODULE.MENU_PALETTE_BYTES))
        rom[MODULE.OBJECT_TILES_OFFSET:
            MODULE.OBJECT_TILES_OFFSET + len(objects)] = objects
        rom[MODULE.SCROLL_MAP_OFFSET:MODULE.SCROLL_MAP_OFFSET + len(scroll)] = scroll
        rom[MODULE.FOLLOWUP_MAP_OFFSET:MODULE.FOLLOWUP_MAP_OFFSET + len(followup)] = followup
        board = self.zero_lz77(MODULE.MAP_BYTES)
        rom[MODULE.TIME_BOARD_MAP_OFFSET:MODULE.TIME_BOARD_MAP_OFFSET + len(board)] = board
        rom[MODULE.DIGIT_OFFSET:MODULE.DIGIT_OFFSET + 40] = bytes(range(40))
        for offset, seed in zip(MODULE.HEADER_OFFSETS, (0x20, 0x30)):
            struct.pack_into("<HH6H", rom, offset, 3, 2,
                             *(seed + index for index in range(6)))
        row_offsets = (0xDB900, 0xDB920, 0xDB940, 0xDB960)
        for index, offset in enumerate(row_offsets):
            struct.pack_into("<I", rom, MODULE.ROW_TABLE + index * 4,
                             0x08000000 + offset)
            width, height = ((2, 2) if index < 3 else (14, 1))
            struct.pack_into("<HH", rom, offset, width, height)
            for cell in range(width * height):
                struct.pack_into("<H", rom, offset + 4 + cell * 2,
                                 0xC000 + index * 0x20 + cell)
        for index in range(MODULE.COMPACT_ROW_COUNT):
            offset = 0xDB980 + index * 0x20
            struct.pack_into("<I", rom, MODULE.COMPACT_ROW_TABLE + index * 4,
                             0x08000000 + offset)
            width, height = ((2, 1) if index < 5 else (14, 1))
            struct.pack_into("<HH", rom, offset, width, height)
            for cell in range(width * height):
                struct.pack_into("<H", rom, offset + 4 + cell * 2,
                                 0xC000 + index * 0x10 + cell)
        for index in range(MODULE.RECORD_PATTERN_COUNT):
            offset = 0xDC000 + index * 0x20
            struct.pack_into("<I", rom,
                             MODULE.RECORD_PATTERN_TABLE + index * 4,
                             0x08000000 + offset)
            for cell in range(MODULE.RECORD_PATTERN_CELLS):
                struct.pack_into("<H", rom, offset + cell * 2,
                                 0xD000 + index * 0x20 + cell)
        struct.pack_into("<8I",rom,0xD97B0,*range(7,-1,-1))
        rom[0xDB6E0:0xDB6E5]=bytes([4,2,0,3,1])
        return bytes(rom)

    def test_payload_contains_rom_art_and_no_fixed_race_hud(self):
        rom = self.fixture()
        data = MODULE.payload(rom)
        header = struct.unpack_from("<16I", data)
        self.assertEqual(header[:15], (MODULE.MAGIC, 0x4000, 256, 20, 2, 4,
                                       1024, 640, 384, 20, 0x800, 10, 16,
                                       16, 64))
        for start, end, source in MODULE.ROM_RANGES:
            self.assertEqual(data[64+start:64+end],rom[source:source+end-start])
        self.assertEqual(data[64+0x4000+0x140:64+0x4000+0x200],rom[0x62774:0x62834])
        map_offset=64+0x4000+0x200+40+32+8+128
        self.assertEqual(data[map_offset:map_offset+0x800],bytes(0x800))
        self.assertIn(struct.pack("<4H", 12, 12, 12, 32), data)
        self.assertEqual(len(data), 64 + 0x4000 + 0x200 + 40 + 32 + 8 +
                         128 + 0x800 + 0x4000 + 0x200 + 0x500 + 0x300 +
                         20 * 9 * 2 + 0x800 + 20 + 32 + 32 + 128 +
                         8 * 0x160 + 32 + 12 + 6 * 32 + 0x800 + 13)
        self.assertEqual(header[15], 8)
        self.assertEqual(data[-13:], bytes(range(7,-1,-1))+bytes([4,2,0,3,1]))
        data=data[:-13]
        tail = 12 + 6 * 32 + 0x800
        self.assertEqual(data[-tail:-tail + 12], struct.pack("<6H", *([8] * 5 + [32])))
        self.assertEqual(data[-0x800:], bytes(0x800))
        data = data[:-tail]
        self.assertEqual(data[-(8 * 0x160 + 32):-32],
                         rom[MODULE.RANK_TILE_OFFSET:
                             MODULE.RANK_TILE_OFFSET + 8 * 0x160])
        expected_rank_palette = bytearray(rom[0x627b4:0x627d4])
        expected_rank_palette[0x12:0x1a] = rom[
            MODULE.RANK_PALETTE_ROM_OFFSET:
            MODULE.RANK_PALETTE_ROM_OFFSET + 8]
        self.assertEqual(data[-32:], expected_rank_palette)

    def test_gp_lifecycle_fixture_has_complete_authored_results(self):
        fixture_path = pathlib.Path(__file__).with_name("make_recovered_pack_fixture.py")
        spec = importlib.util.spec_from_file_location("recovered_fixture", fixture_path)
        fixture = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(fixture)
        data = fixture.authored_results_payload()
        reference = MODULE.payload(self.fixture())
        self.assertEqual(len(data), len(reference))
        self.assertEqual(data[:64], reference[:64])
        self.assertEqual(data[-13:-5], bytes(range(7, -1, -1)))
        self.assertEqual(sorted(data[-5:]), list(range(5)))
        # Followup art/palettes and every fixed-size pattern are authored,
        # including the menu window required when replay returns to results.
        self.assertTrue(any(data[64:64 + 0x4000]))
        row_offset = 64 + 0x4000 + 512 + 40 + 32
        self.assertEqual(struct.unpack_from('<4H', data, row_offset), (12, 12, 12, 32))
        self.assertEqual(data[-13 - 2048:-13 - 2048 + 2], b'\x01\xe0')

    def test_rejects_invalid_content_maps(self):
        for offset,value,message in ((0xD97B0,8,"identity"),(0xDB6E0,5,"option")):
            rom=bytearray(self.fixture());rom[offset]=value
            with self.assertRaisesRegex(ValueError,message):
                MODULE.payload(rom)

    def test_rejects_invalid_pattern_pointer(self):
        rom = bytearray(self.fixture())
        struct.pack_into('<I',rom,MODULE.ROW_TABLE,0x03000000)
        with self.assertRaisesRegex(ValueError,"invalid scoreboard row pointer"):
            MODULE.payload(rom)

    def test_rejects_truncated_rom(self):
        with self.assertRaisesRegex(ValueError,"too small"):
            MODULE.payload(bytes(64))


if __name__ == "__main__":
    unittest.main()
