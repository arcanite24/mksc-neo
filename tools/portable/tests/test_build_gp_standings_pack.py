import importlib.util
import pathlib
import struct
import unittest


PATH = pathlib.Path(__file__).parents[1] / "build_gp_standings_pack.py"
SPEC = importlib.util.spec_from_file_location("build_gp_standings_pack", PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


def literal_lz(data):
    output = bytearray((0x10, len(data) & 255,
                        (len(data) >> 8) & 255, (len(data) >> 16) & 255))
    for start in range(0, len(data), 8):
        output.append(0)
        output += data[start:start + 8]
    return bytes(output)


class BuildGpStandingsPackTests(unittest.TestCase):
    def fixture(self):
        rom = bytearray(0xE9000)
        source_header = 4
        track_index = 12
        track_def = 0x20000
        track_name = 0x21000
        cup_name = 0x22000
        struct.pack_into("<I", rom, MODULE.CUPS_OFFSET + source_header * 4,
                         track_index)
        struct.pack_into("<I", rom,
                         MODULE.TRACK_TABLE_OFFSET + track_index * 4,
                         0x08000000 + track_def)
        struct.pack_into("<I", rom, track_def + 0x30,
                         0x08000000 + track_name)
        struct.pack_into("<I", rom, MODULE.CUP_GRAPHICS_TABLE + 4,
                         0x08000000 + cup_name)
        rom[track_name:track_name + 4 + 0x300 + 0x60] = literal_lz(
            bytes((index * 3) & 255 for index in range(0x300)))
        rom[cup_name:cup_name + 4 + 0x5C0 + 0xB8] = literal_lz(
            bytes((index * 5) & 255 for index in range(0x5C0)))
        background = bytearray(0x800)
        for index in range(0x400):
            struct.pack_into("<H", background, index * 2, index)
        encoded = literal_lz(background)
        rom[0x9B2AC:0x9B2AC + len(encoded)] = encoded
        for destination, source, size in MODULE.STATIC_TILE_RANGES:
            rom[source:source + size] = bytes((destination // 0x400 + 1,)) * size
        rom[0x9A9AC:0x9AA2C] = bytes(range(0x80))
        rom[0xAACF0:0xAAD30] = bytes(range(0x40))
        display = (0, 1, 7, 2, 5, 6, 3, 4)
        struct.pack_into("<8I", rom, MODULE.DRIVER_DISPLAY_TABLE, *display)
        for pattern_index in range(17):
            address = 0x23000 + pattern_index * 0x20
            struct.pack_into("<I", rom,
                             MODULE.DRIVER_PATTERN_TABLE + pattern_index * 4,
                             0x08000000 + address)
            rom[address:address + 20] = bytes((pattern_index,)) * 20
        return bytes(rom)

    def test_extra_uses_its_own_course_directory(self):
        rom=bytearray(self.fixture())
        # This Extra slot points to the fixture's source track definition,
        # while the corresponding GBA slot is deliberately invalid.
        struct.pack_into('<I',rom,0xE74C4+4*4,12)
        struct.pack_into('<I',rom,MODULE.CUPS_OFFSET+4*4,0xffffffff)
        result=MODULE.payload(rom,64,4,1)
        self.assertEqual(struct.unpack_from('<I',result,24)[0],64)
        self.assertEqual(result[32+0x400:32+0x408],bytes(index*3 for index in range(8)))
        with self.assertRaises(ValueError):MODULE.payload(rom,64,4,2)

    def test_builds_source_layout_deterministically(self):
        rom = self.fixture()
        first = MODULE.payload(rom, 24, 4)
        second = MODULE.payload(rom, 24, 4)
        self.assertEqual(first, second)
        header = struct.unpack_from("<8I", first)
        self.assertEqual(header, (MODULE.MAGIC, 0x3000, 256, 1024,
                                  8, 18, 24, 4))
        tiles = first[32:32 + 0x3000]
        self.assertEqual(tiles[0x400:0x408],
                         bytes((index * 3) & 255 for index in range(8)))
        self.assertEqual(tiles[0x800:0x808],
                         bytes((index * 5) & 255 for index in range(8)))
        map_offset = 32 + 0x3000 + 0x200
        background = first[map_offset:map_offset + 0x800]
        self.assertEqual(struct.unpack_from("<H", background,
                                            (1 * 32 + 15) * 2)[0],
                         28 * 32 + 3)
        drivers = first[map_offset + 0x800:]
        # Driver class two maps to display seven: pattern rows 14 and 15.
        self.assertEqual(drivers[2 * 36:2 * 36 + 18], bytes((14,)) * 18)
        self.assertEqual(drivers[2 * 36 + 18:3 * 36], bytes((15,)) * 18)
        self.assertEqual(drivers[-18:], bytes((16,)) * 18)

    def test_rejects_bad_identity_and_lz(self):
        rom = self.fixture()
        with self.assertRaisesRegex(ValueError, "unsupported course"):
            MODULE.payload(rom, 24, 20)
        changed = bytearray(rom)
        changed[0x9B2AC] = 0
        with self.assertRaisesRegex(ValueError, "invalid LZ77"):
            MODULE.payload(bytes(changed), 24, 4)

    def test_portrait_border_overwrites_overlapping_name_bank(self):
        rom = bytearray(self.fixture())
        # Independently model the source uploads, rather than using the
        # importer's range list to decide which overlapping bytes should win.
        portrait_bank = bytes((index * 7 + 3) & 255 for index in range(0x480))
        name_bank = bytes((index * 11 + 5) & 255 for index in range(0xC00))
        rom[0x9AE2C:0x9B2AC] = portrait_bank
        rom[0xACAFC:0xAD6FC] = name_bank
        before = bytes(rom)
        result = MODULE.payload(rom, 24, 4)
        tiles = result[32:32 + 0x3000]
        # All eight 2x2 portrait interiors survive, followed by four shared
        # border tiles A0..A3. The remaining name bank must stay intact.
        self.assertEqual(tiles[0x1000:0x1400], portrait_bank[:0x400])
        self.assertEqual(tiles[0x1400:0x1480], portrait_bank[0x400:])
        self.assertNotEqual(tiles[0x1400:0x1480], name_bank[:0x80])
        self.assertEqual(tiles[0x1480:0x2000], name_bank[0x80:])
        self.assertEqual(bytes(rom), before)


if __name__ == "__main__":
    unittest.main()
