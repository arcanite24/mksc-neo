"""Compile bounded, optional model assets from models.json and local OBJ/PNG files.

Coordinates are model-local numbers; +Z is up, -Y is forward. UVs address
normalized texture coordinates. Geometry/material IDs are local pack keys;
bindings name manifest driver/course IDs rather than source graphics donors.
"""
import json
import math
from pathlib import Path
import struct
import zlib

import png_indexed
from ui_rom import image_asset

MAX_VERTICES = 2048
MAX_TRIANGLES = 4096
MAX_MATERIALS = 16
MAX_BINDINGS = 256
MAX_FILE = 8 * 1024 * 1024
MAX_PAYLOAD = 64 * 1024 * 1024


class ModelError(ValueError):
    pass


def _object(value, allowed, required, label):
    if not isinstance(value, dict):
        raise ModelError(f'{label}: expected an object')
    unknown = set(value) - set(allowed)
    missing = set(required) - set(value)
    if unknown or missing:
        raise ModelError(f'{label}: unknown fields {sorted(unknown)}, missing fields {sorted(missing)}')
    return value


def _integer(value, minimum, maximum, label):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ModelError(f'{label}: expected integer {minimum}..{maximum}')
    return value


def _q16(value, label):
    if type(value) not in (int, float) or (type(value) is float and not math.isfinite(value)):
        raise ModelError(f'{label}: expected a finite number')
    if not -32768 <= value < 32768:
        raise ModelError(f'{label}: outside signed Q16 range')
    scaled = value * 65536
    if not math.isfinite(scaled) or not -2147483648 <= scaled <= 2147483647:
        raise ModelError(f'{label}: outside signed Q16 range')
    # Symmetric nearest conversion; no Python banker rounding or host float cast.
    return math.floor(scaled + 0.5) if scaled >= 0 else math.ceil(scaled - 0.5)


def _vector(value, count, label):
    if not isinstance(value, list) or len(value) != count:
        raise ModelError(f'{label}: expected {count} numbers')
    return [_q16(number, f'{label}[{index}]') for index, number in enumerate(value)]


def quantize_vertex(value, label='vertex'):
    """Encode one MSH1 XYZ/UV vertex as its five signed Q16 wire words.

    Generated asset identities must use these same values rather than raw
    host-libm floating point coordinates that the compiler later discards.
    """
    return _vector(value, 5, label)


def _list(value, maximum, label, empty=False):
    if not isinstance(value, list) or len(value) > maximum or (not empty and not value):
        raise ModelError(f'{label}: expected {0 if empty else 1}..{maximum} entries')
    return value


def _path(root, name, max_file=MAX_FILE):
    if not isinstance(name, str) or not name or '\\' in name or ':' in name:
        raise ModelError('asset path: expected a relative local file')
    relative = Path(name)
    if relative.is_absolute() or '..' in relative.parts:
        raise ModelError(f'{name}: asset path must stay inside the mod source folder')
    path = (root / relative).resolve()
    if not path.is_relative_to(root.resolve()) or not path.is_file():
        raise ModelError(f'{name}: missing file or path outside the mod source folder')
    if path.stat().st_size > max_file:
        raise ModelError(f'{name}: exceeds {max_file // (1024 * 1024)} MiB input limit')
    return path


def _texture(root, name, max_dimension=1024, max_pixels=1024 * 1024, max_file=MAX_FILE):
    """Decode bounded non-interlaced 8-bit PNG into embedded direct ARGB."""
    data = _path(root, name, max_file).read_bytes()
    return _texture_bytes(data, name, max_dimension, max_pixels)


