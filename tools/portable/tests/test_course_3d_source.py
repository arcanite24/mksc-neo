"""ROM-free 2D source decoding, arena admission and immutable overlay tests."""
from dataclasses import FrozenInstanceError
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import course_3d_source as source

COURSE = 17


def words(*values):
    return struct.pack('<'+'I'*len(values), *values)


def fixture(arena=False, identity=COURSE):
    """Small rectangular source world with deliberately distinct AI rows."""
    def section(kind, data, version=1):
        return (kind, identity), ({'kind':kind, 'content_id':identity, 'version':version}, data)
    rows = [[(8+row, 8, row), (8, 40+row, row), (40, 40, row), (40, 8, row)]
            for row in range(3)]
    route = words(0x31545552, 4, 3, 40)
    route += b''.join(words(*point) for row in rows for point in row)
    route += bytes(i % 3 for i in range(40)) + words(1, 0xffffffff, 7, 0, 0, 0, 0)
    rules = words(0x314c5552, identity, 4 if arena else 3, 3, 1, 2, 2, 3, 4 if arena else 3,
                  8*65536+32768, 24*65536)
    rules += struct.pack('<HHIIIHHHH', 12345, 3, 3 if arena else 2,
                         3 if arena else 2, 16*65536, 0 if arena else 3, 180, 120, 0x83)
    rules += bytes((5, 0, 0, 0))
    raster = bytes((0x80, 1, 2, 0x83, 0x7f, 0xff, 2, 3))*2
    tilemap = bytearray([1]*64)
    tilemap[0:4] = bytes((0, 0xd3, 0xe0, 2))
    tiles = bytearray(256*64)
    tiles[64:128] = bytes((1, 2, 0, 1))*16
    tiles[2*64:3*64] = bytes([2])*64
    tiles[0xd3*64:0xd4*64] = tiles[64:128]
    tiles[0xe0*64:0xe1*64] = bytes([3])*64
    palette = [0x7fff, 0x801f, 0x7c00, 0x03e0]+[0]*60
    materials = bytearray([0x26]*256)
    materials[1], materials[2], materials[0xd3], materials[0xe0] = 0x40, 0x22, 0x80, 0x16
    count = 4 if arena else 8
    grid = words(0x31475047, count)+b''.join(words((8+i*4)*65536, 24*65536+i, i*7000)
                                            for i in range(count))
    sky = words(0x31594b53, 32, 1, 2048, 2048, 2048, 32, 32, 32, 32, 0, 0, 0, 0, 0, 0)
    sky += bytes(32)+struct.pack('<H', 31)+bytes(6144)
    sections = dict((
        section(3, rules, 2), section(4, route, 4),
        section(5, words(0x31444c57, 4, 4, 8, 8, 16, 64)+raster+tilemap),
        section(6, words(0x3154414d, 256)+materials),
        section(8, words(0x31444152, 16384, 64, 1, 4, 0)+tiles+struct.pack('<64H', *palette)),
        section(9, sky), section(34, grid),
        section(25, words(0x314a424f, 1)+struct.pack('<IIHBB', 20*65536+1, 32*65536, 0x8800, 9, 30)),
        section(27, words(0x314e4943, 1)+bytes((0x80, 2, 3, 7))),
        section(28, b'SCN1-authored-opaque-passthrough', 4),
    ))
    sections[37, 2] = ({'version':2}, words(0x31544143, 1)+struct.pack('<IBBHI', identity, 3, 2, 101, 42))
    return sections


def replace(sections, kind, data=None, version=None):
    header, previous = sections[kind, COURSE]
    sections[kind, COURSE] = (dict(header, version=header['version'] if version is None else version),
                               previous if data is None else bytes(data))


