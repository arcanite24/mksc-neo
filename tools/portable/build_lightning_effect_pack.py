#!/usr/bin/env python3
"""Build the deterministic source Lightning-effect graphics overlay."""
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


MAGIC = 0x3146584C
ROM_OFFSET = 0x6C38C
TILE_BYTES = 0x800
FRAME_COUNT = 4
TILE_BASE = 0x1A0


def payload(rom: bytes) -> bytes:
    if len(rom) < ROM_OFFSET + TILE_BYTES:
        raise ValueError('ROM is too small for Lightning effect tiles')
    return struct.pack('<4I',MAGIC,TILE_BYTES,FRAME_COUNT,TILE_BASE)+\
        rom[ROM_OFFSET:ROM_OFFSET+TILE_BYTES]


def build(rom_path: pathlib.Path, output: pathlib.Path):
    if output.exists():
        raise ValueError(f'output already exists: {output}')
    data = payload(rom_path.read_bytes())
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='lightning-effect-',
                                     dir=output.parent) as temporary:
        root=pathlib.Path(temporary)
        (root/'lightning-effect.bin').write_bytes(data)
        (root/'manifest.json').write_text(json.dumps({'sections':[{
            'kind':20,'content_id':1,'version':1,'flags':3,
            'semantic_revision':1,'path':'lightning-effect.bin'}]},
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
