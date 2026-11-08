#!/usr/bin/env python3
"""Generate an original, ROM-free elevated glTF course and installable 3D mod."""
import argparse
import copy
import json
import importlib.util
import math
from pathlib import Path
import struct
import sys
import zlib

TOOLS = Path(__file__).resolve().parents[3] / 'tools/portable'
sys.path.insert(0, str(TOOLS))
from mod_kit import default_mods_directory
import mod_kit
import mesh_coin_format
import course_3d_format


def checker_png(path):
    def chunk(kind, payload):
        return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload)&0xffffffff)
    pixels = bytearray()
    for y in range(32):
        pixels.append(0)
        for x in range(32):
            pixels.extend((240,218,116,255) if (x//4+y//4)%2 else (35,93,125,255))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',32,32,8,6,0,0,0))+
                     chunk(b'IDAT',zlib.compress(pixels))+chunk(b'IEND',b''))


def cup_item_weights():
    """Rank-aware cup policy, shared by humans and CPUs without donor bias."""
    ids = {'green': 0x1000, 'triple_green': 0x1001, 'red': 0x1002,
           'triple_red': 0x1003, 'spiny': 0x1004, 'banana': 0x1005,
           'mushroom': 0x1007, 'triple_mushroom': 0x1008, 'golden': 0x1009,
           'star': 0x100a, 'boo': 0x100b, 'lightning': 0x100c}
    # Each rank totals100. Leaders defend; the middle gets targeted attacks
    # and speed recovery. Rare field-wide attacks belong only to the rear.
    rows = (
        {'green': 45, 'banana': 40, 'triple_green': 10, 'mushroom': 5},
        {'green': 30, 'banana': 25, 'triple_green': 15, 'red': 15, 'mushroom': 15},
        {'green': 20, 'banana': 15, 'triple_green': 10, 'red': 25, 'mushroom': 25, 'triple_red': 5},
        {'green': 10, 'banana': 10, 'triple_green': 5, 'red': 25, 'mushroom': 30,
         'triple_mushroom': 15, 'boo': 5},
        {'green': 5, 'banana': 5, 'red': 20, 'triple_red': 10, 'mushroom': 30,
         'triple_mushroom': 20, 'star': 5, 'boo': 5},
        {'red': 15, 'triple_red': 10, 'mushroom': 25, 'triple_mushroom': 25,
         'golden': 10, 'star': 8, 'boo': 5, 'spiny': 1, 'lightning': 1},
        {'red': 10, 'triple_red': 10, 'mushroom': 20, 'triple_mushroom': 25,
         'golden': 15, 'star': 12, 'boo': 5, 'spiny': 1, 'lightning': 2},
        {'red': 5, 'triple_red': 10, 'mushroom': 15, 'triple_mushroom': 25,
         'golden': 20, 'star': 15, 'boo': 5, 'spiny': 2, 'lightning': 3},
    )
    return [[[ids[name], weight] for name, weight in row.items()] for row in rows]


def item_profile(cup=False):
    """Authored metre-based tuning for the bridge, independent of source units."""
    awards = [0x1000, 0x1001, 0x1002, 0x1003, 0x1004, 0x1005,
              0x1007, 0x1008, 0x1009, 0x100a, 0x100b, 0x100c]
    return {
        'profile_id': 1, 'revision': 1,
        'actors': {'capacity': 64, 'radius': .25, 'lifetime_ticks': 600,
                   'owner_grace_ticks': 30, 'release_lock_ticks': 4,
                   'hold_distance': 1.25, 'hold_height': .1, 'orbit_radius': 1.5,
                   'orbit_turn_rate': .5, 'orbit_delay_0': 0, 'orbit_delay_1': 4, 'orbit_delay_2': 8},
        'shell': {'speed': 24, 'launch_bonus': 2, 'acceleration': 4, 'restitution': 1,
                  'max_bounces': 7, 'homing_turn_rate': .75, 'homing_range': 24,
                  'ground_probe': 1, 'ground_snap': .25, 'trap_arm_ticks': 10,
                  'trap_lifetime_ticks': 600, 'trap_lock_ticks': 10},
        'banana': {'throw_speed': 8, 'throw_up_speed': 4, 'gravity': 9.8},
        'hit': {'spin_ticks': 40, 'invulnerability_ticks': 60, 'speed_ratio': .25, 'impulse': 2},
        'mushroom': {'boost_ticks': 90, 'acceleration': 65.7, 'cap_ratio': 1.2, 'golden_ticks': 600},
        'star': {'player_ticks': 450, 'cpu_ticks': 450, 'acceleration': 65.7, 'cap_ratio': 1.05},
        'lightning': {'duration_ticks': 180, 'speed_ratio': .5, 'scale_ratio': .5},
        'boo': {'invisibility_ticks': 180, 'theft_lock_ticks': 120},
        'cpu': {'decision_ticks': 20, 'hold_ticks': 40, 'use_distance': 12, 'backward_distance': 8},
        'roulette': {'preview': awards[:10], 'timeline': [2]*27},
        'weights': {domain: (cup_item_weights() if cup else
                             [[[identity, 1] for identity in awards] for _ in range(8)])
                    for domain in ('player', 'cpu')}}


def crab_frames(models):
    """Four original source-unit silhouettes, with their feet at local Z0."""
    models['materials'].extend([
        {'id':10,'argb':'#FFEE6543'},{'id':11,'argb':'#FFFFBC42'},
        {'id':12,'argb':'#FF202534'},{'id':13,'argb':'#FFFFFFFF'}])
    identities=[]
    for frame in range(4):
        vertices,triangles=[],[]
        def rectangle(left,bottom,right,top,material):
            first=len(vertices)
            vertices.extend([[left,0,bottom,0,1],[right,0,bottom,1,1],
                             [right,0,top,1,0],[left,0,top,0,0]])
            triangles.extend([[first,first+1,first+2,material],
                              [first,first+2,first+3,material]])
        body=frame//2
        rectangle(-4,2,4,7,body)
        rectangle(-7,5,-4,8,body);rectangle(4,5,7,8,body)
        rectangle(-7,8,-5,10,body);rectangle(5,8,7,10,body)
        for x in (-5,-2,1,4):
            shift=(frame&1)*((x+5)%2)
            rectangle(x+shift,0,x+shift+1,3,body)
        rectangle(-3,7,-1,9,3);rectangle(1,7,3,9,3)
        rectangle(-2,7,-1,8,2);rectangle(1,7,2,8,2)
        identity=10+frame
        models['meshes'].append(dict(id=identity,materials=[10,11,12,13],
                                    vertices=vertices,triangles=triangles))
        identities.append(identity)
    return identities


def source_actor_rules(root,document):
    # An original math table, not copied source art or an imported motion asset.
    sine=[]
    for index in range(1024):
        value=math.sin(index*math.tau/1024)*32768
        sine.append(math.floor(value+.5) if value>=0 else math.ceil(value-.5))
    (root/'source-actors.json').write_text(json.dumps(dict(version=1,profiles=[
        dict(profile_id=1,revision=1,sine=sine)]),indent=2)+'\n')
    document['meta']['properties'].extend([
        dict(key='mksc:actor-rules',type='asset',value=dict(kind=104,id=1)),
        dict(key='source:metres-per-unit',type='fixed',value=.1)])


def crab_controller(root,document,frames,battle,ground_follow=False):
    source_actor_rules(root,document)
    if ground_follow:
        document['meta']['properties'].append(
            dict(key='mksc:actor-ground-follow',type='integer',value=1))
    # Movement is along the approach, toward the oncoming kart.
    # Its full source cycle remains inside that road's Y0..30m extent.
    properties=[dict(key='mksc:billboard',type='boolean',value=True),
        dict(key='mksc:controller',type='string',value='crabs'),
        dict(key='mksc:controller-version',type='integer',value=1)]
    values={'x-q16':200*65536,'y-q16':60*65536,'variant':1,
            'checkpoint':0,'placement-order':0,'frame-count':4}
    properties.extend(dict(key='source:'+key,type='integer',value=value)
                      for key,value in values.items())
    properties.extend(dict(key=f'source:frame:{i}',type='asset',value=dict(kind=96,id=identity))
                      for i,identity in enumerate(frames))
    document['objects'].append(dict(class_id=0x1003,model=dict(kind=96,id=frames[0]),
        position=[0,0,0] if battle else [20,4,8] if ground_follow else [20,0,-6],
        scale=[.1]*3,properties=properties))


def snowman_frames(models):
    """Original intact/damaged snow silhouettes, anchored at the floor."""
    models['materials'].extend([
        {'id':20,'argb':'#FFEAF8FF'},{'id':21,'argb':'#FF7EDAF2'},
        {'id':22,'argb':'#FF193653'},{'id':23,'argb':'#FFFF9248'}])
    for phase in range(2):
        vertices,triangles=[],[]
        def rectangle(left,bottom,right,top,material):
            first=len(vertices)
            vertices.extend([[left,0,bottom,0,1],[right,0,bottom,1,1],
                             [right,0,top,1,0],[left,0,top,0,0]])
            triangles.extend([[first,first+1,first+2,material],
                              [first,first+2,first+3,material]])
        if phase==0:
            rectangle(-5,0,5,6,0);rectangle(-4,5,4,11,0)
            rectangle(-3,10,3,16,0);rectangle(-4,9,4,11,1)
            rectangle(-4,16,4,17,2);rectangle(-3,17,3,20,2)
            rectangle(-2,13,-1,14,2);rectangle(1,13,2,14,2)
            rectangle(0,11,3,12,3)
        else:
            rectangle(-6,0,6,3,1);rectangle(-4,2,4,6,0)
            rectangle(-3,5,3,10,0);rectangle(-4,10,4,11,2)
            rectangle(-2,7,-1,8,2);rectangle(1,7,2,8,2)
            rectangle(0,5,3,6,3)
        models['meshes'].append(dict(id=20+phase,materials=[20,21,22,23],
                                    vertices=vertices,triangles=triangles))
    return [20,21]


def snowman_controller(root,document,frames,battle,crabs):
    if not crabs:
        source_actor_rules(root,document)
    properties=[dict(key='mksc:billboard',type='boolean',value=True),
        dict(key='mksc:controller',type='string',value='snowmen'),
        dict(key='mksc:controller-version',type='integer',value=1)]
    values={'x-q16':200*65536,'y-q16':60*65536,'variant':0,
            'checkpoint':0,'placement-order':int(crabs),'frame-count':2}
    properties.extend(dict(key='source:'+key,type='integer',value=value)
                      for key,value in values.items())
    properties.extend(dict(key=f'source:frame:{i}',type='asset',value=dict(kind=96,id=identity))
                      for i,identity in enumerate(frames))
    document['objects'].append(dict(class_id=0x1012,model=dict(kind=96,id=frames[0]),
        position=[-7.25,0,-6] if battle else [20,0,-12 if crabs else -6],
        scale=[.1]*3,properties=properties))



def penguin_frames(models):
    """Six original navy/white/orange silhouettes, feet at local Z0."""
    models['materials'].extend([
        {'id':30,'argb':'#FF243B73'},{'id':31,'argb':'#FFF4F8FF'},
        {'id':32,'argb':'#FFFFA342'},{'id':33,'argb':'#FF101A2B'}])
    for phase in range(6):
        vertices,triangles=[],[]
        def rectangle(left,bottom,right,top,material):
            first=len(vertices)
            vertices.extend([[left,0,bottom,0,1],[right,0,bottom,1,1],
                             [right,0,top,1,0],[left,0,top,0,0]])
            triangles.extend([[first,first+1,first+2,material],
                              [first,first+2,first+3,material]])
        sway=(-1,0,1,1,0,-1)[phase]
        wing=(2,3,4,3,2,1)[phase]
        rectangle(-4+sway,2,4+sway,11,0)
        rectangle(-3+sway,10,3+sway,16,0)
        rectangle(-2+sway,3,2+sway,10,1)
        rectangle(-2+sway,11,2+sway,14,1)
        rectangle(-4-wing+sway,6,-3+sway,8,0)
        rectangle(3+sway,6,4+wing+sway,8,0)
        rectangle(-4,0,-1,2,2);rectangle(1,0,4,2,2)
        rectangle(-1+sway,10,2+sway,11,2)
        rectangle(-2+sway,12,-1+sway,13,3)
        rectangle(1+sway,12,2+sway,13,3)
        models['meshes'].append(dict(id=30+phase,materials=[30,31,32,33],
                                    vertices=vertices,triangles=triangles))
    return list(range(30,36))


def penguin_controller(root,document,frames,battle,crabs,snowmen):
    if not (crabs or snowmen):
        source_actor_rules(root,document)
    order=int(crabs)+int(snowmen)
    properties=[dict(key='mksc:billboard',type='boolean',value=True),
        dict(key='mksc:controller',type='string',value='penguins'),
        dict(key='mksc:controller-version',type='integer',value=1)]
    values={'x-q16':200*65536,'y-q16':60*65536,'variant':0,
            'checkpoint':0,'placement-order':order,'frame-count':6}
    properties.extend(dict(key='source:'+key,type='integer',value=value)
                      for key,value in values.items())
    properties.extend(dict(key=f'source:frame:{i}',type='asset',value=dict(kind=96,id=identity))
                      for i,identity in enumerate(frames))
    if battle:
        position=[-7.25,0,-6] if not order else [-5,0,-7.25]
    else:
        # Separate mixed contacts by the full bridge loop, beyond shared hit grace.
        position=[-20,0,-16] if order else [20,0,-6]
    document['objects'].append(dict(class_id=0x1016,model=dict(kind=96,id=frames[0]),
        position=position,scale=[.1]*3,properties=properties))



def mouse_frames(models):
    """Original walking/crouched mouse art, with feet at local Z0."""
    models['materials'].extend([
        {'id':40,'argb':'#FFC28E62'},{'id':41,'argb':'#FFFFCCDD'},
        {'id':42,'argb':'#FFFFF2CE'},{'id':43,'argb':'#FF29213D'}])
    for phase in range(2):
        vertices,triangles=[],[]
        def rectangle(left,bottom,right,top,material):
            first=len(vertices)
            vertices.extend([[left,0,bottom,0,1],[right,0,bottom,1,1],
                             [right,0,top,1,0],[left,0,top,0,0]])
            triangles.extend([[first,first+1,first+2,material],
                              [first,first+2,first+3,material]])
        rectangle(-5,2,4,7+phase,0)
        rectangle(2,5,6,10+phase,0)
        rectangle(2,9+phase,5,13+phase,0)
        rectangle(3,10+phase,4,12+phase,1)
        rectangle(-4,0,-2,3,0);rectangle(2-phase,0,4+phase,3,0)
        rectangle(-4,3,2,5,2)
        rectangle(5,6,8,7,1);rectangle(5,8,6,9,3)
        rectangle(-8,2+phase,-4,3+phase,1)
        models['meshes'].append(dict(id=40+phase,materials=[40,41,42,43],
                                    vertices=vertices,triangles=triangles))
    return [40,41]


def mouse_controllers(root,document,frames,battle,crabs,snowmen,penguins):
    existing=int(crabs)+int(snowmen)+int(penguins)
    if not existing:
        source_actor_rules(root,document)
    if battle:
        poses=([-7.25,0,-6] if not existing else [-2,0,4],[-4,0,8],[8,0,-4])
    else:
        # The bottom and return straights accommodate the source 75.5-unit walks.
        poses=([20,0,-6] if not existing else [0,8,20],[-4,0,-26],[-20,0,-18])
    for index,(variant,position) in enumerate(zip((0x8001,0,1),poses)):
        properties=[dict(key='mksc:billboard',type='boolean',value=True),
            dict(key='mksc:controller',type='string',value='mice'),
            dict(key='mksc:controller-version',type='integer',value=1)]
        values={'x-q16':200*65536,'y-q16':60*65536,'variant':variant,
                'checkpoint':0,'placement-order':existing+index,'frame-count':2}
        properties.extend(dict(key='source:'+key,type='integer',value=value)
                          for key,value in values.items())
        properties.extend(dict(key=f'source:frame:{i}',type='asset',value=dict(kind=96,id=identity))
                          for i,identity in enumerate(frames))
        document['objects'].append(dict(class_id=0x1015,model=dict(kind=96,id=frames[0]),
            position=position,scale=[.1]*3,properties=properties))



def piranha_frames(models):
    """Five original stalk/mouth poses, all anchored at local feet Z0."""
    models['materials'].extend([
        {'id':50,'argb':'#FF4FC16B'},{'id':51,'argb':'#FFDF4057'},
        {'id':52,'argb':'#FFFFF3CF'},{'id':53,'argb':'#FF25313D'}])
    for phase in range(5):
        vertices,triangles=[],[]
        def rectangle(left,bottom,right,top,material):
            first=len(vertices)
            vertices.extend([[left,0,bottom,0,1],[right,0,bottom,1,1],
                             [right,0,top,1,0],[left,0,top,0,0]])
            triangles.extend([[first,first+1,first+2,material],
                              [first,first+2,first+3,material]])
        height=(14,16,18,12,17)[phase]
        lean=(0,-1,0,1,2)[phase]
        rectangle(-1,0,1,9,0)
        rectangle(-5,2,-1,4,0);rectangle(1,4,5,6,0)
        rectangle(-5+lean,8,5+lean,height,1)
        rectangle(-4+lean,height-2,4+lean,height-1,2)
        rectangle(-3+lean,10,3+lean,12 if phase!=2 else 15,3)
        for x in (-3,0,3):rectangle(x+lean,height-3,x+1+lean,height-2,2)
        models['meshes'].append(dict(id=50+phase,materials=[50,51,52,53],
                                    vertices=vertices,triangles=triangles))
    return list(range(50,55))


def piranha_poses(battle,existing,star):
    if battle:
        return [[-7.25,0,-6],[8,0,0]]
    return [[8,0,-26] if existing else [19.5 if star else 20,0,-2 if star else -6],[-20,0,-12]]


def piranha_controllers(root,document,frames,poses,existing):
    if not existing:
        source_actor_rules(root,document)
    for index,position in enumerate(poses):
        properties=[dict(key='mksc:billboard',type='boolean',value=True),
            dict(key='mksc:controller',type='string',value='piranhas'),
            dict(key='mksc:controller-version',type='integer',value=1)]
        values={'x-q16':200*65536,'y-q16':60*65536,'variant':0,
                'checkpoint':0,'placement-order':existing+index,'frame-count':5}
        properties.extend(dict(key='source:'+key,type='integer',value=value)
                          for key,value in values.items())
        properties.extend(dict(key=f'source:frame:{i}',type='asset',value=dict(kind=96,id=identity))
                          for i,identity in enumerate(frames))
        document['objects'].append(dict(class_id=0x1009,model=dict(kind=96,id=frames[0]),
            position=position,scale=[.1]*3,properties=properties))


def capture_floor(deck,pose,short):
    """Partition one flat collider so capture and ordinary floor never overlap."""
    x,z=pose[0],pose[2]
    low_x,high_x=x-2.8,x+2.8
    low_z,high_z=z-(.9 if short else 2.8),z+(.9 if short else 2.8)
    def rectangle(left,bottom,right,top):
        return [(left,0,bottom),(left,0,top),(right,0,top),(right,0,bottom)]
    result=[];found=False
    for quad in deck:
        left,right=min(p[0] for p in quad),max(p[0] for p in quad)
        bottom,top=min(p[2] for p in quad),max(p[2] for p in quad)
        if not found and all(p[1]==0 for p in quad) and (left<=low_x<high_x<=right and
                                                                      bottom<=low_z<high_z<=top):
            for a,b,c,d in ((left,bottom,low_x,top),(high_x,bottom,right,top),
                          (low_x,bottom,high_x,low_z),(low_x,high_z,high_x,top)):
                if a<c and b<d:result.append(rectangle(a,b,c,d))
            found=True
        else:
            result.append(quad)
    if not found:raise ValueError('capture patch must fit a supported flat floor')
    return result,[rectangle(low_x,low_z,high_x,high_z)]



def seagull_frames(models):
    """Three original resting/flapping bird silhouettes, anchored at feet Z0."""
    models['materials'].extend([
        {'id':60,'argb':'#FFF2F6FF'},{'id':61,'argb':'#FFA8BFD4'},
        {'id':62,'argb':'#FFF4B443'},{'id':63,'argb':'#FF26384A'}])
    for phase in range(3):
        vertices,triangles=[],[]
        def rectangle(left,bottom,right,top,material):
            first=len(vertices)
            vertices.extend([[left,0,bottom,0,1],[right,0,bottom,1,1],
                             [right,0,top,1,0],[left,0,top,0,0]])
            triangles.extend([[first,first+1,first+2,material],
                              [first,first+2,first+3,material]])
        rectangle(-4,2,4,7,0);rectangle(2,6,6,11,0)
        rectangle(5,7,9,8,2);rectangle(4,9,5,10,3)
        rectangle(-3,0,-2,3,2);rectangle(1,0,2,3,2)
        if phase==0:
            rectangle(-4,3,0,5,1);rectangle(-7,3,-4,4,1)
        elif phase==1:
            rectangle(-10,5,-2,8,1);rectangle(0,6,9,9,1)
        else:
            rectangle(-6,6,-3,14,1);rectangle(1,6,4,17,1)
        models['meshes'].append(dict(id=60+phase,materials=[60,61,62,63],
                                    vertices=vertices,triangles=triangles))
    return [60,61,62]


def seagull_controller(root,document,frames,battle,existing,star):
    if not existing:
        source_actor_rules(root,document)
    properties=[dict(key='mksc:billboard',type='boolean',value=True),
        dict(key='mksc:controller',type='string',value='seagulls'),
        dict(key='mksc:controller-version',type='integer',value=1)]
    values={'x-q16':200*65536,'y-q16':60*65536,'variant':0,
            'checkpoint':0,'placement-order':existing,'frame-count':3}
    properties.extend(dict(key='source:'+key,type='integer',value=value)
                      for key,value in values.items())
    properties.extend(dict(key=f'source:frame:{i}',type='asset',value=dict(kind=96,id=identity))
                      for i,identity in enumerate(frames))
    if battle:
        position=[-7.25,0,-6] if not existing else [4,0,-8]
    else:
        position=[-8,8,20] if existing else [19.5 if star else 20,0,-2 if star else -6]
    document['objects'].append(dict(class_id=0x100c,model=dict(kind=96,id=frames[0]),
        position=position,scale=[.1]*3,properties=properties))



def lava_bubble_frames(models):
    """Original two-phase molten droplets, anchored at feet Z0."""
    models['materials'].extend([{'id':70,'argb':'#FFF36A24'},
                                {'id':71,'argb':'#FFFFD34D'}])
    for phase in range(2):
        vertices,triangles=[],[]
        def polygon(points,material):
            first=len(vertices)
            vertices.extend([[x,0,z,0,0] for x,z in points])
            triangles.extend([[first,first+i,first+i+1,material]
                              for i in range(1,len(points)-1)])
        polygon([(-7,0),(7,0),(10,7),(6,15),(0,23 if phase==0 else 19),
                 (-6,15),(-10,7)],0)
        polygon([(-4,3),(4,3),(5,8),(0,15 if phase==0 else 12),(-5,8)],1)
        models['meshes'].append(dict(id=70+phase,materials=[70,71],
                                    vertices=vertices,triangles=triangles))
    return [70,71]


def lava_bubble_controller(root,document,frames,battle,existing,star):
    if not existing:
        source_actor_rules(root,document)
    properties=[dict(key='mksc:billboard',type='boolean',value=True),
        dict(key='mksc:controller',type='string',value='lava_bubbles'),
        dict(key='mksc:controller-version',type='integer',value=1)]
    values={'x-q16':200*65536,'y-q16':60*65536,'variant':5 if star else 3 if existing else 1 if battle else 0,
            'checkpoint':0,'placement-order':existing,'frame-count':2}
    properties.extend(dict(key='source:'+key,type='integer',value=value)
                      for key,value in values.items())
    properties.extend(dict(key=f'source:frame:{i}',type='asset',value=dict(kind=96,id=identity))
                      for i,identity in enumerate(frames))
    if battle:
        position=[-7.25,0,-6] if not existing else [4,0,-8]
    else:
        position=[-8,8,20] if existing else [19.5 if star else 20,0,0 if star else -4.5]
    document['objects'].append(dict(class_id=0x1004,model=dict(kind=96,id=frames[0]),
        position=position,scale=[.1]*3,properties=properties))


def thwomp_frame(models):
    """Original stone face, with feet at Z0; no imported pixels or curves."""
    models['materials'].extend([dict(id=80,argb='#FF507C88'),
        dict(id=81,argb='#FFBCD5D8'),dict(id=82,argb='#FF23373E')])
    vertices=[];triangles=[]
    for left,bottom,right,top,material in ((-16,0,16,32,0),(-13,3,13,29,1),
            (-10,18,-3,22,2),(3,18,10,22,2),(-9,7,9,11,2)):
        first=len(vertices)
        vertices.extend([[left,0,bottom,0,1],[right,0,bottom,1,1],
                         [right,0,top,1,0],[left,0,top,0,0]])
        triangles.extend([[first,first+1,first+2,material],[first,first+2,first+3,material]])
    models['meshes'].append(dict(id=80,materials=[80,81,82],vertices=vertices,triangles=triangles))
    return [80]


def thwomp_controllers(root,document,frames,battle,existing,star):
    if not existing:source_actor_rules(root,document)
    # Original authored curves, not copies of the imported source tables.
    tables=[dict(mode=mode,rows=rows) for mode,rows in (
        (1,[[256,320,12],[256,256,12]]),(4,[[256,288,10],[256,256,10]]),
        (5,[[192,1024,110]]),(6,[[192,1024,18]]),
        (7,[[224,768,112],[240,512,8],[256,256,8]]))]
    (root/'actor-affine.json').write_text(json.dumps(dict(version=1,profiles=[
        dict(profile_id=65081,tables=tables)]),indent=2)+'\n')
    document['meta']['properties'].append(dict(key='mksc:actor-affine-rules',type='asset',value=dict(kind=81,id=65081)))
    poses=([[-7.25,0,-6],[-7.3,0,-3.8]] if battle else
           [[19.5,0,0]] if star else [[20,0,-12],[20,0,-14]])
    if existing and not battle:poses=[[-8,8,20],[8,8,20]]
    for index,position in enumerate(poses):
        properties=[dict(key='mksc:billboard',type='boolean',value=True),
            dict(key='mksc:controller',type='string',value='thwomps'),
            dict(key='mksc:controller-version',type='integer',value=1)]
        values={'x-q16':200*65536,'y-q16':60*65536,'variant':
                0 if star else (3 if battle else 0) if index==0 else 2,
                'checkpoint':0,'placement-order':existing+index,'frame-count':1}
        properties.extend(dict(key='source:'+k,type='integer',value=v) for k,v in values.items())
        properties.append(dict(key='source:frame:0',type='asset',value=dict(kind=96,id=80)))
        document['objects'].append(dict(class_id=0x1002,model=dict(kind=96,id=80),
            position=position,scale=[.1]*3,properties=properties))


def cannonball_frames(models):
    """Original ball and six independent expanding flame silhouettes, feet Z0."""
    models['materials'].extend([dict(id=90,argb='#FF34465B'),dict(id=91,argb='#FF91A5B8'),
        dict(id=92,argb='#FFFF9F32'),dict(id=93,argb='#FFFFDF70')])
    for phase in range(7):
        vertices=[];triangles=[]
        def polygon(points,material):
            first=len(vertices)
            vertices.extend([[x,0,z,0,0] for x,z in points])
            triangles.extend([[first,first+i,first+i+1,material] for i in range(1,len(points)-1)])
        if phase==0:
            polygon([(-5,0),(5,0),(8,4),(8,12),(5,16),(-5,16),(-8,12),(-8,4)],0)
            polygon([(-4,9),(0,9),(0,13),(-4,13)],1)
        else:
            width=8+phase*2;height=22-phase*2
            polygon([(-width,0),(width,0),(width-2,height//2),(width//2,height-2),
                     (0,height),(-width//2,height-2),(-width+2,height//2)],2)
            polygon([(-width//2,2),(width//2,2),(width//3,height//2),(0,height-4),
                     (-width//3,height//2)],3)
        models['meshes'].append(dict(id=90+phase,materials=[90,91,92,93],
                                    vertices=vertices,triangles=triangles))
    return list(range(90,97))


def cannonball_controllers(root,document,frames,battle,existing,star):
    if not existing:source_actor_rules(root,document)
    poses=([[-7.1,0,-4],[8,0,0]] if battle else
           [[19.5,0,-1]] if star else [[20,0,-10],[19.5,0,-1]])
    if existing and not battle:poses=[[-8,8,20],[8,8,20]]
    for index,position in enumerate(poses):
        properties=[dict(key='mksc:billboard',type='boolean',value=True),
            dict(key='mksc:controller',type='string',value='cannonballs'),
            dict(key='mksc:controller-version',type='integer',value=1)]
        values={'x-q16':200*65536,'y-q16':60*65536,'variant':
                0 if star else 3 if index==0 else 2 if battle else 0,
                'checkpoint':0,'placement-order':existing+index,'frame-count':7}
        properties.extend(dict(key='source:'+k,type='integer',value=v) for k,v in values.items())
        properties.extend(dict(key=f'source:frame:{i}',type='asset',value=dict(kind=96,id=v))
                          for i,v in enumerate(frames))
        document['objects'].append(dict(class_id=0x1013,model=dict(kind=96,id=frames[0]),
            position=position,scale=[.1]*3,properties=properties))


def boo_frames(models):
    """Two original opaque ghost cells with independently drawn asymmetric eyes."""
    models['materials'].extend([dict(id=110,argb='#FFF0E8FF'),
        dict(id=111,argb='#FF66559B'),dict(id=112,argb='#FFF280B2')])
    for phase in range(2):
        vertices=[];triangles=[]
        def polygon(points,material):
            first=len(vertices)
            vertices.extend([[x,0,z,0,0] for x,z in points])
            triangles.extend([[first,first+i,first+i+1,material] for i in range(1,len(points)-1)])
        polygon([(-8,0),(-3,3),(0,0),(4,3),(8,0),(11,7),(10,15),
                 (5,20),(-5,20),(-10,15),(-11,7)],0)
        side=-1 if phase==0 else 1
        for center in (-4,4):
            x=center+side
            polygon([(x-1,11),(x+1,11),(x+1,15),(x-1,15)],1)
        polygon([(side*3-3,5),(side*3+3,5),(side*3+2,9),(side*3-2,9)],2)
        models['meshes'].append(dict(id=110+phase,materials=[110,111,112],
                                    vertices=vertices,triangles=triangles))
    return [110,111]


def boo_controllers(root,document,frames,battle,existing,star):
    if not existing:source_actor_rules(root,document)
    poses=([[-7.1,0,-4],[8,0,0]] if battle else
           [[19.5,0,-1]] if star else [[20,0,-10],[19.5,0,-1]])
    if existing and not battle:poses=[[-8,8,20],[8,8,20]]
    for index,position in enumerate(poses):
        properties=[dict(key='mksc:billboard',type='boolean',value=True),
            dict(key='mksc:controller',type='string',value='boos'),
            dict(key='mksc:controller-version',type='integer',value=1)]
        values={'x-q16':200*65536,'y-q16':60*65536,'variant':1 if star else index,
                'checkpoint':0,'placement-order':existing+index,'frame-count':2}
        properties.extend(dict(key='source:'+k,type='integer',value=v) for k,v in values.items())
        properties.extend(dict(key=f'source:frame:{i}',type='asset',value=dict(kind=96,id=v))
                          for i,v in enumerate(frames))
        document['objects'].append(dict(class_id=0x1007,model=dict(kind=96,id=frames[0]),
            position=position,scale=[.1]*3,properties=properties))


def rock_frames(models):
    """Five original volcanic-rock and impact cells, each anchored at feet Z0."""
    models['materials'].extend([dict(id=120,argb='#FF5D4273'),
        dict(id=121,argb='#FFDC6133'),dict(id=122,argb='#FFFFC155')])
    for phase in range(5):
        vertices=[];triangles=[]
        def polygon(points,material):
            first=len(vertices)
            vertices.extend([[x,0,z,0,0] for x,z in points])
            triangles.extend([[first,first+i,first+i+1,material] for i in range(1,len(points)-1)])
        if phase==0:
            polygon([(-6,0),(5,0),(9,5),(6,12),(-2,15),(-8,9)],0)
            polygon([(-3,4),(2,3),(5,8),(0,10)],1)
        else:
            width=7+phase*3;top=13-phase*2
            polygon([(-width,0),(width,0),(width-4,top),(0,top+2),(-width+4,top)],1)
            polygon([(-width//2,1),(width//2,1),(2,top),(-3,top-1)],2)
        models['meshes'].append(dict(id=120+phase,materials=[120,121,122],
                                    vertices=vertices,triangles=triangles))
    return list(range(120,125))


def rock_controllers(root,document,frames,battle,existing,star):
    if not existing:source_actor_rules(root,document)
    poses=([[-7.1,0,-4],[8,0,0],[-4,0,8],[4,0,-8]] if battle else
           [[19.5,0,-1],[-20,0,-16],[-8,8,20],[8,8,20]] if star else
           [[19.5,0,0],[20,0,-4.5],[-20,0,-16],[-8,8,20]])
    for index,position in enumerate(poses):
        properties=[dict(key='mksc:billboard',type='boolean',value=True),
            dict(key='mksc:controller',type='string',value='rocks'),
            dict(key='mksc:controller-version',type='integer',value=1)]
        values={'x-q16':200*65536,'y-q16':60*65536,'variant':index,
                'checkpoint':0,'placement-order':existing+index,'frame-count':5,'affine-q8':256}
        properties.extend(dict(key='source:'+k,type='integer',value=v) for k,v in values.items())
        properties.extend(dict(key=f'source:frame:{i}',type='asset',value=dict(kind=96,id=v))
                          for i,v in enumerate(frames))
        document['objects'].append(dict(class_id=0x101d,model=dict(kind=96,id=frames[0]),
            position=position,scale=[.1]*3,properties=properties))


def teepee_frames(models):
    """Eleven original anchored cells: four tent scraps and seven rider poses."""
    models['materials'].extend([dict(id=140,argb='#FFE6A24B'),
        dict(id=141,argb='#FF7844B8'),dict(id=142,argb='#FF65D5C3')])
    for frame in range(11):
        if frame<4:
            width=12+frame*3;top=24-frame*5
            points=[(-width,0),(width,0),(4,top),(-3,top+2)]
            material=0
        else:
            phase=frame-4;lean=(phase-3)*2
            points=[(-5,0),(5,0),(7+lean,12),(3+lean,19),(-3+lean,19),(-7+lean,12)]
            material=1 if phase%2==0 else 2
        models['meshes'].append(dict(id=140+frame,materials=[140,141,142],
            vertices=[[x,0,z,0,0] for x,z in points],
            triangles=[[0,i,i+1,material] for i in range(1,len(points)-1)]))
    return list(range(140,151))


def teepee_controller(root,document,frames,battle,existing,star):
    if not existing:source_actor_rules(root,document)
    position=([-7.1,0,-4] if battle else [19.5,0,-1] if star else
              [-20,0,-16] if existing else [20,0,-6])
    properties=[dict(key='mksc:billboard',type='boolean',value=True),
        dict(key='mksc:controller',type='string',value='teepees'),
        dict(key='mksc:controller-version',type='integer',value=1)]
    values={'x-q16':200*65536,'y-q16':60*65536,'variant':0,'checkpoint':0,
            'placement-order':existing,'frame-count':11,'affine-q8':256}
    properties.extend(dict(key='source:'+key,type='integer',value=value) for key,value in values.items())
    properties.extend(dict(key=f'source:frame:{i}',type='asset',value=dict(kind=96,id=value))
                      for i,value in enumerate(frames))
    document['objects'].append(dict(class_id=0x1018,model=dict(kind=96,id=frames[0]),
        position=position,scale=[.1]*3,properties=properties))


def falling_star_frames(models):
    """Five original star/dissolve cells, rotating around their local center."""
    models['materials'].extend([dict(id=160,argb='#FFFFCC45'),
        dict(id=161,argb='#FFE56391'),dict(id=162,argb='#FF9867DF')])
    for phase in range(5):
        points=[]
        radius=12-phase
        for index in range(10):
            angle=math.pi/2+index*math.pi/5
            r=radius if index%2==0 else radius*.45
            points.append((math.cos(angle)*r,math.sin(angle)*r))
        vertices=[[0,0,0,0,0]]+[[x,0,z,0,0] for x,z in points]
        models['meshes'].append(dict(id=160+phase,materials=[160,161,162],vertices=vertices,
            triangles=[[0,1+i,1+(i+1)%10,(phase+i//4)%3] for i in range(10)]))
    return list(range(160,165))


def falling_star_controllers(root,document,frames,battle,existing,star):
    if not existing:source_actor_rules(root,document)
    # Protected approach anchor rests on the authored ramp (.4m at local z=.8),
    # allowing the unchanged source descent to meet the genuine Star-driven kart.
    poses=([[-7.1,0,-4],[8,0,0]] if battle else
           [[19.5,.4,.8],[-20,0,-16]] if star else
           [[20,0,-1],[-20,0,-16]])
    for index,position in enumerate(poses):
        properties=[dict(key='mksc:billboard',type='boolean',value=True),
            dict(key='mksc:controller',type='string',value='falling_stars'),
            dict(key='mksc:controller-version',type='integer',value=1)]
        values={'x-q16':200*65536,'y-q16':60*65536,'variant':0 if index==0 else 3,
                'checkpoint':0,'placement-order':existing+index,'frame-count':5}
        properties.extend(dict(key='source:'+key,type='integer',value=value) for key,value in values.items())
        properties.extend(dict(key=f'source:frame:{i}',type='asset',value=dict(kind=96,id=value))
                          for i,value in enumerate(frames))
        document['objects'].append(dict(class_id=0x1011,model=dict(kind=96,id=frames[0]),
            position=position,scale=[.1]*3,properties=properties))


def thundercloud_frames(models):
    """Original nine color phases × eight cloud/warning/bolt silhouettes."""
    colors=('#FF7484A3','#FFA4BCDD','#FFC1AFE7','#FFE1AFE3','#FFF2BDA5',
            '#FFE9DB9B','#FFAADFC4','#FF86CEDA','#FFC8D2E8')
    for phase,color in enumerate(colors):
        material=180+phase*3
        models['materials'].extend([dict(id=material,argb=color),
            dict(id=material+1,argb='#FFFFF3A6'),dict(id=material+2,argb='#FF33445A')])
        for art in range(8):
            vertices=[];triangles=[]
            def polygon(points,paint):
                first=len(vertices);vertices.extend([[x,0,z,0,0] for x,z in points])
                triangles.extend([[first,first+i,first+i+1,paint] for i in range(1,len(points)-1)])
            width=27 if art<4 else 23
            polygon([(-width,28),(width,28),(width+3,39),(16,47),(5,53),
                     (-6,50),(-17,46),(-width-4,37)],0)
            if art%4 in (1,2):
                polygon([(-19,33),(18,33),(12,39),(-14,40)],1)
            if art%4==3:
                polygon([(-3,30),(7,30),(1,18),(8,18),(-7,0),(-2,13),(-10,13)],1)
            polygon([(-12,35),(-8,35),(-8,39),(-12,39)],2)
            polygon([(8,35),(12,35),(12,39),(8,39)],2)
            models['meshes'].append(dict(id=180+phase*8+art,
                materials=[material,material+1,material+2],vertices=vertices,triangles=triangles))
    return list(range(180,252))


def thundercloud_controllers(root,document,frames,battle,existing,star):
    if not existing:source_actor_rules(root,document)
    poses=([[-7.1,0,-4],[8,0,0],[-4,0,8],[4,0,-8]] if battle else
           [[18.5,10,21],[8,8,20],[-20,0,-16],[20,0,-6]] if star else
           [[20,0,0],[8,8,20],[-20,0,-16],[20,0,-6]])
    for variant,position in enumerate(poses):
        properties=[dict(key='mksc:billboard',type='boolean',value=True),
            dict(key='mksc:controller',type='string',value='thunderclouds'),
            dict(key='mksc:controller-version',type='integer',value=1)]
        values={'x-q16':200*65536,'y-q16':60*65536,'variant':variant,
                'checkpoint':0,'placement-order':existing+variant,'frame-count':72}
        properties.extend(dict(key='source:'+key,type='integer',value=value) for key,value in values.items())
        properties.extend(dict(key=f'source:frame:{i}',type='asset',value=dict(kind=96,id=value))
                          for i,value in enumerate(frames))
        # Source construction assigns art4 even for the low bank variants.
        document['objects'].append(dict(class_id=0x1010,model=dict(kind=96,id=frames[4]),
            position=position,scale=[.1]*3,properties=properties))


def common_prop_frames(models):
    """Six original roadside props, including two distinct umbrella cells."""
    colors=('#FF3FB774','#FF4DBED8','#FFF1AE3F','#FFDC5B8E',
            '#FF5F58C7','#FFF66F38','#FF99633D')
    for index,color in enumerate(colors):
        models['materials'].append(dict(id=270+index,argb=color))
        if index==0:points=[(-9,0),(9,0),(9,15),(17,15),(0,34),(-17,15),(-9,15)]
        elif index==1:points=[(-6,0),(6,0),(6,23),(-6,23)]
        elif index==2:points=[(-11,0),(11,0),(15,8),(7,17),(-8,18),(-15,8)]
        elif index==3:points=[(-4,0),(4,0),(4,16),(13,19),(8,28),(-7,28),(-13,19),(-4,16)]
        elif index==4:points=[(-2,0),(2,0),(2,18),(7,18),(0,32),(-7,18),(-2,18)]
        elif index==5:points=[(-2,0),(2,0),(2,20),(20,20),(10,30),(-10,30),(-20,20),(-2,20)]
        else:points=[(-10,0),(10,0),(10,18),(-10,18)]
        models['meshes'].append(dict(id=270+index,materials=[270+index],
            vertices=[[x,0,z,0,0] for x,z in points],
            triangles=[[0,i,i+1,0] for i in range(1,len(points)-1)]))
    return [270,271,272,273,(274,275),276]


def common_prop_controllers(root,document,frames,battle,existing,star):
    if not existing:source_actor_rules(root,document)
    codes=(0x060001,0x080000,0x0a0007,0x0c000b,0x860000,0x980001)
    poses=([[-12,0,-4],[8,0,0],[-4,0,8],[4,0,-8],[-7.1,0,-4],[10,0,9]] if battle else
           [[22,0,-12],[-20,0,-16],[8,8,20],[-8,8,20],
            [19.5,.4,.8] if star else [19.5,0,0],[-22,0,-20]])
    for index,(code,position) in enumerate(zip(codes,poses)):
        cells=list(frames[index]) if isinstance(frames[index],tuple) else [frames[index]]
        properties=[dict(key='mksc:billboard',type='boolean',value=True),
            dict(key='mksc:controller',type='string',value='common_props'),
            dict(key='mksc:controller-version',type='integer',value=1)]
        values={'x-q16':200*65536,'y-q16':60*65536,'code':code,'variant':code&65535,
                'checkpoint':0,'placement-order':existing+index,'frame-count':len(cells),'affine-q8':256}
        properties.extend(dict(key='source:'+key,type='integer',value=value) for key,value in values.items())
        properties.extend(dict(key=f'source:frame:{i}',type='asset',value=dict(kind=96,id=value))
                          for i,value in enumerate(cells))
        document['objects'].append(dict(class_id=0x1019,model=dict(kind=96,id=cells[0]),
            position=position,scale=[.1]*3,properties=properties))


def mechakoopa_frames(models):
    """Original winding-toy cells; every phase keeps local feet at zero."""
    colors=('#FF58C48D','#FF357D63','#FFEAB34D','#FFBF773E',
            '#FF67ACD9','#FF456CBA','#FFE684A5','#FFAC577E')
    for index,color in enumerate(colors):
        model=280+index
        # The asymmetric crown makes front/back and stride cells observable.
        points=[[-9,0],[9,0],[11,10],[7,22],[-5,25],[-11,16]]
        points[0][0]+=index%2*3
        points[3][0]-=(index//2)%2*4
        models['materials'].append(dict(id=model,argb=color))
        models['meshes'].append(dict(id=model,materials=[model],
            vertices=[[x,0,z,0,0] for x,z in points],
            triangles=[[0,i,i+1,0] for i in range(1,len(points)-1)]))
    return [list(range(280,284)),list(range(284,288))]


def mechakoopa_controllers(root,document,frames,battle,existing,star):
    if not existing:source_actor_rules(root,document)
    # The first left-walking toy crosses the ordinary approach; the second
    # right-walking toy patrols the return shoulder without forcing a spin
    # immediately before its tight bottom corner.
    poses=([[-2.3,0,-4],[8,0,0]] if battle else
           [[23.25,.4,.8] if star else [24.4,0,-1],[-20 if star else -25.6,0,-16]])
    for index,position in enumerate(poses):
        variant=1-index
        properties=[dict(key='mksc:billboard',type='boolean',value=True),
            dict(key='mksc:controller',type='string',value='mechakoopas'),
            dict(key='mksc:controller-version',type='integer',value=1)]
        values={'x-q16':200*65536,'y-q16':60*65536,'variant':variant,'checkpoint':0,
                'placement-order':existing+index,'frame-count':4,'affine-q8':224}
        properties.extend(dict(key='source:'+key,type='integer',value=value) for key,value in values.items())
        properties.extend(dict(key=f'source:frame:{i}',type='asset',value=dict(kind=96,id=model))
                          for i,model in enumerate(frames[index]))
        document['objects'].append(dict(class_id=0x101c,model=dict(kind=96,id=frames[index][0]),
            position=position,scale=[.1]*3,properties=properties))


def kamek_frames(models):
    """Two original asymmetric wizard silhouettes, independent anchored cells."""
    for frame,color in enumerate(('#FF4B94D6','#FF8663BF')):
        model=288+frame
        points=[[-10,0],[8,0],[12,14],[5,18],[3,31],[-7,20],[-13,11]]
        if frame:points[3][0]=-2;points[-1][0]=-8
        models['materials'].append(dict(id=model,argb=color))
        models['meshes'].append(dict(id=model,materials=[model],
            vertices=[[x,0,z,0,0] for x,z in points],
            triangles=[[0,i,i+1,0] for i in range(1,len(points)-1)]))
    return [288,289]


def kamek_controller(root,document,frames,battle,existing,star):
    if not existing:source_actor_rules(root,document)
    position=[-9.8,0,-6] if battle else [21.65625,0,.8] if star else [21.2,0,-1]
    properties=[dict(key='mksc:billboard',type='boolean',value=True),
        dict(key='mksc:controller',type='string',value='kamek'),
        dict(key='mksc:controller-version',type='integer',value=1)]
    values={'x-q16':200*65536,'y-q16':60*65536,'variant':0,'checkpoint':0,
            'placement-order':existing,'frame-count':2,'affine-q8':224}
    properties.extend(dict(key='source:'+key,type='integer',value=value) for key,value in values.items())
    properties.extend(dict(key=f'source:frame:{i}',type='asset',value=dict(kind=96,id=model))
                      for i,model in enumerate(frames))
    document['objects'].append(dict(class_id=0x101b,model=dict(kind=96,id=frames[0]),
        position=position,scale=[.1]*3,properties=properties))


def pier_boo_frames(models):
    """Original prop and centered haunt silhouettes with a true mirrored cell."""
    models['materials'].extend([dict(id=290,argb='#FFEEE0FF'),dict(id=291,argb='#FFF3EAFF')])
    for index in range(3):
        bottom,top=(-5,27) if index==0 else (-16,16)
        points=[[-16,bottom+8],[-10,bottom],[2,bottom+4],[11,bottom],[16,bottom+12],
                [11,top-3],[0,top],[-12,top-5]]
        if index==2:points=[[-x,z] for x,z in points]
        models['meshes'].append(dict(id=290+index,materials=[290 if index==0 else 291],
            vertices=[[x,0,z,0,0] for x,z in points],
            triangles=[[0,i,i+1,0] if index!=2 else [0,i+1,i,0]
                       for i in range(1,len(points)-1)]))
    return [290,291,292]


def pier_boo_controller(root,document,frames,battle,existing,star,cup=False):
    if not existing:source_actor_rules(root,document)
    # A cup begins from rear GRID7: its first primary touch must precede
    # faster CPU starters reaching the one-shot roadside prop.
    position=([-7.1,0,-6] if battle else [19.5,0,.8] if star else
              [22,0,-28.5] if cup else [19.6,0,-1])
    properties=[dict(key='mksc:billboard',type='boolean',value=True),
        dict(key='mksc:controller',type='string',value='pier_boos'),
        dict(key='mksc:controller-version',type='integer',value=1)]
    values={'x-q16':200*65536,'y-q16':60*65536,'variant':0,'checkpoint':0,
            'placement-order':existing,'frame-count':3,'affine-q8':256}
    properties.extend(dict(key='source:'+key,type='integer',value=value) for key,value in values.items())
    properties.extend(dict(key=f'source:frame:{i}',type='asset',value=dict(kind=96,id=model))
                      for i,model in enumerate(frames))
    document['objects'].append(dict(class_id=0x1008,model=dict(kind=96,id=frames[0]),
        position=position,scale=[.1]*3,properties=properties))
    document['meta']['properties'].append(dict(key='mksc:actor-visual-seed',type='integer',value=0))


def coin_frame(models):
    """Original gold floor marker, not imported source art."""
    models['materials'].extend([dict(id=130,argb='#FFF6C344'),dict(id=131,argb='#FF946527')])
    models['meshes'].append(dict(id=130,materials=[130,131],
        vertices=[[-4,-4,0,0,0],[4,-4,0,1,0],[4,4,0,1,1],[-4,4,0,0,1],
                  [-2,-2,.01,0,0],[2,-2,.01,1,0],[2,2,.01,1,1],[-2,2,.01,0,1]],
        triangles=[[0,1,2,0],[0,2,3,0],[4,5,6,1],[4,6,7,1]]))


def coin_placements(root,document,battle,cup=False):
    rules=mesh_coin_format.default_profile(modes=31)
    (root/'coins.json').write_text(json.dumps(dict(version=1,profiles=[rules]),indent=2)+'\n')
    document['meta']['properties'].append(dict(key='mksc:coin-rules',type='asset',value=dict(kind=105,id=1)))
    poses=[[-7,0,-4],[8,0,0],[-4,0,8],[4,0,-8]] if battle else [
        [20,0,-20],[20,0,-12],[-8,8,20],[8,8,20]]
    if cup and not battle:
        # Any previous legitimate finish order may choose any next grid slot.
        # Distinct ordinary floor coins keep each start useful for every racer.
        poses=[[(row['position'][0]),0,row['position'][2]] for row in document['grid']]+poses
    for identity,position in enumerate(poses,1):
        properties=[dict(key='mksc:controller',type='string',value='coins'),
            dict(key='mksc:controller-version',type='integer',value=1),
            dict(key='coin:id',type='integer',value=identity)]
        properties.extend(dict(key='coin:half-'+axis,type='fixed',value=.4) for axis in ('x','y','z'))
        document['objects'].append(dict(class_id=0x20000001,model=dict(kind=96,id=130),
            position=position,scale=[.1]*3,properties=properties))

def sky_motion_art(root, models):
    """Original opaque panorama and immutable Z-up cylinder; no imported art."""
    def chunk(kind, data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    pixels=bytearray()
    for y in range(128):
        pixels.append(0)
        for x in range(256):
            cloud=((x-68)**2/900+(y-96)**2/100<1 or
                   (x-183)**2/1600+(y-104)**2/144<1)
            pixels.extend((235,244,255,255) if cloud else (45+y//3,95+y//2,175+y//2,255))
    (root/'sky-motion.png').write_bytes(b'\x89PNG\r\n\x1a\n'+
        chunk(b'IHDR',struct.pack('>IIBBBBB',256,128,8,6,0,0,0))+
        chunk(b'IDAT',zlib.compress(bytes(pixels),9))+chunk(b'IEND',b''))
    models['materials'].append(dict(id=300,argb='#FFFFFFFF',texture='sky-motion.png',wrap_u=True,wrap_v=False))
    vertices=[];triangles=[]
    for i in range(65):
        angle=i*math.tau/64
        for height,v in (((112-128)*1024/136,1),(112*1024/136,0)):
            vertices.append([math.sin(angle)*1024,-math.cos(angle)*1024,height,i*2/64,v])
    for i in range(64):
        a=i*2
        triangles.extend([[a,a+1,a+3,0],[a,a+3,a+2,0]])
    models['meshes'].append(dict(id=300,materials=[300],vertices=vertices,triangles=triangles))
    rules=dict(controller=1,yaw_shift=[7,7,8],layer_mask=1,blend_mode=0,blend_a=0,blend_b=0,
        x_bias=[0,0,0],y_bias=[38,38,38],split_y=-1,split_layer=0,split_yaw_shift=0,
        split_x_bias=0,lightning_hide_layer=0,road_mode=0,layer_source=[0,1,2])
    profile=dict(profile_id=1,revision=1,entry_delay=False,rules=rules,
        layers=[dict(layer=0,logical_width=256,radius_q16=1024*65536)],
        sine=[round(math.sin(i*math.tau/1024)*32768) for i in range(1024)])
    (root/'skies.json').write_text(json.dumps(dict(version=1,profiles=[profile]),indent=2)+'\n')


LOOP_X, LOOP_Z, LOOP_RADIUS, LOOP_SHIFT, LOOP_STEPS = 0, -26, 5, 10, 48


def loop_frame(step):
    """Centre, up and travel of the loop at step 0..LOOP_STEPS. The road rolls
    over a full circle about a cross-course axis while sliding LOOP_SHIFT
    metres sideways, so the exit clears the entry; cross sections stay level."""
    t = step/LOOP_STEPS
    angle = 2*math.pi*t
    centre = (LOOP_X+LOOP_RADIUS*math.sin(angle), LOOP_RADIUS*(1-math.cos(angle)),
              LOOP_Z+LOOP_SHIFT*t*t*(3-2*t))
    return centre, (-math.sin(angle), math.cos(angle), 0)


def loop_edge(step, offset):
    centre, _ = loop_frame(step)
    return tuple(round(c, 6) for c in (centre[0], centre[1], centre[2]+offset))


def anti_gravity_loop(document):
    """MK8-style gravity-path points (Transform) along the loop, CPU and item
    nodes through it and a spin bumper on the ceiling's right lane."""
    first = len(document['zones'])
    for n, step in enumerate(range(0, LOOP_STEPS+1, 4)):
        centre, up = loop_frame(step)
        properties = [dict(key='mksc:anti-gravity', type='boolean', value=True)]
        if step < LOOP_STEPS:
            properties.append(dict(key='mksc:gravity-next', type='integer', value=first+n+1))
        document['zones'].append(dict(center=[round(c, 6) for c in centre], half_extents=[6, 6, 6], kind=1,
            gravity=[round(-u*course_3d_format.DEFAULT_GRAVITY, 6) for u in up], properties=properties))
    exit_z = LOOP_Z+LOOP_SHIFT
    nodes = [[round(c, 6) for c in loop_frame(step)[0]] for step in range(6, LOOP_STEPS, 6)]
    for route in document['routes'][:2]:
        tail = route['nodes'][-1]
        route['nodes'][-1:] = [dict(tail, position=node) for node in
                               nodes+[[LOOP_X+2, 0, exit_z], [20, 0, exit_z]]]
        for i, node in enumerate(route['nodes']):
            node['successors'] = [(i+1) % len(route['nodes'])]
    # The exit merges below the start line, so the line, grid and respawn
    # move up the straight past it.
    document['checkpoints'][4]['center'] = [0, 0, exit_z]
    document['checkpoints'][0]['center'] = [20, 0, -6]
    for route in document['routes'][:2]:
        route['nodes'][0]['position'] = [20, 0, -6]
    for index, slot in enumerate(document['grid']):
        slot['position'] = [18+(index%2)*4, .2, -10-(index//2)*1.5]
    document['respawns'][0]['position'] = [20, 0, -10]
    centre, up = loop_frame(LOOP_STEPS//2)
    document['objects'].append(dict(class_id=2, model=dict(kind=96, id=3),
        position=[round(centre[0], 6), round(centre[1], 6), round(centre[2]+2.5, 6)], scale=[.3, .3, .3],
        properties=[dict(key='mksc:billboard', type='boolean', value=True),
            dict(key='mksc:hazard-radius', type='fixed', value=.75),
            dict(key='mksc:hazard-center', type='vector',
                 value=[round(centre[0]+.75*up[0], 6), round(centre[1]+.75*up[1], 6), round(centre[2]+2.5, 6)]),
            dict(key='mksc:spin-bumper', type='boolean', value=True)]))
    document['meta']['name'] += ' Loop'


# The glider kicker ends the upper deck's left turn 9.5 m up; the path
# dives over the drop onto the left straight, which now runs under it.
GLIDE_RAMP = [(-24,9.5,12),(-24,8,16),(-16,8,16),(-16,9.5,12)]
GLIDE_PATH = [(-20,10,12),(-20,2,0),(-20,-6,-12)]


def glider_path(document):
    """MK8-style glide-path points (Transform) over the drop and the CPU and
    item routes through the air."""
    first = len(document['zones'])
    for n, centre in enumerate(GLIDE_PATH):
        properties = []
        if n+1 < len(GLIDE_PATH):
            properties.append(dict(key='mksc:glide-next', type='integer', value=first+n+1))
        document['zones'].append(dict(center=list(centre), half_extents=[4, 4, 4], kind=2,
            gravity=[0, -course_3d_format.DEFAULT_GRAVITY, 0], properties=properties))
    for route in document['routes'][:2]:
        nodes = route['nodes']
        at = next(i for i, node in enumerate(nodes) if node['position'] == [-20,8,16])
        nodes[at+1:at+1] = [dict(nodes[at], position=[-20,9.5,12]), dict(nodes[at], position=[-20,4,2])]
        for i, node in enumerate(nodes):
            node['successors'] = [(i+1) % len(nodes)]
    document['surfaces'].append(dict(type='road', friction=1, speed_ratio=1, properties=[
        dict(key='mksc:glide', type='boolean', value=True)]))
    document['collision_nodes'].append(dict(node='GlideCollision', surface=len(document['surfaces'])-1, respawn=0))
    document['meta']['name'] += ' Glide'


def source(root, course_id=65536, class_profiles=False, cup=False, items=False, battle=False, crabs=False, snowmen=False, penguins=False, penguins_star=False, mice=False, mice_star=False, piranhas=False, piranhas_star=False, seagulls=False, seagulls_star=False, lava_bubbles=False, lava_bubbles_star=False, thwomps=False, thwomps_star=False, cannonballs=False, cannonballs_star=False, boos=False, boos_star=False, rocks=False, rocks_star=False, coins=False, teepees=False, teepees_star=False, falling_stars=False, falling_stars_star=False, thunderclouds=False, thunderclouds_star=False, common_props=False, common_props_star=False, mechakoopas=False, mechakoopas_star=False, kamek=False, kamek_star=False, pier_boos=False, pier_boos_star=False, pier_boos_repel=False, sky_motion=False, scenery_binding=False, ground_follow=False, anti_gravity=False, glider=False):
    if scenery_binding and battle:
        raise ValueError("scenery_binding requires the non-Battle static tree fixture")
    if glider and (battle or piranhas or ground_follow):
        raise ValueError("glider replaces the left descent ramp; it needs the lap course without piranhas or ground-follow crabs")
    if anti_gravity and (battle or piranhas):
        raise ValueError("anti_gravity rolls the lap's return straight; it needs the lap course without piranhas")
    if pier_boos_repel and not (pier_boos and items and cup and not battle and not pier_boos_star):
        raise ValueError("pier_boos_repel requires pier_boos and items in a normal cup")
    if pier_boos and not items:
        raise ValueError("pier_boos requires items")
    if pier_boos_star and not (pier_boos and items):
        raise ValueError("pier_boos_star requires pier_boos and items")
    if kamek and not items:
        raise ValueError("kamek requires items")
    if kamek_star and not (kamek and items):
        raise ValueError("kamek_star requires kamek and items")
    if ground_follow and (not crabs or battle):
        raise ValueError("ground_follow requires crabs on the bridge course")
    if mechakoopas and not items:
        raise ValueError("mechakoopas requires items")
    if mechakoopas_star and not (mechakoopas and items):
        raise ValueError("mechakoopas_star requires mechakoopas and items")
    if common_props and not items:
        raise ValueError("common_props requires items")
    if common_props_star and not (common_props and items):
        raise ValueError("common_props_star requires common_props and items")
    if thunderclouds and not items:
        raise ValueError("thunderclouds requires items")
    if thunderclouds_star and not (thunderclouds and items):
        raise ValueError("thunderclouds_star requires thunderclouds and items")
    if falling_stars and not items:
        raise ValueError("falling_stars requires items")
    if falling_stars_star and not (falling_stars and items):
        raise ValueError("falling_stars_star requires falling_stars and items")
    if teepees and not items:
        raise ValueError("teepees requires items")
    if teepees_star and not (teepees and items):
        raise ValueError("teepees_star requires teepees and items")
    if coins and not items:
        raise ValueError("coins requires items")
    if rocks_star and not (rocks and items):
        raise ValueError("rocks_star requires rocks and items")
    if boos_star and not (boos and items):
        raise ValueError("boos_star requires boos and items")
    if cannonballs_star and not (cannonballs and items):
        raise ValueError("cannonballs_star requires cannonballs and items")
    if thwomps_star and not (thwomps and items):
        raise ValueError("thwomps_star requires thwomps and items")
    if lava_bubbles_star and not (lava_bubbles and items):
        raise ValueError("lava_bubbles_star requires lava_bubbles and items")
    if seagulls_star and not (seagulls and items):
        raise ValueError("seagulls_star requires seagulls and items")
    if piranhas_star and not (piranhas and items):
        raise ValueError("piranhas_star requires piranhas and items")
    if mice_star and not (mice and items):
        raise ValueError("mice_star requires mice and items")
    if penguins_star and not (penguins and items):
        raise ValueError("penguins_star requires penguins and items")
    if battle and (cup or class_profiles):
        raise ValueError("Battle cannot be combined with cup or class profiles")
    items = items or battle
    if not isinstance(course_id, int) or isinstance(course_id, bool) or not 1 <= course_id <= 0xffffffff-(3 if cup else 0):
        raise ValueError('bridge course IDs must fit the nonzero uint32 range')
    root = Path(root)
    course = root / 'courses3d/bridge'
    course.mkdir(parents=True,exist_ok=True)
    (root/'mod.json').write_text(json.dumps({'id':'bridge-course','name':'Bridge Course',
        'version':'1.0.0','api_version':1,'targets':['3d']},indent=2)+'\n')
    checker_png(course/'deck.png')
    # Reuse only original procedural geometry; all materials/assets remain authored.
    spec = importlib.util.spec_from_file_location('bridge_low_poly',
        Path(__file__).resolve().parents[1]/'low-poly/build.py')
    low_poly = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(low_poly)
    models = low_poly.model_document(2, course_id, 0)
    models['bindings'] = [models['bindings'][0]]
    models['materials'].append({'id':6,'argb':'#FFFFFFFF','texture':'sign.png'})
    models['meshes'].append({'id':3,'materials':[6],
        'vertices':[[-2,0,0,0,1],[2,0,0,1,1],[2,0,3,1,0],[-2,0,3,0,0]],
        'triangles':[[0,1,2,0],[0,2,3,0]]})
    frames = crab_frames(models) if crabs else None
    snow_frames = snowman_frames(models) if snowmen else None
    penguin_art = penguin_frames(models) if penguins else None
    mouse_art = mouse_frames(models) if mice else None
    plant_art = piranha_frames(models) if piranhas else None
    bird_art = seagull_frames(models) if seagulls else None
    bubble_art = lava_bubble_frames(models) if lava_bubbles else None
    stone_art = thwomp_frame(models) if thwomps else None
    ball_art = cannonball_frames(models) if cannonballs else None
    ghost_art = boo_frames(models) if boos else None
    stone_cells = rock_frames(models) if rocks else None
    tent_cells=teepee_frames(models) if teepees else None
    star_cells=falling_star_frames(models) if falling_stars else None
    cloud_cells=thundercloud_frames(models) if thunderclouds else None
    common_cells=common_prop_frames(models) if common_props else None
    mecha_cells=mechakoopa_frames(models) if mechakoopas else None
    kamek_cells=kamek_frames(models) if kamek else None
    pier_cells=pier_boo_frames(models) if pier_boos else None
    if coins:coin_frame(models)
    if sky_motion:sky_motion_art(root,models)
    existing_actors=int(crabs)+int(snowmen)+int(penguins)+3*int(mice)
    plant_poses=piranha_poses(battle,existing_actors,piranhas_star) if piranhas else None
    (root/'models.json').write_text(json.dumps(models,indent=2)+'\n')
    def chunk(kind, payload):
        return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload)&0xffffffff)
    sign = b''.join(b'\0'+b''.join(bytes((229,37,217,255 if (x+y)%4 else 0))
        for x in range(16)) for y in range(16))
    (root/'sign.png').write_bytes(b'\x89PNG\r\n\x1a\n'+
        chunk(b'IHDR',struct.pack('>IIBBBBB',16,16,8,6,0,0,0))+
        chunk(b'IDAT',zlib.compress(sign))+chunk(b'IEND',b''))
    data, accessors, views, meshes, nodes = bytearray(), [], [], [], []

    def mesh(name, quads, material):
        first = len(data)
        vertices, triangles = [], []
        for corners in quads:
            a,b,c = corners[:3]
            u,v = [b[i]-a[i] for i in range(3)],[c[i]-a[i] for i in range(3)]
            normal = [u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
            length = math.sqrt(sum(n*n for n in normal))
            normal = [n/length for n in normal]
            index = len(vertices)
            vertices += [(*point,*normal,*uv) for point,uv in zip(corners,((0,0),(0,4),(4,4),(4,0)))]
            triangles += [index,index+1,index+2,index,index+2,index+3]
        for vertex in vertices:
            data.extend(struct.pack('<8f',*vertex))
        view = len(views)
        views.append({'buffer':0,'byteOffset':first,'byteLength':len(vertices)*32,'byteStride':32})
        attributes = {}
        for semantic,offset,shape in (('POSITION',0,'VEC3'),('NORMAL',12,'VEC3'),('TEXCOORD_0',24,'VEC2')):
            attributes[semantic] = len(accessors)
            accessors.append({'bufferView':view,'byteOffset':offset,'componentType':5126,'count':len(vertices),'type':shape})
        first = len(data)
        data.extend(struct.pack('<'+'H'*len(triangles),*triangles))
        views.append({'buffer':0,'byteOffset':first,'byteLength':len(triangles)*2})
        index = len(accessors)
        accessors.append({'bufferView':len(views)-1,'componentType':5123,'count':len(triangles),'type':'SCALAR'})
        while len(data)%4:
            data.append(0)
        meshes.append({'primitives':[{'attributes':attributes,'indices':index,'material':material}]})
        nodes.append({'name':name,'mesh':len(meshes)-1})

    # The right approach rises from ground level to an upper crossing. A second
    # road passes underneath, with separate roof/wall geometry around its mouth.
    deck = [
        [(16,0,-30),(16,0,0),(24,0,0),(24,0,-30)],
        [(16,0,0),(16,8,16),(24,8,16),(24,0,0)],
        [(-24,8,16),(-24,8,24),(24,8,24),(24,8,16)],
        [(-24,0,-10),(-24,8,16),(-16,8,16),(-16,0,-10)],
        [(-24,0,-30),(-24,0,-10),(-16,0,-10),(-16,0,-30)],
        [(-24,0,-30),(-24,0,-22),(24,0,-22),(24,0,-30)],
        [(-4,0,-22),(-4,0,35),(4,0,35),(4,0,-22)],
    ]
    tunnel = [
        [(-5,0,12),(-5,6,12),(-5,6,28),(-5,0,28)],
        [(5,0,12),(5,0,28),(5,6,28),(5,6,12)],
        [(-5,6,12),(-5,6,28),(5,6,28),(5,6,12)],
    ]
    # Low rails line the raised deck and the ground loop's outer edge as on MK8's
    # roads, so a kart a boost or slipstream carries wide scrapes instead of falling.
    def rail(a,b):
        return [a,(a[0],a[1]+1,a[2]),(b[0],b[1]+1,b[2]),b]
    rails = [rail((24,0,0),(24,8,16)),rail((16,0,0),(16,8,16)),rail((24,8,16),(24,8,24)),
             rail((24,8,24),(-24,8,24)),rail((-16,8,16),(16,8,16)),rail((-24,8,24),(-24,8,16)),
             rail((24,0,0),(24,0,-30)),rail((24,0,-30),(-24,0,-30)),rail((-24,0,-30),(-24,0,-10))]
    if not glider:
        rails += [rail((-24,8,16),(-24,0,-10)),rail((-16,8,16),(-16,0,-10))]
    tunnel += rails
    if battle:
        # A separate enclosed arena has no lap geometry or elevation seams.
        deck = [[(-16,0,-16),(-16,0,16),(16,0,16),(16,0,-16)]]
        tunnel = [
            [(-16,0,-16),(-16,3,-16),(-16,3,16),(-16,0,16)],
            [(16,0,-16),(16,0,16),(16,3,16),(16,3,-16)],
            [(-16,0,-16),(16,0,-16),(16,3,-16),(-16,3,-16)],
            [(-16,0,16),(-16,3,16),(16,3,16),(16,0,16)],
        ]
    if anti_gravity:
        # The return straight becomes a vertical loop through wall and
        # ceiling sections; its exit runs back LOOP_SHIFT metres inboard.
        exit_z = LOOP_Z+LOOP_SHIFT
        deck[5:6] = [[(-24,0,LOOP_Z-4),(-24,0,LOOP_Z+4),(LOOP_X,0,LOOP_Z+4),(LOOP_X,0,LOOP_Z-4)],
                     [(LOOP_X,0,exit_z-4),(LOOP_X,0,exit_z+4),(16,0,exit_z+4),(16,0,exit_z-4)]]
        deck[-1] = [(-4,0,exit_z+4),(-4,0,35),(4,0,35),(4,0,exit_z+4)]
        deck += [[loop_edge(i,-4),loop_edge(i,4),loop_edge(i+1,4),loop_edge(i+1,-4)] for i in range(LOOP_STEPS)]
    if glider:
        deck[3] = GLIDE_RAMP
        deck[4] = [(-24,0,-30),(-24,0,12),(-16,0,12),(-16,0,-30)]
    collision_deck=[quad for quad in deck if quad is not GLIDE_RAMP]
    if piranhas:
        collision_deck,capture_patch=capture_floor(deck,plant_poses[0],
            piranhas_star and not (battle or existing_actors))
    mesh('DeckRender',deck if not piranhas else collision_deck,0);mesh('TunnelRender',tunnel,1)
    if piranhas:
        # The hidden plant's capture floor must remain visible to a player.
        mesh('CaptureRender',capture_patch,2)
        mesh('CaptureCollision',capture_patch,0)
    mesh('DeckCollision',collision_deck,0);mesh('TunnelCollision',tunnel,1)
    if glider:
        mesh('GlideCollision',[GLIDE_RAMP],0)
    gltf = {'asset':{'version':'2.0','generator':'MKSC Neo authored bridge example'},
        'scene':0,'scenes':[{'nodes':list(range(len(nodes)))}],'nodes':nodes,'meshes':meshes,
        'buffers':[{'uri':'scene.bin','byteLength':len(data)}],'bufferViews':views,'accessors':accessors,
        'images':[{'uri':'deck.png'}],'textures':[{'source':0}],
        'materials':[{'name':'Checker deck','doubleSided':True,'pbrMetallicRoughness':
            {'baseColorTexture':{'index':0},'metallicFactor':0,'roughnessFactor':1}},
            {'name':'Tunnel blue','doubleSided':True,'pbrMetallicRoughness':
             {'baseColorFactor':[.13,.27,.37,1],'metallicFactor':0,'roughnessFactor':1}}]}
    if piranhas:
        gltf['materials'].append({'name':'Warm capture sand','doubleSided':True,
            'pbrMetallicRoughness':{'baseColorFactor':[211/255,150/255,75/255,1],
                                   'metallicFactor':0,'roughnessFactor':1}})
    (course/'scene.gltf').write_text(json.dumps(gltf,indent=2)+'\n')
    (course/'scene.bin').write_bytes(data)
    route = [[20,0,-18],[20,0,0],[20,8,16],[20,8,20],[-20,8,20],
             [-20,8,16],[-20,0,-10],[-20,0,-26],[20,0,-26]]
    # Gates cross straight road sections; rounded turns can cut the inside of
    # route vertices without ever crossing a plane placed at the corner itself.
    gate_poses = [([20,0,-18],[0,0,1]), ([20,4,8],[0,0,1]),
                  ([0,8,20],[-1,0,0]), ([-20,4,3],[0,0,-1]),
                  ([0,0,-26],[1,0,0])]
    gates = []
    for i, (point, normal) in enumerate(gate_poses):
        nx, _, nz = normal
        gates.append({'center':point,'normal':normal,'right':[nz,0,-nx],
            'half_width':8,'half_height':10,'key_group':i,'stage':i,
            'flags':5 if i==0 else 4,'successors':[(i+1)%len(gate_poses)]})
    yaw = math.atan2(-22,35);pitch = math.atan2(10,math.hypot(22,35))
    camera_quaternion = [math.cos(yaw/2)*math.sin(pitch/2),math.sin(yaw/2)*math.cos(pitch/2),
                         -math.sin(yaw/2)*math.sin(pitch/2),math.cos(yaw/2)*math.cos(pitch/2)]
    document = {'version':1,'id':course_id,'scene':'scene.gltf',
        'render_nodes':['DeckRender','TunnelRender'],
        'collision_nodes':[{'node':'DeckCollision','surface':0,'respawn':0},
                           {'node':'TunnelCollision','surface':1,'respawn':0}],
        'textures':[{'image':0,'id':65001}],
        'meta':{'name':'Bridge Circuit','author':'MKSC Neo','cup_name':'Authored Heights',
            'modes':['gp','tt','quick','vs'],'laps':3,'cup_id':65536,'cup_order':0,
            'physics_profile':1,'sky_rgba':'#609AC4FF','fog_rgba':'#609AC4FF','fog_near':120,'fog_far':512,
            'properties':[{'key':'example:description','type':'string','value':'Original elevated deck and tunnel course'}]},
        'surfaces':[{'type':'road','friction':1,'speed_ratio':1},{'type':'wall','friction':1,'restitution':.25}],
        'checkpoints':gates,
        # The right ramp crests straight into the deck's 90-degree turn: drivers
        # take the crest slowly, as a kart launched over it lands past the turn.
        'routes':[{'kind':'cpu','closed':True,'nodes':[{'position':p,'radius':4,
            'speed':6 if p==[20,8,16] else 15,
            'successors':[(i+1)%len(route)]} for i,p in enumerate(route)]},
            {'kind':'item','closed':True,'nodes':[{'position':p,'radius':2,'speed':20,
            'successors':[(i+1)%len(route)]} for i,p in enumerate(route)]},
            {'kind':'camera','closed':False,'nodes':[{'position':[32,14,-20],'radius':1,'speed':5,'successors':[1]},
                {'position':[30,16,24],'radius':1,'speed':5,'successors':[]}]}],
        'grid':[{'position':[18+(i%2)*4, .2, -24-(i//2)*1.5]} for i in range(8)],
        'items':[{'position':[20,1,-2],'radius':1,'respawn_ticks':180}],
        'objects':[
            {'class_id':1,'model':{'kind':96,'id':2},'position':[13,0,-20],'scale':[.15,.15,.15]},
            {'class_id':2,'model':{'kind':96,'id':3},'position':[25,0,-17],
             'properties':[{'key':'mksc:billboard','type':'boolean','value':True}]}],
        'respawns':[{'position':[20,0,-24],'checkpoint':0,'radius':4,'route':0}],
        'cameras':[{'position':[32,14,-20],'quaternion':camera_quaternion,'fov':10923,
                    'near':.1,'far':512,'route':2,'priority':1}],
        'zones':[{'center':[0,2,20],'half_extents':[5,3,8],'gravity':[0,-course_3d_format.DEFAULT_GRAVITY,0],'kind':0}]}
    if scenery_binding:
        document['objects'][0]['properties'] = [dict(key='mksc:scenery-art-bank',type='integer',value=0)]
    if anti_gravity:
        anti_gravity_loop(document)
    if glider:
        glider_path(document)
    if battle:
        route = [[-8,0,-8],[-8,0,8],[8,0,8],[8,0,-8]]
        document['meta'].update(name='Checker Battle', cup_name='Authored Arena',
                                modes=['battle'], laps=0)
        document['meta']['properties'] = [{'key':'example:description','type':'string',
                                           'value':'Original enclosed four-player battle arena'}]
        document['checkpoints'] = []
        document['routes'] = [{'kind':kind,'closed':True,'nodes':[
            {'position':point,'radius':2,'speed':8,'successors':[(i+1)%4]}
            for i,point in enumerate(route)]} for kind in ('cpu','item')]
        # Quaternion yaw maps the engine's forward -Y to glTF +Z at identity.
        document['grid'] = []
        for x,z in ((-8,-8),(8,8),(-8,8),(8,-8)):
            yaw = math.atan2(-x,-z)
            document['grid'].append({'position':[x,.2,z],
                'quaternion':[0,math.sin(yaw/2),0,math.cos(yaw/2)]})
        document['objects'] = []
        document['respawns'] = [{'position':[0,0,0],'checkpoint':None,'radius':32,'route':0}]
        document['cameras'][0].update(position=[24,22,-24],route=None)
        document['zones'] = []
    if piranhas:
        document['render_nodes'].append('CaptureRender')
        document['surfaces'].append(dict(type='road',friction=1,speed_ratio=1,properties=[
            dict(key='mksc:capture',type='boolean',value=True),
            dict(key='source:material',type='integer',value=42)]))
        document['collision_nodes'].append(dict(node='CaptureCollision',surface=2,respawn=0))
        document['meta']['properties'].extend([
            dict(key='mksc:capture-forward-speed',type='fixed',value=12),
            # 3.6 m/s keeps the bite launch's earlier flight time under MK8 gravity.
            dict(key='mksc:capture-up-speed',type='fixed',value=3.6)])
    if crabs:
        crab_controller(root,document,frames,battle,ground_follow)
    if snowmen:
        snowman_controller(root,document,snow_frames,battle,crabs)
    if penguins:
        penguin_controller(root,document,penguin_art,battle,crabs,snowmen)
    if mice:
        mouse_controllers(root,document,mouse_art,battle,crabs,snowmen,penguins)
    if piranhas:
        piranha_controllers(root,document,plant_art,plant_poses,existing_actors)
    if seagulls:
        seagull_controller(root,document,bird_art,battle,existing_actors+2*int(piranhas),seagulls_star)
    if lava_bubbles:
        lava_bubble_controller(root,document,bubble_art,battle,
            existing_actors+2*int(piranhas)+int(seagulls),lava_bubbles_star)
    if thwomps:
        thwomp_controllers(root,document,stone_art,battle,
            existing_actors+2*int(piranhas)+int(seagulls)+int(lava_bubbles),thwomps_star)
    if cannonballs:
        cannonball_controllers(root,document,ball_art,battle,
            existing_actors+2*int(piranhas)+int(seagulls)+int(lava_bubbles)+
            (1 if thwomps_star else 2)*int(thwomps),cannonballs_star)
    if boos:
        # Earlier optional Star fixtures can contain one or two actors when
        # mixed. Derive the next source ordinal from actual declarations.
        prior=sum(any(p['key']=='mksc:controller' for p in obj.get('properties',[]))
                  for obj in document['objects'])
        boo_controllers(root,document,ghost_art,battle,prior,boos_star)
    if rocks:
        prior=sum(any(p['key']=='mksc:controller' for p in obj.get('properties',[]))
                  for obj in document['objects'])
        rock_controllers(root,document,stone_cells,battle,prior,rocks_star)
    if teepees:
        prior=sum(any(p['key']=='mksc:controller' and p['value']!='coins'
                      for p in obj.get('properties',[])) for obj in document['objects'])
        teepee_controller(root,document,tent_cells,battle,prior,teepees_star)
    if falling_stars:
        prior=sum(any(p['key']=='mksc:controller' and p['value']!='coins'
                      for p in obj.get('properties',[])) for obj in document['objects'])
        falling_star_controllers(root,document,star_cells,battle,prior,falling_stars_star)
    if thunderclouds:
        prior=sum(any(p['key']=='mksc:controller' and p['value']!='coins'
                      for p in obj.get('properties',[])) for obj in document['objects'])
        thundercloud_controllers(root,document,cloud_cells,battle,prior,thunderclouds_star)
    if common_props:
        prior=sum(any(p['key']=='mksc:controller' and p['value']!='coins'
                      for p in obj.get('properties',[])) for obj in document['objects'])
        common_prop_controllers(root,document,common_cells,battle,prior,common_props_star)
    if mechakoopas:
        prior=sum(1 for obj in document['objects'] if any(p['key']=='mksc:controller' and p['value']!='coins'
                  for p in obj.get('properties',[])))
        mechakoopa_controllers(root,document,mecha_cells,battle,prior,mechakoopas_star)
    if kamek:
        prior=sum(1 for obj in document['objects'] if any(p['key']=='mksc:controller' and p['value']!='coins'
                  for p in obj.get('properties',[])))
        kamek_controller(root,document,kamek_cells,battle,prior,kamek_star)
    if pier_boos:
        prior=sum(1 for obj in document['objects'] if any(p['key']=='mksc:controller' and p['value']!='coins'
                  for p in obj.get('properties',[])))
        pier_boo_controller(root,document,pier_cells,battle,prior,pier_boos_star,cup)
    if (teepees_star or falling_stars_star or thunderclouds_star or common_props_star or mechakoopas_star or kamek_star or pier_boos_star) and not battle:
        # This bounded protected-interaction fixture starts the other seven
        # racers on the safe return straight. They keep ordinary controls and
        # collision eligibility, but cannot consume the one-shot tent before
        # the rear-grid player's genuine roulette/Star approach.
        for index in range(7):
            document['grid'][index] = dict(position=[-22+(index%2)*4,.2,-12-(index//2)*1.5],
                                          quaternion=[0,1,0,0])
    if coins:coin_placements(root,document,battle,cup)
    if items:
        document['meta']['properties'].append({'key':'mksc:item-rules','type':'asset',
                                               'value':{'kind':103,'id':1}})
        # Two rows on the approach and upper deck expose height-aware pickups.
        # Short cup cooldown lets a following field use the same driving lanes.
        document['items'] = [{'position':[x,y,z],'radius':1.25,'respawn_ticks':30 if cup else 180}
                             for x,y,z in ((18,1,-2),(22,1,-2),(-8,9,20),(8,9,20))]
        profile = item_profile(cup=cup)
        if battle:
            document['items'] = [{'position':[x,1,z],'radius':1.25,'respawn_ticks':30}
                                 for x,z in ((-8,-4),(-4,8),(8,4),(4,-8))]
            # Focus the ordinary combat fixture on repeatable targeted attacks.
            # Inventory still rolls, settles, launches and collides normally.
            profile['weights'] = {domain:[[[0x1002,1]] for _ in range(8)]
                                  for domain in ('player','cpu')}
        if penguins_star or mice_star or piranhas_star or seagulls_star or lava_bubbles_star or thwomps_star or cannonballs_star or boos_star or rocks_star or teepees_star or falling_stars_star or thunderclouds_star or common_props_star or mechakoopas_star or kamek_star or pier_boos_star or pier_boos_repel:
            profile['weights'] = {domain:[[[item,1]] for _ in range(8)]
                                  for domain,item in (('player',0x100a),('cpu',0x1007))}
            if not battle:
                # Genuine early pickup leaves time for roulette/use before contact.
                document['items'][0]['position'] = [22,1,-28.5]
                document['items'][1]['position'] = [22,1,-20]
                for box in document['items'][:2]:
                    box['respawn_ticks'] = 3600
                for placed in document['objects']:
                    family=next((p['value'] for p in placed.get('properties',[])
                                 if p['key']=='mksc:controller'),None)
                    if (penguins_star and family=='penguins' and not (crabs or snowmen)) or (
                        mice_star and family=='mice' and not (crabs or snowmen or penguins) and
                        any(p['key']=='source:variant' and p['value']==0x8001
                            for p in placed['properties'])):
                        placed['position'] = [20,0,-2]
        (root/'items.json').write_text(json.dumps({'version':1,'profiles':[profile]},indent=2)+'\n')
    base_bytes = (Path(__file__).resolve().parents[3]/'portable/data/mesh-kart.json').read_bytes()
    if class_profiles:
        base = json.loads(base_bytes)
        profiles = [base]
        for identity, speed in ((50,12),(100,18),(150,24)):
            profile = copy.deepcopy(base)
            profile['profile_id'] = identity
            # One flat ground speed at every level; 150cc keeps it exactly.
            profile['ranges']['speed_ground'] = [speed*65536, speed*65536]
            profile['class_speed_ratio_q16'] = [65536]*3
            profiles.append(profile)
            document['meta']['properties'].append({'key':f'mksc:physics-{identity}cc',
                'type':'asset','value':{'kind':102,'id':identity}})
        (root/'physics.json').write_text(json.dumps({'version':1,'profiles':profiles},indent=2)+'\n')
    else:
        (root/'physics.json').write_bytes(base_bytes)
    if sky_motion:
        document['meta']['properties'].append(dict(key='mksc:sky-motion',type='asset',value=dict(kind=106,id=1)))
        document['objects'].append(dict(class_id=0x20000002,model=dict(kind=96,id=300),position=[0,0,0],
            properties=[dict(key='mksc:sky-layer',type='integer',value=0)]))
    (course/'course.json').write_text(json.dumps(document,indent=2)+'\n')
    if cup:
        # Four colour variants of this authored layout exercise a complete cup;
        # the slower CPU corner target is ordinary editable route data.
        skies = ('#609AC4FF','#C48260FF','#8471B8FF','#529A80FF')
        for index, sky in enumerate(skies):
            target = root / 'courses3d' / ('bridge' if index == 0 else f'bridge-{index+1}')
            target.mkdir(parents=True,exist_ok=True)
            for name in ('scene.gltf','scene.bin','deck.png'):
                if target != course:
                    (target/name).write_bytes((course/name).read_bytes())
            variant = copy.deepcopy(document)
            variant['id'] = course_id+index
            variant['textures'][0]['id'] = 65001+index
            variant['meta'].update(name=f'Bridge Circuit {index+1}',cup_order=index,
                                   sky_rgba=sky,fog_rgba=sky)
            # Cup route aids drive the rivals' controller (corner speed stays first).
            variant['routes'][0]['properties'] = [
                {'key':'mksc:cpu-corner-speed','type':'fixed','value':1.5},
                {'key':'mksc:route-pilot-continuous','type':'boolean','value':True}]
            (target/'course.json').write_text(json.dumps(variant,indent=2)+'\n')
    return root


def build(output, course_id=65536, class_profiles=False, cup=False, items=False, battle=False, crabs=False, snowmen=False, penguins=False, penguins_star=False, mice=False, mice_star=False, piranhas=False, piranhas_star=False, seagulls=False, seagulls_star=False, lava_bubbles=False, lava_bubbles_star=False, thwomps=False, thwomps_star=False, cannonballs=False, cannonballs_star=False, boos=False, boos_star=False, rocks=False, rocks_star=False, coins=False, teepees=False, teepees_star=False, falling_stars=False, falling_stars_star=False, thunderclouds=False, thunderclouds_star=False, common_props=False, common_props_star=False, mechakoopas=False, mechakoopas_star=False, kamek=False, kamek_star=False, pier_boos=False, pier_boos_star=False, pier_boos_repel=False, sky_motion=False, scenery_binding=False, ground_follow=False, anti_gravity=False, glider=False):
    output = Path(output)
    name = 'battle-arena' if battle else 'bridge-cup' if cup else 'bridge-course'
    name += ('-crabs' if crabs else '')+('-snowmen' if snowmen else '')+('-penguins' if penguins else '')+('-star' if penguins_star else '')+('-mice' if mice else '')+('-star' if mice_star else '')+('-piranhas' if piranhas else '')+('-star' if piranhas_star else '')+('-seagulls' if seagulls else '')+('-star' if seagulls_star else '')+('-lava-bubbles' if lava_bubbles else '')+('-star' if lava_bubbles_star else '')+('-thwomps' if thwomps else '')+('-star' if thwomps_star else '')+('-cannonballs' if cannonballs else '')+('-star' if cannonballs_star else '')+('-boos' if boos else '')+('-star' if boos_star else '')+('-rocks' if rocks else '')+('-star' if rocks_star else '')+('-coins' if coins else '')+('-teepees' if teepees else '')+('-star' if teepees_star else '')+('-falling-stars' if falling_stars else '')+('-star' if falling_stars_star else '')+('-thunderclouds' if thunderclouds else '')+('-star' if thunderclouds_star else '')+('-common-props' if common_props else '')+('-star' if common_props_star else '')+('-mechakoopas' if mechakoopas else '')+('-star' if mechakoopas_star else '')+('-kamek' if kamek else '')+('-star' if kamek_star else '')+('-pier-boos' if pier_boos else '')+('-star' if pier_boos_star else '')+('-repel' if pier_boos_repel else '')+('-sky-motion' if sky_motion else '')+('-scenery-binding' if scenery_binding else '')+('-anti-gravity' if anti_gravity else '')+('-glider' if glider else '')
    authored = source(output.parent/'source'/name,
                      course_id,class_profiles,cup,items,battle,crabs,snowmen,penguins,penguins_star,mice,mice_star,piranhas,piranhas_star,seagulls,seagulls_star,lava_bubbles,lava_bubbles_star,thwomps,thwomps_star,cannonballs,cannonballs_star,boos,boos_star,rocks,rocks_star,coins,teepees,teepees_star,falling_stars,falling_stars_star,thunderclouds,thunderclouds_star,common_props,common_props_star,mechakoopas,mechakoopas_star,kamek,kamek_star,pier_boos,pier_boos_star,pier_boos_repel,sky_motion,scenery_binding,ground_follow,anti_gravity,glider)
    return mod_kit.build(authored,None,output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',type=Path)
    parser.add_argument('--course-id',type=int,default=65536)
    parser.add_argument('--class-profiles',action='store_true')
    parser.add_argument('--scenery-binding',action='store_true',help='bind the static tree through scenery art bank0 (non-Battle)')
    parser.add_argument('--anti-gravity',action='store_true',help='turn the return straight into an anti-gravity loop with a spin bumper')
    parser.add_argument('--glider',action='store_true',help='replace the left descent ramp with a glide kicker over a drop')
    parser.add_argument('--sky-motion',action='store_true',help='add an original clock-driven sky profile and cylinder')
    parser.add_argument('--ground-follow',action='store_true',help='place the crab patrol on the bridge slope (requires --crabs --items)')
    parser.add_argument('--crabs',action='store_true',help='add original animated crab controllers')
    parser.add_argument('--snowmen',action='store_true',help='add original two-stage snowman controllers')
    parser.add_argument('--penguins',action='store_true',help='add original six-frame penguin controllers')
    parser.add_argument('--penguins-star',action='store_true',help='author early pickups and Star-only roulette for penguin contacts')
    parser.add_argument('--mice',action='store_true',help='add original walking and hopping mouse controllers')
    parser.add_argument('--mice-star',action='store_true',help='author early pickups and Star-only roulette for mouse contacts')
    parser.add_argument('--piranhas',action='store_true',help='add original hidden plant bite/capture controllers')
    parser.add_argument('--piranhas-star',action='store_true',help='author early pickups and Star-only roulette for plant contacts')
    parser.add_argument('--pier-boos-repel',action='store_true',help='author ordinary Star escape after a source haunt in a cup')
    parser.add_argument('--pier-boos',action='store_true',help='add an original haunt prop (requires --items)')
    parser.add_argument('--pier-boos-star',action='store_true',help='author ordinary Star pickup before the haunt prop')
    parser.add_argument('--kamek',action='store_true',help='add an original flying wizard (requires --items)')
    parser.add_argument('--kamek-star',action='store_true',help='author ordinary Star pickup before the wizard')
    parser.add_argument('--mechakoopas',action='store_true',help='add original walking toys (requires --items)')
    parser.add_argument('--mechakoopas-star',action='store_true',help='author ordinary Star pickup before walking toys')
    parser.add_argument('--common-props',action='store_true',help='add original interactive common props and umbrella (requires --items)')
    parser.add_argument('--common-props-star',action='store_true',help='author early Star pickups for protected common-prop flight')
    parser.add_argument('--thunderclouds',action='store_true',help='add original shared-phase thundercloud controllers (requires --items)')
    parser.add_argument('--thunderclouds-star',action='store_true',help='author early Star pickups for protected thundercloud contacts')
    parser.add_argument('--falling-stars',action='store_true',help='add original falling-star source controllers (requires --items)')
    parser.add_argument('--falling-stars-star',action='store_true',help='author early Star pickups for protected falling-star contacts')
    parser.add_argument('--teepees',action='store_true',help='add original tent/rider source controllers (requires --items)')
    parser.add_argument('--teepees-star',action='store_true',help='author early Star pickups for protected tent/rider contacts')
    parser.add_argument('--coins',action='store_true',help='add original floor coins and explicit modern coin-cap rules (requires --items)')
    parser.add_argument('--rocks',action='store_true',help='add original falling and bouncing volcanic rock controllers')
    parser.add_argument('--rocks-star',action='store_true',help='author early Star pickups for rock contacts')
    parser.add_argument('--boos',action='store_true',help='add original bobbing ghost controllers')
    parser.add_argument('--boos-star',action='store_true',help='author early Star pickups for ghost contacts')
    parser.add_argument('--cannonballs',action='store_true',help='add original falling ball and explosion controllers')
    parser.add_argument('--cannonballs-star',action='store_true',help='author early Star pickups for ball contacts')
    parser.add_argument('--thwomps',action='store_true',help='add original stone crushing controllers')
    parser.add_argument('--thwomps-star',action='store_true',help='author early Star pickups for stone contacts')
    parser.add_argument('--lava-bubbles',action='store_true',help='add original molten droplet controllers')
    parser.add_argument('--lava-bubbles-star',action='store_true',help='author early Star pickups for bubble contacts')
    parser.add_argument('--seagulls',action='store_true',help='add original harmless flapping bird controllers')
    parser.add_argument('--seagulls-star',action='store_true',help='author early pickups and Star-only roulette for bird contacts')
    parser.add_argument('--items',action='store_true',help='add authored terrain item rules and pickup rows')
    parser.add_argument('--cup',action='store_true',help='build four authored colour variants as a GP cup')
    parser.add_argument('--battle',action='store_true',help='build a four-start Battle-only arena with real item boxes')
    arguments = parser.parse_args()
    if arguments.pier_boos_repel and not (arguments.pier_boos and arguments.items and arguments.cup
                                         and not arguments.battle and not arguments.pier_boos_star):
        parser.error("--pier-boos-repel requires --pier-boos --items --cup without --battle/--pier-boos-star")
    if arguments.pier_boos and not arguments.items:
        parser.error("--pier-boos requires --items")
    if arguments.pier_boos_star and not (arguments.pier_boos and arguments.items):
        parser.error("--pier-boos-star requires --pier-boos and --items")
    if arguments.kamek and not arguments.items:
        parser.error("--kamek requires --items")
    if arguments.kamek_star and not (arguments.kamek and arguments.items):
        parser.error("--kamek-star requires --kamek and --items")
    if arguments.mechakoopas and not arguments.items:
        parser.error("--mechakoopas requires --items")
    if arguments.mechakoopas_star and not (arguments.mechakoopas and arguments.items):
        parser.error("--mechakoopas-star requires --mechakoopas and --items")
    if arguments.common_props and not arguments.items:
        parser.error('--common-props requires --items')
    if arguments.common_props_star and not (arguments.common_props and arguments.items):
        parser.error('--common-props-star requires --common-props --items')
    if arguments.thunderclouds and not arguments.items:
        parser.error('--thunderclouds requires --items')
    if arguments.thunderclouds_star and not (arguments.thunderclouds and arguments.items):
        parser.error('--thunderclouds-star requires --thunderclouds --items')
    if arguments.falling_stars and not arguments.items:
        parser.error('--falling-stars requires --items')
    if arguments.falling_stars_star and not (arguments.falling_stars and arguments.items):
        parser.error('--falling-stars-star requires --falling-stars --items')
    if arguments.teepees and not arguments.items:
        parser.error('--teepees requires --items')
    if arguments.teepees_star and not (arguments.teepees and arguments.items):
        parser.error('--teepees-star requires --teepees --items')
    if arguments.coins and not arguments.items:
        parser.error('--coins requires --items')
    if arguments.rocks_star and not (arguments.rocks and arguments.items):
        parser.error('--rocks-star requires --rocks --items')
    if arguments.boos_star and not (arguments.boos and arguments.items):
        parser.error('--boos-star requires --boos --items')
    if arguments.cannonballs_star and not (arguments.cannonballs and arguments.items):
        parser.error('--cannonballs-star requires --cannonballs --items')
    if arguments.thwomps_star and not (arguments.thwomps and arguments.items):
        parser.error('--thwomps-star requires --thwomps --items')
    if arguments.lava_bubbles_star and not (arguments.lava_bubbles and arguments.items):
        parser.error('--lava-bubbles-star requires --lava-bubbles --items')
    if arguments.seagulls_star and not (arguments.seagulls and arguments.items):
        parser.error('--seagulls-star requires --seagulls --items')
    if arguments.piranhas_star and not (arguments.piranhas and arguments.items):
        parser.error('--piranhas-star requires --piranhas --items')
    if arguments.mice_star and not (arguments.mice and arguments.items):
        parser.error('--mice-star requires --mice --items')
    if arguments.penguins_star and not (arguments.penguins and arguments.items):
        parser.error('--penguins-star requires --penguins --items')
    if arguments.scenery_binding and arguments.battle:
        parser.error('--scenery-binding requires the non-Battle static tree fixture')
    if arguments.battle and (arguments.cup or arguments.class_profiles):
        parser.error('--battle cannot be combined with --cup or --class-profiles')
    if arguments.output_dir is None:
        arguments.output_dir = default_mods_directory()
    folder,archive = build(arguments.output_dir,arguments.course_id,arguments.class_profiles,arguments.cup,arguments.items,arguments.battle,arguments.crabs,arguments.snowmen,arguments.penguins,arguments.penguins_star,arguments.mice,arguments.mice_star,arguments.piranhas,arguments.piranhas_star,arguments.seagulls,arguments.seagulls_star,arguments.lava_bubbles,arguments.lava_bubbles_star,arguments.thwomps,arguments.thwomps_star,arguments.cannonballs,arguments.cannonballs_star,arguments.boos,arguments.boos_star,arguments.rocks,arguments.rocks_star,arguments.coins,arguments.teepees,arguments.teepees_star,arguments.falling_stars,arguments.falling_stars_star,arguments.thunderclouds,arguments.thunderclouds_star,arguments.common_props,arguments.common_props_star,arguments.mechakoopas,arguments.mechakoopas_star,arguments.kamek,arguments.kamek_star,arguments.pier_boos,arguments.pier_boos_star,arguments.pier_boos_repel,arguments.sky_motion,arguments.scenery_binding,arguments.ground_follow,arguments.anti_gravity,arguments.glider)
    print(f'3D bridge course: {folder} archive={archive}')
