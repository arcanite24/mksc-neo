#!/usr/bin/env python3
"""Select a surface-trail variant and tint its art from an owner's imported pack."""
import argparse
import json
from pathlib import Path
import struct
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / 'tools/portable'))
from mod_kit import default_mods_directory
import build_asset_pack


def tint_color(value):
    presets = {'cyan': (0, 255, 255), 'purple': (192, 64, 255), 'orange': (255, 128, 0)}
    if isinstance(value, str):
        if value.lower() in presets:
            value = presets[value.lower()]
        elif len(value) == 7 and value.startswith('#'):
            try:
                value = tuple(int(value[i:i+2], 16) for i in (1, 3, 5))
            except ValueError:
                raise ValueError('tint must be cyan, purple, orange, R,G,B or #RRGGBB') from None
        else:
            try:
                value = tuple(int(channel) for channel in value.split(','))
            except ValueError:
                raise ValueError('tint must be cyan, purple, orange, R,G,B or #RRGGBB') from None
    if not isinstance(value, (tuple, list)) or len(value) != 3 or any(type(v) is not int or not 0 <= v <= 255 for v in value):
        raise ValueError('tint channels must be integers in0..255')
    return tuple(v * 31 // 255 for v in value)


def validate_rules(data):
    if (len(data) != 280 or struct.unpack_from('<II', data) != (0x314C5254, 256) or
            any(not 1 <= v <= 5 for v in data[8:264]) or any(data[277:])):
        raise ValueError('base pack has invalid surface trail rules; reimport it')
    if any(not 1 <= data[264+i*2] < data[265+i*2] <= 29 for i in range(5)):
        raise ValueError('base pack has invalid surface trail frame cutoffs')


def tint_art(source, group, tint):
    frames = 1 if group == 4 else 3
    if (len(source) < 32 or struct.unpack_from('<8I', source) !=
            (0x31435846, 0x2000, 80, frames, 0x100, 3, frames*8, 0) or
            len(source) != 32+frames*8+0x2000+160+frames*8):
        raise ValueError('base pack has invalid surface trail art; reimport it')
    palette = 32+frames*8+0x2000
    result = bytearray(source)
    for index in range(80):
        color, = struct.unpack_from('<H', source, palette+index*2)
        channels = [(((color >> (5*i)) & 31)*3+tint[i]*2)//5 for i in range(3)]
        struct.pack_into('<H', result, palette+index*2, (color & 0x8000) | sum(c << (5*i) for i, c in enumerate(channels)))
    return bytes(result)


def build(base, output, kind=4, tint='cyan', spray_material=None):
    if type(kind) is not int or not 1 <= kind <= 5:
        raise ValueError('kind must be in1..5')
    if spray_material is not None and (type(spray_material) is not int or not 0 <= spray_material <= 255):
        raise ValueError('spray material must be in0..255')
    tint = tint_color(tint)
    sections = {(meta['kind'], meta['content_id']): (meta, bytes(data))
                for meta, data in build_asset_pack.read_sections(base)}
    try:
        rule_meta, source = sections[(89, 1)]
        art = [sections[(19, 9+i)] for i in range(5)]
    except KeyError:
        raise ValueError('base pack needs surface trail rules and art; reimport it') from None
    if rule_meta['version'] != 1 or any(meta['version'] != 1 for meta, _ in art):
        raise ValueError('unsupported surface trail section version')
    validate_rules(source)
    rules = bytearray(source)
    rules[8:264] = bytes([kind])*256
    if spray_material is not None:
        rules[276] = spray_material
    payloads = [(rule_meta, bytes(rules))] + [(meta, tint_art(data, i, tint)) for i, (meta, data) in enumerate(art)]
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    entries = []
    for meta, data in payloads:
        name = f"{meta['kind']}-{meta['content_id']}.bin"
        (output / name).write_bytes(data)
        entries.append({key: meta[key] for key in ('kind', 'content_id', 'version', 'flags', 'semantic_revision')} | {'path': name})
    (output / 'pack.json').write_text(json.dumps({'sections': entries}, indent=2)+'\n')
    (output / 'mod.json').write_bytes((HERE / 'mod.json').read_bytes())
    build_asset_pack.build_pack(output / 'pack.json', output / 'content.pack')
    return bytes(rules)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base', type=Path, nargs='?')
    parser.add_argument('--base', dest='base_option', type=Path)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--kind', type=int, choices=range(1, 6), default=4)
    parser.add_argument('--tint', default='cyan')
    parser.add_argument('--spray-material', type=lambda value: int(value, 0))
    args = parser.parse_args()
    if args.output_dir is None:
        args.output_dir = default_mods_directory() / 'surface-trails'
    if (args.base is None) == (args.base_option is None):
        parser.error('provide one base pack, positionally or with --base')
    try:
        build(args.base or args.base_option, args.output_dir, args.kind, args.tint, args.spray_material)
    except ValueError as error:
        parser.exit(1, f'surface-trails: {error}\n')
    print(f'Built surface-trails kind{args.kind} in {args.output_dir}')


if __name__ == '__main__':
    main()
