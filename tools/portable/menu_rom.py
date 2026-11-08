"""Normalize the US single-player mode, engine and Time Trial menu art."""
import struct
from ui_rom import affine_tile_map, cell_image, decode_lz77, image_asset, tile_map, tile_pixel

MENU_BACKGROUND = 0x21000
MENU_HEADING = 0x21001
MENU_MODE = 0x21100
MENU_CLASS = 0x21200
MENU_TRIAL = 0x21300
MENU_PLAYER_MEDALLION = 0x21400
# spm_loadOtherGfx (0800317C) breadcrumb tabs and sub_80036E4 player banner.
MENU_TAB_GAME = 0x26000     # +0..3: 1-P..4-P (D8EBC)
MENU_TAB_MODE = 0x26010     # +action: GP, TT, VS, Battle, -, Quick Run (D8EDC)
MENU_TAB_CLASS = 0x26020    # +0..2: 50/100/150cc (D8F0C)
MENU_TAB_TRIAL = 0x26030    # +0..1: trial, records (D8F24)
MENU_TAB_DRIVER = 0x26040   # +driver 0..7: face tab (08002EA4)
MENU_PLAYER_BANNER = 0x26050  # +player 0..3 (D8E9C)
# sub_8004860 SETTINGS hint and the sub_8005590/sub_80058B0 settings window.
MENU_SETTINGS_HINT = 0x26060
MENU_OPTION_CURSOR = 0x26061
MENU_OPTION_BRACKET = 0x26062
MENU_OPTION_WINDOW = 0x26070  # +action: TT 1, VS 2, Quick Run 5
MENU_FONT = 0x26080   # 96 glyphs for ASCII 0x20..0x7F, 8x16 each
MENU_FRAME = 0x26081  # 3x3 tile nine-slice of the window border


def menu_sections(rom):
    # sub_8004584 shared art; its rotating wheel becomes a native decoration.
    palette = bytearray(512)
    palette[0xa0:0xc0] = rom[0xae03c:0xae05c]
    palette[0xe0:0x120] = rom[0x7cd48:0x7cd88]
    tiles = decode_lz77(rom, 0x7d1e8)
    width, height, pattern = tile_map(tiles, palette, decode_lz77(rom, 0x7da94), 32, 32)
    pixels = [pattern[y * width + x] for y in range(160) for x in range(240)]
    # The wheel is source cell art, not a reconstructed affine display layer.
    object_tiles = bytearray(32768)
    object_palette = bytearray(512)
    object_palette[:64] = rom[0xaf2c0:0xaf300]
    object_palette[0x60:0x80] = rom[0xaf300:0xaf320]
    for address, offset in ((0xaf320, 0), (0xc2fa8, 0x1000), (0xaf778, 0x6c00)):
        data = decode_lz77(rom, address)
        object_tiles[offset:offset + len(data)] = data
    wheel = cell_image(rom, 0xc860c, object_tiles, object_palette)
    w, h = struct.unpack_from('<HH', wheel, 12)
    wheel_pixels = struct.unpack_from(f'<{w*h}I', wheel, 32)
    for y in range(160):
        for x in range(240):
            sx, sy = (x - 176) // 2 + w // 2, (y - 92) // 2 + h // 2
            if 0 <= sx < w and 0 <= sy < h and wheel_pixels[sy*w+sx]:
                pixels[y*240+x] = wheel_pixels[sy*w+sx]
    _, _, back_pattern = tile_map(tiles, palette, decode_lz77(rom, 0x7db98), 32, 32)
    # 4584: BG2CNT448E uses byte map AEE04 and two 8bpp graphics uploads;
    # the green 7DB98 text map belongs to stationary BG1 behind BG0.
    affine_uploads = (decode_lz77(rom, 0xae180), decode_lz77(rom, 0xae7c8))
    if any(len(upload) != 0x2000 for upload in affine_uploads):
        raise ValueError("invalid menu affine graphics size")
    affine_tiles = b"".join(affine_uploads)
    _, _, affine = affine_tile_map(affine_tiles, palette, decode_lz77(rom, 0xaee04), 32)
    sections = [(15, MENU_BACKGROUND, image_asset(240, 160, pixels)),
                (15, 0x21002, image_asset(256, 256, pattern)),
                (15, 0x21003, image_asset(256, 256, affine)),
                (15, 0x21004, wheel),
                (15, 0x21005, cell_image(rom, 0xc8614, object_tiles, object_palette)),
                (15, 0x21006, image_asset(256, 256, back_pattern)),
                (15, MENU_HEADING, cell_image(rom, 0xc86f4, object_tiles, object_palette))]
    # sub_80042C4 and sub_800441C: CellData, graphics, palette, x/y,
    # action. Normalize each medallion separately, including the common rim.
    # The native single-player shell also exposes Battle. Its action-3
    # medallion is row two of 42C4's multiplayer table, retaining that row's
    # cell tile addresses and palette bank before normalizing to image pixels.
    # Local VS (MENU_MODE+4) is row zero of 42C4's two-row link table D905C.
    for base, table, indices in ((MENU_MODE, 0xd8fe4, range(3)),
                                 (MENU_CLASS, 0xd9094, range(3)),
                                 (MENU_TRIAL, 0xd90d0, range(2)),
                                 (MENU_MODE+3, 0xd9020, (2,)),
                                 (MENU_MODE+4, 0xd905c, (0,)),
                                 *((MENU_PLAYER_MEDALLION+player, table, (0,)) for player,table in enumerate((0xd8f94,0xd8fa8,0xd8fbc,0xd8fd0)))):
        for slot, index in enumerate(indices):
            cell, graphics, colors = struct.unpack_from('<3I', rom, table + index*20)
            offset = 0x3c00 + index*0x800
            for address, destination in ((0xb69ec, offset),
                                         (graphics-0x08000000, offset+0x100),
                                         (0xb6a5c, offset+0x700)):
                data = decode_lz77(rom, address)
                object_tiles[destination:destination+len(data)] = data
            p = colors - 0x08000000
            object_palette[0x100+index*32:0x120+index*32] = rom[p:p+32]
            sections.append((15, base+slot,
                cell_image(rom, cell-0x08000000, object_tiles, object_palette)))
    sections += [(15, image_id, cell_image(rom, cell, object_tiles, object_palette))
                 for image_id, cell in ((MENU_SETTINGS_HINT, 0xc8758),
                                        (MENU_OPTION_CURSOR, 0xc87a0),
                                        (MENU_OPTION_BRACKET, 0xc8784))]
    heading_tail, glyphs = heading_sections(rom)
    return sections + option_windows(rom) + tab_sections(rom) + glyphs + [(92, 1, motion_payload(rom)+heading_tail)]


