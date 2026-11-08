"""Compile local static glTF and typed course.json gameplay into C3D1/C3C1.

All authoring coordinates use glTF metres (+Y up, +Z front). The pack uses
(x, -z, y); no authoring paths or floating-point values reach the runtime.
"""
import json
import math
from pathlib import Path
import struct

import model_format

DEFAULT_GRAVITY = 28.1635  # MK8: 0.15 units/frame^2 at one frame per tick, 1/19 m per unit
# The KPH1 default airborne support reserve, encoded exactly as the runtime.
DEFAULT_GROUND_SNAP_Q16 = 655
# Converted source tiers keep the flight time proved at the P3 calibration
# gravity (impulse scaled with gravity; the apex rises by the same ratio).
# Explicit course.json SURF launch values never pass through this adaptation.
P3_GRAVITY = 23.3766
SOURCE_LAUNCH_SCALE = math.sqrt(P3_GRAVITY/9.8)*DEFAULT_GRAVITY/P3_GRAVITY
SOURCE_PANEL_BOOST = 3.0


def source_launch(kind):
    """Encoded SI impulse for source material low-nibble0/2/14."""
    impulse = {0:9,2:6,14:3}[kind]*SOURCE_LAUNCH_SCALE
    return math.floor(impulse*65536+.5)/65536


ABSENT = 0xffffffff
MAX_BLOB = 32 * 1024 * 1024
STRIDES = dict(STRS=1, META=128, VERT=32, TRIS=16, MATS=48, COLV=12, COLT=32,
               SURF=48, CPNT=64, CPED=4, RTHD=32, RTND=32, RTED=4, GRID=32,
               ITEM=32, OBJS=64, RESP=48, CAMR=64, ZONE=80, PROP=32)
REQUIRED = tuple(STRIDES)[:14]
SURFACES = ('road', 'off-road', 'slippery', 'wall', 'boost', 'jump', 'water', 'lava',
            'out-of-bounds', 'respawn-hint')
ROUTES = ('cpu', 'item', 'camera')
PROPERTIES = ('integer', 'fixed', 'boolean', 'string', 'asset', 'object', 'route', 'vector')
MODES = dict(gp=1, tt=2, quick=4, vs=8, battle=16)


class CourseError(ValueError):
    pass


def fail(label, message):
    raise CourseError(f'{label}: {message}')


def obj(value, allowed, required, label):
    if type(value) is not dict:
        fail(label, 'expected an object')
    if set(value) - set(allowed) or set(required) - set(value):
        fail(label, f'unknown fields {sorted(set(value)-set(allowed))}; missing {sorted(set(required)-set(value))}')
    return value


def integer(value, low=0, high=ABSENT, label='integer'):
    if type(value) is not int or not low <= value <= high:
        fail(label, f'expected integer {low}..{high}')
    return value


def number(value, label):
    if type(value) not in (int, float) or not math.isfinite(value):
        fail(label, 'expected a finite number')
    return value


def fixed(value, bits=16, low=-32768, high=32768, label='fixed'):
    value = number(value, label)
    if not low <= value <= high:
        fail(label, f'outside {low}..{high}')
    scaled = value * (1 << bits)
    result = math.floor(scaled + .5) if scaled >= 0 else math.ceil(scaled - .5)
    return integer(result, -0x80000000, 0x7fffffff, label)


def array(value, count, label):
    if type(value) is not list or len(value) != count:
        fail(label, f'expected {count} entries')
    return value


def rows(value, maximum, label, nonempty=False):
    if type(value) is not list or len(value) > maximum or (nonempty and not value):
        fail(label, f'expected {1 if nonempty else 0}..{maximum} entries')
    return value


def position(value, label):
    x, y, z = [fixed(v, low=-4096, high=4096, label=label) for v in array(value, 3, label)]
    return [x, -z, y]


def vector(value, label, unit=False):
    raw = [number(v, label) for v in array(value, 3, label)]
    if unit:
        length = math.hypot(*raw)
        if not length or not math.isfinite(length):
            fail(label, 'zero unit vector')
        raw = [v / length for v in raw]
    x, y, z = [fixed(v, bits=30 if unit else 16, label=label) for v in raw]
    return [integer(v,-0x80000000,0x7fffffff,label) for v in (x,-z,y)]


def quaternion(value, label):
    raw = [number(v, label) for v in array(value, 4, label)]
    length = math.hypot(*raw)
    if not length or not math.isfinite(length):
        fail(label, 'zero quaternion')
    x, y, z, w = [fixed(v/length, bits=30, label=label) for v in raw]
    return [x, -z, y, w]


def color(value, label):
    if type(value) is str and len(value) == 9 and value[0] == '#':
        try:
            return int(value[1:], 16)
        except ValueError:
            pass
    return integer(value, label=label)


def index(value, count, label):
    return ABSENT if value is None else integer(value, 0, count - 1, label)


def pack_words(values):
    return struct.pack('<' + 'I' * len(values), *(value & ABSENT for value in values))


def duplicate_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            fail(key, 'duplicate JSON field')
        result[key] = value
    return result


def read_json(data, label):
    try:
        return json.loads(data, object_pairs_hook=duplicate_object,
                          parse_constant=lambda value: fail(label, f'nonfinite {value}'))
    except (UnicodeError, json.JSONDecodeError) as error:
        fail(label, str(error))


