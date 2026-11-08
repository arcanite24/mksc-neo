"""Bounded MPA1/v1 road/sky palette bindings for mesh courses (kind107).

The ordinary RGB seed textures remain usable without this optional profile.
Index planes preserve color identity; AMB1 retains the accepted-tick schedule.
"""
import json
from pathlib import Path, PurePosixPath
import struct

import ambient_rom

KIND = 107
MAGIC = 0x3141504D
HEADER = 48
MAX_BINDINGS = 128
MAX_INDEX_BYTES = 4 * 1024 * 1024
MAX_VARIANTS = 512


def integer(value, low, high, label):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'palette {label}: expected integer {low}..{high}')
    return value


def _file(root, name, maximum):
    if type(name) is not str or not name or '\\' in name or ':' in name:
        raise ValueError('palette source: expected a relative file')
    path = PurePosixPath(name)
    if path.is_absolute() or any(p in ('', '.', '..') for p in name.split('/')):
        raise ValueError('palette source: unsafe relative file')
    root = Path(root).resolve()
    source = root.joinpath(*path.parts)
    if not source.resolve().is_relative_to(root) or not source.is_file():
        raise ValueError(f'palette source: missing or escaped file {name}')
    # Bound the actual read too: a provider/file can change after a stat.
    with source.open('rb') as stream:
        data = stream.read(maximum + 1)
    if len(data) > maximum:
        raise ValueError(f'palette source: file exceeds {maximum} bytes')
    return data


def ambient_variants(data, road, sky):
    """Enumerate immutable palette pairs, retaining PRE/POST overlap.

    Timer closure is conservative about timing, but never substitutes a new
    motion schedule: the native AMB1 owner still chooses every actual write.
    """
    ambient_rom.validate(data)
    _, _, lane_count, frame_count, patch_count, initial_count, _, _ = struct.unpack_from('<8I', data)
    if not 1 <= lane_count <= 2:
        raise ValueError('palette ambient: expected sunset and optional timer')
    lanes = [struct.unpack_from('<4B4HI4I', data, 32 + i * 32) for i in range(lane_count)]
    sunset = [lane for lane in lanes if lane[2] == ambient_rom.SUNSET]
    timers = [lane for lane in lanes if lane[2] == ambient_rom.TIMER]
    if len(sunset) != 1 or len(timers) != lane_count - 1:
        raise ValueError('palette ambient: unsupported controller')
    if sunset[0][0] != ambient_rom.PRE or sunset[0][5] != 64 or any(
            lane[3] != ambient_rom.AUTO for lane in lanes) or any(
            lane[0] != ambient_rom.POST for lane in timers):
        raise ValueError('palette ambient: sunset/timer stage or trigger')
    frame_base = 32 + lane_count * 32
    frames = [struct.unpack_from('<HHB3x', data, frame_base + i * 8) for i in range(frame_count)]
    patch_base = frame_base + frame_count * 8
    patches = [struct.unpack_from('<BBHHHI', data, patch_base + i * 12)
               for i in range(patch_count + initial_count)]
    if any(p[0] > 1 for p in patches[:patch_count]) or any(
            p[0] > 2 for p in patches[patch_count:]):
        raise ValueError('palette ambient: unsupported patch target')
    payload = data[patch_base + (patch_count + initial_count) * 12:]

    def apply(pair, records):
        output = bytearray(pair)
        for target, _, offset, count, _, source in records:
            if target <= 1:
                at = target * 128 + offset * 2
                output[at:at + count * 2] = payload[source:source + count * 2]
        return bytes(output)

    def frame(pair, index):
        _, first, count = frames[index]
        return apply(pair, patches[first:first + count])

    timer_frames = (range(timers[0][4], timers[0][4] + timers[0][5]) if timers else ())
    variants = []
    seen = set()

    def admit(pair):
        if pair not in seen:
            if len(variants) == MAX_VARIANTS:
                raise ValueError('palette ambient: immutable variant capacity exceeded')
            variants.append(pair)
            seen.add(pair)

    def closure(seeds):
        pending = list(dict.fromkeys(seeds))
        found = set(pending)
        for pair in pending:
            admit(pair)
            for index in timer_frames:
                candidate = frame(pair, index)
                if candidate not in found:
                    found.add(candidate)
                    pending.append(candidate)
        return pending

    seed = struct.pack('<128H', *road, *sky)
    frontier = closure([apply(seed, patches[patch_count:])])
    for index in range(sunset[0][4], sunset[0][4] + 64):
        frontier = closure(frame(pair, index) for pair in frontier)
    return tuple(variants)


