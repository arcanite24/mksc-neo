"""Strict MIR1/v1 authored SI item rules; no imported pixel/gap units."""
import argparse
import json
import math
from pathlib import Path
import struct

KIND=103
VERSION=1
MAGIC=0x3152494d
MAX_FILE=1024*1024
MAX_PROFILES=256
# group, field, inclusive wire bounds, SI Q16 conversion; scalar wire order.
FIELDS = (
    ('actors', 'capacity', 1, 64, False),
    ('actors', 'radius', 3277, 524288, True),
    ('actors', 'lifetime_ticks', 1, 65535, False),
    ('actors', 'owner_grace_ticks', 0, 65535, False),
    ('actors', 'release_lock_ticks', 1, 32767, False),
    ('actors', 'hold_distance', 0, 524288, True),
    ('actors', 'hold_height', 0, 524288, True),
    ('actors', 'orbit_radius', 1, 524288, True),
    ('actors', 'orbit_turn_rate', 1, 262144, True),
    ('actors', 'orbit_delay_0', 0, 32767, False),
    ('actors', 'orbit_delay_1', 0, 32767, False),
    ('actors', 'orbit_delay_2', 0, 32767, False),
    ('shell', 'speed', 1, 16777216, True),
    ('shell', 'launch_bonus', 0, 16777216, True),
    ('shell', 'acceleration', 0, 67108864, True),
    ('shell', 'restitution', 0, 65536, True),
    ('shell', 'max_bounces', 1, 64, False),
    ('shell', 'homing_turn_rate', 1, 262144, True),
    ('shell', 'homing_range', 1, 268435456, True),
    ('shell', 'ground_probe', 1, 524288, True),
    ('shell', 'ground_snap', 0, 524288, True),
    ('shell', 'trap_arm_ticks', 1, 32767, False),
    ('shell', 'trap_lifetime_ticks', 1, 65535, False),
    ('shell', 'trap_lock_ticks', 1, 32767, False),
    ('banana', 'throw_speed', 1, 16777216, True),
    ('banana', 'throw_up_speed', 0, 16777216, True),
    ('banana', 'gravity', 1, 67108864, True),
    ('hit', 'spin_ticks', 1, 32767, False),
    ('hit', 'invulnerability_ticks', 1, 32767, False),
    ('hit', 'speed_ratio', 0, 65536, True),
    ('hit', 'impulse', 0, 16777216, True),
    ('mushroom', 'boost_ticks', 1, 8192, False),
    ('mushroom', 'acceleration', 0, 67108864, True),
    ('mushroom', 'cap_ratio', 65536, 262144, True),
    ('mushroom', 'golden_ticks', 1, 65535, False),
    ('star', 'player_ticks', 1, 32767, False),
    ('star', 'cpu_ticks', 1, 32767, False),
    ('star', 'acceleration', 0, 67108864, True),
    ('star', 'cap_ratio', 65536, 262144, True),
    ('lightning', 'duration_ticks', 48, 32767, False),
    ('lightning', 'speed_ratio', 3277, 65536, True),
    ('lightning', 'scale_ratio', 3277, 65536, True),
    ('boo', 'invisibility_ticks', 1, 32767, False),
    ('boo', 'theft_lock_ticks', 1, 32767, False),
    ('cpu', 'decision_ticks', 1, 32767, False),
    ('cpu', 'hold_ticks', 1, 32767, False),
    ('cpu', 'use_distance', 1, 268435456, True),
    ('cpu', 'backward_distance', 0, 268435456, True),
)
SIZE=32+len(FIELDS)*4+37*4+2*8*16*4


def exact(record, keys, label):
    if type(record) is not dict or set(record) != set(keys):
        raise ValueError(f'{label}: missing or unknown fields')


def integer(value, low, high, label):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'{label}: expected integer {low}..{high}')
    return value


def fixed(value, low, high, label):
    if type(value) not in (int, float):
        raise ValueError(f'{label}: expected finite SI number')
    # Bound before conversion/multiplication, including arbitrarily large JSON ints.
    if not low / 65536 <= value <= high / 65536 or not math.isfinite(value):
        raise ValueError(f'{label}: outside SI range {low / 65536}..{high / 65536}')
    result = math.floor(value * 65536 + .5)
    return integer(result, low, high, label)


def item_id(value, preview=False):
    return type(value) is int and (0x1000 <= value <= 0x100c or
                                  preview and value in (0x100e, 0x100f))


