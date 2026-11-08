"""Surface-trails authors ordinary overlays without committing source art."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

from test_track_format import write_pack
import build_asset_pack
import course_rom

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location('surface_trail_example', ROOT / 'mods/examples/surface-trails/build.py')
EXAMPLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXAMPLE)


class SurfaceTrailModTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.rules = course_rom.surface_trail_payload(None)
        self.art = []
        for group in range(5):
            count = 1 if group == 4 else 3
            frames = b''.join(struct.pack('<IHH', i*8, 8, 1) for i in range(count))
            palette = struct.pack('<80H', *[(i*791) & 0x7fff for i in range(80)])
            cells = b''.join(struct.pack('<4H', 1, 248, 16888, 0x5100+i) for i in range(count))
            self.art.append(struct.pack('<8I', 0x31435846, 0x2000, 80, count, 0x100, 3, count*8, 0)+frames+bytes(0x2000)+palette+cells)
        self.pack = write_pack(self.root, [(89, 1, 1, self.rules)] + [(19, 9+i, 1, data) for i, data in enumerate(self.art)])

    def tearDown(self):
        self.temporary.cleanup()

    def test_all_kinds_preserve_gates_cuts_cells_and_palette_indices(self):
        for kind in range(1, 6):
            destination = self.root / str(kind)
            rules = EXAMPLE.build(self.pack, destination, kind=kind, tint='cyan', spray_material=0x5a)
            self.assertEqual(rules[8:264], bytes([kind])*256)
            self.assertEqual(rules[:8], self.rules[:8])
            self.assertEqual(rules[264:276], self.rules[264:276])
            self.assertEqual(rules[276], 0x5a)
            entries = {(meta['kind'], meta['content_id']): bytes(data) for meta, data in build_asset_pack.read_sections(destination / 'content.pack')}
            self.assertEqual(len(entries), 6)
            self.assertEqual(entries[(89, 1)], rules)
            for group, source in enumerate(self.art):
                data = entries[(19, 9+group)]
                offset = 32+(1 if group == 4 else 3)*8+0x2000
                self.assertEqual(data[:offset], source[:offset])
                self.assertEqual(data[offset+160:], source[offset+160:])
                self.assertNotEqual(data[offset:offset+160], source[offset:offset+160])
            self.assertEqual(json.loads((destination / 'mod.json').read_text())['id'], 'surface-trails')
        EXAMPLE.build(self.pack, self.root / 'repeat', kind=5, tint='0,255,255', spray_material=0x5a)
        self.assertEqual((self.root / '5/content.pack').read_bytes(), (self.root / 'repeat/content.pack').read_bytes())

    def test_invalid_parameters_and_missing_source_fail_before_output(self):
        for kwargs in ({'kind': 0}, {'kind': 6}, {'spray_material': 256}, {'tint': '256,0,0'}, {'tint': '#xyzxyz'}):
            with self.assertRaises(ValueError):
                EXAMPLE.build(self.pack, self.root / 'bad', **kwargs)
        (self.root / 'missing').mkdir()
        missing = write_pack(self.root / 'missing', [(89, 1, 1, self.rules)])
        with self.assertRaisesRegex(ValueError, 'reimport'):
            EXAMPLE.build(missing, self.root / 'bad')
        self.assertFalse((self.root / 'bad').exists())
        broken = bytearray(self.rules);broken[264] = broken[265]
        with self.assertRaisesRegex(ValueError, 'cutoffs'):
            EXAMPLE.validate_rules(broken)
        self.assertEqual(EXAMPLE.tint_color('#00ffff'), (0, 31, 31))


if __name__ == '__main__':
    unittest.main()
