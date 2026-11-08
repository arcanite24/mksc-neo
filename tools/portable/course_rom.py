"""Decode original course containers into the native runtime section formats.

Source layouts follow track_asset, track_raster, terrain_asset, sky_asset and
content_import.c. No compiled exporter, extracted files or reference run is used.
"""
import struct

from build_gp_results_pack import decode_lz77
from ui_rom import cell_image, cell_image_indexed
import ambient_rom
from rom_course_art import rom_grid


# loadtrack_loadObjects class 0x0c constructors, in source variant order.
STATIC_SCENERY_DESCRIPTORS = (0xF09E8, 0xF0148, 0xF04E8, 0xEFDE8,
                            0xF0508, 0xF0168, 0xF0188, 0xF01A8,
                            0xF0D48, 0xF0DC8, 0xF0E08, 0xF0448)


# 532DC: common59E80 props and inert scenery constructors.
# Decorations retain source art/height but never enter the occupied-tile list.
STATIC_OBJECT_DESCRIPTORS = {
    0x010000: 0xF06A8, 0x010001: 0xF06C8, 0x020000: 0xF0488,
    0x070000: 0xF0468, 0x0B0000: 0xF0408, 0x0B0001: 0xF0428,
    0x850000: 0xEFE48, 0x8C0000: 0xEFE48, 0x900000: 0xEFE48,
    0xA00000: 0xF0E28, 0xA10000: 0xF0E68, 0xA10001: 0xF0E68,
    0x9A0000: 0xF0BC8, 0x9B0000: 0xF0BE8,
    0x040000: 0xF0FA8, 0x050000: 0xF0208, 0x050001: 0xF0228,
    0x050002: 0xF0248, 0x060000: 0xF03C8, 0x060001: 0xF03E8,
    0x080000: 0xF04A8, 0x090000: 0xF04C8,
    **{0x0A0000 + variant: descriptor for variant, descriptor in enumerate(
        (0xF02C8, 0xF02E8, 0xF0308, 0xF0328, 0xF0348, 0xF0368, 0xF0388, 0xF03A8))},
    **{0x0C0000 + variant: descriptor for variant, descriptor in enumerate(STATIC_SCENERY_DESCRIPTORS)},
    0x0E0000: 0xF0FC8, 0x860000: 0xEFF88, 0x980000: 0xF0AA8, 0x980001: 0xF0AC8,
    0xA30000: 0xF0FE8, 0x030000: 0xF0128,
    **{0xA20000 + variant: 0xF0EE8 for variant in range(4)},
}
# 58AB0 scrap frames, then 58E10's seven single-cell Shy Guy rider views.
TEEPEE_FRAMES = (0xF0CE8, 0xF0D08, 0xF0D28)
TEEPEE_RIDER_CELLS = tuple(0x7C540 + 8 * view for view in range(7))


# Classes with a native owner: static props above plus one section family each.
PLACED_OBJECT_CLASSES = {code >> 16 for code in STATIC_OBJECT_DESCRIPTORS} | {
    0x80, 0x81, 0x82, 0x84, 0x87, 0x8A, 0x8D, 0x8E, 0x8F, 0x92, 0x97, 0x9C}


def static_scenery(rom, objects, object_code, tiles, palette):
    placements, descriptors = [], []
    for order, (kind, x, y, parameter) in enumerate(objects):
        code = object_code(kind)
        descriptor = STATIC_OBJECT_DESCRIPTORS.get(code)
        if descriptor is None:
            if code >> 16 in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 0x85, 0x86, 0x8C, 0x90, 0x98, 0x9A, 0x9B, 0xA0, 0xA1, 0xA2, 0xA3):
                raise ValueError('unknown static scenery variant')
            continue
        if descriptor not in descriptors:
            descriptors.append(descriptor)
            if code == 0x860000:
                descriptors.append(0xEFF68)  # 55FC0 alternate contact state
            elif code >> 16 == 0xA0:
                descriptors.append(0xF0E48)
            elif code >> 16 == 0xA1:
                descriptors.extend((0xF0E88, 0xF0EA8, 0xF0EC8))
            elif code >> 16 == 0xA2:
                descriptors.extend((0xF0F28, 0xF0F48, 0xF0F68, 0xF0F88))  # 599EC impact
            elif code >> 16 == 3:
                descriptors.extend(TEEPEE_FRAMES)
                descriptors.extend(('cell', cell) for cell in TEEPEE_RIDER_CELLS)
        scale = 224 if (code >> 16 == 12 and code != 0x0C000B) or code >> 16 in (0xA0, 0xA1, 0xA2) else (
            184 if code >> 16 == 6 else 256)
        placements.append(struct.pack('<iiBBHIB3x', (x * 8 + 4) << 16,
                                      (y * 8 + 4) << 16, parameter,
                                      descriptors.index(descriptor), scale, code, order))
    payload = words(0x314E4353, len(placements), len(descriptors), 0)
    payload += b''.join(placements)
    for descriptor in descriptors:
        if isinstance(descriptor, tuple):
            image = cell_image_indexed(rom.data, descriptor[1], tiles, palette)
            payload += (words(0, len(image)) + image) * 4
            continue
        for lod in range(4):
            cell = rom.pointer(descriptor + lod * 8)
            shift = rom.u32(descriptor + lod * 8 + 4)
            if shift > 3:
                raise ValueError('invalid static scenery distance shift')
            image = cell_image_indexed(rom.data, cell, tiles, palette)
            payload += words(shift, len(image)) + image
    return payload


def lava_bubbles(rom, objects, object_code, tiles, palette):
    placements = []
    for order, (kind, x, y, checkpoint) in enumerate(objects):
        code = object_code(kind)
        if code >> 16 == 0x82:
            variant = code & 65535
            if variant > 12:
                raise ValueError('unknown lava bubble variant')
            placements.append(struct.pack('<iiHBB', (x * 8 + 4) << 16,
                (y * 8 + 4) << 16, variant, checkpoint, order))
    if not placements:
        return None
    motion = words(0x3142564C, len(placements)) + b''.join(placements)
    art = words(0x3141424C, 2)
    for descriptor in (0xEFF08, 0xEFF28):
        for lod in range(4):
            image = cell_image_indexed(rom.data, rom.pointer(descriptor + lod * 8), tiles, palette)
            art += words(rom.u32(descriptor + lod * 8 + 4), len(image)) + image
    return motion, art


def thwomps(rom, objects, object_code, tiles, palette):
    placements = []
    for order, (kind, x, y, checkpoint) in enumerate(objects):
        code = object_code(kind)
        if code >> 16 == 0x81:
            variant = code & 65535
            if variant > 11:
                raise ValueError('unknown Thwomp variant')
            placements.append(struct.pack('<iiHBB', (x * 8 + 4) << 16,
                (y * 8 + 4) << 16, variant, checkpoint, order))
    if not placements:
        return None
    motion = words(0x31505754, len(placements)) + b''.join(placements)
    art = words(0x31415754, 1)
    descriptor = 0xEFEE8
    for lod in range(4):
        image = cell_image_indexed(rom.data, rom.pointer(descriptor + lod * 8), tiles, palette)
        art += words(rom.u32(descriptor + lod * 8 + 4), len(image)) + image
    return motion, art



def crabs(rom, objects, object_code, tiles, palette):
    placements = []
    for order, (kind, x, y, checkpoint) in enumerate(objects):
        code = object_code(kind)
        if code >> 16 == 0x80:
            variant = code & 65535
            if variant not in (0, 1, 0x8000, 0x8001, 0x8002):
                raise ValueError('unknown crab variant')
            placements.append(struct.pack('<iiHBB', (x * 8 + 4) << 16,
                (y * 8 + 4) << 16, variant, checkpoint, order))
    if not placements:
        return None
    motion = words(0x31425243, len(placements)) + b''.join(placements)
    art = words(0x31415243, 4)
    for descriptor in (0xEFFC8, 0xEFFE8, 0xF0008, 0xF0028):
        for lod in range(4):
            image = cell_image_indexed(rom.data, rom.pointer(descriptor + lod * 8), tiles, palette)
            art += words(rom.u32(descriptor + lod * 8 + 4), len(image)) + image
    return motion, art


def seagulls(rom, objects, object_code, tiles, palette):
    placements = []
    for order, (kind, x, y, checkpoint) in enumerate(objects):
        code = object_code(kind)
        if code >> 16 == 0x87:
            variant = code & 65535
            if variant != 0:
                raise ValueError('unknown seagull variant')
            placements.append(struct.pack('<iiHBB', (x * 8 + 4) << 16,
                (y * 8 + 4) << 16, variant, checkpoint, order))
    if not placements:
        return None
    motion = words(0x314C4753, len(placements)) + b''.join(placements)
    art = words(0x31414753, 3)
    for descriptor in (0xF0528, 0xF0548, 0xF0568):
        for lod in range(4):
            image = cell_image_indexed(rom.data, rom.pointer(descriptor + lod * 8), tiles, palette)
            art += words(rom.u32(descriptor + lod * 8 + 4), len(image)) + image
    return motion, art

def piranhas(rom, objects, object_code, tiles, palette):
    placements = []
    for order, (kind, x, y, checkpoint) in enumerate(objects):
        code = object_code(kind)
        if code >> 16 == 0x8A:
            variant = code & 65535
            if variant != 0:
                raise ValueError('unknown Piranha Plant variant')
            placements.append(struct.pack('<iiHBB', (x * 8 + 4) << 16,
                (y * 8 + 4) << 16, variant, checkpoint, order))
    if not placements:
        return None
    motion = words(0x314E5250, len(placements)) + b''.join(placements)
    art = words(0x31415250, 5)
    for descriptor in (0xF0588, 0xF05A8, 0xF05C8, 0xF05E8, 0xF0608):
        for lod in range(4):
            image = cell_image_indexed(rom.data, rom.pointer(descriptor + lod * 8), tiles, palette)
            art += words(rom.u32(descriptor + lod * 8 + 4), len(image)) + image
    return motion, art

