"""MSK1/v1 clock/progress-driven 3D sky profiles; no original pack is modified."""
import json
from pathlib import Path
import struct

KIND=106
SIZE=4244
MAGIC=0x314b534d
CONTROLLERS=(0,1,2,3,4,5,6,7)
FIELDS=('controller','yaw_shift','layer_mask','blend_mode','blend_a','blend_b',
        'x_bias','y_bias','split_y','split_layer','split_yaw_shift','split_x_bias',
        'lightning_hide_layer','road_mode','layer_source')
TRIPLES=('yaw_shift','x_bias','y_bias','layer_source')


def integer(value,low,high,label):
    if type(value) is not int or not low<=value<=high:
        raise ValueError(f'sky {label}: expected integer {low}..{high}')
    return value


def rule_words(rules):
    if type(rules) is not dict or set(rules)!=set(FIELDS):
        raise ValueError('sky rules: missing or unknown fields')
    result=[]
    for key in FIELDS:
        values=rules[key] if key in TRIPLES else [rules[key]]
        if type(values) is not list or len(values)!=(3 if key in TRIPLES else 1):
            raise ValueError(f'sky {key}: expected three integers')
        low,high=(-32768,32767) if key in ('x_bias','y_bias','split_x_bias') else (
            (-1,159) if key=='split_y' else (0,15) if key in ('yaw_shift','split_yaw_shift') else
            (0,16) if key in ('blend_a','blend_b') else (0,7) if key in ('controller','layer_mask') else
            (0,1) if key=='blend_mode' else (0,3) if key=='road_mode' else (0,2))
        result.extend(integer(v,low,high,key) for v in values)
    if rules['controller'] not in CONTROLLERS:
        raise ValueError(f'sky controller={rules["controller"]}: controller is unsupported')
    return result


def normalize(record):
    if type(record) is not dict or set(record)!= {'profile_id','revision','entry_delay','rules','layers','sine'}:
        raise ValueError('sky profile: missing or unknown fields')
    identity=integer(record['profile_id'],1,0xfffffffe,'profile_id')
    revision=integer(record['revision'],1,0xffffffff,'revision')
    if type(record['entry_delay']) is not bool:
        raise ValueError('sky entry_delay: expected boolean')
    rules=rule_words(record['rules'])
    layers=record['layers']
    if type(layers) is not list or not 1<=len(layers)<=3:
        raise ValueError('sky layers: expected1..3 logical layers')
    pairs=[0]*6;mask=0
    for layer in layers:
        if type(layer) is not dict or set(layer)!= {'layer','logical_width','radius_q16'}:
            raise ValueError('sky layer: missing or unknown fields')
        index=integer(layer['layer'],0,2,'layer')
        if mask&(1<<index):raise ValueError(f'sky layer={index}: duplicate binding')
        mask|=1<<index
        pairs[index*2]=integer(layer['logical_width'],1,4096,'logical_width')
        pairs[index*2+1]=integer(layer['radius_q16'],1,2048*65536,'radius_q16')
    if mask&~record['rules']['layer_mask']:
        raise ValueError('sky layers: rendered layer is disabled by the rules')
    sine=record['sine']
    if type(sine) is not list or len(sine)!=1024:
        raise ValueError('sky sine: expected1024 signed Q15 values')
    sine=[integer(v,-32768,32768,'sine') for v in sine]
    return (struct.pack('<8I',MAGIC,1,SIZE,identity,revision,mask,int(record['entry_delay']),1024)+
            struct.pack('<23i6I1024i',*rules,*pairs,*sine))


def decode(payload):
    if not isinstance(payload,(bytes,bytearray)) or len(payload)!=SIZE:
        raise ValueError('MSK1: expected4244 bytes')
    header=struct.unpack_from('<8I',payload)
    if header[:3]!=(MAGIC,1,SIZE) or header[6]>1 or header[7]!=1024:
        raise ValueError('MSK1: invalid header')
    values=list(struct.unpack_from('<23i',payload,32));rules={}
    for field in FIELDS:
        count=3 if field in TRIPLES else 1
        rules[field]=values[:count] if count==3 else values[0]
        del values[:count]
    pairs=struct.unpack_from('<6I',payload,124)
    layers=[]
    if not 1<=header[5]<=7:raise ValueError('MSK1: invalid render mask')
    for index in range(3):
        width,radius=pairs[index*2:index*2+2]
        if header[5]&(1<<index):
            layers.append(dict(layer=index,logical_width=width,radius_q16=radius))
        elif width or radius:raise ValueError('MSK1: unbound layer has geometry')
    record=dict(profile_id=header[3],revision=header[4],entry_delay=bool(header[6]),rules=rules,
                layers=layers,sine=list(struct.unpack_from('<1024i',payload,148)))
    normalize(record)
    return record


def compile(document):
    if type(document) is not dict or set(document)!= {'version','profiles'}:
        raise ValueError('skies: expected version/profiles')
    integer(document['version'],1,1,'version')
    records=document['profiles']
    if type(records) is not list or not 1<=len(records)<=256:
        raise ValueError('skies: expected1..256 profiles')
    result=[];seen=set()
    for record in records:
        payload=normalize(record);identity=record['profile_id']
        if identity in seen:raise ValueError(f'sky duplicate profile_id={identity}')
        seen.add(identity)
        result.append((dict(kind=KIND,content_id=identity,version=1,flags=3),payload))
    return result


def load(path):
    def unique(pairs):
        result={}
        for key,value in pairs:
            if key in result:raise ValueError(f'sky duplicate field={key}')
            result[key]=value
        return result
    return json.loads(Path(path).read_text(encoding='utf-8'),object_pairs_hook=unique)
