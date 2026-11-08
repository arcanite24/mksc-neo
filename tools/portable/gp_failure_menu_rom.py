"""Normalize GP retry/quit art from sub_801DC7C ROM tile patterns."""
import struct
from ui_rom import decode_lz77, image_asset, tile_pixel, tile_map

GP_FAILURE_MENU = 0x25000


def failure_sections(rom):
    # Source uploads use tile indices 0x100..0x15f and palette banks E/F.
    tiles = bytes(0x2000) + rom[0xaae3c:0xab63c] + rom[0xada7c:0xade7c]
    palette = bytearray(512)
    palette[0x1c0:0x200] = rom[0xaacf0:0xaad30]
    sections = []
    for page, address in enumerate((0xdb268, 0xdb348)):
        width, height = struct.unpack_from('<2H', rom, address)
        if (width, height) != (12, 6):
            raise ValueError('invalid GP failure menu pattern')
        original = struct.unpack_from('<72H', rom, address+4)
        for choice in range(2):
            cells = list(original)
            row = 1+choice*2 if page == 0 else 3
            column = 3 if page == 0 else 2+choice*5
            count = 7 if page == 0 else 3
            for y in range(row, row+2):
                for x in range(column, column+count):
                    cells[y*width+x] = (cells[y*width+x] & 0xfff) | 0xe000
            cells[row*width+column-1] = 0xf103
            cells[(row+1)*width+column-1] = 0xf903
            indices = []
            for y in range(height*8):
                for x in range(width*8):
                    cell = cells[(y//8)*width+x//8]
                    if cell==0:
                        indices.append(255)
                        continue
                    sx=7-x%8 if cell & 0x400 else x%8
                    sy=7-y%8 if cell & 0x800 else y%8
                    color=tiles[(cell&1023)*32+sy*4+sx//2]>>(4*(sx&1))&15
                    if cell>>12 not in (14,15):
                        raise ValueError('invalid GP failure palette bank')
                    indices.append(255 if color==0 else ((cell>>12)-14)*16+color)
            # Pages: retry, quit, confirm YES, confirm NO.
            sections.append((15, GP_FAILURE_MENU+page*2+choice,
                             struct.pack('<8sHHHHhhIII',b'MKSCIMG2',32,2,width*8,height*8,
                                 0,0,32,32,32+len(indices)+64)+bytes(indices)+palette[0x1c0:0x200]))
    # sub_801E7C8 states 31/32: normalize Game Over directly from source art.
    tiles = bytes(0x1000) + decode_lz77(rom, 0xc2fa8)
    palette = bytearray(512)
    palette[0x140:0x160] = rom[0xc3934:0xc3954]
    cells = decode_lz77(rom, 0xc3954)
    cells += bytes(2048-len(cells))
    width, _, pixels = tile_map(tiles, palette, cells, 32, 32)
    cropped = [pixels[y*width+x] for y in range(160) for x in range(240)]
    sections.append((15, GP_FAILURE_MENU+4, image_asset(240, 160, cropped)))
    # 1E7C8 state29 draws B0A0..B0A3, retaining the original HUD flag bank.
    flag_tiles = rom[0x63c34:0x63cb4]
    flag_palette = rom[0x62794:0x627b4]
    if len(flag_tiles) != 128 or len(flag_palette) != 32:
        raise ValueError('truncated GP credit flag graphics')
    pixels = [tile_pixel(flag_tiles, flag_palette, y//8*2+x//8, x%8, y%8)
              for y in range(16) for x in range(16)]
    sections.append((15, GP_FAILURE_MENU+5, image_asset(16, 16, pixels)))
    return sections