def snowmen(rom, objects, object_code, tiles, palette):
    placements = []
    for order, (kind, x, y, checkpoint) in enumerate(objects):
        code = object_code(kind)
        if code >> 16 == 0x92:
            variant = code & 65535
            if variant != 0:
                raise ValueError('unknown snowman variant')
            placements.append(struct.pack('<iiHBB', (x * 8 + 4) << 16,
                (y * 8 + 4) << 16, variant, checkpoint, order))
    if not placements:
        return None
    motion = words(0x314D4E53, len(placements)) + b''.join(placements)
    art = words(0x31414E53, 2)
    for descriptor in (0xF0668, 0xF0688):
        for lod in range(4):
            image = cell_image_indexed(rom.data, rom.pointer(descriptor + lod * 8), tiles, palette)
            art += words(rom.u32(descriptor + lod * 8 + 4), len(image)) + image
    return motion, art

def mice(rom, objects, object_code, tiles, palette):
    placements = []
    for order, (kind, x, y, checkpoint) in enumerate(objects):
        code = object_code(kind)
        if code >> 16 == 0x8E:
            variant = code & 65535
            if variant not in (0, 1, 0x8001):
                raise ValueError('unknown mouse variant')
            placements.append(struct.pack('<iiHBB', (x * 8 + 4) << 16,
                (y * 8 + 4) << 16, variant, checkpoint, order))
    if not placements:
        return None
    motion = words(0x3153554D, len(placements)) + b''.join(placements)
    art = words(0x3141534D, 2)
    for descriptor in (0xF01C8, 0xF01E8):
        for lod in range(4):
            image = cell_image_indexed(rom.data, rom.pointer(descriptor + lod * 8), tiles, palette)
            art += words(rom.u32(descriptor + lod * 8 + 4), len(image)) + image
    return motion, art

def falling_stars(rom, objects, object_code, tiles, palette):
    placements = []
    for order, (kind, x, y, checkpoint) in enumerate(objects):
        code = object_code(kind)
        if code >> 16 == 0x97:
            variant = code & 65535
            if variant not in range(6):
                raise ValueError('unknown falling star variant')
            placements.append(struct.pack('<iiHBB', (x * 8 + 4) << 16,
                (y * 8 + 4) << 16, variant, checkpoint, order))
    if not placements:
        return None
    motion = words(0x31525453, len(placements)) + b''.join(placements)
    art = words(0x31415253, 5)
    for descriptor in range(0xF0A08, 0xF0AA8, 32):
        for lod in range(4):
            image = cell_image_indexed(rom.data, rom.pointer(descriptor + lod * 8), tiles, palette)
            art += words(rom.u32(descriptor + lod * 8 + 4), len(image)) + image
    return motion, art

def thunderclouds(rom, objects, object_code, tiles, palette):
    placements = []
    for order, (kind, x, y, checkpoint) in enumerate(objects):
        code = object_code(kind)
        if code >> 16 == 0x8F:
            variant = code & 65535
            if variant not in (0, 1, 2, 3):
                raise ValueError('unknown thundercloud variant')
            placements.append(struct.pack('<iiHBB', (x * 8 + 4) << 16,
                (y * 8 + 4) << 16, variant, checkpoint, order))
    if not placements:
        return None
    motion = words(0x31444354, len(placements)) + b''.join(placements)
    # Initial palette plus eight source five-color rows. Both banks are
    # patched together here; the simulation selects each bank independently.
    art = words(0x31414354, 72)
    for phase in range(9):
        colors = bytearray(palette)
        if phase:
            row = rom.span(0x76BCA + (phase-1)*10, 10)
            colors[0x22:0x2C] = row
            colors[0x42:0x4C] = row
        for descriptor in range(0xF08E8, 0xF09E8, 32):
            for lod in range(4):
                image = cell_image_indexed(rom.data, rom.pointer(descriptor + lod*8), tiles, colors)
                art += words(rom.u32(descriptor + lod*8 + 4), len(image)) + image
    return motion, art

def penguins(rom, objects, object_code, tiles, palette):
    placements = []
    for order, (kind, x, y, checkpoint) in enumerate(objects):
        code = object_code(kind)
        if code >> 16 == 0x9C:
            variant = code & 65535
            if variant > 1:
                raise ValueError('unknown penguin variant')
            placements.append(struct.pack('<iiHBB', (x * 8 + 4) << 16,
                (y * 8 + 4) << 16, variant, checkpoint, order))
    if not placements:
        return None
    motion = words(0x314E4750, len(placements)) + b''.join(placements)
    art = words(0x31414750, 6)
    for descriptor in (0xF0C08, 0xF0C28, 0xF0C48, 0xF0C68, 0xF0C88, 0xF0CA8):
        for lod in range(4):
            image = cell_image_indexed(rom.data, rom.pointer(descriptor + lod * 8), tiles, palette)
            art += words(rom.u32(descriptor + lod * 8 + 4), len(image)) + image
    return motion, art

def boos(rom, objects, object_code, tiles, palette):
    placements = []
    for order, (kind, x, y, checkpoint) in enumerate(objects):
        code = object_code(kind)
        if code >> 16 == 0x8D:
            variant = code & 65535
            if variant > 1:
                raise ValueError('unknown boo variant')
            placements.append(struct.pack('<iiHBB', (x * 8 + 4) << 16,
                (y * 8 + 4) << 16, variant, checkpoint, order))
    if not placements:
        return None
    motion = words(0x314F4F42, len(placements)) + b''.join(placements)
    art = words(0x31414F42, 2)
    for descriptor in (0xF07A8, 0xF07C8):
        for lod in range(4):
            image = cell_image_indexed(rom.data, rom.pointer(descriptor + lod * 8), tiles, palette)
            art += words(rom.u32(descriptor + lod * 8 + 4), len(image)) + image
    return motion, art

def cannonballs(rom, objects, object_code, tiles, palette):
    placements = []
    for order, (kind, x, y, checkpoint) in enumerate(objects):
        code = object_code(kind)
        if code >> 16 == 0x84:
            variant = code & 65535
            if variant > 3:
                raise ValueError('unknown cannonball variant')
            placements.append(struct.pack('<iiHBB', (x * 8 + 4) << 16,
                (y * 8 + 4) << 16, variant, checkpoint, order))
    if not placements:
        return None
    motion = words(0x314C4243, len(placements)) + b''.join(placements)
    art = words(0x31414243, 7)
    for descriptor in (0xF0048, 0xF0068, 0xF0088, 0xF00A8, 0xF00C8, 0xF00E8, 0xF0108):
        for lod in range(4):
            image = cell_image_indexed(rom.data, rom.pointer(descriptor + lod * 8), tiles, palette)
            art += words(rom.u32(descriptor + lod * 8 + 4), len(image)) + image
    return motion, art


def words(*values):
    return struct.pack(f'<{len(values)}I', *values)


class Data:
    def __init__(self, data):
        self.data = data

    def span(self, offset, size):
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise ValueError(f'course data range outside source: {offset:#x}+{size:#x}')
        return self.data[offset:offset + size]

    def u16(self, offset):
        return int.from_bytes(self.span(offset, 2), 'little')

    def u32(self, offset):
        return int.from_bytes(self.span(offset, 4), 'little')

    def pointer(self, offset):
        address = self.u32(offset) - 0x08000000
        self.span(address, 1)
        return address

    def lz(self, offset, size):
        self.span(offset, 4)
        result = decode_lz77(self.data, offset)
        if len(result) != size:
            raise ValueError(f'course decompression size at {offset:#x}: {len(result)} != {size}')
        return result


def stream(course, field):
    offset = course.u32(field)
    if not offset:
        return []
    records = []
    while True:
        record = course.span(offset, 4)
        if record[0] == 0:
            return records
        records.append(record)
        offset += 4


def raster(course):
    """sub_8032CA0 checkpoint rectangles/triangles, in original write order."""
    base = course.u32(0xCC)
    count = course.span(base, 1)[0]
    offset = base + course.u16(base + 1)
    width = course.span(4, 1)[0] * 64
    cells = bytearray([127]) * (width * width)
    for index in range(count):
        entry = Data(course.span(offset + index * 12, 12))
        kind = entry.data[0]
        x, y, extent = entry.u16(1), entry.u16(3), entry.u16(5) + 1
        if kind > 4:
            raise ValueError(f'unsupported checkpoint shape {kind}')
        height = entry.u16(7) + 1 if kind == 0 else extent
        value = (index - 128 if index in (0, count - 1) else index) & 255
        for row in range(height):
            cy = y - row if kind >= 3 else y + row
            for column in range(extent if kind == 0 else extent - row):
                cx = x - column if kind in (2, 3) else x + column
                if 0 <= cx < width and 0 <= cy < width:
                    cells[cy * width + cx] = value
    return width, count, bytes(cells)


def terrain(course):
    width, height = (v * 128 for v in course.span(4, 2))
    if not width or not height:
        raise ValueError('empty course terrain dimensions')
    offset = course.u32(0xD4)
    if course.data[3] & 2:
        data = bytearray(0x10000)
        for block in range(16):
            relative = course.u16(offset + block * 2)
            if not relative:
                continue
            chunk = decode_lz77(course.data, offset + relative)
            if len(chunk) > 0x1000:
                raise ValueError('course terrain block exceeds destination')
            data[block * 0x1000:block * 0x1000 + len(chunk)] = chunk
    else:
        data = bytearray(decode_lz77(course.data, offset))
    if len(data) < width * height:
        raise ValueError('course terrain is truncated')
    return width, height, data[:width * height]


