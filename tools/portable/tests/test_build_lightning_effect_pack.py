import importlib.util
import pathlib
import struct
import unittest


PATH=pathlib.Path(__file__).parents[1]/'build_lightning_effect_pack.py'
SPEC=importlib.util.spec_from_file_location('build_lightning_effect_pack',PATH)
MODULE=importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class TestBuildLightningEffectPack(unittest.TestCase):
    def test_payload_contract(self):
        rom=bytearray(MODULE.ROM_OFFSET+MODULE.TILE_BYTES)
        source=bytes((index*13+9)&0xff for index in range(MODULE.TILE_BYTES))
        rom[MODULE.ROM_OFFSET:]=source
        data=MODULE.payload(rom)
        self.assertEqual(struct.unpack_from('<4I',data),
                         (MODULE.MAGIC,0x800,4,0x1a0))
        self.assertEqual(data[16:],source)

    def test_short_rom_is_rejected(self):
        with self.assertRaisesRegex(ValueError,'too small'):
            MODULE.payload(b'bad')


if __name__=='__main__':
    unittest.main()
