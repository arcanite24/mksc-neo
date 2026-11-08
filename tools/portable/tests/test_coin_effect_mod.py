"""Coin-effects overlays preserve script/cell identity while tuning data."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

from test_track_format import write_pack
import build_asset_pack

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location('coin_effect_example', ROOT / 'mods/examples/coin-effects/build.py')
EXAMPLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXAMPLE)


class CoinEffectModTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.rules = struct.pack('<4I18i7H2x', 0x31584643, 9, 3, 4,
                                 *range(-4, 5), *[i*65536 for i in range(9)], 256, 153, 102, 27, 72, 20, 5)
        self.art = []
        for count in (18, 5):
            frames = b''.join(struct.pack('<IHH', i*8, 2 if i == 3 else 8, 3) for i in range(count))
            cells = b''.join(struct.pack('<4H', 0 if i == 3 else 1, 248, 16888, 0x4100+i%4) for i in range(count))
            palette = struct.pack('<80H', *[(i*971) & 0x7fff for i in range(80)])
            self.art.append(struct.pack('<8I', 0x31435846, 0x2000, 80, count, 0x100, 3, count*8, 0)+frames+bytes(0x2000)+palette+cells)
        self.pack = write_pack(self.root, [(90, 1, 1, self.rules)] + [(19, 14+i, 1, data) for i, data in enumerate(self.art)])

    def tearDown(self):
        self.temporary.cleanup()

    def test_motion_volume_tint_and_authored_invisible_frames(self):
        rules = EXAMPLE.build(self.pack, self.root / 'one', tint='cyan', motion_percent=150, bounce_volume=180)
        self.assertEqual(struct.unpack_from('<9i', rules, 16), (-6, -4, -3, -1, 0, 1, 3, 4, 6))
        self.assertEqual(struct.unpack_from('<9i', rules, 52), tuple(i*98304 for i in range(9)))
        self.assertEqual(struct.unpack_from('<3H', rules, 88), (180,)*3)
        self.assertEqual(rules[:16], self.rules[:16])
        self.assertEqual(rules[94:], self.rules[94:])
        entries = {(meta['kind'], meta['content_id']): bytes(data) for meta, data in build_asset_pack.read_sections(self.root / 'one/content.pack')}
        self.assertEqual(set(entries), {(90, 1), (19, 14), (19, 15)})
        for i, source in enumerate(self.art):
            count = (18, 5)[i]
            palette = 32+count*8+0x2000
            result = entries[(19, 14+i)]
            self.assertEqual(result[:palette], source[:palette])
            self.assertEqual(result[palette+160:], source[palette+160:])
            self.assertNotEqual(result[palette:palette+160], source[palette:palette+160])
            self.assertEqual(struct.unpack_from('<H', result, 32+3*8+4)[0], 2)
        EXAMPLE.build(self.pack, self.root / 'two', tint='#00ffff', motion_percent=150, bounce_volume=180)
        self.assertEqual((self.root / 'one/content.pack').read_bytes(), (self.root / 'two/content.pack').read_bytes())
        self.assertEqual(json.loads((self.root / 'one/mod.json').read_text())['id'], 'coin-effects')

    def test_invalid_rule_and_art_boundaries_fail_before_output(self):
        for kwargs in ({'motion_percent': 0}, {'motion_percent': 401}, {'bounce_volume': 257}, {'tint': 'bad'}):
            with self.assertRaises(ValueError):
                EXAMPLE.build(self.pack, self.root / 'bad', **kwargs)
        self.assertFalse((self.root / 'bad').exists())
        bad = bytearray(self.rules);struct.pack_into('<i', bad, 16, -65)
        with self.assertRaisesRegex(ValueError, 'bounds'):
            EXAMPLE.validate_rules(bad)
        bad = bytearray(self.art[0]);struct.pack_into('<H', bad, 32+6, 4)
        with self.assertRaisesRegex(ValueError, 'lifespan'):
            EXAMPLE.tint_art(bad, (0, 31, 31), 72)
        bad = bytearray(self.art[0]);struct.pack_into('<I', bad, 12, 33)
        with self.assertRaisesRegex(ValueError, 'layout'):
            EXAMPLE.tint_art(bad, (0, 31, 31), 72)
        (self.root / 'missing').mkdir()
        missing = write_pack(self.root / 'missing', [(90, 1, 1, self.rules)])
        with self.assertRaisesRegex(ValueError, 'reimport'):
            EXAMPLE.build(missing, self.root / 'bad')


if __name__ == '__main__':
    unittest.main()
