#!/usr/bin/env python3
"""Paint four custom tracks from centre-line polylines and pack them as a new cup.

    python3 build.py BASE.pack --output-dir DIR/mods/custom-cup

Each courses/*.json lists a closed control polygon in world units (8 per
tile), a stock template course and the start column/finish line.
The generator rounds the polygon's corners with arcs, paints road (+-road_half_width), a wall
band and offroad onto a 256x256 tilemap with the template's most-used tiles,
writes a track folder (tracks/<name>/) and compiles it with track_format.
It then emits catalog 4 (custom cup 0, course ids 128..131), course preview
and name art, and the cup icon and name. Only this script and the polylines
ship; everything it writes is derived from the owner's pack and stays local.
"""
import argparse
import collections
import json
import math
from pathlib import Path
import shutil
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/portable'))
from mod_kit import default_mods_directory
import build_asset_pack  # noqa: E402
from mod_kit import text_image  # noqa: E402
import png_indexed  # noqa: E402
import track_format  # noqa: E402
from text_data_mod import _GLYPHS  # noqa: E402
from ui_rom import image_asset  # noqa: E402

HERE = Path(__file__).resolve().parent
COURSES = ('oval', 'kidney', 'hairpin', 'chicane')   # cup order
CUP = 0                       # custom cup 0 = course_cup 11
COURSE_CUP = 11 + CUP
CATALOG_ID = 4
MAP_TILES = 256
WORLD = MAP_TILES * 8
SPACING = 72                  # route point spacing (world units)
START_LEAD = 48               # last-but-one route point lies this far south of finish_y
START_GAP = 20                # start.y = finish_y + 20, like every stock course
MIN_SEPARATION = 136          # centre lines of different track parts stay this far apart
MIN_RADIUS = 96
PREVIEW, NAME, SELECTED = 0x23300, 0x23400, 0x23500
CUP_ICON, CUP_NAME = 0x23100 + 15 + CUP, 0x23200 + 15 + CUP
BOOST_TILES = (0xE0, 0xE1, 0xE2, 0xE3)   # shared north-pointing chevrons
DEFAULT_SURFACES = {'road': ['road'], 'offroad': ['offroad', '0x54'], 'wall': ['wall']}
ROAD, SHOULDER, WALL, OUTSIDE = 0, 1, 2, 3
COURSE_KEYS = {'id', 'name', 'template', 'laps', 'song', 'start_x', 'finish_y', 'points',
               'road_half_width', 'corner_radius', 'surfaces', 'road_material', 'item_boxes', 'coins', 'boosts', 'water_colors', 'sky_tint'}


class CourseError(ValueError):
    pass


# ------------------------------------------------------------- geometry --

def fillet(points, radius):
    """Round each corner with a circular arc; straights stay exactly straight.

    A point may carry its own radius as a third value. Arcs shrink to fit
    half of each neighbouring edge (check_geometry then rejects tight ones).
    """
    result = []
    count = len(points)
    for index, point in enumerate(points):
        (px, py), (ax, ay), (bx, by) = point[:2], points[index - 1][:2], points[(index + 1) % count][:2]
        first, second = math.hypot(px - ax, py - ay), math.hypot(bx - px, by - py)
        if not first or not second:
            raise CourseError(f'repeated point ({px},{py})')
        u1, u2 = ((px - ax) / first, (py - ay) / first), ((bx - px) / second, (by - py) / second)
        turn = math.atan2(u1[0] * u2[1] - u1[1] * u2[0], u1[0] * u2[0] + u1[1] * u2[1])
        if abs(turn) < 1e-3:
            result.append((float(px), float(py)))
            continue
        tangent = min((point[2] if len(point) > 2 else radius) * math.tan(abs(turn) / 2),
                      first / 2, second / 2)
        arc_radius = tangent / math.tan(abs(turn) / 2)
        side = 1 if turn > 0 else -1
        start = (px - u1[0] * tangent, py - u1[1] * tangent)
        centre = (start[0] - u1[1] * side * arc_radius, start[1] + u1[0] * side * arc_radius)
        angle = math.atan2(start[1] - centre[1], start[0] - centre[0])
        steps = max(2, int(abs(turn) * arc_radius / 6))
        for step in range(steps + 1):
            a = angle + turn * step / steps
            result.append((centre[0] + arc_radius * math.cos(a), centre[1] + arc_radius * math.sin(a)))
    return result


