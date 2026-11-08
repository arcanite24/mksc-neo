"""Normalize original tile maps and cell art into native ARGB images."""

import struct

from build_gp_results_pack import decode_lz77

IMAGE_MAGIC = b"MKSCIMG1"
IMAGE_HEADER = 32
TITLE_BACKGROUND = 1
TITLE_LABEL = 0x20000
TITLE_SELECTED_LABEL = 0x20100
TITLE_FRAME = 0x20200
TITLE_UP = 0x20201
TITLE_DOWN = 0x20202
TITLE_LINK_FRAME = 0x20203  # D8C44: the frame title_8000350 draws around the link sub-menu
TITLE_BACKGROUND_FRAME = 0x20300
# isNight/isSunset title variants (title_loadGraphics): base, then flag frames.
TITLE_NIGHT_BACKGROUND = 0x20400
TITLE_SUNSET_BACKGROUND = 0x20500
TITLE_NIGHT_SHIP = 0x20700
TITLE_FALLING_STAR = 0x20701
TITLE_NIGHT_TWINKLE = 0x20600  # +1..7: star colour rows 1..7 over row 0


def image_asset(width, height, pixels, anchor=(0, 0)):
    if not 1 <= width <= 4096 or not 1 <= height <= 4096 or len(pixels) != width * height:
        raise ValueError("invalid native image dimensions")
    return (struct.pack("<8sHHHHhhIII", IMAGE_MAGIC, IMAGE_HEADER, 1,
                        width, height, *anchor, 0, IMAGE_HEADER,
                        IMAGE_HEADER + len(pixels) * 4) +
            struct.pack(f"<{len(pixels)}I", *pixels))


def color(value):
    # Match the existing software renderer's 5-bit expansion.
    r, g, b = value & 31, value >> 5 & 31, value >> 10 & 31
    return 0xFF000000 | ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2)


