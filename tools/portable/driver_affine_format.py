"""Editable AFR1/v1 assets, shared with the native driver affine decoder.

actor-affine.json contains version1/profiles; each profile has profile_id and
five ordered tables (modes1,4,5,6,7), each with rows [inverseX,inverseY,ticks].
The existing wire stores neither content ID nor revision; the section owns ID.
"""
import json
from pathlib import Path
import struct

KIND=81
VERSION=1
MODES=(1,4,5,6,7)


def integer(value,low,high,label):
    if type(value) is not int or not low<=value<=high:
        raise ValueError(f'actor affine {label}: expected integer {low}..{high}')
    return value


def normalize(record):
    if type(record) is not dict or set(record)!={'profile_id','tables'}:
        raise ValueError('actor affine profile: expected profile_id and tables')
    integer(record['profile_id'],1,0xfffffffe,'profile_id')
    tables=record['tables']
    if type(tables) is not list or len(tables)!=5:
        raise ValueError('actor affine tables: expected modes1,4,5,6,7')
    result=bytearray(struct.pack('<4sI',b'AFR1',5))
    for mode,table in zip(MODES,tables):
        if type(table) is not dict or set(table)!={'mode','rows'}:
            raise ValueError('actor affine table: expected mode and rows')
        integer(table['mode'],mode,mode,'mode')
        rows=table['rows']
        if type(rows) is not list or not 1<=len(rows)<=32:
            raise ValueError('actor affine rows: expected1..32 rows')
        result.extend(struct.pack('<2I',mode,len(rows)))
        for row in rows:
            if type(row) is not list or len(row)!=3:
                raise ValueError('actor affine row: expected inverseX,inverseY,ticks')
            x=integer(row[0],1,65535,'inverseX')
            y=integer(row[1],1,65535,'inverseY')
            ticks=integer(row[2],1,127,'ticks')
            result.extend(struct.pack('<HHBB',x,y,ticks,0))
    return bytes(result)


def decode(payload,identity):
    if not isinstance(payload,(bytes,bytearray)) or len(payload)<8 or payload[:8]!=struct.pack('<4sI',b'AFR1',5):
        raise ValueError('AFR1: invalid header')
    record=dict(profile_id=identity,tables=[])
    cursor=8
    for expected in MODES:
        if len(payload)-cursor<8:
            raise ValueError('AFR1: truncated table')
        mode,count=struct.unpack_from('<2I',payload,cursor);cursor+=8
        if mode!=expected or not 1<=count<=32 or len(payload)-cursor<count*6:
            raise ValueError('AFR1: invalid mode/count or truncated rows')
        rows=[]
        for _ in range(count):
            x,y,ticks,reserved=struct.unpack_from('<HHBB',payload,cursor);cursor+=6
            if reserved:raise ValueError('AFR1: nonzero reserved byte')
            rows.append([x,y,ticks])
        record['tables'].append(dict(mode=mode,rows=rows))
    if cursor!=len(payload):raise ValueError('AFR1: trailing bytes')
    normalize(record)
    return record


def compile(document):
    if type(document) is not dict or set(document)!={'version','profiles'}:
        raise ValueError('actor affine: expected version and profiles')
    integer(document['version'],1,1,'version')
    profiles=document['profiles']
    if type(profiles) is not list or not 1<=len(profiles)<=256:
        raise ValueError('actor affine: expected1..256 profiles')
    result=[];seen=set()
    for record in profiles:
        payload=normalize(record);identity=record['profile_id']
        if identity in seen:raise ValueError(f'actor affine: duplicate profile_id {identity}')
        seen.add(identity)
        result.append((dict(kind=KIND,content_id=identity,version=1,flags=3),payload))
    return result


def load(path):
    def unique(pairs):
        result={}
        for key,value in pairs:
            if key in result:raise ValueError(f'actor affine: duplicate field {key}')
            result[key]=value
        return result
    return json.loads(Path(path).read_text(encoding='utf-8'),object_pairs_hook=unique)


def imported(sections):
    """Require exactly one explicit imported AFR1, with no implicit stock ID."""
    candidates=[(identity,header,payload) for (kind,identity),(header,payload) in sections.items() if kind==KIND]
    if len(candidates)!=1:raise ValueError('thwomps: expected exactly one imported AFR1 asset')
    identity,header,payload=candidates[0]
    if header.get('version')!=1:raise ValueError('thwomps AFR1: unsupported version')
    return decode(payload,identity)
