import importlib.util
import json
import pathlib
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).parents[1] / "build_asset_pack.py"
SPEC = importlib.util.spec_from_file_location("build_asset_pack", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class AssetPackBuilderTests(unittest.TestCase):
    def test_rebuild_is_identical_and_sorted(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            (root / "a.bin").write_bytes(b"deterministic route")
            (root / "b.bin").write_bytes(b"presentation")
            manifest = root / "pack.json"
            manifest.write_text(json.dumps({"sections": [
                {"kind": 8, "content_id": 24, "flags": 0,
                 "path": "b.bin"},
                {"kind": 4, "content_id": 24, "flags": 3,
                 "semantic_revision": 2, "path": "a.bin"},
            ]}))
            first = root / "first.pack"
            second = root / "second.pack"
            first_ids = MODULE.build_pack(manifest, first)
            reordered = json.loads(manifest.read_text())
            reordered["sections"].reverse()
            manifest.write_text(json.dumps(reordered))
            second_ids = MODULE.build_pack(manifest, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(first_ids, second_ids)
            self.assertEqual(first.read_bytes()[:8], b"MKSCPACK")

    def test_duplicate_and_empty_sections_reject(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            (root / "empty.bin").write_bytes(b"")
            (root / "data.bin").write_bytes(b"x")
            manifest = root / "pack.json"
            manifest.write_text(json.dumps({"sections": [
                {"kind": 4, "content_id": 1, "path": "data.bin"},
                {"kind": 4, "content_id": 1, "path": "data.bin"},
            ]}))
            with self.assertRaisesRegex(ValueError, "duplicate"):
                MODULE.build_pack(manifest, root / "duplicate.pack")
            manifest.write_text(json.dumps({"sections": [
                {"kind": 4, "content_id": 1, "path": "empty.bin"},
            ]}))
            with self.assertRaisesRegex(ValueError, "empty"):
                MODULE.build_pack(manifest, root / "empty.pack")


if __name__ == "__main__":
    unittest.main()
