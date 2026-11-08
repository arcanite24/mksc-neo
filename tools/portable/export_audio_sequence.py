#!/usr/bin/env python3
"""Normalize one assembled M4A song object into a portable sequence asset."""

import argparse
import pathlib
import struct

MAGIC = b"MKSCAUD1"
HEADER_SIZE = 32
EVENT_SIZE = 16
CLOCKS = list(range(25)) + [28, 30, 32, 36, 40, 42, 44, 48, 52, 54, 56,
    60, 64, 66, 68, 72, 76, 78, 80, 84, 88, 90, 92, 96]

END, TEMPO, PRIORITY, KEY_SHIFT, VOICE, VOLUME, PAN, BEND, BEND_RANGE, \
    LFO_SPEED, LFO_DELAY, MOD_DEPTH, MOD_TYPE, TUNE, NOTE, TIE, END_TIE, \
    MEMORY_SET, LOOP, LOOP_POINT = range(1, 21)

PARAMETER_OPS = {
    0xBA: PRIORITY, 0xBB: TEMPO, 0xBC: KEY_SHIFT, 0xBD: VOICE,
    0xBE: VOLUME, 0xBF: PAN, 0xC0: BEND, 0xC1: BEND_RANGE,
    0xC2: LFO_SPEED, 0xC3: LFO_DELAY, 0xC4: MOD_DEPTH,
    0xC5: MOD_TYPE, 0xC8: TUNE,
}


class ExportError(ValueError):
    pass


class Elf32:
    def __init__(self, path):
        self.data = pathlib.Path(path).read_bytes()
        if self.data[:6] != b"\x7fELF\x01\x01":
            raise ExportError("expected little-endian ELF32 object")
        fields = struct.unpack_from("<HHIIIIIHHHHHH", self.data, 16)
        shoff, shentsize, shnum, shstrndx = fields[5], fields[10], fields[11], fields[12]
        if shentsize != 40 or shoff + shnum * shentsize > len(self.data):
            raise ExportError("invalid ELF section table")
        raw = [struct.unpack_from("<IIIIIIIIII", self.data, shoff + i * 40)
               for i in range(shnum)]
        names = self._section_bytes(raw[shstrndx], raw)
        self.sections = []
        for entry in raw:
            self.sections.append({
                "name": self._cstring(names, entry[0]), "type": entry[1],
                "offset": entry[4], "size": entry[5], "link": entry[6],
                "info": entry[7], "entsize": entry[9],
            })
        self.symbols = []
        self.relocations = {}
        self.relocation_records = {}
        self._read_symbols()
        self._read_relocations()

    @staticmethod
    def _cstring(data, offset):
        if offset >= len(data):
            raise ExportError("invalid ELF string offset")
        return data[offset:data.index(0, offset)].decode("ascii")

    def _section_bytes(self, entry, entries=None):
        offset, size = entry[4], entry[5]
        if offset + size > len(self.data):
            raise ExportError("ELF section exceeds file")
        return self.data[offset:offset + size]

    def section(self, name):
        for section in self.sections:
            if section["name"] == name:
                start = section["offset"]
                return self.data[start:start + section["size"]]
        raise ExportError(f"missing {name} section")

    def _read_symbols(self):
        for section in self.sections:
            if section["type"] != 2:
                continue
            strings = self.section(self.sections[section["link"]]["name"])
            if section["entsize"] != 16 or section["size"] % 16:
                raise ExportError("invalid ELF symbol table")
            for offset in range(section["offset"], section["offset"] + section["size"], 16):
                name, value, size, info, other, shndx = struct.unpack_from(
                    "<IIIBBH", self.data, offset)
                self.symbols.append({"name": self._cstring(strings, name),
                                     "value": value, "size": size,
                                     "info": info, "shndx": shndx})

    def _read_relocations(self):
        for section in self.sections:
            if section["type"] != 9:
                continue
            if section["entsize"] != 8 or section["size"] % 8:
                raise ExportError("invalid ELF relocation table")
            target = self.sections[section["info"]]["name"]
            target_bytes = self.section(target)
            for offset in range(section["offset"], section["offset"] + section["size"], 8):
                place, info = struct.unpack_from("<II", self.data, offset)
                symbol_index, kind = info >> 8, info & 0xff
                if kind != 2 or symbol_index >= len(self.symbols):
                    continue
                if place + 4 > len(target_bytes):
                    raise ExportError("relocation exceeds target section")
                symbol = self.symbols[symbol_index]
                addend = struct.unpack_from("<I", target_bytes, place)[0]
                self.relocation_records[(target, place)] = (
                    symbol["name"], symbol["shndx"], symbol["value"], addend)
                if symbol["shndx"] >= len(self.sections) or \
                        self.sections[symbol["shndx"]]["name"] != target:
                    continue
                self.relocations[(target, place)] = symbol["value"] + addend

    def symbol(self, name, section_name=".rodata"):
        for symbol in self.symbols:
            if symbol["name"] == name and symbol["shndx"] < len(self.sections) and \
                    self.sections[symbol["shndx"]]["name"] == section_name:
                return symbol["value"]
        raise ExportError(f"missing symbol {name}")

    def pointer(self, section_name, offset):
        try:
            return self.relocations[(section_name, offset)]
        except KeyError as error:
            raise ExportError(f"missing internal relocation at 0x{offset:x}") from error

    def relocation(self, section_name, offset):
        try:
            return self.relocation_records[(section_name, offset)]
        except KeyError as error:
            raise ExportError(f"missing relocation at 0x{offset:x}") from error


