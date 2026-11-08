"""Write deterministic, editable glTF sources for generated mesh courses.

Geometry arrives in glTF metres. Each material primitive stores only its used
vertices, avoiding repeated decoding of a whole terrain accessor at compilation.
"""
import hashlib
import json
from pathlib import Path
import struct
import zlib


def write_json(path, document):
    Path(path).write_text(json.dumps(document, separators=(',', ':'), allow_nan=False)+'\n')


def argb_png(width, height, pixels):
    """Encode exact ARGB pixels as an RGBA PNG without palette conversion."""
    if not 0 < width <= 4096 or not 0 < height <= 4096 or width*height != len(pixels):
        raise ValueError('converted texture: invalid dimensions/pixel count')
    def chunk(kind, data):
        return struct.pack('>I', len(data))+kind+data+struct.pack('>I', zlib.crc32(kind+data)&0xffffffff)
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for color in pixels[y*width:(y+1)*width]:
            raw.extend(((color >> 16)&255, (color >> 8)&255, color&255, (color >> 24)&255))
    return (b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>2I5B', width,height,8,6,0,0,0))+
            chunk(b'IDAT', zlib.compress(raw,9))+chunk(b'IEND', b''))


class AssetIds:
    """Stable content IDs, shared across all courses in one companion mod."""
    def __init__(self):
        self.payloads = {}

    def intern(self, kind, payload):
        payload = bytes(payload)
        identity = int.from_bytes(hashlib.sha256(struct.pack('<I',kind)+payload).digest()[:4],'little')
        if identity in (0, 0xffffffff):
            identity = 1
        while (kind,identity) in self.payloads:
            if self.payloads[kind,identity] == payload:
                return identity
            identity = identity+1 if identity < 0xfffffffe else 1
        self.payloads[kind,identity] = payload
        return identity


class Scene:
    def __init__(self, directory):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.binary = bytearray()
        self.document = dict(asset={'version':'2.0','generator':'MKSC Neo course converter'},
                             scene=0, scenes=[{'nodes':[]}], nodes=[], meshes=[],
                             buffers=[], bufferViews=[], accessors=[], images=[], textures=[],
                             samplers=[{'wrapS':10497,'wrapT':10497},
                                       {'wrapS':33071,'wrapT':33071}], materials=[])
        self.texture_mappings = []
        self.image_ids = {}

    def texture(self, width, height, pixels, identities, *, identity_extra=None):
        wire = argb_png(width,height,pixels)
        canonical = struct.pack('<2I',width,height)+b''.join(struct.pack('<I',color) for color in pixels)
        # Equal seed RGBA may conceal distinct animated palette indices. Keep
        # that optional ownership in identity without changing the PNG bytes.
        if identity_extra is not None:
            canonical+=b'EXTRA'+bytes(identity_extra)
        identity = identities.intern(101, canonical)
        if identity in self.image_ids:
            return self.image_ids[identity]
        image = len(self.document['images'])
        filename = f'texture-{identity}.png'
        (self.directory/filename).write_bytes(wire)
        self.document['images'].append({'uri':filename})
        self.texture_mappings.append({'image':image,'id':identity})
        self.image_ids[identity] = image
        return image

    def material(self, argb=0xffffffff, image=None, repeat=False, cutout=False):
        pbr = dict(baseColorFactor=[((argb >> shift)&255)/255 for shift in (16,8,0,24)],
                   metallicFactor=0, roughnessFactor=1)
        if image is not None:
            wraps = (bool(repeat),bool(repeat)) if isinstance(repeat,bool) else repeat
            sampler = dict(wrapS=10497 if wraps[0] else 33071,
                           wrapT=10497 if wraps[1] else 33071)
            if sampler not in self.document['samplers']:
                self.document['samplers'].append(sampler)
            texture = len(self.document['textures'])
            self.document['textures'].append({'source':image,
                'sampler':self.document['samplers'].index(sampler)})
            pbr['baseColorTexture'] = {'index':texture}
        material = dict(pbrMetallicRoughness=pbr, doubleSided=False,
                        extensions={'KHR_materials_unlit':{}})
        if cutout:
            material.update(alphaMode='MASK',alphaCutoff=1/65536)
        index = len(self.document['materials'])
        self.document['materials'].append(material)
        return index

    def accessor(self, values, shape, component=5126):
        width = {'SCALAR':1,'VEC2':2,'VEC3':3}[shape]
        while len(self.binary)%4:
            self.binary.append(0)
        offset = len(self.binary)
        for row in values:
            self.binary.extend(struct.pack('<'+('f' if component == 5126 else 'I')*width,
                                           *(row if width != 1 else (row,))))
        views, accessors = self.document['bufferViews'], self.document['accessors']
        views.append({'buffer':0,'byteOffset':offset,'byteLength':len(self.binary)-offset})
        accessors.append({'bufferView':len(views)-1,'componentType':component,
                          'count':len(values),'type':shape})
        return len(accessors)-1

    def mesh(self, name, vertices, triangles):
        """Vertices: (XYZ, normalXYZ, UV) flattened; triangles: (a,b,c,material)."""
        groups = {}
        for a,b,c,material in triangles:
            groups.setdefault(material,[]).append((a,b,c))
        primitives = []
        for material, faces in groups.items():
            remap, compact, indices = {}, [], []
            for triangle in faces:
                for index in triangle:
                    if index not in remap:
                        remap[index] = len(compact)
                        compact.append(vertices[index])
                    indices.append(remap[index])
            attributes = {semantic:self.accessor([v[start:start+count] for v in compact], shape)
                          for semantic,start,count,shape in (('POSITION',0,3,'VEC3'),
                          ('NORMAL',3,3,'VEC3'),('TEXCOORD_0',6,2,'VEC2'))}
            primitives.append({'attributes':attributes,'indices':self.accessor(indices,'SCALAR',5125),
                               'material':material})
        if not primitives:
            raise ValueError(f'{name}: empty generated mesh')
        mesh = len(self.document['meshes'])
        self.document['meshes'].append({'primitives':primitives})
        node = len(self.document['nodes'])
        self.document['nodes'].append({'name':name,'mesh':mesh})
        self.document['scenes'][0]['nodes'].append(node)
        return name

    def save(self):
        self.document['buffers'] = [{'uri':'scene.bin','byteLength':len(self.binary)}]
        (self.directory/'scene.bin').write_bytes(self.binary)
        write_json(self.directory/'scene.gltf',self.document)