def padded_lz(source, offset, size):
    """Decode into a cleared fixed-capacity destination, rejecting overflow."""
    source.span(offset, 4)
    data = decode_lz77(source.data, offset)
    if len(data) > size:
        raise ValueError(f'course decompression exceeds destination: {len(data)} > {size}')
    return data + bytes(size - len(data))


def split_lz(source, offset, blocks, stride):
    data = bytearray(blocks * stride)
    for block in range(blocks):
        relative = source.u16(offset + block * 2)
        if relative:
            data[block * stride:(block + 1) * stride] = padded_lz(
                source, offset + relative, stride)
    return data


def source_course(rom, header, container, offsets, reference_field):
    """Signed header-table aliases used by loadtrack_loadTileset/sub_803159C."""
    def load(index):
        definition = rom.pointer(0xE7FEC + index * 4)
        course_id = rom.u32(definition)
        if not 0 <= course_id < len(offsets) - 1:
            raise ValueError('referenced course outside directory')
        return Data(container.span(offsets[course_id],
                                   offsets[course_id + 1] - offsets[course_id]))
    course = load(header)
    relative = int.from_bytes(course.span(reference_field, 1), 'little', signed=True)
    if relative:
        if not 0 <= header + relative < 52:
            raise ValueError('course reference outside header table')
        course = load(header + relative)
    return course


def road_tiles(course):
    offset = course.u32(0x80)
    if not course.data[1]:
        return bytearray(course.span(offset, 0x4000))
    if course.data[3] & 1:
        return split_lz(course, offset, 4, 0x1000)
    return bytearray(padded_lz(course, offset, 0x4000))


def sky_shapes(size):
    # track_loadSkySize: one layer may extend horizontally or vertically.
    shapes = [(32, 32)] * 3
    extended = {0x20: (0, 64, 32), 4: (0, 32, 64),
                0x40: (1, 64, 32), 8: (1, 32, 64),
                0x80: (2, 64, 32), 0x10: (2, 32, 64)}
    if size in extended:
        layer, width, height = extended[size]
        shapes[layer] = (width, height)
    elif size not in (1, 2):
        raise ValueError(f'unsupported sky shape {size}')
    return shapes


def sky_motion_payload(behavior, material=0):
    """316C8 callback profiles normalized to scroll rules; no runtime source IDs."""
    profiles = {
        1: (0, (6, 7, 8), (-40, 0, 0), (38, 38, 38)),
        2: (1, (7, 7, 8), (0, -96, 0), (38, 32, 38)),
        3: (0, (6, 7, 8), (0, 0, 0), (38, 38, 38)),
        4: (0, (6, 7, 8), (-40, 0, 0), (70, 70, 70)),
        6: (4, (6, 7, 8), (-40, 0, 0), (38, 38, 182)),
        7: (0, (6, 7, 8), (0, 0, 0), (38, 38, 38)),
        8: (2, (0, 8, 7), (0, 0, 0), (38, -10, 38)),
        10: (0, (6, 7, 8), (-40, 0, 0), (38, 38, 38)),
        11: (0, (6, 7, 8), (0, 0, 0), (38, 38, 38)),
        13: (0, (6, 7, 8), (0, -40, 0), (38, 38, 38)),
        14: (0, (6, 7, 8), (0, 0, 0), (38, 38, 38)),
        15: (0, (6, 7, 8), (0, 0, 0), (38, 38, 38)),
        16: (6, (6, 6, 7), (0, 0, -24), (38, 38, 38)),
        17: (0, (6, 7, 8), (-40, 0, 0), (38, 38, 38)),
        18: (0, (6, 7, 7), (-40, 0, 0), (38, 38, 38)),
        19: (3, (8, 7, 6), (0, 0, 0), (352, 6, 6)),
        20: (7, (8, 7, 6), (0, 0, 0), (38, 38, 38)),
        21: (0, (6, 7, 8), (0, 0, 0), (38, 38, 38)),
        22: (0, (6, 7, 8), (0, 0, 0), (38, 38, 38)),
        23: (5, (6, 7, 8), (-40, 0, 0), (38, 38, 38)),
        28: (0, (6, 7, 8), (-40, 0, 0), (70, 70, 70)),
        29: (0, (6, 7, 8), (-40, 0, 0), (38, 38, 38)),
        37: (0, (6, 7, 8), (-40, 0, 0), (38, 38, 182)),
        43: (0, (6, 7, 8), (-40, 0, 0), (38, 38, 182)),
        51: (0, (8, 7, 6), (0, 0, 0), (32, -88, 6)),
    }
    if behavior not in profiles:
        raise ValueError(f'unsupported sky motion behavior {behavior}')
    controller, shifts, x_bias, y_bias = profiles[behavior]
    split = (24, 2, 8, 0) if behavior == 8 else (-1, 0, 0, 0)
    hide = 0 if behavior in (19, 20, 51) else 2
    sources = (2, 1, 0) if hide == 0 else (0, 1, 2)
    road_mode = (3 if material == 14 else 1) if behavior in (8, 10, 17, 19, 20, 21, 28, 51) else 0
    return (words(0x31524D53, 1) +
            struct.pack('<5B3B3h3hhBBhBB3BB', controller, 7, 0, 0, 0,
                        *shifts, *x_bias, *y_bias, *split, hide, 0, *sources, road_mode))


def sky(rom, background, behavior=None, material=0):
    container = Data(rom.span(0x234000, 0x24000))
    base = container.u32(background * 4)
    size, flags = container.span(base, 2)
    shapes = sky_shapes(size)
    if flags & 3 != 3:
        raise ValueError(f'unsupported sky compression {flags}')
    gfx, front, mid, back, palette = (base + container.u32(base + field)
                                     for field in (4, 8, 12, 16, 20))
    tiles = split_lz(container, gfx, 5, 0x800)
    maps = [padded_lz(container, offset, width * height * 2)
            for offset, (width, height) in zip((front, mid, back), shapes)]
    motion = sky_motion_payload(behavior, material) if behavior is not None else b''
    return (words(0x31594B53, len(tiles), 63, *(len(m) for m in maps),
                  *(width for width, _ in shapes), 32, size, flags,
                  container.u16(base + 2), 96, background, len(motion)) + tiles +
            container.span(palette, 126) + b''.join(maps) + motion)


def recovery_fx_payload(r):
    """D950 freeze palette spans and C4D8 drip displacements."""
    durations = []
    for row in range(3):
        frame, duration = r.span(0xF2C48 + row*2, 2)
        if frame != 1 or not 1 <= duration <= 127:
            raise ValueError('invalid recovery freeze sequence')
        durations.append(duration)
    if r.span(0xF2C4E, 2) != bytes(2):
        raise ValueError('invalid recovery freeze terminator')
    dy = struct.unpack('<9b', r.span(0xF31A0, 9))
    if any(value < -16 or value > 16 for value in dy):
        raise ValueError('invalid recovery drip displacement')
    return struct.pack('<II3B9bI', 0x31584652, 3, *durations, *dy, 0)


IMPACT_SEQUENCES = (0xF2CBC, 0xF2CEC, 0xF2D1C, 0xF2D4C, 0xF2D7C, 0xF2DAC)


def impact_effect_payload(r):
    """378FC/B554/B5B8/B61C: paired forward/reverse scripts in source order."""
    ticks = []
    for address in IMPACT_SEQUENCES:
        art = race_particle_payload(r, address)
        count, = struct.unpack_from('<I', art, 12)
        ticks.append(sum(struct.unpack_from('<H', art, 32+index*8+6)[0]+1 for index in range(count)))
    return struct.pack('<II6H', 0x31584649, 6, *ticks)


def coin_effect_payload(r):
    """B680/B7D0 motion, bounce volumes and B44C source script lifetimes."""
    horizontal = struct.unpack('<9i', r.span(0xF2F2C, 36))
    upward = struct.unpack('<9i', r.span(0xF2F50, 36))
    volumes = struct.unpack('<3H', r.span(0xF2F74, 6))
    if any(not -64 <= value <= 64 for value in horizontal) or any(not 0 <= value <= 64*65536 for value in upward):
        raise ValueError('invalid coin effect motion')
    if any(value > 256 for value in volumes):
        raise ValueError('invalid coin effect volume')
    scripts = (r.pointer(0xF2F04), r.pointer(0xF2F08), 0xF2EB4, 0xF2EE4)
    if any(r.pointer(0xF2F04+slot*4) != scripts[1] for slot in range(1, 9)):
        raise ValueError('invalid coin effect variant scripts')
    ticks = []
    for address in scripts:
        payload = race_particle_payload(r, address)
        count, = struct.unpack_from('<I', payload, 12)
        ticks.append(sum(struct.unpack_from('<H', payload, 32+index*8+6)[0]+1 for index in range(count)))
    return struct.pack('<4I18i7H2x', 0x31584643, 9, 3, 4, *horizontal, *upward, *volumes, *ticks)


def surface_trail_payload(r):
    """E68C/E1F4/DF10 material selection and frame cuts, normalized Thumb constants."""
    kinds = bytearray([1] * 256)
    for material, kind in ((0x54, 2), (0x5A, 3), (0x5C, 4), (0x58, 5)):
        kinds[material] = kind
    return (words(0x314C5254, 256) + kinds + bytes([9, 19] * 3 + [5, 11] * 2) +
            bytes([0x50, 0x50, 0x5C, 0, 0, 0]))


