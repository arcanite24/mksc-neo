#!/usr/bin/env python3
"""Inspect a Nintendo DS cartridge the way the native game importer reads it.

  nds_inspect.py list ROM [GLOB ...]      files with sizes; matching .carc/.narc
                                          archives are listed with their contents
  nds_inspect.py extract ROM PATH [--out DIR]
                                          one file; PATH may go into archives,
                                          e.g. data/Course/cross_course.carc/course_map.nkm
  nds_inspect.py text ROM PATH            BMG messages as index: text

Output goes to .mksc-tools/portable/runs/nds-inspect/ unless --out is given.
The C readers in portable/import/nds/ are the source of truth; this mirrors
them for research only and never writes into the repository.
"""
import argparse
import fnmatch
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUT = ROOT / '.mksc-tools/portable/runs/nds-inspect'


def lz77(data):
    kind, size = data[0], int.from_bytes(data[1:4], 'little')
    at = 4
    if size == 0:
        size, at = int.from_bytes(data[4:8], 'little'), 8
    if kind not in (0x10, 0x11):
        raise ValueError(f'unsupported compression {kind:#x}')
    out = bytearray()
    while len(out) < size:
        flags = data[at]
        at += 1
        for bit in range(7, -1, -1):
            if len(out) >= size:
                break
            if not flags & (1 << bit):
                out.append(data[at])
                at += 1
                continue
            first = data[at]
            if kind == 0x10:
                length, distance, at = (first >> 4) + 3, ((first & 15) << 8 | data[at + 1]) + 1, at + 2
            elif first >> 4 == 0:
                length = ((first & 15) << 4 | data[at + 1] >> 4) + 0x11
                distance, at = ((data[at + 1] & 15) << 8 | data[at + 2]) + 1, at + 3
            elif first >> 4 == 1:
                length = ((first & 15) << 12 | data[at + 1] << 4 | data[at + 2] >> 4) + 0x111
                distance, at = ((data[at + 2] & 15) << 8 | data[at + 3]) + 1, at + 4
            else:
                length, distance, at = (first >> 4) + 1, ((first & 15) << 8 | data[at + 1]) + 1, at + 2
            if distance > len(out):
                raise ValueError('match before output start')
            for _ in range(length):
                out.append(out[-distance])
    return bytes(out[:size])


def walk(names, table, files, directory=0, prefix=''):
    """FNT names to (path, bytes); table holds absolute (start, end) pairs."""
    cursor, file_id = struct.unpack_from('<IH', names, directory * 8)
    while True:
        kind = names[cursor]
        cursor += 1
        if kind == 0:
            return
        name = names[cursor:cursor + (kind & 0x7F)].decode('ascii')
        cursor += kind & 0x7F
        if kind & 0x80:
            child = struct.unpack_from('<H', names, cursor)[0] & 0xFFF
            cursor += 2
            yield from walk(names, table, files, child, prefix + name + '/')
        else:
            start, end = table[file_id]
            yield prefix + name, files[start:end]
            file_id += 1


def rom_files(rom):
    fnt, fnt_size, fat, fat_size = struct.unpack_from('<IIII', rom, 0x40)
    table = [struct.unpack_from('<II', rom, fat + index * 8) for index in range(fat_size // 8)]
    return dict(walk(rom[fnt:fnt + fnt_size], table, rom))


def narc_files(blob):
    if blob[:4] != b'NARC':
        blob = lz77(blob)
    if blob[:4] != b'NARC':
        raise ValueError('not a NARC')
    at = 16
    chunks = {}
    for _ in range(3):
        magic, size = blob[at:at + 4], struct.unpack_from('<I', blob, at + 4)[0]
        chunks[magic] = blob[at + 8:at + size]
        at += size
    allocation = chunks[b'BTAF']
    count = struct.unpack_from('<H', allocation, 0)[0]
    table = [struct.unpack_from('<II', allocation, 4 + index * 8) for index in range(count)]
    named = dict(walk(chunks[b'BTNF'], table, chunks[b'GMIF']))
    return named or {f'#{index}': chunks[b'GMIF'][start:end] for index, (start, end) in enumerate(table)}


def open_path(rom, path):
    """Resolve a path that may continue inside .carc/.narc archives."""
    files, parts, rest = rom_files(rom), path.split('/'), ''
    for split in range(len(parts), 0, -1):
        head = '/'.join(parts[:split])
        if head in files:
            data, rest = files[head], '/'.join(parts[split:])
            break
    else:
        raise KeyError(f'{path}: not in the ROM')
    return open_path_in(narc_files(data), rest, path) if rest else data


def open_path_in(files, path, full):
    parts = path.split('/')
    for split in range(len(parts), 0, -1):
        head = '/'.join(parts[:split])
        if head in files:
            rest = '/'.join(parts[split:])
            return open_path_in(narc_files(files[head]), rest, full) if rest else files[head]
    raise KeyError(f'{full}: missing {path}')


def bmg_messages(blob):
    if blob[:8] != b'MESGbmg1' or blob[16] != 2:
        raise ValueError('not a UTF-16 BMG')
    at, sections, offsets, text = 0x20, struct.unpack_from('<I', blob, 12)[0], [], b''
    for _ in range(sections):
        magic, size = blob[at:at + 4], struct.unpack_from('<I', blob, at + 4)[0]
        payload = blob[at + 8:at + size]
        if magic == b'INF1':
            count, entry = struct.unpack_from('<HH', payload, 0)
            offsets = [struct.unpack_from('<I', payload, 8 + index * entry)[0] for index in range(count)]
        elif magic == b'DAT1':
            text = payload
        at += size
    for offset in offsets:
        chars, at = [], offset
        while (code := struct.unpack_from('<H', text, at)[0]) != 0:
            if code == 0x1A:
                length = text[at + 2]
                chars.append(f'<tag {text[at + 2:at + length].hex()}>')
                at += length
                continue
            chars.append(chr(code) if code < 0xE000 else f'<U+{code:04X}>')
            at += 2
        yield ''.join(chars)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    listing = commands.add_parser('list')
    listing.add_argument('rom', type=Path)
    listing.add_argument('globs', nargs='*')
    extract = commands.add_parser('extract')
    extract.add_argument('rom', type=Path)
    extract.add_argument('path')
    extract.add_argument('--out', type=Path, default=DEFAULT_OUT)
    text = commands.add_parser('text')
    text.add_argument('rom', type=Path)
    text.add_argument('path')
    args = parser.parse_args()
    rom = args.rom.read_bytes()
    print(f'{rom[:12].rstrip(bytes(1)).decode("ascii")} ({rom[12:16].decode("ascii")}), {len(rom)} bytes')
    if args.command == 'list':
        for path, data in rom_files(rom).items():
            print(f'{len(data):9d} {path}')
            if path.endswith(('.carc', '.narc')) and args.globs and any(fnmatch.fnmatchcase(path, glob) for glob in args.globs):
                for inner, payload in narc_files(data).items():
                    print(f'{len(payload):9d}   {path}/{inner} {payload[:4]!r}')
    elif args.command == 'extract':
        data = open_path(rom, args.path)
        target = args.out / args.path.replace('/', '__')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        print(f'{len(data)} bytes -> {target}')
    else:
        for index, message in enumerate(bmg_messages(open_path(rom, args.path))):
            print(f'{index:4d}: {message}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
