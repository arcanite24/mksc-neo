import importlib.util
import pathlib
import struct
import sys
import unittest

TOOLS = pathlib.Path(__file__).parents[1]
sys.path.insert(0, str(TOOLS))
SCRIPT = TOOLS / "export_audio_sample.py"
SPEC = importlib.util.spec_from_file_location("export_audio_sample", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class AudioSampleExporterTests(unittest.TestCase):
    def test_pcm_header_and_payload(self):
        payload = bytes([0x80, 0, 1, 2, 0x7f])
        source = struct.pack("<HHIII", 0, 0x4000, 10512 << 10, 2,
                             len(payload) - 1) + payload
        output = MODULE.normalize_pcm(source, 133)
        self.assertEqual(output[:8], b"MKSCSMP1")
        self.assertEqual(struct.unpack_from("<H", output, 12)[0], 133)
        self.assertEqual(output[14:16], bytes([MODULE.PCM_S8, MODULE.LOOP]))
        self.assertEqual(struct.unpack_from("<III", output, 16),
                         (10512 << 10, len(payload), 2))
        self.assertEqual(output[40:], payload)

    def test_psg_and_malformed_source(self):
        output = MODULE.normalize_psg(bytes(range(16)), 0x8009)
        self.assertEqual(output[14], MODULE.PSG_WAVE4)
        self.assertEqual(struct.unpack_from("<I", output, 20)[0], 32)
        with self.assertRaisesRegex(MODULE.ExportError, "32 packed"):
            MODULE.normalize_psg(b"short", 1)
        with self.assertRaisesRegex(MODULE.ExportError, "size"):
            MODULE.normalize_pcm(struct.pack("<HHIII",0,0,10512<<10,0,9)+b"x",1)
        with self.assertRaisesRegex(MODULE.ExportError, "zero"):
            MODULE.normalize_pcm(struct.pack("<HHIII",0,0,1,0,0)+b"x",1)

    def test_fractional_q10_frequency_is_preserved(self):
        frequency = (50003 << 10) + 800
        source = struct.pack("<HHIII", 0, 0, frequency, 0, 0) + b"x"
        output = MODULE.normalize_pcm(source, 96)
        self.assertEqual(struct.unpack_from("<I", output, 16)[0], frequency)


if __name__ == "__main__":
    unittest.main()
