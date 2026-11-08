#!/usr/bin/env python3
"""Convert imported 2D course content into editable, installable 3D mod sources.

The original pack is read-only. Geometry and modern physics live exclusively in
the generated 3D mod, compiled by the ordinary mod kit.
"""
import argparse
from collections import defaultdict
from dataclasses import replace
from fractions import Fraction
import json
import math
from pathlib import Path
import shutil
import struct

import course_3d_art as art
import course_3d_gameplay as gameplay
import course_3d_format
from course_3d_gltf import AssetIds, Scene, argb_png, write_json
import course_3d_source as source_format
import course_3d_terrain as terrain_format
import course_3d_handpass as handpass_format
import mod_kit
import model_format
import driver_affine_format
import mesh_coin_format
import mesh_kart_format
import mesh_source_actor_format
import mesh_sky_format
import mesh_palette_format
import track_format

ROOT = Path(__file__).resolve().parents[2]


def surface(code, jump_max_distance=None):
    """Explicit modern adaptation; source byte remains available to hand passes."""
    result = dict(type='road',friction=1,speed_ratio=1)
    if 0x80 <= code < 0x90:
        result.update(type='wall',restitution=.2)
    elif code == 0x5a:
        result.update(type='off-road',speed_ratio=.6,friction=.8)
    elif code < 0x20:
        kind = code&15
        if kind in (0,2,14):
            result.update(type='jump',launch=course_3d_format.source_launch(kind))
        elif kind == 6:
            result.update(type='boost',boost=course_3d_format.SOURCE_PANEL_BOOST)
        elif kind in (8,12):
            result.update(type='slippery',friction=.3)
    elif 0x20 <= code < 0x30:
        result.update(type={0x22:'water',0x24:'lava',0x2a:'road'}.get(code,'out-of-bounds'))
    result['properties'] = [{'key':'source:material','type':'integer','value':code}]
    if code == 0x2a:
        result['properties'].append(dict(key='mksc:capture',type='boolean',value=True))
    if jump_max_distance is not None:
        result['properties'].append({'key':'mksc:jump-max-distance','type':'fixed',
                                     'value':jump_max_distance})
    if 0x40 <= code < 0x80:
        result['properties'].append({'key':'source:ground-row','type':'integer','value':(code>>1)&15})
    return result


def terrain_scene(source, terrain, scene, document, regions, identities, scale, jump_limits=None,
                  palette_bindings=None):
    images = [scene.texture(t.width,t.height,t.pixels,identities,
              **({'identity_extra':t.indices} if t.indices is not None else {})) for t in terrain.textures]
    if palette_bindings is not None:
        for image,texture in zip(images,terrain.textures):
            if texture.indices is not None:
                palette_bindings.append((101,scene.texture_mappings[image]['id'],0,
                                         texture.width,texture.height,texture.indices))
    for material in terrain.materials:
        scene.material(material.argb,None if material.texture is None else images[material.texture],
                       material.repeat,material.cutout)
    document['render_nodes'].append(scene.mesh('Terrain',terrain.vertices,terrain.triangles))
    groups = defaultdict(list)
    for a,b,c,code in terrain.collision_triangles:
        center = [sum(terrain.collision_vertices[i][axis] for i in (a,b,c))/3 for axis in (0,2)]
        tx = max(0,min(source.map_width-1,math.floor(center[0]/(scale*8))))
        ty = max(0,min(source.map_height-1,math.floor(-center[1]/(scale*8))))
        recovery = regions[ty*source.map_width+tx]
        ceiling = ((jump_limits or {}).get((tx,ty))
                   if code < 0x20 and code&15 in (0,2,14) else None)
        groups[code,recovery,ceiling].append((a,b,c,0))
    collision = [(*p,0,1,0,0,0) for p in terrain.collision_vertices]
    keys = sorted({(code,ceiling) for code,_,ceiling in groups},
                  key=lambda key:(key[0],-1 if key[1] is None else key[1]))
    document['surfaces'] = [surface(code,ceiling) for code,ceiling in keys]
    indices = {key:i for i,key in enumerate(keys)}
    for code,recovery,ceiling in sorted(groups,
            key=lambda key:(key[0],key[1],-1 if key[2] is None else key[2])):
        triangles = groups[code,recovery,ceiling]
        identity = indices[code,ceiling]
        name = scene.mesh(f'Collision-{code:02x}-{recovery}-{identity}',collision,triangles)
        document['collision_nodes'].append(dict(node=name,surface=identity,respawn=recovery))