def heading_sections(rom):
    """3FD0: individual source cells, reverse full-cell targets and strip provenance."""
    tiles=bytearray(32768)
    for address,offset in ((0xaf320,0),(0xc2fa8,0x1000)):
        data=decode_lz77(rom,address);tiles[offset:offset+len(data)]=data
    palette=bytearray(512);palette[:64]=rom[0xaf2c0:0xaf300]
    tail=bytearray(struct.pack('<II',0x3147484d,4));sections=[]
    for group,(table,strip_id) in enumerate(((0xd90f8,0x21001),(0xd9230,0x22000),
                                            (0xd95bc,0x23010),(0xd9588,0x23011))):
        def pointer(offset):
            address=struct.unpack_from('<I',rom,offset)[0]-0x08000000
            if not 0<=address<len(rom):raise ValueError('heading pointer outside ROM')
            return address
        full=pointer(table);count=struct.unpack_from('<H',rom,full)[0]
        if not 1<=count<=16:raise ValueError('heading letter count outside1..16')
        strip=bytearray(cell_image(rom,full,tiles,palette))
        if group==1:
            x,y=struct.unpack_from('<hh',strip,16);struct.pack_into('<hh',strip,16,x+128,y+16)
        elif group>=2:struct.pack_into('<hh',strip,16,0,0)
        digest=2166136261
        for value in strip:digest=((digest^value)*16777619)&0xffffffff
        tail.extend(struct.pack('<III',count,strip_id,digest))
        for row in range(count):
            a,b,_=struct.unpack_from('<3H',rom,full+2+(count-1-row)*6)
            x=b&511;x=x if x<=255 else -(x^511)
            y=a&255;y=y if y<128 else y-256
            image_id=0x29000+group*0x20+row
            tail.extend(struct.pack('<Ihh',image_id,128+x,24+y))
            sections.append((15,image_id,cell_image(rom,pointer(table+4+row*4),tiles,palette)))
    return bytes(tail),sections


