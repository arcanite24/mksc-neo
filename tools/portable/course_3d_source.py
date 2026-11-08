"""Immutable, ROM-independent 2D content reader for course conversion.

Coordinates retain source units (eight per terrain tile); this module does not
choose metre scaling, regenerate checkpoint rasters, or mutate overlay inputs.
"""
from dataclasses import dataclass
from collections.abc import Mapping
import struct
from typing import Optional

import track_format


class SourceError(ValueError):
    """A conversion input error naming the course and source section."""


@dataclass(frozen=True)
class SourceRules:
    revision: int
    schema: int
    progress: int
    finish: int
    laps: int
    start_x_q16: int
    start_y_q16: int
    heading: int
    finish_y_q16: int
    start_index: int
    start_checkpoint_raw: int
    countdown_ticks: int
    rescue_ticks: int
    item_strength_threshold: Optional[int]


@dataclass(frozen=True)
class SourcePoint:
    x: int
    y: int
    mode: int


@dataclass(frozen=True)
class SourceGrid:
    x_q16: int
    y_q16: int
    heading: int


@dataclass(frozen=True)
class SourceBox:
    x_q16: int
    y_q16: int
    parameter: int
    half_extent: int


@dataclass(frozen=True)
class SourceCoin:
    x: int
    y: int
    parameter: int


@dataclass(frozen=True)
class SourceSection:
    kind: int
    version: int
    data: bytes


@dataclass(frozen=True)
class SourceCourse:
    course_id: int
    name: str
    catalog_id: int
    cup: int
    slot: int
    source_id: Optional[int]
    song: Optional[int]
    rules: SourceRules
    raster_width: int
    raster_height: int
    raster: bytes
    map_width: int
    map_height: int
    tilemap: bytes
    materials: bytes
    palette: tuple
    tiles_argb: tuple
    alias_tiles: tuple
    road_flags: int
    road_block_count: int
    route_rows: tuple
    target_table: bytes
    sentinel_mask: int
    sentinel_points: tuple
    grid: tuple
    item_boxes: tuple
    coins: tuple
    sky: Optional[bytes]
    sky_version: int
    passthrough: tuple
    tiles_indices: Optional[tuple] = None

    @property
    def arena(self):
        return self.rules.progress == 3

    @property
    def cup_id(self):
        return (self.catalog_id << 16) | self.cup

    @property
    def width(self):
        return self.map_width * 8

    @property
    def height(self):
        return self.map_height * 8


def _fail(course, section, message):
    raise SourceError(f'course={course} {section}: {message}')


def _signed(value):
    return value if value < 0x80000000 else value - 0x100000000


def _words(data, offset, count):
    return struct.unpack_from(f'<{count}I', data, offset)


def _valid_key(key):
    return isinstance(key, tuple) and len(key) == 2 and all(
        type(value) is int and 0 <= value <= 0xffffffff for value in key)


def _sections(sections, overlay, course):
    """Normalize both load_pack mappings and compile_track overlay records."""
    if not isinstance(sections, Mapping):
        _fail(course, 'sections', 'expected load_pack section mapping')
    result = {}
    for key, record in sections.items():
        if not _valid_key(key):
            _fail(course, 'sections', 'invalid section key')
        try:
            header, data = record
            version = header['version']
        except (TypeError, ValueError, KeyError):
            _fail(course, 'sections', f'invalid section record {key}')
        result[key] = _section(key[0], version, data, course)
    if isinstance(overlay, Mapping):
        for key, record in overlay.items():
            if not _valid_key(key):
                _fail(course, 'overlay', 'invalid section key')
            try:
                header, data = record
                version = header['version']
            except (TypeError, ValueError, KeyError):
                _fail(course, 'overlay', f'invalid section record {key}')
            result[key] = _section(key[0], version, data, course)
    else:
        try:
            records = iter(overlay)
        except TypeError:
            _fail(course, 'overlay', 'expected section records')
        for record in records:
            try:
                key = record['kind'], record['content_id']
                if not _valid_key(key):
                    _fail(course, 'overlay', 'invalid kind/content ID')
                result[key] = _section(key[0], record['version'], record['data'], course)
            except (TypeError, KeyError):
                _fail(course, 'overlay', 'expected compile_track section records')
    return result


