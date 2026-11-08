"""Bounded static glTF additions for fingerprinted hand passes.

Preparation owns immutable decoded data. Applying stages all owner changes and
new image files before publishing; existing scene/resource indices stay stable.
An optional ground anchor translates the complete addition rigidly, including
its roof, rather than draping vertices over the height field.
"""
from copy import deepcopy
from dataclasses import dataclass
import json
import math
from pathlib import Path
import struct
import tempfile

import course_3d_format as wire
import model_format

MAX_ADDITIONS = 32
MAX_BYTES = 32*1024*1024
MAX_VERTICES = 131072
MAX_TRIANGLES = 262144
MAX_MATERIALS = 256
MAX_IMAGES = 128


class MeshError(ValueError):
    pass


def _fail(message):
    raise MeshError('handpass meshes: '+message)


def _json(value):
    return json.dumps(value,sort_keys=True,separators=(',',':'),allow_nan=False)


@dataclass(frozen=True)
class Primitive:
    vertices: tuple
    triangles: tuple
    material: int


@dataclass(frozen=True)
class PreparedAddition:
    render: tuple
    collision: tuple
    surfaces: tuple
    materials: tuple
    textures: tuple
    images: tuple
    anchor: object


def _validator(gltf, surfaces, counts=None):
    # Reuse the actual wire compiler's material/SURF admission and encoding.
    compiler = wire.CourseCompiler.__new__(wire.CourseCompiler)
    compiler.gltf = gltf
    compiler.doc = {'surfaces':surfaces}
    compiler.counts = counts or {'objects':4096,'routes':16384}
    compiler.tables = {tag:bytearray() for tag in wire.STRIDES}
    compiler.strings = {}
    compiler.texture_ids = {i:i+1 for i in range(len(gltf.doc.get('images',[])))}
    if gltf.doc.get('materials'):
        compiler.materials()
    compiler.surfaces()
    return compiler


