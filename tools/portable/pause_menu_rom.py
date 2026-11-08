"""Normalize the US Time Trial pause art (802C06C/802CB7C/802CF78)."""
import struct
from ui_rom import image_asset, tile_pixel

PAUSE_PANEL = 0x24000
PAUSE_LABEL = 0x24100
PAUSE_SELECTED_LABEL = 0x24200
PAUSE_CURSOR = 0x24300


def pause_sections(rom):
    # Single-player tile uploads and the two pause palette banks. The ROM's
    # default menu lists Continue, Try Again, Change Track, Change Char, Quit.
    tiles = rom[0xaae3c:0xab63c] + rom[0xab63c:0xab7fc] + rom[0xad6fc:0xad8bc]
    palette = bytearray(512)
    palette[0x1c0:0x200] = rom[0xaacf0:0xaad30]

    def rectangle(cells, columns, rows, selected=False):
        pixels = []
        indices = []
        for y in range(rows * 8):
            for x in range(columns * 8):
                cell = cells[(y // 8) * columns + x // 8]
                if selected:
                    sx=7-x%8 if cell&0x400 else x%8
                    sy=7-y%8 if cell&0x800 else y%8
                    color=tiles[(cell&1023)*32+sy*4+sx//2]>>(4*(sx&1))&15
                    indices.append(color if color else 255)
                pixels.append(tile_pixel(tiles, palette, cell & 1023,
                    7-x%8 if cell & 0x400 else x%8,
                    7-y%8 if cell & 0x800 else y%8,
                    14 if selected else cell >> 12))
        if selected:
            return struct.pack('<8sHHHHhhIII',b'MKSCIMG2',32,2,columns*8,rows*8,
                0,0,32,32,32+len(indices)+64)+bytes(indices)+palette[0x1c0:0x200]
        return image_asset(columns * 8, rows * 8, pixels)

    def panel(height, width=10):
        cells = [0] * (width * height)
        for y in range(height):
            if y in (0, height-1):
                row = [0xf001,0xf002] + [0xf022]*(width-4) + [0xf402,0xf401]
                if y: row = [c ^ 0x800 for c in row]
            else:
                edge = 0xf021 if y == 1 else 0xf821 if y == height-2 else 0xf020
                row = [edge] + [0]*(width-2) + [edge ^ 0x400]
            cells[y*width:(y+1)*width] = row
        return rectangle(cells, width, height)

    def label(action, row):
        pattern = rom[0xddee8 + action]
        cells = struct.unpack_from('<14H', rom, 0xddf58 + pattern*28)
        return [(15,base+row,rectangle(cells,7,2,selected))
                for base, selected in ((PAUSE_LABEL,False),(PAUSE_SELECTED_LABEL,True))]

    # Descriptors: x, y, width, height, count, pad, item pointer.
    layouts = ((0xddf18, (10, 4, 10, 12, 5), 0xddf10),   # Time Trial
               (0xddef4, (10, 4, 10, 8, 3), 0xddef1),    # GP with lives
               (0xddf04, (10, 5, 10, 6, 2), 0xddf00))    # GP without lives
    for address, layout, items in layouts:
        if tuple(rom[address:address+5]) != layout or \
           struct.unpack_from('<I', rom, address+8)[0] != 0x8000000 + items:
            raise ValueError('invalid pause layout')
    if tuple(rom[0xddef1:0xddef4]) != (0, 5, 1) or tuple(rom[0xddf00:0xddf02]) != (0, 1):
        raise ValueError('invalid GP pause items')
    sections = [(15, PAUSE_PANEL + index, panel(layout[3]))
                for index, (_, layout, _) in enumerate(layouts)]
    # Time Trial rows are the action order Continue, Retry, Track, Character, Quit.
    for row, action in enumerate(rom[0xddf10:0xddf15]):
        sections += label(action, row)
    sections += label(5, 5)  # GP Retry
    cursor = struct.unpack_from('<2H',rom,0xde054)
    sections.append((15,PAUSE_CURSOR,rectangle(cursor,1,2)))
    return sections
