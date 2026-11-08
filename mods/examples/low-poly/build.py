#!/usr/bin/env python3
"""Build original, procedural kart/tree models through the normal mod kit.

Example: python3 mods/examples/low-poly/build.py --output-dir .mksc-tools/portable/runs/r2/mods
Use --driver and --course/--art-bank to bind your own manifest content instead.
"""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools' / 'portable'))
from mod_kit import default_mods_directory
import mod_kit


def cuboid(vertices, triangles, center, half, material):
    offset = len(vertices)
    for x, y, z in ((-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1),
                    (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)):
        vertices.append([center[0] + x * half[0], center[1] + y * half[1],
                         center[2] + z * half[2], (x + 1) / 2, (y + 1) / 2])
    for a, b, c, d in ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
                       (3, 7, 6, 2), (0, 4, 7, 3), (1, 2, 6, 5)):
        triangles.extend([[offset + a, offset + b, offset + c, material],
                          [offset + a, offset + c, offset + d, material]])


def model_document(driver, course, art_bank):
    kart_v, kart_t, tree_v, tree_t = [], [], [], []
    cuboid(kart_v, kart_t, (0, 4, 0), (6, 2, 8), 0)
    cuboid(kart_v, kart_t, (0, 8, -2), (3, 3, 3), 1)
    for x in (-6, 6):
        for z in (-5, 5):
            cuboid(kart_v, kart_t, (x, 2, z), (2, 2, 2), 2)
    cuboid(tree_v, tree_t, (0, 7, 0), (2, 7, 2), 0)
    # An angular canopy has its own geometry rather than extracted game art.
    tree_v += [[-10, 10, -10, 0, 0], [10, 10, -10, 1, 0],
               [10, 10, 10, 1, 1], [-10, 10, 10, 0, 1], [0, 30, 0, .5, .5]]
    tree_t += [[8, 9, 12, 1], [9, 10, 12, 1], [10, 11, 12, 1],
               [11, 8, 12, 1], [8, 11, 10, 1], [8, 10, 9, 1]]
    # Source world coordinates use Z-up; the construction helper above uses Y-up.
    for vertices in (kart_v, tree_v):
        for vertex in vertices:
            vertex[1], vertex[2] = vertex[2], vertex[1]
    return {'version': 1, 'materials': [
        {'id': 1, 'argb': '#FF26BED8'}, {'id': 2, 'argb': '#FFFFCE52'},
        {'id': 3, 'argb': '#FF293342'}, {'id': 4, 'argb': '#FF805438'},
        {'id': 5, 'argb': '#FF50B060'}],
        'meshes': [{'id': 1, 'materials': [1, 2, 3], 'vertices': kart_v, 'triangles': kart_t},
                   {'id': 2, 'materials': [4, 5], 'vertices': tree_v, 'triangles': tree_t}],
        'bindings': [{'kind': 'driver', 'target': driver, 'mesh': 1},
                     {'kind': 'scenery', 'target': course, 'art_bank': art_bank, 'mesh': 2}]}


def build(output, driver=1, course=24, art_bank=0):
    output = Path(output)
    source = output.parent / 'source' / 'low-poly'
    source.mkdir(parents=True, exist_ok=True)
    (source / 'mod.json').write_text(json.dumps({
        'id': 'low-poly', 'name': 'Low Poly', 'version': '1.0.0', 'api_version': 1}, indent=2) + '\n')
    (source / 'models.json').write_text(json.dumps(model_document(driver, course, art_bank), indent=2) + '\n')
    return mod_kit.build(source, None, output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', '--output', dest='output', type=Path)
    parser.add_argument('--driver', type=int, default=1, help='manifest driver ID')
    parser.add_argument('--course', type=int, default=24, help='manifest course ID (Peach Circuit by default)')
    parser.add_argument('--art-bank', type=int, default=0, help='course-local scenery art bank')
    args = parser.parse_args()
    if args.output is None:
        args.output = default_mods_directory()
    folder, archive = build(args.output, args.driver, args.course, args.art_bank)
    print(f'Built {folder}\nZIP {archive}')


if __name__ == '__main__':
    main()
