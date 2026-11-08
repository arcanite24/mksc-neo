"""Strict authored KPH2/v2 kart profiles for mesh-course movement.

A profile holds MK8's 13 stats as ranges (a Q16 value at stat level 0 and at
level 19), base stat levels, stat points for the stock drivers 1..8, per-class
speed ratios and the body/contact/respawn coefficients. `resolved` mirrors
`portable_mesh_kart_rules_resolve` for tools that need one racer's numbers.
"""
import argparse
import json
from pathlib import Path
import struct

KIND = 102
VERSION = 2
MAGIC = 0x3148504B
Q = 65536
LEVEL_MAX = 19
STATS = ('weight', 'acceleration', 'traction_on', 'traction_off', 'mini_turbo',
         'speed_ground', 'speed_water', 'speed_air', 'speed_antigravity',
         'handling_ground', 'handling_water', 'handling_air', 'handling_antigravity')
# Range name, owning stat, inclusive endpoint bounds; order is the wire order.
# Handling rows: normal and drift turn rates in radians a tick at 7+ units/frame,
# and the spin-turn gain.
_HANDLING = lambda stat: ((f'{stat}_normal', stat, 0, Q//4), (f'{stat}_drift', stat, 0, Q//4),
                          (f'{stat}_charge', stat, 0, 16*Q))
RANGES = (
    ('weight_low', 'weight', Q, 128*Q),
    ('weight_high', 'weight', Q, 128*Q),
    ('weight_boost', 'weight', Q, 128*Q),
    ('acceleration_limit', 'acceleration', 0, 1024*Q),
    ('acceleration_rate', 'acceleration', 1, Q),
    ('traction_on', 'traction_on', 0, Q),
    ('traction_off', 'traction_off', 0, Q),
    ('charge_1', 'mini_turbo', Q, 2048*Q),
    ('charge_2', 'mini_turbo', Q, 2048*Q),
    ('charge_3', 'mini_turbo', Q, 2048*Q),
    ('mt1_ticks', 'mini_turbo', Q, 2048*Q),
    ('mt1_cap', 'mini_turbo', Q, 4*Q),
    ('mt1_acceleration', 'mini_turbo', 0, 1024*Q),
    ('mt2_ticks', 'mini_turbo', Q, 2048*Q),
    ('mt2_cap', 'mini_turbo', Q, 4*Q),
    ('mt2_acceleration', 'mini_turbo', 0, 1024*Q),
    ('trick_ticks', 'mini_turbo', Q, 2048*Q),
    ('trick_cap', 'mini_turbo', Q, 4*Q),
    ('trick_acceleration', 'mini_turbo', 0, 1024*Q),
    ('speed_ground', 'speed_ground', Q, 128*Q),
    ('speed_water', 'speed_water', Q, 128*Q),
    ('speed_air', 'speed_air', Q, 128*Q),
    ('speed_antigravity', 'speed_antigravity', Q, 128*Q),
    *_HANDLING('handling_ground'), *_HANDLING('handling_water'),
    *_HANDLING('handling_air'), *_HANDLING('handling_antigravity'),
)
# Scalar field, inclusive bounds, in wire order after the class ratios.
SCALARS = (
    ('handling_response_q16', 0, 4194304),
    ('drift_response_q16', 0, 4194304),
    ('minimum_drift_speed_ratio_q16', 1, Q),
    ('outside_drift_angle_q16', 0, 16384),
    ('outside_drift_recovery_q16', 0, 4194304),
    ('air_steering_ratio_q16', 0, Q),
    ('normal_alignment_response_q16', 0, 4194304),
    ('panel_boost_ticks', 1, 8192),
    ('panel_cap_ratio_q16', Q, 4*Q),
    ('panel_acceleration_q16', 0, 67108864),
    ('hop_velocity_q16', 0, 16777216),
    ('terminal_fall_speed_q16', 1, 16777216),
    ('ground_snap_q16', 0, 524288),
    ('ground_probe_q16', 1, 524288),
    ('kart_radius_q16', 3277, 524288),
    ('ride_height_q16', 3277, 524288),
    ('ground_normal_cutoff_q30', 1, 1073741824),
    ('respawn_ticks', 1, 8192),
    ('respawn_grace_ticks', 0, 8192),
    ('respawn_lift_q16', 0, 524288),
)
WORDS = 2*len(RANGES) + len(STATS)*9 + 6 + len(SCALARS)
SIZE = 32 + 4*WORDS
FIELDS = {'version', 'profile_id', 'revision', 'ranges', 'base_levels_q16', 'driver_points_q16',
          'class_speed_ratio_q16', 'cpu_speed_ratio_q16', *(name for name, _, _ in SCALARS)}
# MK8 engine constants mirrored from portable/core/mesh_kart_mk8.h.
REVERSE_Q16 = 824060
BRAKE_RATE_Q16 = 918
BRAKE_LIMIT_Q16 = 51504
TURNS_PER_SECOND_Q16 = 622980  # one radian a tick, in turns/s


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'{key}: duplicate field')
        result[key] = value
    return result


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=unique_object)


