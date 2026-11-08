#!/usr/bin/env python3
"""Export a course to an editable track folder, or compile a folder to sections.

export PACK COURSE_ID DIR
    Write track.json, tileset.png/.json, tilemap.png, raster.png, sky.bin and sky PNGs,
    minimap.png, name.png and pass-through binaries for one racing course.
compile DIR --base PACK --output OUT_SECTIONS
    Rebuild the course sections (1,3,4,5,6,8,9,11,23,25,27,33,34,70 and the
    pass-through kinds) into OUT_SECTIONS/manifest.json + *.bin, a manifest
    accepted by build_asset_pack.py. Anything the folder omits is derived:
    progress raster from the route, AI rows and modes, GP grid, minimap and
    standings name tiles. Global data comes from template_course.

All coordinates in track.json are world units (8 per terrain tile, 16 per
raster cell). Section layouts follow course_rom.py and assets/*.c.
"""
import argparse
from dataclasses import dataclass
import json
import math
import pathlib
import struct
import sys

import build_asset_pack
import png_indexed
from course_rom import ITEM_STRENGTH_THRESHOLDS
from text_data_mod import _GLYPHS

FORMAT = 1
CUSTOM_IDS = range(128, 192)
ROUTE_LIMIT = 127              # raster values keep bit7 for the finish region
OBJECT_LIMIT = 64
OFF_ROUTE = 0x7F
FINISH_REGION = 0x80
RASTER_CELL = 16
CORRIDOR = 256                 # derived raster: world units from the route
VOID_CORRIDOR = 80             # ...but cells without ground only this close
FINISH_HALF_WIDTH = 128        # finish crossings counted within this of start.x
AI_ROW_OFFSET = 16
GRID_HALF_SPACING, GRID_ROW_SPACING, GRID_START_GAP = 20, 24, 8
MINIMAP_ROAD, MINIMAP_EDGE = 14, 11
NAME_SIZE = (96, 16)
NAME_TILE_OFFSET = 32 + 0x400   # PGS1 header + tiles+0x400, 12x2 tiles
MINIMAP_OFFSET = 0x418          # GGH1 64x64 4bpp, 8x8 tiles
HUD_MINIMAP_BANK = 16           # HUD palette entries 16..31 (BG bank 11)
PGS_NAME_BANK = 192             # PGS1 palette entries 192..207 (bank 12)
SHARED_TILES = range(0xE0, 0x100)   # ROM tiles shared by every course
ALIAS_TILES = range(0xD0, 0xE0)     # runtime object tile aliases
SHARED_COLOURS = range(48, 64)

# Documented material names (driver_material.c / ARCHITECTURE.md).
MATERIALS = {
    'road': 0x40, 'offroad': 0x5A, 'wall': 0x80, 'boost': 0x16,
    'jump': 0x10, 'small_jump': 0x12, 'bump': 0x1E, 'pit': 0x20,
    'lava': 0x24, 'bounds': 0x26, 'water': 0x22,
}
MATERIAL_NAMES = {value: name for name, value in MATERIALS.items()}
# Custom tracks may use named materials plus other ground rows and walls.

REQUIRED_KINDS = (1, 3, 4, 5, 6, 8, 9, 11, 23, 25, 27, 33, 34, 70)
PASSTHROUGH = {
    36: ('opening', 27),
    28: ('scenery', 4), 39: ('lava_bubbles', 2), 40: ('lava_bubble_art', 1),
    41: ('thwomps', 2), 42: ('thwomp_art', 1), 43: ('crabs', 1),
    44: ('crab_art', 1), 45: ('seagulls', 1), 46: ('seagull_art', 1),
    47: ('cannonballs', 1), 48: ('cannonball_art', 1), 49: ('piranhas', 1),
    50: ('piranha_art', 1), 51: ('snowmen', 1), 52: ('snowman_art', 1),
    53: ('penguins', 1), 54: ('penguin_art', 1), 55: ('boos', 1),
    56: ('boo_art', 1), 73: ('mice', 1), 74: ('mouse_art', 1),
    75: ('thunderclouds', 1), 76: ('thundercloud_art', 1),
    77: ('falling_stars', 1), 78: ('falling_star_art', 1),
    84: ('ambient', 1),
    93: ('records', 1),
}
TRACK_KEYS = {'format', 'id', 'name', 'laps', 'song', 'template_course', 'start',
              'finish_y', 'route', 'ai_rows', 'target_table', 'ai_sentinels',
              'grid', 'item_boxes', 'coins', 'passthrough', 'progress',
              'rules_version', 'item_strength_threshold', 'countdown_ticks',
              'rescue_ticks'}


class TrackError(ValueError):
    """A named validation failure; str() starts with the code."""

    def __init__(self, code, message):
        super().__init__(f'{code}: {message}')
        self.code = code


def u32s(data, offset, count):
    return struct.unpack_from(f'<{count}I', data, offset)


def words(*values):
    return struct.pack(f'<{len(values)}I', *values)


def q16_units(value, what):
    if value & 0xFFFF:
        raise TrackError('FractionalCoordinate', f'{what} {value:#x} is not a whole world unit')
    return (value if value < 0x80000000 else value - (1 << 32)) >> 16


def load_pack(path):
    return {(header['kind'], header['content_id']): (header, bytes(data))
            for header, data in build_asset_pack.read_sections(path)}


def _base_sections(base):
    return load_pack(base) if isinstance(base, (str, pathlib.Path)) else base


# ---------------------------------------------------------------- export --

def parse_rules(data, version):
    if version not in (1, 2) or len(data) != (68 if version == 1 else 72) or \
            u32s(data, 0, 1)[0] != 0x314C5552:
        raise TrackError('RulesFormat', f'unsupported RUL1 v{version} ({len(data)} bytes)')
    (_, course, revision, rules_id, rules_rev, handling, handling_rev,
     character, schema, start_x, start_y) = u32s(data, 0, 11)
    heading, start_index, progress, finish, finish_y, laps, countdown, rescue, \
        start_cp = struct.unpack_from('<HHIIIHHHH', data, 44)
    return dict(course=course, revision=revision, rules_id=rules_id,
                fixed=(rules_rev, handling, handling_rev, character), schema=schema,
                start_x=start_x, start_y=start_y, heading=heading,
                start_index=start_index, progress=progress, finish=finish,
                finish_y=finish_y, laps=laps, countdown=countdown, rescue=rescue,
                start_cp=start_cp, threshold=data[68] if version == 2 else None)


def parse_route(data):
    magic, count, rows, table = u32s(data, 0, 4)
    if magic != 0x31545552 or rows != 3 or table != 40 or \
            len(data) != 16 + count * rows * 12 + 40 + 28:
        raise TrackError('RouteFormat', 'expected RUT1 v4 with three AI rows')
    points = [u32s(data, 16 + index * 12, 3) for index in range(count * rows)]
    tail = 16 + count * rows * 12
    sentinel = u32s(data, tail + 40, 7)
    return (count, [points[row * count:(row + 1) * count] for row in range(rows)],
            list(data[tail:tail + 40]), sentinel)