def _section(kind, version, data, course):
    if type(kind) is not int or not 0 <= kind <= 0xffffffff or \
            type(version) is not int or not 1 <= version <= 0xffffffff or not isinstance(data, (bytes, bytearray, memoryview)):
        _fail(course, 'sections', 'invalid kind, version or payload')
    return SourceSection(kind, version, bytes(data))


def _required(sections, course, kind, label):
    try:
        return sections[kind, course]
    except KeyError:
        _fail(course, label, f'missing kind {kind}')


def _world(section, course):
    data = section.data
    if section.version != 1 or len(data) < 28:
        _fail(course, 'WLD1', 'expected version1 header')
    magic, rw, rh, mw, mh, rn, mn = _words(data, 0, 7)
    if magic != 0x31444c57 or not (0 < mw <= 256 and 0 < mh <= 256) or \
            rw * 2 != mw or rh * 2 != mh or rn != rw * rh or mn != mw * mh or \
            len(data) != 28 + rn + mn:
        _fail(course, 'WLD1', 'invalid dimensions, counts or length (raster16/map8 units)')
    return rw, rh, data[28:28+rn], mw, mh, data[28+rn:]


def _route(section, course, raster):
    if section.version != 4:
        _fail(course, 'RUT1', 'expected version4 three-row route')
    try:
        count, rows, target, sentinel = track_format.parse_route(section.data)
    except (track_format.TrackError, struct.error) as error:
        _fail(course, 'RUT1', str(error))
    if not 2 <= count <= 127 or any(value >= 3 for value in target):
        _fail(course, 'RUT1', 'invalid route count or target-row selector')
    if sentinel[0] & ~7:
        _fail(course, 'RUT1', 'invalid sentinel mask')
    points = tuple((_signed(sentinel[1+i*2]), _signed(sentinel[2+i*2])) for i in range(3))
    if any(point != (0, 0) and not sentinel[0] & (1 << i) for i, point in enumerate(points)):
        _fail(course, 'RUT1', 'inactive sentinel coordinates must be zero')
    if any((value & 127) != 127 and (value & 127) >= count for value in raster):
        _fail(course, 'WLD1', 'raster checkpoint exceeds route count')
    decoded = []
    for row in rows:
        if any(mode > 255 for _, _, mode in row):
            _fail(course, 'RUT1', 'AI mode exceeds byte range')
        if any(not (-32768 <= _signed(x) <= 32767 and -32768 <= _signed(y) <= 32767)
               for x, y, _ in row):
            _fail(course, 'RUT1', 'route coordinate cannot fit source Q16')
        decoded.append(tuple(SourcePoint(_signed(x), _signed(y), mode) for x, y, mode in row))
    return count, tuple(decoded), bytes(target), sentinel[0], points


def _rules(section, course, count):
    try:
        rules = track_format.parse_rules(section.data, section.version)
    except (track_format.TrackError, struct.error) as error:
        _fail(course, 'RUL1', str(error))
    if rules['course'] != course or (rules['progress'], rules['finish']) not in ((1, 1), (2, 2), (3, 3)):
        _fail(course, 'RUL1', 'course identity or progress/finish combination')
    if rules['start_index'] >= count or rules['start_cp'] > 255 or \
            (rules['progress'] != 1 and ((rules['start_cp'] & 127) >= count or
             (rules['start_cp'] & 127) != rules['start_index'])):
        _fail(course, 'RUL1', 'invalid initial checkpoint')
    if (rules['progress'] == 3 and rules['laps'] != 0) or \
            (rules['progress'] != 3 and not 1 <= rules['laps'] <= 5) or \
            not rules['countdown'] or not rules['rescue']:
        _fail(course, 'RUL1', 'invalid laps or countdown/recovery duration')
    if section.version == 2 and (rules['threshold'] not in range(1, 9) or any(section.data[69:72])):
        _fail(course, 'RUL1', 'invalid item threshold or reserved bytes')
    return SourceRules(rules['revision'], rules['schema'], rules['progress'], rules['finish'],
        rules['laps'], _signed(rules['start_x']), _signed(rules['start_y']), rules['heading'],
        _signed(rules['finish_y']), rules['start_index'], rules['start_cp'], rules['countdown'],
        rules['rescue'], rules['threshold'])


