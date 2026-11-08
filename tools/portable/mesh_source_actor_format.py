"""Strict MSA1/v1 source-controller rules; no implicit driver or ROM lookup.

source-actors.json is {"version":1,"profiles":[{"profile_id":positive,
"revision":positive,"sine":[1024 signed Q15 integers]}]}. compile validates the
whole collection before returning kind104 sections for the ordinary mod kit.
"""
import json
from pathlib import Path
import struct

KIND = 104
VERSION = 1
SIZE = 4128
MAGIC = 0x3141534d


def _integer(value, low, high, label):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'source actors {label}: expected integer {low}..{high}')
    return value


def _sine(values):
    if type(values) is not list or len(values) != 1024:
        raise ValueError('source actors sine: expected exactly 1024 Q15 entries')
    return [_integer(v, -32768, 32768, f'sine[{i}]') for i, v in enumerate(values)]


def normalize(record):
    if type(record) is not dict or set(record) != {'profile_id', 'revision', 'sine'}:
        raise ValueError('source actors profile: missing or unknown fields')
    identity = _integer(record['profile_id'], 1, 0xfffffffe, 'profile_id')
    revision = _integer(record['revision'], 1, 0xffffffff, 'revision')
    sine = _sine(record['sine'])
    return struct.pack('<8I1024i', MAGIC, VERSION, SIZE, identity, revision, 1024, 0, 0, *sine)


def decode(payload):
    if not isinstance(payload, (bytes, bytearray)) or len(payload) != SIZE:
        raise ValueError('MSA1: expected exactly 4128 bytes')
    header = struct.unpack_from('<8I', payload)
    if header[:3] != (MAGIC, VERSION, SIZE) or header[5:] != (1024, 0, 0):
        raise ValueError('MSA1: invalid header')
    record = dict(profile_id=header[3], revision=header[4],
                  sine=list(struct.unpack_from('<1024i', payload, 32)))
    normalize(record)
    return record


def compile(document):
    if type(document) is not dict or set(document) != {'version', 'profiles'}:
        raise ValueError('source actors: expected version and profiles')
    _integer(document['version'], VERSION, VERSION, 'version')
    profiles = document['profiles']
    if type(profiles) is not list or not 1 <= len(profiles) <= 256:
        raise ValueError('source actors: expected 1..256 profiles')
    result, seen = [], set()
    for record in profiles:
        payload = normalize(record)
        identity = record['profile_id']
        if identity in seen:
            raise ValueError(f'source actors: duplicate profile_id {identity}')
        seen.add(identity)
        result.append((dict(kind=KIND, content_id=identity, version=VERSION, flags=3), payload))
    return result


def load(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f'source actors {key}: duplicate field')
            result[key] = value
        return result
    return json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=unique)


def imported_sine(sections):
    """Read validated MOT1 prefixes, requiring every declared table to agree.

    Input is track_format.load_pack's (kind,id)->(header,payload) mapping.
    Motion tuning tails vary by version; only the fixed header and complete
    sine prefix are consumed. No source character/content ID is assumed.
    """
    selected = None
    for (kind, identity), (header, payload) in sorted(sections.items()):
        if kind != 13:
            continue
        label = f'MOT1 id={identity}'
        version = header.get('version') if isinstance(header, dict) else None
        if type(version) is not int or version not in range(1, 9) or len(payload) < 36+4096:
            raise ValueError(f'{label}: unsupported version or truncated sine prefix')
        words = struct.unpack_from('<9I', payload)
        expected = (0x31544f4d, 1024, 64, 8, 8, 8, 16 if version == 1 else 256, 10)
        if words[:8] != expected or words[8] not in (4, 16):
            raise ValueError(f'{label}: invalid motion header')
        values = _sine(list(struct.unpack_from('<1024i', payload, 36)))
        if selected is not None and values != selected:
            raise ValueError(f'{label}: source sine tables disagree')
        selected = values
    if selected is None:
        raise ValueError('source actors: no imported MOT1 sine table')
    return selected
