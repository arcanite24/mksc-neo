"""Normalize textual graphic overrides using the native menu font and canvas."""
from pathlib import Path
import re
import struct

_GLYPHS = {character: int(bits, 16) for character,bits in re.findall(
    r"TEXT_GLYPH\('(.)', (0x[0-9a-f]+)\)",
    (Path(__file__).resolve().parents[2]/'portable/core/text_glyphs.inc').read_text())}


def validate(records):
    if type(records) is not list or len(records)>64:
        raise ValueError('text: expected at most64 records')
    seen=set()
    for record in records:
        if type(record) is not dict or set(record)-{'id','value','color_rgb'}:
            raise ValueError('text: expected id, value and optional color_rgb')
        identity=record.get('id')
        if type(identity) is not int or not 1<=identity<=0xffffffff or identity in seen:
            raise ValueError('text.id: invalid or duplicate graphic ID')
        seen.add(identity)
        value=record.get('value')
        if type(value) is not str or not 1<=len(value)<=48 or any(c not in _GLYPHS for c in value):
            raise ValueError('text.value: expected1..48 supported uppercase menu glyphs')
        color=record.get('color_rgb',0xffffff)
        if type(color) is not int or not 0<=color<=0xffffff:
            raise ValueError('text.color_rgb: expected integer0..16777215')


def render(record,meta,data):
    if len(data)<32 or meta['version']!=1 or data[:8]!=b'MKSCIMG1':
        raise ValueError('text.id: expected native frontend image v1')
    header,version,width,height=struct.unpack_from('<4H',data,8)
    reserved,offset,total=struct.unpack_from('<3I',data,20)
    if ((header,version,reserved,offset,total)!=(32,1,0,32,len(data)) or
        not 1<=width<=4096 or not 1<=height<=4096 or len(data)!=32+width*height*4):
        raise ValueError('text.id: expected native frontend image v1')
    value=record['value'];text_width=len(value)*4-1
    scale=min(4,width//text_width,height//5)
    if not scale:
        raise ValueError('text.value: text does not fit image canvas')
    result=bytearray(data[:32])+bytearray(len(data)-32)
    left,top=(width-text_width*scale)//2,(height-5*scale)//2
    for i,character in enumerate(value):
        glyph=_GLYPHS[character]
        for row in range(5):
            for column in range(3):
                if not glyph&(1<<(14-row*3-column)):continue
                for dy in range(scale):
                    for dx in range(scale):
                        offset=32+((top+row*scale+dy)*width+left+(i*4+column)*scale+dx)*4
                        struct.pack_into('<I',result,offset,0xff000000|record.get('color_rgb',0xffffff))
    return result