def load(path):
    path = Path(path)
    if path.stat().st_size > MAX_BLOB:
        fail(str(path), 'source JSON exceeds 32 MiB')
    return read_json(path.read_text(encoding='utf-8'), str(path))


def local_path(root, name):
    if type(name) is not str or not name or '\\' in name or ':' in name:
        fail('path', 'expected a relative local path')
    relative = Path(name)
    path = (root / relative).resolve()
    if relative.is_absolute() or '..' in relative.parts or not path.is_relative_to(root.resolve()) or not path.is_file():
        fail(name, 'missing asset or path outside course root')
    if path.stat().st_size > MAX_BLOB:
        fail(name, 'asset exceeds 32 MiB')
    return path


def identity():
    return [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]


def multiply(a, b):
    return [sum(a[k*4+r] * b[c*4+k] for k in range(4)) for c in range(4) for r in range(4)]


def node_matrix(node):
    if 'matrix' in node:
        if any(key in node for key in ('translation', 'rotation', 'scale')):
            fail('node', 'matrix and TRS are mutually exclusive')
        matrix = [number(v, 'node.matrix') for v in array(node['matrix'], 16, 'node.matrix')]
        if [matrix[i] for i in (3, 7, 11, 15)] != [0, 0, 0, 1]:
            fail('node.matrix', 'expected an affine transform')
        return matrix
    x, y, z, w = [number(v, 'node.rotation') for v in array(node.get('rotation', [0, 0, 0, 1]), 4, 'node.rotation')]
    length = math.hypot(x,y,z,w)
    if not length or not math.isfinite(length):
        fail('node.rotation', 'zero quaternion')
    x, y, z, w = [v/length for v in (x, y, z, w)]
    sx, sy, sz = [number(v, 'node.scale') for v in array(node.get('scale', [1, 1, 1]), 3, 'node.scale')]
    tx, ty, tz = [number(v, 'node.translation') for v in array(node.get('translation', [0, 0, 0]), 3, 'node.translation')]
    return [(1-2*y*y-2*z*z)*sx, (2*x*y+2*z*w)*sx, (2*x*z-2*y*w)*sx, 0,
            (2*x*y-2*z*w)*sy, (1-2*x*x-2*z*z)*sy, (2*y*z+2*x*w)*sy, 0,
            (2*x*z+2*y*w)*sz, (2*y*z-2*x*w)*sz, (1-2*x*x-2*y*y)*sz, 0,
            tx, ty, tz, 1]


def transform(matrix, value):
    return [sum(matrix[c*4+r]*value[c] for c in range(3))+matrix[12+r] for r in range(3)]