class Models:
    def __init__(self, directory, identities, pier_haunt_images=None):
        self.directory,self.identities = Path(directory),identities
        self.materials,self.meshes = {},{}
        self.pier_haunt_images=pier_haunt_images

    def material(self, image, *, wrap_u=False, wrap_v=False):
        canonical = struct.pack('<2I',image.width,image.height)+struct.pack(
            f'<{len(image.pixels)}I',*image.pixels)
        # Keep every existing clamp material ID unchanged. Wrapped sampling is
        # part of the immutable material identity, not merely its image bytes.
        if wrap_u or wrap_v:
            canonical=b'WRAP'+bytes((int(wrap_u),int(wrap_v)))+canonical
        if image.indices is not None:canonical+=b'IDX1'+image.indices
        material = self.identities.intern(97,canonical)
        path = f'art-{material}.png'
        (self.directory/path).write_bytes(argb_png(image.width,image.height,image.pixels))
        self.materials[material] = dict(id=material,argb=0xffffffff,texture=path)
        if wrap_u or wrap_v:self.materials[material].update(wrap_u=wrap_u,wrap_v=wrap_v)
        return material

    def image(self, image):
        material = self.material(image)
        # Model coordinates use engine XYZ, unlike the glTF road source.
        left,right = image.anchor_x,image.anchor_x+image.width
        bottom,top = -image.anchor_y-image.height,-image.anchor_y
        vertices = [[left,0,bottom,0,1],[right,0,bottom,1,1],
                    [right,0,top,1,0],[left,0,top,0,0]]
        descriptor = dict(materials=[material],vertices=vertices,
                          triangles=[[0,1,2,0],[0,2,3,0]])
        mesh = self.identities.intern(96,json.dumps(descriptor,sort_keys=True).encode())
        self.meshes[mesh] = dict(descriptor,id=mesh)
        return mesh

    def coin_tile(self, pixels, *, indices=None):
        """Own the exact RAD1 C0 tile on an upward engine-XY floor quad."""
        material=self.material(art.PixelImage(8,8,0,0,tuple(pixels),indices))
        descriptor=dict(materials=[material],vertices=[[-4,-4,0,0,0],[4,-4,0,1,0],
            [4,4,0,1,1],[-4,4,0,0,1]],triangles=[[0,1,2,0],[0,2,3,0]])
        mesh=self.identities.intern(96,json.dumps(descriptor,sort_keys=True).encode())
        self.meshes[mesh]=dict(descriptor,id=mesh)
        return mesh

    def save(self):
        if self.meshes:
            write_json(self.directory/'models.json',dict(version=1,
                materials=[self.materials[k] for k in sorted(self.materials)],
                meshes=[self.meshes[k] for k in sorted(self.meshes)],bindings=[]))




