import pathlib
import struct
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).parents[1]))
from ui_rom import affine_tile_map, palette_atlas, cell_image_indexed, image_asset, tile_map, cell_image, tile_pixel
from course_menu_rom import preview_panel_palette, locked_cup_palette, records_ghost_sections
from countdown_rom import finish_lakitu_program, countdown_sections
from gp_failure_menu_rom import failure_sections, GP_FAILURE_MENU
from pause_menu_rom import pause_sections, PAUSE_SELECTED_LABEL


class NativeUiImportTests(unittest.TestCase):
    def test_records_ghost_import_preserves_counts_anchors_and_schedule_holds(self):
        rom=bytearray(0xd9800)
        struct.pack_into('<H',rom,0xc96d4,4)
        for i,(x,tile) in enumerate(((-21,0x3a0),(-5,0x3a2),(1,0x3a3),(17,0x3a5))):
            struct.pack_into('<3H',rom,0xc96d6+i*6,0x40fc if i%2==0 else 0xfc,x&511,0xf000|tile)
        for i in range(2):struct.pack_into('<4H',rom,0x1000+i*8,1,253,510,0xf3e0)
        for table,count in ((0xd9684,2),(0xd9694,8),(0xd9724,2),(0xd9734,8),(0xd96d4,2),(0xd96e4,8)):
            for row in range(count):struct.pack_into('<2I',rom,table+row*8,0x08001000+(row%2)*8,row+1)
        palette=bytearray(512);struct.pack_into('<2H',palette,482,0x001f,0x03e0)
        counts=b''.join(bytes([0x11 if tile%2==0 else 0x22])*32 for tile in range(18))
        ghosts=bytes([0x10])+bytes([0x11])*1023
        sources={0xb2068:palette,0xb3424:counts,0xb2c38:ghosts}
        with patch('course_menu_rom.decode_lz77',side_effect=lambda _,address:sources[address]):
            sections=records_ghost_sections(rom)
        images={identity:data for kind,identity,data in sections if kind==15}
        self.assertEqual(len(images),17) # fifteen count images, two deduplicated cells
        self.assertEqual(struct.unpack_from('<HHhh',images[0x23860],12),(46,8,-21,-4))
        for identity,anchor in ((0x23861,(-21,-4)),(0x2386e,(1,-4))):
            self.assertEqual(struct.unpack_from('<HHhh',images[identity],12),(8,8,*anchor))
        self.assertEqual(struct.unpack_from('<I',images[0x23861],32)[0],0xffff0000)
        self.assertEqual(struct.unpack_from('<I',images[0x2386e],32)[0],0xff00ff00)
        self.assertEqual(struct.unpack_from('<HHhh',images[0x23880],12),(8,8,-2,-3))
        self.assertEqual(struct.unpack_from('<2I',images[0x23880],32),(0,0xffff0000))
        rules=sections[-1][2]
        self.assertEqual(len(rules),340)
        self.assertEqual(struct.unpack_from('<4I',rules),(0x31484752,1,6,0))
        self.assertEqual(struct.unpack_from('<I',rules,76)[0],2)
        self.assertEqual(struct.unpack_from('<IHH',rules,80),(0x23880,2,0))
        self.assertEqual(struct.unpack_from('<IHH',rules,88),(0x23881,3,0))
        struct.pack_into('<I',rom,0xd9688,65535)
        with patch('course_menu_rom.decode_lz77',side_effect=lambda _,address:sources[address]):
            with self.assertRaisesRegex(ValueError,'ghost frame'):records_ghost_sections(rom)

    def test_failure_credit_flag_uses_hud_palette_four_tiles_and_transparency(self):
        rom=bytearray(0xdb400)
        for address in (0xdb268,0xdb348):
            struct.pack_into('<2H',rom,address,12,6)
        colors=(0x7c1f,0x001f,0x03e0,0x7c00,0x7fff)
        struct.pack_into('<5H',rom,0x62794,*colors)
        for tile in range(4):
            rom[0x63c34+tile*32:0x63c54+tile*32]=bytes([(tile+1)*0x11])*32
        rom[0x63c34]=0x10
        # Unrelated result and Game Over banks cannot recolor the credit flag.
        rom[0x655ac:0x6562c]=bytes([0x77])*128
        with patch('gp_failure_menu_rom.decode_lz77',return_value=bytes(2048)):
            sections=failure_sections(rom)
        self.assertEqual([identity for _,identity,_ in sections],list(range(GP_FAILURE_MENU,GP_FAILURE_MENU+6)))
        flag=sections[-1][2]
        self.assertEqual(struct.unpack_from('<HHhh',flag,12),(16,16,0,0))
        pixels=struct.unpack_from('<256I',flag,32)
        self.assertEqual(pixels[0],0)
        self.assertEqual(pixels[1],0xffff0000)
        self.assertEqual(pixels[8],0xff00ff00)
        self.assertEqual(pixels[8*16],0xff0000ff)
        self.assertEqual(pixels[15*16+15],0xffffffff)
        self.assertEqual(sum(pixel!=0 for pixel in pixels),255)

    def test_failure_selected_palette_indices_keep_both_zero_colors_transparent(self):
        rom=bytearray(0xec000)
        for address in (0xdb268,0xdb348):
            struct.pack_into('<2H72H',rom,address,12,6,*([0xf100]*72))
        rom[0xaae3c:0xaae3c+32]=bytes([0x01])*32
        struct.pack_into('<H',rom,0xaacf0,0x7fff)
        struct.pack_into('<H',rom,0xaad10,0x001f)
        with patch('gp_failure_menu_rom.decode_lz77',return_value=bytes(2048)):
            image=failure_sections(rom)[0][2]
        self.assertEqual(image[:8],b'MKSCIMG2')
        self.assertEqual(struct.unpack_from('<I',image,20)[0],32)
        self.assertEqual(image[32],17)
        self.assertEqual(image[33],255)
        self.assertEqual(image[32+8*96+24],1)
        self.assertEqual(image[32+8*96+25],255)
        self.assertEqual(struct.unpack_from('<H',image,32+96*48)[0],0x7fff)
        self.assertEqual(struct.unpack_from('<H',image,32+96*48+32)[0],0x001f)

    def test_pause_selected_labels_keep_indices_for_private_palette_clock(self):
        rom=bytearray(0xe1000)
        for address,layout,items in ((0xddf18,(10,4,10,12,5),0xddf10),
            (0xddef4,(10,4,10,8,3),0xddef1),(0xddf04,(10,5,10,6,2),0xddf00)):
            rom[address:address+5]=bytes(layout)
            struct.pack_into('<I',rom,address+8,0x08000000+items)
        rom[0xddf10:0xddf15]=bytes((0,1,2,3,4))
        rom[0xddef1:0xddef4]=bytes((0,5,1));rom[0xddf00:0xddf02]=bytes((0,1))
        struct.pack_into('<14H',rom,0xddf58,*([0xf000]*14))
        rom[0xaae3c:0xaae3c+32]=bytes([0x01])*32
        struct.pack_into('<H',rom,0xaacf0,0x7fff)
        sections={identity:data for _,identity,data in pause_sections(rom)}
        for index in range(6):
            image=sections[PAUSE_SELECTED_LABEL+index]
            self.assertEqual(image[:8],b'MKSCIMG2')
            self.assertEqual(image[32:34],bytes((1,255)))
            self.assertEqual(struct.unpack_from('<H',image,32+56*16)[0],0x7fff)

    def test_finish_lakitu_images_preserve_each_source_cell_geometry(self):
        rom=bytearray(0xef800)
        struct.pack_into('<4H',rom,0xee764,1,0,0,0x73c0)
        struct.pack_into('<H',rom,0x66ece,0x001f)
        cells={21+i:0x100+i*8 for i in range(8)}
        for i in range(8):
            struct.pack_into('<4H',rom,cells[21+i],1,254,(-i)&511,0x7140)
        for address in (0x6b58c,0x6b78c,0x6b98c):
            rom[address:address+512]=bytes([0x11])*512
        with patch('countdown_rom.finish_lakitu_program',return_value=(b'',cells)):
            images={key:data for kind,key,data in countdown_sections(rom)}
        for i in range(8):
            image=images[0x27030+i]
            self.assertEqual(struct.unpack_from('<HHhh',image,12),(8,8,-i,-2))
            self.assertEqual(struct.unpack_from('<64I',image,32),(0xffff0000,)*64)

    def test_finish_lakitu_program_normalizes_holds_cells_and_phase_markers(self):
        # Invented durations and cells exercise data extraction independently of ROM art.
        commands=[(0x43,0x0805045d)]
        for bank in range(21,29):
            commands += [(0x11,0x08000100+(bank-21)*8),(0x41,bank),(0xf4,0x0804fea1)]
            if bank==22:commands += [(0x43,0x08050df9)]
            if bank==28:commands += [(0x43,0x08050755)]
            commands += [(0xf1,20 if bank==21 else 3),(0xf1,1)]
        commands += [(0x42,0)]
        rom=b''.join(struct.pack('<II',*command) for command in commands)+bytes(512)
        payload,cells=finish_lakitu_program(rom,0)
        self.assertEqual(struct.unpack_from('<4s4HI',payload),(b'FLK1',8,21,24,4,0))
        self.assertEqual(struct.unpack_from('<BBH',payload,16),(21,0,21))
        self.assertEqual(struct.unpack_from('<BBH',payload,44),(28,0,4))
        self.assertEqual(cells,{bank:0x100+(bank-21)*8 for bank in range(21,29)})
        bad=bytearray(rom);struct.pack_into('<I',bad,4,0x08000001)
        with self.assertRaisesRegex(ValueError,'callback'):
            finish_lakitu_program(bad,0)
        bad=bytearray(rom);struct.pack_into('<I',bad,20,29)
        with self.assertRaisesRegex(ValueError,'art bank'):
            finish_lakitu_program(bad,0)

    def test_locked_cup_palette_dims_each_bank_and_preserves_authored_special(self):
        rom=bytearray(0xb13b8)
        rom[0xb1398:0xb13b8]=bytes(range(32))
        for cup in range(5):
            palette=bytearray([99])*512
            start=0x100+cup*32
            struct.pack_into('<16H',palette,start,*([0x7fff,0x4210]*8))
            result=locked_cup_palette(rom,palette,cup)
            expected=bytes(range(32)) if cup==4 else struct.pack('<16H',*([0x3def,0x2108]*8))
            self.assertEqual(result[start:start+32],expected)
            self.assertEqual(result[:start],palette[:start])
            self.assertEqual(result[start+32:],palette[start+32:])
            self.assertEqual(len(result),512)
        with self.assertRaisesRegex(ValueError,'invalid locked'):
            locked_cup_palette(rom,bytes(512),5)

    def test_preview_panel_palette_uses_cup_table_and_preserves_other_colors(self):
        rom=bytearray(0xd94b4)
        for cup in range(5):
            struct.pack_into('<I',rom,0xd94a0+cup*4,0x08000100+cup*10)
            rom[0x100+cup*10:0x10a+cup*10]=bytes([cup+1])*10
            rom[0xae11c+cup*20:0xae12e+cup*20]=bytes([cup+10])*18
        for cup in range(5):
            palette=preview_panel_palette(rom,bytes([99])*512,cup)
            self.assertEqual(palette[0x1e0:0x1ea],bytes([cup+1])*10)
            self.assertEqual(palette[14:32],bytes([cup+10])*18)
            self.assertEqual(palette[0x1ea:],bytes([99])*22)
        struct.pack_into('<I',rom,0xd94a0,0x09000000)
        with self.assertRaisesRegex(ValueError,'outside ROM'):
            preview_panel_palette(rom,bytes(512),0)

    def test_affine_byte_map_retains_upper_tile_ids_and_full_palette(self):
        tiles = bytearray(256 * 64)
        tiles[129 * 64] = 81
        tiles[129 * 64 + 7] = 82
        palette = bytearray(512)
        struct.pack_into('<2H', palette, 81 * 2, 0x7FE0, 0x001F)
        cells = bytearray(16 * 16)
        cells[1] = 129
        width, height, pixels = affine_tile_map(tiles, palette, cells, 16)
        self.assertEqual((width, height), (128, 128))
        self.assertEqual(pixels[8], 0xFF00FFFF)
        self.assertEqual(pixels[15], 0xFFFF0000)
        self.assertEqual(pixels[9], 0)
        self.assertEqual(sum(pixel != 0 for pixel in pixels), 2)
        with self.assertRaisesRegex(ValueError, 'affine tile map size'):
            affine_tile_map(tiles, palette, cells[:-1], 16)
        with self.assertRaisesRegex(ValueError, 'exceeds graphics'):
            affine_tile_map(tiles[:64], palette, cells, 16)

    def test_cell_anchor_flip_and_transparency(self):
        rom = struct.pack("<4H", 1, 0x00FC, 0x11FE, 0)
        tiles = bytes([0x01]) + bytes(31)
        palette = bytes(2) + struct.pack("<H", 0x001F) + bytes(28)
        image = cell_image(rom, 0, tiles, palette)
        self.assertEqual(struct.unpack_from("<HHhh", image, 12), (8, 8, -2, -4))
        pixels = struct.unpack_from("<64I", image, 32)
        self.assertEqual(pixels[7], 0xFFFF0000)
        self.assertEqual(sum(p != 0 for p in pixels), 1)

    def test_palette_atlas_retains_anchor_and_each_source_color(self):
        rom=struct.pack('<4H',1,0x00FC,0x01FE,0)
        tiles=bytes([0x11])*32
        palettes=[bytes(2)+struct.pack('<H',color)+bytes(28) for color in (0x001F,0x7C00)]
        image=palette_atlas(rom,0,tiles,palettes)
        self.assertEqual(struct.unpack_from('<HHhh',image,12),(8,16,-2,-4))
        self.assertEqual(struct.unpack_from('<64I',image,32),(0xFFFF0000,)*64)
        self.assertEqual(struct.unpack_from('<64I',image,32+256),(0xFF0000FF,)*64)
        with self.assertRaisesRegex(ValueError,'frame count'):
            palette_atlas(rom,0,tiles,[])

    def test_indexed_cell_preserves_source_bank_and_transparency(self):
        rom = struct.pack("<4H", 1, 0x00FC, 0x11FE, 0x1000)
        tiles = bytes([0x21]) + bytes(31)
        palette = bytes(64)
        image = cell_image_indexed(rom, 0, tiles, palette, 32)
        self.assertEqual(image[:8], b"MKSCIMG2")
        self.assertEqual(struct.unpack_from("<HHhh", image, 12), (8, 8, -2, -4))
        self.assertEqual(struct.unpack_from("<I", image, 20)[0], 32 | 32 << 16)
        self.assertEqual(image[32:40], bytes([255]*6 + [50, 49]))
        self.assertEqual(image[96:], palette)
        with self.assertRaisesRegex(ValueError, "palette"):
            cell_image_indexed(rom, 0, tiles, palette, 100)

    def test_8bit_panorama_uses_full_palette(self):
        tiles = bytes([32]) + bytes(63)
        palette = bytes(64) + struct.pack("<H", 0x03E0) + bytes(446)
        self.assertEqual(tile_pixel(tiles, palette, 0, 0, 0, depth=8), 0xFF00FF00)
        self.assertEqual(tile_pixel(tiles, palette, 0, 1, 0, depth=8), 0)
        with self.assertRaisesRegex(ValueError, "exceeds graphics"):
            tile_pixel(tiles, palette, 1, 0, 0, depth=8)

    def test_text_map_blocks_and_flip_bits(self):
        cells = bytearray(64 * 32 * 2)
        struct.pack_into("<H", cells, 1024 * 2, 0x0400)
        tiles = bytes([0x01]) + bytes(31)
        palette = bytes(2) + struct.pack("<H", 0x7C00) + bytes(28)
        width, height, pixels = tile_map(tiles, palette, cells, 64, 32)
        self.assertEqual((width, height), (512, 256))
        self.assertEqual(pixels[256 + 7], 0xFF0000FF)
        self.assertEqual(pixels[256], 0)

    def test_image_size_and_source_bounds(self):
        with self.assertRaises(ValueError):
            image_asset(0, 1, [])
        with self.assertRaises(ValueError):
            image_asset(2, 1, [0])
        with self.assertRaisesRegex(ValueError, "cell list"):
            cell_image(bytes(2), 0, bytes(32), bytes(32))
        with self.assertRaisesRegex(ValueError, "tile map size"):
            tile_map(bytes(32), bytes(32), bytes(2), 32, 32)


if __name__ == "__main__":
    unittest.main()
