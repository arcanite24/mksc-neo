import importlib.util
import pathlib
import struct
import unittest


PATH=pathlib.Path(__file__).parents[1]/'build_gp_finish_banner_pack.py'
SPEC=importlib.util.spec_from_file_location('build_gp_finish_banner_pack',PATH)
MODULE=importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class TestBuildGpFinishBannerPack(unittest.TestCase):
    def test_literal_lz77_and_payload(self):
        raw=bytes((index*3)&0xff for index in range(MODULE.TILE_BYTES))
        encoded=bytearray(b'\x10'+len(raw).to_bytes(3,'little'))
        for start in range(0,len(raw),8):
            encoded.append(0)
            encoded.extend(raw[start:start+8])
        rom=bytearray(max(MODULE.CELLS_OFFSET+MODULE.CELL_BYTES,
                          MODULE.TILES_OFFSET+len(encoded)))
        rom[MODULE.TILES_OFFSET:MODULE.TILES_OFFSET+len(encoded)]=encoded
        rom[MODULE.PALETTE_OFFSET:MODULE.PALETTE_OFFSET+MODULE.PALETTE_BYTES]=\
            bytes(range(MODULE.PALETTE_BYTES))
        rom[MODULE.CELLS_OFFSET:MODULE.CELLS_OFFSET+MODULE.CELL_BYTES]=\
            bytes(range(0x10,0x20))
        value=MODULE.payload(bytes(rom))
        self.assertEqual(struct.unpack_from('<8I',value),
                         (MODULE.MAGIC,MODULE.TILE_BYTES,48,MODULE.CELL_BYTES,
                          MODULE.TILE_BASE,MODULE.ORIGIN_X,MODULE.ORIGIN_Y,0))
        self.assertEqual(value[32:32+MODULE.TILE_BYTES],raw)

    def test_bad_stream_is_rejected(self):
        with self.assertRaisesRegex(ValueError,'LZ77'):
            MODULE.decode_lz77(b'bad',0)


if __name__=='__main__':
    unittest.main()