def _primitive(gltf, name, render=True, counts=None):
    result = []
    for points,normals,uvs,triangles,material in gltf.primitives(name):
        if not triangles:
            _fail(f'{name}: empty primitive')
        if counts is not None:
            counts[0] += len(points)
            counts[1] += len(triangles)
            if counts[0]>MAX_VERTICES or counts[1]>MAX_TRIANGLES:
                _fail('combined addition geometry cap exceeded')
        wire.CourseCompiler.validate_triangles(points,triangles,name)
        if normals is None:
            normals = [[0.,0.,0.] for _ in points]
            for a,b,c in triangles:
                u = [points[b][i]-points[a][i] for i in range(3)]
                v = [points[c][i]-points[a][i] for i in range(3)]
                normal = [u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
                for index in (a,b,c):
                    normals[index] = [normals[index][i]+normal[i] for i in range(3)]
        used = {i for face in triangles for i in face}
        for i in used:
            if render:
                wire.vector(normals[i],'addition normal',True)
            elif not any(normals[i]):
                normals[i] = [0.,1.,0.]
        vertices = []
        for i,point in enumerate(points):
            if i not in used:
                continue
            uv = uvs[i] if uvs else (0.,0.)
            wire.position(point,'addition position')
            for value in uv:
                wire.fixed(value,label='addition UV')
            vertices.append(tuple((*point,*normals[i],*uv)))
        remap = {old:new for new,old in enumerate(sorted(used))}
        result.append(Primitive(tuple(vertices),tuple(tuple(remap[i] for i in t) for t in triangles),material))
    if not result:
        _fail(f'{name}: empty mesh')
    return tuple(result)


def _image(gltf, image):
    if 'uri' in image:
        if set(image)-{'uri','name','mimeType'}:
            _fail('unsupported image declaration')
        path = gltf.resource_path(image['uri'])
        return path.read_bytes(), path
    if set(image)-{'bufferView','mimeType','name'} or image.get('mimeType') != 'image/png':
        _fail('embedded image must be PNG')
    views = gltf.doc.get('bufferViews',[])
    view = views[wire.integer(image.get('bufferView'),0,len(views)-1,'image view')]
    buffer = gltf.buffers[wire.integer(view.get('buffer'),0,len(gltf.buffers)-1,'image buffer')]
    start = wire.integer(view.get('byteOffset',0),0,len(buffer),'image offset')
    length = wire.integer(view.get('byteLength'),1,len(buffer)-start,'image length')
    if 'byteStride' in view:
        _fail('strided embedded image')
    return buffer[start:start+length],None


def _material_keys(material):
    allowed = {'name','pbrMetallicRoughness','normalTexture','emissiveTexture','emissiveFactor',
               'alphaMode','alphaCutoff','doubleSided','extensions'}
    wire.obj(material,allowed,(),'addition material')
    pbr = material.get('pbrMetallicRoughness',{})
    wire.obj(pbr,('baseColorFactor','baseColorTexture','metallicRoughnessTexture','metallicFactor','roughnessFactor'),(),'addition PBR')
    normal = material.get('normalTexture',{})
    wire.obj(normal,('index','texCoord','scale'),(),'addition normal texture')
    if normal.get('scale',1) != 1:
        _fail('normal texture scale is unsupported by C3D')
    for info in (pbr.get('baseColorTexture'),pbr.get('metallicRoughnessTexture'),
                 material.get('normalTexture'),material.get('emissiveTexture')):
        if info is not None:
            wire.obj(info,('index','texCoord','scale') if info is normal else ('index','texCoord'),
                     ('index',),'addition texture info')


def load(root, rows):
    """Prepare at most 32 closed-schema additions, confining every resource."""
    if type(rows) is not list or len(rows)>MAX_ADDITIONS:
        _fail('expected at most 32 mesh additions')
    root = Path(root).resolve()
    result, resources = [], {}
    geometry_counts = [[0,0],[0,0]]
    materials_count = image_count = decoded_bytes = 0
    def resource(path):
        path = path.resolve()
        resources[path] = path.stat().st_size
        if sum(resources.values()) > MAX_BYTES:
            _fail('combined source resource limit exceeds 32 MiB')
    try:
        for ordinal,row in enumerate(rows):
            wire.obj(row,('scene','render_nodes','collision_nodes','surfaces','ground_anchor'),
                     ('scene','render_nodes','collision_nodes','surfaces'),f'addition {ordinal}')
            resource(wire.local_path(root,row['scene']))
            class BoundedGltf(wire.Gltf):
                def resource_path(self, uri):
                    path = super().resource_path(uri)
                    # Gltf calls this before reading each external buffer; the
                    # image decoder uses the same hook before any PNG read.
                    resource(path)
                    return path
            gltf = BoundedGltf(root,row['scene'])
            renders = wire.rows(row['render_nodes'],4096,'addition render nodes')
            collisions = wire.rows(row['collision_nodes'],4096,'addition collision nodes')
            surfaces = wire.rows(row['surfaces'],256,'addition surfaces')
            if not renders and not collisions:
                _fail('addition requires render or collision nodes')
            if any(type(n) is not str for n in renders) or len(set(renders)) != len(renders):
                _fail('invalid or duplicate render selector')
            names = []
            for selector in collisions:
                wire.obj(selector,('node','surface','respawn'),('node','surface'),'addition collision selector')
                wire.integer(selector['surface'],0,len(surfaces)-1,'local surface')
                if 'respawn' in selector:
                    wire.integer(selector['respawn'],0,1023,'existing respawn')
                if type(selector['node']) is not str or selector['node'] in names:
                    _fail('invalid or duplicate collision selector')
                names.append(selector['node'])
            materials = gltf.doc.get('materials',[])
            if type(materials) is not list or materials_count+len(materials)>MAX_MATERIALS:
                _fail('combined material cap exceeded')
            image_rows = wire.rows(gltf.doc.get('images',[]),MAX_IMAGES,'addition images')
            if image_count+len(image_rows)>MAX_IMAGES:
                _fail('combined image cap exceeded')
            wire.rows(gltf.doc.get('textures',[]),1024,'addition textures')
            for material in materials:
                _material_keys(material)
            _validator(gltf,surfaces)
            images = []
            for image in image_rows:
                data,path = _image(gltf,image)
                payload = model_format._texture_bytes(data,'addition image',4096,4000000)
                width,height = struct.unpack_from('<HH',payload,12)
                offset = struct.unpack_from('<I',payload,24)[0]
                decoded_bytes += width*height*4
                if decoded_bytes > MAX_BYTES:
                    _fail('combined decoded image limit exceeds 32 MiB')
                images.append((width,height,tuple(struct.unpack_from(f'<{width*height}I',payload,offset))))
            texture_rows = gltf.doc.get('textures',[])
            for texture in texture_rows:
                if set(texture)-{'source','sampler','name'}:
                    _fail('unsupported texture declaration')
                wire.integer(texture.get('source'),0,len(images)-1,'texture source')
            # The wire validator checks all referenced samplers and wrap modes.
            textures = []
            for texture in texture_rows:
                sampler = gltf.doc.get('samplers',[])[texture['sampler']] if 'sampler' in texture else {}
                if set(sampler)-{'wrapS','wrapT','magFilter','minFilter','name'}:
                    _fail('unsupported sampler declaration')
                textures.append((texture['source'],_json(sampler)))
            render = tuple((name,_primitive(gltf,name,True,geometry_counts[0])) for name in renders)
            collision = tuple((selector['node'],selector['surface'],selector.get('respawn'),
                               _primitive(gltf,selector['node'],False,geometry_counts[1])) for selector in collisions)
            for _,primitives in render:
                for primitive in primitives:
                    wire.integer(primitive.material,0,len(materials)-1,'render material')
            materials_count += len(materials)
            image_count += len(images)
            if any(v>MAX_VERTICES or t>MAX_TRIANGLES for v,t in geometry_counts) or materials_count>MAX_MATERIALS or image_count>MAX_IMAGES:
                _fail('combined geometry/material/image cap exceeded')
            anchor = row.get('ground_anchor')
            if anchor is not None:
                wire.array(anchor,2,'ground anchor')
                for value in anchor:
                    wire.fixed(value,label='ground anchor')
                anchor = tuple(anchor)
            result.append(PreparedAddition(render,collision,tuple(_json(s) for s in surfaces),
                          tuple(_json(m) for m in materials),tuple(textures),tuple(images),anchor))
        return tuple(result)
    except (OSError,ValueError,KeyError,TypeError,IndexError,AttributeError,struct.error) as exc:
        if isinstance(exc,MeshError):
            raise
        raise MeshError(f'handpass meshes: {exc}') from exc


def _apply(additions, scene, document, identities, field=None):
    """Append prepared geometry/resources transactionally; empty additions are inert."""
    if not additions:
        return
    if len(additions)>MAX_ADDITIONS or any(not isinstance(a,PreparedAddition) for a in additions):
        _fail('invalid prepared additions')
    staged, doc, ids = deepcopy(scene),deepcopy(document),deepcopy(identities)
    with tempfile.TemporaryDirectory(prefix='.mesh-additions-',dir=scene.directory) as temporary:
        staged.directory = Path(temporary)
        for ordinal,addition in enumerate(additions):
            offset = 0 if addition.anchor is None or field is None else field.sample_world(*addition.anchor)
            wire.fixed(offset,label='ground anchor elevation')
            image_map = [staged.texture(w,h,pixels,ids) for w,h,pixels in addition.images]
            texture_map = []
            for source,sampler_json in addition.textures:
                sampler = json.loads(sampler_json)
                samplers = staged.document['samplers']
                if sampler not in samplers:
                    samplers.append(sampler)
                texture_map.append(len(staged.document['textures']))
                staged.document['textures'].append({'source':image_map[source],'sampler':samplers.index(sampler)})
            material_base = len(staged.document['materials'])
            for raw in addition.materials:
                material = json.loads(raw)
                pbr = material.get('pbrMetallicRoughness',{})
                for info in (pbr.get('baseColorTexture'),pbr.get('metallicRoughnessTexture'),
                             material.get('normalTexture'),material.get('emissiveTexture')):
                    if info is not None:
                        info['index'] = texture_map[info['index']]
                staged.document['materials'].append(material)
            surface_base = len(doc['surfaces'])
            doc['surfaces'].extend(json.loads(s) for s in addition.surfaces)
            def mesh(name, primitives, kind):
                existing = {n.get('name') for n in staged.document['nodes']}
                target = f'Handpass-{ordinal}-{kind}-{name}'
                if target in existing:
                    _fail('mesh namespace already exists')
                vertices,faces = [],[]
                for primitive in primitives:
                    base = len(vertices)
                    for x,y,z,nx,ny,nz,u,v in primitive.vertices:
                        point = [x,y+offset,z]
                        wire.position(point,'lifted addition position')
                        vertices.append((*point,nx,ny,nz,u,v))
                    faces.extend((*[base+i for i in face],material_base+primitive.material)
                                 for face in primitive.triangles)
                staged.mesh(target,vertices,faces)
                return target
            for name,primitives in addition.render:
                doc['render_nodes'].append(mesh(name,primitives,'render'))
            for name,surface,respawn,primitives in addition.collision:
                selector = dict(node=mesh(name,primitives,'collision'),surface=surface_base+surface)
                if respawn is not None:
                    if respawn >= len(doc.get('respawns',[])):
                        _fail('collision respawn is not an existing course respawn')
                    selector['respawn'] = respawn
                doc['collision_nodes'].append(selector)
        if len(doc['surfaces'])>256 or len(staged.document['materials'])>256 or len(staged.texture_mappings)>128:
            _fail('composed surface/material/texture cap exceeded')
        # Run the production geometry encoder against staged buffers. Its
        # complete encoded-record welding, rather than accessor-count sums,
        # determines both render and collision caps.
        staged.save()
        gltf = wire.Gltf(staged.directory.resolve(),'scene.gltf')
        compiler = _validator(gltf,doc['surfaces'],
                   {key:len(doc.get(key,[])) for key in ('objects','routes','surfaces','respawns','zones')})
        compiler.doc = doc
        compiler.geometry()
        doc['textures'] = deepcopy(staged.texture_mappings)
        # Preserve destination bytes before publishing any image. An atomic
        # rename can fail after previous images were published; roll those back
        # as well as leaving the staged Python owners unpublished.
        images = sorted(p for p in staged.directory.iterdir() if p.suffix == '.png')
        prior, backup_bytes = {}, 0
        for path in images:
            target = scene.directory/path.name
            if target.exists():
                backup_bytes += target.stat().st_size
                if backup_bytes > MAX_BYTES:
                    _fail('image publication backup limit exceeds 32 MiB')
                prior[target] = target.read_bytes()
            else:
                prior[target] = None
        published = []
        try:
            for path in images:
                target = scene.directory/path.name
                path.replace(target)
                published.append(target)
        except OSError:
            for target in reversed(published):
                if prior[target] is None:
                    target.unlink(missing_ok=True)
                else:
                    target.write_bytes(prior[target])
            raise
        staged.directory = scene.directory
        scene.__dict__.clear()
        scene.__dict__.update(staged.__dict__)
        document.clear()
        document.update(doc)
        identities.__dict__.clear()
        identities.__dict__.update(ids.__dict__)


def apply(additions, scene, document, identities, field=None):
    """Append static additions, preserving caller owners on validation failure."""
    try:
        return _apply(additions,scene,document,identities,field)
    except (OSError,ValueError,KeyError,TypeError,IndexError,AttributeError,struct.error) as exc:
        if isinstance(exc,MeshError):
            raise
        raise MeshError(f'handpass meshes: {exc}') from exc