def tileset_image(tiles):
    pixels = bytearray(128 * 128)
    for tile in range(256):
        for row in range(8):
            start = ((tile // 16) * 8 + row) * 128 + (tile % 16) * 8
            pixels[start:start + 8] = tiles[tile * 64 + row * 8:tile * 64 + row * 8 + 8]
    return pixels


def tileset_tiles(pixels):
    tiles = bytearray(0x4000)
    for tile in range(256):
        for row in range(8):
            start = ((tile // 16) * 8 + row) * 128 + (tile % 16) * 8
            tiles[tile * 64 + row * 8:tile * 64 + row * 8 + 8] = pixels[start:start + 8]
    return tiles


def planar_image(data, columns, rows):
    """4bpp GBA tiles (row-major tile order) to one pixel byte per index."""
    width = columns * 8
    pixels = bytearray(width * rows * 8)
    for tile in range(columns * rows):
        for y in range(8):
            for x in range(8):
                value = data[tile * 32 + y * 4 + x // 2] >> (x & 1) * 4 & 15
                pixels[((tile // columns) * 8 + y) * width + (tile % columns) * 8 + x] = value
    return pixels


def planar_tiles(pixels, columns, rows):
    width = columns * 8
    data = bytearray(columns * rows * 32)
    for tile in range(columns * rows):
        for y in range(8):
            for x in range(0, 8, 2):
                source = ((tile // columns) * 8 + y) * width + (tile % columns) * 8 + x
                data[tile * 32 + y * 4 + x // 2] = pixels[source] | pixels[source + 1] << 4
    return data


def palette_rgb(data, offset, count):
    return [png_indexed.bgr555_to_rgb(colour)
            for colour in struct.unpack_from(f'<{count}H', data, offset)]


def material_label(value):
    return MATERIAL_NAMES.get(value, f'{value:#04x}')


def catalog_song(sections, course_id):
    for (kind, _), (_, data) in sections.items():
        if kind != 37 or len(data) < 8 or u32s(data, 0, 1)[0] != 0x31544143:
            continue
        count = u32s(data, 4, 1)[0]
        for row in range(count):
            if 8 + row * 12 + 12 > len(data):
                break
            ident, packed, _ = u32s(data, 8 + row * 12, 3)
            if ident == course_id:
                return packed >> 16
    return None


SKY_MOTION_KEYS = {'controller', 'layer_mask', 'blend_mode', 'blend_a', 'blend_b',
                   'yaw_shift', 'x_bias', 'y_bias', 'split_y', 'split_layer',
                   'split_yaw_shift', 'split_x_bias', 'lightning_hide_layer', 'layer_source', 'road_mode'}


def sky_motion_bytes(motion):
    """Encode the bounded SKY1 v2 motion record without host or ROM pointers."""
    if type(motion) is not dict or set(motion) != SKY_MOTION_KEYS:
        raise TrackError('SkyFormat', 'sky-motion.json must contain exactly the documented motion fields')
    def integer(key, low, high):
        return _int(motion[key], low, high, 'SkyFormat', 'sky-motion.' + key)
    def vector(key, low, high):
        values = motion[key]
        if type(values) is not list or len(values) != 3:
            raise TrackError('SkyFormat', 'sky-motion.' + key + ' needs three values')
        return [_int(value, low, high, 'SkyFormat', 'sky-motion.' + key) for value in values]
    return (struct.pack('<IIBBBBB3B', 0x31524D53, 1,
                        integer('controller', 0, 7), integer('layer_mask', 0, 7),
                        integer('blend_mode', 0, 1), integer('blend_a', 0, 16),
                        integer('blend_b', 0, 16), *vector('yaw_shift', 0, 15)) +
            struct.pack('<3h3hhBBhBB3BB', *vector('x_bias', -32768, 32767),
                        *vector('y_bias', -32768, 32767), integer('split_y', -1, 159),
                        integer('split_layer', 0, 2), integer('split_yaw_shift', 0, 15),
                        integer('split_x_bias', -32768, 32767), integer('lightning_hide_layer', 0, 2), 0,
                        *vector('layer_source', 0, 2), integer('road_mode', 0, 3)))


def sky_motion(data):
    extension = u32s(data, 60, 1)[0]
    if extension == 0:
        return None
    if extension != 40:
        raise TrackError('SkyFormat', 'sky motion extension must contain 40 bytes')
    tail = data[-extension:]
    if len(tail) != 40 or u32s(tail, 0, 2) != (0x31524D53, 1) or any(tail[i] for i in (35,)):
        raise TrackError('SkyFormat', 'invalid SMR1 sky motion record')
    motion = dict(zip(('controller', 'layer_mask', 'blend_mode', 'blend_a', 'blend_b'), tail[8:13]))
    motion.update(yaw_shift=list(tail[13:16]), x_bias=list(struct.unpack_from('<3h', tail, 16)),
                  y_bias=list(struct.unpack_from('<3h', tail, 22)),
                  split_y=struct.unpack_from('<h', tail, 28)[0], split_layer=tail[30],
                  split_yaw_shift=tail[31], split_x_bias=struct.unpack_from('<h', tail, 32)[0],
                  lightning_hide_layer=tail[34], layer_source=list(tail[36:39]), road_mode=tail[39])
    sky_motion_bytes(motion)  # Share export and authoring bounds.
    return motion


def sky_layout(data):
    """Validate SKY1 and retain its original metadata and screenblock maps."""
    if len(data) < 64 or data[:4] != b'SKY1':
        raise TrackError('SkyFormat', 'expected a SKY1 section payload')
    fields = u32s(data, 0, 16)
    tile_bytes, colours = fields[1:3]
    if not 0 < tile_bytes <= 0x2800 or tile_bytes % 32 or not 0 < colours <= 64:
        raise TrackError('SkyFormat', 'invalid sky tile or palette size')
    if len(data) != 64 + tile_bytes + colours * 2 + sum(fields[3:6]) + fields[15]:
        raise TrackError('SkyFormat', 'sky payload size does not match its header')
    for size, width in zip(fields[3:6], fields[6:9]):
        if width not in (32, 64) or size not in (width * 32 * 2, width * 64 * 2):
            raise TrackError('SkyFormat', 'invalid sky layer dimensions')
    sky_motion(data)
    return tile_bytes, colours, (tile_bytes // 32 + 15) // 16


def export_sky(data, directory):
    tile_bytes, colours, rows = sky_layout(data)
    (directory / 'sky.bin').write_bytes(data)
    motion = sky_motion(data)
    motion_path = directory / 'sky-motion.json'
    if motion is not None:
        motion_path.write_text(json.dumps(motion, indent=1) + '\n')
    elif motion_path.exists():
        motion_path.unlink()
    # A tile may appear in several palette banks; its PNG stores nibble values.
    padded = data[64:64 + tile_bytes] + bytes(rows * 16 * 32 - tile_bytes)
    png_indexed.write(directory / 'sky-tiles.png', 128, rows * 8,
                      planar_image(padded, 16, rows))
    png_indexed.write(directory / 'sky-palette.png', colours, 1, bytes(range(colours)),
                      palette_rgb(data, 64 + tile_bytes, colours))


def compile_sky(data, directory):
    tile_bytes, colours, rows = sky_layout(data)
    result = bytearray(data)
    image = _image(directory, 'sky-tiles.png', 'SkyFormat', (128, rows * 8), required=False)
    if image is not None:
        if max(image[2]) > 15:
            raise TrackError('SkyFormat', 'sky-tiles.png pixels must use indices 0..15')
        tiles = planar_tiles(image[2], 16, rows)
        if any(tiles[tile_bytes:]):
            raise TrackError('SkyFormat', 'sky-tiles.png padding tiles must remain zero')
        result[64:64 + tile_bytes] = tiles[:tile_bytes]
    image = _image(directory, 'sky-palette.png', 'SkyFormat', (colours, 1),
                   indexed=True, required=False)
    if image is not None:
        if image[2] != bytes(range(colours)) or len(image[3]) != colours:
            raise TrackError('SkyFormat', 'sky-palette.png must retain its palette order and count')
        offset = 64 + tile_bytes
        original = struct.unpack_from(f'<{colours}H', data, offset)
        palette = [png_indexed.rgb_to_bgr555(rgb) | (prior & 0x8000)
                   for rgb, prior in zip(image[3], original)]
        result[offset:offset + colours * 2] = struct.pack(f'<{colours}H', *palette)
    motion_path = directory / 'sky-motion.json'
    if motion_path.exists():
        try:
            motion = json.loads(motion_path.read_text())
        except json.JSONDecodeError as error:
            raise TrackError('SkyFormat', f'sky-motion.json: {error}') from None
        tail = sky_motion_bytes(motion)
        extension = u32s(result, 60, 1)[0]
        if extension:
            result[-extension:] = tail
        else:
            result += tail
        result[60:64] = words(len(tail))
    return bytes(result)


def export_course(base, course_id, directory):
    sections = _base_sections(base)
    missing = [kind for kind in REQUIRED_KINDS if (kind, course_id) not in sections]
    if missing:
        raise TrackError('MissingSection', f'course {course_id} lacks kinds {missing}')
    if (12, 1) not in sections:
        raise TrackError('MissingSection', 'pack lacks the HUD palette (kind 12 id 1)')
    data = {kind: sections[(kind, course_id)][1] for kind in REQUIRED_KINDS}
    version = {kind: sections[(kind, course_id)][0]['version'] for kind in REQUIRED_KINDS}
    directory = pathlib.Path(directory)
    directory.mkdir(parents=True, exist_ok=True)

    rules = parse_rules(data[3], version[3])
    if rules['progress'] == 3:
        raise TrackError('ArenaNotSupported', f'course {course_id} is a Battle arena')
    source = rules['progress'] == 2
    count, rows, target_table, sentinel = parse_route(data[4])
    expected = ((3, 3, 2, 2, 0x80 | count - 1) if source else (1, 1, 1, 1, 0))
    if (rules['revision'], rules['schema'], rules['progress'], rules['finish'],
            rules['start_cp']) != expected or rules['rules_id'] != rules['revision'] or \
            rules['fixed'] != (1, 2, 2, 3) or rules['start_index'] != count - 1:
        raise TrackError('RulesFormat', f'course {course_id} rules do not follow the stock layout')
    track = {
        'format': FORMAT, 'id': course_id, 'name': f'COURSE {course_id}',
        'laps': rules['laps'], 'song': catalog_song(sections, course_id) or 1,
        'template_course': course_id,
        'progress': 'source' if source else 'demo',
        'rules_version': version[3],
        'start': {'x': q16_units(rules['start_x'], 'start.x'),
                  'y': q16_units(rules['start_y'], 'start.y'),
                  'heading': rules['heading']},
        'finish_y': q16_units(rules['finish_y'], 'finish_y'),
        'route': [{'x': x, 'y': y, 'mode': mode} for x, y, mode in rows[0]],
        'ai_rows': [[{'x': x, 'y': y, 'mode': mode} for x, y, mode in row] for row in rows[1:]],
        'target_table': target_table,
        'ai_sentinels': {'mask': sentinel[0],
                         'points': [list(sentinel[1 + i * 2:3 + i * 2]) for i in range(3)]},
    }
    if rules['threshold'] is not None:
        track['item_strength_threshold'] = rules['threshold']
    if rules['countdown'] != 180:
        track['countdown_ticks'] = rules['countdown']
    if rules['rescue'] != 180:
        track['rescue_ticks'] = rules['rescue']

    magic, raster_w, raster_h, map_w, map_h, raster_n, map_n = u32s(data[5], 0, 7)
    if magic != 0x31444C57 or len(data[5]) != 28 + raster_n + map_n:
        raise TrackError('WorldFormat', 'malformed WLD1')
    png_indexed.write(directory / 'raster.png', raster_w, raster_h, data[5][28:28 + raster_n])
    png_indexed.write(directory / 'tilemap.png', map_w, map_h, data[5][28 + raster_n:])

    magic, tile_bytes, colours, flags, blocks, zero = u32s(data[8], 0, 6)
    if magic != 0x31444152 or tile_bytes != 0x4000 or colours != 64 or zero or \
            len(data[8]) != 24 + tile_bytes + colours * 2:
        raise TrackError('TilesetFormat', 'expected RAD1 with 256 tiles and 64 colours')
    tiles = data[8][24:24 + tile_bytes]
    raw_palette = struct.unpack_from('<64H', data[8], 24 + tile_bytes)
    palette = palette_rgb(data[8], 24 + tile_bytes, 64)
    palette += [(0, 0, 0)] * (max(max(tiles) + 1, 64) - 64)
    png_indexed.write(directory / 'tileset.png', 128, 128, tileset_image(tiles), palette)
    tileset = {'flags': flags, 'block_count': blocks,
               'materials': [material_label(value) for value in data[6][8:264]]}
    high = [index for index, colour in enumerate(raw_palette) if colour & 0x8000]
    if high:
        tileset['palette_bit15'] = high
    (directory / 'tileset.json').write_text(json.dumps(tileset, indent=1) + '\n')

    export_sky(data[9], directory)
    hud = sections[(12, 1)][1]
    hud_palette = 44 + u32s(hud, 4, 1)[0] + HUD_MINIMAP_BANK * 2
    png_indexed.write(directory / 'minimap.png', 64, 64,
                      planar_image(data[33][MINIMAP_OFFSET:MINIMAP_OFFSET + 0x800], 8, 8),
                      palette_rgb(hud, hud_palette, 16))
    standings = data[23]
    name_palette = 32 + 0x3000 + PGS_NAME_BANK * 2
    png_indexed.write(directory / 'name.png', *NAME_SIZE,
                      planar_image(standings[NAME_TILE_OFFSET:NAME_TILE_OFFSET + 24 * 32], 12, 2),
                      palette_rgb(standings, name_palette, 16))

    magic, boxes = u32s(data[25], 0, 2)
    if magic != 0x314A424F or len(data[25]) != 8 + boxes * 12:
        raise TrackError('ObjectFormat', 'malformed OBJ1')
    track['item_boxes'] = []
    for index in range(boxes):
        x, y, kind, parameter, extent = struct.unpack_from('<IIHBB', data[25], 8 + index * 12)
        if kind != 0x8800 or extent != 0x1E:
            raise TrackError('ObjectFormat', 'unsupported item box record')
        track['item_boxes'].append({'x': q16_units(x, 'item box x'),
                                    'y': q16_units(y, 'item box y'), 'param': parameter})
    magic, coins = u32s(data[27], 0, 2)
    if magic != 0x314E4943 or len(data[27]) != 8 + coins * 4:
        raise TrackError('ObjectFormat', 'malformed CIN1')
    track['coins'] = [{'x': data[27][9 + i * 4] * 8, 'y': data[27][10 + i * 4] * 8,
                       'param': data[27][11 + i * 4]} for i in range(coins)]
    magic, grid_count = u32s(data[34], 0, 2)
    if magic != 0x31475047 or len(data[34]) != 8 + grid_count * 12:
        raise TrackError('GridFormat', 'malformed GPG1')
    track['grid'] = [{'x': q16_units(x, 'grid x'), 'y': q16_units(y, 'grid y'), 'heading': h}
                     for x, y, h in (u32s(data[34], 8 + i * 12, 3) for i in range(grid_count))]
    passthrough = {}
    for kind, (name, default) in PASSTHROUGH.items():
        if (kind, course_id) not in sections:
            continue
        header, payload = sections[(kind, course_id)]
        (directory / f'{name}.bin').write_bytes(payload)
        passthrough[str(kind)] = (f'{name}.bin' if header['version'] == default else
                                  {'file': f'{name}.bin', 'version': header['version']})
    track['passthrough'] = passthrough
    (directory / 'track.json').write_text(json.dumps(track, indent=1) + '\n')
    return track


# ------------------------------------------------------------ derivation --

def _closest(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    length = dx * dx + dy * dy
    t = 0.0 if not length else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / length))
    cx, cy = ax + t * dx, ay + t * dy
    return (px - cx) ** 2 + (py - cy) ** 2, t


def ground_cells(tilemap, map_width, materials):
    """Per raster cell: does any of its 2x2 terrain tiles have a ground material."""
    width = map_width // 2
    ground = bytearray(width * (len(tilemap) // map_width // 2))
    for cy in range(len(ground) // width):
        for cx in range(width):
            ground[cy * width + cx] = any(
                0x10 <= materials[tilemap[(cy * 2 + j) * map_width + cx * 2 + i]] < 0x20 or
                0x40 <= materials[tilemap[(cy * 2 + j) * map_width + cx * 2 + i]] < 0x60
                for j in (0, 1) for i in (0, 1))
    return ground


def derive_raster(route, finish_y, width, height, ground=None, corridor=CORRIDOR):
    """Route index per 16-unit cell (course_rom.raster semantics).

    route[k-1] lies in region k, so a cell takes region (nearest point + 1);
    regions 0 and n-1 carry bit7 and meet exactly at finish_y on the
    segment that crosses the finish line. Cells farther than CORRIDOR, or
    without ground tiles and farther than VOID_CORRIDOR, stay 0x7f.
    """
    count = len(route)
    cells = bytearray([OFF_ROUTE]) * (width * height)
    limit = corridor * corridor
    segments = [(route[i][0], route[i][1], route[(i + 1) % count][0], route[(i + 1) % count][1])
                for i in range(count)]
    for cy in range(height):
        py = cy * RASTER_CELL + RASTER_CELL / 2
        for cx in range(width):
            px = cx * RASTER_CELL + RASTER_CELL / 2
            best, best_segment, best_t = limit, None, 0.0
            for index, (ax, ay, bx, by) in enumerate(segments):
                if min(ax, bx) - corridor > px or max(ax, bx) + corridor < px or \
                        min(ay, by) - corridor > py or max(ay, by) + corridor < py:
                    continue
                distance, t = _closest(px, py, ax, ay, bx, by)
                if distance < best:
                    best, best_segment, best_t = distance, index, t
            if best_segment is None or (ground is not None and not ground[cy * width + cx]
                                        and best > VOID_CORRIDOR * VOID_CORRIDOR):
                continue
            if best_segment == count - 2:
                region = 0 if py < finish_y else count - 1
            else:
                nearest = best_segment + (best_t >= 0.5)
                region = (nearest + 1) % count
            value = region | (FINISH_REGION if region in (0, count - 1) else 0)
            cells[cy * width + cx] = value
    return bytes(cells)


def _turn(route, index):
    count = len(route)
    (px, py), (x, y), (nx, ny) = (route[(index - 1) % count][:2], route[index][:2],
                                  route[(index + 1) % count][:2])
    first, second = math.atan2(y - py, x - px), math.atan2(ny - y, nx - x)
    return (second - first + math.pi) % (2 * math.pi) - math.pi


def derive_mode(route, index):
    """AI mode 1 (sharp) .. 3 (straight) from the turn at and after a point."""
    degrees = math.degrees(abs(_turn(route, index)) + abs(_turn(route, (index + 1) % len(route))))
    return 1 if degrees >= 50 else 2 if degrees >= 8 else 3


def derive_ai_rows(route):
    """Rows 1/2 sit 16 units either side of the centre line on turns."""
    count = len(route)
    rows = [[], []]
    for index, (x, y, mode) in enumerate(route):
        (px, py), (nx, ny) = route[(index - 1) % count][:2], route[(index + 1) % count][:2]
        dx, dy = nx - px, ny - py
        length = math.hypot(dx, dy) or 1.0
        turn = _turn(route, index)
        side = 0 if abs(turn) < 0.15 else (1 if turn > 0 else -1)
        for row, sign in ((0, -side), (1, side)):
            # Stock row 1 lies on the side whose cross product opposes the turn.
            ox = round(sign * AI_ROW_OFFSET * dy / length / 8) * 8
            oy = round(-sign * AI_ROW_OFFSET * dx / length / 8) * 8
            rows[row].append((x + ox, y + oy, mode))
    return rows


def derive_grid(start):
    return [(start[0] - GRID_HALF_SPACING + 2 * GRID_HALF_SPACING * (rank & 1),
             start[1] + GRID_START_GAP + GRID_ROW_SPACING * rank, start[2]) for rank in range(8)]


def road_material(value):
    return 0x40 <= value <= 0x50 or value == 0x5E or 0x10 <= value <= 0x1E


def derive_minimap(tilemap, width, materials):
    scale = width // 64
    road = bytearray(64 * 64)
    for y in range(64):
        for x in range(64):
            hits = sum(road_material(materials[tilemap[(y * scale + j) * width + x * scale + i]])
                       for j in range(scale) for i in range(scale))
            road[y * 64 + x] = hits * 2 >= scale * scale
    pixels = bytearray(64 * 64)
    for y in range(64):
        for x in range(64):
            if road[y * 64 + x]:
                pixels[y * 64 + x] = MINIMAP_ROAD
            elif any(road[ny * 64 + nx] for ny in range(max(0, y - 1), min(64, y + 2))
                     for nx in range(max(0, x - 1), min(64, x + 2))):
                pixels[y * 64 + x] = MINIMAP_EDGE
    return pixels


def derive_name(name, template_pixels):
    """Render the name with the shared 3x5 menu font into a 96x16 image."""
    text = name.upper()
    unsupported = sorted({c for c in text if c not in _GLYPHS})
    if unsupported:
        raise TrackError('NameGlyph', f'name uses unsupported characters {unsupported}')
    colours = [value for value in template_pixels if value]
    ink = max(set(colours), key=colours.count) if colours else 1
    width, height = NAME_SIZE
    scale = 2 if len(text) * 8 - 2 <= width else 1
    text_width = len(text) * 4 * scale - scale
    if text_width > width:
        raise TrackError('NameTooLong', f'name {name!r} needs {text_width} pixels (max {width})')
    pixels = bytearray(width * height)
    left, top = (width - text_width) // 2, (height - 5 * scale) // 2
    for index, character in enumerate(text):
        glyph = _GLYPHS[character]
        for row in range(5):
            for column in range(3):
                if glyph & 1 << (14 - row * 3 - column):
                    for dy in range(scale):
                        for dx in range(scale):
                            pixels[(top + row * scale + dy) * width + left +
                                   (index * 4 + column) * scale + dx] = ink
    return pixels


# --------------------------------------------------------------- compile --

def _int(value, low, high, code, what):
    if type(value) is not int or not low <= value <= high:
        raise TrackError(code, f'{what} must be an integer in {low}..{high}, got {value!r}')
    return value


def _point(entry, what, size):
    if type(entry) is not dict or not {'x', 'y'} <= set(entry) or set(entry) - {'x', 'y', 'mode', 'heading', 'param'}:
        raise TrackError('PointFormat', f'{what} must be an object with x and y')
    x = _int(entry['x'], 0, size - 1, 'OutOfBounds', f'{what}.x')
    y = _int(entry['y'], 0, size - 1, 'OutOfBounds', f'{what}.y')
    return x, y


def _image(directory, name, code, size=None, indexed=None, required=True):
    path = directory / name
    if not path.exists():
        if required:
            raise TrackError('MissingFile', f'{name} is required')
        return None
    try:
        width, height, pixels, palette = png_indexed.read(path)
    except png_indexed.PngError as error:
        raise TrackError(code, str(error)) from None
    if indexed is not None and (palette is not None) != indexed:
        raise TrackError(code, f'{name} must be {"indexed" if indexed else "greyscale"}')
    if size is not None and (width, height) != size:
        raise TrackError('ImageSize', f'{name} must be {size[0]}x{size[1]}, got {width}x{height}')
    return width, height, pixels, palette


def _material(entry, index):
    if type(entry) is int and 0 <= entry <= 255:
        return entry
    if type(entry) is str:
        if entry in MATERIALS:
            return MATERIALS[entry]
        try:
            value = int(entry, 16) if entry.lower().startswith('0x') else None
        except ValueError:
            value = None
        if value is not None and 0 <= value <= 255:
            return value
    raise TrackError('MaterialName', f'tileset.json materials[{index}] = {entry!r} is not a '
                     f'documented name ({", ".join(MATERIALS)}) or a 0xNN byte')


def _finish_crossings(route, start_x, finish_y):
    north, south = [], []
    count = len(route)
    for index in range(count):
        (ax, ay), (bx, by) = route[index][:2], route[(index + 1) % count][:2]
        if (ay >= finish_y) == (by >= finish_y):
            continue
        x = ax + (bx - ax) * (finish_y - ay) / (by - ay)
        if abs(x - start_x) > FINISH_HALF_WIDTH:
            continue
        (north if by < ay else south).append(index)
    return north, south


def _rules_payload(track_id, revision, schema, start, rules, finish_y, count, version,
                   threshold):
    source = rules['progress'] == 'source'
    data = (words(0x314C5552, track_id, revision, revision, 1, 2, 2, 3, schema,
                  start[0] << 16 & 0xFFFFFFFF, start[1] << 16 & 0xFFFFFFFF) +
            struct.pack('<HHIIIHHHH', start[2], count - 1, 2 if source else 1,
                        2 if source else 1, finish_y << 16, rules['laps'],
                        rules['countdown'], rules['rescue'],
                        (0x80 | count - 1) if source else 0))
    return data + (struct.pack('<B3x', threshold) if version == 2 else b'')


@dataclass(frozen=True)
class _TrackDefinition:
    track: dict
    template: int
    track_id: int
    custom: bool
    rules: dict
    rules_version: int
    threshold: int
    base_data: dict
    base_version: dict


@dataclass(frozen=True)
class _TerrainData:
    width: int
    height: int
    size: int
    tilemap: bytes
    tileset: dict
    materials: bytes
    tiles: bytes
    colours: list
    road: bytes


@dataclass(frozen=True)
class _RouteData:
    points: list
    start: tuple
    finish_y: int
    count: int
    payload: bytes


@dataclass(frozen=True)
class _PresentationData:
    gp_hud: bytearray
    standings: bytearray
    name_pixels: bytes
    sky: bytes




def _read_track_definition(directory, sections):
    path = directory / 'track.json'
    if not path.exists():
        raise TrackError('MissingFile', 'track.json is required')
    try:
        track = json.loads(path.read_text())
    except json.JSONDecodeError as error:
        raise TrackError('BadJson', f'track.json: {error}') from None
    if type(track) is not dict:
        raise TrackError('BadJson', 'track.json must hold an object')
    unknown = sorted(set(track) - TRACK_KEYS)
    if unknown:
        raise TrackError('UnknownField', f'track.json has unknown fields {unknown}')
    for key in ('id', 'name', 'laps', 'song', 'template_course', 'start', 'finish_y', 'route'):
        if key not in track:
            raise TrackError('MissingField', f'track.json lacks {key!r}')
    if track.get('format', FORMAT) != FORMAT:
        raise TrackError('FormatVersion', f'track.json format must be {FORMAT}')
    template = _int(track['template_course'], 1, 255, 'TemplateMissing', 'template_course')
    missing = [kind for kind in REQUIRED_KINDS if (kind, template) not in sections]
    if missing:
        raise TrackError('TemplateMissing', f'base pack lacks template course {template} kinds {missing}')
    base_data = {kind: sections[(kind, template)][1] for kind in REQUIRED_KINDS}
    base_version = {kind: sections[(kind, template)][0]['version'] for kind in REQUIRED_KINDS}
    track_id = track['id']
    if type(track_id) is not int or not (track_id in CUSTOM_IDS or track_id == template):
        raise TrackError('IdOutOfRange', f'id must be {CUSTOM_IDS.start}..{CUSTOM_IDS.stop - 1} '
                         f'or equal template_course, got {track_id!r}')
    custom = track_id in CUSTOM_IDS
    if type(track['name']) is not str or not track['name'].strip():
        raise TrackError('MissingField', 'name must be a non-empty string')
    _int(track['song'], 1, 0xFFFF, 'SongOutOfRange', 'song')
    rules = {'laps': _int(track['laps'], 1, 5, 'LapsOutOfRange', 'laps'),
             'progress': track.get('progress', 'source'),
             'countdown': _int(track.get('countdown_ticks', 180), 1, 0xFFFF, 'RulesFormat', 'countdown_ticks'),
             'rescue': _int(track.get('rescue_ticks', 180), 1, 0xFFFF, 'RulesFormat', 'rescue_ticks')}
    if rules['progress'] not in ('source', 'demo') or (custom and rules['progress'] != 'source'):
        raise TrackError('RulesFormat', 'progress must be "source" (or "demo" for a stock re-export)')
    template_rules = parse_rules(base_data[3], base_version[3])
    rules_version = _int(track.get('rules_version', 2), 1, 2, 'RulesFormat', 'rules_version')
    if 'item_strength_threshold' in track:
        threshold = _int(track['item_strength_threshold'], 1, 8, 'RulesFormat', 'item_strength_threshold')
    else:
        threshold = template_rules['threshold'] or ITEM_STRENGTH_THRESHOLDS.get(template, 8)

    return _TrackDefinition(track, template, track_id, custom, rules,
                            rules_version, threshold, base_data, base_version)


def _read_terrain(directory, definition):
    base_data, custom = definition.base_data, definition.custom
    # Terrain and tiles.
    map_w, map_h, tilemap, _ = _image(directory, 'tilemap.png', 'TilemapFormat', indexed=False)
    if map_w != map_h or map_w not in (128, 256):
        raise TrackError('ImageSize', f'tilemap.png must be 128x128 or 256x256, got {map_w}x{map_h}')
    size = map_w * 8
    _, _, tile_pixels, tile_palette = _image(directory, 'tileset.png', 'TilesetFormat',
                                             (128, 128), indexed=True)
    if len(tile_palette) < 64:
        raise TrackError('TilesetFormat', 'tileset.png palette must hold at least 64 colours')
    tileset_path = directory / 'tileset.json'
    if not tileset_path.exists():
        raise TrackError('MissingFile', 'tileset.json is required')
    try:
        tileset = json.loads(tileset_path.read_text())
    except json.JSONDecodeError as error:
        raise TrackError('BadJson', f'tileset.json: {error}') from None
    if type(tileset) is not dict or type(tileset.get('materials')) is not list or \
            len(tileset['materials']) != 256:
        raise TrackError('MaterialName', 'tileset.json needs 256 materials')
    materials = bytes(_material(entry, index) for index, entry in enumerate(tileset['materials']))
    tiles = tileset_tiles(tile_pixels)
    colours = [png_indexed.rgb_to_bgr555(rgb) for rgb in tile_palette[:64]]
    for index in tileset.get('palette_bit15', []):
        colours[_int(index, 0, 63, 'TilesetFormat', 'palette_bit15 entry')] |= 0x8000
    road = base_data[8]
    template_tiles = road[24:24 + 0x4000]
    template_colours = list(struct.unpack_from('<64H', road, 24 + 0x4000))
    if tiles[SHARED_TILES.start * 64:] != template_tiles[SHARED_TILES.start * 64:]:
        raise TrackError('ReservedTileChanged', 'tiles 0xE0..0xFF are shared ROM art and must '
                         'match the template tileset')
    if [c & 0x7FFF for c in colours[48:]] != [c & 0x7FFF for c in template_colours[48:]]:
        raise TrackError('ReservedTileChanged', 'palette colours 48..63 are shared and must '
                         'match the template palette')
    used = set(tilemap)
    if custom:
        aliases = sorted(tile for tile in used if tile in ALIAS_TILES)
        if aliases:
            raise TrackError('ReservedTileUsed', f'tilemap uses object alias tiles '
                             f'{[hex(t) for t in aliases]} (0xD0..0xDF)')
        for tile in sorted(used):
            if materials[tile] not in MATERIAL_NAMES and not (
                    0x40 <= materials[tile] <= 0x5F or 0x80 <= materials[tile] <= 0x8F):
                raise TrackError('ForbiddenMaterial', f'tile {tile:#04x} uses undocumented '
                                 f'material {materials[tile]:#04x}')

    return _TerrainData(map_w, map_h, size, tilemap, tileset, materials,
                        tiles, colours, road)


def _read_route(definition, size):
    track, rules, base_data = definition.track, definition.rules, definition.base_data
    # Route, start and finish.
    if type(track['route']) is not list or len(track['route']) < 4:
        raise TrackError('RouteTooShort', 'route needs at least 4 points')
    if len(track['route']) > ROUTE_LIMIT:
        raise TrackError('RouteTooLong', f'route has {len(track["route"])} points (max {ROUTE_LIMIT})')
    route = []
    for index, entry in enumerate(track['route']):
        x, y = _point(entry, f'route[{index}]', size)
        if x % 8 or y % 8:
            raise TrackError('RouteNotAligned', f'route[{index}] ({x},{y}) must be multiples of 8')
        route.append([x, y, entry.get('mode')])
    for index, point in enumerate(route):
        point[2] = derive_mode(route, index) if point[2] is None else \
            _int(point[2], 0, 255, 'RouteFormat', f'route[{index}].mode')
    count = len(route)
    start_entry = track['start']
    start_x, start_y = _point(start_entry, 'start', size)
    start = (start_x, start_y, _int(start_entry.get('heading', 0), 0, 0xFFFF, 'RulesFormat', 'start.heading'))
    finish_y = _int(track['finish_y'], 0, size - 1, 'OutOfBounds', 'finish_y')
    if rules['progress'] == 'source':
        if start_y <= finish_y:
            raise TrackError('StartPastFinish', f'start.y {start_y} must lie south of finish_y {finish_y}')
        north, south = _finish_crossings(route, start_x, finish_y)
        if south:
            raise TrackError('SouthboundFinish', f'route segment {south[0]} crosses finish_y '
                             'southbound; laps count only northbound crossings')
        if north != [count - 2]:
            raise TrackError('FinishCrossing', 'the route must cross finish_y northbound exactly '
                             f'once near start.x, on its last segment {count - 2}->{count - 1} '
                             f'(found {north})')

    if 'ai_rows' in track:
        if type(track['ai_rows']) is not list or len(track['ai_rows']) != 2 or \
                any(type(row) is not list or len(row) != count for row in track['ai_rows']):
            raise TrackError('RouteFormat', f'ai_rows must be two lists of {count} points')
        ai_rows = []
        for row_index, row in enumerate(track['ai_rows']):
            points = []
            for index, entry in enumerate(row):
                x, y = _point(entry, f'ai_rows[{row_index}][{index}]', size)
                points.append((x, y, _int(entry.get('mode', route[index][2]), 0, 255,
                                          'RouteFormat', 'ai_rows mode')))
            ai_rows.append(points)
    else:
        ai_rows = derive_ai_rows(route)
    table = track.get('target_table')
    if table is None:
        table = list(parse_route(base_data[4])[2])
    if type(table) is not list or len(table) != 40 or any(v not in (0, 1, 2) for v in table):
        raise TrackError('RouteFormat', 'target_table must hold 40 row numbers 0..2')
    sentinel = track.get('ai_sentinels', {'mask': 0, 'points': [[0, 0]] * 3})
    try:
        mask = _int(sentinel['mask'], 0, 7, 'RouteFormat', 'ai_sentinels.mask')
        sentinel_points = [(_int(p[0], 0, 0xFFFFFFFF, 'RouteFormat', 'ai_sentinels point'),
                            _int(p[1], 0, 0xFFFFFFFF, 'RouteFormat', 'ai_sentinels point'))
                           for p in sentinel['points']]
    except (KeyError, TypeError, IndexError):
        raise TrackError('RouteFormat', 'ai_sentinels needs mask and three [x, y] points') from None
    if len(sentinel_points) != 3 or any(not mask >> row & 1 and point != (0, 0)
                                        for row, point in enumerate(sentinel_points)):
        raise TrackError('RouteFormat', 'ai_sentinels points must be zero where mask is clear')
    route_payload = words(0x31545552, count, 3, 40)
    for row in [route] + ai_rows:
        route_payload += b''.join(words(*point) for point in row)
    route_payload += bytes(table) + words(mask) + b''.join(words(*p) for p in sentinel_points)

    return _RouteData(route, start, finish_y, count, route_payload)


def _read_raster(directory, definition, terrain, route_data):
    map_w, tilemap, materials = terrain.width, terrain.tilemap, terrain.materials
    route, start, finish_y, count = (route_data.points, route_data.start,
                                   route_data.finish_y, route_data.count)
    start_x, start_y, _ = start
    rules = definition.rules
    # Raster.
    raster_w = raster_h = map_w // 2
    raster_image = _image(directory, 'raster.png', 'RasterFormat', (raster_w, raster_h),
                          indexed=False, required=False)
    if raster_image is None:
        raster = derive_raster(route, finish_y, raster_w, raster_h,
                               ground_cells(tilemap, map_w, materials))
    else:
        raster = raster_image[2]
        bad = sorted({v for v in raster if v & 0x7F != OFF_ROUTE and v & 0x7F >= count})
        if bad:
            raise TrackError('RasterValue', f'raster.png values {bad[:4]} exceed the {count} route points')
    if rules['progress'] == 'source':
        cell = raster[(start_y // RASTER_CELL) * raster_w + start_x // RASTER_CELL]
        if cell != (0x80 | count - 1):
            raise TrackError('StartOutsideRaster', f'raster cell at start is {cell:#04x}, expected '
                             f'{0x80 | count - 1:#04x} (the last, finish-flagged region)')

    return raster


def _compile_objects(track, size):
    # Objects.
    boxes, coins = track.get('item_boxes', []), track.get('coins', [])
    for name, items in (('item_boxes', boxes), ('coins', coins)):
        if type(items) is not list or len(items) > OBJECT_LIMIT:
            raise TrackError('TooManyObjects', f'{name} holds more than {OBJECT_LIMIT} objects')
    box_payload = words(0x314A424F, len(boxes))
    coin_payload = words(0x314E4943, len(coins))
    for name, items in (('item_boxes', boxes), ('coins', coins)):
        for index, entry in enumerate(items):
            x, y = _point(entry, f'{name}[{index}]', size)
            if x % 8 or y % 8:
                raise TrackError('ObjectNotAligned', f'{name}[{index}] must sit on multiples of 8')
            parameter = _int(entry.get('param', 0), 0, 255, 'ObjectFormat', f'{name}[{index}].param')
            if name == 'item_boxes':
                box_payload += struct.pack('<IIHBB', x << 16, y << 16, 0x8800, parameter, 0x1E)
            else:
                coin_payload += bytes((0x80, x // 8, y // 8, parameter))

    return box_payload, coin_payload


def _compile_grid(track, size, start):
    if 'grid' in track:
        grid_entries = track['grid']
        if type(grid_entries) is not list or len(grid_entries) != 8:
            raise TrackError('GridFormat', 'grid must list 8 start positions')
        grid = [(*_point(entry, f'grid[{index}]', size),
                 _int(entry.get('heading', 0), 0, 0xFFFF, 'GridFormat', 'grid heading'))
                for index, entry in enumerate(grid_entries)]
    else:
        grid = derive_grid(start)
    grid_payload = words(0x31475047, 8) + b''.join(
        words(x << 16, y << 16, heading) for x, y, heading in grid)

    return grid_payload


def _read_presentation(directory, definition, terrain):
    track, track_id, base_data = definition.track, definition.track_id, definition.base_data
    map_w, tilemap, materials = terrain.width, terrain.tilemap, terrain.materials
    # Presentation.
    minimap_image = _image(directory, 'minimap.png', 'MinimapFormat', (64, 64), required=False)
    minimap = derive_minimap(tilemap, map_w, materials) if minimap_image is None else minimap_image[2]
    if max(minimap) > 15:
        raise TrackError('MinimapFormat', 'minimap.png pixels must use palette indices 0..15')
    gp_hud = bytearray(base_data[33])
    gp_hud[4:8] = words(track_id)
    gp_hud[MINIMAP_OFFSET:MINIMAP_OFFSET + 0x800] = planar_tiles(minimap, 8, 8)
    standings = bytearray(base_data[23])
    standings[24:28] = words(track_id)
    name_image = _image(directory, 'name.png', 'NameFormat', NAME_SIZE, required=False)
    if name_image is None:
        template_name = planar_image(standings[NAME_TILE_OFFSET:NAME_TILE_OFFSET + 24 * 32], 12, 2)
        name_pixels = derive_name(track['name'], template_name)
    else:
        name_pixels = name_image[2]
    if max(name_pixels) > 15:
        raise TrackError('NameFormat', 'name.png pixels must use palette indices 0..15')
    standings[NAME_TILE_OFFSET:NAME_TILE_OFFSET + 24 * 32] = planar_tiles(name_pixels, 12, 2)
    sky_path = directory / 'sky.bin'
    sky = sky_path.read_bytes() if sky_path.exists() else base_data[9]
    sky = compile_sky(sky, directory)

    return _PresentationData(gp_hud, standings, name_pixels, sky)


def _construct_payloads(definition, terrain, route_data, raster, presentation,
                        box_payload, coin_payload, grid_payload):
    track_id, template, rules = definition.track_id, definition.template, definition.rules
    rules_version, threshold = definition.rules_version, definition.threshold
    base_data, base_version = definition.base_data, definition.base_version
    start, finish_y, count = route_data.start, route_data.finish_y, route_data.count
    route_payload = route_data.payload
    map_w, map_h = terrain.width, terrain.height
    raster_w = raster_h = map_w // 2
    tilemap, materials, tiles = terrain.tilemap, terrain.materials, terrain.tiles
    tileset, colours, road = terrain.tileset, terrain.colours, terrain.road
    gp_hud, standings, sky = presentation.gp_hud, presentation.standings, presentation.sky
    # Keep tile flags validation after presentation reads, as in the folder format.
    revision = 3 if rules['progress'] == 'source' else 1
    manifest = bytearray(base_data[1])
    fields = list(u32s(manifest, 0, len(manifest) // 4))
    fields[1], fields[2] = track_id, revision
    fields[3:10] = [track_id if value == template else value for value in fields[3:10]]
    manifest = words(*fields)
    material_payload = bytearray(base_data[6])
    material_payload[8:264] = materials
    tile_flags = _int(tileset.get('flags', u32s(road, 12, 1)[0]), 0, 255, 'TilesetFormat', 'flags')
    tile_blocks = _int(tileset.get('block_count', u32s(road, 16, 1)[0]), 0, 255, 'TilesetFormat', 'block_count')
    road_payload = (words(0x31444152, 0x4000, 64, tile_flags, tile_blocks, 0) + tiles +
                    struct.pack('<64H', *colours))
    world = (words(0x31444C57, raster_w, raster_h, map_w, map_h, len(raster), len(tilemap)) +
             bytes(raster) + bytes(tilemap))
    payloads = {
        1: (manifest, base_version[1]),
        3: (_rules_payload(track_id, revision, revision, start, rules, finish_y, count,
                           rules_version, threshold), rules_version),
        4: (route_payload, 4), 5: (world, base_version[5]),
        6: (bytes(material_payload), base_version[6]), 8: (road_payload, base_version[8]),
        9: (sky, 2 if u32s(sky, 60, 1)[0] else 1), 11: (base_data[11], base_version[11]),
        23: (bytes(standings), base_version[23]), 25: (box_payload, base_version[25]),
        27: (coin_payload, base_version[27]), 33: (bytes(gp_hud), base_version[33]),
        34: (grid_payload, base_version[34]), 70: (base_data[70], base_version[70]),
    }
    return payloads


def _append_optional_payloads(directory, definition, sections, payloads, name_pixels):
    track, template = definition.track, definition.template
    # Ambient behavior follows the template unless the folder supplies its own
    # raw section. Missing files therefore remain compatible with old packs.
    ambient_path = directory / 'ambient.bin'
    if ambient_path.is_file():
        payloads[84] = (ambient_path.read_bytes(), 1)
    elif (84, template) in sections:
        header, ambient = sections[(84, template)]
        payloads[84] = (ambient, header['version'])
    passthrough = track.get('passthrough', {})
    if type(passthrough) is not dict:
        raise TrackError('PassthroughKind', 'passthrough must map kinds to files')
    for key, entry in passthrough.items():
        if not key.isdigit() or int(key) not in PASSTHROUGH:
            raise TrackError('PassthroughKind', f'kind {key} cannot pass through '
                             f'(allowed: {sorted(PASSTHROUGH)})')
        kind = int(key)
        name, version = (entry, PASSTHROUGH[kind][1]) if type(entry) is str else (
            entry.get('file'), entry.get('version', PASSTHROUGH[kind][1])) if type(entry) is dict else (None, None)
        if type(name) is not str or pathlib.PurePath(name).name != name or not (directory / name).is_file():
            raise TrackError('MissingFile', f'passthrough {key} file {name!r} is missing')
        payloads[kind] = ((directory / name).read_bytes(), _int(version, 1, 255, 'PassthroughKind', 'version'))
    # GP opening follows the template even when its optional export is omitted.
    if 36 not in payloads and (36, template) in sections:
        header, opening = sections[(36, template)]
        payloads[36] = (opening, header['version'])
    if 93 not in payloads and (93, template) in sections:
        header, records = sections[(93, template)]
        payloads[93] = (records, header['version'])
    if 93 in payloads:
        records, version = payloads[93]
        if version != 1 or len(records) != 32 or u32s(records, 0, 2) != (0x31525454, 6):
            raise TrackError('RecordsFormat', 'records.bin must contain TTR1 version1')
        previous = 0
        for row in range(6):
            driver, reserved, time = struct.unpack_from('<BBH', records, 8+row*4)
            if driver > 7 or reserved or not 1 <= time <= 59999 or (row < 5 and time < previous):
                raise TrackError('RecordsFormat', 'invalid Time Trial record target')
            if row < 5:
                previous = time
    if 36 in payloads and payloads[36][1] == 27:
        opening = bytearray(payloads[36][0])
        if len(opening) != 5224 or u32s(opening, 0, 2) != (0x31544f47, 0xa00):
            raise TrackError('OpeningFormat', 'opening.bin must contain GOT1 version27')
        opening[8+0xe0:8+0x3e0] = planar_tiles(name_pixels, 12, 2)
        payloads[36] = (bytes(opening), 27)


def _section_records(sections, template, track_id, payloads):
    result = []
    for kind, (data, version) in sorted(payloads.items()):
        header = sections.get((kind, template), ({'flags': 3, 'semantic_revision': 1}, None))[0]
        result.append({'kind': kind, 'content_id': track_id, 'version': version,
                       'flags': header.get('flags', 3),
                       'semantic_revision': header.get('semantic_revision', 1), 'data': data})
    return result


def compile_track(directory, base):
    """Return section dicts {kind, content_id, version, flags, semantic_revision, data}."""
    directory = pathlib.Path(directory)
    sections = _base_sections(base)
    definition = _read_track_definition(directory, sections)
    terrain = _read_terrain(directory, definition)
    route = _read_route(definition, terrain.size)
    raster = _read_raster(directory, definition, terrain, route)
    boxes, coins = _compile_objects(definition.track, terrain.size)
    grid = _compile_grid(definition.track, terrain.size, route.start)
    presentation = _read_presentation(directory, definition, terrain)
    payloads = _construct_payloads(definition, terrain, route, raster, presentation,
                                   boxes, coins, grid)
    _append_optional_payloads(directory, definition, sections, payloads,
                              presentation.name_pixels)
    return _section_records(sections, definition.template, definition.track_id, payloads)


def write_sections(sections, output):
    """Write *.bin plus a build_asset_pack manifest.json into output."""
    output = pathlib.Path(output)
    output.mkdir(parents=True, exist_ok=True)
    manifest = []
    for section in sections:
        name = f'{section["kind"]}-{section["content_id"]}.bin'
        (output / name).write_bytes(section['data'])
        manifest.append({key: section[key] for key in
                         ('kind', 'content_id', 'version', 'flags', 'semantic_revision')} |
                        {'path': name})
    (output / 'manifest.json').write_text(json.dumps({'sections': manifest}, indent=1) + '\n')
    return output / 'manifest.json'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    export = commands.add_parser('export', help='write a track folder for one course')
    export.add_argument('pack', type=pathlib.Path)
    export.add_argument('course_id', type=int)
    export.add_argument('directory', type=pathlib.Path)
    build = commands.add_parser('compile', help='compile a track folder into sections')
    build.add_argument('directory', type=pathlib.Path)
    build.add_argument('--base', type=pathlib.Path, required=True)
    build.add_argument('--output', type=pathlib.Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == 'export':
            track = export_course(args.pack, args.course_id, args.directory)
            print(f'exported course {track["id"]} ({len(track["route"])} route points) to {args.directory}')
        else:
            sections = compile_track(args.directory, args.base)
            manifest = write_sections(sections, args.output)
            print(f'compiled {len(sections)} sections for course {sections[0]["content_id"]} into {manifest}')
    except (OSError, ValueError) as error:
        print(f'track_format: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