def option_windows(rom):
    """sub_8005590 BG1 windows: menu font at char base 0x6004000, colors 251..255.

    The same tiles give the native menu font (ASCII 0x20..0x7F, 8x16 glyphs,
    top tile 0x80 + row*64 + column, bottom tile +32) and a nine-slice of the
    Quick Run window border for native-only screens.
    """
    tiles = bytearray(0x6000)
    for address, offset in ((0xa97f8, 0x1000), (0xa9e2c, 0x2800), (0xaa56c, 0x4000)):
        data = decode_lz77(rom, address)
        tiles[offset:offset+len(data)] = data
    palette = bytearray(512)
    palette[0x1f6:0x200] = rom[0xaad30:0xaad3a]

    def rectangle(cells, columns, rows, anchor):
        width, height = columns * 8, rows * 8
        pixels = [0] * (width * height)
        for y in range(height):
            for x in range(width):
                cell = cells[(y // 8) * columns + x // 8]
                pixels[y * width + x] = tile_pixel(tiles, palette, cell & 1023,
                    7 - x % 8 if cell & 0x400 else x % 8,
                    7 - y % 8 if cell & 0x800 else y % 8, cell >> 12)
        return image_asset(width, height, pixels, anchor)

    sections, maps = [], {}
    for action, address in ((1, 0xafbc0), (5, 0xafaa8), (2, 0xafcd4)):
        if action == 2:
            # VS alone uploads its mushroom-start icon tiles and bank 12.
            tiles[0x5200:0x5300] = rom[0xaf914:0xafa14]
            palette[0x180:0x1a0] = rom[0x627f4:0x62814]
        cells = maps[action] = struct.unpack('<640H', decode_lz77(rom, address))
        used = [i for i, cell in enumerate(cells) if cell]
        left, right = min(i % 32 for i in used), max(i % 32 for i in used) + 1
        top, bottom = used[0] // 32, used[-1] // 32 + 1
        sections.append((15, MENU_OPTION_WINDOW + action, rectangle(
            [cells[(top + y) * 32 + left + x] for y in range(bottom - top) for x in range(right - left)],
            right - left, bottom - top, (left * 8, top * 8))))
    glyphs = [0xf000 | 0x80 + (c >> 5) * 64 + (c & 31) + row * 32 for row in range(2) for c in range(96)]
    sections.append((15, MENU_FONT, rectangle(glyphs, 96, 2, (0, 0))))
    border = maps[5]
    sections.append((15, MENU_FRAME, rectangle(
        [border[y * 32 + x] for y in (6, 7, 13) for x in (5, 6, 24)],
        3, 3, (0, 0))))
    return sections


def tab_sections(rom):
    """Tabs at OBJ 0x2400..0x3800: frame head, table graphics, frame tail."""
    def pointer(address):
        return struct.unpack_from('<I', rom, address)[0] - 0x08000000

    def tab(cell, uploads, colors):
        tiles = bytearray(0x4000)
        palette = bytearray(512)
        for address, offset in uploads:
            data = decode_lz77(rom, address)
            tiles[offset:offset+len(data)] = data
        for bank, address in colors:
            palette[bank*32:bank*32+32] = rom[address:address+32]
        return cell_image(rom, cell, tiles, palette)

    sections = []
    for player in range(4):
        sections.append((15, MENU_PLAYER_BANNER+player, tab(0xc8634,
            ((0xb7e00, 0x2400), (pointer(0xd8e9c+player*8), 0x2500), (0xb7e40, 0x2700)),
            ((2, pointer(0xd8ea0+player*8)),))))
    for base, table, count, cell, offset, bank in (
            (MENU_TAB_GAME, 0xd8ebc, 4, 0xc8644, 0x3000, 4),
            (MENU_TAB_MODE, 0xd8edc, 6, 0xc864c, 0x3200, 5),
            (MENU_TAB_CLASS, 0xd8f0c, 3, 0xc8654, 0x3400, 6),
            (MENU_TAB_TRIAL, 0xd8f24, 2, 0xc8654, 0x3400, 6)):
        for index in range(count):
            if struct.unpack_from('<I', rom, table+index*8)[0] == 0:
                continue  # Action 4 has no tab.
            sections.append((15, base+index, tab(cell,
                ((0xb7564, offset), (pointer(table+index*8), offset+0x80), (0xb7594, offset+0x180)),
                ((bank, pointer(table+index*8+4)),))))
    for driver in range(8):
        # 08002EA4 swaps faces 2 and 3; the frame shares the 1-P tab colors.
        face = 0x63834 + (3 if driver == 2 else 2 if driver == 3 else driver) * 128
        tiles = bytearray(0x4000)
        tiles[0x2c00:0x2c80] = rom[face:face+128]
        frame = decode_lz77(rom, 0xb8534)
        tiles[0x3600:0x3600+len(frame)] = frame
        palette = bytearray(512)
        palette[0x60:0x80] = rom[0x627d4:0x627f4]
        colors = pointer(0xd8ec0)
        palette[0x80:0xa0] = rom[colors:colors+32]
        sections.append((15, MENU_TAB_DRIVER+driver, cell_image(rom, 0xc865c, tiles, palette)))
    return sections


def motion_payload(rom):
    """Source menu anchors and Q15 curves, independent of baked sprite canvases."""
    out = bytearray(struct.pack('<4I', 0x314D4546, 9, 14, 5))
    for slot in range(9):
        start = struct.unpack_from('<2h', rom, 0xD91E8 + slot*4)
        end = struct.unpack_from('<2h', rom, 0xD9130 + slot*20 + 12)
        out.extend(struct.pack('<4h', *start, *end))
    for table, count in ((0xD8FE4, 3), (0xD9094, 3), (0xD90D0, 2), (0xD905C, 2),
                         (0xD8F94,1),(0xD8FA8,1),(0xD8FBC,1),(0xD8FD0,1)):
        for row in range(count):
            out.extend(rom[table+row*20+12:table+row*20+16])
    out.extend(rom[0xD8E88:0xD8E88+20])
    out.extend(rom[0xE645C:0xE645C+4096])
    for slot in range(5):
        out.extend(rom[0xD94FC+slot*20+12:0xD94FC+slot*20+16])
    if len(out) != 4280:
        raise ValueError('frontend motion source table truncated')
    return bytes(out)
