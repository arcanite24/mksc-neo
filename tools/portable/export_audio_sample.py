#!/usr/bin/env python3
"""Normalize one assembled M4A PCM or programmable-wave object."""

import argparse
import pathlib
import struct

from export_audio_sequence import Elf32, ExportError

MAGIC = b"MKSCSMP1"
HEADER_SIZE = 40
PCM_S8 = 1
PSG_WAVE4 = 2
LOOP = 1


def build_asset(sample_id, encoding, flags, frequency_q10, count, loop_start,
                payload):
    total = HEADER_SIZE + len(payload)
    header = struct.pack("<8sHHHBBIIIIII", MAGIC, HEADER_SIZE, 1, sample_id,
        encoding, flags, frequency_q10, count, loop_start, HEADER_SIZE, total, 0)
    return header + payload


def normalize_pcm(raw, sample_id):
    if len(raw) < 17:
        raise ExportError("PCM wave section is too small")
    wave_type, status, frequency, loop_start, stored_size = struct.unpack_from(
        "<HHIII", raw)
    payload = raw[16:]
    if wave_type != 0 or status & ~0x4000:
        raise ExportError("unsupported PCM wave type or status")
    if stored_size + 1 != len(payload):
        raise ExportError("PCM wave size does not match payload")
    if frequency < 1024:
        raise ExportError("PCM source rate is zero")
    flags = LOOP if status & 0x4000 else 0
    if flags and loop_start >= len(payload):
        raise ExportError("PCM loop start exceeds payload")
    if not flags and loop_start != 0:
        raise ExportError("non-looping PCM wave has a loop start")
    return build_asset(sample_id, PCM_S8, flags, frequency,
                       len(payload), loop_start, payload)


def normalize_psg(raw, sample_id):
    if len(raw) != 16:
        raise ExportError("programmable wave must contain 32 packed samples")
    return build_asset(sample_id, PSG_WAVE4, 0, 0, 32, 0, raw)


def export_object(object_path, symbol, sample_id):
    elf = Elf32(object_path)
    if symbol.startswith("wave_"):
        raw = elf.section(f".rodata.{symbol}")
        if elf.symbol(symbol, f".rodata.{symbol}") != 0:
            raise ExportError("PCM wave symbol is not at section start")
        return normalize_pcm(raw, sample_id)
    if symbol.startswith("gbwave_"):
        raw = elf.section(".rodata")
        offset = elf.symbol(symbol)
        return normalize_psg(raw[offset:offset + 16], sample_id)
    raise ExportError("sample symbol must start with wave_ or gbwave_")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("object", type=pathlib.Path)
    parser.add_argument("symbol")
    parser.add_argument("sample_id", type=int)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    if not 0 <= args.sample_id <= 0xffff:
        parser.error("sample_id must fit in 16 bits")
    try:
        output = export_object(args.object, args.symbol, args.sample_id)
    except ExportError as error:
        parser.error(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(f"audio sample: id={args.sample_id} bytes={len(output)}")


if __name__ == "__main__":
    main()
