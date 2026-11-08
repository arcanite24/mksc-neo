import importlib.util
import pathlib
import sys
import unittest

TOOLS = pathlib.Path(__file__).parents[1]
sys.path.insert(0, str(TOOLS))
SCRIPT = TOOLS / "export_audio_voice_bank.py"
SPEC = importlib.util.spec_from_file_location("export_audio_voice_bank", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class FakeFlattener(MODULE.BankFlattener):
    def __init__(self):
        super().__init__(".")

    def resolve(self, symbol, index, key, stack=()):
        source = 10 if key < 64 else 11
        return (0, 60, 0, 64, 255, 0, 255, 0,
                MODULE.PCM, source, 0, 0)


class AudioVoiceBankExporterTests(unittest.TestCase):
    def test_sample_symbol_ids(self):
        self.assertEqual(MODULE.BankFlattener.sample_id("wave_133"), 134)
        self.assertEqual(MODULE.BankFlattener.sample_id("gbwave_009"), 0x8009)
        with self.assertRaisesRegex(MODULE.ExportError, "unsupported"):
            MODULE.BankFlattener.sample_id("other")

    def test_key_split_ranges_coalesce_and_drums_do_not(self):
        flat = FakeFlattener()
        first, count = flat.composite("bank", 0, False)
        self.assertEqual((first, count), (0, 2))
        self.assertEqual(flat.mappings[0][:2], [0, 63])
        self.assertEqual(flat.mappings[1][:2], [64, 127])
        drums = FakeFlattener()
        first, count = drums.composite("drums", 0, True)
        self.assertEqual((first, count), (0, 128))
        self.assertTrue(all(item[0] == item[1] for item in drums.mappings))


if __name__ == "__main__":
    unittest.main()