class Curve:
    """A closed polyline parameterised by arc length."""

    def __init__(self, points):
        self.points = points
        self.lengths = [0.0]
        for index, (ax, ay) in enumerate(points):
            bx, by = points[(index + 1) % len(points)]
            self.lengths.append(self.lengths[-1] + math.hypot(bx - ax, by - ay))
        self.total = self.lengths[-1]

    def at(self, s):
        """(x, y, unit dx, unit dy) at arc length s."""
        s %= self.total
        low, high = 0, len(self.points)
        while high - low > 1:
            middle = (low + high) // 2
            if self.lengths[middle] <= s:
                low = middle
            else:
                high = middle
        (ax, ay), (bx, by) = self.points[low], self.points[(low + 1) % len(self.points)]
        length = self.lengths[low + 1] - self.lengths[low] or 1.0
        t = (s - self.lengths[low]) / length
        return ax + (bx - ax) * t, ay + (by - ay) * t, (bx - ax) / length, (by - ay) / length

    def samples(self, step, origin=0.0):
        count = max(4, round(self.total / step))
        return [self.at(origin + index * self.total / count) for index in range(count)]

    def finish_origin(self, start_x, y):
        """Arc length where the curve passes (start_x, y) heading north."""
        for index, (ax, ay) in enumerate(self.points):
            bx, by = self.points[(index + 1) % len(self.points)]
            if abs(ax - start_x) < 0.01 and abs(bx - start_x) < 0.01 and by < y <= ay:
                return self.lengths[index] + (ay - y)
        raise CourseError(f'the smoothed line never heads north along x={start_x} through y={y}; '
                          'give the start straight more length')


def check_geometry(curve, start_x, finish_y, half_width):
    probe = curve.samples(16)
    step = curve.total / len(probe)
    # Straight start: the grid sits up to finish_y + 200 behind the line.
    origin = curve.finish_origin(start_x, finish_y + START_LEAD)
    for offset in range(-80, 240, 8):
        x, _, dx, dy = curve.at(origin + START_LEAD - offset)
        if abs(x - start_x) > 0.5 or dy > -0.99:
            raise CourseError(f'start straight must run north on x={start_x} from finish_y+240 to '
                              'finish_y-80')
    for index in range(len(probe)):
        (ax, ay, adx, ady), (bx, by, bdx, bdy) = probe[index], probe[(index + 2) % len(probe)]
        turn = abs(math.atan2(adx * bdy - ady * bdx, adx * bdx + ady * bdy))
        if turn > 1e-6 and 2 * step / turn < MIN_RADIUS:
            raise CourseError(f'curve radius {2 * step / turn:.0f} near ({ax:.0f},{ay:.0f}) is below '
                              f'{MIN_RADIUS}')
    window = int(4 * MIN_SEPARATION / step) + 1
    for i, (ax, ay, _, _) in enumerate(probe):
        for j in range(i + window, len(probe) - max(0, window - i)):
            bx, by = probe[j][:2]
            if (ax - bx) ** 2 + (ay - by) ** 2 < MIN_SEPARATION ** 2:
                raise CourseError(f'track parts near ({ax:.0f},{ay:.0f}) and ({bx:.0f},{by:.0f}) are '
                                  f'closer than {MIN_SEPARATION}')
    margin = half_width + 64
    if any(not margin <= v <= WORLD - margin for x, y, _, _ in probe for v in (x, y)):
        raise CourseError(f'track must stay {margin} units inside the {WORLD}-unit map')


def snap(value):
    return int(round(value / 8)) * 8