def normalize(record):
    groups = {group for group, *_ in FIELDS}
    exact(record, groups | {'profile_id', 'revision', 'roulette', 'weights'}, 'item profile')
    profile_id = integer(record['profile_id'], 1, 0xfffffffe, 'profile_id')
    revision = integer(record['revision'], 1, 0xffffffff, 'revision')
    for group in groups:
        exact(record[group], [name for owner, name, *_ in FIELDS if owner == group], group)
    words = []
    values = {}
    for group, name, low, high, si in FIELDS:
        label = f'{group}.{name}'
        value = (fixed if si else integer)(record[group][name], low, high, label)
        words.append(value)
        values[label] = value
    if values['actors.release_lock_ticks'] > values['shell.trap_arm_ticks']:
        raise ValueError('release lock exceeds trap arm time')
    if values['shell.trap_lock_ticks'] > values['shell.trap_lifetime_ticks']:
        raise ValueError('trap lock exceeds trap lifetime')
    if values['shell.ground_snap'] > values['shell.ground_probe']:
        raise ValueError('ground snap exceeds probe')
    exact(record['roulette'], ('preview', 'timeline'), 'roulette')
    for field, count in (('preview', 10), ('timeline', 27)):
        row = record['roulette'][field]
        if type(row) is not list or len(row) != count:
            raise ValueError(f'roulette.{field}: expected {count} entries')
        for value in row:
            if field == 'preview':
                if not item_id(value, True):
                    raise ValueError('roulette.preview: invalid logical item')
            else:
                integer(value, 1, 32767, 'roulette.timeline')
            words.append(value)
    exact(record['weights'], ('player', 'cpu'), 'weights')
    weights = bytearray()
    for domain in ('player', 'cpu'):
        ranks = record['weights'][domain]
        if type(ranks) is not list or len(ranks) != 8:
            raise ValueError(f'weights.{domain}: expected eight rank rows')
        for rank, row in enumerate(ranks):
            if type(row) is not list or not 1 <= len(row) <= 16:
                raise ValueError(f'weights.{domain}[{rank}]: expected 1..16 awards')
            seen, total = set(), 0
            for entry in row:
                if type(entry) is not list or len(entry) != 2 or not item_id(entry[0]):
                    raise ValueError(f'weights.{domain}[{rank}]: invalid award pair')
                item, weight = entry
                integer(weight, 1, 65535, 'item weight')
                if item in seen:
                    raise ValueError('duplicate item in weight row')
                seen.add(item)
                total += weight
                if total > 65535:
                    raise ValueError('rank weight sum exceeds 65535')
                weights.extend(struct.pack('<HH', item, weight))
            weights.extend(bytes((16-len(row))*4))
    header = struct.pack('<8I', MAGIC, VERSION, SIZE, profile_id, revision, len(FIELDS), 8, 16)
    return header + struct.pack(f'<{len(words)}I', *words) + weights


def decode(payload):
    if len(payload) != SIZE:
        raise ValueError('MIR1: incorrect size')
    header = struct.unpack_from('<8I', payload)
    if header[:3] != (MAGIC, VERSION, SIZE) or header[5:] != (len(FIELDS), 8, 16):
        raise ValueError('MIR1: invalid header')
    record = {'profile_id': header[3], 'revision': header[4], 'roulette': {}, 'weights': {}}
    at = 32
    for group, name, _, _, si in FIELDS:
        value, = struct.unpack_from('<I', payload, at)
        at += 4
        record.setdefault(group, {})[name] = value / 65536 if si else value
    for name, count in (('preview', 10), ('timeline', 27)):
        record['roulette'][name] = list(struct.unpack_from(f'<{count}I', payload, at))
        at += count*4
    for domain in ('player', 'cpu'):
        rows = []
        for _ in range(8):
            row, ended = [], False
            for _ in range(16):
                item, weight = struct.unpack_from('<HH', payload, at)
                at += 4
                if not item:
                    if weight:
                        raise ValueError('MIR1: nonzero weight padding')
                    ended = True
                elif ended:
                    raise ValueError('MIR1: nonzero award after padding')
                else:
                    row.append([item, weight])
            rows.append(row)
        record['weights'][domain] = rows
    if normalize(record) != payload:
        raise ValueError('MIR1: noncanonical payload')
    return record


def load(path):
    path = Path(path)
    with path.open('rb') as source:
        data = source.read(MAX_FILE+1)
    if len(data) > MAX_FILE:
        raise ValueError('items.json exceeds 1 MiB')
    def pairs(rows):
        result = {}
        for key, value in rows:
            if key in result:
                raise ValueError(f'duplicate JSON field {key}')
            result[key] = value
        return result
    def constant(value):
        raise ValueError(f'non-finite JSON constant {value}')
    try:
        return json.loads(data, object_pairs_hook=pairs, parse_constant=constant)
    except RecursionError as error:
        raise ValueError('items.json nesting is too deep') from error


def compile(document):
    exact(document, ('version', 'profiles'), 'item profiles')
    integer(document['version'], 1, 1, 'item profiles.version')
    records = document['profiles']
    if type(records) is not list or not 1 <= len(records) <= MAX_PROFILES:
        raise ValueError('item profiles: expected 1..256 profiles')
    result, seen = [], set()
    for record in records:
        payload = normalize(record)
        identity = record['profile_id']
        if identity in seen:
            raise ValueError(f'duplicate item profile_id {identity}')
        seen.add(identity)
        result.append(({'kind': KIND, 'content_id': identity, 'version': VERSION, 'flags': 3}, payload))
    return result


def c_header(payload, name, source):
    """MIR1 words for native importers that ship the default profile."""
    words = struct.unpack(f'<{len(payload) // 4}I', payload)
    rows = ',\n'.join('    ' + ', '.join(f'0x{word:08x}u' for word in words[index:index+7])
                       for index in range(0, len(words), 7))
    guard = name.upper() + '_H'
    return (f'/* Generated by tools/portable/mesh_item_format.py --c-header {name} from {source}. */\n'
            f'#ifndef {guard}\n#define {guard}\n\n#include <stdint.h>\n\n'
            f'static const uint32_t {name}[{len(words)}] = {{\n{rows}}};\n\n#endif\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--c-header', metavar='NAME', required=True,
                        help='write a C header defining NAME as the first profile\'s MIR1 words')
    args = parser.parse_args()
    _, payload = compile(load(args.source))[0]
    args.output.write_text(c_header(payload, args.c_header, args.source.name))


if __name__ == '__main__':
    main()