# MAT1 adds tails, without moving the per-tile material bytes. Validate the full
# declared layout before exposing that prefix; gameplay decoders own tail values.
_MATERIAL_TAILS = (4, 4, 4, 12, 1152, 8, 8, 8, 6, 32, 2178, 124, 4, 24, 12)


def _materials(section, course):
    data = section.data
    version = section.version
    expected = 264 + sum(_MATERIAL_TAILS[:version-1])
    if not 1 <= version <= 16 or len(data) != expected or _words(data, 0, 2) != (0x3154414d, 256):
        _fail(course, 'MAT1', 'invalid version, tile count or layout length')
    return data[8:264]


def _argb(color):
    r, g, b = color & 31, (color >> 5) & 31, (color >> 10) & 31
    return 0xff000000 | ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2)


def _tiles(section, course, preserve_indices=False):
    data = section.data
    if section.version != 1 or len(data) < 24:
        _fail(course, 'RAD1', 'expected version1 header')
    magic, tile_bytes, colors, flags, blocks, reserved = _words(data, 0, 6)
    if magic != 0x31444152 or tile_bytes != 16384 or colors != 64 or reserved or \
            flags > 255 or blocks > 255 or len(data) != 24 + tile_bytes + colors * 2:
        _fail(course, 'RAD1', 'invalid tile/palette counts, flags or payload length')
    indices = data[24:24+tile_bytes]
    palette = struct.unpack_from('<64H', data, 24+tile_bytes)
    # The native ground sampler leaves invalid palette references transparent,
    # including references in used tiles; never mask them into another color.
    rgba = (0,) + tuple(_argb(value) for value in palette[1:]) + (0,)*(256-colors)
    pixels = tuple(tuple(rgba[index] for index in indices[start:start+64])
                   for start in range(0, tile_bytes, 64))
    preserved = tuple(bytes(index if 0 < index < colors else 255
                            for index in indices[start:start+64])
                      for start in range(0,tile_bytes,64)) if preserve_indices else None
    return palette, pixels, flags, blocks, preserved


def _grid(section, course, arena, width, height):
    data = section.data
    count = 4 if arena else 8
    if section.version != 1 or len(data) != 8 + count * 12 or _words(data, 0, 2) != (0x31475047, count):
        _fail(course, 'GPG1', f'expected {count} actual start rows')
    result = []
    for i in range(count):
        x, y, heading = _words(data, 8+i*12, 3)
        x, y = _signed(x), _signed(y)
        if not (0 <= x < width * 65536 and 0 <= y < height * 65536) or heading > 65535:
            _fail(course, 'GPG1', f'invalid grid row {i}')
        result.append(SourceGrid(x, y, heading))
    return tuple(result)


def _boxes(section, course, width, height):
    data = section.data
    if section.version != 1 or len(data) < 8:
        _fail(course, 'OBJ1', 'expected version1 header')
    magic, count = _words(data, 0, 2)
    if magic != 0x314a424f or count > 64 or len(data) != 8 + count * 12:
        _fail(course, 'OBJ1', 'invalid count or payload length')
    result = []
    for i in range(count):
        x, y, kind, parameter, extent = struct.unpack_from('<IIHBB', data, 8+i*12)
        x, y = _signed(x), _signed(y)
        if kind != 0x8800 or extent != 0x1e or not (0 <= x < width*65536 and 0 <= y < height*65536):
            _fail(course, 'OBJ1', f'unsupported or out-of-map box {i}')
        result.append(SourceBox(x, y, parameter, extent))
    return tuple(result)


