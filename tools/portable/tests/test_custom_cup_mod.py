"""The custom-cup example paints, compiles and packs four tracks without game data."""
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import png_indexed
import track_format
from test_track_format import COURSE, stock_sections, write_pack

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location('custom_cup_example', ROOT / 'mods/examples/custom-cup/build.py')
EXAMPLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXAMPLE)
TEMPLATES = (26, 32, 34)


def template_sections():
    """The synthetic course 24 plus copies standing in for templates 26, 32 and 34."""
    sections = stock_sections()
    motion = {'controller': 7, 'layer_mask': 5, 'blend_mode': 1, 'blend_a': 4,
              'blend_b': 16, 'yaw_shift': [5,6,7], 'x_bias': [-40,12,-18],
              'y_bias': [70,38,70], 'split_y': 24, 'split_layer': 2,
              'split_yaw_shift': 8, 'split_x_bias': -64, 'lightning_hide_layer': 2,
              'layer_source': [0,2,1], 'road_mode': 3}
    sections = [(kind,cid,2 if kind == 9 else version,
                 data[:60] + struct.pack('<I',40) + data[64:] + track_format.sky_motion_bytes(motion)
                 if kind == 9 else data) for kind,cid,version,data in sections]
    sections.append((84, COURSE, 1, b'AMB1' + bytes(range(64))))
    copies = [(kind, template, version, data) for template in TEMPLATES
              for kind, content_id, version, data in sections
              if content_id == COURSE and kind in (*track_format.REQUIRED_KINDS,84)]
    return sections + copies


class CustomCupTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.pack = write_pack(self.root, template_sections())

    def tearDown(self):
        self.temporary.cleanup()

    def test_builds_four_courses_catalog_and_art_deterministically(self):
        entries = EXAMPLE.build(self.pack, self.root / 'one')
        EXAMPLE.build(self.pack, self.root / 'two')
        self.assertEqual((self.root / 'one/content.pack').read_bytes(),
                         (self.root / 'two/content.pack').read_bytes())
        self.assertEqual(json.loads((self.root / 'one/mod.json').read_text())['id'], 'custom-cup')
        built = track_format.load_pack(self.root / 'one/content.pack')
        self.assertEqual(len(built), len(entries))
        for course_id in range(128, 132):
            self.assertTrue(all((kind, course_id) in built for kind in track_format.REQUIRED_KINDS))
        catalog = built[(37, 4)]
        self.assertEqual(catalog[0]['version'], 2)
        data = catalog[1]
        self.assertEqual(struct.unpack_from('<2I', data), (0x31544143, 4))
        rows = [struct.unpack_from('<IBBHI', data, 8 + i * 12) for i in range(4)]
        self.assertEqual([(r[0], r[1], r[2], r[4]) for r in rows],
                         [(128 + i, 0, i, i) for i in range(4)])
        self.assertTrue(all(row[3] >= 1 for row in rows))
        sizes = {}
        for (kind, content_id), (_, payload) in built.items():
            if kind == 15:
                self.assertEqual(payload[:8], b'MKSCIMG1')
                sizes[content_id] = struct.unpack_from('<HH', payload, 12)
        expected = {0x2310F: (32, 32), 0x2320F: (64, 16)}
        for track in range(4):
            expected.update({0x23300 + 44 + track: (80, 64), 0x23400 + 44 + track: (96, 16),
                             0x23500 + 44 + track: (96, 16)})
        self.assertEqual(sizes, expected)

        # Painted folders: north start straight, finish tiles, walls, route spacing.
        for name in EXAMPLE.COURSES:
            folder = self.root / 'one/tracks' / name
            track = json.loads((folder / 'track.json').read_text())
            self.assertEqual((folder / 'ambient.bin').read_bytes(), b'AMB1' + bytes(range(64)))
            self.assertEqual(built[(84,track['id'])][1], (folder / 'ambient.bin').read_bytes())
            motion = json.loads((folder / 'sky-motion.json').read_text())
            sky_header,sky = built[(9,track['id'])]
            self.assertEqual(sky_header['version'],2)
            self.assertEqual(track_format.sky_motion(sky),motion)
            self.assertEqual((motion['controller'],motion['layer_source'],motion['road_mode']),(7,[0,2,1],3))
            width, _, tilemap, _ = png_indexed.read(folder / 'tilemap.png')
            self.assertEqual(width, 256)
            start, finish_y = track['start'], track['finish_y']
            self.assertEqual(start['y'], finish_y + 20)
            self.assertEqual(tilemap[(finish_y // 8 - 1) * 256 + start['x'] // 8], 0xFE)
            self.assertEqual(tilemap[(start['y'] // 8) * 256 + start['x'] // 8], 1)
            self.assertIn(3, tilemap)
            route = [(p['x'], p['y']) for p in track['route']]
            self.assertLessEqual(len(route), track_format.ROUTE_LIMIT)
            gaps = [((ax - bx) ** 2 + (ay - by) ** 2) ** 0.5
                    for (ax, ay), (bx, by) in zip(route, route[1:] + route[:1])]
            self.assertTrue(all(56 <= gap <= 96 for gap in gaps), (name, min(gaps), max(gaps)))
            self.assertEqual(route[-1][0], start['x'])
            self.assertLess(route[-1][1], finish_y)
            self.assertGreater(route[-2][1], start['y'])
            self.assertTrue(track['item_boxes'] and track['coins'])
            for box in track['item_boxes']:
                self.assertIn(tilemap[(box['y'] // 8) * 256 + box['x'] // 8], (1, 0xFE, *EXAMPLE.BOOST_TILES))

    def test_oval_edits_water_and_sky_through_pngs(self):
        EXAMPLE.build(self.pack, self.root / 'environment')
        folder = self.root / 'environment/tracks/oval'
        built = track_format.load_pack(self.root / 'environment/content.pack')
        materials = json.loads((folder / 'tileset.json').read_text())['materials']
        _, _, tilemap, _ = png_indexed.read(folder / 'tilemap.png')
        water = {tile for tile in tilemap if materials[tile] == 'water'}
        self.assertTrue(water)
        self.assertGreater(sum(tile in water for tile in tilemap), 10000)
        self.assertEqual(built[(6, 128)][1][8 + next(iter(water))], 0x22)
        base = track_format.load_pack(self.pack)
        self.assertNotEqual(built[(9, 128)][1], base[(9, 32)][1])
        self.assertNotEqual(built[(8, 128)][1], base[(8, 32)][1])
        # Recompiling the shipped editable folder retains both edited sections.
        compiled = {section['kind']: section['data'] for section in track_format.compile_track(folder, base)}
        self.assertEqual(compiled[8], built[(8, 128)][1])
        self.assertEqual(compiled[9], built[(9, 128)][1])

    def test_rejects_tight_corners_and_taken_ids(self):
        courses = self.root / 'courses'
        shutil.copytree(EXAMPLE.HERE / 'courses', courses)
        path = courses / 'oval.json'
        course = json.loads(path.read_text())
        course['corner_radius'] = 40
        path.write_text(json.dumps(course))
        with self.assertRaisesRegex(EXAMPLE.CourseError, 'radius'):
            EXAMPLE.build(self.pack, self.root / 'bad', courses)
        course['corner_radius'] = 192
        course['points'] = [[1448, 1648], [1448, 1100], [600, 1100], [600, 1648]]
        path.write_text(json.dumps(course))
        with self.assertRaisesRegex(EXAMPLE.CourseError, 'start straight'):
            EXAMPLE.build(self.pack, self.root / 'bad', courses)
        taken = self.root / 'taken'
        taken.mkdir()
        catalog = struct.pack('<2IIBBHI', 0x31544143, 1, 129, 0, 0, 1, 0)
        pack = write_pack(taken, template_sections() + [(37, 4, 2, catalog)])
        with self.assertRaisesRegex(EXAMPLE.CourseError, 'already appear'):
            EXAMPLE.build(pack, self.root / 'bad')

if __name__ == '__main__':
    unittest.main()