def surface_trail_art_payload(r, group):
    """DF10 source age-ordered cells; 49D78 spray occupies the fifth group."""
    groups = ((0x7AFCC, 0x7AFD4, 0x7AFDC), (0x7AFFC, 0x7B004, 0x7B00C),
              (0x7B014, 0x7B01C, 0x7B024), (0x7B044, 0x7B04C, 0x7B054), (0x7AF14,))
    if not 0 <= group < len(groups):
        raise ValueError('invalid surface trail art group')
    cells = bytearray()
    frames = []
    for address in groups[group]:
        if r.u16(address) != 1:
            raise ValueError('invalid surface trail cell count')
        frames.append(struct.pack('<IHH', len(cells), 8, 1))
        cells.extend(r.span(address, 8))
    return (words(0x31435846, 0x2000, 80, len(frames), 0x100, 3, len(cells), 0) +
            b''.join(frames) + r.span(0x66F0C, 0x2000) + r.span(0x66E4C, 160) + cells)


def race_particle_payload(r, sequence):
    """Flatten 5B44C source cells/duration+1 frames; no host or ROM pointers."""
    frames, cells = [], bytearray()
    for index in range(33):
        address, duration = struct.unpack('<II', r.span(sequence + index * 8, 8))
        if address == 0 and duration == 0:
            break
        if index == 32 or not 0 < duration <= 255:
            raise ValueError('invalid race particle sequence')
        offset = r.pointer(sequence + index * 8)
        size = 2 + r.u16(offset) * 6
        if len(cells) + size > 528:
            raise ValueError('race particle cells exceed capacity')
        frames.append(struct.pack('<IHH', len(cells), size, duration))
        cells.extend(r.span(offset, size))
    if not frames:
        raise ValueError('empty race particle sequence')
    return (words(0x31435846, 0x2000, 80, len(frames), 0x100, 3, len(cells), 0) +
            b''.join(frames) + r.span(0x66F0C, 0x2000) + r.span(0x66E4C, 160) + cells)


def battle_balloon_payload(r):
    """BA58/BAD0's single cell, normalized to native pixels/anchor."""
    # EC30 loads this Battle-only block at source tile8C. The balloon
    # uses tile9C; those low pages are absent from ordinary race effects.
    tiles=decode_lz77(r.data,0x6D06C)
    if len(tiles)!=640:
        raise ValueError('invalid Battle balloon tile block')
    return cell_image_indexed(r.data, 0x7B534,
                      bytes(0x1180) + tiles,
                      bytes(96) + r.span(0x66E4C, 160))


def weather_payload(r, behavior, tiles, palette):
    """36D1C/365B0: normalize the weather owner and course-bank cells."""
    enabled = int(behavior == 16)
    choices = []
    for table in (0xEB37C, 0xEB38C):
        row = [r.u16(table + index * 2) for index in range(8)] if enabled else [0] * 8
        if any(value > 3 for value in row):
            raise ValueError('weather spawn choice outside stages0..3')
        choices.extend(row)
    images = [cell_image_indexed(r.data, cell, tiles, palette)
              for cell in (0x7C2C8, 0x7C2C0, 0x7C2B8)] if enabled else []
    sizes = [len(image) for image in images] if enabled else [0] * 3
    return (struct.pack('<IIBB2x16B3I2I', 0x31485457, 1, enabled, 12 if enabled else 0,
                        *choices, *sizes, 0, 0) + b''.join(images))


def finish_payload(r):
    """BC18/BC9E/BCD0/BD3A/BE08 camera rules and A90C donor schedules."""
    # Camera constants are normalized from the matched Thumb instructions.
    result = struct.pack('<II4HB3x', 0x314E4946, 8, 1024, 32768, 80, 220, 3)
    for driver in range(8):
        address = r.pointer(0xF2BF4 + driver*4)
        source, mapping, rows = [], {}, []
        for index in range(33):
            frame, duration, reserved0, reserved1 = r.span(address+index*4, 4)
            if reserved0 or reserved1:
                raise ValueError('invalid finish schedule padding')
            if frame == 0 and duration == 0:
                break
            if index == 32:
                raise ValueError('finish schedule exceeds row budget')
            if frame != 255:
                if frame > 33 or not 1 <= duration <= 127:
                    raise ValueError('invalid finish schedule row')
                mapping[index] = len(rows)
                rows.append((frame, duration, index))
            source.append((frame, duration))
        if not rows:
            raise ValueError('empty finish schedule')
        normalized = []
        for frame, duration, index in rows:
            cursor, seen = index+1, set()
            while cursor < len(source):
                if cursor < 0 or cursor in seen:
                    raise ValueError('invalid finish schedule jump')
                seen.add(cursor)
                if source[cursor][0] != 255:
                    break
                delta = source[cursor][1]
                cursor += delta if delta < 128 else delta-256
            if cursor < 0 or cursor > len(source):
                raise ValueError('invalid finish schedule jump')
            next_row = 255 if cursor == len(source) else mapping[cursor]
            normalized.append(struct.pack('<4B', frame, duration, next_row, 0))
        result += words(len(rows)) + b''.join(normalized)
    return result


def driver_view_payload(r):
    """CA78 response/speed limits normalized independently of driver graphics."""
    limits = r.span(0xEC846, 30)
    if any(value > 18 for value in limits):
        raise ValueError('invalid driver view limit')
    return words(0x31525644, 15) + limits


def driver_art_payload(r):
    """CBE4 speed bob spans and CD50/CC80 surface refresh policy."""
    spans = struct.unpack('<16H', r.span(0xEC826, 32))
    source_masks = struct.unpack('<16I', r.span(0xEC2C0, 64))
    if any(span > 2047 for span in spans):
        raise ValueError('invalid driver art speed span')
    if any(mask not in (0, 0xFFFFFFFF) for mask in source_masks):
        raise ValueError('invalid driver art refresh mask')
    return (words(0x31524144, 16) + struct.pack('<16H', *spans) +
            bytes(7 if mask == 0 else 3 for mask in source_masks))


def driver_projection_payload(r):
    """CD50 LOD thresholds and 4378C/4379C sprite-cell geometry."""
    result = words(0x31525044, 3)  # DPR1
    for index, distance in enumerate((511, 1023, 4096)):
        attr = struct.unpack('<H', r.span(0xECDE0 + index * 2, 2))[0]
        size = 8 << (attr >> 14)
        x = attr & 511
        if x >= 256:
            x -= 512
        y = struct.unpack('<b', r.span(0xECDDC + index, 1))[0]
        if size != 64 >> index or attr & 0x3E00:
            raise ValueError('invalid driver projection cell')
        # CD50's affine body cells use double-size bounds; pixel axes stay size.
        result += struct.pack('<HHhhBBH', distance, size, x, y, 1, 0, 0)
    return result


def driver_affine_payload(r):
    """5A824/B07C/B164/B1FC/B294 rows normalized to inverse affine coefficients."""
    tables = ((1, 0xF2AC0, 1, 1), (4, 0xF2C54, 1, 1),
              (5, 0xF2C80, 0, 7), (6, 0xF2C86, 0, 7), (7, 0xF2C8C, 1, 1))
    result = words(0x31524641, len(tables))  # AFR1
    for mode, address, x_shift, y_shift in tables:
        rows = []
        for index in range(33):
            x, y, duration = r.span(address + index * 3, 3)
            if duration == 0:
                break
            if index == 32 or x == 0 or y == 0 or duration > 127:
                raise ValueError('invalid driver affine table')
            rows.append(struct.pack('<HHBB', x << x_shift, y << y_shift, duration, 0))
        if not rows:
            raise ValueError('empty driver affine table')
        result += words(mode, len(rows)) + b''.join(rows)
    return result


def hud_graphics_payload(r):
    """HUD2 maps, graphics and logical-page palette selectors."""
    hud = words(0x32445548, 0x1000, 96, 20, 60, 6, 2, 6, 14 * 0x80, 16, 17)
    for offset, size in ((0x62834, 0x1000), (0x62774, 192), (0xEE67E, 40),
                         (0xEE6A6, 60), (0xEE7AC, 12), (0xEEF88, 4),
                         (0xEE7BC, 12), (0x6F33C, 14 * 0x80), (0xEE7CC, 32)):
        hud += r.span(offset, size)
    hud += r.span(0xEE65C,34)
    return hud


def shared_sections(rom):
    """ROM art and collision tables shared by every imported course."""
    r = Data(rom)
    collision = words(0x314C4F43, 8, 8, 4, 4, 4, 4, 8)
    for offset, size in ((0xEDA74, 32), (0xEDA94, 32), (0xEC770, 8),
                         (0xEC778, 8), (0xEC780, 8), (0xEC788, 8), (0xEC248, 32)):
        collision += r.span(offset, size)
    hud = hud_graphics_payload(r)
    minimap = words(0x314E494D, 30, 6, 5, 0x240, 0) + r.span(0xEE76C, 60) + r.span(0x66C0C, 0x240)
    frames = ((0x7AE50, 8, 2), (0x7AEEC, 8, 3), (0x7AE58, 8, 4),
              (0x7AE60, 62, 4), (0x7AEA0, 74, 2))
    effect = words(0x31435846, 0x2000, 80, 5, 0x100, 3, 304, 0)
    cursor = 0
    for _, size, ticks in frames:
        effect += struct.pack('<IHH', cursor, size, ticks)
        cursor += size
    effect += r.span(0x66F0C, 0x2000) + r.span(0x66E4C, 160)
    effect += b''.join(r.span(offset, size) for offset, size, _ in frames)
    effect += r.span(0x7ABB4, 144)  # six cells each: green/red/spiny shells
    box = words(0x31584249, 8 * 0x200, 16, 8, 32, 32)
    box += b''.join(r.span(r.pointer(0xED9AC + index * 4), 0x200) for index in range(8))
    box += r.span(0x6D71C, 32)
    markers = words(0x314B4D47, 64, 32, 0x118) + r.span(0x6720C, 64)
    markers += r.span(0x66E8C, 32) + r.span(0x66ECC, 32)
    return [(12, 1, hud, 2), (14, 1, collision, 1), (19, 1, effect, 2),
            (19, 2, race_particle_payload(r, 0xF2F7C), 1),
            (19, 3, race_particle_payload(r, r.pointer(0xF2F04)), 1),
            (19, 4, battle_balloon_payload(r), 1),
            (19, 5, race_particle_payload(r, 0xF312C), 1),  # C19C lava smoke puff
            (19, 6, race_particle_payload(r, 0xF3014), 1),  # C09C water/lava entry splash
            (19, 7, race_particle_payload(r, 0xF3158), 1),  # C388 thaw
            (19, 8, race_particle_payload(r, 0xF3180), 1),  # C448 water drip
            (88, 1, recovery_fx_payload(r), 1),
            *[(19, 9+group, surface_trail_art_payload(r, group), 1) for group in range(5)],
            (89, 1, surface_trail_payload(r), 1),
            (19, 14, race_particle_payload(r, r.pointer(0xF2F08)), 1),
            (19, 15, race_particle_payload(r, 0xF2EB4), 1),
            (19, 16, race_particle_payload(r, 0xF2EE4), 1),
            (90, 1, coin_effect_payload(r), 1),
            *[(19, 17+index, race_particle_payload(r, address), 1) for index, address in enumerate(IMPACT_SEQUENCES)],
            (91, 1, impact_effect_payload(r), 1),
            (26, 1, box, 1), (29, 1, markers, 2),
            (81, 1, driver_affine_payload(r), 1),
            (82, 1, driver_projection_payload(r), 1),
            (83, 1, driver_art_payload(r), 1),
            (85, 1, driver_view_payload(r), 1),
            (86, 1, finish_payload(r), 1)], minimap


