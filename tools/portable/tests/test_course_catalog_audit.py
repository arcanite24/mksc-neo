import importlib.util
import pathlib
import struct
import unittest

SCRIPT = pathlib.Path(__file__).parents[1] / "audit_course_catalog.py"
SPEC = importlib.util.spec_from_file_location("audit_course_catalog", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class CourseCatalogAuditTests(unittest.TestCase):
    def make_blob(self, include_start=True):
        blob = bytearray(0x240)
        for field in MODULE.REQUIRED_OFFSET_FIELDS:
            struct.pack_into("<I", blob, field, 0x100)
        struct.pack_into("<I", blob, 0xCC, 0x100)
        blob[0x100] = 2
        struct.pack_into("<H", blob, 0x103, 0x20)
        struct.pack_into("<HH", blob, 0x120, 10, 20)
        struct.pack_into("<HH", blob, 0x128, 30, 40)
        struct.pack_into("<I", blob, 0x8C, 0x180)
        struct.pack_into("<I", blob, 0x94, 0x190)
        struct.pack_into("<I", blob, 0x98, 0x1A0)
        blob[0x180:0x188] = bytes((1, 2, 3, 4, 0, 0, 0, 0))
        blob[0x190:0x198] = bytes((2, 3, 4, 5, 0, 0, 0, 0))
        command = (0x8B, 10, 20, 1) if include_start else (0x81, 1, 1, 0)
        blob[0x1A0:0x1A8] = bytes((*command, 0, 0, 0, 0))
        return bytes(blob)

    def test_source_start_finish_formulas(self):
        row = MODULE.inspect_course(self.make_blob(), 24, 4, 0)
        self.assertEqual(row["status"], "compatible")
        self.assertEqual(row["checkpoint_count"], 2)
        self.assertEqual(row["start"], {"x": 120, "y": 188,
                                         "route_index": 1})
        self.assertEqual(row["finish_y"], 168)

    def test_missing_command_and_truncated_stream_block(self):
        row = MODULE.inspect_course(self.make_blob(False), 24, 4, 0)
        self.assertEqual(row["status"], "blocked")
        self.assertIn("start_finish_command_count_0", row["blockers"])
        broken = bytearray(self.make_blob())
        struct.pack_into("<I", broken, 0x98, len(broken) - 2)
        row = MODULE.inspect_course(bytes(broken), 24, 4, 0)
        self.assertEqual(row["status"], "blocked")
        self.assertIn("no terminator", row["blockers"][0])


if __name__ == "__main__":
    unittest.main()
