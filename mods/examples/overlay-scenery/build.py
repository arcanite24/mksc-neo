#!/usr/bin/env python3
"""Build an original pink tree as an independent dual-target model overlay."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/portable'))
import mod_kit

MESH_ID = 131002
MATERIAL_ID = 131102
COLOR = (241,73,163)
DEFAULT_OUTPUT = ROOT/'.mksc-tools/portable/runs/m13/mods'


def model_document(course=65536,art_bank=0,pose=0):
    spec = importlib.util.spec_from_file_location('original_low_poly',ROOT/'mods/examples/low-poly/build.py')
    geometry = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(geometry)
    vertices,triangles = [],[]
    geometry.cuboid(vertices,triangles,(0,6,0),(1.5,6,1.5),0)
    vertices += [[-8,10,-8,0,0],[8,10,-8,1,0],[8,10,8,1,1],[-8,10,8,0,1],[0,28,0,.5,.5]]
    triangles += [[8,9,12,0],[9,10,12,0],[10,11,12,0],[11,8,12,0],[8,11,10,0],[8,10,9,0]]
    for vertex in vertices:
        vertex[1],vertex[2] = vertex[2],vertex[1]
    return dict(version=1,binding_mode='overlay',
        materials=[dict(id=MATERIAL_ID,argb='#FFF149A3')],
        meshes=[dict(id=MESH_ID,materials=[MATERIAL_ID],vertices=vertices,triangles=triangles)],
        bindings=[dict(kind='scenery',target=course,art_bank=art_bank,pose=pose,mesh=MESH_ID,
                       scale=[1,1,1],pivot=[0,0,0])])


def source(root,course=65536,art_bank=0,pose=0):
    root = Path(root)
    root.mkdir(parents=True,exist_ok=True)
    (root/'mod.json').write_text(json.dumps(dict(id='overlay-scenery',name='Pink Overlay Tree',
        version='1.0.0',api_version=1,targets=['2d','3d']),indent=2)+'\n')
    (root/'models.json').write_text(json.dumps(model_document(course,art_bank,pose),indent=2)+'\n')
    return root


def build(output=DEFAULT_OUTPUT,course=65536,art_bank=0,pose=0):
    output = Path(output)
    return mod_kit.build(source(output.parent/'source/overlay-scenery',course,art_bank,pose),None,output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',type=Path,default=DEFAULT_OUTPUT)
    parser.add_argument('--course',type=int,default=65536,help='exact course ID')
    parser.add_argument('--art-bank',type=int,default=0)
    parser.add_argument('--pose',type=int,default=0)
    args = parser.parse_args()
    folder,archive = build(args.output_dir,args.course,args.art_bank,args.pose)
    print(f'Overlay scenery: {folder}\nZIP {archive}')