def coins(source,document,models,scale, *, palette_bindings=None):
    """One shared coin per imported C0 cell above a derived C1 road tile."""
    placements=getattr(source,'coins',())
    if not placements:return 0
    if len(source.tiles_argb)<=0xc0 or len(source.tiles_argb[0xc0])!=64:
        raise ValueError(f'course={source.course_id} coins: missing RAD1 C0 tile')
    indices=source.tiles_indices[0xc0] if palette_bindings is not None else None
    mesh=models.coin_tile(source.tiles_argb[0xc0],**({'indices':indices} if indices is not None else {}))
    if palette_bindings is not None:
        material=models.meshes[mesh]['materials'][0]
        palette_bindings.append((97,material,0,8,8,indices))
    seen=set()
    for order,p in enumerate(placements):
        cell=(p.x//8,p.y//8)
        if p.x%8 or p.y%8 or not (0<=cell[0]<source.map_width and 0<=cell[1]<source.map_height):
            raise ValueError(f'course={source.course_id} coins: invalid source cell')
        if cell in seen:continue
        seen.add(cell)
        identity=cell[1]*source.map_width+cell[0]+1
        properties=[dict(key='mksc:controller',type='string',value='coins'),
            dict(key='mksc:controller-version',type='integer',value=1),
            dict(key='coin:id',type='integer',value=identity),
            dict(key='source:placement',type='integer',value=order),
            dict(key='source:parameter',type='integer',value=p.parameter)]
        properties.extend(dict(key='coin:half-'+axis,type='fixed',value=4*scale) for axis in ('x','y','z'))
        document['objects'].append(dict(class_id=0x20000001,model=dict(kind=96,id=mesh),
            position=[(p.x+4)*scale,.005,-(p.y+4)*scale],scale=[scale]*3,properties=properties))
    return len(seen)

def coin_road_tiles(source):
    """Restore the native consumed-coin C1 artwork without mutating source/COLT.

    Only genuine CIN1 cells whose original top is C0 are replaced. Pickup art
    retains the original C0 pixels in a separate owner-controlled floor quad.
    """
    tiles=bytearray(source.tilemap)
    for coin in getattr(source,'coins',()):
        x,y=coin.x//8,coin.y//8
        if coin.x%8 or coin.y%8 or not (0<=x<source.map_width and 0<=y<source.map_height):
            raise ValueError(f'course={source.course_id} coins: invalid source cell')
        index=y*source.map_width+x
        if tiles[index]==0xc0:
            if len(source.tiles_argb)<=0xc1:
                raise ValueError(f'course={source.course_id} coins: missing RAD1 C1 tile')
            tiles[index]=0xc1
    return bytes(tiles)


def rock_placements(source):
    """Validate controller-owned SCN1 A2 rows without treating them as scenery."""
    placements=[p for p in art.decode_scenery(source).placements if p.source_code>>16==0xa2]
    for p in placements:
        if (p.source_code&0xffff)>3 or p.parameter>=128:
            raise ValueError(f'course={source.course_id} rocks: invalid variant/checkpoint')
    return placements

def teepee_placements(source):
    """SCN1 class03 is a required tent/rider controller, never static scenery."""
    placements=[p for p in art.decode_scenery(source).placements if p.source_code>>16==3]
    for p in placements:
        if p.source_code != 0x030000 or p.parameter >= 128:
            raise ValueError(f'course={source.course_id} teepees: invalid variant/checkpoint')
    return placements


COMMON_PROP_VARIANTS = {6:1,8:0,10:7,12:11,0x86:0,0x98:1}
# Source inert constructors have no update callback or tile-list insertion.
# Match portable/core/course_prop.c; other codes must not become decorations.
INERT_SCENERY_VARIANTS = {1:1,2:0,7:0,11:1,0x85:0,0x8c:0,0x90:0,0x9a:0,0x9b:0}


def common_prop_placements(source):
    """Required interactive SCN1 constructors; inert scenery is not promoted."""
    placements=[p for p in art.decode_scenery(source).placements
                if p.source_code>>16 in COMMON_PROP_VARIANTS]
    for p in placements:
        if (p.source_code&65535)>COMMON_PROP_VARIANTS[p.source_code>>16] or p.parameter>=128:
            raise ValueError(f'course={source.course_id} common_props: invalid code/checkpoint')
    return placements


def mechakoopa_placements(source):
    """SCN1 A1 keeps source walking state rather than baked static sizing."""
    placements=[p for p in art.decode_scenery(source).placements if p.source_code>>16==0xa1]
    for p in placements:
        if (p.source_code&65535)>1 or p.parameter>=128:
            raise ValueError(f'course={source.course_id} mechakoopas: invalid variant/checkpoint')
    return placements


def kamek_placements(source):
    placements=[p for p in art.decode_scenery(source).placements if p.source_code>>16==0xa0]
    for p in placements:
        if p.source_code!=0xa00000 or p.parameter>=128:
            raise ValueError(f'course={source.course_id} kamek: invalid variant/checkpoint')
    return placements


def pier_boo_placements(source):
    placements=[p for p in art.decode_scenery(source).placements if p.source_code>>16==0xa3]
    for p in placements:
        if p.source_code!=0xa30000 or p.parameter>=128:
            raise ValueError(f'course={source.course_id} pier_boos: invalid variant/checkpoint')
    return placements


def scenery(source, document, models, scale):
    decoded = art.decode_scenery(source)
    interactive=set(COMMON_PROP_VARIANTS)|{3,0xa0,0xa1,0xa2,0xa3}
    # Check the complete list before emitting any image/model/object. A late
    # unsupported constructor must not publish an earlier static substitute.
    for placement in decoded.placements:
        family=placement.source_code>>16
        if family in interactive:continue
        maximum=INERT_SCENERY_VARIANTS.get(family)
        label=(f'course={source.course_id} SCN1 code=0x{placement.source_code:08x} '
               f'placement={placement.placement_order}')
        if maximum is None:
            raise ValueError(f'{label}: unsupported source controller')
        if (placement.source_code&65535)>maximum:
            raise ValueError(f'{label}: invalid inert variant')
    banks = [models.image(bank.image) for bank in decoded.banks]
    controllers=0
    for placement in decoded.placements:
        if placement.source_code>>16 in (3,0xa0,0xa1,0xa2,0xa3) or placement.source_code>>16 in COMMON_PROP_VARIANTS:
            code_family=placement.source_code>>16
            pier_boo=code_family==0xa3
            kamek=code_family==0xa0
            mechakoopa=code_family==0xa1
            teepee=code_family==3
            common=code_family in COMMON_PROP_VARIANTS
            family='pier_boos' if pier_boo else 'kamek' if kamek else 'mechakoopas' if mechakoopa else 'common_props' if common else 'teepees' if teepee else 'rocks'
            maximum=0 if kamek or pier_boo else 1 if mechakoopa else COMMON_PROP_VARIANTS[code_family] if common else 0 if teepee else 3
            if (placement.source_code&0xffff)>maximum or placement.parameter>=128:
                raise ValueError(f'course={source.course_id} {family}: invalid variant/checkpoint')
            properties=[dict(key='mksc:billboard',type='boolean',value=True),
                dict(key='mksc:controller',type='string',value=family),
                dict(key='mksc:controller-version',type='integer',value=1)]
            values={'x-q16':placement.x_q16,'y-q16':placement.y_q16,
                'variant':placement.source_code&0xffff,'checkpoint':placement.parameter,
                'placement-order':placement.placement_order,
                'frame-count':3 if pier_boo else 2 if kamek else 4 if mechakoopa else (2 if code_family==0x86 else 1) if common else 11 if teepee else 5,
                'affine-q8':placement.affine_q8}
            if common:values['code']=placement.source_code
            properties.extend(dict(key='source:'+k,type='integer',value=v) for k,v in values.items())
            frame_ids=[banks[bank] for bank in placement.frame_bank_indices]
            if pier_boo:
                if models.pier_haunt_images is None:
                    raise ValueError(f'course={source.course_id} pier_boos: missing global FXC1 haunt artwork')
                frame_ids.extend(models.image(image) for image in models.pier_haunt_images)
            properties.extend(dict(key=f'source:frame:{i}',type='asset',value=dict(kind=96,id=model))
                              for i,model in enumerate(frame_ids))
            document['objects'].append(dict(class_id=0x1008 if pier_boo else 0x101b if kamek else 0x101c if mechakoopa else 0x1019 if common else 0x1018 if teepee else 0x101d,model=dict(kind=96,id=banks[placement.art_index]),
                position=gameplay.position(placement.x_q16/65536,placement.y_q16/65536,scale),
                scale=[scale]*3,properties=properties))
            controllers+=1
            continue
        factor = scale*256/placement.affine_q8
        properties = [{'key':'mksc:billboard','type':'boolean','value':True},
                      {'key':'source:placement','type':'integer','value':placement.placement_order},
                      {'key':'source:parameter','type':'integer','value':placement.parameter}]
        if len(placement.frame_bank_indices)>1:
            properties.append({'key':'source:frame-models','type':'string',
                'value':','.join(str(banks[i]) for i in placement.frame_bank_indices)})
        document['objects'].append(dict(class_id=placement.source_code,
            model={'kind':96,'id':banks[placement.art_index]},
            position=gameplay.position(placement.x_q16/65536,placement.y_q16/65536,
                                       scale,placement.height_q16/65536*scale),
            scale=[factor]*3,properties=properties))

    return controllers


def actors(source, document, models, scale):
    """Emit decoded controllers, refusing nonempty unsupported families."""
    count = 0
    supported={'crabs':(4,0x1003),'snowmen':(2,0x1012),'penguins':(6,0x1016),'mice':(2,0x1015),'piranhas':(5,0x1009),'seagulls':(3,0x100c),'lava_bubbles':(2,0x1004),'thwomps':(1,0x1002),'cannonballs':(7,0x1013),'boos':(2,0x1007),'falling_stars':(5,0x1011),'thunderclouds':(72,0x1010)}
    families=art.decode_actors(source)
    for family in families:
        if family.placements and family.family not in supported:
            raise ValueError(f'course={source.course_id} {family.family}: unsupported actor controller')
    for family in families:
        if not family.placements:continue
        frames = [models.image(bank.image) for bank in family.banks]
        frame_count,class_id=supported[family.family]
        if len(frames) != frame_count:
            raise ValueError(f'course={source.course_id} {family.family}: expected {frame_count} frame banks')
        for placement in family.placements:
            properties = [dict(key='mksc:billboard',type='boolean',value=True),
                dict(key='mksc:controller',type='string',value=family.family),
                dict(key='mksc:controller-version',type='integer',value=1)]
            values = {'x-q16':placement.x_q16,'y-q16':placement.y_q16,
                'variant':placement.variant,'checkpoint':placement.checkpoint,
                'placement-order':placement.placement_order,'frame-count':frame_count}
            properties.extend(dict(key='source:'+key,type='integer',value=value)
                              for key,value in values.items())
            properties.extend(dict(key=f'source:frame:{i}',type='asset',
                value=dict(kind=96,id=identity)) for i,identity in enumerate(frames))
            document['objects'].append(dict(class_id=class_id,
                model=dict(kind=96,id=frames[4 if family.family=='thunderclouds' else 2 if family.family=='crabs' and placement.variant>=0x8000 else 0]),
                position=gameplay.position(placement.x_q16/65536,placement.y_q16/65536,scale),
                scale=[scale]*3,properties=properties))
            count += 1
    return count


def sky(source, scene, document, identities, scale, *, models=None, profiles=None, sine=None, decoded=None, palette_bindings=None):
    decoded = art.decode_sky(source) if decoded is None else decoded
    if decoded is None:
        return
    motion = decoded.motion
    dynamic=motion is not None and motion.controller in (1,2,3,4,5,6,7)
    if dynamic and (models is None or profiles is None or sine is None):
        raise ValueError(f'course={source.course_id} sky controller={motion.controller}: missing sky-profile context')
    layers=[]
    center_x,center_z = source.width*scale/2,-source.height*scale/2
    for logical in range(3):
        if motion is not None and not motion.layer_mask&(1<<logical):
            continue
        bank = motion.layer_source[logical] if motion is not None else logical
        image = decoded.layers[bank].image
        visible = [y for y in range(image.height)
                   if any(p>>24 for p in image.pixels[y*image.width:(y+1)*image.width])]
        if not visible:
            continue
        first,last = visible[0],visible[-1]+1
        cropped = image.pixels[first*image.width:last*image.width]
        if dynamic:
            indices=image.indices[first*image.width:last*image.width] if image.indices is not None else None
            if motion.controller==4 and (indices is None or palette_bindings is None):
                raise ValueError(f'course={source.course_id} sunset sky: missing indexed palette context')
            material=models.material(art.PixelImage(image.width,last-first,0,0,tuple(cropped),indices),wrap_u=True)
            if indices is not None and palette_bindings is not None:
                palette_bindings.append((97,material,1,image.width,last-first,indices))
        else:
            texture = scene.texture(image.width,last-first,cropped,identities)
            material = scene.material(image=texture,repeat=(True,False),cutout=True)
        shift = motion.yaw_shift[logical] if motion is not None else 6+logical
        repeat = 65536/((1<<shift)*image.width)
        if motion is not None and motion.controller == 2:
            # SPEED_PAN's front is translation-driven; retain its initial
            # panorama without inventing hundreds of yaw repetitions.
            repeat = 1 if logical == 0 else repeat*(2 if logical==1 else 1)
        bias_x = motion.x_bias[logical] if motion is not None else 0
        bias_y = motion.y_bias[logical] if motion is not None else 38
        horizon = 74+bias_y
        radius = 1024+logical*8
        bottom,top = (horizon-last)*radius/136,(horizon-first)*radius/136
        vertices,triangles = [],[]
        for i in range(65):
            theta = i*math.tau/64
            sin_theta,cos_theta = math.sin(theta),math.cos(theta)
            u = i*repeat/64+(120+bias_x)/image.width
            for height,v in ((bottom,1),(top,0)):
                vertices.append((center_x+sin_theta*radius,height,center_z+cos_theta*radius,
                                 -sin_theta,0,-cos_theta,u,v))
        for i in range(64):
            a = i*2
            triangles.extend(((a,a+1,a+3,material),(a,a+3,a+2,material)))
        if dynamic:
            # Ordinary models use engine XYZ. Build local coordinates directly:
            # subtracting a course center would introduce hash-only float drift.
            descriptor=dict(materials=[material],
                vertices=[[math.sin((i//2)*math.tau/64)*radius,
                           -math.cos((i//2)*math.tau/64)*radius,v[1],v[6],v[7]]
                          for i,v in enumerate(vertices)],
                triangles=[[a,b,c,0] for a,b,c,_ in triangles])
            # Canonicalize before both hashing and publishing: libm sin/cos
            # can differ by an ULP across hosts despite identical MSH1 bytes.
            descriptor['vertices']=[[q/65536 for q in model_format.quantize_vertex(v)]
                                    for v in descriptor['vertices']]
            mesh=identities.intern(96,json.dumps(descriptor,sort_keys=True).encode())
            models.meshes[mesh]=dict(descriptor,id=mesh)
            document['objects'].append(dict(class_id=0x20000002,model=dict(kind=96,id=mesh),
                position=[center_x,0,center_z],properties=[dict(key='mksc:sky-layer',type='integer',value=logical)]))
            layers.append(dict(layer=logical,logical_width=image.width,radius_q16=radius*65536))
        else:
            document['render_nodes'].append(scene.mesh(f'Sky-{logical}',vertices,triangles))
    if dynamic and layers:
        rules={key:list(getattr(motion,key)) if key in mesh_sky_format.TRIPLES else getattr(motion,key)
               for key in mesh_sky_format.FIELDS}
        try:
            record=dict(profile_id=1,revision=1,entry_delay=motion.controller==3,rules=rules,
                        layers=layers,sine=list(sine() if callable(sine) else sine))
            payload=mesh_sky_format.normalize(record)
        except ValueError as error:
            raise ValueError(f'course={source.course_id} sky controller={motion.controller}: {error}') from None
        identity=identities.intern(106,payload)
        record['profile_id']=identity
        profiles[identity]=record
        document['meta'].setdefault('properties',[]).append(
            dict(key='mksc:sky-motion',type='asset',value=dict(kind=106,id=identity)))
    # The initial clear color belongs to the source sky palette; alpha-zero
    # cutout layers reveal it rather than an unrelated generic blue backdrop.
    if decoded.palette:
        color = decoded.palette[decoded.background_index%len(decoded.palette)]
        # Decoder preserves source BGR555 palette words.
        r,g,b = color&31,(color>>5)&31,(color>>10)&31
        document['meta']['sky_rgba'] = ((r*8+r//4)<<24)|((g*8+g//4)<<16)|((b*8+b//4)<<8)|255


def _collision_partitions(source, regions, jump_limits):
    """Intern merge-only (RESP, encoded launch distance) ownership per cell.

    Terrain merges collision rectangles before terrain_scene assigns SURF by
    centroid. Every emitted coefficient boundary must therefore partition that
    merge; these IDs never replace the actual RESP ownership metadata.
    """
    if len(regions)!=source.map_width*source.map_height:
        raise ValueError(f'course={source.course_id} collision partition: region count mismatch')
    identities={}
    result=[]
    for cell,region in enumerate(regions):
        code=source.materials[source.tilemap[cell]]
        value=jump_limits.get((cell%source.map_width,cell//source.map_width))
        ceiling=(course_3d_format.fixed(value,low=1/65536,label='jump-max-distance')
                 if value is not None and code<0x20 and code&15 in (0,2,14) else None)
        key=(region,ceiling)
        if key not in identities:
            identities[key]=len(identities)
        result.append(identities[key])
    return result


def palette_profile(source, decoded_sky, bindings, directory, identities, models, profiles):
    """Own exact AMB1/index files and bind the existing seed RGBA resources."""
    ambient=[s for s in source.passthrough if s.kind==84]
    if len(ambient)!=1 or ambient[0].version!=1:
        raise ValueError(f'course={source.course_id} sunset: missing/duplicate or invalid AMB1')
    directory=Path(directory)
    relative=directory.relative_to(models.directory)
    (directory/'ambient.bin').write_bytes(ambient[0].data)
    unique={}
    for kind,identity,bank,width,height,indices in bindings:
        key=kind,identity
        value=bank,width,height,bytes(indices)
        if key in unique and unique[key]!=value:
            raise ValueError(f'course={source.course_id} sunset: conflicting palette binding {key}')
        unique[key]=value
    rows=[]
    for (kind,identity),(bank,width,height,indices) in sorted(unique.items()):
        name=f'palette-{kind}-{identity}.idx'
        (directory/name).write_bytes(indices)
        rows.append(dict(kind=kind,id=identity,bank=bank,width=width,height=height,
                         indices=(relative/name).as_posix()))
    record=dict(profile_id=1,revision=1,road_seed=list(source.palette),
        sky_seed=list(decoded_sky.palette)+(64-len(decoded_sky.palette))*[0],
        ambient=(relative/'ambient.bin').as_posix(),bindings=rows)
    try:payload=mesh_palette_format.normalize(record,models.directory)
    except (OSError,ValueError) as error:
        raise ValueError(f'course={source.course_id} sunset: {error}') from None
    identity=identities.intern(107,payload)
    record['profile_id']=identity
    profiles[identity]=record
    return identity


HANDPASS_ROUTE_HEIGHT_TOLERANCE_Q16 = 1024  #1/64 metre, navigation only.
HANDPASS_ROUTE_WORK_LIMIT = 4194304


def lift_handpass(document, field, source, scale):
    """Lift generated anchors and route edges over the same sampled terrain.

    Original route node indices and checkpoint identities stay stable. New
    route guides append to the table and only subdivide existing edges. Their
    Q16 heights approximate all sampled breakpoints within1/64m; terrain and
    original anchors remain exact. Work is bounded across every route/edge.
    """
    work = 0
    def spend():
        nonlocal work
        work += 1
        if work > HANDPASS_ROUTE_WORK_LIMIT:
            raise ValueError(f'course={source.course_id} hand pass: route guide work budget exceeded')

    def simplify(samples):
        # Each entry is (exact binary input fraction, original lifted point,
        # encoded XYZ). Iterative RDP preserves first-max ties and both ends.
        keep = {0,len(samples)-1}
        pending = [(0,len(samples)-1)]
        while pending:
            left,right = pending.pop()
            t0,_,a = samples[left]
            t1,_,b = samples[right]
            maximum,chosen = Fraction(HANDPASS_ROUTE_HEIGHT_TOLERANCE_Q16),None
            for i in range(left+1,right):
                spend()
                t,_,point = samples[i]
                expected = a[2]+(b[2]-a[2])*(t-t0)/(t1-t0)
                error = abs(point[2]-expected)
                if error > maximum:
                    maximum,chosen = error,i
            if chosen is not None:
                keep.add(chosen)
                pending.extend(((chosen,right),(left,chosen)))
        return [samples[i][1:] for i in sorted(keep)[1:-1]]

    def lift(position):
        x, y, z = position
        return [x, y + field.sample_world(x, z), z]

    for route in document['routes']:
        original = route['nodes']
        nodes = [dict(node, position=lift(node['position']), successors=[])
                 for node in original]
        raised = any(a['position'][1] != b['position'][1]
                     for a,b in zip(original,nodes))
        for i,node in enumerate(original):
            for successor in node['successors']:
                a,b=node['position'],original[successor]['position']
                sampled=[(t,[a[k]+(b[k]-a[k])*t for k in range(3)])
                         for t in field.edge_parameters(a,b)[1:-1]]
                # A flat unaffected edge retains its exact original encoding.
                if not any(field.sample_world(p[0],p[2]) for _,p in sampled) and \
                    not field.sample_world(a[0],a[2]) and not field.sample_world(b[0],b[2]):
                    nodes[i]['successors'].append(successor)
                    continue
                raised=True
                start = course_3d_format.position(nodes[i]['position'],'hand-pass route')
                end = course_3d_format.position(nodes[successor]['position'],'hand-pass route')
                samples = [(Fraction(0),nodes[i]['position'],start)]
                for t,point in sampled:
                    spend()
                    lifted=lift(point)
                    encoded=course_3d_format.position(lifted,'hand-pass route')
                    # Near-coincident grid/diagonal crossings can become the
                    # same Q16 point. Never publish a zero-length CPU edge.
                    if encoded == samples[-1][2] or encoded == end:
                        continue
                    fraction = Fraction(t)
                    if not samples[-1][0] < fraction < 1:
                        raise ValueError(f'course={source.course_id} hand pass: unordered route samples')
                    samples.append((fraction,lifted,encoded))
                samples.append((Fraction(1),nodes[successor]['position'],end))
                previous=i
                for lifted,_ in simplify(samples):
                    if len(nodes)>=16384:
                        raise ValueError(f'course={source.course_id} hand pass: route node cap exceeded')
                    index=len(nodes)
                    nodes[previous]['successors'].append(index)
                    nodes.append(dict(position=lifted, radius=min(node['radius'],original[successor]['radius'],.8),
                                      speed=min(node['speed'],original[successor]['speed']), successors=[]))
                    previous=index
                nodes[previous]['successors'].append(successor)
        route['nodes']=nodes
        if raised:
            route['properties']=[p for p in route.get('properties',[])
                                 if p['key']!='mksc:cpu-ground-route']
    if sum(len(r['nodes']) for r in document['routes'])>16384 or \
       sum(len(n['successors']) for r in document['routes'] for n in r['nodes'])>16384:
        raise ValueError(f'course={source.course_id} hand pass: route node/edge cap exceeded')
    for key in ('grid','items','respawns','cameras'):
        for placed in document[key]:placed['position']=lift(placed['position'])
    for gate in document['checkpoints']:gate['center']=lift(gate['center'])
    for placed in document['objects']:
        if not any(p['key']=='mksc:sky-layer' for p in placed.get('properties',[])):
            placed['position']=lift(placed['position'])


def convert(source, directory, identities, models, *, scale=.1, gp=False, maximum_speed=36,
            route_profile=None, actor_profile=None, affine_profile=None, coin_profile=1,
            sky_profiles=None, sky_sine=None, palette_profiles=None, handpass=None):
    decoded_sky = art.decode_sky(source)
    sunset=decoded_sky is not None and decoded_sky.motion is not None and decoded_sky.motion.controller==4
    if sunset:
        if source.tiles_indices is None or palette_profiles is None:
            raise ValueError(f'course={source.course_id} sunset: missing indexed source/profile context')
        decoded_sky=art.decode_sky(source,preserve_indices=True)
    bindings=[] if sunset else None
    progress_sky = decoded_sky is not None and decoded_sky.motion is not None and decoded_sky.motion.controller in (2,4,5)
    jump_limits = {}
    document,regions = gameplay.derive(source,scale=scale,physics_profile=1,item_profile=1,
                                       jump_limits=jump_limits,maximum_speed=maximum_speed,
                                       route_profile=route_profile,progress_provenance=progress_sky)
    if not gp and 'gp' in document['meta']['modes']:
        document['meta']['modes'].remove('gp')
    terrain = terrain_format.encode(source.map_width,source.map_height,source.tilemap,
        source.tiles_argb,source.materials,units_per_pixel=scale,
        collision_regions=_collision_partitions(source,regions,jump_limits),
        render_tilemap=coin_road_tiles(source),
        wall_height=gameplay.converted_wall_height(scale),
        **({'elevation_grid':handpass.elevation_grid} if handpass is not None else {}),
        **({'indexed_tiles':source.tiles_indices} if sunset else {}))
    scene = Scene(directory)
    terrain_scene(source,terrain,scene,document,regions,identities,scale,jump_limits,
                  **({'palette_bindings':bindings} if sunset else {}))
    if any(p['key']=='mksc:capture' and p['value']
           for row in document['surfaces'] for p in row.get('properties',[])):
        forward=min(36,(route_profile or {}).get('forward_speed_q16',24*65536)/65536)
        upward=(0x2f0/256)*scale*262144/4389
        for name,value in (('forward',forward),('up',upward)):
            encoded=course_3d_format.fixed(value,low=0,high=256,label='capture-'+name+'-speed')
            document['meta']['properties'].append(dict(key='mksc:capture-'+name+'-speed',
                type='fixed',value=encoded/65536))
    document['meta']['properties'].append(dict(key='mksc:coin-rules',type='asset',
                                               value=dict(kind=105,id=coin_profile)))
    coins(source,document,models,scale,**({'palette_bindings':bindings} if sunset else {}))
    scenery_controllers=scenery(source,document,models,scale)
    if actors(source,document,models,scale) or scenery_controllers:
        if actor_profile is None:
            raise ValueError(f'course={source.course_id} source actors: missing source actor rules')
        document['meta']['properties'].append(
            dict(key='mksc:actor-rules',type='asset',value=dict(kind=104,id=actor_profile)))
    if any(p.get('key')=='mksc:controller' and p.get('value')=='thwomps'
           for obj in document['objects'] for p in obj.get('properties',[])):
        if affine_profile is None:
            raise ValueError(f'course={source.course_id} thwomps: missing actor affine rules')
        document['meta']['properties'].append(dict(key='mksc:actor-affine-rules',type='asset',
                                                   value=dict(kind=81,id=affine_profile)))
    sky(source,scene,document,identities,scale,models=models,profiles=sky_profiles,sine=sky_sine,
        decoded=decoded_sky,palette_bindings=bindings)
    if sunset:
        if not any(p['key']=='mksc:sky-motion' for p in document['meta']['properties']):
            raise ValueError(f'course={source.course_id} sunset: no rendered sky layer for motion owner')
        identity=palette_profile(source,decoded_sky,bindings,directory,identities,models,palette_profiles)
        document['meta']['properties'].append(dict(key='mksc:palette-motion',type='asset',
                                                   value=dict(kind=107,id=identity)))
    if handpass is not None and any(handpass.grid):
        lift_handpass(document,handpass,source,scale)
    if handpass is not None and handpass.additions:
        handpass_format.mesh_additions.apply(handpass.additions,scene,document,identities,handpass)
    document['textures'] = scene.texture_mappings
    scene.save()
    write_json(Path(directory)/'course.json',document)
    return document


def catalog_courses(sections):
    identities = []
    for (kind,catalog),(_,payload) in sorted(sections.items()):
        if kind == 37:
            identities.extend(row['course'] for row in mod_kit.catalog_rows(sections,catalog))
    if len(set(identities)) != len(identities):
        raise ValueError('conversion catalog: duplicate course identities')
    return identities


def read_conversion_source(sections, identity, *, name=None, overlay=()):
    """Resolve the source identity used by conversion and authored hand passes.

    Sunset palette controllers retain original road texel indices. Hand-pass
    fingerprints must bind this same representation, including preserved indices.
    """
    course=source_format.read_course(sections,identity,name=name,overlay=overlay)
    motion=track_format.sky_motion(course.sky) if course.sky is not None else None
    if motion is not None and motion['controller']==4:
        course=source_format.read_course(sections,identity,preserve_indices=True,name=name,overlay=overlay)
    return course


def generate(base, directory, *, courses=(), tracks=(), scale=.1, mod_id='converted-courses',
             handpass=None, export_handpass=None):
    if type(scale) not in (int,float) or not math.isfinite(scale) or not .02 <= scale <= 2:
        raise ValueError('conversion scale: expected finite metres per source unit in .02..2')
    if not courses and not tracks:
        raise ValueError('conversion: select courses, --all or track folders')
    base,directory = Path(base),Path(directory)
    sections = track_format.load_pack(base)
    names = {row['id']:row['name'] for row in json.loads(
        (ROOT/'portable/data/menu-names.json').read_text())['courses']}
    selected = [read_conversion_source(sections,identity,name=names.get(identity)) for identity in courses]
    custom_count = 0
    for track in tracks:
        definition = json.loads((Path(track)/'track.json').read_text())
        overlay = track_format.compile_track(track,sections)
        course = read_conversion_source(sections,definition['id'],overlay=overlay,name=definition['name'])
        if not course.catalog_id:
            course = replace(course,catalog_id=mod_kit.CUSTOM_CATALOG,
                             cup=custom_count//4,slot=custom_count%4)
            custom_count += 1
        selected.append(replace(course,song=definition['song']))
    if len({course.course_id for course in selected}) != len(selected):
        raise ValueError('conversion: duplicate selected course IDs')
    fields={} if handpass is None else handpass_format.load(handpass,selected,scale)
    if export_handpass is not None:
        handpass_format.export(export_handpass,selected,scale)
    if directory.exists() and any(directory.iterdir()):
        raise ValueError('conversion source: choose a fresh empty directory')
    directory.mkdir(parents=True,exist_ok=True)
    write_json(directory/'mod.json',dict(id=mod_id,name='Converted 3D Courses',version='1.0.0',
                                       api_version=1,targets=['3d']))
    physics = json.loads((ROOT/'portable/data/mesh-kart.json').read_text())
    # Some source roads are only one eight-unit tile wide. Preserve their
    # geometry and modern handling while fitting the collider inside that lane.
    physics['kart_radius_q16'] = min(physics['kart_radius_q16'],round(scale*2.5*65536))
    write_json(directory/'physics.json',physics)
    # Planners read one stock racer's resolved rules (150cc, base levels).
    planner = mesh_kart_format.resolved(physics)
    item_path = ROOT/'portable/data/mesh-items.json'
    item_rules = json.loads(item_path.read_text())['profiles']
    boost_ratio = max([planner['panel_cap_ratio_q16']/65536,
                       *(ratio/65536 for ratio in planner['boost_cap_ratio_q16']),
                       *(profile[kind]['cap_ratio'] for profile in item_rules
                         for kind in ('mushroom','star'))])
    maximum_speed = planner['forward_speed_q16']/65536*boost_ratio
    shutil.copyfile(item_path,directory/'items.json')
    identities = AssetIds()
    pier_courses=[course.course_id for course in selected if pier_boo_placements(course)]
    pier_images=None
    if pier_courses:
        try:pier_images=art.decode_pier_haunt(sections[19,1])
        except (KeyError,ValueError) as error:
            raise ValueError(f'course={pier_courses[0]} pier_boos: missing/invalid global haunt art: {error}') from None
    models = Models(directory,identities,pier_images)
    coin_rules=mesh_coin_format.default_profile()
    coin_profile=identities.intern(105,mesh_coin_format.normalize(coin_rules))
    coin_rules['profile_id']=coin_profile
    write_json(directory/'coins.json',dict(version=1,profiles=[coin_rules]))
    actor_profile = None
    if any(any(family.family in ('crabs','snowmen','penguins','mice','piranhas','seagulls','lava_bubbles','thwomps','cannonballs','boos','falling_stars','thunderclouds') and family.placements
               for family in art.decode_actors(course)) or rock_placements(course) or teepee_placements(course) or common_prop_placements(course) or mechakoopa_placements(course) or kamek_placements(course) or pier_boo_placements(course) for course in selected):
        sine = mesh_source_actor_format.imported_sine(sections)
        actor_profile = identities.intern(104,struct.pack('<I1024i',1,*sine))
        source_rules = dict(version=1,profiles=[dict(profile_id=actor_profile,revision=1,sine=sine)])
        mesh_source_actor_format.compile(source_rules)
        write_json(directory/'source-actors.json',source_rules)

    affine_profile=None
    thwomp_courses=[c.course_id for c in selected if any(
        f.family=='thwomps' and f.placements for f in art.decode_actors(c))]
    if thwomp_courses:
        try:
            record=driver_affine_format.imported(sections)
            affine_profile=identities.intern(81,driver_affine_format.normalize(record))
            record['profile_id']=affine_profile
            write_json(directory/'actor-affine.json',dict(version=1,profiles=[record]))
        except ValueError as error:
            raise ValueError(f'course={thwomp_courses[0]} thwomps: {error}') from None

    sky_profiles={}
    palette_profiles={}
    sky_sine=None
    def imported_sky_sine():
        nonlocal sky_sine
        if sky_sine is None:
            sky_sine=mesh_source_actor_format.imported_sine(sections)
        return sky_sine

    cups = defaultdict(list)
    for course in selected:
        if not course.arena:
            cups[course.cup_id].append(course.slot)
    complete = {cup for cup,slots in cups.items() if sorted(slots)==[0,1,2,3]}
    for course in sorted(selected,key=lambda c:c.course_id):
        convert(course,directory/'courses3d'/str(course.course_id),identities,models,scale=scale,
                gp=course.cup_id in complete,maximum_speed=maximum_speed,route_profile=planner,
                actor_profile=actor_profile,affine_profile=affine_profile,coin_profile=coin_profile,
                sky_profiles=sky_profiles,sky_sine=imported_sky_sine,palette_profiles=palette_profiles,
                handpass=fields.get(course.course_id))
    if sky_profiles:
        write_json(directory/'skies.json',dict(version=1,profiles=[sky_profiles[k] for k in sorted(sky_profiles)]))
    if palette_profiles:
        write_json(directory/'palettes.json',dict(version=1,profiles=[palette_profiles[k] for k in sorted(palette_profiles)]))
    models.save()
    return directory


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base',type=Path,required=True)
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--all',action='store_true')
    group.add_argument('--course',type=int,action='append',default=[])
    parser.add_argument('--track',type=Path,action='append',default=[])
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--mod-id',default='converted-courses')
    parser.add_argument('--scale',type=float,default=.1)
    parser.add_argument('--handpass',type=Path,help='separate fingerprinted Blender handpass.json')
    parser.add_argument('--export-handpass',type=Path,help='fresh folder for flat editable elevation guides')
    args = parser.parse_args(argv)
    try:
        courses = catalog_courses(track_format.load_pack(args.base)) if args.all else args.course
        source = generate(args.base,args.source,courses=courses,tracks=args.track,
                          scale=args.scale,mod_id=args.mod_id,handpass=args.handpass,
                          export_handpass=args.export_handpass)
        folder,archive = mod_kit.build(source,None,args.output)
        print(f'Converted 3D mod: {folder} archive={archive}')
    except (OSError,ValueError) as error:
        parser.exit(1,f'conversion: {error}\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
