"""Strict MCR1/v1 modern coin-cap rules (452 bytes, kind105).

coins.json is version1/profiles; each profile explicitly owns identity/revision,
modes,cap,initial,100 speed_ratios_q16 and losses hit/rescue/contact/crush/haunt.
The default curve is an authored modern adaptation, not original vehicle stats.
"""
import json
from pathlib import Path
import struct
KIND=105
VERSION=1
SIZE=452
MAGIC=0x3152434d
LOSSES=('hit','rescue','contact','crush','haunt')


def integer(value,low,high,label):
    if type(value) is not int or not low<=value<=high:
        raise ValueError(f'coin rules {label}: expected integer {low}..{high}')
    return value


def default_profile(identity=1,modes=13):
    return dict(profile_id=identity,revision=1,modes=modes,cap=99,initial=5,
        speed_ratios_q16=[((90+min(c,10))*65536+50)//100 for c in range(100)],
        losses=dict(zip(LOSSES,(4,2,1,8,1))))


def normalize(record):
    if type(record) is not dict or set(record)!={'profile_id','revision','modes','cap','initial','speed_ratios_q16','losses'}:
        raise ValueError('coin rules profile: missing or unknown fields')
    identity=integer(record['profile_id'],1,0xfffffffe,'profile_id')
    revision=integer(record['revision'],1,0xffffffff,'revision')
    modes=integer(record['modes'],1,31,'modes')
    cap=integer(record['cap'],1,99,'cap')
    initial=integer(record['initial'],0,cap,'initial')
    ratios=record['speed_ratios_q16']
    if type(ratios) is not list or len(ratios)!=100:
        raise ValueError('coin rules speed_ratios_q16: expected100 entries')
    ratios=[integer(v,3277,65536,f'speed_ratios_q16[{i}]') for i,v in enumerate(ratios)]
    losses=record['losses']
    if type(losses) is not dict or set(losses)!=set(LOSSES):
        raise ValueError('coin rules losses: expected hit/rescue/contact/crush/haunt')
    amounts=[integer(losses[key],0,99,'losses.'+key) for key in LOSSES]
    return struct.pack('<113I',MAGIC,1,SIZE,identity,revision,modes,cap,initial,*ratios,*amounts)


def decode(payload):
    if not isinstance(payload,(bytes,bytearray)) or len(payload)!=SIZE:
        raise ValueError('MCR1: expected452 bytes')
    words=struct.unpack('<113I',payload)
    if words[:3]!=(MAGIC,1,SIZE):raise ValueError('MCR1: invalid header')
    record=dict(profile_id=words[3],revision=words[4],modes=words[5],cap=words[6],initial=words[7],
        speed_ratios_q16=list(words[8:108]),losses=dict(zip(LOSSES,words[108:])))
    normalize(record)
    return record


def compile(document):
    if type(document) is not dict or set(document)!={'version','profiles'}:
        raise ValueError('coin rules: expected version/profiles')
    integer(document['version'],1,1,'version')
    records=document['profiles']
    if type(records) is not list or not 1<=len(records)<=256:
        raise ValueError('coin rules: expected1..256 profiles')
    result=[];seen=set()
    for record in records:
        payload=normalize(record);identity=record['profile_id']
        if identity in seen:raise ValueError(f'coin rules: duplicate profile_id {identity}')
        seen.add(identity)
        result.append((dict(kind=KIND,content_id=identity,version=1,flags=3),payload))
    return result


def load(path):
    def unique(pairs):
        result={}
        for key,value in pairs:
            if key in result:raise ValueError(f'coin rules: duplicate field {key}')
            result[key]=value
        return result
    return json.loads(Path(path).read_text(encoding='utf-8'),object_pairs_hook=unique)
