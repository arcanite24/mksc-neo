#!/usr/bin/env python3
"""Build a three-color sky/scenery cycle from an owner's imported pack.

python3 mods/examples/ambient-cycle/build.py BASE.pack --output-dir MODS/ambient-cycle
The example changes palette indices, leaving track art, geometry and rules intact.
"""
import argparse
import json
from pathlib import Path
import struct
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / 'tools/portable'))
from mod_kit import default_mods_directory
import ambient_rom
import build_asset_pack
import track_format

TINTS = ((31, 8, 16), (8, 24, 31), (24, 12, 31))


def tinted(palette, tint):
    """Keep the original shading while mixing in one authored RGB555 hue."""
    result = bytearray()
    for (color,) in struct.iter_unpack('<H', palette):
        channels = [(((color >> (5 * i)) & 31) * 3 + tint[i] * 2) // 5
                    for i in range(3)]
        result += struct.pack('<H', sum(c << (5 * i) for i, c in enumerate(channels)))
    return bytes(result)


def build(base, output, course=24, ticks=20):
    if type(course) is not int or not 0 <= course <= 191:
        raise ValueError('course must be an ID in 0..191')
    if type(ticks) is not int or not 1 <= ticks <= 65535:
        raise ValueError('ticks must be in 1..65535')
    sections = {(meta['kind'], meta['content_id']): (meta, bytes(data))
                for meta, data in build_asset_pack.read_sections(base)}
    try:
        meta, source = sections[(84, course)]
        _, sky = sections[(9, course)]
    except KeyError:
        raise ValueError(f'base pack needs course {course} sky and ambient sections; reimport it') from None
    if meta['version'] != 1:
        raise ValueError('unsupported ambient section version')
    ambient_rom.validate(source)
    _, _, lanes, frames, patches, initial, _, seed_offset = struct.unpack_from('<8I', source)
    blob = 32 + lanes * 32 + frames * 8 + (patches + initial) * 12
    seed = source[blob + seed_offset:blob + seed_offset + 256]
    tile_bytes, colors, _ = track_format.sky_layout(sky)
    sky_palette = sky[64 + tile_bytes:64 + tile_bytes + colors * 2]
    builder = ambient_rom.Builder(seed)
    builder.lane([(ticks, [(ambient_rom.SKY, 0, tinted(sky_palette, tint)),
                           (ambient_rom.OBJECT, 0, tinted(seed[:96], tint))])
                  for tint in TINTS], stage=ambient_rom.POST, policy=ambient_rom.ENTRY,
                 controller=ambient_rom.TIMER, trigger=ambient_rom.AUTO, loop=0)
    payload = builder.build()
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    name = f'84-{course}.bin'
    (output / name).write_bytes(payload)
    entry = {'kind': 84, 'content_id': course, 'version': 1,
             'flags': meta['flags'], 'semantic_revision': meta['semantic_revision'], 'path': name}
    (output / 'pack.json').write_text(json.dumps({'sections': [entry]}, indent=2) + '\n')
    (output / 'mod.json').write_bytes((HERE / 'mod.json').read_bytes())
    build_asset_pack.build_pack(output / 'pack.json', output / 'content.pack')
    return payload


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('base', type=Path)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--course', type=int, default=24)
    parser.add_argument('--ticks', type=int, default=20)
    args = parser.parse_args()
    if args.output_dir is None:
        args.output_dir = default_mods_directory() / 'ambient-cycle'
    try:
        build(args.base, args.output_dir, args.course, args.ticks)
    except ValueError as error:
        parser.exit(1, f'ambient-cycle: {error}\n')
    print(f'Built ambient-cycle for course {args.course} in {args.output_dir}')


if __name__ == '__main__':
    main()
