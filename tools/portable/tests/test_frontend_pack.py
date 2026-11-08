import importlib.util
import pathlib
import struct
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).parents[1]
sys.path.insert(0, str(ROOT))
from pause_menu_rom import pause_sections, PAUSE_PANEL, PAUSE_CURSOR, PAUSE_LABEL, PAUSE_SELECTED_LABEL
from ui_rom import color
SPEC = importlib.util.spec_from_file_location('build_frontend_pack', ROOT / 'build_frontend_pack.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class FrontendPackTests(unittest.TestCase):
    def test_rejects_modified_rom(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            (root / 'bad.gba').write_bytes(b'bad')
            with self.assertRaisesRegex(ValueError, 'unmodified US ROM'):
                MODULE.build_rom(root / 'bad.gba', root / 'out.pack')
            self.assertFalse((root / 'out.pack').exists())

    def test_pause_import_keeps_selection_palette_and_cursor_flip(self):
        rom = bytearray(0xe1000)
        for address, layout, items in ((0xddf18,(10,4,10,12,5),0xddf10),
                                       (0xddef4,(10,4,10,8,3),0xddef1),
                                       (0xddf04,(10,5,10,6,2),0xddf00)):
            rom[address:address+5] = bytes(layout)
            struct.pack_into('<I',rom,address+8,0x8000000+items)
        rom[0xddf10:0xddf15] = bytes((0,4,2,3,1))
        rom[0xddf00:0xddf02] = bytes((0,1))
        rom[0xddee8:0xddef4] = bytes((4,0,1,2,3,5,4,5,6,0,5,1))
        # Synthetic glyphs, independent colors for the two source banks.
        struct.pack_into('<H',rom,0xaacf0+2,0x001f)
        struct.pack_into('<H',rom,0xaacf0+34,0x03e0)
        for tile in range(1,64):
            rom[0xaae3c+tile*32:0xaae3c+(tile+1)*32] = bytes((0x11,))*32
        for pattern in range(6):
            struct.pack_into('<14H',rom,0xddf58+28*pattern,*([0xf004]*14))
        # Upper cursor tile has one pixel, lower tile is its vertical flip.
        rom[0xaae3c+3*32:0xaae3c+4*32] = bytes((1,))+bytes(31)
        struct.pack_into('<2H',rom,0xde054,0xf003,0xf803)
        sections = {i: b for k,i,b in pause_sections(rom) if k == 15}
        self.assertEqual(len(sections),16)
        self.assertEqual(struct.unpack_from('<HH',sections[PAUSE_PANEL],12),(80,96))
        self.assertEqual(struct.unpack_from('<HH',sections[PAUSE_PANEL+1],12),(80,64))
        self.assertEqual(struct.unpack_from('<HH',sections[PAUSE_PANEL+2],12),(80,48))
        for row in range(6):
            self.assertEqual(struct.unpack_from('<I',sections[PAUSE_LABEL+row],32)[0],color(0x03e0))
            selected=sections[PAUSE_SELECTED_LABEL+row]
            self.assertEqual(selected[:8],b'MKSCIMG2')
            self.assertEqual(selected[32],1)
            width,height=struct.unpack_from('<HH',selected,12)
            self.assertEqual(struct.unpack_from('<H',selected,32+width*height+2*selected[32])[0],0x001f)
        cursor = struct.unpack_from('<128I',sections[PAUSE_CURSOR],32)
        self.assertEqual([i for i,pixel in enumerate(cursor) if pixel], [0,120])
        # The interior remains transparent over the paused race.
        panel = struct.unpack_from('<7680I',sections[PAUSE_PANEL],32)
        self.assertEqual(panel[48*80+40],0)
        self.assertEqual(sections,{i:b for k,i,b in pause_sections(rom) if k == 15})
        rom[0xddf1a] = 0
        with self.assertRaisesRegex(ValueError,'layout'):
            pause_sections(rom)


if __name__ == '__main__':
    unittest.main()