def integer(value, low, high, field):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'{field}: expected integer {low}..{high}')
    return value


def stat_map(value, field, required, low, high):
    if type(value) is not dict or not set(value) <= set(STATS) or (required and set(value) != set(STATS)):
        raise ValueError(f'{field}: expected {"every" if required else "known"} stat names')
    return [integer(value.get(stat, 0), low, high, f'{field}.{stat}') for stat in STATS]


def words(record):
    """Validated payload words (without the 8-word header)."""
    if type(record) is not dict or set(record) != FIELDS:
        raise ValueError('physics profile: missing or unknown fields')
    integer(record['version'], VERSION, VERSION, 'version')
    integer(record['profile_id'], 1, 0xffffffff, 'profile_id')
    integer(record['revision'], 1, 0xffffffff, 'revision')
    ranges = record['ranges']
    if type(ranges) is not dict or set(ranges) != {name for name, *_ in RANGES}:
        raise ValueError('ranges: expected every range name')
    result = []
    for name, _, low, high in RANGES:
        pair = ranges[name]
        if type(pair) is not list or len(pair) != 2:
            raise ValueError(f'ranges.{name}: expected [level0, level19]')
        result.extend(integer(v, low, high, f'ranges.{name}') for v in pair)
    for smaller, larger in (('charge_1', 'charge_2'), ('charge_2', 'charge_3'),
                            ('mt1_ticks', 'mt2_ticks')):
        if any(a >= b for a, b in zip(ranges[smaller], ranges[larger])):
            raise ValueError(f'ranges.{smaller}: must stay below {larger}')
    result.extend(stat_map(record['base_levels_q16'], 'base_levels_q16', True, 0, LEVEL_MAX*Q))
    points = record['driver_points_q16']
    if type(points) is not dict or set(points) != {str(d) for d in range(1, 9)}:
        raise ValueError('driver_points_q16: expected stock drivers "1".."8"')
    for driver in range(1, 9):
        result.extend(stat_map(points[str(driver)], f'driver_points_q16.{driver}', False,
                               -LEVEL_MAX*Q, LEVEL_MAX*Q))
    ratios = record['class_speed_ratio_q16']
    if type(ratios) is not list or len(ratios) != 3:
        raise ValueError('class_speed_ratio_q16: expected 50/100/150cc entries')
    result.extend(integer(v, Q//4, 2*Q, 'class_speed_ratio_q16') for v in ratios)
    cpu = record['cpu_speed_ratio_q16']
    if type(cpu) is not list or len(cpu) != 3:
        raise ValueError('cpu_speed_ratio_q16: expected 50/100/150cc entries')
    result.extend(integer(v, Q//2, Q, 'cpu_speed_ratio_q16') for v in cpu)
    for name, low, high in SCALARS:
        result.append(integer(record[name], low, high, name))
    if record['ground_snap_q16'] > record['ground_probe_q16']:
        raise ValueError('ground_snap_q16: must not exceed ground_probe_q16')
    for engine_class in range(3):
        for level in (0, LEVEL_MAX):
            rules = resolved(record, engine_class, [level*Q]*len(STATS), checked=False)
            for cap in (*rules['boost_cap_ratio_q16'], rules['panel_cap_ratio_q16']):
                if rules['forward_speed_q16']*cap > 256*Q*Q:
                    raise ValueError('boost cap: effective speed exceeds 256 m/s')
    return result


def normalize(record):
    payload = words(record)
    return struct.pack(f'<8I{len(payload)}i', MAGIC, VERSION, SIZE, record['profile_id'],
                       record['revision'], 0, 0, 0, *payload)


def decode(payload):
    if len(payload) != SIZE:
        raise ValueError(f'KPH2: expected {SIZE} bytes')
    header = struct.unpack_from('<8I', payload)
    if header[:3] != (MAGIC, VERSION, SIZE) or any(header[5:8]):
        raise ValueError('KPH2: invalid header')
    values = list(struct.unpack_from(f'<{WORDS}i', payload, 32))
    record = {'version': VERSION, 'profile_id': header[3], 'revision': header[4]}
    record['ranges'] = {name: values[2*i:2*i+2] for i, (name, *_) in enumerate(RANGES)}
    at = 2*len(RANGES)
    record['base_levels_q16'] = dict(zip(STATS, values[at:at+len(STATS)]))
    at += len(STATS)
    record['driver_points_q16'] = {}
    for driver in range(1, 9):
        row = values[at:at+len(STATS)]
        record['driver_points_q16'][str(driver)] = {s: v for s, v in zip(STATS, row) if v}
        at += len(STATS)
    record['class_speed_ratio_q16'] = values[at:at+3]
    at += 3
    record['cpu_speed_ratio_q16'] = values[at:at+3]
    at += 3
    for name, _, _ in SCALARS:
        record[name] = values[at]
        at += 1
    normalize(record)
    return record


def _trunc(numerator, denominator):
    """C integer division (toward zero)."""
    quotient = abs(numerator)//abs(denominator)
    return quotient if (numerator >= 0) == (denominator > 0) else -quotient


def drive(v, target, rate, limit, m, f, sign):
    x = v*sign
    if x < target:
        step = min(_trunc((target-x)*rate, Q), limit)
        x += _trunc(_trunc(step*f, Q)*m, Q)
    else:
        x = _trunc(x*m, Q)
    return x*sign


def levels(record, stock_driver=0, extra=None):
    """Stat levels for a stock driver (1..8, 0 none) plus extra points."""
    result = []
    for i, stat in enumerate(STATS):
        level = record['base_levels_q16'][stat]
        if 1 <= stock_driver <= 8:
            level += record['driver_points_q16'][str(stock_driver)].get(stat, 0)
        if extra:
            level += extra[i]
        result.append(max(0, min(LEVEL_MAX*Q, level)))
    return result


def resolved(record, engine_class=2, stat_levels=None, checked=True):
    """One racer's rules, matching portable_mesh_kart_rules_resolve."""
    if checked:
        words(record)
    if stat_levels is None:
        stat_levels = levels(record)
    level = dict(zip(STATS, stat_levels))

    def value(name):
        stat = next(owner for n, owner, *_ in RANGES if n == name)
        low, high = record['ranges'][name]
        x = max(0, min(LEVEL_MAX*Q, level[stat]))
        return low + _trunc((high-low)*x, LEVEL_MAX*Q)

    top = _trunc(value('speed_ground')*record['class_speed_ratio_q16'][engine_class], Q)
    speed, ticks = top, 0
    while speed > 0 and ticks < 4096:
        speed = drive(speed, REVERSE_Q16, BRAKE_RATE_Q16, BRAKE_LIMIT_Q16, Q, Q, -1)
        ticks += 1
    mt1, mt2 = value('mt1_ticks')//Q, value('mt2_ticks')//Q
    rules = {name: record[name] for name, _, _ in SCALARS}
    rules.update(
        profile_id=record['profile_id'], revision=record['revision'], forward_speed_q16=top,
        reverse_speed_q16=REVERSE_Q16, acceleration_limit_q16=value('acceleration_limit'),
        acceleration_rate_q16=value('acceleration_rate'),
        coast_deceleration_q16=_trunc(top*57049, Q),
        brake_deceleration_q16=_trunc(top*262144, max(ticks, 1)*4389),
        charge_thresholds=[value(f'charge_{i}')//Q for i in (1, 2, 3)],
        neutral_charge_rate=2, inward_charge_rate=5,
        boost_ticks=[mt1, mt2, 2*mt2-mt1 if 2*mt2 > mt1 else mt2],
        boost_cap_ratio_q16=[value('mt1_cap'), value('mt2_cap'), value('mt2_cap')],
        boost_acceleration_q16=[value('mt1_acceleration'), value('mt2_acceleration'),
                                value('mt2_acceleration')],
        handling_turn_rate_q16=_trunc(value('handling_ground_normal')*TURNS_PER_SECOND_Q16, Q),
        drift_turn_rate_q16=_trunc(value('handling_ground_drift')*TURNS_PER_SECOND_Q16, Q),
        spin_gain_q16=value('handling_ground_charge'),
        antigravity_speed_q16=_trunc(value('speed_antigravity')*record['class_speed_ratio_q16'][engine_class], Q),
        antigravity_turn_rate_q16=_trunc(value('handling_antigravity_normal')*TURNS_PER_SECOND_Q16, Q),
        antigravity_drift_turn_rate_q16=_trunc(value('handling_antigravity_drift')*TURNS_PER_SECOND_Q16, Q),
        antigravity_spin_gain_q16=value('handling_antigravity_charge'),
        glide_speed_q16=_trunc(value('speed_air')*record['class_speed_ratio_q16'][engine_class], Q),
        traction_on_q16=value('traction_on'), traction_off_q16=value('traction_off'),
        trick_ticks=value('trick_ticks')//Q, trick_cap_ratio_q16=value('trick_cap'),
        trick_acceleration_q16=value('trick_acceleration'),
        traction_level_q16=max(0, min(LEVEL_MAX*Q, level['traction_off']))//LEVEL_MAX,
        weight_q16=[value('weight_low'), value('weight_high'), value('weight_boost')])
    return rules


def compile(document):
    """Compile one profile or a {"version":1,"profiles":[...]} collection."""
    if type(document) is dict and 'profiles' in document:
        if set(document) != {'version', 'profiles'}:
            raise ValueError('physics profiles: missing or unknown fields')
        integer(document['version'], 1, 1, 'physics profiles.version')
        records = document['profiles']
        if type(records) is not list or not 1 <= len(records) <= 256:
            raise ValueError('physics profiles: expected 1..256 profiles')
    else:
        records = [document]
    result = []
    seen = set()
    for record in records:
        payload = normalize(record)
        identity = record['profile_id']
        if identity in seen:
            raise ValueError(f'physics profiles: duplicate profile_id {identity}')
        seen.add(identity)
        result.append(({'kind': KIND, 'content_id': identity, 'version': VERSION,
                        'flags': 3}, payload))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--c-header', metavar='NAME',
                        help='write a C header defining NAME as the KPH2 words instead of bytes')
    args = parser.parse_args()
    payload = normalize(load(args.source))
    if args.c_header:
        args.output.write_text(c_header(payload, args.c_header, args.source.name))
    else:
        args.output.write_bytes(payload)


def c_header(payload, name, source):
    """KPH2 words for native importers that ship the default profile."""
    count = len(payload)//4
    words_ = struct.unpack(f'<{count}I', payload)
    rows = ',\n'.join('    ' + ', '.join(f'0x{word:08x}u' for word in words_[index:index+7])
                       for index in range(0, count, 7))
    guard = name.upper() + '_H'
    return (f'/* Generated by tools/portable/mesh_kart_format.py --c-header {name} from {source}. */\n'
            f'#ifndef {guard}\n#define {guard}\n\n#include <stdint.h>\n\n'
            f'static const uint32_t {name}[{count}] = {{\n{rows}}};\n\n#endif\n')


if __name__ == '__main__':
    main()