def tile_pixel(tiles, palette, tile, x, y, palette_bank=0, depth=4):
    offset = tile * (64 if depth == 8 else 32) + y * (8 if depth == 8 else 4) + (x if depth == 8 else x // 2)
    if not 0 <= offset < len(tiles):
        raise ValueError(f"UI tile {tile} exceeds graphics")
    index = tiles[offset] if depth == 8 else tiles[offset] >> (4 * (x & 1)) & 15
    if index == 0:
        return 0
    offset = ((palette_bank * 16 if depth == 4 else 0) + index) * 2
    if offset + 2 > len(palette):
        raise ValueError("UI palette index exceeds colors")
    return color(struct.unpack_from("<H", palette, offset)[0])


def tile_map(tiles, palette, cells, width, height, depth=4):
    if width % 32 or height % 32 or len(cells) != width * height * 2:
        raise ValueError("invalid text tile map size")
    out = [0] * (width * height * 64)
    pixel_width = width * 8
    for cy in range(height):
        for cx in range(width):
            block = (cy // 32) * (width // 32) + cx // 32
            index = block * 1024 + (cy % 32) * 32 + cx % 32
            cell = struct.unpack_from("<H", cells, index * 2)[0]
            for y in range(8):
                for x in range(8):
                    out[(cy * 8 + y) * pixel_width + cx * 8 + x] = tile_pixel(
                        tiles, palette, cell & 1023,
                        7 - x if cell & 0x400 else x,
                        7 - y if cell & 0x800 else y, cell >> 12, depth)
    return width * 8, height * 8, out


def affine_tile_map(tiles, palette, cells, side):
    """Normalize byte-indexed 8bpp affine tiles without text-map bank/flip bits."""
    if side not in (16, 32, 64, 128) or len(cells) != side * side:
        raise ValueError("invalid affine tile map size")
    width = side * 8
    pixels = [0] * (width * width)
    for y in range(width):
        for x in range(width):
            tile = cells[(y // 8) * side + x // 8]
            pixels[y * width + x] = tile_pixel(tiles, palette, tile, x % 8, y % 8, depth=8)
    return width, width, pixels


def _cell_image(rom, address, tiles, palette, palette_base=None):
    """Decode a source CellData list once; no OBJ state reaches the runtime."""
    sizes = (((8, 8), (16, 16), (32, 32), (64, 64)),
             ((16, 8), (32, 8), (32, 16), (64, 32)),
             ((8, 16), (8, 32), (16, 32), (32, 64)))
    count = struct.unpack_from("<H", rom, address)[0]
    if not 1 <= count <= 128 or address + 2 + count * 6 > len(rom):
        raise ValueError("invalid UI cell list")
    parts = []
    for i in range(count):
        a, b, c = struct.unpack_from("<3H", rom, address + 2 + i * 6)
        if a & 0x2000 or a >> 14 == 3:
            raise ValueError("unsupported UI cell format")
        w, h = sizes[a >> 14][b >> 14]
        x, y = b & 511, a & 255
        x = x - 512 if x >= 256 else x
        y = y - 256 if y >= 128 else y
        parts.append((x, y, w, h, b, c))
    left, top = min(p[0] for p in parts), min(p[1] for p in parts)
    width = max(p[0] + p[2] for p in parts) - left
    height = max(p[1] + p[3] for p in parts) - top
    out = [255 if palette_base is not None else 0] * (width * height)
    for ox, oy, w, h, b, c in reversed(parts):
        for y in range(h):
            for x in range(w):
                sx = w - 1 - x if b & 0x1000 else x
                sy = h - 1 - y if b & 0x2000 else y
                pixel = tile_pixel(tiles, palette,
                    (c & 1023) + (sy // 8) * (w // 8) + sx // 8,
                    sx % 8, sy % 8, c >> 12)
                if pixel:
                    if palette_base is not None:
                        tile = (c & 1023) + (sy // 8) * (w // 8) + sx // 8
                        pixel = palette_base + (c >> 12) * 16 + (tiles[tile * 32 + (sy % 8) * 4 + (sx % 8) // 2] >> (4 * (sx & 1)) & 15)
                    out[(oy - top + y) * width + ox - left + x] = pixel
    if palette_base is None:
        return image_asset(width, height, out, (left, top))
    count = len(palette) // 2
    if len(palette) % 2 or not 1 <= count <= 128 or not 0 <= palette_base <= 128-count:
        raise ValueError("invalid indexed image palette")
    return (struct.pack("<8sHHHHhhIII", b"MKSCIMG2", 32, 2, width, height,
        left, top, count | palette_base << 16, 32, 32+len(out)+len(palette)) + bytes(out) + palette)


def cell_image(rom, address, tiles, palette):
    return _cell_image(rom, address, tiles, palette)


def cell_image_indexed(rom, address, tiles, palette, palette_base=0):
    return _cell_image(rom, address, tiles, palette, palette_base)


def title_backgrounds(rom, variant):
    """Flag frames for the normal, sunset or night title palette and sky."""
    palette = bytearray(512)
    p = decode_lz77(rom, {"normal": 0xCDAE4, "sunset": 0xCDB78, "night": 0xCDC0C}[variant])
    palette[:len(p)] = p
    p = decode_lz77(rom, 0xCD9A0)
    palette[128:128 + len(p)] = p
    if variant == "sunset":
        palette[128:160] = decode_lz77(rom, 0xCDA9C)
        palette[224:256] = decode_lz77(rom, 0xCDAC4)
    elif variant == "night":
        palette[128:160] = rom[0xD5CA0:0xD5CC0]
        palette[224:256] = rom[0xD5CC0:0xD5CE0]
    logo_tiles = decode_lz77(rom, 0xCDCA0)
    sky_tiles = bytearray(decode_lz77(rom, 0xCEBB4) + decode_lz77(rom, 0xD0060))
    # The settled title replaces the cloud art with its driver panorama.
    drivers = b"".join(decode_lz77(rom, p) for p in (0xD0DE0, 0xD1D20, 0xD2BA8, 0xD3A90))
    sky_tiles[0x2800:0x2800 + len(drivers)] = drivers
    sky_tiles[:64] = bytes(64)
    sky_map = decode_lz77(rom, 0xD53D8 if variant == "night" else 0xD4E10)
    logo = tile_map(logo_tiles, palette, decode_lz77(rom, 0xD4794), 32, 32)

    def compose(palette):
        layers = [
            (tile_map(sky_tiles, palette, sky_map, 32, 32), 0, 80),
            (tile_map(sky_tiles, palette, decode_lz77(rom, 0xD58F8), 32, 32, depth=8), 0, 96),
            (logo, 0, 0),
        ]
        base = [color(struct.unpack_from("<H", palette)[0])] * (240 * 160)
        for (w, h, pixels), x_origin, y_origin in layers:
            for y in range(160):
                for x in range(240):
                    pixel = pixels[((y + y_origin) % h) * w + (x + x_origin) % w]
                    if pixel:
                        base[y * 240 + x] = pixel
        return base

    base = compose(palette)
    frames, flagged = [], set()
    for phase in range(5):
        address = struct.unpack_from("<I", rom, 0xD8E6C + phase * 4)[0] - 0x08000000
        flag_tiles = bytearray(logo_tiles)
        flag_tiles.extend(bytes(0x2000 - len(flag_tiles)))
        flag_tiles.extend(decode_lz77(rom, address))
        w, h, flag = tile_map(flag_tiles, palette, decode_lz77(rom, 0xD455C), 32, 32)
        pixels = list(base)
        for y in range(160):
            for x in range(240):
                source = flag[((y + 32) % h) * w + x]
                if source and not logo[2][y * 256 + x]:
                    flagged.add(y * 240 + x)
                    target = pixels[y * 240 + x]
                    value = 0xFF000000
                    for shift in (0, 8, 16):
                        value |= (((source >> shift & 255) * 10 +
                                   (target >> shift & 255) * 6) // 16) << shift
                    pixels[y * 240 + x] = value
        frames.append(image_asset(240, 160, pixels))
    twinkles = []
    if variant == "night":
        # Stars cycle bank 7 colours 10..13 through eight rows every 9 frames.
        for row in range(1, 8):
            shifted = bytearray(palette)
            start = 0xD5CC0 + row * 32
            shifted[224 + 20:224 + 28] = rom[start + 20:start + 28]
            changed = [i for i, (a, b) in enumerate(zip(base, compose(shifted)))
                       if a != b and i not in flagged]
            pixels = compose(shifted)
            if not changed:
                changed = [0]
            xs = [i % 240 for i in changed]
            ys = [i // 240 for i in changed]
            x0, y0, x1, y1 = min(xs), min(ys), max(xs) + 1, max(ys) + 1
            keep = set(changed)
            twinkles.append(image_asset(x1 - x0, y1 - y0, [
                pixels[y * 240 + x] if y * 240 + x in keep else 0
                for y in range(y0, y1) for x in range(x0, x1)], (x0, y0)))
    return frames, twinkles


def palette_atlas(rom, cell, tiles, palettes):
    """Vertical palette frames preserve the original authored cell anchor."""
    if not 1 <= len(palettes) <= 32:
        raise ValueError('invalid title palette atlas frame count')
    rows = [cell_image(rom,cell,tiles,p) for p in palettes]
    width,height,ax,ay = struct.unpack_from('<HHhh',rows[0],12)
    pixels=[]
    for row in rows:
        if struct.unpack_from('<HHhh',row,12)!=(width,height,ax,ay):
            raise ValueError('title palette atlas geometry changed')
        pixels.extend(struct.unpack_from(f'<{width*height}I',row,32))
    return image_asset(width,height*len(rows),pixels,(ax,ay))


def title_sections(rom):
    """US title_loadGraphics (08000A34) and title_main (08000E6C) assets."""
    if len(rom) != 0x400000 or rom[0xAC:0xB0] != b"AMKE":
        raise ValueError("title import requires the US ROM")
    sections = []
    for variant, first in (("normal", TITLE_BACKGROUND), ("sunset", TITLE_SUNSET_BACKGROUND),
                           ("night", TITLE_NIGHT_BACKGROUND)):
        frames, twinkles = title_backgrounds(rom, variant)
        for phase, image in enumerate(frames):
            image_id = first if phase == 0 else \
                (TITLE_BACKGROUND_FRAME if variant == "normal" else first) + phase
            sections.append((15, image_id, image))
        for row, image in enumerate(twinkles, 1):
            sections.append((15, TITLE_NIGHT_TWINKLE + row, image))
    object_tiles = bytearray(0x8000)
    for source, destination in ((0xCAE78, 0x1400), (0xCB62C, 0x2900),
            (0xCBA10, 0x3E00), (0xCC1F0, 0x5300), (0xCC748, 0x6800),
            (0xCCCEC, 0x6C00), (0xCC8F4, 0x7400)):
        p = decode_lz77(rom, source)
        object_tiles[destination:destination + len(p)] = p
    object_palette = decode_lz77(rom, 0xCAC64)
    for index in range(3):
        option = rom[0xD8E64 + index]
        for base_id, table in ((TITLE_LABEL, 0xD8E4C), (TITLE_SELECTED_LABEL, 0xD8E34)):
            address = struct.unpack_from("<I", rom, table + option * 4)[0] - 0x08000000
            sections.append((15, base_id + index,
                             cell_image(rom, address, object_tiles, object_palette)))
    for image_id, address in ((TITLE_FRAME, 0xD8B14), (TITLE_UP, 0xD8BAC), (TITLE_DOWN, 0xD8BA4),
                              (TITLE_LINK_FRAME, 0xD8C44)):
        sections.append((15, image_id, cell_image(rom, address, object_tiles, object_palette)))
    # title_8000350 updates bank9 chrome every6 ticks and rotates bank10's
    # four highlight colors every4 ticks. Keep each sprite origin intact.
    chrome_colors = decode_lz77(rom,0xCADC4)
    palettes=[]
    for row in range(16):
        colors=bytearray(object_palette)
        colors[304:320]=chrome_colors[row*32+16:row*32+32]
        palettes.append(colors)
    for index,cell in enumerate((0xD8B14,0xD8BAC,0xD8BA4,0xD8C44)):
        sections.append((15,0x20800+index,palette_atlas(rom,cell,object_tiles,palettes)))
    palettes=[]
    for phase in range(4):
        colors=bytearray(object_palette)
        for i in range(4):
            source=0xCAE70+((i+((~phase)&3))&3)*2
            colors[(175-i)*2:(176-i)*2]=rom[source:source+2]
        palettes.append(colors)
    for index in range(3):
        option=rom[0xD8E64+index]
        cell=struct.unpack_from('<I',rom,0xD8E34+option*4)[0]-0x08000000
        sections.append((15,0x20810+index,palette_atlas(rom,cell,object_tiles,palettes)))
    sections.extend(((15, TITLE_NIGHT_SHIP, cell_image(rom,0xD8DDC,object_tiles,object_palette)),
                     (15, TITLE_FALLING_STAR, cell_image(rom,0xD8B9C,object_tiles,object_palette))))
    return sections