def _coins(section, course, width, height):
    if section is None:
        return ()
    data = section.data
    if section.version != 1 or len(data) < 8:
        _fail(course, 'CIN1', 'expected version1 header')
    magic, count = _words(data, 0, 2)
    if magic != 0x314e4943 or count > 64 or len(data) != 8 + count * 4:
        _fail(course, 'CIN1', 'invalid count or payload length')
    result = []
    for marker, x, y, parameter in struct.iter_unpack('<4B', data[8:]):
        if marker != 0x80 or x * 8 >= width or y * 8 >= height:
            _fail(course, 'CIN1', 'invalid coin marker or position')
        result.append(SourceCoin(x*8, y*8, parameter))
    return tuple(result)


def _catalog(sections, course):
    found = None
    for (kind, identity), section in sorted(sections.items()):
        if kind != 37:
            continue
        data = section.data
        if section.version not in (1, 2) or len(data) < 8:
            _fail(course, 'CAT1', 'unsupported catalog version/header')
        magic, count = _words(data, 0, 2)
        if magic != 0x31544143 or count > 256 or len(data) != 8 + count * 12 or identity > 65535:
            _fail(course, 'CAT1', 'invalid catalog count or length')
        for at in range(8, len(data), 12):
            ident, cup, slot, song, source = struct.unpack_from('<IBBHI', data, at)
            if ident != course:
                continue
            row = identity, cup, slot, source, song
            if found is not None:
                _fail(course, 'CAT1', 'course appears in multiple catalog rows')
            found = row
    return found if found is not None else (0, 0, 0, None, None)


def read_course(sections, course_id, *, overlay=(), name=None, preserve_indices=False):
    """Decode a load_pack mapping plus optional compile_track section records.

    Text names are supplied by the caller/folder: CRS1 and CAT1 retain graphical
    names, not text. Without one, use a neutral ID label. Alias tiles retain their
    own imported pixels/materials; RAD1 contains no alias-to-original directory.
    Opt-in tiles_indices retains raw palette identities; zero/invalid is255.
    """
    if type(course_id) is not int or not 0 <= course_id <= 0xffffffff:
        _fail(course_id, 'sections', 'course ID must be uint32')
    if name is not None and (not isinstance(name, str) or not name.strip() or '\0' in name):
        _fail(course_id, 'name', 'expected nonempty text without NUL')
    if type(preserve_indices) is not bool:
        _fail(course_id, 'RAD1', 'preserve_indices must be boolean')
    selected = _sections(sections, overlay, course_id)
    rw, rh, raster, mw, mh, tilemap = _world(_required(selected, course_id, 5, 'WLD1'), course_id)
    count, routes, targets, mask, sentinels = _route(_required(selected, course_id, 4, 'RUT1'), course_id, raster)
    rules = _rules(_required(selected, course_id, 3, 'RUL1'), course_id, count)
    materials = _materials(_required(selected, course_id, 6, 'MAT1'), course_id)
    palette, tiles, flags, blocks, indices = _tiles(_required(selected, course_id, 8, 'RAD1'),
                                                  course_id,preserve_indices)
    grid = _grid(_required(selected, course_id, 34, 'GPG1'), course_id, rules.progress == 3, mw*8, mh*8)
    boxes = _boxes(_required(selected, course_id, 25, 'OBJ1'), course_id, mw*8, mh*8)
    coins = _coins(selected.get((27, course_id)), course_id, mw*8, mh*8)
    sky = selected.get((9, course_id))
    if sky is not None:
        try:
            track_format.sky_layout(sky.data)
        except (track_format.TrackError, struct.error) as error:
            _fail(course_id, 'SKY1', str(error))
        if sky.version not in (1, 2):
            _fail(course_id, 'SKY1', 'unsupported sky version')
    catalog, cup, slot, source, song = _catalog(selected, course_id)
    passthrough = tuple(section for (kind, ident), section in sorted(selected.items())
                        if ident == course_id and kind in track_format.PASSTHROUGH)
    return SourceCourse(course_id, name if name is not None else f'COURSE {course_id}',
        catalog, cup, slot, source, song, rules, rw, rh, raster, mw, mh, tilemap, materials,
        palette, tiles, tuple(sorted(set(tilemap) & set(range(0xd0, 0xe0)))), flags, blocks,
        routes, targets, mask, sentinels, grid, boxes, coins,
        sky.data if sky else None, sky.version if sky else 0, passthrough, indices)
