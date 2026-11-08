#!/usr/bin/env python3
"""Build the deterministic source GP finish-banner graphics overlay."""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import struct
import sys
import tempfile

TOOLS=pathlib.Path(__file__).parent
sys.path.insert(0,str(TOOLS))
import build_asset_pack


MAGIC=0x31424647
TILES_OFFSET=0x6D210
TILE_BYTES=0x800
PALETTE_OFFSET=0x6D00C
PALETTE_BYTES=0x60
CELLS_OFFSET=0x7B998
CELL_BYTES=0x10
TILE_BASE=0xA0
ORIGIN_X=120
ORIGIN_Y=63


def decode_lz77(source: bytes, offset: int) -> bytes:
    if offset+4>len(source) or source[offset]!=0x10:
        raise ValueError('invalid GP finish-banner LZ77 header')
    size=int.from_bytes(source[offset+1:offset+4],'little')
    cursor=offset+4
    output=bytearray()
    while len(output)<size:
        if cursor>=len(source):
            raise ValueError('truncated GP finish-banner LZ77 flags')
        flags=source[cursor];cursor+=1
        for bit in range(7,-1,-1):
            if len(output)>=size: break
            if (flags&(1<<bit))==0:
                if cursor>=len(source):
                    raise ValueError('truncated GP finish-banner LZ77 literal')
                output.append(source[cursor]);cursor+=1
            else:
                if cursor+2>len(source):
                    raise ValueError('truncated GP finish-banner LZ77 match')
                pair=(source[cursor]<<8)|source[cursor+1];cursor+=2
                length=(pair>>12)+3
                distance=(pair&0xfff)+1
                if distance>len(output):
                    raise ValueError('invalid GP finish-banner LZ77 distance')
                for _ in range(length):
                    output.append(output[-distance])
                    if len(output)>=size: break
    return bytes(output)


def payload(rom: bytes) -> bytes:
    if len(rom)<max(PALETTE_OFFSET+PALETTE_BYTES,
                    CELLS_OFFSET+CELL_BYTES,TILES_OFFSET+4):
        raise ValueError('ROM is too small for GP finish-banner assets')
    tiles=decode_lz77(rom,TILES_OFFSET)
    if len(tiles)!=TILE_BYTES:
        raise ValueError('wrong GP finish-banner decoded tile size')
    header=struct.pack('<8I',MAGIC,TILE_BYTES,PALETTE_BYTES//2,CELL_BYTES,
                       TILE_BASE,ORIGIN_X,ORIGIN_Y,0)
    return header+tiles+rom[PALETTE_OFFSET:PALETTE_OFFSET+PALETTE_BYTES]+\
        rom[CELLS_OFFSET:CELLS_OFFSET+CELL_BYTES]


def build(rom_path: pathlib.Path,output: pathlib.Path):
    if output.exists():
        raise ValueError(f'output already exists: {output}')
    data=payload(rom_path.read_bytes())
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='gp-finish-banner-',
                                     dir=output.parent) as temporary:
        root=pathlib.Path(temporary)
        (root/'gp-finish-banner.bin').write_bytes(data)
        (root/'manifest.json').write_text(json.dumps({'sections':[{
            'kind':21,'content_id':1,'version':1,'flags':3,
            'semantic_revision':1,'path':'gp-finish-banner.bin'}]},
            separators=(',',':'))+'\n')
        pack_digest,deterministic=build_asset_pack.build_pack(
            root/'manifest.json',output)
    return hashlib.sha256(data).hexdigest(),pack_digest,deterministic


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom',type=pathlib.Path,required=True)
    parser.add_argument('--output',type=pathlib.Path,required=True)
    args=parser.parse_args()
    try:
        payload_digest,pack_digest,deterministic=build(args.rom,args.output)
    except (OSError,ValueError) as error:
        parser.error(str(error))
    print(f'payload_sha256={payload_digest}')
    print(f'pack_sha256={pack_digest}')
    print(f'deterministic_sha256={deterministic}')
    return 0


if __name__=='__main__':
    raise SystemExit(main())
