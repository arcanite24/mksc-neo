#!/usr/bin/env python3
"""Flatten one M4A voice group into a validated portable voice bank."""

import argparse
import pathlib
import re
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from export_audio_sequence import Elf32, ExportError

MAGIC = b"MKSCVOC1"
HEADER_SIZE = 40
VOICE_SIZE = 20
MAPPING_SIZE = 8
PCM, PSG_SQUARE, PSG_WAVE, PSG_NOISE, KEY_SPLIT, DRUM = range(1, 7)


class UnmappedKey(Exception):
    pass


class BankFlattener:
    def __init__(self, object_dir):
        self.object_dir = pathlib.Path(object_dir)
        self.cache = {}
        self.output_voices = []
        self.direct_cache = {}
        self.mappings = []

    def elf(self, symbol):
        if symbol not in self.cache:
            path = self.object_dir / f"{symbol}.o"
            if not path.is_file():
                raise ExportError(f"missing bank dependency {path}")
            self.cache[symbol] = Elf32(path)
        return self.cache[symbol]

    def tone_count(self, symbol):
        data = self.elf(symbol).section(".rodata")
        if len(data) % 12:
            raise ExportError(f"{symbol} is not a ToneData table")
        return len(data) // 12

    def raw_tone(self, symbol, index):
        elf = self.elf(symbol)
        data = elf.section(".rodata")
        offset = index * 12
        if offset + 12 > len(data):
            raise UnmappedKey()
        return elf, data, offset, data[offset:offset + 12]

    @staticmethod
    def relocation_name(elf, offset):
        name, _section, _value, addend = elf.relocation(".rodata", offset)
        return name, struct.unpack("<i", struct.pack("<I", addend))[0]

    @staticmethod
    def sample_id(name):
        match = re.fullmatch(r"wave_(\d+)", name)
        if match:
            return int(match.group(1)) + 1
        match = re.fullmatch(r"gbwave_(\d+)", name)
        if match:
            return 0x8000 + int(match.group(1))
        raise ExportError(f"unsupported sample symbol {name}")

    def sample_source(self, source, offset, kind):
        name, _ = self.relocation_name(source, offset)
        return self.sample_id(name)

    def mapping_key(self, symbol, addend, key):
        elf = self.elf(symbol)
        data = elf.section(".rodata")
        offset = elf.symbol(symbol) + addend + key
        if offset < 0 or offset >= len(data):
            raise UnmappedKey()
        return data[offset]

    def direct(self, symbol, index):
        elf, _data, offset, raw = self.raw_tone(symbol, index)
        tone_type, key, length, pan = raw[:4]
        attack, decay, sustain, release = raw[8:12]
        channel = tone_type & 7
        if tone_type & 0xC0:
            raise ExportError("composite voice reached direct decoder")
        if channel == 0:
            kind = PCM
            source_id = self.sample_source(elf, offset + 4, kind)
        elif channel in (1, 2):
            kind, source_id = PSG_SQUARE, struct.unpack_from("<I", raw, 4)[0]
        elif channel == 3:
            kind = PSG_WAVE
            source_id = self.sample_source(elf, offset + 4, kind)
        elif channel == 4:
            kind, source_id = PSG_NOISE, struct.unpack_from("<I", raw, 4)[0]
        else:
            raise ExportError(f"unsupported voice type 0x{tone_type:02x}")
        if key > 127 or (kind == PSG_SQUARE and source_id > 3) or \
                (kind == PSG_NOISE and source_id > 1):
            raise ExportError("invalid direct voice parameter")
        return (tone_type, key, length, pan, attack, decay, sustain, release,
                kind, source_id, 0, 0)

    def resolve(self, symbol, index, key, stack=()):
        identity = (symbol, index, key)
        if identity in stack or len(stack) >= 8:
            raise ExportError("recursive voice table")
        elf, _data, offset, raw = self.raw_tone(symbol, index)
        tone_type = raw[0]
        if not tone_type & 0xC0:
            return self.direct(symbol, index)
        child, _ = self.relocation_name(elf, offset + 4)
        if tone_type & 0x80:
            return self.resolve(child, key, key, stack + (identity,))
        mapping_name, addend = self.relocation_name(elf, offset + 8)
        return self.resolve(child, self.mapping_key(mapping_name, addend, key),
                            key, stack + (identity,))

    def append_direct(self, voice):
        if voice not in self.direct_cache:
            self.direct_cache[voice] = len(self.output_voices)
            self.output_voices.append(voice)
        return self.direct_cache[voice]

    def composite(self, symbol, index, is_drum):
        groups = []
        for key in range(128):
            try:
                voice = self.resolve(symbol, index, key)
            except UnmappedKey:
                continue
            target = self.append_direct(voice)
            tone_key, pan = voice[1], voice[3]
            if not is_drum and groups and groups[-1][2:] == [target, tone_key, pan] \
                    and groups[-1][1] + 1 == key:
                groups[-1][1] = key
            else:
                groups.append([key, key, target, tone_key, pan])
        if not groups:
            raise ExportError("composite voice has no mapped keys")
        first = len(self.mappings)
        self.mappings.extend(groups)
        return first, len(groups)

    def export(self, symbol, bank_id):
        count = self.tone_count(symbol)
        if not 1 <= count <= 128:
            raise ExportError("top-level voice bank must contain 1..128 voices")
        # Reserve original indices before appending flattened child voices.
        self.output_voices = [None] * count
        for index in range(count):
            elf, _data, offset, raw = self.raw_tone(symbol, index)
            tone_type, key, length, pan = raw[:4]
            if tone_type & 0xC0:
                is_drum = bool(tone_type & 0x80)
                first, mapping_count = self.composite(symbol, index, is_drum)
                self.output_voices[index] = (tone_type, key, length, pan,
                    0, 0, 0, 0, DRUM if is_drum else KEY_SPLIT, 0,
                    first, mapping_count)
            else:
                self.output_voices[index] = self.direct(symbol, index)
                self.direct_cache.setdefault(self.output_voices[index], index)
        voice_bytes = bytearray()
        for voice in self.output_voices:
            (tone_type,key,length,pan,attack,decay,sustain,release,kind,
             source_id,first,mapping_count)=voice
            voice_bytes.extend(struct.pack("<BBBBBBBBB3xIHH",tone_type,key,
                length,pan,attack,decay,sustain,release,kind,source_id,first,
                mapping_count))
        mapping_bytes = bytearray()
        for start,end,target,tone_key,pan in self.mappings:
            mapping_bytes.extend(struct.pack("<BBHBBH",start,end,target,
                                             tone_key,pan,0))
        mappings_offset=HEADER_SIZE+len(voice_bytes)
        total=mappings_offset+len(mapping_bytes)
        header=struct.pack("<8sHHHHIIIIII",MAGIC,HEADER_SIZE,1,bank_id,
            len(self.output_voices),len(self.mappings),HEADER_SIZE,
            mappings_offset,total,0,0)
        return header+voice_bytes+mapping_bytes


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("object_dir",type=pathlib.Path)
    parser.add_argument("symbol")
    parser.add_argument("bank_id",type=int)
    parser.add_argument("output",type=pathlib.Path)
    args=parser.parse_args()
    if not 1<=args.bank_id<=0xffff:parser.error("bank_id must be in 1..65535")
    try: output=BankFlattener(args.object_dir).export(args.symbol,args.bank_id)
    except (ExportError,UnmappedKey) as error:parser.error(str(error))
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(output)
    voices=struct.unpack_from("<H",output,14)[0]
    mappings=struct.unpack_from("<I",output,16)[0]
    print(f"audio voice bank: id={args.bank_id} voices={voices} mappings={mappings} bytes={len(output)}")


if __name__=="__main__":main()
