"""ROM-free checks of normalized start-light composition."""
import pathlib
import struct
import sys
import unittest
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from countdown_rom import countdown_sections, item_art_intro_payload, START_IMAGE
from ui_rom import color


class CountdownTests(unittest.TestCase):
    def test_item_art_intro_preserves_authored_script_waits(self):
        rom=bytearray(0xEFD8C)
        addresses=(0xEFD7C,0xEFD84,0xEF248,0xEF258,0xEF278,0xEF290,0xEF2B0,0xEF2C8)
        waits=(3,7,11,13,17,19,23,8192)
        for address,wait in zip(addresses,waits):
            struct.pack_into('<II',rom,address,0xF1,wait)
        self.assertEqual(struct.unpack('<4sHH8H',item_art_intro_payload(rom)),(b'IAC1',8,0,*waits))
        for command,wait in ((0,3),(0xF1,0),(0xF1,8193)):
            struct.pack_into('<II',rom,addresses[0],command,wait)
            with self.assertRaisesRegex(ValueError,'intro wait'):
                item_art_intro_payload(rom)

    def test_images_keep_transparency_origins_and_source_body_frames(self):
        rom = bytearray(0xef800)
        # Invented finish script and cells exercise the strict source-program
        # path without storing any extracted graphics or script rows.
        commands = [(0x43, 0x0805045d)]
        for index in range(8):
            cell = 0x7c900 + index * 8
            struct.pack_into('<4H', rom, cell, 1, 253, (-index) & 511, 0x7140)
            if index == 1:
                commands.append((0x43, 0x08050df9))
            if index == 7:
                commands.append((0x43, 0x08050755))
            commands.extend(((0x11, 0x08000000 + cell), (0x41, 21 + index),
                             (0xf4, 0x0804fea1), (0xf1, 20 if index == 0 else 3)))
        commands.append((0x42, 0))
        for index, command in enumerate(commands):
            struct.pack_into('<2I', rom, 0xef578 + index * 8, *command)
        for address in (0x6b58c, 0x6b78c, 0x6b98c):
            rom[address:address + 512] = bytes([0x11]) * 512
            rom[address] = 0x10  # transparent first texel, opaque second texel
        struct.pack_into('<H',rom,0x627d6,0x03e0)
        rom[0x6500c:0x6524c]=bytes([0x11])*576
        rom[0x6500c]=0x10
        for index in range(16):
            struct.pack_into('<H', rom, 0x66ECC + index * 2, index)
        rom[0x68F8C:0x6918C] = bytes([0x11]) * 0x200
        rom[0x6918C:0x6938C] = bytes([0x22]) * 0x200
        rom[0x6938C:0x6970C] = bytes([0x33]) * 0x380
        # Invented replay dial map, distinct bank-C/OBJ-bank7 colors and
        # the source-style signed needle origin exercise both image paths.
        struct.pack_into('<H',rom,0x627b6,0x7c00)
        struct.pack_into('<30H',rom,0xee76c,*([0]+[0xc140]*29))
        rom[0x66c0c:0x66e4c]=bytes([0x11])*576
        rom[0x66c0c]=0x10
        struct.pack_into('<4H',rom,0xee764,1,224,0xc1e0,0x73c0)
        rom[0x6640c:0x66c0c]=bytes([0x11])*2048
        rom[0x6640c]=0x10
        sections = countdown_sections(rom)
        self.assertEqual([section[:2] for section in sections],
                         [(15, START_IMAGE + i) for i in range(7)] +
                         [(15, START_IMAGE + 16 + i) for i in range(2)] +
                         [(15, START_IMAGE + 32 + i) for i in range(4)] +
                         [(15, START_IMAGE + 48 + i) for i in range(8)] + [(15,0x27040),(15,0x27041),(15,0x27042)])
        for index, (_, _, payload) in enumerate(sections[:7]):
            self.assertEqual(struct.unpack_from('<HHhh', payload, 12),
                             (36, 37, 0, -5))
            pixels = struct.unpack_from('<1332I', payload, 32)
            self.assertEqual(pixels[0], 0)
            self.assertEqual(pixels[8], color(2 if index & 1 else 1))
            self.assertEqual(pixels[5 * 36], color(3))
        # Recovery body and both rod segments use independently decoded tiles.
        rom[0x6B18C:0x6B38C] = bytes([0x44]) * 512
        rom[0x6B38C:0x6B58C] = bytes([0x55]) * 512
        rom[0x6B08C:0x6B18C] = bytes([0x66]) * 256
        for index, (_, _, payload) in enumerate(countdown_sections(rom)[7:9]):
            self.assertEqual(struct.unpack_from('<HHhh', payload, 12), (32,64,-7,-28))
            pixels = struct.unpack_from('<2048I', payload,32)
            self.assertEqual(pixels[0],0)
            self.assertEqual(pixels[12*32],color(4+index))
            self.assertEqual(pixels[5],color(6))
            self.assertEqual(pixels[63*32+5],color(6))
        for bank,offset in enumerate((0x6A48C,0x6A78C,0x6AA8C,0x6AD8C)):
            rom[offset:offset+512]=bytes([0x11*(bank+1)])*512
            rom[offset+512:offset+768]=bytes([0x77])*256
        for bank,(_,_,payload) in enumerate(countdown_sections(rom)[9:13]):
            self.assertEqual(struct.unpack_from('<HHhh',payload,12),(33,36,-16,-16))
            pixels=struct.unpack_from('<1188I',payload,32)
            self.assertEqual(pixels[0],color(bank+1))
            self.assertEqual(pixels[35*33+1],color(7))
            self.assertEqual(pixels[35*33],0)
        for bank, (_, _, payload) in enumerate(countdown_sections(rom)[13:21]):
            self.assertEqual(struct.unpack_from('<HHhh', payload, 12), (8, 8, -bank, -3))
            pixels = struct.unpack_from('<64I', payload, 32)
            self.assertEqual(pixels[0], 0)
            self.assertEqual(pixels[1:], (color(1),) * 63)
        replay=sections[21][2]
        self.assertEqual(struct.unpack_from('<HHhh',replay,12),(72,16,0,0))
        pixels=struct.unpack_from('<1152I',replay,32)
        self.assertEqual(pixels[0],0)
        self.assertEqual(pixels[1:],(0xff00ff00,)*1151)
        dial=sections[22][2]
        self.assertEqual(struct.unpack_from('<HHhh',dial,12),(48,40,0,0))
        pixels=struct.unpack_from('<1920I',dial,32)
        self.assertEqual(pixels[0:8],(0,)*8)
        self.assertEqual(pixels[8:10],(0,0xff0000ff))
        needle=sections[23][2]
        self.assertEqual(struct.unpack_from('<HHhh',needle,12),(64,64,-32,-32))
        pixels=struct.unpack_from('<4096I',needle,32)
        self.assertEqual(pixels[0],0)
        self.assertEqual(pixels[1:],(color(1),)*4095)
        struct.pack_into('<H',rom,0xee76c,0xb140)
        with self.assertRaisesRegex(ValueError,'speedometer'):
            countdown_sections(rom)
        with self.assertRaisesRegex(ValueError, 'truncated'):
            countdown_sections(bytes(0x100))
