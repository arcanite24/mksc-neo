"""Normalize authored modern drift profiles; no imported game data is involved."""
import argparse
import json
from pathlib import Path
import struct

KIND = 95
VERSION = 1
MAGIC = 0x3152444D
PAYLOAD = struct.Struct('<III3H3H3HH3iI')
AIR_PAYLOAD = struct.Struct('<HHHHiHBB')
AIR_FIELDS = ('minimum_air_ticks', 'trick_window_ticks', 'boost_ticks',
              'cap_ratio_256', 'acceleration_q16', 'minimum_speed_ratio_256', 'launch_mask')
FIELDS = ('version', 'profile_id', 'revision', 'charge_ticks', 'boost_ticks',
          'cap_ratio_256', 'minimum_speed_ratio_256', 'acceleration_q16')


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'{key}: duplicate field')
        result[key] = value
    return result


def integer(value, low, high, field):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'{field}: expected integer {low}..{high}')
    return value


def tier_values(record, field, low, high):
    values = record[field]
    if type(values) is not list or len(values) != 3:
        raise ValueError(f'{field}: expected three tiers')
    return [integer(value, low, high, field) for value in values]


def normalize(record):
    """Return the fixed little-endian MDR1 payload accepted by the C decoder."""
    if type(record) is not dict:
        raise ValueError('modern drift: expected an object')
    version = integer(record.get('version'), 1, 2, 'version')
    fields = (*FIELDS, 'air') if version == 2 else FIELDS
    if set(record) != set(fields):
        raise ValueError('modern drift: expected exactly ' + ', '.join(fields))
    profile = integer(record['profile_id'], 1, 0xFFFFFFFF, 'profile_id')
    revision = integer(record['revision'], 1, 0xFFFFFFFF, 'revision')
    charge = tier_values(record, 'charge_ticks', 1, 8192)
    if not charge[0] < charge[1] < charge[2]:
        raise ValueError('charge_ticks: tiers must strictly increase')
    boost = tier_values(record, 'boost_ticks', 1, 8192)
    caps = tier_values(record, 'cap_ratio_256', 256, 1024)
    minimum = integer(record['minimum_speed_ratio_256'], 1, 256,
                      'minimum_speed_ratio_256')
    acceleration = tier_values(record, 'acceleration_q16', 1, 128 * 65536)
    payload = PAYLOAD.pack(MAGIC, profile, revision, *charge, *boost, *caps,
                           minimum, *acceleration, 0)
    if version == 2:
        air = record['air']
        if type(air) is not dict or set(air) != set(AIR_FIELDS):
            raise ValueError('air: expected exactly ' + ', '.join(AIR_FIELDS))
        air_minimum = integer(air['minimum_air_ticks'], 1, 8192, 'air.minimum_air_ticks')
        window = integer(air['trick_window_ticks'], air_minimum, 8192, 'air.trick_window_ticks')
        boost = integer(air['boost_ticks'], 1, 8192, 'air.boost_ticks')
        cap = integer(air['cap_ratio_256'], 256, 1024, 'air.cap_ratio_256')
        acceleration = integer(air['acceleration_q16'], 1, 128 * 65536, 'air.acceleration_q16')
        minimum = integer(air['minimum_speed_ratio_256'], 1, 256, 'air.minimum_speed_ratio_256')
        mask = integer(air['launch_mask'], 1, 3, 'air.launch_mask')
        payload += AIR_PAYLOAD.pack(air_minimum, window, boost, cap, acceleration, minimum, mask, 0)
    return payload


def decode(data):
    """Validate and expose wire data for a mod editor without silent repair."""
    if len(data) not in (PAYLOAD.size, PAYLOAD.size + AIR_PAYLOAD.size):
        raise ValueError('MDR1: expected 48 or 64 bytes')
    fields = PAYLOAD.unpack_from(data)
    if fields[0] != MAGIC or fields[-1] != 0:
        raise ValueError('MDR1: invalid magic or reserved bytes')
    record = dict(version=VERSION, profile_id=fields[1], revision=fields[2],
                  charge_ticks=list(fields[3:6]), boost_ticks=list(fields[6:9]),
                  cap_ratio_256=list(fields[9:12]), minimum_speed_ratio_256=fields[12],
                  acceleration_q16=list(fields[13:16]))
    if len(data) == 64:
        air = AIR_PAYLOAD.unpack_from(data, PAYLOAD.size)
        if air[-1] != 0:
            raise ValueError('MDR1: nonzero air reserved byte')
        record['version'] = 2
        record['air'] = dict(zip(AIR_FIELDS, air[:-1]))
    normalize(record)
    return record


def load(path):
    path = Path(path)
    if path.stat().st_size > 16384:
        raise ValueError('modern drift: JSON exceeds 16384 bytes')
    record = json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=unique_object)
    normalize(record)
    return record


def c_header(data):
    version = decode(data)['version']
    name = 'air' if version == 2 else 'drift'
    guard = f'PORTABLE_MODERN_{name.upper()}_PAYLOAD_H'
    rows = ['    ' + ', '.join(f'0x{byte:02x}' for byte in data[offset:offset + 12]) + ','
            for offset in range(0, len(data), 12)]
    return ('/* Generated from authored modern drift JSON; do not edit. */\n'
            f'#ifndef {guard}\n#define {guard}\n#include <stdint.h>\n'
            f'static const uint8_t portable_modern_{name}_payload[{len(data)}] = {{\n' +
            '\n'.join(rows) + '\n};\n#endif\n')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--header', type=Path)
    args = parser.parse_args(argv)
    if args.binary is None and args.header is None:
        parser.error('at least one of --binary or --header is required')
    data = normalize(load(args.source))
    for path, content in ((args.binary, data), (args.header, c_header(data).encode('ascii'))):
        if path is not None:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