def _texture_bytes(data, name, max_dimension=1024, max_pixels=1024 * 1024):
    """The same checked PNG decoder also accepts a bounded GLB image view."""
    if data[:8] != png_indexed.SIGNATURE:
        raise ModelError(f'{name}: expected a PNG image')
    offset, header, palette, transparency, compressed = 8, None, None, b'', bytearray()
    ended = False
    while offset + 12 <= len(data):
        length, kind = struct.unpack_from('>I4s', data, offset)
        if length > len(data) - offset - 12:
            raise ModelError(f'{name}: truncated PNG chunk')
        body = data[offset + 8:offset + 8 + length]
        crc = struct.unpack_from('>I', data, offset + 8 + length)[0]
        if zlib.crc32(kind + body) & 0xffffffff != crc:
            raise ModelError(f'{name}: PNG CRC mismatch')
        offset += length + 12
        if kind == b'IHDR':
            if header is not None or length != 13 or offset != 33:
                raise ModelError(f'{name}: invalid PNG header')
            header = struct.unpack('>IIBBBBB', body)
        elif kind == b'PLTE':
            if not length or length % 3 or length > 768:
                raise ModelError(f'{name}: invalid PNG palette')
            palette = [tuple(body[i:i + 3]) for i in range(0, length, 3)]
        elif kind == b'tRNS':
            transparency = body
        elif kind == b'IDAT':
            compressed += body
        elif kind == b'IEND':
            if length or offset != len(data):
                raise ModelError(f'{name}: invalid PNG end')
            ended = True
            break
        elif kind[0] & 32 == 0:
            raise ModelError(f'{name}: unsupported PNG chunk {kind!r}')
    if not header or not ended:
        raise ModelError(f'{name}: incomplete PNG')
    width, height, depth, color, compression, filtering, interlace = header
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color)
    if (not 1 <= width <= max_dimension or not 1 <= height <= max_dimension or width * height > max_pixels or depth != 8 or
            channels is None or compression or filtering or interlace):
        raise ModelError(f'{name}: expected non-interlaced 8-bit PNG, dimensions 1..{max_dimension}')
    if color == 3 and (palette is None or len(transparency) > len(palette)):
        raise ModelError(f'{name}: invalid indexed PNG palette/transparency')
    if transparency and ((color == 0 and len(transparency) != 2) or
                         (color == 2 and len(transparency) != 6) or color in (4, 6)):
        raise ModelError(f'{name}: invalid PNG transparency')
    stride = width * channels
    expected = height * (stride + 1)
    inflater = zlib.decompressobj()
    try:
        raw = inflater.decompress(compressed, expected + 1)
    except zlib.error:
        raise ModelError(f'{name}: corrupt PNG pixels') from None
    if len(raw) != expected or not inflater.eof or inflater.unused_data:
        raise ModelError(f'{name}: PNG expanded size mismatch')
    pixels, previous = [], bytearray(stride)
    transparent_rgb = struct.unpack('>3H', transparency) if color == 2 and transparency else None
    transparent_grey = struct.unpack('>H', transparency)[0] if color == 0 and transparency else None
    for y in range(height):
        start = y * (stride + 1)
        filter_type = raw[start]
        row = bytearray(raw[start + 1:start + 1 + stride])
        if filter_type > 4:
            raise ModelError(f'{name}: invalid PNG filter')
        for x in range(stride):
            left = row[x - channels] if x >= channels else 0
            up, corner = previous[x], previous[x - channels] if x >= channels else 0
            prediction = (0, left, up, (left + up) // 2,
                          png_indexed._paeth(left, up, corner))[filter_type]
            row[x] = (row[x] + prediction) & 255
        for x in range(0, stride, channels):
            alpha = 255
            if color == 3:
                index = row[x]
                if index >= len(palette):
                    raise ModelError(f'{name}: pixel index outside PNG palette')
                red, green, blue = palette[index]
                alpha = transparency[index] if index < len(transparency) else 255
            elif color in (0, 4):
                red = green = blue = row[x]
                alpha = row[x + 1] if color == 4 else (0 if red == transparent_grey else 255)
            else:
                red, green, blue = row[x:x + 3]
                alpha = row[x + 3] if color == 6 else (0 if (red, green, blue) == transparent_rgb else 255)
            pixels.append(alpha << 24 | red << 16 | green << 8 | blue)
        previous = row
    return image_asset(width, height, pixels)


