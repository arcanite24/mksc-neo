#!/usr/bin/env python3
"""Author original pixel snow and a weather profile without ROM-derived data."""
import argparse
import json
from pathlib import Path
import struct
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / 'tools/portable'))
from mod_kit import default_mods_directory
import build_asset_pack


def payload(count=12):
    if type(count) is not int or not 1 <= count <= 12:
        raise ValueError('particle count must be in1..12')
    images = []
    for radius in (0, 1, 2):
        pixels = bytearray([255] * 64)
        for y in range(8):
            for x in range(8):
                if abs(x-3)+abs(y-3) <= radius:
                    pixels[y*8+x] = 1
        palette = struct.pack('<2H', 0, 0x7FFF)
        images.append(struct.pack('<8sHHHHhhIII', b'MKSCIMG2', 32, 2, 8, 8,
                                  0, 0, 2, 32, 100) + pixels + palette)
    # Authored uniform choices demonstrate all four source motion stages.
    choices = tuple(range(4))*4
    return (struct.pack('<IIBB2x16B3I2I', 0x31485457, 1, 1, count,
                        *choices, *(len(image) for image in images), 0, 0) + b''.join(images))


def build(output, course=24, count=12):
    if type(course) is not int or not 0 <= course <= 191:
        raise ValueError('course must be an ID in0..191')
    data = payload(count)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    name = f'87-{course}.bin'
    (output / name).write_bytes(data)
    entry = {'kind': 87, 'content_id': course, 'version': 1, 'flags': 0,
             'semantic_revision': 1, 'path': name}
    (output / 'pack.json').write_text(json.dumps({'sections': [entry]}, indent=2)+'\n')
    (output / 'mod.json').write_bytes((HERE / 'mod.json').read_bytes())
    build_asset_pack.build_pack(output / 'pack.json', output / 'content.pack')
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--course', type=int, default=24)
    parser.add_argument('--count', type=int, default=12)
    args = parser.parse_args()
    if args.output_dir is None:
        args.output_dir = default_mods_directory() / 'snow-weather'
    try:
        build(args.output_dir, args.course, args.count)
    except ValueError as error:
        parser.exit(1, f'snow-weather: {error}\n')
    print(f'Built snow-weather for course {args.course} in {args.output_dir}')


if __name__ == '__main__':
    main()
