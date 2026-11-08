"""The snow-weather SDK example is ROM-free and uses normal section overlays."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/portable'))
import build_asset_pack
SPEC = importlib.util.spec_from_file_location('snow_weather_example', ROOT / 'mods/examples/snow-weather/build.py')
EXAMPLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXAMPLE)


class WeatherModTests(unittest.TestCase):
    def test_authored_weather_pack_is_reproducible_and_course_scoped(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            data = EXAMPLE.build(root / 'one', course=27, count=5)
            EXAMPLE.build(root / 'two', course=27, count=5)
            self.assertEqual((root / 'one/content.pack').read_bytes(),
                             (root / 'two/content.pack').read_bytes())
            sections = list(build_asset_pack.read_sections(root / 'one/content.pack'))
            self.assertEqual(len(sections), 1)
            meta, actual = sections[0]
            self.assertEqual((meta['kind'], meta['content_id'], meta['version']), (87, 27, 1))
            self.assertEqual(bytes(actual), data)
            self.assertEqual(struct.unpack_from('<IIBB', data), (0x31485457, 1, 1, 5))
            self.assertEqual(struct.unpack_from('<3I', data, 28), (100, 100, 100))
            sizes = []
            for index in range(3):
                image = data[48+index*100:48+(index+1)*100]
                self.assertEqual(image[:8], b'MKSCIMG2')
                self.assertEqual(struct.unpack_from('<HHhh', image, 12), (8, 8, 0, 0))
                self.assertLessEqual(set(image[32:96]), {1, 255})
                sizes.append(sum(pixel != 255 for pixel in image[32:96]))
            self.assertEqual(sizes, [1, 5, 13])
            self.assertEqual(json.loads((root / 'one/mod.json').read_text())['id'], 'snow-weather')

    def test_invalid_authoring_options_fail_before_writing(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'invalid'
            for course, count in ((-1, 12), (192, 12), (24, 0), (24, 13), (24, True)):
                with self.assertRaises(ValueError):
                    EXAMPLE.build(output, course, count)
                self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