class TrackParser:
    def __init__(self, data, pointers, track, clocks=None):
        self.data, self.pointers, self.track = data, pointers, track
        self.clocks = CLOCKS if clocks is None else clocks
        if len(self.clocks) != 49:
            raise ExportError("sequence clock table must contain 49 entries")
        self.tick = 0
        self.running = None
        self.key = self.velocity = 0
        self.events = []
        self.offset_events = {}
        self.offset_ticks = {}
        self.emitted_offsets = set()
        self.steps = 0

    def byte(self, offset):
        if offset < 0 or offset >= len(self.data):
            raise ExportError("track command exceeds sequence section")
        return self.data[offset]

    def emit(self, offset, operation, value=0, arg0=0, arg1=0, target=None):
        self.offset_events.setdefault(offset, len(self.events))
        self.emitted_offsets.add(offset)
        self.events.append({"tick": self.tick, "track": self.track,
                            "op": operation, "value": value, "arg0": arg0,
                            "arg1": arg1, "target_offset": target,
                            "local": len(self.events)})

    def parse(self, start):
        self._run(start, False, [])
        if not self.events or self.events[-1]["op"] not in (END, LOOP):
            raise ExportError(f"track {self.track} did not terminate")
        targets = {event["target_offset"] for event in self.events
                   if event["target_offset"] is not None}
        for target in sorted(targets, key=self.offset_events.__getitem__):
            if target in self.emitted_offsets:
                continue
            boundary = self.offset_events[target]
            self.events.insert(boundary, {
                "tick": self.offset_ticks[target], "track": self.track,
                "op": LOOP_POINT, "value": 0, "arg0": 0, "arg1": 0,
                "target_offset": None, "local": boundary,
            })
            for offset, index in tuple(self.offset_events.items()):
                if index >= boundary:
                    self.offset_events[offset] = index + 1
            self.offset_events[target] = boundary
            self.emitted_offsets.add(target)
        for local, event in enumerate(self.events):
            event["local"] = local
        for event in self.events:
            target = event.pop("target_offset")
            if target is not None:
                if target not in self.offset_events:
                    raise ExportError(f"loop target 0x{target:x} is not an event boundary")
                event["target_local"] = self.offset_events[target]
        return self.events

    def _run(self, offset, pattern, stack):
        while True:
            self.steps += 1
            if self.steps > 1000000:
                raise ExportError("sequence expansion exceeds step budget")
            command_offset = offset
            self.offset_events.setdefault(command_offset, len(self.events))
            self.offset_ticks.setdefault(command_offset, self.tick)
            command = self.byte(offset)
            if command < 0x80:
                if self.running is None:
                    raise ExportError(f"missing running status at 0x{offset:x}")
                command = self.running
            else:
                offset += 1
                if command >= 0xBD:
                    self.running = command
            if 0x80 <= command <= 0xB0:
                self.tick += self.clocks[command - 0x80]
                continue
            if command == 0xB1:
                if pattern:
                    raise ExportError("FINE inside pattern")
                self.emit(command_offset, END)
                return
            if command == 0xB2:
                target = self.pointers(offset)
                offset += 4
                if target >= command_offset:
                    raise ExportError("only bounded backward GOTO is supported")
                self.emit(command_offset, LOOP, target=target)
                return
            if command == 0xB3:
                target = self.pointers(offset)
                offset += 4
                if target in stack or len(stack) >= 3:
                    raise ExportError("recursive or over-depth pattern")
                self._run(target, True, stack + [target])
                continue
            if command == 0xB4:
                if pattern:
                    return
                continue
            if command == 0xB5:
                raise ExportError("REPT is not used by the V03 inventory")
            if command == 0xB9:
                operation, address, value = self.data[offset:offset + 3]
                offset += 3
                if operation != 0:
                    raise ExportError("only MEMACC set is used by the V03 inventory")
                self.emit(command_offset, MEMORY_SET, arg0=address, arg1=value)
                continue
            if command in PARAMETER_OPS:
                value = self.byte(offset)
                if value >= 0x80:
                    raise ExportError(f"missing parameter at 0x{command_offset:x}")
                offset += 1
                self.emit(command_offset, PARAMETER_OPS[command], value=value)
                continue
            if command == 0xCE:
                key = self.key
                if self.byte(offset) < 0x80:
                    key = self.byte(offset)
                    offset += 1
                self.emit(command_offset, END_TIE, arg0=key)
                continue
            if 0xCF <= command <= 0xFF:
                key, velocity = self.key, self.velocity
                if self.byte(offset) < 0x80:
                    key = self.byte(offset); offset += 1
                    if self.byte(offset) < 0x80:
                        velocity = self.byte(offset); offset += 1
                gate = 0 if command == 0xCF else self.clocks[command - 0xCF]
                if self.byte(offset) < 0x80:
                    gate += self.byte(offset); offset += 1
                if gate > 255:
                    raise ExportError("note gate exceeds normalized field")
                self.key, self.velocity = key, velocity
                self.emit(command_offset, TIE if command == 0xCF else NOTE,
                          value=gate, arg0=key, arg1=velocity)
                continue
            raise ExportError(f"unsupported command 0x{command:02x} at 0x{command_offset:x}")


