"""Stable static UI strings shared by importer, SDK and native renderer."""
import json
import re
import struct
from pathlib import Path
import text_data_mod
CATALOG={int(identity):json.loads(value) for identity,value in re.findall(
    r'UI_STRING\((\d+), ("[^"\n]*")\)',
    (Path(__file__).resolve().parents[2]/'portable/core/ui_strings.inc').read_text())}
def validate(records):
    if not isinstance(records,list) or len(records)>64:
        raise ValueError('strings: expected at most64 records')
    seen=set()
    for record in records:
        if not isinstance(record,dict) or set(record)!={'id','value'}:
            raise ValueError('strings: expected id and value')
        identity=record['id'];value=record['value']
        if type(identity) is not int or identity not in CATALOG or identity in seen:
            raise ValueError('strings.id: unknown or duplicate UI string ID')
        seen.add(identity)
        if not isinstance(value,str) or not value or len(value)>len(CATALOG[identity]) or any(c not in text_data_mod._GLYPHS for c in value):
            raise ValueError('strings.value: unsupported glyph, empty or longer than original label')
def payload(value):
    data=value.encode('ascii')
    return b'UIT1'+struct.pack('<I',len(data))+data+b'\0'
def sections():
    return [(66,identity,payload(value),1) for identity,value in CATALOG.items()]