def distance_field(curve):
    """Per tile: distance from its centre to the centre line (inf when far)."""
    field = [math.inf] * (MAP_TILES * MAP_TILES)
    reach = 80
    dense = curve.samples(6)
    for index, (ax, ay, _, _) in enumerate(dense):
        bx, by = dense[(index + 1) % len(dense)][:2]
        dx, dy = bx - ax, by - ay
        length = dx * dx + dy * dy or 1.0
        for ty in range(max(0, int((min(ay, by) - reach) // 8)),
                        min(MAP_TILES, int((max(ay, by) + reach) // 8) + 1)):
            py = ty * 8 + 4
            for tx in range(max(0, int((min(ax, bx) - reach) // 8)),
                            min(MAP_TILES, int((max(ax, bx) + reach) // 8) + 1)):
                px = tx * 8 + 4
                t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / length))
                distance = math.hypot(px - ax - t * dx, py - ay - t * dy)
                cell = ty * MAP_TILES + tx
                if distance < field[cell]:
                    field[cell] = distance
    return field


# ------------------------------------------------------------- template --

def surface_material(names, used_materials, kind):
    for name in names:
        value = track_format._material(name, kind)
        if value in used_materials:
            return value
    raise CourseError(f'template uses none of the {kind} materials {names}')


def block_pattern(tilemap, width, materials, material, allow_shared):
    """Most common 2x2 block of one material, else its most-used tile."""
    def usable(tile):
        return (materials[tile] == material and tile not in track_format.ALIAS_TILES and
                (allow_shared or tile not in track_format.SHARED_TILES))
    singles = collections.Counter(t for t in tilemap if usable(t))
    if not singles:
        raise CourseError(f'template has no usable tile with material {material:#04x}')
    blocks = collections.Counter()
    for y in range(0, width - 1):
        for x in range(0, width - 1):
            block = (tilemap[y * width + x], tilemap[y * width + x + 1],
                     tilemap[(y + 1) * width + x], tilemap[(y + 1) * width + x + 1])
            if all(usable(t) for t in block) and len(set(block)) > 1:
                blocks[(block, x & 1, y & 1)] += 1
    top_tile, top_count = singles.most_common(1)[0]
    if blocks:
        (block, px, py), count = blocks.most_common(1)[0]
        if count * 4 >= top_count:
            return lambda x, y: block[((y - py) & 1) * 2 + ((x - px) & 1)]
    if not allow_shared:
        return lambda x, y: top_tile
    tiles = [tile for tile, count in singles.most_common(4) if count * 4 >= top_count]
    return lambda x, y: tiles[(x + 2 * y) % len(tiles)]


def finish_tile(template_dir, template_track, tilemap, width, materials, road_tile):
    start_x, finish_y = template_track['start']['x'], template_track['finish_y']
    counts = collections.Counter(
        tilemap[row * width + x] for row in (finish_y // 8 - 1, finish_y // 8)
        for x in range((start_x - 32) // 8, (start_x + 32) // 8 + 1)
        if 0 <= row < width and 0 <= x < width)
    for tile, _ in counts.most_common():
        if tile != road_tile and 0x40 <= materials[tile] < 0x60 and \
                tile not in track_format.ALIAS_TILES:
            return tile
    raise CourseError(f'{template_dir}: no finish-line tile beside the template start')


def tile_colour(tiles, palette, tile):
    pixels = [tiles[((tile // 16) * 8 + y) * 128 + (tile % 16) * 8 + x] for y in range(8) for x in range(8)]
    return tuple(sum(palette[p][c] for p in pixels) // 64 for c in range(3))


# ---------------------------------------------------------------- course --

def check_rgb(value):
    if not isinstance(value, list) or len(value) != 3 or any(
            type(channel) is not int or not 0 <= channel <= 255 for channel in value):
        raise CourseError('environment colors must be RGB triples in 0..255')


def load_course(path):
    course = json.loads(Path(path).read_text())
    unknown = set(course) - COURSE_KEYS
    if unknown:
        raise CourseError(f'{path}: unknown fields {sorted(unknown)}')
    if course['id'] not in track_format.CUSTOM_IDS:
        raise CourseError(f'{path}: id must be 128..191')
    if course['start_x'] % 8 or course['finish_y'] % 8:
        raise CourseError(f'{path}: start_x and finish_y must be multiples of 8')
    return course


def paint_surface_map(course, curve, patterns, materials, finish, half,
                      wall_inner, wall_outer, start_x, finish_y):
    """Paint terrain and the finish stripe before placing course objects."""
    field = distance_field(curve)
    classes = bytearray(MAP_TILES * MAP_TILES)
    tilemap = bytearray(MAP_TILES * MAP_TILES)
    for index, distance in enumerate(field):
        y, x = divmod(index, MAP_TILES)
        if distance < half:
            classes[index], tilemap[index] = ROAD, patterns['road'](x, y)
        elif distance < wall_inner:
            classes[index], tilemap[index] = SHOULDER, patterns['offroad'](x, y)
        elif distance < wall_outer:
            classes[index], tilemap[index] = WALL, patterns['wall'](x, y)
        else:
            classes[index], tilemap[index] = OUTSIDE, patterns['offroad'](x, y)
    if 'road_material' in course:
        # Drive the painted road tiles like stock asphalt (or another named surface).
        for tile in {tilemap[i] for i in range(len(classes)) if classes[i] == ROAD}:
            materials[tile] = track_format._material(course['road_material'], tile)
    row = finish_y // 8 - 1
    for x in range(MAP_TILES):
        if classes[row * MAP_TILES + x] == ROAD and abs(x * 8 + 4 - start_x) < half + 8:
            tilemap[row * MAP_TILES + x] = finish

    return classes, tilemap


def place_course_objects(course, curve, origin, route, tilemap, materials):
    """Keep boost, item-box and coin authoring in route order."""
    def along(fraction):
        return curve.at(origin + START_LEAD + fraction * curve.total)

    def route_index(x, y):
        return min(range(len(route)), key=lambda i: (route[i][0] - x) ** 2 + (route[i][1] - y) ** 2)

    for fraction in course.get('boosts', []):
        x, y, dx, dy = along(fraction)
        if dy > -0.98:
            raise CourseError(f'{course["name"]}: boost at {fraction} must sit on a northbound straight')
        tx, ty = int(x) // 8 - 2, int(y) // 8
        for j in range(2):
            for i, tile in enumerate(BOOST_TILES):
                tilemap[(ty + j) * MAP_TILES + tx + i] = tile
        for tile in BOOST_TILES:
            materials[tile] = track_format.MATERIALS['boost']
    boxes, coins = [], []
    for fraction in course.get('item_boxes', []):
        x, y, dx, dy = along(fraction)
        param = route_index(x, y)
        boxes += [{'x': snap(x - dy * o), 'y': snap(y + dx * o), 'param': param} for o in (-24, -8, 8, 24)]
    for number, fraction in enumerate(course.get('coins', [])):
        side = 16 if number & 1 else -16
        for step in (-16, 0, 16):
            x, y, dx, dy = along(fraction + step / curve.total)
            coins.append({'x': snap(x - dy * side), 'y': snap(y + dx * side), 'param': route_index(x, y)})

    return boxes, coins


def paint_water_landscape(course, folder, materials, tilemap, classes,
                          tile_pixels, palette):
    if 'water_colors' in course:
        # A new tile outside the guard wall uses genuine water material. Palette
        # entries are shared with terrain, just as when editing tileset.png.
        free = next((tile for tile in range(0xD0) if tile not in tilemap), None)
        if free is None:
            raise CourseError('no unused local tile for the water landscape')
        colors = course['water_colors']
        if not isinstance(colors, list) or len(colors) != 2:
            raise CourseError('water_colors must contain two RGB colors')
        check_rgb(colors[0]); check_rgb(colors[1])
        indices = sorted(range(1, 48), key=lambda i: sum(
            (palette[i][c] - colors[0][c]) ** 2 for c in range(3)))[:2]
        palette = list(palette)
        for index, color in zip(indices, colors):
            palette[index] = tuple(color)
        tile_pixels = bytearray(tile_pixels)
        for y in range(8):
            for x in range(8):
                tile_pixels[(free // 16 * 8 + y) * 128 + free % 16 * 8 + x] = indices[(y // 2 + x // 4) & 1]
        materials[free] = track_format.MATERIALS['water']
        for index, kind in enumerate(classes):
            if kind == OUTSIDE:
                tilemap[index] = free
        png_indexed.write(folder / 'tileset.png', 128, 128, tile_pixels, palette)
    return tile_pixels, palette


def paint_course(course, sections, folder, work):
    """Write a complete track folder; return (track, classes, colours)."""
    template = course['template']
    template_dir = work / f'template-{template}'
    if not template_dir.exists():
        track_format.export_course(sections, template, template_dir)
    template_track = json.loads((template_dir / 'track.json').read_text())
    tileset = json.loads((template_dir / 'tileset.json').read_text())
    materials = [track_format._material(entry, index) for index, entry in enumerate(tileset['materials'])]
    width, _, template_map, _ = png_indexed.read(template_dir / 'tilemap.png')
    _, _, tile_pixels, palette = png_indexed.read(template_dir / 'tileset.png')
    local = {materials[tile] for tile in template_map if tile < track_format.ALIAS_TILES.start}
    shared = {materials[tile] for tile in template_map}
    surfaces = dict(DEFAULT_SURFACES, **course.get('surfaces', {}))
    patterns = {}
    for name in ('road', 'offroad', 'wall'):
        material = surface_material(surfaces[name], shared if name == 'wall' else local, name)
        patterns[name] = block_pattern(template_map, width, materials, material, name == 'wall')
    finish = finish_tile(template_dir, template_track, template_map, width, materials,
                         patterns['road'](0, 0))

    half = course.get('road_half_width', 40)
    wall_inner, wall_outer = half + 4, half + 16
    start_x, finish_y = course['start_x'], course['finish_y']
    curve = Curve(fillet(course['points'], course.get('corner_radius', 192)))
    check_geometry(curve, start_x, finish_y, half)
    origin = curve.finish_origin(start_x, finish_y + START_LEAD)
    samples = curve.samples(SPACING, origin)
    points = [(snap(x), snap(y)) for x, y, _, _ in samples]
    route = points[2:] + points[:2]        # finish crossing is the last segment
    if len(route) > track_format.ROUTE_LIMIT:
        raise CourseError(f'{course["name"]}: {len(route)} route points (max {track_format.ROUTE_LIMIT})')

    classes, tilemap = paint_surface_map(
        course, curve, patterns, materials, finish, half,
        wall_inner, wall_outer, start_x, finish_y)

    boxes, coins = place_course_objects(course, curve, origin, route, tilemap, materials)

    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    for name in ('tileset.png', 'sky.bin', 'sky-tiles.png', 'sky-palette.png', 'sky-motion.json', 'ambient.bin'):
        if (template_dir / name).exists():
            shutil.copyfile(template_dir / name, folder / name)
    tile_pixels, palette = paint_water_landscape(
        course, folder, materials, tilemap, classes, tile_pixels, palette)
    if 'sky_tint' in course:
        tint = course['sky_tint']
        check_rgb(tint)
        width, height, pixels, sky_palette = png_indexed.read(folder / 'sky-palette.png')
        sky_palette = [tuple((color[c] + tint[c]) // 2 for c in range(3)) for color in sky_palette]
        png_indexed.write(folder / 'sky-palette.png', width, height, pixels, sky_palette)
    tileset['materials'] = [track_format.material_label(value) for value in materials]
    (folder / 'tileset.json').write_text(json.dumps(tileset, indent=1) + '\n')
    png_indexed.write(folder / 'tilemap.png', MAP_TILES, MAP_TILES, bytes(tilemap))
    track = {
        'format': track_format.FORMAT, 'id': course['id'], 'name': course['name'],
        'laps': course.get('laps', 3), 'song': course.get('song', template_track['song']),
        'template_course': template,
        'start': {'x': start_x, 'y': finish_y + START_GAP, 'heading': 0},
        'finish_y': finish_y,
        'route': [{'x': x, 'y': y} for x, y in route],
        'item_boxes': boxes, 'coins': coins,
    }
    (folder / 'track.json').write_text(json.dumps(track, indent=1) + '\n')
    colours = {name: tile_colour(tile_pixels, palette, patterns[name](0, 0))
               for name in ('road', 'offroad', 'wall')}
    return track, classes, colours


# ------------------------------------------------------------------ art --

def argb(rgb):
    return 0xFF000000 | rgb[0] << 16 | rgb[1] << 8 | rgb[2]


def preview_image(classes, colours, course):
    """80x64 picture of the painted map: 4x4 tiles per pixel, finish marked."""
    pixels = [0xFF101010] * (80 * 64)
    wall, road, offroad = argb(colours['wall']), argb(colours['road']), argb(colours['offroad'])
    for y in range(64):
        for x in range(64):
            counts = collections.Counter(classes[(y * 4 + j) * MAP_TILES + x * 4 + i]
                                         for j in range(4) for i in range(4))
            colour = road if counts[ROAD] >= 5 else wall if counts[WALL] >= 4 else offroad
            pixels[y * 80 + x + 8] = colour
    fx, fy = course['start_x'] // 32 + 8, course['finish_y'] // 32
    for dx in range(-2, 2):
        pixels[fy * 80 + fx + dx] = 0xFFFFFFFF if dx & 1 else 0xFF000000
    for i in range(80):
        pixels[i] = pixels[63 * 80 + i] = 0xFF000000
    for j in range(64):
        pixels[j * 80] = pixels[j * 80 + 79] = 0xFF000000
    return image_asset(80, 64, pixels)


def cup_icon():
    pixels = [0] * (32 * 32)
    for y in range(32):
        for x in range(32):
            radius = math.hypot(x - 15.5, y - 15.5)
            if radius < 14:
                shade = int(255 - radius * 8)
                pixels[y * 32 + x] = 0xFF000000 | 0x20 << 16 | shade << 8 | 0xFF
            elif radius < 15.5:
                pixels[y * 32 + x] = 0xFF101040
    glyph = _GLYPHS['N']
    for row in range(5):
        for column in range(3):
            if glyph & 1 << (14 - row * 3 - column):
                for dy in range(4):
                    for dx in range(4):
                        pixels[(6 + row * 4 + dy) * 32 + 10 + column * 4 + dx] = 0xFFFFFFFF
    return image_asset(32, 32, pixels)


# ----------------------------------------------------------------- build --

def build(base, output, courses_dir=HERE / 'courses', work=None):
    output = Path(output)
    sections = track_format.load_pack(base) if isinstance(base, (str, Path)) else base
    courses = [load_course(Path(courses_dir) / f'{name}.json') for name in COURSES]
    ids = [course['id'] for course in courses]
    if len(set(ids)) != len(ids):
        raise CourseError('course ids must be unique')
    taken = {struct.unpack_from('<I', data, 8 + row * 12)[0]
             for (kind, _), (_, data) in sections.items()
             if kind == 37 and data[:4] == b'CAT1'
             for row in range(struct.unpack_from('<I', data, 4)[0])}
    if taken & set(ids):
        raise CourseError(f'course ids {sorted(taken & set(ids))} already appear in a base catalog')
    payloads = []
    with tempfile.TemporaryDirectory() as temporary:
        work = Path(work or temporary)
        catalog = struct.pack('<2I', 0x31544143, len(courses))
        for track_number, (name, course) in enumerate(zip(COURSES, courses)):
            folder = output / 'tracks' / name
            track, classes, colours = paint_course(course, sections, folder, work)
            for section in track_format.compile_track(folder, sections):
                payloads.append((section, section['data']))
            catalog += struct.pack('<IBBHI', course['id'], CUP, track_number, track['song'], track_number)
            slot = COURSE_CUP * 4 + track_number
            art = ((PREVIEW, preview_image(classes, colours, course)),
                   (NAME, text_image(course['name'], 96, 16, 0xFFFF9400, 0xFFEF3900, 0xFF6B0094)),
                   (SELECTED, text_image(course['name'], 96, 16, 0xFFFFEF94, 0xFFFFAD00, 0xFF945ABD)))
            for base_id, data in art:
                payloads.append(({'kind': 15, 'content_id': base_id + slot, 'version': 1}, data))
    payloads.append(({'kind': 37, 'content_id': CATALOG_ID, 'version': 2}, catalog))
    payloads.append(({'kind': 15, 'content_id': CUP_ICON, 'version': 1}, cup_icon()))
    payloads.append(({'kind': 15, 'content_id': CUP_NAME, 'version': 1},
                     text_image('NEO CUP', 64, 16, 0xFFFF9400, 0xFFEF3900, 0xFF6B0094)))
    output.mkdir(parents=True, exist_ok=True)
    entries = []
    for meta, data in payloads:
        name = f'{meta["kind"]}-{meta["content_id"]}.bin'
        (output / name).write_bytes(data)
        entries.append({'kind': meta['kind'], 'content_id': meta['content_id'],
                        'version': meta['version'], 'flags': meta.get('flags', 3),
                        'semantic_revision': meta.get('semantic_revision', 1), 'path': name})
    (output / 'pack.json').write_text(json.dumps({'sections': entries}, indent=1) + '\n')
    (output / 'mod.json').write_bytes((HERE / 'mod.json').read_bytes())
    build_asset_pack.build_pack(output / 'pack.json', output / 'content.pack')
    return entries


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('base', type=Path, help='owner pack with the template courses')
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args(argv)
    if args.output_dir is None:
        args.output_dir = default_mods_directory() / 'custom-cup'
    try:
        entries = build(args.base, args.output_dir)
    except (OSError, ValueError) as error:
        print(f'custom-cup: {error}', file=sys.stderr)
        return 1
    print(f'Built custom-cup: {len(entries)} sections in {args.output_dir}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