def normalize(record, root):
    fields = {'profile_id', 'revision', 'road_seed', 'sky_seed', 'ambient', 'bindings'}
    if type(record) is not dict or set(record) != fields:
        raise ValueError('palette profile: missing or unknown fields')
    identity = integer(record['profile_id'], 1, 0xfffffffe, 'profile_id')
    revision = integer(record['revision'], 1, 0xffffffff, 'revision')
    seeds = []
    for key in ('road_seed', 'sky_seed'):
        colors = record[key]
        if type(colors) is not list or len(colors) != 64:
            raise ValueError(f'palette {key}: expected64 RGB555 words')
        seeds.append([integer(v, 0, 65535, key) for v in colors])
    ambient = _file(root, record['ambient'], 300000)
    ambient_variants(ambient, *seeds)
    bindings = record['bindings']
    if type(bindings) is not list or not 1 <= len(bindings) <= MAX_BINDINGS:
        raise ValueError('palette bindings: expected1..128 entries')
    rows = []
    total_indices = 0
    for binding in bindings:
        if type(binding) is not dict or set(binding) != {
                'kind', 'id', 'bank', 'width', 'height', 'indices'}:
            raise ValueError('palette binding: missing or unknown fields')
        kind = integer(binding['kind'], 97, 101, 'binding kind')
        if kind not in (97, 101):
            raise ValueError('palette binding: expected material97 or texture101')
        content_id = integer(binding['id'], 1, 0xfffffffe, 'binding id')
        bank = integer(binding['bank'], 0, 1, 'binding bank')
        width = integer(binding['width'], 1, 1024, 'binding width')
        height = integer(binding['height'], 1, 1024, 'binding height')
        total_indices += width * height
        if total_indices > MAX_INDEX_BYTES:
            raise ValueError('palette bindings: index-plane capacity exceeded')
        indices = _file(root, binding['indices'], width * height)
        if len(indices) != width * height or any(v > 63 and v != 255 for v in indices):
            raise ValueError('palette binding: invalid index plane size or color')
        rows.append(((kind, content_id), bank, width, height, indices))
    rows.sort(key=lambda row: row[0])
    if len({row[0] for row in rows}) != len(rows):
        raise ValueError('palette bindings: duplicate target')
    planes = bytearray()
    table = bytearray()
    for (kind, content_id), bank, width, height, indices in rows:
        if len(planes) + len(indices) > MAX_INDEX_BYTES:
            raise ValueError('palette bindings: index-plane capacity exceeded')
        table.extend(struct.pack('<8I', kind, content_id, bank, width, height,
                                 len(planes), len(indices), 0))
        planes.extend(indices)
    size = HEADER + 256 + len(table) + len(ambient) + len(planes)
    return (struct.pack('<12I', MAGIC, 1, size, identity, revision, len(rows),
                        len(ambient), len(planes), 0, 0, 0, 0) +
            struct.pack('<128H', *seeds[0], *seeds[1]) + table + ambient + planes)


def compile(document, root):
    if type(document) is not dict or set(document) != {'version', 'profiles'}:
        raise ValueError('palettes: expected version/profiles')
    integer(document['version'], 1, 1, 'version')
    profiles = document['profiles']
    if type(profiles) is not list or not 1 <= len(profiles) <= 256:
        raise ValueError('palettes: expected1..256 profiles')
    result, identities = [], set()
    total = 0
    for record in profiles:
        payload = normalize(record, root)
        total += len(payload)
        if total > 96 * 1024 * 1024:
            raise ValueError('palette profiles: archive capacity exceeded')
        identity = record['profile_id']
        if identity in identities:
            raise ValueError('palette duplicate profile_id')
        identities.add(identity)
        result.append((dict(kind=KIND, content_id=identity, version=1, flags=3), payload))
    return result


def load(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f'palette duplicate field={key}')
            result[key] = value
        return result
    return json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=unique)
