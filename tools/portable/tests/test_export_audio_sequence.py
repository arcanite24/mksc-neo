import importlib.util
import pathlib
import unittest

SCRIPT = pathlib.Path(__file__).parents[1] / "export_audio_sequence.py"
SPEC = importlib.util.spec_from_file_location("export_audio_sequence", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class AudioSequenceExporterTests(unittest.TestCase):
    def test_running_status_waits_and_note_arguments(self):
        data = bytes([
            0xBC, 0, 0xBB, 75, 0xBD, 41, 0xC0, 32,
            0xD5, 81, 127, 0x81, 0xC0, 36, 0x81, 44,
            0x81, 52, 0x81, 60, 0x81, 64, 0x93, 0xB1,
        ])
        events = MODULE.TrackParser(data, lambda _: 0, 0).parse(0)
        self.assertEqual([event["tick"] for event in events],
                         [0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 24])
        self.assertEqual(events[4]["op"], MODULE.NOTE)
        self.assertEqual((events[4]["value"], events[4]["arg0"],
                          events[4]["arg1"]), (6, 81, 127))
        self.assertEqual([event["value"] for event in events[5:10]],
                         [36, 44, 52, 60, 64])

    def test_level_zero_pend_and_backward_loop(self):
        data = bytes([0xBC, 0, 0xB4, 0x81, 0xBE, 5, 0xB2, 0, 0, 0, 0])
        events = MODULE.TrackParser(data, lambda offset: 0, 0).parse(0)
        self.assertEqual([event["op"] for event in events],
                         [MODULE.KEY_SHIFT, MODULE.VOLUME, MODULE.LOOP])
        self.assertEqual(events[-1]["target_local"], 0)
        self.assertEqual(events[-1]["tick"], 1)

    def test_silent_wait_loop_gets_explicit_anchor(self):
        data = bytes([0xCF, 60, 127, 0xB0, 0xB0,
                      0xB2, 3, 0, 0, 0])
        events = MODULE.TrackParser(data, lambda _: 3, 0).parse(0)
        self.assertEqual([event["op"] for event in events],
                         [MODULE.TIE, MODULE.LOOP_POINT, MODULE.LOOP])
        self.assertEqual([event["tick"] for event in events], [0, 0, 192])
        self.assertEqual(events[-1]["target_local"], 1)

    def test_unsupported_and_unbounded_control_flow_reject(self):
        with self.assertRaisesRegex(MODULE.ExportError, "REPT"):
            MODULE.TrackParser(bytes([0xB5]), lambda _: 0, 0).parse(0)
        data = bytes([0xBC, 0, 0xB2, 0, 0, 0, 0, 0xB1])
        with self.assertRaisesRegex(MODULE.ExportError, "backward"):
            MODULE.TrackParser(data, lambda _: 7, 0).parse(0)
        with self.assertRaisesRegex(MODULE.ExportError, "MEMACC"):
            MODULE.TrackParser(bytes([0xB9, 1, 0, 0]), lambda _: 0, 0).parse(0)


if __name__ == "__main__":
    unittest.main()
