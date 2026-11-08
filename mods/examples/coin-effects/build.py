#!/usr/bin/env python3
"""Tint dropped/rising coins and tune their imported motion and bounce volume."""
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
    presets = {'cyan': (0, 255, 255), 'purple': (192, 64, 255), 'gold': (255, 192, 0)}
    if isinstance(value, str):
        if value.lower() in presets:
            value = presets[value.lower()]
        else:
            try:
                value = (tuple(int(value[i:i+2], 16) for i in (1, 3, 5))
                         if len(value) == 7 and value.startswith('#') else tuple(int(v) for v in value.split(',')))
            except ValueError:
                raise ValueError('tint must be cyan, purple, gold, R,G,B or #RRGGBB') from None
    if not isinstance(value, (tuple, list)) or len(value) != 3 or any(type(v) is not int or not 0 <= v <= 255 for v in value):
        raise ValueError('tint channels must be integers in0..255')
    return tuple(v*31//255 for v in value)


def validate_rules(data):
    if len(data) != 104 or struct.unpack_from('<4I', data) != (0x31584643, 9, 3, 4) or any(data[102:]):
        raise ValueError('invalid coin effect rules; reimport base pack')
    horizontal = struct.unpack_from('<9i', data, 16)
    upward = struct.unpack_from('<9i', data, 52)
    volumes = struct.unpack_from('<3H', data, 88)
    ticks = struct.unpack_from('<4H', data, 94)
    if (any(not -64 <= v <= 64 for v in horizontal) or any(not 0 <= v <= 64*65536 for v in upward) or
            any(v > 256 for v in volumes) or any(not 1 <= v <= 8192 for v in ticks)):
        raise ValueError('invalid coin effect rule bounds')


def tint_art(source, tint, ticks):
    if len(source) < 32:
        raise ValueError('invalid coin art header')
    magic, tiles, colors, count, tile_base, palette_base, cell_bytes, reserved = struct.unpack_from('<8I', source)
    if (magic != 0x31435846 or tiles != 0x2000 or colors != 80 or not 1 <= count <= 32 or
            tile_base != 0x100 or palette_base != 3 or reserved or cell_bytes > 528 or
            len(source) != 32+count*8+tiles+colors*2+cell_bytes):
        raise ValueError('invalid coin art layout; reimport base pack')
    cells = 32+count*8+tiles+colors*2
    total = 0
    for index in range(count):
        offset, size, duration = struct.unpack_from('<IHH', source, 32+index*8)
        if not 1 <= duration <= 255 or size < 2 or offset+size > cell_bytes:
            raise ValueError('invalid coin art frame')
        if size != 2+struct.unpack_from('<H', source, cells+offset)[0]*6:
            raise ValueError('invalid coin art cell')
        total += duration+1
    if total != ticks:
        raise ValueError('coin art lifespan differs from rules')
    result = bytearray(source)
    palette = 32+count*8+tiles
    for index in range(colors):
        color, = struct.unpack_from('<H', source, palette+index*2)
        channels = [(((color >> (5*i)) & 31)*3+tint[i]*2)//5 for i in range(3)]
        struct.pack_into('<H', result, palette+index*2, (color & 0x8000) | sum(v << (5*i) for i, v in enumerate(channels)))
    return bytes(result)


def build(base, output, tint='cyan', motion_percent=100, bounce_volume=None):
    if type(motion_percent) is not int or not 1 <= motion_percent <= 400:
        raise ValueError('motion percent must be in1..400')
    if bounce_volume is not None and (type(bounce_volume) is not int or not 0 <= bounce_volume <= 256):
        raise ValueError('bounce volume must be in0..256')
    tint = tint_color(tint)
    sections = {(meta['kind'], meta['content_id']): (meta, bytes(data)) for meta, data in build_asset_pack.read_sections(base)}
    try:
        rule_meta, source = sections[(90, 1)]
        art = [sections[(19, content)] for content in (14, 15)]
    except KeyError:
        raise ValueError('base pack needs coin effect rules and art; reimport it') from None
    if rule_meta['version'] != 1 or any(meta['version'] != 1 for meta, _ in art):
        raise ValueError('unsupported coin effect section version')
    validate_rules(source)
    rules = bytearray(source)
    for index, value in enumerate(struct.unpack_from('<18i', source, 16)):
        scaled = abs(value)*motion_percent//100
        struct.pack_into('<i', rules, 16+index*4, -scaled if value < 0 else scaled)
    if bounce_volume is not None:
        struct.pack_into('<3H', rules, 88, *([bounce_volume]*3))
    validate_rules(rules)
    ticks = struct.unpack_from('<4H', rules, 94)
    payloads = [(rule_meta, bytes(rules))] + [(meta, tint_art(data, tint, ticks[i+1])) for i, (meta, data) in enumerate(art)]
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
    parser.add_argument('--tint', default='cyan')
    parser.add_argument('--motion-percent', type=int, default=100)
    parser.add_argument('--bounce-volume', type=int)
    args = parser.parse_args()
    if args.output_dir is None:
        args.output_dir = default_mods_directory() / 'coin-effects'
    if (args.base is None) == (args.base_option is None):
        parser.error('provide one base pack, positionally or with --base')
    try:
        build(args.base or args.base_option, args.output_dir, args.tint, args.motion_percent, args.bounce_volume)
    except ValueError as error:
        parser.exit(1, f'coin-effects: {error}\n')
    print(f'Built coin-effects in {args.output_dir}')


if __name__ == '__main__':
    main()