def normal_matrix(matrix):
    a, b, c = [matrix[i:i+3] for i in (0, 4, 8)]
    cross = lambda u, v: [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
    bc, ca, ab = cross(b, c), cross(c, a), cross(a, b)
    determinant = sum(a[i]*bc[i] for i in range(3))
    if not math.isfinite(determinant) or determinant == 0:
        fail('node', 'singular transform')
    return [v/determinant for column in (bc, ca, ab) for v in column], determinant


class Gltf:
    def __init__(self, root, filename):
        self.root = root
        path = local_path(root, filename)
        self.resource_directory = path.parent.relative_to(root.resolve())
        data = path.read_bytes()
        binary = None
        if data[:4] == b'glTF':
            if len(data) < 20 or struct.unpack_from('<II', data, 4) != (2, len(data)):
                fail(filename, 'invalid GLB header')
            chunks, offset = [], 12
            while offset < len(data):
                if offset + 8 > len(data):
                    fail(filename, 'truncated GLB chunk')
                length, kind = struct.unpack_from('<II', data, offset)
                if length % 4 or length > len(data)-offset-8:
                    fail(filename, 'invalid GLB chunk bounds')
                chunks.append((kind, data[offset+8:offset+8+length]))
                offset += length+8
            if not chunks or chunks[0][0] != 0x4e4f534a or len(chunks) > 2 or (len(chunks) == 2 and chunks[1][0] != 0x004e4942):
                fail(filename, 'expected JSON then optional BIN chunk')
            data = chunks[0][1]
            binary = chunks[1][1] if len(chunks) == 2 else None
        self.doc = read_json(data, filename)
        if self.doc.get('asset', {}).get('version') != '2.0' or self.doc.get('extensionsRequired') or self.doc.get('animations'):
            fail(filename, 'requires static glTF 2.0 without required extensions')
        self.buffers = []
        total_buffers = 0
        for i, buffer in enumerate(rows(self.doc.get('buffers', []),4096,'buffers')):
            length = integer(buffer.get('byteLength'), 1, MAX_BLOB, 'buffer.byteLength')
            total_buffers += length
            if total_buffers > MAX_BLOB:
                fail('buffers','combined source buffers exceed 32 MiB')
            payload = self.resource_path(buffer['uri']).read_bytes() if 'uri' in buffer else binary if i == 0 else None
            if payload is None or len(payload) < length or len(payload) - length > (3 if 'uri' not in buffer else 0):
                fail('buffer', 'declared length does not match storage')
            self.buffers.append(payload[:length])
        self.nodes = rows(self.doc.get('nodes', []), 4096, 'nodes', True)
        self.transforms, self.names = {}, {}
        parents = {}
        for i, node in enumerate(self.nodes):
            if 'skin' in node or 'weights' in node:
                fail('node', 'skinning/morphs are unsupported')
            for child in node.get('children', []):
                integer(child, 0, len(self.nodes)-1, 'node child')
                if child in parents:
                    fail('node', 'multiple parents')
                parents[child] = i
            name = node.get('name')
            if name is not None:
                if type(name) is not str or name in self.names:
                    fail('node.name', 'node names must be unique strings')
                self.names[name] = i
        for i in range(len(self.nodes)):
            pending, active, current = [], set(), i
            while current not in self.transforms:
                if current in active:
                    fail('node', 'transform cycle')
                active.add(current);pending.append(current)
                if current not in parents:
                    break
                current = parents[current]
            for current in reversed(pending):
                matrix = node_matrix(self.nodes[current])
                self.transforms[current] = multiply(self.transforms[parents[current]],matrix) if current in parents else matrix

    def resource_path(self, uri):
        if type(uri) is not str or ':' in uri or '\\' in uri or uri.startswith('/'):
            fail('glTF URI', 'expected a relative local asset')
        return local_path(self.root,(self.resource_directory / uri).as_posix())

    def accessor(self, index_value, shape, components):
        accessors = self.doc.get('accessors', [])
        accessor = accessors[integer(index_value, 0, len(accessors)-1, 'accessor index')]
        if 'sparse' in accessor or accessor.get('normalized', False):
            fail('accessor', 'sparse/normalized attributes are unsupported')
        component = accessor.get('componentType')
        if accessor.get('type') != shape or component not in components:
            fail('accessor', f'expected {shape}, component type {components}')
        code, size = {5121: ('B', 1), 5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}[component]
        lanes = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3}[shape]
        count = integer(accessor.get('count'), 1, 262144*3 if shape == 'SCALAR' else 131072, 'accessor.count')
        views = self.doc.get('bufferViews', [])
        view = views[integer(accessor.get('bufferView'), 0, len(views)-1, 'bufferView')]
        payload = self.buffers[integer(view.get('buffer'), 0, len(self.buffers)-1, 'buffer')]
        base = integer(view.get('byteOffset', 0), 0, len(payload), 'bufferView.byteOffset')
        length = integer(view.get('byteLength'), 1, len(payload)-base, 'bufferView.byteLength')
        offset = integer(accessor.get('byteOffset', 0), 0, length, 'accessor.byteOffset')
        stride = integer(view.get('byteStride', lanes*size), lanes*size, 252, 'bufferView.byteStride')
        if stride % size or offset % size or base % size or offset+(count-1)*stride+lanes*size > length:
            fail('accessor', 'alignment/stride/storage bounds invalid')
        values = [struct.unpack_from('<'+code*lanes, payload, base+offset+i*stride) for i in range(count)]
        if component == 5126 and any(not math.isfinite(v) for row in values for v in row):
            fail('accessor', 'nonfinite float')
        return values

    def primitives(self, name):
        if type(name) is not str or name not in self.names:
            fail('scene selector', f'unknown node {name!r}')
        i = self.names[name]
        node = self.nodes[i]
        meshes = self.doc.get('meshes', [])
        mesh = meshes[integer(node.get('mesh'), 0, len(meshes)-1, 'node.mesh')]
        matrix = self.transforms[i]
        inverse_normal, determinant = normal_matrix(matrix)
        for primitive in mesh.get('primitives', []):
            if primitive.get('mode', 4) != 4 or primitive.get('targets'):
                fail(name, 'requires static triangle primitives')
            attributes = primitive.get('attributes', {})
            if set(attributes) - {'POSITION', 'NORMAL', 'TEXCOORD_0'}:
                fail(name, 'unsupported vertex attributes (positions, normals and UV0 only)')
            positions = self.accessor(attributes.get('POSITION'), 'VEC3', (5126,))
            if len(positions) > 131072:
                fail(name, 'vertex cap exceeded')
            normals = self.accessor(attributes['NORMAL'], 'VEC3', (5126,)) if 'NORMAL' in attributes else None
            uvs = self.accessor(attributes['TEXCOORD_0'], 'VEC2', (5126,)) if 'TEXCOORD_0' in attributes else None
            if normals is not None and len(normals) != len(positions) or uvs is not None and len(uvs) != len(positions):
                fail(name, 'attribute counts differ')
            indices = [v[0] for v in self.accessor(primitive['indices'], 'SCALAR', (5121, 5123, 5125))] if 'indices' in primitive else list(range(len(positions)))
            if len(indices) % 3 or any(i >= len(positions) for i in indices):
                fail(name, 'triangle indices invalid')
            points = [transform(matrix, p) for p in positions]
            transformed_normals = None if normals is None else [
                [sum(inverse_normal[c*3+r]*n[c] for c in range(3)) for r in range(3)] for n in normals]
            triangles = [indices[j:j+3] for j in range(0, len(indices), 3)]
            if determinant < 0:
                triangles = [[a, c, b] for a, b, c in triangles]
            yield points, transformed_normals, uvs, triangles, primitive.get('material', 0)


