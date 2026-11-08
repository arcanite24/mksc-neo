"""ROM-free checks of source geometry and atomic pack construction."""
import pathlib
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import course_rom
import ambient_rom
import menu_rom
import import_rom
from rom_course_art import rom_grid
from ui_rom import color


def indexed_pixel(image, offset=0):
    width, height = struct.unpack_from('<HH', image, 12)
    count_and_base = struct.unpack_from('<I', image, 20)[0]
    index = image[32 + offset]
    if index == 255:
        return 0
    palette = 32 + width * height
    return color(struct.unpack_from('<H', image, palette + (index - (count_and_base >> 16)) * 2)[0])



class CourseSourceTests(unittest.TestCase):
    def test_hud_logical_page_palette_selectors_are_imported(self):
        rom=bytearray(0xef000)
        selectors=tuple(0xa000+(i%6)*0x1000 for i in range(17))
        struct.pack_into('<17H',rom,0xEE65C,*selectors)
        payload=course_rom.hud_graphics_payload(course_rom.Data(rom))
        self.assertEqual(struct.unpack_from('<I',payload,40)[0],17)
        self.assertEqual(struct.unpack('<17H',payload[-34:]),selectors)
        struct.pack_into('<H',rom,0xEE65C+15*2,0xc000)
        changed=course_rom.hud_graphics_payload(course_rom.Data(rom))
        self.assertEqual(changed[:-34],payload[:-34])
        self.assertEqual(struct.unpack_from('<H',changed,len(changed)-34+30)[0],0xc000)

    def test_minimap_scale_follows_source_header_and_directional_metadata(self):
        blob=bytearray(0x100)
        course=course_rom.Data(blob)
        self.assertEqual(course_rom.minimap_coordinate_shift(4,course),21)
        self.assertEqual(course_rom.minimap_coordinate_shift(32,course),20)
        blob[0xCA]=1
        self.assertEqual(course_rom.minimap_coordinate_shift(4,course),20)
        # Runtime custom IDs do not participate in this source metadata rule.
        self.assertEqual(course_rom.minimap_coordinate_shift(28,course),20)

    def test_time_trial_default_records_follow_course_data_and_catalog_eligibility(self):
        rom=bytearray(0xe7524)
        struct.pack_into('<I',rom,0xe7464,24)
        struct.pack_into('<I',rom,0x120,0x08000200)
        for row in range(6):
            struct.pack_into('<BBH',rom,0x200+row*4,row,0,20 if row==0 else 100+row*37)
        payload=course_rom.time_trial_record_defaults(course_rom.Data(rom),24,0x100)
        self.assertEqual(payload[:8],struct.pack('<II',0x31525454,6))
        self.assertEqual([struct.unpack_from('<BBH',payload,8+row*4) for row in range(6)],
                         [(row,0,100+row*37) for row in range(1,6)]+[(0,0,20)])
        # Uncatalogued private stages and arena headers never dereference their defaults.
        self.assertIsNone(course_rom.time_trial_record_defaults(course_rom.Data(rom),100,len(rom)))
        self.assertIsNone(course_rom.time_trial_record_defaults(course_rom.Data(rom),44,len(rom)))
        for offset,value in ((0x204,8),(0x205,1),(0x206,0),(0x20a,1)):
            bad=bytearray(rom);bad[offset]=value
            if offset==0x206:bad[offset+1]=0
            with self.assertRaisesRegex(ValueError,'record'):
                course_rom.time_trial_record_defaults(course_rom.Data(bad),24,0x100)

    def test_heading_cells_keep_anchors_reverse_targets_and_strip_digest(self):
        rom=bytearray(0xE0000);struct.pack_into('<H',rom,0xaf2c2,31)
        for group,table in enumerate((0xd90f8,0xd9230,0xd95bc,0xd9588)):
            full=0x1000+group*0x80
            struct.pack_into('<I',rom,table,0x08000000+full)
            struct.pack_into('<H6H',rom,full,2,0xff,0x1f0,0,2,5,0)
            for row in range(2):
                cell=0x2000+group*0x80+row*8
                struct.pack_into('<I',rom,table+4+row*4,0x08000000+cell)
                struct.pack_into('<4H',rom,cell,1,0xf8,0x1f8,0)
        with patch('menu_rom.decode_lz77',side_effect=lambda _,address:bytes([0x11])*32 if address==0xaf320 else b''):
            tail,glyphs=menu_rom.heading_sections(rom)
        self.assertEqual(struct.unpack_from('<II',tail),(0x3147484d,4))
        self.assertEqual(len(glyphs),8)
        offset=8
        for group in range(4):
            count,strip_id,digest=struct.unpack_from('<III',tail,offset);offset+=12
            self.assertEqual(count,2);self.assertNotEqual(digest,0)
            self.assertEqual(struct.unpack_from('<Ihh',tail,offset),(0x29000+group*32,133,26))
            self.assertEqual(struct.unpack_from('<Ihh',tail,offset+8),(0x29001+group*32,113,23))
            offset+=16
        self.assertEqual(struct.unpack_from('<hh',glyphs[0][2],16),(-8,-8))

    def test_finish_hud_tail_imports_each_dimensioned_record(self):
        rom=bytearray(0xDB254)
        frames=(0xDB0B2,0xDB0D6,0xDB0FA,0xDB11E,0xDB142,0xDB166,0xDB18A,0xDB1AE)
        for i,address in enumerate(frames):
            struct.pack_into('<HH16H',rom,address,4,4,*([i+1]*16))
            target=0x100+i*16
            struct.pack_into('<I',rom,0xDB234+i*4,0x08000000+target)
            struct.pack_into('<HH4H',rom,target,2,2,*([i+10]*4))
        payload=course_rom.gp_finish_hud_tail(course_rom.Data(rom))
        self.assertEqual(len(payload),384)
        self.assertEqual(struct.unpack_from('<HH16H',payload,7*36),(4,4,*([8]*16)))
        self.assertEqual(struct.unpack_from('<HH4H',payload,288+7*12),(2,2,*([17]*4)))
        struct.pack_into('<H',rom,frames[3],5)
        with self.assertRaisesRegex(ValueError,'frame dimensions'):
            course_rom.gp_finish_hud_tail(course_rom.Data(rom))

    def test_gp_opening_uses_catalog_cup_course_name_and_all_classes(self):
        rom=bytearray(0xEFD6C)
        struct.pack_into('<I',rom,0xE7464+9*4,24)
        struct.pack_into('<I',rom,0x100+0x30,0x08000200)
        struct.pack_into('<I',rom,0xEFD58+2*4,0x08000300)
        struct.pack_into('<I',rom,0xEFD44+2*4,0x08000400)
        rom[0x400:0x420]=bytes(range(32))
        rom[0x9AA4C:0x9AA4C+576]=b''.join(bytes([i+1])*192 for i in range(3))
        rom[0x76EB4:0x76F74]=bytes([7])*192
        rom[0x76F74:0x76FF4]=bytes([8])*128
        def decoded(_,offset):
            return {0x200:bytes([3])*768,0x300:bytes([4])*1472,0x76EA8:bytes([5])*32}[offset]
        with patch('course_rom.decode_lz77',side_effect=decoded):
            payload=course_rom.gp_opening_payload(course_rom.Data(rom),24,0x100)
        self.assertEqual(len(payload),5224)
        self.assertEqual(struct.unpack_from('<II',payload),(0x31544f47,0xa00))
        self.assertEqual(payload[8+0xe0:8+0x3e0],bytes([3])*768)
        self.assertEqual(payload[8+0x400:8+0x9c0],bytes([4])*1472)
        self.assertEqual(payload[-608:-576],bytes(range(32)))
        self.assertEqual(payload[-576:],b''.join(bytes([i+1])*192 for i in range(3)))
        # Private stages share normal course decoding but have no cup entry;
        # no class/name/palette stream may be requested for them.
        with patch('course_rom.decode_lz77',side_effect=AssertionError('private stage requested GP art')):
            self.assertIsNone(course_rom.gp_opening_payload(course_rom.Data(rom),100,0x100))
            self.assertIsNone(course_rom.gp_opening_payload(course_rom.Data(rom),101,0x100))

    def test_live_gp_hud_animation_tail_preserves_maps_and_palette_rows(self):
        rom=bytearray(0xEED80)
        for row,address in enumerate((0xEEBD0,0xEEC3C,0xEECA8,0xEED14)):
            struct.pack_into('<HH',rom,address,4,13)
            rom[address+4:address+108]=bytes([row+1])*104
        rom[0x64C7C:0x64C8C]=bytes(range(16))
        rom[0x64814:0x64834]=bytes(range(32))
        rom[0x64C8C:0x64C94]=bytes(range(200,208))
        for row,address in enumerate((0x64C94,0x64C9C,0x64CA4)):
            rom[address:address+6]=bytes([row+10])*6
        payload=course_rom.gp_race_hud_animation_tail(course_rom.Data(rom))
        self.assertEqual(len(payload),490)
        self.assertEqual(payload[:416],b''.join(bytes([row+1])*104 for row in range(4)))
        self.assertEqual(payload[416:432],bytes(range(16)))
        self.assertEqual(payload[432:464],bytes(range(32)))
        self.assertEqual(payload[464:482],b''.join(bytes([row+10])*6 for row in range(3)))
        self.assertEqual(payload[482:],bytes(range(200,208)))
        struct.pack_into('<H',rom,0xEEC3C,5)
        with self.assertRaisesRegex(ValueError,'transition dimensions'):
            course_rom.gp_race_hud_animation_tail(course_rom.Data(rom))

    def test_frontend_motion_import_preserves_authored_anchors_and_signed_sine(self):
        rom = bytearray(0xE745C)
        for slot in range(9):
            struct.pack_into('<2h', rom, 0xD91E8+slot*4, -300+slot, 280-slot)
            struct.pack_into('<2h', rom, 0xD9130+slot*20+12, 40+slot, 50-slot)
        for index, (table, count) in enumerate(((0xD8FE4,3),(0xD9094,3),(0xD90D0,2),(0xD905C,2))):
            for row in range(count):
                struct.pack_into('<2h', rom, table+row*20+12, index*10+row, -index-row)
        struct.pack_into('<5h', rom, 0xD8E88, -1, 2, -3, 4, -5)
        struct.pack_into('<3i', rom, 0xE645C, -32768, 32768, -12345)
        for slot in range(5):
            struct.pack_into('<2h',rom,0xD94FC+slot*20+12,48+slot*14,48+slot*22)
        payload = menu_rom.motion_payload(rom)
        self.assertEqual(len(payload), 4280)
        self.assertEqual(struct.unpack_from('<4I',payload), (0x314D4546,9,14,5))
        self.assertEqual(struct.unpack_from('<4h',payload,16+8*8), (-292,272,48,42))
        self.assertEqual(struct.unpack_from('<2h',payload,88+8*4), (30,-3))
        self.assertEqual(payload[144:164],rom[0xD8E88:0xD8E88+20])
        self.assertEqual(struct.unpack_from('<3i',payload,164), (-32768,32768,-12345))
        self.assertEqual(struct.unpack_from('<2h',payload,4260+4*4),(104,136))
        with self.assertRaisesRegex(ValueError, 'truncated'):
            menu_rom.motion_payload(rom[:-1])

    def test_weather_import_uses_behavior_and_course_bank_cells(self):
        rom = bytearray(0xEB39C)
        choices = (3, 2, 1, 0, 3, 2, 1, 0, 0, 1, 2, 3, 0, 1, 2, 3)
        struct.pack_into('<16H', rom, 0xEB37C, *choices)
        for index, cell in enumerate((0x7C2C8, 0x7C2C0, 0x7C2B8)):
            struct.pack_into('<4H', rom, cell, 1, 0xFE, 0x1FF, index)
        tiles = bytes([0x11])*32 + bytes([0x22])*32 + bytes([0x33])*32
        palette = struct.pack('<16H', *range(16))
        payload = course_rom.weather_payload(course_rom.Data(rom), 16, tiles, palette)
        self.assertEqual(struct.unpack_from('<IIBB', payload), (0x31485457, 1, 1, 12))
        self.assertEqual(tuple(payload[12:28]), choices)
        self.assertEqual(struct.unpack_from('<3I', payload, 28), (128, 128, 128))
        for index in range(3):
            image = payload[48+index*128:48+(index+1)*128]
            self.assertEqual(image[:8], b'MKSCIMG2')
            self.assertEqual(struct.unpack_from('<HHhh', image, 12), (8, 8, -1, -2))
            self.assertEqual(image[32:96], bytes([index+1])*64)
            self.assertEqual(image[96:], palette)
        disabled = course_rom.weather_payload(course_rom.Data(b''), 15, b'', b'')
        self.assertEqual(disabled, struct.pack('<II', 0x31485457, 1)+bytes(40))
        struct.pack_into('<H', rom, 0xEB37C, 4)
        with self.assertRaisesRegex(ValueError, 'weather spawn choice'):
            course_rom.weather_payload(course_rom.Data(rom), 16, tiles, palette)

    def test_ambient_script_preserves_empty_hold_and_long_duration(self):
        rom = bytearray(0xF1B40)
        commands = ((0xF1, 100), (0x41, 0), (0xF4, 0x0805A1E5),
                    (0xF1, 360), (0xF2, 0xFFFFFFFC))
        for index, command in enumerate(commands):
            struct.pack_into('<II', rom, 0x100+index*8, *command)
        struct.pack_into('<I', rom, 0xF1B2C, 0x08000200)
        struct.pack_into('<2H', rom, 0x200, 0x1234, 0x5678)
        builder = ambient_rom.Builder(bytes(256))
        builder.script(course_rom.Data(rom), 0x100)
        payload = builder.build()
        self.assertEqual(struct.unpack_from('<8I', payload),
                         (ambient_rom.MAGIC, 1, 1, 2, 1, 0, 260, 0))
        self.assertEqual(struct.unpack_from('<HHBBH', payload, 64), (100, 0, 0, 0, 0))
        self.assertEqual(struct.unpack_from('<HHBBH', payload, 72), (360, 0, 1, 0, 0))
        self.assertEqual(struct.unpack_from('<BBHHHI', payload, 80), (0, 0, 60, 2, 0, 256))
        self.assertEqual(payload[-4:], struct.pack('<2H', 0x1234, 0x5678))
        malformed = bytearray(payload)
        malformed[32+2] = 7
        with self.assertRaisesRegex(ValueError, 'ambient lane'):
            ambient_rom.validate(malformed)
        malformed = bytearray(payload)
        malformed[80+2] = 63
        with self.assertRaisesRegex(ValueError, 'ambient patch'):
            ambient_rom.validate(malformed)

    def test_ambient_palette_and_image_patch_units_are_distinct(self):
        builder = ambient_rom.Builder(bytes(256))
        builder.patch(ambient_rom.OBJECT, 103, struct.pack('<H', 0x4321), initial=True)
        builder.lane([(3, [(ambient_rom.SKY, 2, struct.pack('<2H', 1, 2)),
                           (ambient_rom.SKY_TILES, 0x1CA0, b'abc')])])
        payload = builder.build()
        self.assertEqual(struct.unpack_from('<8I', payload)[2:6], (1, 1, 2, 1))
        self.assertEqual(struct.unpack_from('<BBHHHI', payload, 72)[2:4], (2, 2))
        self.assertEqual(struct.unpack_from('<BBHHHI', payload, 84)[2:4], (0x1CA0, 3))
        self.assertEqual(struct.unpack_from('<BBHHHI', payload, 96)[2:4], (103, 1))
        with self.assertRaisesRegex(ValueError, 'unaligned'):
            builder.patch(ambient_rom.SKY, 0, b'abc')

    def test_ambient_callback_keeps_disjoint_palette_ranges(self):
        rom = bytearray(0xF1F64)
        struct.pack_into('<I', rom, 0xF1F44, 0x08000100)
        struct.pack_into('<I', rom, 0xF1F60, 0x08000140)
        rom[0x100:0x120] = bytes([17])*32
        rom[0x140:0x160] = bytes([33])*32
        patches = ambient_rom.callback_patches(course_rom.Data(rom), 0x0805A321, 0)
        self.assertEqual(patches, [(ambient_rom.SKY, 16, bytes([17])*32),
                                   (ambient_rom.SKY, 0, bytes([33])*32)])
        with self.assertRaisesRegex(ValueError, 'unsupported ambient palette callback'):
            ambient_rom.callback_patches(course_rom.Data(rom), 0x08000001, 0)

    def test_mushroom_rules_decode_source_instructions(self):
        rom=bytearray(0x482D0)
        for offset,value in ((0x3AABE,0x2032),(0x4814E,0x281d),(0x482CA,0x2196),
                             (0x482CC,0x0089),(0x38510,0x20c8),(0x38512,0x0380)):
            struct.pack_into('<H',rom,offset,value)
        struct.pack_into('<I',rom,0x3851C,0x07dfffff)
        kind,identity,data,version=course_rom.mushroom_rules_section(course_rom.Data(rom))
        self.assertEqual((kind,identity,version),(60,1,1))
        self.assertEqual(struct.unpack('<7I',data),(0x3153554d,5,50,30,600,50,0x07e00000))
        rom[0x482CC]=0
        with self.assertRaisesRegex(ValueError,'Thumb LSLS'):
            course_rom.mushroom_rules_section(course_rom.Data(rom))

    def test_gp_scoring_uses_rank_plus_one_source_table(self):
        rom = bytearray(0xECD5C)
        rom[0xDB0A8:0xDB0B1] = bytes([0,9,6,3,1,0,0,0,0])
        kind, identity, data, version = course_rom.gp_scoring_section(course_rom.Data(rom))
        self.assertEqual((kind,identity,version),(59,1,3))
        self.assertEqual(struct.unpack('<II8BI',data[:20]),(0x31545047,8,9,6,3,1,0,0,0,0,3))
        self.assertEqual(data[20:], rom[0xECD00:0xECD5C])
        rom[0xDB0A9] = 64
        with self.assertRaisesRegex(ValueError,'byte score budget'):
            course_rom.gp_scoring_section(course_rom.Data(rom))

    def test_gp_player_odds_preserve_order_and_pad_after_terminator(self):
        rom = bytearray(0xEDC34)
        offsets = (0xECE1C,0xECE50,0xECE84,0xECEB0,0xECEE4,0xECF10,
                   0xECF4C,0xECF88,0xECFC4,0xECFF8,0xED02C)
        for offset in offsets:
            struct.pack_into('<6H',rom,offset,0x1000,9,0x1000,1,0,255)
        for index in range(10):
            struct.pack_into('<HH',rom,0xECDF0+index*4,0x1000+index,31+index)
        struct.pack_into('<28i',rom,0xEDA04,*([3]*27+[0]))
        struct.pack_into('<16I',rom,0xEC8B4,*range(1,17))
        # Authored visible/hidden sequence, independent of the original script.
        script=[]
        for index in range(14): script.extend((0x11,index%2,0xF1,4))
        script.extend((0,0))
        struct.pack_into('<'+'I'*len(script),rom,0xEDB14,*script)
        sections = course_rom.gp_player_item_sections(course_rom.Data(rom))
        self.assertEqual([s[1] for s in sections],list(range(1,13)))
        self.assertEqual(len(sections[-1][2]),176)
        self.assertEqual(struct.unpack_from('<10H',sections[-1][2],8),tuple(range(0x1000,0x100A)))
        self.assertEqual(sections[-1][2][162:],bytes(255 if i%2==0 else 0 for i in range(14)))
        for kind, identity, data, version in sections[:-1]:
            self.assertEqual((kind,version,len(data)),(58,1,72))
            self.assertEqual(struct.unpack_from('<II6H',data),(0x31575047,16,0x1000,9,0x1000,1,0,255))
            self.assertEqual(data[20:],bytes(52))
        struct.pack_into('<H',rom,offsets[0]+10,254)
        with self.assertRaisesRegex(ValueError,'terminator'):
            course_rom.gp_player_item_sections(course_rom.Data(rom))

    def test_balloon_cell_normalizes_anchor_tiles_and_palette(self):
        rom=bytearray(0x7B53C)
        struct.pack_into('<4H',rom,0x7B534,1,0x00D7,0x41F8,0x449C)
        tiles=bytes(0x200)+bytes([0x11])*128
        struct.pack_into('<H',rom,0x66E4C+17*2,0x001F)
        with patch('course_rom.decode_lz77',return_value=tiles):
            payload=course_rom.battle_balloon_payload(course_rom.Data(rom))
        self.assertEqual(struct.unpack_from('<HHhh',payload,12),(16,16,-8,-41))
        self.assertEqual(indexed_pixel(payload),0xFFFF0000)
        struct.pack_into('<H',rom,0x7B534,0)
        with patch('course_rom.decode_lz77',return_value=tiles), self.assertRaisesRegex(ValueError,'cell list'):
            course_rom.battle_balloon_payload(course_rom.Data(rom))

    def test_battle_elimination_duration_is_imported_and_checked(self):
        rom = bytearray(0xF2C54)
        rom[0xF2C50:0xF2C54] = bytes((1,10,0,0))
        self.assertEqual(course_rom.battle_elimination_rules(course_rom.Data(rom)),struct.pack('<I',10))
        for value in ((1,0,0,0),(1,128,0,0),(2,10,0,0),(1,10,1,0),(1,10,0,1)):
            rom[0xF2C50:0xF2C54] = bytes(value)
            with self.assertRaisesRegex(ValueError,'Battle elimination animation'):
                course_rom.battle_elimination_rules(course_rom.Data(rom))

    def test_battle_odds_and_hidden_box_timer(self):
        rom = bytearray(0xED0A8)
        rows = (((0x1007,10),(0,255)), ((0x1002,25),(0x100a,3),(0,255)),
                ((0x1002,35),(0x100a,5),(0x1005,10),(0x1000,25),(0,255)))
        for offset, table in zip((0xED080,0xED06C,0xED058), rows):
            for index, row in enumerate(table):
                struct.pack_into('<HH',rom,offset+index*4,*row)
        struct.pack_into('<H',rom,0x43A6A,0x2078)
        rules = course_rom.battle_item_rules(course_rom.Data(rom))
        self.assertEqual(len(rules),124)
        self.assertEqual(struct.unpack_from('<I',rules,120)[0],120)
        for index, table in enumerate(rows):
            expected = b''.join(struct.pack('<HH',*row) for row in table)
            self.assertEqual(rules[index*40:index*40+len(expected)],expected)
            self.assertEqual(rules[index*40+len(expected):(index+1)*40],bytes(40-len(expected)))
        struct.pack_into('<H',rom,0xED058,0x100d)
        with self.assertRaisesRegex(ValueError,'Battle roulette item'):
            course_rom.battle_item_rules(course_rom.Data(rom))
        struct.pack_into('<H',rom,0xED058,0x1002)
        struct.pack_into('<H',rom,0xED058+18,0)
        with self.assertRaisesRegex(ValueError,'Battle roulette terminator'):
            course_rom.battle_item_rules(course_rom.Data(rom))

    def test_directional_arena_grid_uses_four_source_headings(self):
        rom=bytearray(0x258800)
        struct.pack_into('<I',rom,0xE7FEC+24*4,0x08000100)
        struct.pack_into('<I',rom,0x100,0)
        struct.pack_into('<2I',rom,0x258000,0x100,0x500)
        struct.pack_into('<I',rom,0x258100+0x98,0x100)
        rom[0x258100+0xCA]=1
        struct.pack_into('<4H',rom,0xE745C,0,0x8000,0x4000,0xC000)
        for rank in range(8):
            struct.pack_into('<4B',rom,0x258200+rank*4,0x81+rank,20+rank,40+rank,3)
        _,rows=rom_grid(rom,24,4)
        self.assertEqual(rows,[((164+i*8)<<16,(324+i*8)<<16,h)
                               for i,h in enumerate((0,0x8000,0x4000,0xC000))])
        with self.assertRaisesRegex(ValueError,'four player starts'):
            rom_grid(rom,24)
        rom[0x258200+12]=0x82
        with self.assertRaisesRegex(ValueError,'duplicate grid'):
            rom_grid(rom,24,4)

    def test_extra_grid_preserves_source_uncentered_x(self):
        rom=bytearray(0x258800)
        struct.pack_into('<I',rom,0xE7FEC+32*4,0x08000100)
        struct.pack_into('<2I',rom,0x258000,0x100,0x500)
        struct.pack_into('<I',rom,0x258100+0x98,0x100)
        for rank in range(8):
            struct.pack_into('<4B',rom,0x258200+rank*4,0x81+rank,20,40,3)
        self.assertEqual(rom_grid(rom,32)[1],[(160<<16,324<<16,0)]*8)

    def test_final_directory_grid_ends_at_container_boundary(self):
        rom=bytearray(0x258000+0xB0000)
        struct.pack_into('<I',rom,0xE7FEC+28*4,0x08000100)
        struct.pack_into('<I',rom,0x100,48)
        struct.pack_into('<2I',rom,0x258000+48*4,0xAFE00,0xDEADBEEF)
        start=0x258000+0xAFE00
        struct.pack_into('<I',rom,start+0x98,0x100)
        for rank in range(8):
            struct.pack_into('<4B',rom,start+0x100+rank*4,0x81+rank,20+rank,40+rank,3)
        self.assertEqual(rom_grid(rom,28),(48,[((164+i*8)<<16,(324+i*8)<<16,0) for i in range(8)]))
        struct.pack_into('<I',rom,0x100,49)
        with self.assertRaisesRegex(ValueError,'outside directory'):
            rom_grid(rom,28)

    def test_route_sentinel_reads_across_course_directory_boundary(self):
        # The first course ends at1200; rows1/2 address the next ROM entry.
        data = bytearray(1800)
        for offset, x, y in ((1116, 7, 9), (1396, 11, 13), (1676, 17, 19)):
            struct.pack_into('<HH', data, offset, x, y)
        payload = course_rom.route_sentinel_targets(course_rom.Data(data), 60, 40, 35)
        self.assertEqual(struct.unpack('<7I', payload),
                         (7, 56, 72, 88, 104, 136, 152))
        # An actual container boundary remains unavailable, never zero padded.
        truncated = course_rom.route_sentinel_targets(course_rom.Data(data[:1500]), 60, 40, 35)
        self.assertEqual(struct.unpack('<7I', truncated),
                         (3, 56, 72, 88, 104, 0, 0))

    def test_mushroom_particle_flattens_source_cells_and_timing(self):
        rom = bytearray(0xF3000)
        for index in range(12):
            cell = 0x100 + index * 64
            struct.pack_into('<H', rom, cell, 7)
            struct.pack_into('<II', rom, 0xF2F7C + index * 8,
                             0x08000000 + cell, 4 if index in (2, 9) else 3)
        payload = course_rom.race_particle_payload(course_rom.Data(rom), 0xF2F7C)
        header = struct.unpack_from('<8I', payload)
        self.assertEqual(header, (0x31435846, 0x2000, 80, 12, 0x100, 3, 528, 0))
        frames = [struct.unpack_from('<IHH', payload, 32 + i * 8) for i in range(12)]
        self.assertEqual(sum(frame[2] + 1 for frame in frames), 50)
        self.assertEqual(frames[-1], (484, 44, 3))
        struct.pack_into('<H', rom, 0x100, 100)
        with self.assertRaisesRegex(ValueError, 'cells exceed'):
            course_rom.race_particle_payload(course_rom.Data(rom), 0xF2F7C)

    def test_finish_schedule_resolves_relative_jump_rows(self):
        rom = bytearray(0xF2C14)
        for driver in range(8):
            address = 0x100+driver*32
            struct.pack_into('<I', rom, 0xF2BF4+driver*4, 0x08000000+address)
            # Invented donor schedule loops its final art row, omitting the jump.
            rom[address:address+16] = bytes((24,3,0,0,30,5,0,0,255,255,0,0,0,0,0,0))
        payload = course_rom.finish_payload(course_rom.Data(rom))
        self.assertEqual(struct.unpack_from('<II4HB', payload),
                         (0x314E4946,8,1024,32768,80,220,3))
        self.assertEqual(len(payload),116)
        for driver in range(8):
            offset = 20+driver*12
            self.assertEqual(struct.unpack_from('<I',payload,offset)[0],2)
            self.assertEqual(payload[offset+4:offset+12],bytes((24,3,1,0,30,5,1,0)))
        rom[0x109] = 0
        with self.assertRaisesRegex(ValueError,'finish schedule jump'):
            course_rom.finish_payload(course_rom.Data(rom))
        rom[0x109] = 255
        rom[0x105] = 128
        with self.assertRaisesRegex(ValueError,'finish schedule row'):
            course_rom.finish_payload(course_rom.Data(rom))
        rom[0x105] = 5
        rom[0x106] = 1
        with self.assertRaisesRegex(ValueError,'finish schedule padding'):
            course_rom.finish_payload(course_rom.Data(rom))

    def test_driver_view_limits_preserve_source_rows_and_bounds(self):
        rom = bytearray(0xEC864)
        response, speed = bytes(range(15)), bytes(range(14,-1,-1))
        rom[0xEC846:0xEC864] = response + speed
        payload = course_rom.driver_view_payload(course_rom.Data(rom))
        self.assertEqual(len(payload), 38)
        self.assertEqual(struct.unpack_from('<II', payload), (0x31525644, 15))
        self.assertEqual(payload[8:], response+speed)
        for address in (0xEC846, 0xEC863):
            old = rom[address]
            rom[address] = 19
            with self.assertRaisesRegex(ValueError, 'driver view limit'):
                course_rom.driver_view_payload(course_rom.Data(rom))
            rom[address] = old

    def test_driver_art_normalizes_spans_and_refresh_policy(self):
        rom = bytearray(0xEC846)
        spans = tuple(index * 7 for index in range(16))
        source_masks = tuple(0 if index % 3 else 0xFFFFFFFF for index in range(16))
        struct.pack_into('<16H', rom, 0xEC826, *spans)
        struct.pack_into('<16I', rom, 0xEC2C0, *source_masks)
        payload = course_rom.driver_art_payload(course_rom.Data(rom))
        self.assertEqual(len(payload), 56)
        self.assertEqual(struct.unpack_from('<II16H', payload), (0x31524144, 16, *spans))
        self.assertEqual(payload[40:], bytes(7 if mask == 0 else 3 for mask in source_masks))
        struct.pack_into('<H', rom, 0xEC826, 2048)
        with self.assertRaisesRegex(ValueError, 'driver art speed span'):
            course_rom.driver_art_payload(course_rom.Data(rom))
        struct.pack_into('<H', rom, 0xEC826, 0)
        struct.pack_into('<I', rom, 0xEC2C0, 1)
        with self.assertRaisesRegex(ValueError, 'driver art refresh mask'):
            course_rom.driver_art_payload(course_rom.Data(rom))

    def test_driver_projection_normalizes_source_cell_geometry(self):
        rom = bytearray(0xECDE6)
        rom[0xECDDC:0xECDDF] = bytes((210, 233, 245))
        struct.pack_into('<3H', rom, 0xECDE0, 0xC1E0, 0x81F0, 0x41F8)
        payload = course_rom.driver_projection_payload(course_rom.Data(rom))
        self.assertEqual(struct.unpack_from('<II', payload), (0x31525044, 3))
        self.assertEqual(len(payload), 44)
        self.assertEqual([struct.unpack_from('<HHhhBBH', payload, 8 + index * 12)
                          for index in range(3)],
                         [(511, 64, -32, -46, 1, 0, 0),
                          (1023, 32, -16, -23, 1, 0, 0),
                          (4096, 16, -8, -11, 1, 0, 0)])
        # Anchors remain data, independent of the source art-size validation.
        rom[0xECDDC] = 209
        self.assertEqual(struct.unpack_from('<h', course_rom.driver_projection_payload(
            course_rom.Data(rom)), 14)[0], -47)
        for attr in (0x81E0, 0xC3E0):
            struct.pack_into('<H', rom, 0xECDE0, attr)
            with self.assertRaisesRegex(ValueError, 'driver projection cell'):
                course_rom.driver_projection_payload(course_rom.Data(rom))

    def test_driver_affine_tables_normalize_axes_and_exclude_terminators(self):
        rom = bytearray(0xF2D00)
        addresses = (0xF2AC0, 0xF2C54, 0xF2C80, 0xF2C86, 0xF2C8C)
        for address, rows in zip(addresses, (((124,132,1),(128,128,2)),
                                            ((192,192,1),),((224,3,110),),
                                            ((224,3,18),),((112,192,112),(128,128,1)))):
            for index, row in enumerate(rows):
                rom[address + index * 3:address + index * 3 + 3] = bytes(row)
        payload = course_rom.driver_affine_payload(course_rom.Data(rom))
        self.assertEqual(struct.unpack_from('<2I', payload), (0x31524641,5))
        offset, actual = 8, []
        for table in range(5):
            mode, count = struct.unpack_from('<2I', payload, offset)
            offset += 8
            rows = [struct.unpack_from('<HHBB',payload,offset+index*6) for index in range(count)]
            offset += count * 6
            actual.append((mode,rows))
        self.assertEqual(actual, [(1,[(248,264,1,0),(256,256,2,0)]),
                                  (4,[(384,384,1,0)]),(5,[(224,384,110,0)]),
                                  (6,[(224,384,18,0)]),(7,[(224,384,112,0),(256,256,1,0)])])
        self.assertEqual(offset,len(payload))
        for row in ((0,128,1),(128,128,128)):
            rom[0xF2AC0:0xF2AC3] = bytes(row)
            with self.assertRaisesRegex(ValueError,'invalid driver affine'):
                course_rom.driver_affine_payload(course_rom.Data(rom))
        rom[0xF2AC0:0xF2AC3] = bytes(3)
        with self.assertRaisesRegex(ValueError,'empty driver affine'):
            course_rom.driver_affine_payload(course_rom.Data(rom))
        rom[0xF2AC0:0xF2AC0+99] = bytes((128,128,1))*33
        with self.assertRaisesRegex(ValueError,'invalid driver affine'):
            course_rom.driver_affine_payload(course_rom.Data(rom))

    def test_impact_scripts_preserve_paired_art_and_authored_lifetimes(self):
        rom = bytearray(0xF2DDC)
        for variant, address in enumerate(course_rom.IMPACT_SEQUENCES):
            for frame in range(5):
                cell = 0x100+variant*200+frame*32
                struct.pack_into('<H', rom, cell, variant%3+1)
                struct.pack_into('<II', rom, address+frame*8, 0x08000000+cell, variant+1)
        rules = course_rom.impact_effect_payload(course_rom.Data(rom))
        self.assertEqual(struct.unpack('<II6H', rules), (0x31584649, 6, 10, 15, 20, 25, 30, 35))
        for variant, address in enumerate(course_rom.IMPACT_SEQUENCES):
            art = course_rom.race_particle_payload(course_rom.Data(rom), address)
            size = 2+(variant%3+1)*6
            self.assertEqual(struct.unpack_from('<I', art, 12)[0], 5)
            self.assertEqual(struct.unpack_from('<I', art, 24)[0], size*5)
            self.assertEqual([struct.unpack_from('<IHH', art, 32+i*8) for i in range(5)],
                             [(i*size, size, variant+1) for i in range(5)])
        struct.pack_into('<II', rom, course_rom.IMPACT_SEQUENCES[2], 0x08000100, 0)
        with self.assertRaisesRegex(ValueError, 'particle sequence'):
            course_rom.impact_effect_payload(course_rom.Data(rom))

    def test_coin_effect_rules_preserve_signed_motion_and_script_lifetimes(self):
        rom = bytearray(0xF2F7C)
        scripts = (0x100, 0x300, 0xF2EB4, 0xF2EE4)
        for slot in range(9):
            struct.pack_into('<I', rom, 0xF2F04+slot*4, 0x08000000+scripts[slot != 0])
        horizontal = tuple(range(-4, 5))
        upward = tuple(i*65536 for i in range(9))
        struct.pack_into('<18i3H', rom, 0xF2F2C, *horizontal, *upward, 0, 153, 256)
        for address, frames in zip(scripts, (7, 18, 5, 1)):
            for index in range(frames):
                # Alternate authored visible and zero-cell frames.
                cell = 0x700 if index & 1 else 0x710
                struct.pack_into('<II', rom, address+index*8, 0x08000000+cell, 1+index%4)
        struct.pack_into('<H', rom, 0x700, 1)
        payload = course_rom.coin_effect_payload(course_rom.Data(rom))
        self.assertEqual(len(payload), 104)
        self.assertEqual(struct.unpack_from('<4I18i3H', payload), (0x31584643, 9, 3, 4, *horizontal, *upward, 0, 153, 256))
        ticks = tuple(sum(2+i%4 for i in range(count)) for count in (7, 18, 5, 1))
        self.assertEqual(struct.unpack_from('<4H', payload, 94), ticks)
        art = course_rom.race_particle_payload(course_rom.Data(rom), scripts[1])
        self.assertEqual(struct.unpack_from('<I', art, 12)[0], 18)
        self.assertEqual([struct.unpack_from('<H', art, 32+i*8+4)[0] for i in range(4)], [2, 8, 2, 8])
        struct.pack_into('<i', rom, 0xF2F2C, -65)
        with self.assertRaisesRegex(ValueError, 'motion'):
            course_rom.coin_effect_payload(course_rom.Data(rom))
        struct.pack_into('<i', rom, 0xF2F2C, 0)
        struct.pack_into('<H', rom, 0xF2F74, 257)
        with self.assertRaisesRegex(ValueError, 'volume'):
            course_rom.coin_effect_payload(course_rom.Data(rom))
        struct.pack_into('<H', rom, 0xF2F74, 0)
        struct.pack_into('<I', rom, 0xF2F24, 0x08000100)
        with self.assertRaisesRegex(ValueError, 'variant scripts'):
            course_rom.coin_effect_payload(course_rom.Data(rom))

    def test_surface_trail_rules_and_art_keep_material_variants_and_order(self):
        rules = course_rom.surface_trail_payload(course_rom.Data(bytes()))
        self.assertEqual(len(rules), 280)
        self.assertEqual(struct.unpack_from('<II', rules), (0x314C5254, 256))
        self.assertEqual([rules[8+i] for i in (0, 0x54, 0x5A, 0x5C, 0x58, 255)], [1, 2, 3, 4, 5, 1])
        self.assertEqual(rules[264:], bytes([9, 19]*3+[5, 11]*2+[0x50, 0x50, 0x5C, 0, 0, 0]))
        groups = ((0x7AFCC, 0x7AFD4, 0x7AFDC), (0x7AFFC, 0x7B004, 0x7B00C),
                  (0x7B014, 0x7B01C, 0x7B024), (0x7B044, 0x7B04C, 0x7B054), (0x7AF14,))
        rom = bytearray(0x7B060)
        rom[0x66F0C:0x66F10] = b'ART!'
        rom[0x66E4C:0x66E50] = b'PAL!'
        for group, addresses in enumerate(groups):
            for frame, address in enumerate(addresses):
                struct.pack_into('<4H', rom, address, 1, 2+frame, 3+group, 4+frame)
            payload = course_rom.surface_trail_art_payload(course_rom.Data(rom), group)
            count = len(addresses)
            self.assertEqual(struct.unpack_from('<8I', payload), (0x31435846, 0x2000, 80, count, 0x100, 3, 8*count, 0))
            self.assertEqual([struct.unpack_from('<IHH', payload, 32+i*8) for i in range(count)],
                             [(i*8, 8, 1) for i in range(count)])
            self.assertEqual(payload[32+count*8:36+count*8], b'ART!')
            self.assertEqual(payload[32+count*8+0x2000:36+count*8+0x2000], b'PAL!')
            self.assertEqual(payload[-count*8:], b''.join(rom[a:a+8] for a in addresses))
        struct.pack_into('<H', rom, 0x7AF14, 2)
        with self.assertRaisesRegex(ValueError, 'cell count'):
            course_rom.surface_trail_art_payload(course_rom.Data(rom), 4)
        with self.assertRaisesRegex(ValueError, 'art group'):
            course_rom.surface_trail_art_payload(course_rom.Data(rom), 5)

    def test_recovery_effect_rules_preserve_durations_and_signed_displacements(self):
        rom = bytearray(0xF31A9)
        rom[0xF2C48:0xF2C50] = bytes((1, 17, 1, 23, 1, 31, 0, 0))
        dy = (0, -1, 1, -2, 2, -3, 3, -16, 16)
        struct.pack_into('<9b', rom, 0xF31A0, *dy)
        payload = course_rom.recovery_fx_payload(course_rom.Data(rom))
        self.assertEqual(struct.unpack('<II3B9bI', payload), (0x31584652, 3, 17, 23, 31, *dy, 0))
        for offset, value, message in ((0xF2C48, 2, 'freeze sequence'),
                                        (0xF2C49, 0, 'freeze sequence'),
                                        (0xF2C4E, 1, 'freeze terminator'),
                                        (0xF31A0, 17, 'drip displacement')):
            old = rom[offset];rom[offset] = value
            with self.assertRaisesRegex(ValueError, message):
                course_rom.recovery_fx_payload(course_rom.Data(rom))
            rom[offset] = old

    def test_thaw_and_drip_art_sequences_preserve_cell_sizes(self):
        rom = bytearray(0xF3190)
        for index, (count, duration) in enumerate(((4, 4), (11, 3), (13, 2))):
            cell = 0x100 + index*100
            struct.pack_into('<H', rom, cell, count)
            struct.pack_into('<II', rom, 0xF3158+index*8, 0x08000000+cell, duration)
        struct.pack_into('<H', rom, 0x500, 1)
        struct.pack_into('<II', rom, 0xF3180, 0x08000500, 8)
        thaw = course_rom.race_particle_payload(course_rom.Data(rom), 0xF3158)
        drip = course_rom.race_particle_payload(course_rom.Data(rom), 0xF3180)
        self.assertEqual(struct.unpack_from('<8I', thaw), (0x31435846, 0x2000, 80, 3, 0x100, 3, 174, 0))
        self.assertEqual([struct.unpack_from('<IHH', thaw, 32+i*8) for i in range(3)],
                         [(0, 26, 4), (26, 68, 3), (94, 80, 2)])
        self.assertEqual(struct.unpack_from('<IHH', drip, 32), (0, 8, 8))
        self.assertEqual(struct.unpack_from('<I', drip, 24)[0], 8)

    def test_entry_splash_preserves_three_source_cells_and_durations(self):
        rom = bytearray(0xF3040)
        for index in range(3):
            cell = 0x100 + index * 8
            struct.pack_into('<H', rom, cell, 1)
            struct.pack_into('<II', rom, 0xF3014 + index * 8, 0x08000000 + cell, 8)
        payload = course_rom.race_particle_payload(course_rom.Data(rom), 0xF3014)
        self.assertEqual(struct.unpack_from('<8I', payload),
                         (0x31435846, 0x2000, 80, 3, 0x100, 3, 24, 0))
        frames = [struct.unpack_from('<IHH', payload, 32 + index * 8) for index in range(3)]
        self.assertEqual(frames, [(0, 8, 8), (8, 8, 8), (16, 8, 8)])
        self.assertEqual(payload[-24:], (struct.pack('<H', 1) + bytes(6)) * 3)

    def test_coin_pickup_sequence_includes_blank_rotation_cell(self):
        rom = bytearray(0xF3000)
        for index, duration in enumerate((4,3,2,1,3,4,3)):
            cell = 0x100 + index * 8
            struct.pack_into('<H', rom, cell, 0 if index == 3 else 1)
            struct.pack_into('<II', rom, 0xF2DDC + index * 8, 0x08000000+cell, duration)
        payload = course_rom.race_particle_payload(course_rom.Data(rom), 0xF2DDC)
        self.assertEqual(struct.unpack_from('<8I', payload),
                         (0x31435846,0x2000,80,7,0x100,3,50,0))
        frames = [struct.unpack_from('<IHH',payload,32+i*8) for i in range(7)]
        self.assertEqual(frames[3], (24,2,1))
        self.assertEqual(sum(row[2]+1 for row in frames),27)

    def test_mushroom_voices_preserve_driver_and_size_order(self):
        rom = bytearray(0xECC68)
        voices = tuple(range(202, 218))
        struct.pack_into('<16H', rom, 0xECC48, *voices)
        self.assertEqual(struct.unpack('<16H', course_rom.mushroom_voices(course_rom.Data(rom))), voices)
        struct.pack_into('<H', rom, 0xECC48 + 30, 0)
        with self.assertRaisesRegex(ValueError, 'mushroom voice'):
            course_rom.mushroom_voices(course_rom.Data(rom))

    def test_shell_orbit_delays_preserve_source_narrowing(self):
        rom=bytearray(0x100000)
        struct.pack_into('<6I',rom,0xEDAFC,0,17,34,256,511,513)
        self.assertEqual(course_rom.shell_orbit_delays(course_rom.Data(rom)),bytes((0,17,34,0,255,1)))

    def test_cpu_red_decisions_are_rom_data(self):
        rom=bytearray(0x100000)
        decisions=bytes((2,0,0,0,3,3,3,1))
        rom[0xECCF7:0xECCFF]=decisions
        self.assertEqual(course_rom.cpu_red_decisions(course_rom.Data(rom)),decisions)
        rom[0xECCF7]=4
        with self.assertRaisesRegex(ValueError,'invalid CPU red'):
            course_rom.cpu_red_decisions(course_rom.Data(rom))

    def test_cpu_green_decisions_are_rom_data(self):
        rom=bytearray(0x100000)
        decisions=bytes((2,0,0,0,3,1,3,3))
        rom[0xECCE7:0xECCEF]=decisions
        self.assertEqual(course_rom.cpu_green_decisions(course_rom.Data(rom)),decisions)
        rom[0xECCE7]=4
        with self.assertRaises(ValueError):
            course_rom.cpu_green_decisions(course_rom.Data(rom))

    def test_cpu_banana_decisions_are_rom_data(self):
        rom=bytearray(0xECCF7)
        rom[0xECCEF:0xECCF7]=bytes((0,0,0,2,3,1,3,3))
        self.assertEqual(course_rom.cpu_banana_decisions(course_rom.Data(rom)),bytes((0,0,0,2,3,1,3,3)))
        rom[0xECCEF]=4
        with self.assertRaisesRegex(ValueError,'invalid CPU banana'):
            course_rom.cpu_banana_decisions(course_rom.Data(rom))

    def test_cpu_roulette_resolves_source_course_engine_and_lap_tables(self):
        rom = bytearray(0xEDA00)
        for engine, jump in enumerate((0x43E48,0x43EC0,0x43F58)):
            branch=(0x08043F1C,0x08043FB4,0x08043FA8)[engine]
            struct.pack_into('<I',rom,jump+7*4,branch)
        for table in range(9):
            for driver in range(8):
                for rank in range(8):
                    struct.pack_into('<I',rom,0xED0A8+table*256+(driver*8+rank)*4,
                                     0x1000+(table+driver+rank)%13)
        data=course_rom.cpu_item_profiles(course_rom.Data(rom),11)
        self.assertEqual(len(data),1152)
        values=struct.unpack('<576H',data)
        for engine, group in enumerate((6,3,0)):
            for variant in range(3):
                for driver in range(8):
                    for rank in range(8):
                        self.assertEqual(values[((engine*3+variant)*8+driver)*8+rank],
                                         0x1000+(group+variant+driver+rank)%13)
        # 43DE8 sends headers outside4..23 to the generic lap-variant tables.
        for header in (24, 25, 26, 27, 32, 51):
            extra=struct.unpack('<576H',course_rom.cpu_item_profiles(course_rom.Data(rom),header))
            for engine in range(3):
                self.assertEqual(extra[engine*192:(engine+1)*192],values[384:576])
        struct.pack_into('<I',rom,0x43E48+7*4,0xDEADBEEF)
        with self.assertRaisesRegex(ValueError,'unknown source CPU item branch'):
            course_rom.cpu_item_profiles(course_rom.Data(rom),11)

    def test_static_scenery_uses_class_variant_palette_and_all_distance_images(self):
        rom = bytearray(0xF1300)
        descriptors = (*course_rom.STATIC_SCENERY_DESCRIPTORS[:2], 0xEFF88, 0xEFF68)
        for variant, descriptor in enumerate(descriptors):
            for lod in range(4):
                cell = 0x100 + variant * 32 + lod * 8
                struct.pack_into('<II', rom, descriptor + lod * 8,
                                 0x08000000 + cell, lod)
                struct.pack_into('<4H', rom, cell, 1, 0xF8, 0x1FC,
                                 ((variant % 3) << 12) | lod)
        palette = struct.pack('<48H', *range(48))
        tiles = bytes([0x11]) * 128
        payload = course_rom.static_scenery(course_rom.Data(rom),
            [(2, 1, 2, 3), (7, 4, 5, 6), (8, 7, 8, 9), (9, 10, 11, 12)],
            lambda kind: {2: 0x810000, 7: 0x0C0000, 8: 0x0C0001, 9: 0x860000}[kind],
            tiles, palette)
        self.assertEqual(struct.unpack_from('<4I', payload), (0x314E4353, 3, 4, 0))
        self.assertEqual(struct.unpack_from('<iiBBHIB3x', payload, 16),
                         (36 << 16, 44 << 16, 6, 0, 224, 0x0C0000, 1))
        self.assertEqual(struct.unpack_from('<iiBBHIB3x', payload, 56),
                         (84 << 16, 92 << 16, 12, 2, 256, 0x860000, 3))
        offset, pixels = 76, []
        for variant in range(4):
            for lod in range(4):
                shift, size = struct.unpack_from('<II', payload, offset)
                self.assertEqual(shift, lod)
                image = payload[offset + 8:offset + 8 + size]
                self.assertEqual(struct.unpack_from('<HHhh', image, 12), (8, 8, -4, -8))
                pixels.append(indexed_pixel(image))
                offset += 8 + size
        self.assertEqual(offset, len(payload))
        self.assertNotEqual(pixels[0], pixels[4])
        self.assertNotEqual(pixels[8], pixels[12])

    def test_broken_pier_boo_imports_one_unscaled_image(self):
        rom = bytearray(0xF1300)
        for lod in range(4):
            struct.pack_into('<II', rom, 0xF0FE8 + lod*8, 0x08000100, lod)
        struct.pack_into('<4H', rom, 0x100, 1, 0xF8, 0x1FC, 0)
        payload = course_rom.static_scenery(course_rom.Data(rom),
            [(0, 1, 2, 3)], lambda _: 0xA30000,
            bytes([0x11])*64, struct.pack('<48H', *range(48)))
        self.assertEqual(struct.unpack_from('<4I', payload), (0x314E4353, 1, 1, 0))
        self.assertEqual(struct.unpack_from('<iiBBHIB3x', payload, 16)[3:], (0, 256, 0xA30000, 0))

    def test_every_placed_object_class_has_a_native_owner(self):
        # Course objects never drop silently.
        self.assertTrue({0x03, 0xA2, 0xA3} <= course_rom.PLACED_OBJECT_CLASSES)

    def test_lake_rocks_import_rock_and_four_impact_images(self):
        rom = bytearray(0xF1300)
        for descriptor in (0xF0EE8, 0xF0F28, 0xF0F48, 0xF0F68, 0xF0F88):
            for lod in range(4):
                struct.pack_into('<II', rom, descriptor + lod*8, 0x08000100, lod)
        struct.pack_into('<4H', rom, 0x100, 1, 0xF8, 0x1FC, 0)
        payload = course_rom.static_scenery(course_rom.Data(rom),
            [(0, 1, 2, 3), (0, 4, 5, 6)], lambda kind: 0xA20000 + (kind & 3),
            bytes([0x11])*64, struct.pack('<48H', *range(48)))
        self.assertEqual(struct.unpack_from('<4I', payload), (0x314E4353, 2, 5, 0))
        self.assertEqual(struct.unpack_from('<iiBBHIB3x', payload, 16)[3:], (0, 224, 0xA20000, 0))
        self.assertEqual(struct.unpack_from('<iiBBHIB3x', payload, 36)[3:], (0, 224, 0xA20000, 1))

    def test_teepee_imports_scraps_and_seven_rider_views(self):
        rom = bytearray(0xF1300)
        for descriptor in (0xF0128,) + course_rom.TEEPEE_FRAMES:
            for lod in range(4):
                struct.pack_into('<II', rom, descriptor + lod*8, 0x08000100, lod)
        struct.pack_into('<4H', rom, 0x100, 1, 0xF8, 0x1FC, 0)
        for cell in course_rom.TEEPEE_RIDER_CELLS:
            struct.pack_into('<4H', rom, cell, 1, 0xF8, 0x1FC, 0)
        payload = course_rom.static_scenery(course_rom.Data(rom),
            [(0, 1, 2, 3)], lambda _: 0x030000,
            bytes([0x11])*64, struct.pack('<48H', *range(48)))
        self.assertEqual(struct.unpack_from('<4I', payload), (0x314E4353, 1, 11, 0))
        self.assertEqual(struct.unpack_from('<iiBBHIB3x', payload, 16)[3:], (0, 256, 0x030000, 0))
        offset, shifts = 36, []
        for _ in range(44):
            shift, size = struct.unpack_from('<II', payload, offset)
            shifts.append(shift)
            self.assertEqual(payload[offset+8:offset+16], b'MKSCIMG2')
            offset += 8+size
        self.assertEqual(offset, len(payload))
        self.assertEqual(shifts[:16], [0, 1, 2, 3]*4)
        self.assertEqual(shifts[16:], [0]*28)

    def test_kamek_imports_both_camera_facing_images(self):
        rom = bytearray(0xF1300)
        for descriptor in (0xF0E28, 0xF0E48):
            for lod in range(4):
                struct.pack_into('<II', rom, descriptor + lod*8, 0x08000100, lod)
        struct.pack_into('<4H', rom, 0x100, 1, 0xF8, 0x1FC, 0)
        payload = course_rom.static_scenery(course_rom.Data(rom),
            [(0, 1, 2, 3)], lambda _: 0xA00000,
            bytes([0x11])*64, struct.pack('<48H', *range(48)))
        self.assertEqual(struct.unpack_from('<4I', payload), (0x314E4353, 1, 2, 0))
        self.assertEqual(struct.unpack_from('<iiBBHIB3x', payload, 16)[3:], (0, 224, 0xA00000, 0))
        offset = 36
        for _ in range(8):
            shift, size = struct.unpack_from('<II', payload, offset)
            self.assertLessEqual(shift, 3)
            self.assertEqual(payload[offset+8:offset+16], b'MKSCIMG2')
            offset += 8+size
        self.assertEqual(offset, len(payload))

    def test_mechakoopas_import_both_directions_and_four_stride_images(self):
        rom = bytearray(0xF1300)
        for descriptor in (0xF0E68, 0xF0E88, 0xF0EA8, 0xF0EC8):
            for lod in range(4):
                struct.pack_into('<II', rom, descriptor + lod*8, 0x08000100, lod)
        struct.pack_into('<4H', rom, 0x100, 1, 0xF8, 0x1FC, 0)
        payload = course_rom.static_scenery(course_rom.Data(rom),
            [(0, 1, 2, 3), (1, 4, 5, 6)], lambda i: 0xA10000+i,
            bytes([0x11])*64, struct.pack('<48H', *range(48)))
        self.assertEqual(struct.unpack_from('<4I', payload), (0x314E4353, 2, 4, 0))
        for i in range(2):
            row = struct.unpack_from('<iiBBHIB3x', payload, 16+i*20)
            self.assertEqual(row[3:], (0, 224, 0xA10000+i, i))
        offset = 56
        for _ in range(16):
            shift, size = struct.unpack_from('<II', payload, offset)
            self.assertLessEqual(shift, 3)
            self.assertEqual(payload[offset+8:offset+16], b'MKSCIMG2')
            offset += 8+size
        self.assertEqual(offset, len(payload))
        with self.assertRaisesRegex(ValueError, 'unknown static scenery variant'):
            course_rom.static_scenery(course_rom.Data(rom), [(0, 1, 2, 3)],
                lambda _: 0xA10002, bytes(64), bytes(96))

    def test_inert_scenery_preserves_source_art_and_placement_order(self):
        codes = (0x010000, 0x010001, 0x020000, 0x070000, 0x0B0000,
                 0x0B0001, 0x850000, 0x8C0000, 0x900000, 0x9A0000, 0x9B0000)
        expected = (0xF06A8, 0xF06C8, 0xF0488, 0xF0468, 0xF0408,
                    0xF0428, 0xEFE48, 0xEFE48, 0xEFE48, 0xF0BC8, 0xF0BE8)
        rom = bytearray(0xF1300)
        for descriptor in expected:
            for lod in range(4):
                struct.pack_into('<II', rom, descriptor + lod*8, 0x08000100, lod)
        struct.pack_into('<4H', rom, 0x100, 1, 0xF8, 0x1FC, 0)
        payload = course_rom.static_scenery(course_rom.Data(rom),
            [(i, i+1, i+2, i+3) for i in range(len(codes))],
            lambda i: codes[i], bytes([0x11])*64, struct.pack('<48H', *range(48)))
        self.assertEqual(struct.unpack_from('<4I', payload), (0x314E4353, 11, 9, 0))
        descriptors = []
        for i, (code, descriptor) in enumerate(zip(codes, expected)):
            if descriptor not in descriptors:
                descriptors.append(descriptor)
            self.assertEqual(struct.unpack_from('<iiBBHIB3x', payload, 16+i*20),
                (((i+1)*8+4)<<16, ((i+2)*8+4)<<16, i+3,
                 descriptors.index(descriptor), 256, code, i))
        offset = 16+11*20
        for _ in range(9):
            for lod in range(4):
                shift, size = struct.unpack_from('<II', payload, offset)
                self.assertEqual(shift, lod)
                self.assertEqual(payload[offset+8:offset+16], b'MKSCIMG2')
                offset += 8+size
        self.assertEqual(offset, len(payload))

    def test_thunderclouds_share_imported_palette_rows_and_distance_art(self):
        rom = bytearray(0xF1000)
        for row in range(8):
            struct.pack_into('<5H', rom, 0x76BCA+row*10, *[row*5+i+1 for i in range(5)])
        for frame, descriptor in enumerate(range(0xF08E8, 0xF09E8, 32)):
            for lod in range(4):
                cell = 0x100+frame*32+lod*8
                struct.pack_into('<II', rom, descriptor+lod*8, 0x08000000+cell, lod)
                struct.pack_into('<4H', rom, cell, 1, 0xF8, 0x1FC, (1+frame//4)<<12)
        codes = (0x8F0000, 0x810000, 0x8F0001, 0x8F0002, 0x8F0003)
        motion, art = course_rom.thunderclouds(course_rom.Data(rom),
            [(i, i+1, i+2, i+3) for i in range(5)], lambda i: codes[i],
            bytes([0x11])*32, bytes(96))
        self.assertEqual(struct.unpack_from('<II', motion), (0x31444354, 4))
        for row, i in enumerate((0, 2, 3, 4)):
            self.assertEqual(struct.unpack_from('<iiHBB', motion, 8+row*12),
                (((i+1)*8+4)<<16, ((i+2)*8+4)<<16, codes[i]&65535, i+3, i))
        self.assertEqual(struct.unpack_from('<II', art), (0x31414354, 72))
        offset = 8
        samples = []
        for phase in range(9):
            colors = []
            for frame in range(8):
                for lod in range(4):
                    shift, size = struct.unpack_from('<II', art, offset)
                    self.assertEqual(shift, lod)
                    self.assertEqual(art[offset+8:offset+16], b'MKSCIMG2')
                    if lod == 0:
                        colors.append(indexed_pixel(art[offset+8:offset+8+size]))
                    offset += 8+size
            self.assertEqual(colors[0], colors[4])
            samples.append(colors[0])
        self.assertEqual(offset, len(art))
        self.assertEqual(len(set(samples)), 9)
        with self.assertRaisesRegex(ValueError, 'thundercloud variant'):
            course_rom.thunderclouds(course_rom.Data(rom), [(0, 0, 0, 0)], lambda _: 0x8F0004,
                bytes(32), bytes(96))

    def test_mice_preserve_axes_hopper_and_all_art_distances(self):
        rom = bytearray(0xF1000)
        for frame, descriptor in enumerate((0xF01C8, 0xF01E8)):
            for lod in range(4):
                cell = 0x100+frame*32+lod*8
                struct.pack_into('<II', rom, descriptor+lod*8, 0x08000000+cell, lod)
                struct.pack_into('<4H', rom, cell, 1, 0xF8, 0x1FC, frame)
        codes = (0x8E0000, 0x810000, 0x8E0001, 0x8E8001)
        motion, art = course_rom.mice(course_rom.Data(rom),
            [(i, i+1, i+2, i+3) for i in range(4)], lambda i: codes[i],
            bytes([0x11])*64, struct.pack('<48H', *range(48)))
        self.assertEqual(struct.unpack_from('<II', motion), (0x3153554D, 3))
        for row, i in enumerate((0, 2, 3)):
            self.assertEqual(struct.unpack_from('<iiHBB', motion, 8+row*12),
                (((i+1)*8+4)<<16, ((i+2)*8+4)<<16, codes[i]&65535, i+3, i))
        self.assertEqual(struct.unpack_from('<II', art), (0x3141534D, 2))
        offset = 8
        for _ in range(2):
            for lod in range(4):
                shift, size = struct.unpack_from('<II', art, offset)
                self.assertEqual(shift, lod)
                self.assertEqual(art[offset+8:offset+16], b'MKSCIMG2')
                offset += 8+size
        self.assertEqual(offset, len(art))
        with self.assertRaisesRegex(ValueError, 'mouse variant'):
            course_rom.mice(course_rom.Data(rom), [(0, 0, 0, 0)], lambda _: 0x8E0002,
                bytes(64), bytes(96))

    def test_falling_stars_preserve_six_variants_and_all_art_distances(self):
        rom = bytearray(0xF1000)
        for frame, descriptor in enumerate(range(0xF0A08, 0xF0AA8, 32)):
            for lod in range(4):
                cell = 0x100+frame*32+lod*8
                struct.pack_into('<II', rom, descriptor+lod*8, 0x08000000+cell, lod)
                struct.pack_into('<4H', rom, cell, 1, 0xF8, 0x1FC, frame)
        codes = tuple(0x970000+i for i in range(6))
        motion, art = course_rom.falling_stars(course_rom.Data(rom),
            [(i, i+1, i+2, i+3) for i in range(6)], lambda i: codes[i],
            bytes([0x11])*160, struct.pack('<48H', *range(48)))
        self.assertEqual(struct.unpack_from('<II', motion), (0x31525453, 6))
        for row, i in enumerate(range(6)):
            self.assertEqual(struct.unpack_from('<iiHBB', motion, 8+row*12),
                (((i+1)*8+4)<<16, ((i+2)*8+4)<<16, codes[i]&65535, i+3, i))
        self.assertEqual(struct.unpack_from('<II', art), (0x31415253, 5))
        offset = 8
        for _ in range(5):
            for lod in range(4):
                shift, size = struct.unpack_from('<II', art, offset)
                self.assertEqual(shift, lod)
                self.assertEqual(art[offset+8:offset+16], b'MKSCIMG2')
                offset += 8+size
        self.assertEqual(offset, len(art))
        with self.assertRaisesRegex(ValueError, 'falling star variant'):
            course_rom.falling_stars(course_rom.Data(rom), [(0, 0, 0, 0)], lambda _: 0x970006,
                bytes(64), bytes(96))

    def test_lava_bubbles_preserve_variants_and_decode_both_art_phases(self):
        rom = bytearray(0xF1000)
        for frame, descriptor in enumerate((0xEFF08, 0xEFF28)):
            for lod in range(4):
                cell = 0x100 + frame * 32 + lod * 8
                struct.pack_into('<II', rom, descriptor + lod * 8, 0x08000000 + cell, lod)
                struct.pack_into('<4H', rom, cell, 1, 0xF8, 0x1FC, frame)
        result = course_rom.lava_bubbles(course_rom.Data(rom),
            [(7, 8, 9, 10), (2, 3, 4, 9)],
            lambda kind: {2: 0x82000C, 7: 0x810000}[kind], bytes([0x11])*64,
            struct.pack('<48H', *range(48)))
        motion, art = result
        self.assertEqual(struct.unpack('<IIiiHBB', motion),
                         (0x3142564C, 1, 28 << 16, 36 << 16, 12, 9, 1))
        self.assertEqual(struct.unpack_from('<II', art), (0x3141424C, 2))
        offset = 8
        for _ in range(2):
            for lod in range(4):
                shift, size = struct.unpack_from('<II', art, offset)
                self.assertEqual(shift, lod)
                self.assertEqual(art[offset + 8:offset + 16], b'MKSCIMG2')
                offset += 8 + size
        self.assertEqual(offset, len(art))

    def test_thwomps_preserve_patrol_variant_and_all_distance_art(self):
        rom = bytearray(0xF1000)
        for frame, descriptor in enumerate((0xEFEE8,)):
            for lod in range(4):
                cell = 0x100 + frame * 32 + lod * 8
                struct.pack_into('<II', rom, descriptor + lod * 8, 0x08000000 + cell, lod)
                struct.pack_into('<4H', rom, cell, 1, 0xF8, 0x1FC, frame)
        result = course_rom.thwomps(course_rom.Data(rom),
            [(7, 8, 9, 10), (2, 3, 4, 9)],
            lambda kind: {2: 0x81000B, 7: 0x820000}[kind], bytes([0x11])*64,
            struct.pack('<48H', *range(48)))
        motion, art = result
        self.assertEqual(struct.unpack('<IIiiHBB', motion),
                         (0x31505754, 1, 28 << 16, 36 << 16, 11, 9, 1))
        self.assertEqual(struct.unpack_from('<II', art), (0x31415754, 1))
        offset = 8
        for _ in range(1):
            for lod in range(4):
                shift, size = struct.unpack_from('<II', art, offset)
                self.assertEqual(shift, lod)
                self.assertEqual(art[offset + 8:offset + 16], b'MKSCIMG2')
                offset += 8 + size
        self.assertEqual(offset, len(art))

    def test_crabs_keep_axis_variants_placement_order_and_four_art_frames(self):
        rom = bytearray(0xF1000)
        for frame, descriptor in enumerate((0xEFFC8, 0xEFFE8, 0xF0008, 0xF0028)):
            for lod in range(4):
                cell = 0x100 + frame * 32 + lod * 8
                struct.pack_into('<II', rom, descriptor + lod * 8, 0x08000000 + cell, lod)
                struct.pack_into('<4H', rom, cell, 1, 0xF8, 0x1FC, frame)
        motion, art = course_rom.crabs(course_rom.Data(rom),
            [(7, 8, 9, 10), (2, 3, 4, 9), (3, 5, 6, 8)],
            lambda kind: {2: 0x808001, 3: 0x808002, 7: 0x810000}[kind],
            bytes([0x11])*128, struct.pack('<96H', *range(96)))
        self.assertEqual(struct.unpack_from('<II', motion), (0x31425243, 2))
        self.assertEqual(struct.unpack_from('<iiHBB', motion, 8),
                         (28 << 16, 36 << 16, 0x8001, 9, 1))
        self.assertEqual(struct.unpack_from('<iiHBB', motion, 20),
                         (44 << 16, 52 << 16, 0x8002, 8, 2))
        self.assertEqual(struct.unpack_from('<II', art), (0x31415243, 4))
        offset = 8
        for frame in range(4):
            for lod in range(4):
                shift, size = struct.unpack_from('<II', art, offset)
                self.assertEqual(shift, lod)
                self.assertEqual(art[offset + 8:offset + 16], b'MKSCIMG2')
                offset += 8 + size
        self.assertEqual(offset, len(art))

    def test_seagulls_keep_placement_order_and_three_art_frames(self):
        rom = bytearray(0xF1000)
        for frame, descriptor in enumerate((0xF0528, 0xF0548, 0xF0568)):
            for lod in range(4):
                cell = 0x100 + frame * 32 + lod * 8
                struct.pack_into('<II', rom, descriptor + lod * 8, 0x08000000 + cell, lod)
                struct.pack_into('<4H', rom, cell, 1, 0xF8, 0x1FC, frame)
        motion, art = course_rom.seagulls(course_rom.Data(rom),
            [(7, 8, 9, 10), (2, 3, 4, 9), (3, 5, 6, 8)],
            lambda kind: {2: 0x870000, 3: 0x870000, 7: 0x810000}[kind],
            bytes([0x11])*128, struct.pack('<96H', *range(96)))
        self.assertEqual(struct.unpack_from('<II', motion), (0x314C4753, 2))
        self.assertEqual(struct.unpack_from('<iiHBB', motion, 8),
                         (28 << 16, 36 << 16, 0, 9, 1))
        self.assertEqual(struct.unpack_from('<iiHBB', motion, 20),
                         (44 << 16, 52 << 16, 0, 8, 2))
        self.assertEqual(struct.unpack_from('<II', art), (0x31414753, 3))
        offset = 8
        for frame in range(3):
            for lod in range(4):
                shift, size = struct.unpack_from('<II', art, offset)
                self.assertEqual(shift, lod)
                self.assertEqual(art[offset + 8:offset + 16], b'MKSCIMG2')
                offset += 8 + size
        self.assertEqual(offset, len(art))

    def test_piranhas_keep_placement_order_and_five_art_frames(self):
        rom = bytearray(0xF1000)
        for frame, descriptor in enumerate((0xF0588, 0xF05A8, 0xF05C8, 0xF05E8, 0xF0608)):
            for lod in range(4):
                cell = 0x100 + frame * 32 + lod * 8
                struct.pack_into('<II', rom, descriptor + lod * 8, 0x08000000 + cell, lod)
                struct.pack_into('<4H', rom, cell, 1, 0xF8, 0x1FC, frame)
        motion, art = course_rom.piranhas(course_rom.Data(rom),
            [(7, 8, 9, 10), (2, 3, 4, 9), (3, 5, 6, 8)],
            lambda kind: {2: 0x8A0000, 3: 0x8A0000, 7: 0x810000}[kind],
            bytes([0x11])*256, struct.pack('<96H', *range(96)))
        self.assertEqual(struct.unpack_from('<II', motion), (0x314E5250, 2))
        self.assertEqual(struct.unpack_from('<iiHBB', motion, 8),
                         (28 << 16, 36 << 16, 0, 9, 1))
        self.assertEqual(struct.unpack_from('<iiHBB', motion, 20),
                         (44 << 16, 52 << 16, 0, 8, 2))
        self.assertEqual(struct.unpack_from('<II', art), (0x31415250, 5))
        offset = 8
        for frame in range(5):
            for lod in range(4):
                shift, size = struct.unpack_from('<II', art, offset)
                self.assertEqual(shift, lod)
                self.assertEqual(art[offset + 8:offset + 16], b'MKSCIMG2')
                offset += 8 + size
        self.assertEqual(offset, len(art))

    def test_snowmen_keep_placement_order_and_two_art_frames(self):
        rom = bytearray(0xF1000)
        for frame, descriptor in enumerate((0xF0668, 0xF0688)):
            for lod in range(4):
                cell = 0x100 + frame * 32 + lod * 8
                struct.pack_into('<II', rom, descriptor + lod * 8, 0x08000000 + cell, lod)
                struct.pack_into('<4H', rom, cell, 1, 0xF8, 0x1FC, frame)
        motion, art = course_rom.snowmen(course_rom.Data(rom),
            [(7, 8, 9, 10), (2, 3, 4, 9), (3, 5, 6, 8)],
            lambda kind: {2: 0x920000, 3: 0x920000, 7: 0x810000}[kind],
            bytes([0x11])*256, struct.pack('<96H', *range(96)))
        self.assertEqual(struct.unpack_from('<II', motion), (0x314D4E53, 2))
        self.assertEqual(struct.unpack_from('<iiHBB', motion, 8),
                         (28 << 16, 36 << 16, 0, 9, 1))
        self.assertEqual(struct.unpack_from('<iiHBB', motion, 20),
                         (44 << 16, 52 << 16, 0, 8, 2))
        self.assertEqual(struct.unpack_from('<II', art), (0x31414E53, 2))
        offset = 8
        for frame in range(2):
            for lod in range(4):
                shift, size = struct.unpack_from('<II', art, offset)
                self.assertEqual(shift, lod)
                self.assertEqual(art[offset + 8:offset + 16], b'MKSCIMG2')
                offset += 8 + size
        self.assertEqual(offset, len(art))

    def test_penguins_keep_placement_order_and_six_art_frames(self):
        rom = bytearray(0xF1000)
        for frame, descriptor in enumerate((0xF0C08, 0xF0C28, 0xF0C48, 0xF0C68, 0xF0C88, 0xF0CA8)):
            for lod in range(4):
                cell = 0x100 + frame * 32 + lod * 8
                struct.pack_into('<II', rom, descriptor + lod * 8, 0x08000000 + cell, lod)
                struct.pack_into('<4H', rom, cell, 1, 0xF8, 0x1FC, frame)
        motion, art = course_rom.penguins(course_rom.Data(rom),
            [(7, 8, 9, 10), (2, 3, 4, 9), (3, 5, 6, 8)],
            lambda kind: {2: 0x9C0000, 3: 0x9C0001, 7: 0x810000}[kind],
            bytes([0x11])*256, struct.pack('<96H', *range(96)))
        self.assertEqual(struct.unpack_from('<II', motion), (0x314E4750, 2))
        self.assertEqual(struct.unpack_from('<iiHBB', motion, 8),
                         (28 << 16, 36 << 16, 0, 9, 1))
        self.assertEqual(struct.unpack_from('<iiHBB', motion, 20),
                         (44 << 16, 52 << 16, 1, 8, 2))
        self.assertEqual(struct.unpack_from('<II', art), (0x31414750, 6))
        offset = 8
        for frame in range(6):
            for lod in range(4):
                shift, size = struct.unpack_from('<II', art, offset)
                self.assertEqual(shift, lod)
                self.assertEqual(art[offset + 8:offset + 16], b'MKSCIMG2')
                offset += 8 + size
        self.assertEqual(offset, len(art))

    def test_boos_keep_placement_order_and_two_art_frames(self):
        rom = bytearray(0xF1000)
        for frame, descriptor in enumerate((0xF07A8, 0xF07C8)):
            for lod in range(4):
                cell = 0x100 + frame * 32 + lod * 8
                struct.pack_into('<II', rom, descriptor + lod * 8, 0x08000000 + cell, lod)
                struct.pack_into('<4H', rom, cell, 1, 0xF8, 0x1FC, frame)
        motion, art = course_rom.boos(course_rom.Data(rom),
            [(7, 8, 9, 10), (2, 3, 4, 9), (3, 5, 6, 8)],
            lambda kind: {2: 0x8D0000, 3: 0x8D0001, 7: 0x810000}[kind],
            bytes([0x11])*256, struct.pack('<96H', *range(96)))
        self.assertEqual(struct.unpack_from('<II', motion), (0x314F4F42, 2))
        self.assertEqual(struct.unpack_from('<iiHBB', motion, 8),
                         (28 << 16, 36 << 16, 0, 9, 1))
        self.assertEqual(struct.unpack_from('<iiHBB', motion, 20),
                         (44 << 16, 52 << 16, 1, 8, 2))
        self.assertEqual(struct.unpack_from('<II', art), (0x31414F42, 2))
        offset = 8
        for frame in range(2):
            for lod in range(4):
                shift, size = struct.unpack_from('<II', art, offset)
                self.assertEqual(shift, lod)
                self.assertEqual(art[offset + 8:offset + 16], b'MKSCIMG2')
                offset += 8 + size
        self.assertEqual(offset, len(art))

    def test_cannonballs_keep_variants_and_seven_art_frames(self):
        rom = bytearray(0xF1000)
        for frame, descriptor in enumerate((0xF0048, 0xF0068, 0xF0088, 0xF00A8, 0xF00C8, 0xF00E8, 0xF0108)):
            for lod in range(4):
                cell = 0x100 + frame * 32 + lod * 8
                struct.pack_into('<II', rom, descriptor + lod * 8, 0x08000000 + cell, lod)
                struct.pack_into('<4H', rom, cell, 1, 0xF8, 0x1FC, frame)
        motion, art = course_rom.cannonballs(course_rom.Data(rom),
            [(7, 8, 9, 10), (2, 3, 4, 9), (3, 5, 6, 8)],
            lambda kind: {2: 0x840001, 3: 0x840003, 7: 0x810000}[kind],
            bytes([0x11])*256, struct.pack('<96H', *range(96)))
        self.assertEqual(struct.unpack_from('<II', motion), (0x314C4243, 2))
        self.assertEqual(struct.unpack_from('<iiHBB', motion, 8),
                         (28 << 16, 36 << 16, 1, 9, 1))
        self.assertEqual(struct.unpack_from('<iiHBB', motion, 20),
                         (44 << 16, 52 << 16, 3, 8, 2))
        self.assertEqual(struct.unpack_from('<II', art), (0x31414243, 7))
        offset = 8
        for frame in range(7):
            for lod in range(4):
                shift, size = struct.unpack_from('<II', art, offset)
                self.assertEqual(shift, lod)
                self.assertEqual(art[offset + 8:offset + 16], b'MKSCIMG2')
                offset += 8 + size
        self.assertEqual(offset, len(art))

    def test_shapes_clip_and_preserve_source_write_order(self):
        source = bytearray(512)
        source[4] = 1
        struct.pack_into('<I', source, 0xCC, 256)
        source[256] = 3
        struct.pack_into('<H', source, 257, 8)
        # First/last checkpoints use signed source IDs; later writes win.
        for index, entry in enumerate(((0, 0, 0, 1, 1),
                                       (2, 0, 1, 1, 0),
                                       (4, 63, 63, 1, 0))):
            kind, x, y, extent, height = entry
            struct.pack_into('<BHHHH', source, 264 + index * 12,
                             kind, x, y, extent, height)
        width, count, cells = course_rom.raster(course_rom.Data(source))
        self.assertEqual((width, count), (64, 3))
        self.assertEqual([cells[i] for i in (0, 1, 64, 65, 128)],
                         [128, 128, 1, 128, 1])
        self.assertEqual(cells[63 * 64 + 63], 130)
        self.assertEqual(cells[62 * 64 + 63], 130)
        self.assertEqual(cells[62 * 64 + 62], 127)
        source[264] = 5
        with self.assertRaisesRegex(ValueError, 'shape'):
            course_rom.raster(course_rom.Data(source))

    def test_split_chunks_keep_stride_and_reject_overflow(self):
        source = bytearray(32)
        struct.pack_into('<3H', source, 0, 8, 0, 20)
        with patch.object(course_rom, 'decode_lz77', side_effect=(b'ab', b'c')):
            self.assertEqual(course_rom.split_lz(course_rom.Data(source), 0, 3, 4),
                             b'ab\0\0' + bytes(4) + b'c\0\0\0')
        with patch.object(course_rom, 'decode_lz77', return_value=b'12345'):
            with self.assertRaisesRegex(ValueError, 'exceeds destination'):
                course_rom.split_lz(course_rom.Data(source), 0, 3, 4)

    def test_signed_course_alias_selects_header_not_course_id(self):
        rom = bytearray(0xE8100)
        struct.pack_into('<I', rom, 0xE7FEC + 5 * 4, 0x08000100)
        struct.pack_into('<I', rom, 0xE7FEC + 4 * 4, 0x08000104)
        struct.pack_into('<2I', rom, 0x100, 1, 0)
        container = bytearray(512)
        container[0] = 42
        container[256] = 99
        container[256 + 0x30] = 255  # header 5 references header 4
        selected = course_rom.source_course(course_rom.Data(rom), 5,
            course_rom.Data(container), [0, 256, 512], 0x30)
        self.assertEqual(selected.data[0], 42)
        container[256 + 0x30] = 0
        selected = course_rom.source_course(course_rom.Data(rom), 5,
            course_rom.Data(container), [0, 256, 512], 0x30)
        self.assertEqual(selected.data[0], 99)
        container[256 + 0x30] = 128
        with self.assertRaisesRegex(ValueError, 'outside header table'):
            course_rom.source_course(course_rom.Data(rom), 5,
                course_rom.Data(container), [0, 256, 512], 0x30)

    def test_sky_motion_profiles_keep_course_rules_distinct(self):
        peach = course_rom.sky_motion_payload(4)
        self.assertEqual(len(peach), 40)
        self.assertEqual(struct.unpack_from('<II', peach), (0x31524D53, 1))
        self.assertEqual(peach[8:16], bytes((0, 7, 0, 0, 0, 6, 7, 8)))
        self.assertEqual(struct.unpack_from('<3h3hh', peach, 16), (-40, 0, 0, 70, 70, 70, -1))
        self.assertEqual(peach[34:40], bytes((2, 0, 0, 1, 2, 0)))
        self.assertEqual(course_rom.sky_motion_payload(19, 14)[39], 3)
        self.assertEqual(course_rom.sky_motion_payload(19, 2)[39], 1)
        self.assertEqual(course_rom.sky_motion_payload(4, 14)[39], 0)
        mario = course_rom.sky_motion_payload(8)
        self.assertEqual(mario[8], 2)
        self.assertEqual(struct.unpack_from('<hBBh', mario, 28), (24, 2, 8, 0))
        for behavior in (19, 20, 51):
            self.assertEqual(course_rom.sky_motion_payload(behavior)[34:40], bytes((0, 0, 2, 1, 0, 1)))
        for behavior in (1, 2, 3, 4, 6, 7, 8, 10, 11, 13, 14, 15, 16,
                         17, 18, 19, 20, 21, 22, 23, 28, 29, 37, 43, 51):
            self.assertEqual(len(course_rom.sky_motion_payload(behavior)), 40)
        with self.assertRaisesRegex(ValueError, 'unsupported sky motion'):
            course_rom.sky_motion_payload(255)

    def test_sky_motion_extension_follows_original_map_bytes(self):
        rom = bytearray(0x258000)
        container = 0x234000
        struct.pack_into('<I', rom, container, 4)
        rom[container + 4:container + 6] = bytes((2, 3))
        struct.pack_into('<5I', rom, container + 8, 24, 26, 28, 30, 32)
        with patch('course_rom.split_lz', return_value=bytes(32)), patch(
                'course_rom.padded_lz', side_effect=lambda data, offset, length: bytes(length)):
            legacy = course_rom.sky(course_rom.Data(rom), 0)
            animated = course_rom.sky(course_rom.Data(rom), 0, 4)
        self.assertEqual(struct.unpack_from('<I', legacy, 60)[0], 0)
        self.assertEqual(struct.unpack_from('<I', animated, 60)[0], 40)
        self.assertEqual(legacy[:60], animated[:60])
        self.assertEqual(legacy[64:], animated[64:-40])
        self.assertEqual(animated[-40:], course_rom.sky_motion_payload(4))

    def test_sky_shapes_extend_only_the_named_layer(self):
        for size, layer, dimensions in ((0x20, 0, (64, 32)), (4, 0, (32, 64)),
                                         (0x40, 1, (64, 32)), (8, 1, (32, 64)),
                                         (0x80, 2, (64, 32)), (0x10, 2, (32, 64))):
            shapes = course_rom.sky_shapes(size)
            self.assertEqual(shapes[layer], dimensions)
            self.assertEqual([shape for i, shape in enumerate(shapes) if i != layer],
                             [(32, 32), (32, 32)])
        with self.assertRaisesRegex(ValueError, 'unsupported sky shape'):
            course_rom.sky_shapes(255)

    def test_stream_requires_terminator_and_checked_source_ranges(self):
        source = bytearray(12)
        struct.pack_into('<I', source, 0, 4)
        source[4:8] = b'\x80\x03\x04\x00'
        self.assertEqual(course_rom.stream(course_rom.Data(source), 0),
                         [b'\x80\x03\x04\x00'])
        source[8] = 1
        with self.assertRaisesRegex(ValueError, 'outside source'):
            course_rom.stream(course_rom.Data(source), 0)
        with self.assertRaises(ValueError):
            course_rom.Data(source).span(-1, 1)


class ImportTests(unittest.TestCase):
    def test_bad_rom_creates_no_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            rom = root / 'game.gba'
            rom.write_bytes(b'invalid')
            output = root / 'new' / 'game.pack'
            with self.assertRaisesRegex(ValueError, 'unmodified US'):
                import_rom.build(rom, output)
            self.assertFalse(output.parent.exists())

    def test_deterministic_publication_no_clobber_and_failure_cleanup(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            rom = root / 'game.gba'
            rom.write_bytes(b'fixture')
            payloads = [(2, 7, b'character', 1), (1, 4, b'course', 1)]
            with patch.object(import_rom, 'sections', return_value=payloads):
                first, second = root / 'one.pack', root / 'two.pack'
                self.assertEqual(import_rom.build(rom, first), import_rom.build(rom, second))
                self.assertEqual(first.read_bytes(), second.read_bytes())
                original = first.read_bytes()
                with self.assertRaisesRegex(ValueError, 'already exists'):
                    import_rom.build(rom, first)
                self.assertEqual(first.read_bytes(), original)
                payloads.append(payloads[0])
                with self.assertRaisesRegex(ValueError, 'duplicate'):
                    import_rom.build(rom, root / 'failed.pack')
                self.assertEqual(sorted(p.name for p in root.iterdir()),
                                 ['game.gba', 'one.pack', 'two.pack'])
