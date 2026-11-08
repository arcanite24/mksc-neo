"""Decode sub_804FEA0's seven Lakitu/start-light pictures into native images."""
import struct
from ui_rom import cell_image, image_asset, tile_pixel

START_IMAGE = 0x27000


def finish_lakitu_program(rom, address=0xef578):
    """Normalize EF578 holds and callback phase boundaries, without a runtime VM."""
    rows, cells, boundaries = [], {}, {}
    bank, cell, elapsed = None, None, 0
    callbacks = {0x0805045d: 'entrance', 0x08050df9: 'bob', 0x08050755: 'exit'}
    for turn in range(128):
        command, argument = struct.unpack_from('<II', rom, address+turn*8)
        if command == 0x43:
            if argument not in callbacks or callbacks[argument] in boundaries:
                raise ValueError('invalid finish Lakitu callback')
            boundaries[callbacks[argument]] = elapsed
        elif command == 0x11:
            cell = argument-0x08000000
            if not 0 <= cell < len(rom):
                raise ValueError('finish Lakitu cell outside ROM')
        elif command == 0x41:
            bank = argument
            if not 21 <= bank <= 28 or cell is None or cells.get(bank, cell) != cell:
                raise ValueError('invalid finish Lakitu art bank')
            cells[bank] = cell
        elif command == 0xf4:
            if argument not in (0x0804fea1, 0x08050c45):
                raise ValueError('invalid finish Lakitu art callback')
        elif command == 0xf1:
            if bank is None or not 1 <= argument <= 65535:
                raise ValueError('invalid finish Lakitu hold')
            if rows and rows[-1][0] == bank:
                rows[-1][1] += argument
            else:
                rows.append([bank, argument])
            elapsed += argument
        elif command == 0x42:
            break
        else:
            raise ValueError('unsupported finish Lakitu command')
    else:
        raise ValueError('finish Lakitu command budget exceeded')
    if (set(boundaries) != {'entrance', 'bob', 'exit'} or boundaries['entrance'] != 0 or
            not 20 <= boundaries['bob'] < boundaries['exit'] < elapsed < 65535 or
            not 1 <= len(rows) <= 32 or set(cells) != set(range(21, 29))):
        raise ValueError('invalid finish Lakitu phase schedule')
    data = struct.pack('<4s4HI', b'FLK1', len(rows), boundaries['bob'],
                       boundaries['exit']-boundaries['bob'], elapsed-boundaries['exit'], 0)
    data += b''.join(struct.pack('<BBH', bank, 0, duration) for bank, duration in rows)
    return data, cells


def item_art_intro_payload(rom):
    """The eight F1 holds omitted by a direct race-entry handoff."""
    waits = []
    for address in (0xEFD7C,0xEFD84,0xEF248,0xEF258,0xEF278,0xEF290,0xEF2B0,0xEF2C8):
        command, wait = struct.unpack_from('<II',rom,address)
        if command != 0xf1 or not 1 <= wait <= 8192:
            raise ValueError('invalid item art intro wait')
        waits.append(wait)
    return struct.pack('<4sHH8H',b'IAC1',8,0,*waits)


def recovery_motion_payload(rom):
    tail, _ = finish_lakitu_program(rom)
    return (struct.pack('<II', 0x31434b4c, 16)+rom[0xef050:0xef0d0]+tail+
            item_art_intro_payload(rom))


