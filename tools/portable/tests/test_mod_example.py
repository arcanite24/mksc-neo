import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("neon_example", ROOT / "mods/examples/neon-night/build.py")
EXAMPLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXAMPLE)


class ModExampleTests(unittest.TestCase):
    def test_visual_sections_rebuild_without_game_data(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            road = struct.pack("<6I", 0x31444152, 64, 16, 0, 0, 0) + bytes(64) + bytes(32)
            driver = struct.pack("<8I", 0x33524143, 34 * 2048, 256, 64, 64, 0, 34, 64)
            driver += bytes(64 + 34 * 2048 + 25 * (512 + 128) + 256 * 2)
            (root / "road.bin").write_bytes(road)
            (root / "driver.bin").write_bytes(driver)
            (root / "base.json").write_text(json.dumps({"sections": [
                {"kind": 8, "content_id": 24, "path": "road.bin"},
                {"kind": 10, "content_id": 1, "version": 3, "path": "driver.bin"},
            ]}))
            EXAMPLE.BUILDER.build_pack(root / "base.json", root / "base.pack")
            for name in ("one", "two"):
                entries = EXAMPLE.build(root / "base.pack", root / name)
                self.assertEqual(len(entries), 2)
            self.assertEqual((root / "one/content.pack").read_bytes(), (root / "two/content.pack").read_bytes())
            self.assertNotEqual(road, (root / "one/8-24.bin").read_bytes())
            art = (root / "one/10-1.bin").read_bytes()
            self.assertEqual(art[96:96 + 2048], EXAMPLE.prism(64))
            self.assertNotEqual(driver, art)
            self.assertEqual(json.loads((root / "one/mod.json").read_text())["id"], "neon-night")
            damaged = bytearray((root / "base.pack").read_bytes())
            damaged[-1] ^= 1
            (root / "base.pack").write_bytes(damaged)
            with self.assertRaisesRegex(ValueError, "checksum"):
                EXAMPLE.build(root / "base.pack", root / "bad")
