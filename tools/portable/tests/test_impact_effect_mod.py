"""Impact SDK overrides own all six scripts and synchronized lifetime rules."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

from test_track_format import write_pack
import build_asset_pack

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location('impact_effect_example', ROOT / 'mods/examples/impact-effects/build.py')
EXAMPLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXAMPLE)


class ImpactEffectModTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.rules = struct.pack('<II6H', 0x31584649, 6, 14, 14, 14, 14, 13, 13)
        self.art = []
        for variant in range(6):
            durations = (1, 1, 3, 2, 2 if variant < 4 else 1)
            cells = b''.join(struct.pack('<4H', 1, 248+frame, 16888+variant, 0x4100+frame) for frame in range(5))
            palette = struct.pack('<80H', *[(i*791) & 0x7fff for i in range(80)])
            frames = b''.join(struct.pack('<IHH', i*8, 8, duration) for i, duration in enumerate(durations))
            self.art.append(struct.pack('<8I', 0x31435846, 0x2000, 80, 5, 0x100, 3, 40, 0)+frames+bytes(0x2000)+palette+cells)
        self.pack = write_pack(self.root, [(91, 1, 1, self.rules)] + [(19, 17+i, 1, data) for i, data in enumerate(self.art)])

    def tearDown(self):
        self.temporary.cleanup()

    def test_palette_and_synchronized_lifetimes_preserve_all_six_anchors(self):
        rules = EXAMPLE.build(self.pack, self.root / 'one', tint='cyan', duration_percent=150)
        self.assertEqual(struct.unpack('<II6H', rules), (0x31584649, 6, 20, 20, 20, 20, 19, 19))
        entries = {(meta['kind'], meta['content_id']): bytes(data) for meta, data in build_asset_pack.read_sections(self.root / 'one/content.pack')}
        self.assertEqual(set(entries), {(91, 1)} | {(19, 17+i) for i in range(6)})
        for variant, source in enumerate(self.art):
            result = entries[(19, 17+variant)]
            self.assertEqual(result[:32], source[:32])
            self.assertEqual(result[-40:], source[-40:])
            self.assertEqual(result[72:72+0x2000], source[72:72+0x2000])
            self.assertNotEqual(result[72+0x2000:72+0x2000+160], source[72+0x2000:72+0x2000+160])
            self.assertEqual(sum(struct.unpack_from('<H', result, 32+i*8+6)[0]+1 for i in range(5)),
                             struct.unpack_from('<H', rules, 8+variant*2)[0])
            for i in range(5):
                self.assertEqual(result[32+i*8:38+i*8], source[32+i*8:38+i*8])
        EXAMPLE.build(self.pack, self.root / 'two', tint='#00ffff', duration_percent=150)
        self.assertEqual((self.root / 'one/content.pack').read_bytes(), (self.root / 'two/content.pack').read_bytes())
        self.assertEqual(json.loads((self.root / 'one/mod.json').read_text())['id'], 'impact-effects')
        short = EXAMPLE.build(self.pack, self.root / 'short', duration_percent=25)
        self.assertEqual(struct.unpack_from('<6H', short, 8), (10,)*6)

    def test_invalid_parameters_incomplete_import_and_mismatched_timing_fail(self):
        for kwargs in ({'duration_percent': 24}, {'duration_percent': 401}, {'tint': 'bad'}):
            with self.assertRaises(ValueError):
                EXAMPLE.build(self.pack, self.root / 'bad', **kwargs)
        self.assertFalse((self.root / 'bad').exists())
        bad = bytearray(self.art[0]);struct.pack_into('<H', bad, 38, 255)
        with self.assertRaisesRegex(ValueError, 'lifespan'):
            EXAMPLE.tint_art(bad, (0, 31, 31), 14)
        (self.root / 'missing').mkdir()
        missing = write_pack(self.root / 'missing', [(91, 1, 1, self.rules)])
        with self.assertRaisesRegex(ValueError, 'reimport'):
            EXAMPLE.build(missing, self.root / 'bad')


if __name__ == '__main__':
    unittest.main()
