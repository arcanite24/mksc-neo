"""Normalize sub_80287A8/27A6C ceremony art, without captured display state."""
import struct
from ui_rom import decode_lz77, image_asset, tile_pixel, cell_image
from course_menu_rom import course_sections, COURSE_CUP, COURSE_CUP_NAME
from pause_menu_rom import pause_sections, PAUSE_SELECTED_LABEL


def _legacy_payload(rom):
    def pointer(address):
        value = struct.unpack_from('<I', rom, address)[0] - 0x08000000
        if not 0 <= value < len(rom):
            raise ValueError('ceremony pointer outside ROM')
        return value

    palette = bytearray(512)
    colors = decode_lz77(rom, 0xc18fc)
    palette[0x120:0x120+len(colors)] = colors
    tiles = bytearray(32768)
    for address, offset in ((0xc19c4,0),(0xc1a10,0x2a0),(0xc1a38,0x700),
                            (0xc1ad8,0xb00),(0xc1b44,0xf00),(0xc1bcc,0x1300),
                            (0xc1c50,0x1e40)):
        data = decode_lz77(rom,address)
        tiles[offset:offset+len(data)] = data
    images = []
    # 27A6C writes the eleven rank-label tiles as 3,3,5 cells in bank12.
    for rank in range(8):
        start = pointer(0xee704 + rank*4)
        tiles[0x1c00:0x1c00+0x160] = rom[start:start+0x160]
        pixels = []
        for y in range(24):
            for x in range(40):
                row, column = y//8, x//8
                pixels.append(tile_pixel(tiles,palette,0xe0+row*3+column,x%8,y%8,12)
                              if column < (5 if row == 2 else 3) else 0)
        images.append(image_asset(40,24,pixels))
    # The rating is a nine-tile square, shared by every cup and engine class.
    for rating in range(8):
        data = decode_lz77(rom,pointer(0xdc504+rating*4))
        if len(data) != 9*32:
            raise ValueError('invalid ceremony rating art')
        pixels = [tile_pixel(data,palette,(y//8)*3+x//8,x%8,y%8,13)
                  for y in range(24) for x in range(24)]
        images.append(image_asset(24,24,pixels))
    # C2330's class digits: 50,100,150. Reuse its source cells without text fonts.
    data = decode_lz77(rom,0xc19c4)
    tiles[:len(data)] = data
    data = decode_lz77(rom,0xc1c50)
    tiles[0x1e40:0x1e40+len(data)] = data
    cells = decode_lz77(rom,0xc2330)
    for column, width in ((1,2),(3,3),(6,3)):
        pixels = []
        for y in range(16):
            for x in range(width*8):
                cell = struct.unpack_from('<H',cells,((5+y//8)*32+column+x//8)*2)[0]
                pixels.append(tile_pixel(tiles,palette,cell&1023,x%8,y%8,cell>>12))
        images.append(image_asset(width*8,16,pixels))
    menu = {(kind,ident):data for kind,ident,data in course_sections(rom)}
    images += [menu[15,COURSE_CUP_NAME+cup] for cup in range(5)]
    images += [menu[15,COURSE_CUP+cup] for cup in range(5)]
    # The cheering audience is the original two-frame celebration cell pair.
    obj_tiles = decode_lz77(rom,0xc0910) + bytes(32768)
    obj_palette = decode_lz77(rom,0xbf5a4)
    for cell in (0xc997c,0xc9984):
        images.append(cell_image(rom,cell,obj_tiles,obj_palette))
    images.append(next(data for kind, ident, data in pause_sections(rom)
                       if ident == PAUSE_SELECTED_LABEL))
    # Result coin marker and digits, normalized into native eight-pixel images.
    hud_tiles=rom[0x62834:0x64834]
    hud_palette=bytearray(512)
    hud_palette[0x160:0x180]=rom[0x62794:0x627b4]
    hud_palette[0x180:0x1a0]=rom[0x627b4:0x627d4]
    for tile, bank in [(0x20,12)]+[(0x10+digit,11) for digit in range(10)]:
        images.append(image_asset(8,8,[tile_pixel(hud_tiles,hud_palette,tile,x,y,bank)
            for y in range(8) for x in range(8)]))
    assert len(images) == 43
    # Native presentation timing and colors; no original loading chronology.
    return (struct.pack('<8I',0x33435047,43,120,180,32,0xff182848,0xff506830,0xffe8d890) +
            b''.join(struct.pack('<I',len(image))+image for image in images))


def _timed_actor_tracks(rom, pointer, cell):
    frames = []
    tracks = []
    for index in range(8):
        x, y = struct.unpack_from("<hh", rom, 0xDC014 + index * 16)
        sequence = pointer(0xDC018 + index * 16)
        initial, remaining, bias = struct.unpack_from("<HHh", rom, 0xDC01C + index * 16)
        first = len(frames)
        for frame in range(32):
            source = struct.unpack_from("<I", rom, sequence + frame * 8)[0]
            if not source:
                break
            duration = struct.unpack_from("<H", rom, sequence + frame * 8 + 4)[0]
            frames.append((cell(source - 0x08000000), duration))
        count = len(frames) - first
        if not count or initial >= count or len(frames) > 32:
            raise ValueError("invalid ceremony actor sequence")
        tracks.append((x, y, bias, first, count, initial, remaining, 0))
    return frames, tracks

def _background_panels(rom, bg_tiles, bg_palette, append):
    panels = []
    for address, offset in ((0xC2E60, 0x180), (0xC2420, 0), (0xC2654, 0)):
        cells = bytearray(32 * 20 * 2)
        data = decode_lz77(rom, address)
        cells[offset : offset + len(data)] = data
        if address != 0xC2E60:
            for x, y, width, height in (
                (72, 80, 24, 16),
                (208, 48, 24, 24),
                (216, 72, 24, 16),
            ):
                for cy in range(y // 8, (y + height) // 8):
                    for cx in range(x // 8, (x + width) // 8):
                        struct.pack_into("<H", cells, (cy * 32 + cx) * 2, 0)
            for row in range(4):
                for x, width in ((112, 16), (136, 56), (200, 8), (224, 16)):
                    for cy in range(12 + row * 2, 14 + row * 2):
                        for cx in range(x // 8, (x + width) // 8):
                            struct.pack_into("<H", cells, (cy * 32 + cx) * 2, 0)
        pixels = []
        for y in range(160):
            for x in range(240):
                value = struct.unpack_from("<H", cells, ((y // 8) * 32 + x // 8) * 2)[0]
                pixels.append(
                    tile_pixel(
                        bg_tiles,
                        bg_palette,
                        value & 1023,
                        7 - x % 8 if value & 0x400 else x % 8,
                        7 - y % 8 if value & 0x800 else y % 8,
                        value >> 12,
                    )
                )
        panels.append(append(image_asset(240, 160, pixels)))
    return panels

def _header_targets(rom):
    targets = []
    for cell_address, origin, count in ((0xC97AC, 160, 16), (0xC9890, 136, 7)):
        parts = struct.unpack_from("<H", rom, cell_address)[0]
        for i in range(count):
            a, b, _ = struct.unpack_from(
                "<3H", rom, cell_address + 2 + min(i, parts - 1) * 6
            )
            x = b & 511
            x = -(x ^ 511) if x > 255 else x
            y = (a & 255) - 256 if (a & 255) >= 128 else a & 255
            targets.append((origin + x, 32 + y))
    return targets

def ceremony_payload(rom):
    """GPC4 appends authored ceremony layers and bounded controller data."""

    def pointer(address):
        value = struct.unpack_from("<I", rom, address)[0] - 0x08000000
        if not 0 <= value < len(rom):
            raise ValueError("ceremony pointer outside ROM")
        return value

    images = []
    darkenable = []

    def append(data, darken=0):
        images.append(data)
        darkenable.append(darken)
        return 43 + len(images) - 1

    tiles = bytearray(32768)
    for address, offset in (
        (0xBF788, 0x2000),
        (0xBF7EC, 0x2800),
        (0xBFFA8, 0x6000),
        (0xC0910, 0),
    ):
        data = decode_lz77(rom, address)
        tiles[offset : offset + len(data)] = data
    palette = bytearray(decode_lz77(rom, 0xBF5A4))

    def cell(address):
        count = struct.unpack_from("<H", rom, address)[0]
        banks = [
            struct.unpack_from("<H", rom, address + 6 + i * 6)[0] >> 12
            for i in range(count)
        ]
        darken = int(all(bank not in (5, 6, 7, 12, 13, 14, 15) for bank in banks))
        return append(cell_image(rom, address, tiles, palette), darken)

    # Every cup has its own graphics at the same authored cell positions.
    headers = []
    cup_labels = []
    for cup in range(5):
        data = decode_lz77(rom, pointer(0xDC4F0 + cup * 4))
        tiles[0x2600 : 0x2600 + len(data)] = data
        cup_labels.append(cell(0xC9944))
        headers.append(
            [cell(pointer(0xDBA64 + i * 4)) for i in range(16)]
            + [cell(pointer(0xDBAA4 + i * 4)) for i in range(7)]
        )
    fly = []
    for i in range(8):
        data = decode_lz77(rom, pointer(0xDBCE0 + i * 4))
        tiles[0x5800 : 0x5800 + len(data)] = data
        fly.append(cell(0xC9974))
    trophy = []
    for rank in range(3):
        source = pointer(0xDBE28 + rank * 4)
        palette[0x1E0:0x200] = rom[
            pointer(0xDBE38 + rank * 4) : pointer(0xDBE38 + rank * 4) + 32
        ]
        row = []
        for frame in range(16):
            data = decode_lz77(rom, pointer(source + frame * 4))
            tiles[0x2200 : 0x2200 + len(data)] = data
            row.append(cell(0xC996C))
        trophy.append(row)
    drop = cell(0xC979C)
    shadow = cell(0xC9954)
    sparkles = []
    confetti = []
    data = decode_lz77(rom, 0xC17B8)
    tiles[0x2000 : 0x2000 + len(data)] = data
    for phase in range(12):
        start = pointer(0xDBD38 + phase * 4)
        palette[0xE0:0x100] = rom[start : start + 32]
        sparkles.append(cell(0xC993C))
        confetti.append(cell(0xC9934))
    # Timed actors use constructor graphics; never stale trophy uploads.
    for address, offset in (
        (0xBF788, 0x2000),
        (0xBF7EC, 0x2800),
        (0xBFFA8, 0x6000),
        (0xC0910, 0),
    ):
        data = decode_lz77(rom, address)
        tiles[offset : offset + len(data)] = data
    palette = bytearray(decode_lz77(rom, 0xBF5A4))
    frames, tracks = _timed_actor_tracks(rom, pointer, cell)
    bg_palette = bytearray(512)
    data = decode_lz77(rom, 0xC18FC)
    bg_palette[0x120 : 0x120 + len(data)] = data
    bg_tiles = bytearray(rom[0x62834:0x66834]) + bytearray(16384)
    bg_tiles[0x2000:0x3000] = rom[0xABEFC:0xACEFC]
    for address, offset in (
        (0xC19C4, 0),
        (0xC2858, 0x160),
        (0xC1A10, 0x2A0),
        (0xC1A38, 0x700),
        (0xC1AD8, 0xB00),
        (0xC1B44, 0xF00),
        (0xC1BCC, 0x1300),
        (0xC1C50, 0x1E40),
        (0xC2E60, 0x2180),
    ):
        data = decode_lz77(rom, address)
        bg_tiles[offset : offset + len(data)] = data
    panels = _background_panels(rom, bg_tiles, bg_palette, append)
    glyphs = []
    rank_labels = []
    template = decode_lz77(rom, 0xC2330)
    for index in range(20):
        width = 8 if index < 12 else 16
        pixels = []
        for y in range(16):
            for x in range(width):
                if index < 12:
                    at = ((10 + y // 8) * 32 + index) * 2
                else:
                    at = ((3 + y // 8) * 32 + (index - 12) * 2 + x // 8) * 2
                value = struct.unpack_from("<H", template, at)[0]
                pixels.append(
                    tile_pixel(
                        bg_tiles, bg_palette, value & 1023, x % 8, y % 8, value >> 12
                    )
                )
        (glyphs if index < 12 else rank_labels).append(
            append(image_asset(width, 16, pixels))
        )
    summary_coin = append(
        image_asset(
            8,
            16,
            [
                tile_pixel(bg_tiles, bg_palette, 0x15 + y // 8, x, y % 8, 12)
                for y in range(16)
                for x in range(8)
            ],
        )
    )
    # Dynamic source coin pairs use OBJ bank12; the final total and unlock
    # text retain local indices so their source palette pulse stays lossless.
    data = decode_lz77(rom, 0xC17E0)
    tiles[0x5800 : 0x5800 + len(data)] = data
    palette = bytearray(decode_lz77(rom, 0xBF5A4))
    coin_digits = [cell(pointer(0xDBBB8 + i * 4)) for i in range(10)]

    def indexed_map(cells, cx, cy, width, height, bank):
        pixels = []
        for y in range(height):
            for x in range(width):
                v = struct.unpack_from(
                    "<H", cells, ((cy + y // 8) * 32 + cx + x // 8) * 2
                )[0]
                sx = 7 - x % 8 if v & 0x400 else x % 8
                sy = 7 - y % 8 if v & 0x800 else y % 8
                nibble = (
                    bg_tiles[(v & 1023) * 32 + sy * 4 + sx // 2] >> (4 * (sx & 1)) & 15
                )
                pixels.append(nibble if nibble else 255)
        colors = bg_palette[bank * 32 : (bank + 1) * 32]
        return append(
            struct.pack(
                "<8sHHHHhhIII",
                b"MKSCIMG2",
                32,
                2,
                width,
                height,
                0,
                0,
                16,
                32,
                32 + len(pixels) + 32,
            )
            + bytes(pixels)
            + colors
        )

    total_digits = [indexed_map(template, i, 10, 8, 16, 15) for i in range(10)]
    highlight_classes = [
        indexed_map(template, x, 5, w * 8, 16, 14) for x, w in ((1, 2), (3, 3), (6, 3))
    ]
    unlock_image = indexed_map(decode_lz77(rom, 0xC2830), 0, 0, 120, 8, 14)
    pulse_colors = (
        rom[0xAAD9C:0xAADBC]
        + rom[0xAADDC:0xAADFC]
        + rom[0xAAD3C:0xAAD5C]
        + rom[0xAAD7C:0xAAD9C]
    )
    # Metadata is fixed-width and all image references are local native IDs.
    meta = struct.pack("<4I", 0x31524543, 100, len(images), len(frames))
    meta += struct.pack("<115H", *(v for row in headers for v in row))
    meta += struct.pack("<8H", *fly) + struct.pack(
        "<48H", *(v for row in trophy for v in row)
    )
    meta += struct.pack("<4H", 0, drop, sparkles[0], confetti[0])
    meta += rom[0xDBE34:0xDBE37] + bytes(1)
    meta += b"".join(struct.pack("<hhh5H", *row) for row in tracks)
    meta += b"".join(struct.pack("<2H", *row) for row in frames)
    meta += struct.pack("<3H", *panels) + bytes(2)
    # Sparkle/confetti images are interleaved, so retain explicit references.
    meta += struct.pack("<12H", *sparkles) + struct.pack("<12H", *confetti)
    meta += rom[0xDC22C:0xDC250] + struct.pack("<2H", shadow, summary_coin)
    targets = _header_targets(rom)
    meta += b"".join(struct.pack("<2h", *row) for row in targets)
    meta += struct.pack("<25H", *cup_labels, *glyphs, *rank_labels)
    meta += (
        struct.pack(
            "<24H", *coin_digits, *total_digits, *highlight_classes, unlock_image
        )
        + pulse_colors
    )
    meta += bytes(darkenable)
    legacy = bytearray(_legacy_payload(rom))
    struct.pack_into("<I", legacy, 0, 0x34435047)
    return (
        bytes(legacy) + meta + b"".join(struct.pack("<I", len(i)) + i for i in images)
    )