def mushroom_voices(r):
    """A9AC indexes ECC48 by driver, then normal/small flag58 bit10."""
    data = r.span(0xECC48, 32)
    if any(value == 0 for value in struct.unpack('<16H', data)):
        raise ValueError('invalid mushroom voice song')
    return data


def shell_orbit_rules_section(r):
    """46DE4/46F5C shared triple-shell orbit, in original integer units."""
    if r.u16(0x46dfe)&0xff00!=0x2100 or r.u16(0x46f66)&0xff00!=0x2300 or r.u16(0x46e5e)&0xff00!=0x2000:
        raise ValueError('shell orbit: expected MOVS immediates')
    radius=r.u16(0x46dfe)&255
    if radius!=r.u16(0x46f66)&255:raise ValueError('shell orbit: inconsistent radius')
    if (r.u16(0x46dfa),r.u16(0x46dfc),r.u16(0x46e02),r.u16(0x46e04))!=(0x0088,0x1840,0x0600,0x1400):
        raise ValueError('shell orbit: unexpected angle expression')
    step=((1<<(r.u16(0x46dfa)>>6&31))+1)<<8
    period=(r.u16(0x46e5e)&255)+1
    if not 1<=radius<=255 or not 1<=step<=65535 or not 1<=period<=65535:
        raise ValueError('shell orbit: out of range')
    return (67,1,words(0x3142524f,3,radius,step,period),1)


def shell_orbit_delays(r):
    """473FC/47560 narrow each original delay word to the actor's byte74."""
    return bytes(r.u32(0xEDAFC + i * 4) & 255 for i in range(6))


def cpu_red_decisions(r):
    """4279C's four-way held-shell decisions for a human ahead/behind."""
    data = r.span(0xECCF7, 8)
    if any(value > 3 for value in data):
        raise ValueError('invalid CPU red shell release decision')
    return data


def cpu_green_decisions(r):
    """421F4's hold/throw/drop decision for a human ahead/behind."""
    data = r.span(0xECCE7, 8)
    if any(value > 3 for value in data):
        raise ValueError('invalid CPU green shell release decision')
    return data


def cpu_banana_decisions(r):
    """42454's four-way hold/throw/drop decision, for a human ahead/behind."""
    data = r.span(0xECCEF, 8)
    if any(value > 3 for value in data):
        raise ValueError('invalid CPU banana release decision')
    return data


def banana_rules_section(r):
    """7C2C/7AAC/7F1C/791C/7848: player/CPU throws and disposal."""
    def immediate(offset,opcode,register=0):
        instruction=r.u16(offset)
        if instruction&0xff00!=opcode|register<<8:raise ValueError('banana rules: unexpected Thumb immediate')
        return instruction&255
    def shifted(offset,register):
        value=immediate(offset,0x2000,register);instruction=r.u16(offset+2)
        if instruction&0xf83f!=register<<3|register:raise ValueError('banana rules: expected LSLS immediate')
        return value<<(instruction>>6&31)
    if r.u16(0x47c94)!=0x0100 or r.u16(0x47caa)!=0x0100:
        raise ValueError('banana rules: unequal player throw shifts')
    speed=1<<(15+(r.u16(0x47c94)>>6&31))
    if r.u16(0x47d1e)!=0x1140 or r.u16(0x47d2e)!=0x1140:
        raise ValueError('banana rules: unequal CPU flight divisors')
    cpu_ticks=1<<(r.u16(0x47d1e)>>6&31)
    velocity=shifted(0x47cae,0)
    if velocity!=shifted(0x47d32,0):raise ValueError('banana rules: unequal throw arcs')
    gravity=(0x100000000-r.u32(0x47bb0))&0xffffffff
    grace=immediate(0x47d44,0x2000);release=immediate(0x47c08,0x2800)
    hold=immediate(0x48014,0x2800)+1;destroy_velocity=shifted(0x4795e,1)
    destroy_gravity=(0x100000000-r.u32(0x47880))&0xffffffff
    values=(speed,cpu_ticks,velocity,gravity,grace,release,hold,destroy_velocity,destroy_gravity)
    return (65,1,words(0x314e4142,9,*values),1)


def red_shell_rules_section(r):
    """4676C/4D4C/5C18/5A3C/5910: shared red/Spiny lifecycle."""
    def immediate(offset,opcode,register=0):
        instruction=r.u16(offset)
        if instruction&0xff00!=opcode|register<<8:raise ValueError('red/Spiny shell rules: unexpected Thumb immediate')
        return instruction&255
    def shifted(offset,register):
        value=immediate(offset,0x2000,register);instruction=r.u16(offset+2)
        if instruction&0xf83f!=register<<3|register:raise ValueError('red/Spiny shell rules: expected LSLS immediate')
        return value<<(instruction>>6&31)
    bonus=shifted(0x46aa6,2);cap=immediate(0x46ac0,0x2000)
    if immediate(0x46abc,0x2800)+1!=cap:raise ValueError('red shell rules: speed cap mismatch')
    grace=immediate(0x46788,0x2000,4)
    route=immediate(0x46802,0x2000)
    release=immediate(0x45344,0x2800);hold=immediate(0x4714e,0x2800)+1
    speeds=tuple(immediate(offset,0x2000,2) for offset in (0x44ef0,0x44ec6,0x44ef4))
    acceleration=immediate(0x44f02,0x3000);deceleration=immediate(0x44efa,0x3800)
    arm=immediate(0x45dd4,0x2800);lock=immediate(0x459a0,0x2800)
    lifetime=(r.u32(0x45b00)>>16)+1
    if r.u32(0x4649c)+1!=lifetime:raise ValueError('red shell rules: trap deadlines differ')
    if immediate(0x45dc2,0x2800)!=release:raise ValueError('red shell rules: release clocks differ')
    velocity=shifted(0x47890,2);gravity=(0x100000000-r.u32(0x47880))&0xffffffff
    values=(bonus,cap,grace,route,release,hold,*speeds,acceleration,deceleration,arm,lifetime,lock,velocity,gravity)
    limits=(32767,255,65535,255,32767,32767,255,255,255,255,255,32767,32767,32767,0xffffff,0xffffff)
    if any(not (0 if index in (0,2) else 1)<=value<=limit for index,(value,limit) in enumerate(zip(values,limits))):
        raise ValueError('red shell rules: source values out of range')
    return (64,1,words(0x31485352,16,*values),1)


def green_shell_rules_section(r):
    """4676C/6FF4/4BA0/7884/7848: green release and complete actor lifecycle."""
    def immediate(offset,opcode,register=0):
        instruction=r.u16(offset)
        if instruction&0xff00!=opcode|register<<8:raise ValueError('green shell rules: unexpected Thumb immediate')
        return instruction&255
    def shifted(offset,register):
        value=immediate(offset,0x2000,register);instruction=r.u16(offset+2)
        if instruction&0xf83f!=register<<3|register:raise ValueError('green shell rules: expected LSLS immediate')
        return value<<(instruction>>6&31)
    bonus=shifted(0x46876,2);cap=immediate(0x46890,0x2000)
    if immediate(0x4688c,0x2800)+1!=cap:raise ValueError('green shell rules: speed cap mismatch')
    grace=immediate(0x46788,0x2000,4);walls=immediate(0x468e4,0x2000)
    release=immediate(0x44cda,0x2800);hold=immediate(0x4714e,0x2800)+1
    velocity=shifted(0x47890,2);gravity=(0x100000000-r.u32(0x47880))&0xffffffff
    values=(bonus,cap,grace,walls,release,hold,velocity,gravity)
    limits=(32767,255,65535,7,32767,32767,0xffffff,0xffffff)
    if any(not (0 if index in (0,2) else 1)<=value<=limit for index,(value,limit) in enumerate(zip(values,limits))):
        raise ValueError('green shell rules: source values out of range')
    return (63,1,words(0x31485347,8,*values),1)


