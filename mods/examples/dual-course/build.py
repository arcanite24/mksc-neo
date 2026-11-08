#!/usr/bin/env python3
"""Build one authored loop for both recovered 2D and modern 3D Time Trial.

Committed layout/code is original. Generation requires an owner-imported pack
for the 2D template's shared art/rules; generated sources and ZIP stay local.
Edit tracks/oval for 2D, or courses3d/128 glTF/course.json for the mesh version,
then rebuild that source folder with the ordinary mod kit and the same base.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/portable'))
import convert_course_3d
import mod_kit
import track_format

DEFAULT_OUTPUT = ROOT/'.mksc-tools/portable/runs/t6/mods'
COURSE_ID = 128


def source(base,directory):
    """Create fresh editable source; never overwrite an artist's existing work."""
    directory = Path(directory)
    if directory.exists() or directory.is_symlink():
        raise ValueError('dual-course source must be fresh; rebuild edited sources with mod_kit')
    directory.parent.mkdir(parents=True,exist_ok=True)
    base = Path(base)
    sections = track_format.load_pack(base)
    spec = importlib.util.spec_from_file_location('original_custom_cup',ROOT/'mods/examples/custom-cup/build.py')
    author = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(author)
    with tempfile.TemporaryDirectory(dir=directory.parent,prefix='dual-course-') as temporary:
        staging = Path(temporary)
        track = staging/'author/tracks/oval'
        definition = author.load_course(author.HERE/'courses/oval.json')
        author.paint_course(definition,sections,track,staging/'templates')
        generated = convert_course_3d.generate(base,staging/'source',courses=(),tracks=[track],mod_id='dual-course')
        shutil.copytree(track,generated/'tracks/oval')
        (generated/'cup.json').write_text(json.dumps(dict(cup=0,name='DUAL LOOP',tracks=['oval']),indent=2)+'\n')
        manifest = json.loads((generated/'mod.json').read_text())
        manifest.update(name='Dual Course',targets=['2d','3d'])
        (generated/'mod.json').write_text(json.dumps(manifest,indent=2)+'\n')
        os.replace(generated,directory)
    return directory


def build(base,output=DEFAULT_OUTPUT):
    output = Path(output)
    authored = source(base,output.parent/'source/dual-course')
    return mod_kit.build(authored,base,output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base',type=Path,required=True,help='owner-imported game.pack; never modified')
    parser.add_argument('--output-dir',type=Path,default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    folder,archive = build(args.base,args.output_dir)
    print(f'Dual authored course: {folder}\nZIP {archive}')