class CourseCompiler:
    def __init__(self, document, root):
        allowed = ('version', 'id', 'scene', 'render_nodes', 'collision_nodes', 'textures', 'meta',
                   'surfaces', 'checkpoints', 'routes', 'grid', 'items', 'objects', 'respawns', 'cameras', 'zones')
        self.doc = obj(document, allowed, allowed[:5] + ('meta', 'surfaces', 'checkpoints', 'routes', 'grid'), 'course.json')
        if type(document['version']) is not int or document['version'] != 1:
            fail('course.version', 'expected 1')
        self.id = integer(document['id'],1,ABSENT,'course.id')
        self.root = Path(root)
        self.gltf = Gltf(self.root, document['scene'])
        self.tables = {tag: bytearray() for tag in STRIDES}
        self.strings = {}
        self.string('')
        self.counts = {}
        for name, maximum in (('surfaces', 256), ('checkpoints', 1024), ('routes', 16384),
                              ('grid', 64), ('items', 4096), ('objects', 4096), ('respawns', 1024),
                              ('cameras', 1024), ('zones', 4096)):
            self.counts[name] = len(rows(document.get(name, []), maximum, name,
                                        name in ('surfaces', 'routes', 'grid')))
        self.texture_ids = {}
        self.texture_sections = []

    def string(self, text):
        if type(text) is not str or '\0' in text:
            fail('string', 'expected NUL-free UTF-8 text')
        try:
            data = text.encode('utf-8')
        except UnicodeError:
            fail('string', 'invalid UTF-8 text')
        if text not in self.strings:
            if len(self.tables['STRS']) + len(data) + 1 > 65536:
                fail('STRS', '64 KiB string cap exceeded')
            self.strings[text] = len(self.tables['STRS'])
            self.tables['STRS'] += data + b'\0'
        return self.strings[text]

    def append(self, tag, values):
        data = pack_words(values)
        if len(data) != STRIDES[tag]:
            fail(tag, 'internal row stride mismatch')
        self.tables[tag] += data
        return len(self.tables[tag]) // STRIDES[tag] - 1

    def properties(self, entries):
        entries = rows(entries, 16384, 'properties')
        first = len(self.tables['PROP']) // 32
        for prop in entries:
            obj(prop, ('key', 'type', 'value', 'required'), ('key', 'type', 'value'), 'property')
            if type(prop['key']) is not str or ':' not in prop['key']:
                fail('property.key', 'expected a namespaced key containing colon')
            if prop.get('required', False) is not False:
                fail(prop['key'], 'required property semantics unsupported in T0')
            if prop['type'] not in PROPERTIES:
                fail('property.type', 'unknown type')
            kind = PROPERTIES.index(prop['type'])
            value = prop['value']
            if prop['key'] == 'mksc:item-rules' and (kind != 4 or
                    type(value) is not dict or type(value.get('kind')) is not int or
                    value.get('kind') != 103):
                fail(prop['key'], 'expected an ASSET reference to MIR1 kind 103')
            if kind == 0:
                words = [integer(value, -0x80000000, 0x7fffffff, 'property.integer')]
            elif kind == 1:
                words = [fixed(value, label='property.fixed')]
            elif kind == 2:
                if type(value) is not bool:
                    fail('property.boolean', 'expected boolean')
                words = [int(value)]
            elif kind == 3:
                words = [self.string(value)]
            elif kind == 4:
                obj(value, ('kind', 'id'), ('kind', 'id'), 'property.asset')
                words = [integer(value['kind'],1,ABSENT-1,'property.asset.kind'),
                         integer(value['id'],1,ABSENT-1,'property.asset.id')]
            elif kind in (5, 6):
                words = [integer(value,0,self.counts['objects' if kind == 5 else 'routes']-1,'property reference')]
            else:
                words = vector(value, 'property.vector')
            self.append('PROP', [self.string(prop['key']), kind, *words, *([0] * (4-len(words))), 0, 0])
        if len(self.tables['PROP']) // 32 > 16384:
            fail('PROP', 'property cap exceeded')
        return [first, len(entries)]

    def textures(self):
        images = self.gltf.doc.get('images', [])
        used_ids = set()
        for mapping in rows(self.doc.get('textures', []), 128, 'textures'):
            obj(mapping, ('image', 'id'), ('image', 'id'), 'texture mapping')
            image = integer(mapping['image'], 0, len(images)-1, 'texture image')
            identity = integer(mapping['id'],1,ABSENT-1,'texture id')
            if image in self.texture_ids or identity in used_ids:
                fail('textures', 'conflicting image/ID declarations')
            source = images[image]
            if 'uri' in source:
                if 'bufferView' in source:
                    fail('texture image','URI and bufferView are mutually exclusive')
                data = self.gltf.resource_path(source['uri']).read_bytes()
            else:
                views = self.gltf.doc.get('bufferViews',[])
                view = views[integer(source.get('bufferView'),0,len(views)-1,'image.bufferView')]
                buffer = self.gltf.buffers[integer(view.get('buffer'),0,len(self.gltf.buffers)-1,'image.buffer')]
                start = integer(view.get('byteOffset',0),0,len(buffer),'image.byteOffset')
                length = integer(view.get('byteLength'),1,len(buffer)-start,'image.byteLength')
                if source.get('mimeType') != 'image/png' or 'byteStride' in view:
                    fail('texture image','expected an unstrided image/png buffer view')
                data = buffer[start:start+length]
            payload = model_format._texture_bytes(data,f'image {image}',max_dimension=4096,max_pixels=4000000)
            self.texture_ids[image] = identity
            used_ids.add(identity)
            self.texture_sections.append(({'kind': 101, 'content_id': identity, 'version': 1, 'flags': 3}, payload))

    def texture_ref(self, info):
        if info is None:
            return ABSENT, 0
        if info.get('texCoord', 0) != 0 or info.get('extensions'):
            fail('material texture', 'only untransformed UV0 is supported')
        textures = self.gltf.doc.get('textures', [])
        texture = textures[integer(info.get('index'), 0, len(textures)-1, 'texture index')]
        image = integer(texture.get('source'), 0, len(self.gltf.doc.get('images', []))-1, 'texture source')
        if image not in self.texture_ids:
            fail('material texture', f'image {image} has no explicit kind-101 ID mapping')
        samplers = self.gltf.doc.get('samplers', [])
        sampler = samplers[integer(texture['sampler'], 0, len(samplers)-1, 'sampler')] if 'sampler' in texture else {}
        flags = 0
        for key, bit in (('wrapS', 1), ('wrapT', 2)):
            mode = sampler.get(key, 10497)
            if mode not in (10497, 33071):
                fail('material sampler', 'only REPEAT and CLAMP_TO_EDGE are supported')
            flags |= bit if mode == 10497 else 0
        return self.texture_ids[image], flags

    def materials(self):
        materials = rows(self.gltf.doc.get('materials', []), 256, 'materials', True)
        for material in materials:
            pbr = material.get('pbrMetallicRoughness', {})
            references = [self.texture_ref(info) for info in (pbr.get('baseColorTexture'),
                material.get('normalTexture'),material.get('emissiveTexture'),pbr.get('metallicRoughnessTexture'))]
            base, normal, emissive, metal = [identity for identity,_ in references]
            wraps = {flags for identity,flags in references if identity != ABSENT}
            if len(wraps)>1:
                fail('material sampler','all maps must share wrap modes')
            flags = next(iter(wraps),0)
            def rgba(values, count):
                channels = [fixed(v, bits=0, low=0, high=255, label='material color')
                            for v in [number(v, 'material color') * 255 for v in array(values, count, 'material color')]]
                if count == 3:
                    channels.append(255)
                return channels[0]<<24 | channels[1]<<16 | channels[2]<<8 | channels[3]
            flags |= 4 if material.get('doubleSided', False) else 0
            extensions = material.get('extensions', {})
            if set(extensions) - {'KHR_materials_unlit'}:
                fail('material', 'unsupported material extension')
            flags |= 8 if 'KHR_materials_unlit' in extensions else 0
            alpha = material.get('alphaMode', 'OPAQUE')
            if alpha not in ('OPAQUE', 'MASK', 'BLEND'):
                fail('material.alphaMode', 'unknown mode')
            flags |= {'OPAQUE': 0, 'MASK': 16, 'BLEND': 32}[alpha]
            self.append('MATS', [base, normal, emissive, metal,
                rgba(pbr.get('baseColorFactor', [1, 1, 1, 1]), 4),
                rgba(material.get('emissiveFactor', [0, 0, 0]), 3),
                fixed(pbr.get('metallicFactor', 1), low=0, high=1),
                fixed(pbr.get('roughnessFactor', 1), low=0, high=1),
                fixed(material.get('alphaCutoff', .5), low=0, high=1), flags, 0, 0])

    def geometry(self):
        # Keys are the complete emitted wire records: positions alone must not
        # merge render UV/normal seams. Dict insertion gives stable first use.
        welded = {'VERT': {}, 'COLV': {}}

        def vertex(tag, values):
            encoded = pack_words(values)
            known = welded[tag]
            if encoded not in known:
                if len(known) >= 131072:
                    fail(tag, 'vertex cap exceeded after welding')
                known[encoded] = len(known)
                self.tables[tag] += encoded
            return known[encoded]

        selectors = rows(self.doc['render_nodes'], 4096, 'render_nodes', True)
        if len(set(selectors)) != len(selectors):
            fail('render_nodes', 'duplicate node selector')
        for name in selectors:
            for points, normals, uvs, triangles, material in self.gltf.primitives(name):
                if len(self.tables['TRIS'])//16+len(triangles)>262144:
                    fail('render geometry','vertex/triangle cap exceeded')
                integer(material, 0, len(self.tables['MATS'])//48-1, 'primitive material')
                self.validate_triangles(points, triangles, name)
                if normals is None:
                    normals = [[0., 0., 0.] for _ in points]
                    for a, b, c in triangles:
                        ab = [points[b][i]-points[a][i] for i in range(3)]
                        ac = [points[c][i]-points[a][i] for i in range(3)]
                        n = [ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0]]
                        for index_value in (a, b, c):
                            normals[index_value] = [normals[index_value][i]+n[i] for i in range(3)]
                used = {i for triangle in triangles for i in triangle}
                remap = {}
                for i, point in enumerate(points):
                    if i not in used:
                        continue
                    uv = [fixed(v, label='UV') for v in (uvs[i] if uvs else (0, 0))]
                    remap[i] = vertex('VERT', position(point, 'render position') +
                                      vector(normals[i], 'normal', True) + uv)
                for triangle in triangles:
                    self.append('TRIS', [remap[i] for i in triangle]+[material])
        collision_nodes = rows(self.doc['collision_nodes'], 4096, 'collision_nodes', True)
        seen = set()
        for selector in collision_nodes:
            obj(selector, ('node', 'surface', 'respawn', 'gravity_zone', 'owner'), ('node', 'surface'), 'collision selector')
            if selector['node'] in seen:
                fail('collision_nodes', 'duplicate node selector')
            seen.add(selector['node'])
            surface = integer(selector['surface'],0,self.counts['surfaces']-1,'collision surface')
            respawn = index(selector.get('respawn'), self.counts['respawns'], 'collision respawn')
            zone = index(selector.get('gravity_zone'), self.counts['zones'], 'collision gravity zone')
            owner = index(selector.get('owner'), self.counts['objects'], 'collision owner')
            for points, _, _, triangles, _ in self.gltf.primitives(selector['node']):
                if len(self.tables['COLT'])//32+len(triangles)>262144:
                    fail('collision geometry','vertex/triangle cap exceeded')
                if owner != ABSENT:
                    placement = self.doc['objects'][owner]
                    matrix = node_matrix({'translation': placement['position'], 'rotation': placement.get('quaternion', [0,0,0,1]),
                                          'scale': placement.get('scale', [1,1,1])})
                    inverse, _ = normal_matrix(matrix)
                    points = [[sum(inverse[r*3+c]*(p[c]-matrix[12+c]) for c in range(3)) for r in range(3)] for p in points]
                self.validate_triangles(points, triangles, selector['node'])
                used = {i for triangle in triangles for i in triangle}
                remap = {i: vertex('COLV', position(p, 'collision position'))
                         for i, p in enumerate(points) if i in used}
                for triangle in triangles:
                    self.append('COLT', [remap[i] for i in triangle]+[surface, 0, respawn, zone, owner])
        for tag, maximum in (('VERT',131072),('COLV',131072),('TRIS',262144),('COLT',262144)):
            count = len(self.tables[tag])//STRIDES[tag]
            if count < (3 if tag in ('VERT','COLV') else 1) or count > maximum:
                fail(tag, 'geometry count outside bounds')

    @staticmethod
    def validate_triangles(points, triangles, label):
        quantized = [position(p, label) for p in points]
        for a, b, c in triangles:
            u = [quantized[b][i]-quantized[a][i] for i in range(3)]
            v = [quantized[c][i]-quantized[a][i] for i in range(3)]
            if not any((u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])):
                fail(label, 'degenerate triangle after Q16 conversion')

    def meta(self):
        keys = ('name','author','cup_name','modes','laps','cup_id','cup_order','music','preview','minimap','sky',
                'sky_rgba','fog_rgba','fog_near','fog_far','ambient_rgba','sun_direction','sun_rgba',
                'gravity','physics_profile','properties','sort_key')
        meta = obj(self.doc['meta'], keys, ('name','modes','laps','cup_id','cup_order'), 'meta')
        modes = rows(meta['modes'], 5, 'meta.modes', True)
        if any(mode not in MODES for mode in modes) or len(set(modes)) != len(modes):
            fail('meta.modes', 'unknown or duplicate mode')
        self.mode_bits = sum(MODES[mode] for mode in modes)
        if self.mode_bits != 16 and not self.counts['checkpoints']:
            fail('checkpoints', 'lap-capable courses require checkpoints')
        self.cup = integer(meta['cup_id'], label='meta.cup_id')
        self.slot = integer(meta['cup_order'], label='meta.cup_order')
        music = obj(meta.get('music', {'kind':ABSENT,'id':ABSENT}), ('kind','id'), ('kind','id'), 'meta.music')
        music_kind, music_id = integer(music['kind']), integer(music['id'])
        if (music_kind == ABSENT) != (music_id == ABSENT):
            fail('meta.music','kind and ID must both be absent or both present')
        if music_kind != ABSENT:
            integer(music_kind,1,ABSENT-1,'meta.music.kind')
            integer(music_id,1,ABSENT-1,'meta.music.id')
        artwork = [integer(meta.get(key,ABSENT),1,ABSENT,'meta.'+key) for key in ('preview','minimap','sky')]
        near = fixed(meta.get('fog_near', 0), low=0, label='fog_near')
        far = fixed(meta.get('fog_far', 512), low=0, label='fog_far')
        if far <= near:
            fail('meta.fog', 'far must exceed near')
        self.append('META', [self.string(meta['name']),self.string(meta.get('author','')),
            self.string(meta.get('cup_name','')),self.mode_bits,integer(meta['laps'],0 if self.mode_bits == 16 else 1,99,'meta.laps'),
            self.cup,self.slot,music_kind,music_id,
            *artwork,
            color(meta.get('sky_rgba','#5080B0FF'),'sky_rgba'),color(meta.get('fog_rgba','#5080B0FF'),'fog_rgba'),
            near,far,color(meta.get('ambient_rgba','#808080FF'),'ambient_rgba'),
            *vector(meta.get('sun_direction',[0,-1,0]),'sun_direction',True),
            color(meta.get('sun_rgba','#FFFFFFFF'),'sun_rgba'),fixed(meta.get('gravity',DEFAULT_GRAVITY),low=0,label='gravity'),
            integer(meta.get('physics_profile',0)),0,*self.properties(meta.get('properties',[])),*[0]*6])

    def surfaces(self):
        for surface in self.doc['surfaces']:
            keys = ('type','friction','speed_ratio','rolling_resistance','restitution','boost','launch','gravity_ratio','event','properties')
            obj(surface, keys, ('type',), 'surface')
            if surface['type'] not in SURFACES:
                fail('surface.type', 'unknown surface')
            scalars = [fixed(surface.get(key,default),low=0,high=high,label='surface.'+key)
                       for key,default,high in (('friction',1,32768),('speed_ratio',1,32768),
                       ('rolling_resistance',0,32768),('restitution',0,1),('boost',0,32768),
                       ('launch',0,32768),('gravity_ratio',1,32768))]
            self.append('SURF',[SURFACES.index(surface['type']),0,*scalars,
                               integer(surface.get('event',0)),*self.properties(surface.get('properties',[]))])

    @staticmethod
    def graph(edges, start, label, closed=True):
        reached, visiting, pending = set(), set(), [(start,False)]
        while pending:
            node, leaving = pending.pop()
            if leaving:
                visiting.remove(node)
            elif node in visiting:
                if not closed: fail(label,'cycle requires a declared closed route')
            elif node not in reached:
                reached.add(node);visiting.add(node);pending.append((node,True))
                for successor in reversed(edges[node]):
                    integer(successor,0,len(edges)-1,label+' successor')
                    pending.append((successor,False))
        if len(reached) != len(edges):
            fail(label,'unreachable graph node')

    def checkpoints(self):
        edges = []
        for gate in self.doc['checkpoints']:
            obj(gate,('center','normal','right','half_width','half_height','key_group','stage','successors','flags'),
                ('center','normal','right','half_width','half_height','successors'),'checkpoint')
            successors = rows(gate['successors'],16384,'checkpoint successors')
            first = len(self.tables['CPED'])//4
            self.tables['CPED'] += pack_words([integer(v,0,self.counts['checkpoints']-1,'checkpoint successor') for v in successors])
            normal, right = vector(gate['normal'],'gate.normal',True),vector(gate['right'],'gate.right',True)
            if abs(sum(a*b for a,b in zip(normal,right))) > (1<<60)//1024:
                fail('checkpoint','normal and right must be perpendicular')
            self.append('CPNT',position(gate['center'],'gate.center')+normal+right+
                [fixed(gate['half_width'],low=1/65536,label='gate.half_width'),
                 fixed(gate['half_height'],low=1/65536,label='gate.half_height'),
                 integer(gate.get('key_group',0)),integer(gate.get('stage',0)),first,len(successors),
                 integer(gate.get('flags',0),0,7,'gate.flags')])
            edges.append(successors)
        if len(self.tables['CPED'])//4>16384:
            fail('CPED','edge cap exceeded')
        if edges:
            self.graph(edges,0,'checkpoints')

    def routes(self):
        for route in self.doc['routes']:
            obj(route,('kind','nodes','start_node','closed','properties'),('kind','nodes'),'route')
            if route['kind'] not in ROUTES:
                fail('route.kind','unknown route kind')
            nodes = rows(route['nodes'],16384,'route.nodes',True)
            first = len(self.tables['RTND'])//32
            start = integer(route.get('start_node',0),0,len(nodes)-1,'route.start_node')
            if type(route.get('closed',False)) is not bool:
                fail('route.closed','expected boolean')
            edges = []
            for node in nodes:
                obj(node,('position','radius','speed','successors'),('position','radius','speed','successors'),'route node')
                successors = rows(node['successors'],16384,'route successors')
                edge = len(self.tables['RTED'])//4
                self.tables['RTED'] += pack_words([first+integer(v,0,len(nodes)-1,'route successor') for v in successors])
                self.append('RTND',position(node['position'],'route.position')+
                    [fixed(node['radius'],low=1/65536,label='route.radius'),fixed(node['speed'],low=0,label='route.speed'),
                     edge,len(successors),0])
                edges.append(successors)
            self.graph(edges,start,'route',route.get('closed',False))
            self.append('RTHD',[ROUTES.index(route['kind']),first,len(nodes),first+start,int(route.get('closed',False)),
                                *self.properties(route.get('properties',[])),0])
        if len(self.tables['RTND'])//32>16384 or len(self.tables['RTED'])//4>16384:
            fail('routes','node/edge cap exceeded')

    def placements(self):
        for grid in self.doc['grid']:
            obj(grid,('position','quaternion'),('position',),'grid')
            self.append('GRID',position(grid['position'],'grid.position')+quaternion(grid.get('quaternion',[0,0,0,1]),'grid.quaternion')+[0])
        for item in self.doc.get('items',[]):
            obj(item,('position','radius','yaw','respawn_ticks','properties'),('position','radius','respawn_ticks'),'item')
            self.append('ITEM',position(item['position'],'item.position')+
                [fixed(item['radius'],low=1/65536,label='item.radius'),integer(item.get('yaw',0),0,65535,'item.yaw'),
                 integer(item['respawn_ticks'],1,60000,'item.respawn_ticks'),*self.properties(item.get('properties',[]))])
        for placed in self.doc.get('objects',[]):
            obj(placed,('class_id','model','position','quaternion','scale','route','properties'),('class_id','model','position'),'object')
            model = obj(placed.get('model',{'kind':ABSENT,'id':ABSENT}),('kind','id'),('kind','id'),'object.model')
            scale = [fixed(v,low=1/65536,label='object.scale') for v in array(placed.get('scale',[1,1,1]),3,'object.scale')]
            self.append('OBJS',[integer(placed['class_id']),integer(model['kind'],1,ABSENT-1,'object.model.kind'),
                integer(model['id'],1,ABSENT-1,'object.model.id'),
                *position(placed['position'],'object.position'),*quaternion(placed.get('quaternion',[0,0,0,1]),'object.quaternion'),
                scale[0],scale[2],scale[1],index(placed.get('route'),self.counts['routes'],'object.route'),
                *self.properties(placed.get('properties',[]))])
        for respawn in self.doc.get('respawns',[]):
            obj(respawn,('position','quaternion','checkpoint','radius','route'),('position','checkpoint','radius'),'respawn')
            self.append('RESP',position(respawn['position'],'respawn.position')+
                quaternion(respawn.get('quaternion',[0,0,0,1]),'respawn.quaternion')+
                [index(respawn['checkpoint'],self.counts['checkpoints'],'respawn.checkpoint')
                 if self.mode_bits == 16 else integer(respawn['checkpoint'],0,self.counts['checkpoints']-1,'respawn.checkpoint'),
                 fixed(respawn['radius'],low=1/65536,label='respawn.radius'),0,
                 index(respawn.get('route'),self.counts['routes'],'respawn.route'),0])
        for camera in self.doc.get('cameras',[]):
            obj(camera,('position','quaternion','fov','near','far','route','target_object','priority','properties'),
                ('position','fov','near','far'),'camera')
            near, far = fixed(camera['near'],low=1/65536,label='camera.near'),fixed(camera['far'],low=1/65536,label='camera.far')
            if near>=far:
                fail('camera','near must be less than far')
            self.append('CAMR',position(camera['position'],'camera.position')+
                quaternion(camera.get('quaternion',[0,0,0,1]),'camera.quaternion')+
                [integer(camera['fov'],1,32767,'camera.fov'),near,far,
                 index(camera.get('route'),self.counts['routes'],'camera.route'),
                 index(camera.get('target_object'),self.counts['objects'],'camera.target_object'),0,
                 integer(camera.get('priority',0)),*self.properties(camera.get('properties',[]))])
        for zone in self.doc.get('zones',[]):
            obj(zone,('center','half_extents','quaternion','gravity','kind','priority','event','properties'),
                ('center','half_extents','gravity','kind'),'zone')
            extents = [fixed(v,low=1/65536,high=4096,label='zone.half_extents') for v in array(zone['half_extents'],3,'zone.half_extents')]
            self.append('ZONE',position(zone['center'],'zone.center')+[extents[0],extents[2],extents[1]]+
                quaternion(zone.get('quaternion',[0,0,0,1]),'zone.quaternion')+vector(zone['gravity'],'zone.gravity')+
                [integer(zone['kind']),0,integer(zone.get('priority',0)),integer(zone.get('event',0)),
                 *self.properties(zone.get('properties',[])),0])

    def emit(self):
        self.textures();self.materials();self.meta();self.surfaces()
        self.checkpoints();self.routes();self.placements();self.geometry()
        tags = [tag for tag in STRIDES if tag in REQUIRED or self.tables[tag]]
        directory, offset = bytearray(), 64+20*len(tags)
        body = bytearray()
        for tag in tags:
            payload = self.tables[tag]
            directory += struct.pack('<4s4I',tag.encode('ascii'),offset,len(payload)//STRIDES[tag],STRIDES[tag],int(tag in REQUIRED))
            body += payload
            padding = (-len(payload))%4
            body += bytes(padding)
            offset += len(payload)+padding
        if offset>MAX_BLOB:
            fail('course','32 MiB blob cap exceeded')
        header = struct.pack('<4s15I',b'C3D1',1,offset,self.id,0,64,len(tags),*[0]*9)
        catalog = struct.pack('<4s9I',b'C3C1',1,1,24,self.id,self.cup,self.slot,self.mode_bits,
                              integer(self.doc['meta'].get('sort_key',self.slot)),0)
        return [({'kind':99,'content_id':self.id,'version':1,'flags':3},header+directory+body),
                ({'kind':100,'content_id':self.id,'version':1,'flags':3},catalog),*self.texture_sections]


def compile(document, source_root):
    try:
        return CourseCompiler(document,Path(source_root).resolve()).emit()
    except (KeyError, IndexError, TypeError, struct.error) as error:
        raise CourseError(f'course.json: malformed source field ({error})') from None