def boo_rules_section(r):
    """48694/4886C: immunity, target lock, orbit lifecycle and repulsion."""
    def mov(offset,register):
        instruction=r.u16(offset)
        if instruction&0xff00!=0x2000|register<<8:
            raise ValueError('Boo rules: expected MOVS immediate')
        return instruction&255
    def shifted(offset,register):
        value=mov(offset,register);instruction=r.u16(offset+2)
        if instruction&0xf83f!=register<<3|register:
            raise ValueError('Boo rules: expected LSLS immediate')
        return value<<(instruction>>6&31)
    invisible,lock=shifted(0x486b8,0),shifted(0x48974,0)
    orbit=mov(0x488e6,0)
    if r.u16(0x48b4e)!=0x2800|orbit-1:
        raise ValueError('Boo rules: unequal approach and departure')
    step=shifted(0x4893c,2)
    if step!=shifted(0x489ec,3) or step!=shifted(0x48ad6,5):
        raise ValueError('Boo rules: unequal orbit steps')
    bob=shifted(0x48a56,3);velocity=shifted(0x48bc0,0)
    gravity=(0x100000000-r.u32(0x48c00))&0xffffffff
    values=(invisible,lock,orbit,step,bob,velocity,gravity)
    limits=(32767,32767,255,32767,32767,0xffffff,0xffffff)
    if any(not 1<=value<=limit for value,limit in zip(values,limits)):
        raise ValueError('Boo rules: invalid source values')
    return (62,1,words(0x314f4f42,7,*values),1)


def item_status_rules_section(r):
    """485DC/483A0: Star local/CPU protection and shared Lightning timers."""
    def mov(offset,register):
        instruction=r.u16(offset)
        if instruction&0xff00!=0x2000|register<<8:
            raise ValueError('item status rules: expected Thumb MOVS immediate')
        return instruction&255
    def shifted(offset):
        value=mov(offset,0)
        if r.u16(offset+2)!=0x0040:
            raise ValueError('item status rules: expected LSLS r0,r0,1')
        return value*2
    player,cpu=shifted(0x48612),mov(0x4860c,1)
    lightning=shifted(0x483e4)
    if lightning!=shifted(0x4847c) or not 1<=player<=32767 or not 1<=cpu<=32767 or not 48<=lightning<=32767:
        raise ValueError('item status rules: invalid or unequal source duration')
    return (61,1,words(0x31544153,3,player,cpu,lightning),1)


def mushroom_rules_section(r):
    """A9AC/48138/482B8/8298: response duration, actor turns and mode8 drive."""
    def mov(offset, register):
        instruction = r.u16(offset)
        if instruction & 0xff00 != 0x2000 | register << 8:
            raise ValueError('mushroom rules: expected Thumb MOVS immediate')
        return instruction & 255
    def shifted(offset, register):
        value = mov(offset, register)
        instruction = r.u16(offset + 2)
        if instruction & 0xf83f != register << 3 | register:
            raise ValueError('mushroom rules: expected Thumb LSLS immediate')
        return value << (instruction >> 6 & 31)
    boost = mov(0x3AABE, 0)
    single = r.u16(0x4814E)
    if single & 0xff00 != 0x2800:
        raise ValueError('mushroom rules: expected actor counter comparison')
    single = (single & 255) + 1
    golden = shifted(0x482CA, 1)
    acceleration = shifted(0x38510, 0)
    speed = r.u32(0x3851C) + 1
    if not 1 <= boost <= 65535 or not 1 <= golden <= 65535 or acceleration & 65535 or not 1 <= acceleration >> 16 <= 32767 or not 1 <= speed <= 0x07e00000:
        raise ValueError('mushroom rules: source values out of range')
    return (60, 1, words(0x3153554d, 5, boost, single, golden, acceleration >> 16, speed), 1)


def gp_scoring_section(r):
    """1E7C8 awards DB0A8[rank+1] into byte-sized cumulative scores."""
    awards = r.span(0xDB0A9, 8)
    if any(value > 63 for value in awards):
        raise ValueError('GP finish award exceeds four-race byte score budget')
    return (59, 1, words(0x31545047, 8) + awards + words(3) + r.span(0xECD00, 92), 3)


def gp_player_item_sections(r):
    """43BB0: leader A/B, then nine unsigned leader-progress gap tables."""
    offsets = (0xECE1C, 0xECE50, 0xECE84, 0xECEB0, 0xECEE4, 0xECF10,
               0xECF4C, 0xECF88, 0xECFC4, 0xECFF8, 0xED02C)
    result = []
    for identity, offset in enumerate(offsets, 1):
        rows, total = [], 0
        for index in range(16):
            item, weight = struct.unpack('<HH', r.span(offset + index * 4, 4))
            rows.append((item, weight))
            if item == 0:
                if weight != 255 or not 0 < total <= 32767:
                    raise ValueError('invalid GP player roulette terminator/total')
                rows += [(0, 0)] * (15 - index)
                break
            if not 0x1000 <= item <= 0x100C or weight == 0:
                raise ValueError('invalid GP player roulette item/weight')
            total += weight
        else:
            raise ValueError('unterminated GP player roulette table')
        result.append((58, identity, words(0x31575047, 16) +
                       b''.join(struct.pack('<HH', *row) for row in rows), 1))
    # ECDF0 is ten item/weight pairs; preview consumes only each item.
    preview = tuple(r.u16(0xECDF0 + i*4) for i in range(10))
    timeline = struct.unpack('<28i', r.span(0xEDA04, 112))
    if timeline[-1] != 0 or any(not 0 < n <= 32767 for n in timeline[:-1]):
        raise ValueError('invalid roulette timeline')
    # 43BB0 branch thresholds are normalized alongside its imported odds.
    payload = words(0x31544C52, 10) + struct.pack('<10H27H8H', *preview,
        *timeline[:-1], 2, 6, 12, 18, 24, 36, 50, 60)
    payload += r.span(0xEC8B4, 64)
    script = struct.unpack('<72I', r.span(0xEDB14, 288))
    visible, frame, slots = True, 0, []
    callbacks = {0x08048545: 0, 0x08048565: 1, 0x0804858D: 2, 0x080485B5: 3}
    for index in range(0, len(script), 2):
        command, value = script[index:index + 2]
        if command == 0: break
        if command == 0x11: visible = value != 0
        elif command == 0xF4:
            if value in callbacks: frame = callbacks[value]
        elif command == 0xF1:
            if value != 4: raise ValueError('invalid lightning frame duration')
            slots.append(frame if visible else 255)
        elif command != 0x43: raise ValueError('unknown lightning script command')
    if len(slots) != 14: raise ValueError('invalid lightning frame count')
    payload += bytes(slots)
    result.append((58, 12, payload, 1))
    return result


def battle_item_rules(r):
    """43BB0/44028: balloon-count odds;439FC:120-turn hidden box wait."""
    result = bytearray()
    for offset in (0xED080, 0xED06C, 0xED058):
        total = 0
        for index in range(10):
            item, weight = struct.unpack('<HH', r.span(offset + index * 4, 4))
            if item == 0:
                if weight != 255 or not 0 < total <= 32767:
                    raise ValueError('invalid Battle roulette terminator or total')
                result += struct.pack('<HH', item, weight)
                result += bytes((9 - index) * 4)
                break
            if not 0x1000 <= item <= 0x100C:
                raise ValueError('invalid Battle roulette item')
            total += weight
            result += struct.pack('<HH', item, weight)
        else:
            raise ValueError('unterminated Battle roulette table')
    # Thumb MOV r0,#0x78 in439FC's Battle branch, not a runtime constant.
    if r.u16(0x43A6A) != 0x2078:
        raise ValueError('unexpected Battle item box hidden timer instruction')
    return bytes(result) + words(r.u16(0x43A6A) & 255)


def time_trial_record_defaults(r, header, definition):
    """805D30C: five course totals and an independent best-lap target."""
    slots = [r.u32(table+i*4) for table in (0xe7464,0xe74c4) for i in range(20)]
    if header not in slots:
        return None
    source = r.pointer(definition+0x20)
    records = [r.span(source+index*4,4) for index in (1,2,3,4,5,0)]
    previous = 0
    for index, record in enumerate(records):
        driver, reserved, centiseconds = struct.unpack('<BBH',record)
        if driver > 7 or reserved or not 1 <= centiseconds <= 59999:
            raise ValueError('invalid Time Trial default record')
        if index < 5:
            if centiseconds < previous:
                raise ValueError('unsorted Time Trial default records')
            previous = centiseconds
    return words(0x31525454,6)+b''.join(records)


def gp_opening_payload(r, header, definition):
    """524CC: cup/class/course art without a course-specific timeline."""
    slots = [r.u32(table+i*4) for table in (0xE7464,0xE74C4) for i in range(20)]
    if header not in slots:
        return None  # Private stages share course decoding but have no GP entry.
    cup = (slots.index(header)%20)//4
    tiles = bytearray(0xA00)
    classes = r.span(0x9AA4C, 3*192)
    tiles[0x20:0xE0] = classes[:192]
    tiles[0xE0:0x3E0] = r.lz(r.pointer(definition+0x30),768)
    tiles[0x400:0x9C0] = r.lz(r.pointer(0xEFD58+cup*4),1472)
    tiles[0x9C0:0x9E0] = r.lz(0x76EA8,32)
    tilemap = bytearray(0x800)
    tilemap[:0xC0] = r.span(0x76EB4,0xC0)
    tilemap[18*64:20*64] = r.span(0x76F74,128)
    return words(0x31544F47,0xA00)+tiles+tilemap+r.span(r.pointer(0xEFD44+cup*4),32)+classes


def minimap_coordinate_shift(header, course):
    """32D0C selects map scale from source header aliases and directional starts."""
    return 20 if (header&32) or course.span(0xCA,1)[0] else 21