class SourceTests(unittest.TestCase):
    def test_source_units_catalog_and_three_independent_route_rows(self):
        course = source.read_course(fixture(), COURSE, name='An authored loop')
        self.assertEqual((course.course_id, course.name, course.catalog_id, course.cup,
                          course.slot, course.source_id, course.song, course.cup_id),
                         (COURSE, 'An authored loop', 2, 3, 2, 42, 101, 0x20003))
        self.assertEqual((course.width, course.height, course.rules.start_x_q16,
                          course.rules.finish_y_q16), (64, 64, 8*65536+32768, 16*65536))
        self.assertEqual((course.raster_width, course.map_width), (4, 8))
        self.assertEqual(tuple(row[0].x for row in course.route_rows), (8, 9, 10))
        self.assertEqual(tuple(row[0].mode for row in course.route_rows), (0, 1, 2))
        self.assertEqual(course.target_table, bytes(i%3 for i in range(40)))
        self.assertEqual((course.sentinel_mask, course.sentinel_points), (1, ((-1,7),(0,0),(0,0))))
        self.assertEqual(course.rules.start_checkpoint_raw, 0x83)
        self.assertEqual(course.item_boxes[0], source.SourceBox(20*65536+1,32*65536,9,30))
        self.assertEqual(course.coins[0], source.SourceCoin(16,24,7))
        self.assertEqual(len(course.grid), 8)
        self.assertEqual(course.grid[7], source.SourceGrid(36*65536,24*65536+7,49000))
        self.assertTrue(course.sky.startswith(b'SKY1'))
        self.assertEqual(course.passthrough[0].data, b'SCN1-authored-opaque-passthrough')

    def test_palette_zero_bit15_alias_and_shared_tile_semantics(self):
        course = source.read_course(fixture(), COURSE)
        self.assertEqual(course.palette[:4], (0x7fff,0x801f,0x7c00,0x03e0))
        self.assertEqual(course.tiles_argb[1][:4], (0xffff0000,0xff0000ff,0,0xffff0000))
        self.assertEqual(course.tiles_argb[0], (0,)*64)
        self.assertEqual(course.tiles_argb[0xd3], course.tiles_argb[1])
        self.assertNotEqual(course.materials[0xd3], course.materials[1])
        self.assertEqual(course.alias_tiles, (0xd3,))
        self.assertEqual(course.tiles_argb[0xe0], (0xff00ff00,)*64)
        self.assertEqual(course.tilemap[:4], bytes((0,0xd3,0xe0,2)))

    def test_opt_in_ground_indices_preserve_equal_colors_and_invalid_transparency(self):
        sections=fixture()
        data=bytearray(sections[8,COURSE][1])
        data[24+64:24+70]=bytes((1,2,0,64,255,63))
        struct.pack_into('<H',data,24+16384+2*2,0x801f) # Same seed RGB as index1.
        replace(sections,8,data)
        saved=bytes(data)
        plain=source.read_course(sections,COURSE)
        indexed=source.read_course(sections,COURSE,preserve_indices=True)
        self.assertIsNone(plain.tiles_indices)
        self.assertEqual(indexed.tiles_argb,plain.tiles_argb)
        self.assertEqual(indexed.tiles_argb[1][:2],(0xffff0000,)*2)
        self.assertEqual(indexed.tiles_indices[1][:6],bytes((1,2,255,255,255,63)))
        self.assertEqual(indexed.tiles_indices[0],bytes((255,))*64)
        self.assertEqual(sections[8,COURSE][1],saved)
        self.assertIsInstance(indexed.tiles_indices,tuple)
        self.assertIsInstance(indexed.tiles_indices[1],bytes)
        self.assertEqual(source.read_course(sections,COURSE,preserve_indices=False),plain)
        with self.assertRaisesRegex(source.SourceError,'preserve_indices'):
            source.read_course(sections,COURSE,preserve_indices=1)

    def test_used_outside_palette_texels_are_transparent_without_masking(self):
        sections = fixture()
        data = bytearray(sections[8,COURSE][1])
        data[24+64:24+68] = bytes((64,255,1,2))
        replace(sections,8,data)
        course = source.read_course(sections,COURSE)
        self.assertIn(1,course.tilemap)
        self.assertEqual(course.tiles_argb[1][:4], (0,0,0xffff0000,0xff0000ff))

    def test_arena_is_content_driven_not_an_id_range(self):
        course = source.read_course(fixture(arena=True, identity=70000), 70000)
        self.assertTrue(course.arena)
        self.assertEqual((course.rules.progress,course.rules.finish,course.rules.laps), (3,3,0))
        self.assertEqual(len(course.grid), 4)
        self.assertEqual(course.name, 'COURSE 70000')

    def test_overlay_copies_mutable_bytes_and_keeps_input_unchanged(self):
        sections = fixture()
        original = sections.copy()
        box = bytearray(words(0x314a424f, 0))
        overlay = [{'kind':25,'content_id':COURSE,'version':1,'data':box}]
        course = source.read_course(sections, COURSE, overlay=overlay)
        self.assertEqual(course.item_boxes, ())
        box[0] = 0
        self.assertEqual(sections, original)
        self.assertEqual(len(source.read_course(sections, COURSE).item_boxes), 1)
        with self.assertRaises(FrozenInstanceError):
            course.name = 'mutated'
        with self.assertRaises(TypeError):
            course.tiles_argb[1][0] = 0

    def test_absent_optional_metadata_is_explicit(self):
        sections = fixture()
        for key in ((37,2),(9,COURSE),(27,COURSE),(28,COURSE)):
            del sections[key]
        course = source.read_course(sections, COURSE)
        self.assertEqual((course.catalog_id,course.song,course.source_id), (0,None,None))
        self.assertEqual((course.coins,course.passthrough,course.sky,course.sky_version), ((),(),None,0))

    def test_named_malformed_sections(self):
        cases = (
            ('WLD1', 5, lambda p: p[:-1], None),
            ('WLD1', 5, lambda p: p[:28]+bytes([7])+p[29:], None),
            ('RUT1', 4, lambda p: p[:-1], None),
            ('RUT1', 4, lambda p: p[:160]+bytes([3])+p[161:], None),
            ('RUL1', 3, lambda p: p[:69]+bytes([1])+p[70:], None),
            ('MAT1', 6, lambda p: p+bytes(1), None),
            ('RAD1', 8, lambda p: p[:8]+words(0)+p[12:], None),
            ('GPG1', 34, lambda p: p[:-12], None),
            ('OBJ1', 25, lambda p: p[:18]+bytes([0,0])+p[20:], None),
            ('CIN1', 27, lambda p: p[:8]+bytes([0])+p[9:], None),
            ('SKY1', 9, lambda p: p[:-1], None),
        )
        for label, kind, mutate, version in cases:
            with self.subTest(label=label,kind=kind):
                sections = fixture()
                replace(sections,kind,mutate(sections[kind,COURSE][1]),version)
                with self.assertRaisesRegex(source.SourceError, f'course={COURSE} {label}:'):
                    source.read_course(sections,COURSE)

    def test_inactive_sentinel_and_duplicate_catalog_rejected(self):
        sections = fixture()
        data = bytearray(sections[4,COURSE][1])
        struct.pack_into('<I',data,len(data)-28,0)
        replace(sections,4,data)
        with self.assertRaisesRegex(source.SourceError,'RUT1: inactive sentinel'):
            source.read_course(sections,COURSE)
        sections = fixture()
        sections[37,3] = sections[37,2]
        with self.assertRaisesRegex(source.SourceError,'CAT1: course appears'):
            source.read_course(sections,COURSE)

    def test_material_layout_versions_and_missing_required(self):
        sections = fixture()
        data = sections[6,COURSE][1]+bytes(sum(source._MATERIAL_TAILS))
        replace(sections,6,data,16)
        self.assertEqual(len(source.read_course(sections,COURSE).materials),256)
        del sections[34,COURSE]
        with self.assertRaisesRegex(source.SourceError,'GPG1: missing kind 34'):
            source.read_course(sections,COURSE)


if __name__ == '__main__':
    unittest.main()