def _obj(root, name):
    """Read bounded v/vt/f geometry with explicitly triangulated faces."""
    path = _path(root, name)
    positions, texcoords, vertices, triangles, lookup = [], [], [], [], {}
    for line_number, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
        tokens = line.partition('#')[0].split()
        if not tokens:
            continue
        label = f'{name}:{line_number}'
        command, values = tokens[0], tokens[1:]
        if command in ('o', 'g', 's'):
            continue
        if command in ('v', 'vt'):
            count = 3 if command == 'v' else 2
            if len(values) != count:
                raise ModelError(f'{label}: {command} needs {count} coordinates')
            try:
                vector = _vector([float(v) for v in values], count, label)
            except ValueError as error:
                raise ModelError(f'{label}: {error}') from None
            target = positions if command == 'v' else texcoords
            if len(target) >= MAX_VERTICES:
                raise ModelError(f'{label}: too many {command} entries')
            target.append(vector)
        elif command == 'f':
            if len(values) != 3:
                raise ModelError(f'{label}: export triangulated OBJ faces (exactly three corners)')
            face = []
            for token in values:
                parts = token.split('/')
                if len(parts) > 2 or not parts[0] or (len(parts) == 2 and not parts[1]):
                    raise ModelError(f'{label}: use position or position/UV face indices')
                resolved = []
                for part, entries in zip(parts, (positions, texcoords)):
                    try:
                        index = int(part)
                    except ValueError:
                        raise ModelError(f'{label}: invalid face index {part}') from None
                    if index == 0 or not -len(entries) <= index <= len(entries):
                        raise ModelError(f'{label}: face index {index} outside current entries')
                    resolved.append(index - 1 if index > 0 else len(entries) + index)
                key = tuple(resolved)
                if key not in lookup:
                    if len(vertices) >= MAX_VERTICES:
                        raise ModelError(f'{label}: split UV geometry exceeds {MAX_VERTICES} vertices')
                    lookup[key] = len(vertices)
                    vertices.append(positions[key[0]] + (texcoords[key[1]] if len(key) == 2 else [0, 0]))
                face.append(lookup[key])
            for index in range(1, len(face) - 1):
                if len(triangles) >= MAX_TRIANGLES:
                    raise ModelError(f'{label}: exceeds {MAX_TRIANGLES} triangles')
                triangles.append([face[0], face[index], face[index + 1], 0])
        else:
            raise ModelError(f'{label}: unsupported OBJ directive {command}; use v, vt and f')
    if not vertices or not triangles:
        raise ModelError(f'{name}: no triangle geometry')
    return vertices, triangles


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ModelError(f'duplicate JSON field {key}')
        result[key] = value
    return result


def load(path):
    path = Path(path)
    if path.stat().st_size > MAX_FILE:
        raise ModelError(f'{path.name}: exceeds 8 MiB input limit')
    return json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=_unique_object)


def _append_payload(payloads, kind, identity, wire, version=1):
    if sum(len(data) for _, data in payloads) + len(wire) > MAX_PAYLOAD:
        raise ModelError('models: compiled payloads exceed 64 MiB')
    payloads.append(({'kind': kind, 'content_id': identity, 'version': version, 'flags': 0}, wire))