def gp_race_hud_animation_tail(r):
    """4DBF4 rank transitions and 4D6DC coin flash palette rows."""
    result = bytearray()
    for offset in (0xEEBD0, 0xEEC3C, 0xEECA8, 0xEED14):
        if (r.u16(offset), r.u16(offset+2)) != (4, 13):
            raise ValueError('GP rank transition dimensions changed')
        result.extend(r.span(offset+4, 104))
    result.extend(r.span(0x64C7C, 16))
    result.extend(r.span(0x64814, 32))
    for offset in (0x64C94, 0x64C9C, 0x64CA4):
        result.extend(r.span(offset, 6))
    result.extend(r.span(0x64C8C, 8))
    return bytes(result)


def gp_finish_hud_tail(r):
    """1D2A4/1D33C: full portrait frames and opponent pattern cells."""
    result=bytearray()
    for offset in (0xDB0B2,0xDB0D6,0xDB0FA,0xDB11E,0xDB142,0xDB166,0xDB18A,0xDB1AE):
        if (r.u16(offset),r.u16(offset+2))!=(4,4):
            raise ValueError('finish portrait frame dimensions changed')
        result.extend(r.span(offset,36))
    for index in range(8):
        offset=r.pointer(0xDB234+index*4)
        if (r.u16(offset),r.u16(offset+2))!=(2,2):
            raise ValueError('finish portrait pattern dimensions changed')
        result.extend(r.span(offset,12))
    return bytes(result)


def battle_elimination_rules(r):
    """B020/AFA4: one animation frame then the revenge-bomb role toggle."""
    frame, ticks, terminal_frame, terminal_ticks = r.span(0xF2C50, 4)
    if frame != 1 or not 1 <= ticks <= 127 or terminal_frame or terminal_ticks:
        raise ValueError('invalid Battle elimination animation')
    return words(ticks)


def cpu_item_profiles(r, header):
    """43DE8: resolve course/class branch tables into engine/variant/kart/rank data."""
    if not (4 <= header <= 28 or 32 <= header <= 51):
        raise ValueError('CPU item profile requires a recovered course header')
    # The source branches select three contiguous lap-variant tables. Normalize
    # those choices at import; native code never dispatches original addresses.
    # Headers outside4..23 take43FE8, using the generic three lap variants.
    groups = {0x08043F1C: 6, 0x08043FB4: 3,
              0x08043F10: 0, 0x08043FA8: 0, 0x08043FDC: 0, 0x08043FE8: 0}
    result = bytearray()
    for jump_table in (0x43E48, 0x43EC0, 0x43F58):
        branch = r.u32(jump_table + (header-4)*4) if header <= 23 else 0x08043FE8
        if branch not in groups:
            raise ValueError('unknown source CPU item branch')
        for variant in range(3):
            offset = 0xED0A8 + (groups[branch]+variant)*256
            values = [r.u32(offset+index*4) for index in range(64)]
            if any(value and not 0x1000 <= value <= 0x100C for value in values):
                raise ValueError('invalid source CPU item ID')
            result += struct.pack('<64H', *values)
    return bytes(result)


def route_sentinel_targets(container, course_offset, route_offset, count):
    """Resolve AD0/B10's checkpoint127 against the source ROM container.

    loadtrack_8031990 keeps courseData_bin + directory offset, not a copy
    bounded by the next directory entry. Row127 may therefore address bytes
    in the following course. Import just the resulting coordinates; native
    runtime code never follows original offsets or accesses adjacent assets.
    """
    mask, points = 0, b''
    for row in range(3):
        offset = course_offset + route_offset + (row * count + 127) * 8
        if 0 <= offset and offset + 4 <= len(container.data):
            mask |= 1 << row
            points += words(container.u16(offset) * 8,
                            container.u16(offset + 2) * 8)
        else:
            points += words(0, 0)
    return words(mask) + points


def surface_sound_rules(r, material):
    # C12C copies EDD40, then patches the course material theme.
    songs = list(struct.unpack('<16H', r.span(0xEDD40, 32)))
    patches = {12: {6: 80, 7: 71, 8: 82}, 6: {1: 77},
               10: {1: 71}, 0: {0: 76, 7: 76},
               18: {8: 83}, 16: {10: 84}}
    for index, song in patches.get(material, {}).items():
        songs[index] = song
    # v2 appends the trackDef+0x10 theme: F9E4 plays 182 only for theme 8.
    return (struct.pack('<4sHH16H', b'SRF1', 2, 16, *songs) +
            bytes(6 if song in (81, 137) else 4 if song in (82, 83) else 0
                  for song in songs) + bytes((material, 0, 0, 0)))


# Player roulette strength budget per source course ID 24..43 (the
# switch in the player item roulette); every other course uses 8.
ITEM_STRENGTH_THRESHOLDS = {30: 5, 36: 4, 41: 4, 43: 4,
                            32: 6, 35: 6, 37: 6, 38: 6, 40: 6, 42: 6,
                            24: 7, 25: 7, 28: 7, 29: 7, 39: 7}


def _course_actor_sections(
    r, header, c, container, offsets, objects, object_code, course_id
):
    # sub_803159C resolves graphics through EC, but keeps this course's palette.
    scenery_source = source_course(r, header, container, offsets, 0xEC)
    art_offset = scenery_source.u32(0xE4)
    if not art_offset:
        art = bytes(0x2000)
    elif scenery_source.data[3] & 4:
        art = split_lz(scenery_source, art_offset, 2, 0x1000)
    else:
        art = padded_lz(scenery_source, art_offset, 0x2000)
    # sub_803159C copies 0x18 words: three 16-color palette banks.
    scenery = static_scenery(r, objects, object_code, art, c.span(c.u32(0xE8), 96))
    bubbles = lava_bubbles(r, objects, object_code, art, c.span(c.u32(0xE8), 96))
    bubble_sections = (
        []
        if bubbles is None
        else [(39, course_id, bubbles[0], 2), (40, course_id, bubbles[1], 1)]
    )
    thwomp = thwomps(r, objects, object_code, art, c.span(c.u32(0xE8), 96))
    thwomp_sections = (
        []
        if thwomp is None
        else [(41, course_id, thwomp[0], 2), (42, course_id, thwomp[1], 1)]
    )
    crab = crabs(r, objects, object_code, art, c.span(c.u32(0xE8), 96))
    crab_sections = (
        []
        if crab is None
        else [(43, course_id, crab[0], 1), (44, course_id, crab[1], 1)]
    )
    seagull = seagulls(r, objects, object_code, art, c.span(c.u32(0xE8), 96))
    seagull_sections = (
        []
        if seagull is None
        else [(45, course_id, seagull[0], 1), (46, course_id, seagull[1], 1)]
    )
    piranha = piranhas(r, objects, object_code, art, c.span(c.u32(0xE8), 96))
    piranha_sections = (
        []
        if piranha is None
        else [(49, course_id, piranha[0], 1), (50, course_id, piranha[1], 1)]
    )
    snowman = snowmen(r, objects, object_code, art, c.span(c.u32(0xE8), 96))
    snowman_sections = (
        []
        if snowman is None
        else [(51, course_id, snowman[0], 1), (52, course_id, snowman[1], 1)]
    )
    star = falling_stars(r, objects, object_code, art, c.span(c.u32(0xE8), 96))
    star_sections = (
        []
        if star is None
        else [(77, course_id, star[0], 1), (78, course_id, star[1], 1)]
    )
    cloud = thunderclouds(r, objects, object_code, art, c.span(c.u32(0xE8), 96))
    cloud_sections = (
        []
        if cloud is None
        else [(75, course_id, cloud[0], 1), (76, course_id, cloud[1], 1)]
    )
    mouse = mice(r, objects, object_code, art, c.span(c.u32(0xE8), 96))
    mouse_sections = (
        []
        if mouse is None
        else [(73, course_id, mouse[0], 1), (74, course_id, mouse[1], 1)]
    )
    penguin = penguins(r, objects, object_code, art, c.span(c.u32(0xE8), 96))
    penguin_sections = (
        []
        if penguin is None
        else [(53, course_id, penguin[0], 1), (54, course_id, penguin[1], 1)]
    )
    boo = boos(r, objects, object_code, art, c.span(c.u32(0xE8), 96))
    boo_sections = (
        [] if boo is None else [(55, course_id, boo[0], 1), (56, course_id, boo[1], 1)]
    )
    # 48D44 installs the common race-effect bank after course scenery and
    # its five palettes after the three course banks. Normalize once here.
    cannonball = cannonballs(
        r,
        objects,
        object_code,
        art + r.span(0x66F0C, 0x4000),
        c.span(c.u32(0xE8), 96) + r.span(0x66E4C, 160),
    )
    cannonball_sections = (
        []
        if cannonball is None
        else [(47, course_id, cannonball[0], 1), (48, course_id, cannonball[1], 1)]
    )
    # Emission order is independent of the source decode order above.
    sections = (
        bubble_sections
        + thwomp_sections
        + crab_sections
        + seagull_sections
        + cannonball_sections
        + piranha_sections
        + snowman_sections
        + penguin_sections
        + boo_sections
        + mouse_sections
        + cloud_sections
        + star_sections
    )
    return scenery, art, sections

def _course_hud_payload(r, header, c, course_id):
    # sub_804DBF4 rank strip: five layouts and the source-class portrait pages.
    pages = r.span(0xEE6FA, 8)
    if max(pages) >= 8:
        raise ValueError("GP portrait page outside bank")
    gp_hud = words(0x31484747, course_id, 5, 52) + pages + r.span(0x64834, 0x400)
    gp_hud += c.lz(c.u32(0xC4), 0x800)
    for offset in (0xEED80, 0xEEDEC, 0xEEE58, 0xEEEC4, 0xEEB64):
        if (r.u16(offset), r.u16(offset + 2)) != (4, 13):
            raise ValueError("GP rank strip dimensions changed")
        gp_hud += r.span(offset + 4, 104)
    gp_hud += (
        r.span(0x64C34, 64) + gp_race_hud_animation_tail(r) + gp_finish_hud_tail(r)
    )
    gp_hud += words(minimap_coordinate_shift(header, c))
    return gp_hud

