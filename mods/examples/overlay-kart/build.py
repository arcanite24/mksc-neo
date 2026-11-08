#!/usr/bin/env python3
"""Build an original cyan kart as an independent dual-target model overlay."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/portable'))
import mod_kit

MESH_ID = 131001
MATERIAL_ID = 131101
COLOR = (19,199,237)
DEFAULT_OUTPUT = ROOT/'.mksc-tools/portable/runs/m13/mods'


def model_document(driver=1):
    spec = importlib.util.spec_from_file_location('original_low_poly',ROOT/'mods/examples/low-poly/build.py')
    geometry = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(geometry)
    vertices,triangles = [],[]
    geometry.cuboid(vertices,triangles,(0,3,0),(9,3,13),0)
    geometry.cuboid(vertices,triangles,(0,8,-3),(5,3,6),0)
    for x in (-9,9):
        for z in (-8,8):
            geometry.cuboid(vertices,triangles,(x,2,z),(2,2,3),0)
    for vertex in vertices:
        vertex[1],vertex[2] = vertex[2],vertex[1]
    return dict(version=1,binding_mode='overlay',
        materials=[dict(id=MATERIAL_ID,argb='#FF13C7ED')],
        meshes=[dict(id=MESH_ID,materials=[MATERIAL_ID],vertices=vertices,triangles=triangles)],
        bindings=[dict(kind='driver',target=driver,mesh=MESH_ID)])


def source(root,driver=1):
    root = Path(root)
    root.mkdir(parents=True,exist_ok=True)
    (root/'mod.json').write_text(json.dumps(dict(id='overlay-kart',name='Cyan Overlay Kart',
        version='1.0.0',api_version=1,targets=['2d','3d']),indent=2)+'\n')
    (root/'models.json').write_text(json.dumps(model_document(driver),indent=2)+'\n')
    return root


def build(output=DEFAULT_OUTPUT,driver=1):
    output = Path(output)
    return mod_kit.build(source(output.parent/'source/overlay-kart',driver),None,output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',type=Path,default=DEFAULT_OUTPUT)
    parser.add_argument('--driver',type=int,default=1,help='exact manifest driver ID')
    args = parser.parse_args()
    folder,archive = build(args.output_dir,args.driver)
    print(f'Overlay kart: {folder}\nZIP {archive}')