def compile(document, root):
    """Return existing mod-kit (section metadata, bytes) payload tuples."""
    root = Path(root)
    _object(document, ('version', 'materials', 'meshes', 'bindings', 'binding_mode'),
            ('version', 'materials', 'meshes', 'bindings'), 'models')
    _integer(document['version'], 1, 1, 'models.version')
    binding_mode = document.get('binding_mode', 'replace')
    if type(binding_mode) is not str or binding_mode not in ('replace', 'overlay'):
        raise ModelError('models.binding_mode: expected replace or overlay')
    materials, meshes, bindings, payloads = set(), set(), set(), []
    for index, row in enumerate(_list(document['materials'], 256, 'materials')):
        label = f'materials[{index}]'
        _object(row, ('id', 'argb', 'wrap_u', 'wrap_v', 'texture'), ('id', 'argb'), label)
        identity = _integer(row['id'], 1, 0xffffffff, f'{label}.id')
        if identity in materials:
            raise ModelError(f'{label}: duplicate material ID {identity}')
        materials.add(identity)
        color = row['argb']
        if isinstance(color, str) and len(color) == 9 and color.startswith('#'):
            try:
                color = int(color[1:], 16)
            except ValueError:
                raise ModelError(f'{label}.argb: expected #AARRGGBB') from None
        color = _integer(color, 0, 0xffffffff, f'{label}.argb')
        flags = 0
        for bit, field in enumerate(('wrap_u', 'wrap_v')):
            if field in row and type(row[field]) is not bool:
                raise ModelError(f'{label}.{field}: expected boolean')
            flags |= int(row.get(field, False)) << bit
        texture = _texture(root, row['texture']) if 'texture' in row else b''
        _append_payload(payloads, 97, identity,
                        struct.pack('<4sIII', b'MTL1', color, flags, len(texture)) + texture)
    for index, row in enumerate(_list(document['meshes'], 256, 'meshes')):
        label = f'meshes[{index}]'
        _object(row, ('id', 'materials', 'vertices', 'triangles', 'obj'), ('id', 'materials'), label)
        identity = _integer(row['id'], 1, 0xffffffff, f'{label}.id')
        if identity in meshes:
            raise ModelError(f'{label}: duplicate mesh ID {identity}')
        meshes.add(identity)
        material_ids = [_integer(v, 1, 0xffffffff, f'{label}.materials') for v in
                        _list(row['materials'], MAX_MATERIALS, f'{label}.materials')]
        if any(v not in materials for v in material_ids):
            raise ModelError(f'{label}: references an undefined material')
        if 'obj' in row:
            if 'vertices' in row or 'triangles' in row:
                raise ModelError(f'{label}: choose OBJ or JSON geometry, not both')
            vertices, triangles = _obj(root, row['obj'])
        else:
            vertices = [quantize_vertex(v, f'{label}.vertices[{i}]') for i, v in enumerate(
                _list(row.get('vertices'), MAX_VERTICES, f'{label}.vertices'))]
            triangles = _list(row.get('triangles'), MAX_TRIANGLES, f'{label}.triangles')
        encoded_triangles = []
        for i, triangle in enumerate(triangles):
            if not isinstance(triangle, list) or len(triangle) != 4:
                raise ModelError(f'{label}.triangles[{i}]: expected three indices and material slot')
            encoded_triangles.append([_integer(v, 0, len(vertices) - 1, f'{label}.triangles[{i}]')
                                      for v in triangle[:3]] +
                                     [_integer(triangle[3], 0, len(material_ids) - 1,
                                               f'{label}.triangles[{i}].material')])
        wire = struct.pack('<4sIII', b'MSH1', len(vertices), len(encoded_triangles), len(material_ids))
        wire += b''.join(struct.pack('<I', v) for v in material_ids)
        wire += b''.join(struct.pack('<5i', *v) for v in vertices)
        wire += b''.join(struct.pack('<4H', *t) for t in encoded_triangles)
        _append_payload(payloads, 96, identity, wire)
    rows = []
    for index, row in enumerate(_list(document['bindings'], MAX_BINDINGS, 'bindings', empty=True)):
        label = f'bindings[{index}]'
        _object(row, ('kind', 'target', 'art_bank', 'pose', 'mesh', 'scale', 'pivot'),
                ('kind', 'target', 'mesh'), label)
        if row['kind'] not in ('driver', 'scenery'):
            raise ModelError(f'{label}.kind: expected driver or scenery')
        kind = 1 if row['kind'] == 'driver' else 2
        target = _integer(row['target'], 1, 0xffffffff, f'{label}.target')
        bank = _integer(row.get('art_bank', 0), 0, 63, f'{label}.art_bank')
        pose = _integer(row.get('pose', 0), 0, 31, f'{label}.pose')
        mesh = _integer(row['mesh'], 1, 0xffffffff, f'{label}.mesh')
        if mesh not in meshes or (kind == 1 and bank):
            raise ModelError(f'{label}: undefined mesh or nonzero driver art bank')
        key = (kind, target, bank, pose)
        if key in bindings:
            raise ModelError(f'{label}: duplicate binding key {key}')
        bindings.add(key)
        scale = _vector(row.get('scale', [1, 1, 1]), 3, f'{label}.scale')
        if not all(scale):
            raise ModelError(f'{label}.scale: axes must be nonzero')
        pivot = _vector(row.get('pivot', [0, 0, 0]), 3, f'{label}.pivot')
        rows.append(struct.pack('<5I6i', kind, target, bank, pose, mesh, *scale, *pivot))
    # Placed C3D objects reference meshes directly. An empty registry section
    # would replace unrelated model bindings when composing ordered mods.
    if rows:
        _append_payload(payloads, 98, 1, struct.pack('<4sI', b'BND1', len(rows)) + b''.join(rows),
                        version=2 if binding_mode == 'overlay' else 1)
    return payloads