def course_sections(rom, header, source_progress=False, content_id=None, arena=False):
    """Import a track definition; IDs and start/finish positions come from ROM."""
    r = Data(rom)
    definition = r.pointer(0xE7FEC + header * 4)
    course_id, background = r.u32(definition), r.u32(definition + 4)
    background_behavior = r.u32(definition + 8)
    container = Data(r.span(0x258000, 0xB0000))
    if not 0 <= course_id < 49:
        raise ValueError(f"course {course_id} outside directory")
    offsets = [container.u32(i * 4) for i in range(49)] + [len(container.data)]
    if offsets != sorted(offsets) or offsets[0] < 49 * 4:
        raise ValueError("invalid course directory")
    c = Data(
        container.span(offsets[course_id], offsets[course_id + 1] - offsets[course_id])
    )
    raster_width, count, raster_cells = raster(c)
    width, height, map_cells = terrain(c)
    # Native IDs are independent of the source course-directory index.
    if content_id is not None:
        course_id = content_id
    tiles = road_tiles(source_course(r, header, container, offsets, 0x30))
    tiles[0x3800:0x4000] = r.lz(0x76C1C, 0x800)
    objects = stream(c, 0x8C)
    # Runtime tile aliases belong to static map-mutating objects. Actor-only
    # placements (e.g. crabs) keep their original terrain and render separately.
    class_tables = (
        0xF1054,
        0xF1060,
        0xF1088,
        0xF10A0,
        0xF1118,
        0xF1130,
        0xF1154,
        0xF10A0,
        0xF1174,
        0xF1188,
        0xF11A0,
        0xF10A0,
        0xF11C0,
        0xF11D8,
        0xF1204,
        0xF1228,
        0xF125C,
        0xF11EC,
        0xF10A0,
        0xF126C,
        0xF1008,
        0xF10A0,
        0xF1154,
        0xF1008,
        0xF1054,
    )
    if not (4 <= header <= 28 or 32 <= header <= 51):
        raise ValueError(f"course header {header}: object class not recovered")

    def object_code(kind):
        # 53DC4 selects the common table outside headers4..28.
        table = (
            0xF1008 - 0x200
            if kind > 127
            else (class_tables[header - 4] if header <= 28 else 0xF1008)
        )
        return r.u32(table + kind * 4)

    # Every placed class needs a native owner; never drop one silently.
    unrecovered = {
        object_code(kind) >> 16 for kind, _, _, _ in objects
    } - PLACED_OBJECT_CLASSES
    if unrecovered:
        raise ValueError(
            "course object class not recovered: "
            + ", ".join(f"{c:#x}" for c in sorted(unrecovered))
        )

    aliases = {}
    alias_codes = {4, 5, 6, 8, 9, 10, 12, 14, 0x86, 0x95, 0x96, 0x98}
    for kind, x, y, _ in objects:
        if object_code(kind) >> 16 not in alias_codes:
            continue
        if x >= width or y >= height:
            raise ValueError("course object outside terrain")
        source = map_cells[y * width + x]
        if source not in aliases:
            dynamic = 0xD0 + len(aliases)
            if dynamic >= 0xE0:
                raise ValueError("course has too many dynamic tile aliases")
            aliases[source] = dynamic
            tiles[dynamic * 64 : (dynamic + 1) * 64] = tiles[
                source * 64 : (source + 1) * 64
            ]
        map_cells[y * width + x] = aliases[source]
    material = c.span(c.u32(0x88), 256)
    palette = c.span(c.u32(0x84), 96) + r.span(0x76DE8, 32)
    boundary = [record for record in stream(c, 0x98) if record[0] == 0x8B]
    if arena:
        if boundary:
            raise ValueError("arena must not contain a start/finish line")
        _, starts = rom_grid(rom, header, 4)
        start_x, start_y, start_heading = starts[0]
        start_checkpoint = raster_cells[
            (start_y >> 20) * raster_width + (start_x >> 20)
        ]
        if start_checkpoint & 127 >= count:
            raise ValueError("arena start outside checkpoint regions")
        revision, rules_id, schema = 4, 4, 4
        rules = words(
            0x314C5552,
            course_id,
            revision,
            rules_id,
            1,
            2,
            2,
            3,
            schema,
            start_x,
            start_y,
        ) + struct.pack(
            "<HHIIIHHHH",
            start_heading,
            start_checkpoint & 127,
            3,
            3,
            0,
            0,
            180,
            180,
            start_checkpoint,
        )
    elif len(boundary) != 1:
        raise ValueError("course must have exactly one start/finish line")
    else:
        _, sx, sy, _ = boundary[0]
        revision, rules_id, schema = (3, 3, 3) if source_progress else (1, 1, 1)
        rules = words(
            0x314C5552,
            course_id,
            revision,
            rules_id,
            1,
            2,
            2,
            3,
            schema,
            (sx * 8 + 40) << 16,
            (sy * 8 + 28) << 16,
        ) + struct.pack(
            "<HHIIIHHHH",
            0,
            count - 1,
            2 if source_progress else 1,
            2 if source_progress else 1,
            (sy * 8 + 8) << 16,
            r.u32(definition + 0x34),
            180,
            180,
            (0x80 | count - 1) if source_progress else 0,
        )
    # RUL1 v2 appends the roulette threshold, keyed by the native ID.
    rules += struct.pack("<B3x", ITEM_STRENGTH_THRESHOLDS.get(course_id, 8))
    route_base = c.u32(0xCC)
    route_offset = route_base + c.u16(route_base + 3)
    target = r.span(r.pointer(definition + 0x1C), 40)
    if max(target) >= 3:
        raise ValueError("course AI target row outside original three rows")
    route = words(0x31545552, count, 3, 40)
    for index in range(count * 3):
        record = Data(c.span(route_offset + index * 8, 8))
        route += words(record.u16(0) * 8, record.u16(2) * 8, record.data[4])
    route += target
    route += route_sentinel_targets(
        container, offsets[r.u32(definition)], route_offset, count
    )
    item_boxes = [
        record for record in stream(c, 0x94) if object_code(record[0]) == 0xFFFF0000
    ]
    boxes = words(0x314A424F, len(item_boxes)) + b"".join(
        struct.pack("<IIHBB", x << 19, y << 19, 0x8800, parameter, 0x1E)
        for _, x, y, parameter in item_boxes
    )
    coins = stream(c, 0x90)
    if any(record[0] != 0x80 for record in coins):
        raise ValueError("invalid coin placement marker")
    manifest = words(
        0x31535243, course_id, revision, *([course_id] * 7), 1, 1, *([0] * 5)
    )
    _, minimap = shared_sections(rom)
    scenery, art, actor_sections = _course_actor_sections(
        r, header, c, container, offsets, objects, object_code, course_id
    )
    gp_hud = _course_hud_payload(r, header, c, course_id)
    opening = None if arena else gp_opening_payload(r, header, definition)
    opening_sections = [] if opening is None else [(36, course_id, opening, 27)]
    defaults = None if arena else time_trial_record_defaults(r, header, definition)
    if defaults is not None:
        opening_sections.append((93, course_id, defaults, 1))
    return (
        [
            (1, course_id, manifest, 1),
            (3, course_id, rules, 2),
            (70, course_id, surface_sound_rules(r, r.u32(definition + 0x10) & 255), 2),
            (4, course_id, route, 4),
            (
                5,
                course_id,
                words(
                    0x31444C57,
                    raster_width,
                    raster_width,
                    width,
                    height,
                    len(raster_cells),
                    len(map_cells),
                )
                + raster_cells
                + map_cells,
                1,
            ),
            (
                6,
                course_id,
                words(0x3154414D, 256)
                + material
                + words(
                    0x03A00000 if r.u32(definition + 0x10) & 255 == 12 else 0,
                    8 if r.u32(definition + 0x10) & 255 == 8 else 0,
                    170
                    if r.u32(definition + 0x10) & 255 == 0
                    else 169
                    if r.u32(definition + 0x10) & 255 == 12
                    else 168,
                )
                + struct.pack("<6H", *(r.u32(0xEC268 + i * 4) for i in range(6)))
                + cpu_item_profiles(r, header)
                + cpu_banana_decisions(r)
                + cpu_green_decisions(r)
                + cpu_red_decisions(r)
                + shell_orbit_delays(r)
                + mushroom_voices(r)
                + r.span(0xEDD60, 2178)
                + battle_item_rules(r)
                + battle_elimination_rules(r)
                + words(256, 128, 0x1000, 0x3000, 6, 64)
                + words(128, 0x1000, 48),
                16,
            ),
            (
                8,
                course_id,
                words(0x31444152, len(tiles), 64, c.data[3], 4, 0) + tiles + palette,
                1,
            ),
            (
                9,
                course_id,
                sky(r, background, background_behavior, r.u32(definition + 16)),
                2,
            ),
            (
                87,
                course_id,
                weather_payload(r, background_behavior, art, c.span(c.u32(0xE8), 96)),
                1,
            ),
            (
                84,
                course_id,
                ambient_rom.payload(
                    r,
                    background_behavior,
                    r.u32(definition + 12),
                    r.u32(definition + 16),
                    c.span(c.u32(0xE8), 96),
                ),
                1,
            ),
            (11, course_id, minimap, 1),
            (25, course_id, boxes, 1),
            (28, course_id, scenery, 4),
            (27, course_id, words(0x314E4943, len(coins)) + b"".join(coins), 1),
            (33, course_id, gp_hud, 5),
        ]
        + opening_sections
        + actor_sections
    )
