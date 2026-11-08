"""The ambient-cycle SDK example authors exact-index lanes from an owner pack."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

from test_track_format import stock_sections, write_pack, COURSE
import ambient_rom
import build_asset_pack

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location('ambient_cycle_example', ROOT / 'mods/examples/ambient-cycle/build.py')
EXAMPLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXAMPLE)


class AmbientModTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.seed = struct.pack('<128H', *[(i * 739) & 0x7fff for i in range(128)])
        builder = ambient_rom.Builder(self.seed)
        builder.lane([(7, [(ambient_rom.OBJECT, 101, bytes(4))])])
        self.pack = write_pack(self.root, stock_sections() + [(84, COURSE, 1, builder.build())])

    def tearDown(self):
        self.temporary.cleanup()

    def test_loop_scopes_durations_and_seed_are_deterministic(self):
        payload = EXAMPLE.build(self.pack, self.root / 'one', ticks=17)
        EXAMPLE.build(self.pack, self.root / 'two', ticks=17)
        self.assertEqual((self.root / 'one/content.pack').read_bytes(),
                         (self.root / 'two/content.pack').read_bytes())
        self.assertTrue(ambient_rom.validate(payload))
        magic, version, lanes, frames, patches, initial, size, seed = struct.unpack_from('<8I', payload)
        self.assertEqual((version, lanes, frames, patches, initial, seed), (1, 1, 3, 6, 0, 0))
        lane = struct.unpack_from('<4B4HI4I', payload, 32)
        self.assertEqual(lane, (ambient_rom.POST, ambient_rom.ENTRY, ambient_rom.TIMER,
                               ambient_rom.AUTO, 0, 3, 0, 0, 0, 0, 0, 0, 0))
        for i in range(3):
            self.assertEqual(struct.unpack_from('<HHBBH', payload, 64 + i * 8), (17, i * 2, 2, 0, 0))
        records = [struct.unpack_from('<BBHHHI', payload, 88 + i * 12) for i in range(6)]
        self.assertEqual([(p[0], p[2], p[3]) for p in records],
                         [(ambient_rom.SKY, 0, 63), (ambient_rom.OBJECT, 0, 48)] * 3)
        blob = 88 + 6 * 12
        self.assertEqual(payload[blob:blob + 256], self.seed)
        self.assertEqual(len({payload[blob+p[5]:blob+p[5]+p[3]*2] for p in records[::2]}), 3)
        entries = list(build_asset_pack.read_sections(self.root / 'one/content.pack'))
        self.assertEqual(len(entries), 1)
        self.assertEqual((entries[0][0]['kind'], entries[0][0]['content_id']), (84, COURSE))
        self.assertEqual(json.loads((self.root / 'one/mod.json').read_text())['id'], 'ambient-cycle')

    def test_invalid_parameters_and_missing_import_are_clear(self):
        for ticks in (0, 65536):
            with self.assertRaisesRegex(ValueError, 'ticks'):
                EXAMPLE.build(self.pack, self.root / 'bad', ticks=ticks)
        with self.assertRaisesRegex(ValueError, 'reimport'):
            EXAMPLE.build(self.pack, self.root / 'bad', course=25)


if __name__ == '__main__':
    unittest.main()