def export_object(object_path, symbol, song_id, bank_id, player_index=None):
    elf = Elf32(object_path)
    data = elf.section(".rodata")
    header = elf.symbol(symbol)
    return export_sequence(data, header,
                           lambda offset: elf.pointer(".rodata", offset),
                           song_id, bank_id, player_index=player_index)


def export_sequence(data, header, pointer, song_id, bank_id, clocks=None, player_index=None):
    """Normalize sequence bytes with either ELF relocations or ROM pointers."""
    if header < 0 or header + 8 > len(data):
        raise ExportError("song header exceeds section")
    track_count, block_count, priority, reverb = data[header:header + 4]
    if not 1 <= track_count <= 16 or block_count != 0:
        raise ExportError("unsupported song header")
    tracks = []
    for track in range(track_count):
        start = pointer(header + 8 + track * 4)
        parser = TrackParser(data, pointer, track, clocks)
        tracks.append(parser.parse(start))
    merged = sorted((event for events in tracks for event in events),
                    key=lambda event: (event["tick"], event["track"], event["local"]))
    global_indices = {(event["track"], event["local"]): index
                      for index, event in enumerate(merged)}
    body = bytearray()
    for event in merged:
        target = 0
        if event["op"] == LOOP:
            target = global_indices[(event["track"], event["target_local"])]
        body.extend(struct.pack("<IIHBBBBH", event["tick"], target,
            event["value"], event["track"], event["op"], event["arg0"],
            event["arg1"], 0))
    total = HEADER_SIZE + len(body)
    if player_index is not None and not 0 <= player_index < 11:
        raise ExportError("invalid sequence player")
    header_bytes = struct.pack("<8sHHHBBBBHIII", MAGIC, HEADER_SIZE, 1 if player_index is None else 2,
        song_id, track_count, priority, reverb, 0 if player_index is None else player_index, bank_id, len(merged),
        HEADER_SIZE, total)
    return header_bytes + body


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("object", type=pathlib.Path)
    parser.add_argument("symbol")
    parser.add_argument("song_id", type=int)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--bank-id", type=int, required=True)
    args = parser.parse_args()
    if not 0 <= args.song_id < 403:
        parser.error("song_id must be in 0..402")
    if not 1 <= args.bank_id <= 0xffff:
        parser.error("bank_id must be in 1..65535")
    try:
        output = export_object(args.object, args.symbol, args.song_id,
                               args.bank_id)
    except ExportError as error:
        parser.error(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(f"audio sequence: song={args.song_id} events={(len(output)-32)//16} "
          f"bytes={len(output)}")


if __name__ == "__main__":
    main()