def countdown_sections(rom):
    # sub_804FEA0 states 0..6 alternate the body and replace the lamp strip.
    palette = rom[0x66ECC:0x66EEC]
    sections = []
    for index, (body, lamps) in enumerate(((0x68F8C, 0x6938C),
                                          (0x6918C, 0x6948C),
                                          (0x68F8C, 0x6948C),
                                          (0x6918C, 0x6958C),
                                          (0x68F8C, 0x6958C),
                                          (0x6918C, 0x6968C),
                                          (0x68F8C, 0x6968C))):
        tiles = rom[body:body + 0x200] + rom[lamps:lamps + 0x80] + rom[0x6940C:0x6948C]
        if index == 0:
            tiles = rom[body:body + 0x200] + rom[lamps:lamps + 0x100]
        if len(tiles) != 0x300 or len(palette) != 32:
            raise ValueError('truncated Lakitu start graphics')
        pixels = [0] * (36 * 37)
        # Source start-light composition: 8x32 lamp, 32x32 body, 8x16 overlay.
        # Normalize their relative origins and tile addresses only at import.
        for ox, oy, width, height, tile in reversed(((0, 5, 8, 32, 16),
                                                    (4, 0, 32, 32, 0),
                                                    (4, 0, 8, 16, 20))):
            for y in range(height):
                for x in range(width):
                    pixel = tile_pixel(tiles, palette,
                        tile + y // 8 * (width // 8) + x // 8, x % 8, y % 8)
                    if pixel:
                        pixels[(oy + y) * 36 + ox + x] = pixel
        sections.append((15, START_IMAGE + index, image_asset(36, 37, pixels, (0, -5))))
    # 804FEA0 banks19/20, 807C830 recovery composition. Normalize the
    # signed origins and tile offsets into an ordinary 32x64 image.
    for bank, body in enumerate((0x6B18C, 0x6B38C)):
        tiles = rom[body:body + 512] + rom[0x6B08C:0x6B18C]
        pixels = [0] * (32 * 64)
        for ox, oy, width, height, tile in reversed(((5, 0, 8, 32, 16),
                                                    (5, 32, 8, 32, 20),
                                                    (0, 12, 32, 32, 0))):
            for y in range(height):
                for x in range(width):
                    pixel = tile_pixel(tiles, palette,
                        tile + y // 8 * (width // 8) + x // 8, x % 8, y % 8)
                    if pixel:
                        pixels[(oy + y) * 32 + ox + x] = pixel
        sections.append((15, START_IMAGE + 16 + bank,
                         image_asset(32, 64, pixels, (-7, -28))))
    # EF0D0 wrong-way banks15..18, 807C820: sign32x16 then body32x32.
    for bank, offset in enumerate((0x6A48C,0x6A78C,0x6AA8C,0x6AD8C)):
        tiles=rom[offset:offset+768]
        if len(tiles)!=768:
            raise ValueError('truncated Lakitu wrong-way graphics')
        pixels=[0]*(33*36)
        for ox,oy,width,height,tile in reversed(((1,20,32,16,16),(0,0,32,32,0))):
            for y in range(height):
                for x in range(width):
                    pixel=tile_pixel(tiles,palette,tile+y//8*(width//8)+x//8,x%8,y%8)
                    if pixel:pixels[(oy+y)*33+ox+x]=pixel
        sections.append((15,START_IMAGE+32+bank,image_asset(33,36,pixels,(-16,-16))))
    # 4FEA0 uploads body and flag banks; source cells retain every signed anchor.
    _, cells = finish_lakitu_program(rom)
    finish_banks = ((0x6b58c,0x6bb8c),(0x6b78c,0x6bc8c),(0x6b78c,0x6bd8c),
                    (0x6b78c,0x6be8c),(0x6b58c,0x6bf8c),(0x6b98c,0x6c08c),
                    (0x6b98c,0x6c18c),(0x6b98c,0x6c28c))
    colors = bytearray(512)
    colors[224:256] = palette
    for index, (body, flag) in enumerate(finish_banks):
        tiles = bytearray(32768)
        tiles[0x2800:0x2a00] = rom[body:body+512]
        tiles[0x2a00:0x2b00] = rom[flag:flag+256]
        sections.append((15, START_IMAGE+48+index,
                         cell_image(rom,cells[21+index],tiles,colors)))
    replay_tiles=rom[0x6500c:0x6524c]
    replay_palette=rom[0x627d4:0x627f4]
    if len(replay_tiles)!=576 or len(replay_palette)!=32:
        raise ValueError('truncated Replay Mode graphics')
    sections.append((15,0x27040,image_asset(72,16,[
        tile_pixel(replay_tiles,replay_palette,y//8*9+x//8,x%8,y%8)
        for y in range(16) for x in range(72)])))
    # EDAC retains this speedometer only during replay; F494 rotates its
    # authored 64x64 needle around the source (24,136) origin.
    dial_tiles=rom[0x66c0c:0x66e4c]
    dial_palette=rom[0x627b4:0x627d4]
    cells=struct.unpack_from('<30H',rom,0xee76c)
    dial=[]
    for y in range(40):
        for x in range(48):
            cell=cells[y//8*6+x//8]
            if cell == 0:
                dial.append(0)
            elif cell>>12 != 12 or not 0x140 <= (cell&1023) < 0x152:
                raise ValueError('invalid replay speedometer cell')
            else:
                dial.append(tile_pixel(dial_tiles,dial_palette,(cell&1023)-0x140,
                    7-x%8 if cell&0x400 else x%8,7-y%8 if cell&0x800 else y%8))
    sections.append((15,0x27041,image_asset(48,40,dial)))
    needle_tiles=bytearray(32768)
    needle_tiles[0x7800:0x8000]=rom[0x6640c:0x66c0c]
    needle_colors=bytearray(512)
    needle_colors[224:256]=rom[0x66ecc:0x66eec]
    sections.append((15,0x27042,cell_image(rom,0xee764,needle_tiles,needle_colors)))
    return sections
