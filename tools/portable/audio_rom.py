"""Decode the US ROM's M4A song table into native audio pack sections."""

import bisect
import struct

from export_audio_sequence import ExportError, export_sequence
from export_audio_voice_bank import BankFlattener, PCM, PSG_WAVE, UnmappedKey
from export_audio_sample import normalize_pcm, normalize_psg

ROM_BASE = 0x08000000
SONG_TABLE = 0x102498  # gSongTable, US revision 0
SONG_COUNT = 403


class RomAudio:
    def __init__(self, data):
        self.data = data
        if len(data) != 0x400000 or data[0xAC:0xB0] != b"AMKE" or data[0xBC] != 0:
            raise ExportError("audio import requires the 4 MiB US revision 0 ROM")
        self.songs = []
        for song_id in range(1, SONG_COUNT):
            header = self.pointer(SONG_TABLE + song_id * 8)
            if data[header] == 0:  # Original dummy_song entries have no tracks.
                continue
            bank = self.pointer(header + 4)
            self.songs.append((song_id, header, bank))
        self.banks = sorted({bank for _, _, bank in self.songs})
        self.boundaries = set(self.banks + [SONG_TABLE])
        # ToneData tables may be shorter than 128 entries. Child tables, key
        # maps and wave data also terminate tables; use their ROM addresses,
        # rather than reading into the next allocation as another voice.
        pending = list(self.banks)
        visited = set()
        while pending:
            address = pending.pop()
            if address in visited:
                continue
            visited.add(address)
            for index in range(128):
                offset = address + index * 12
                limits = self.banks + [SONG_TABLE] if address in self.banks else self.boundaries
                end = min((b for b in limits if b > address), default=len(data))
                if offset + 12 > min(end, address + 128 * 12):
                    break
                raw = self.read(offset, 12)
                tone_type = raw[0]
                if tone_type & 0xC0:
                    child = self.pointer(offset + 4)
                    self.boundaries.add(child)
                    pending.append(child)
                    if not tone_type & 0x80:
                        self.boundaries.add(self.pointer(offset + 8))
                elif tone_type & 7 in (0, 3):
                    self.boundaries.add(self.pointer(offset + 4))
        self.boundaries = sorted(self.boundaries)
        self.samples = {}

    def read(self, offset, size):
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise ExportError(f"audio data outside ROM at 0x{offset:x}, size={size}")
        return self.data[offset:offset + size]

    def pointer(self, offset):
        value = struct.unpack("<I", self.read(offset, 4))[0]
        address = value - ROM_BASE
        if address < 0 or address >= len(self.data):
            raise ExportError(f"invalid ROM audio pointer 0x{value:08x} at 0x{offset:x}")
        return address

    def tone_count(self, address):
        if address in self.banks:
            following = bisect.bisect_right(self.banks, address)
            end = self.banks[following] if following < len(self.banks) else SONG_TABLE
            return min(128, (end - address) // 12)
        following = bisect.bisect_right(self.boundaries, address)
        end = self.boundaries[following] if following < len(self.boundaries) else len(self.data)
        return min(128, (end - address) // 12)

    def sample_id(self, kind, address):
        identity = (kind, address)
        if identity not in self.samples:
            count = sum(k == kind for k, _ in self.samples)
            sample_id = (0x8000 if kind == PSG_WAVE else 1) + count
            if count >= 0x7FFF:
                raise ExportError("too many audio samples")
            self.samples[identity] = sample_id
        return self.samples[identity]

    def engine_sound_rules(self):
        # sub_8040FEC profiles, sub_8041244 volume buckets and song tables.
        # Each ROM volume entry is a four-byte pair; only the first halfword
        # controls the engine track. Decode it instead of serializing padding.
        payload = struct.pack("<4s6H", b"ENG1", 3, 8, 16, 4, 6, 4)
        payload += self.read(0xEC94C, 36)
        payload += b"".join(self.read(0xEC970 + index * 4, 2)
                            for index in range(8 * 16))
        payload += self.read(0xECB88, 4 * 8 * 2)
        payload += self.read(0xECB70, 4 * 2)
        return payload

    def character_audio_rules(self):
        slots=self.read(0xD92BC,8)
        if any(slot>=9 for slot in slots) or len(set(slots))!=8:
            raise ExportError("invalid character audio slot map")
        locks=[struct.unpack("<H",self.read(0xD92C4+slot*2,2))[0] for slot in slots]
        revs=struct.unpack("<16H",self.read(0xD92FA,32))
        launch=struct.unpack("<16H",self.read(0xD95FC,32))
        menu=struct.unpack("<9H",self.read(0xD92E8,18))
        race=struct.unpack("<8H",self.read(0xECB78,16))
        ceremony=struct.unpack("<8I",self.read(0xDBC48,32))
        songs=locks+list(revs[::2])+list(launch[::2])+list(race)+list(ceremony)
        if (any(not 0<song<403 for song in songs) or any(song>=403 for song in menu) or
                any(volume>1023 for volume in revs[1::2]+launch[1::2])):
            raise ExportError("invalid character audio cue")
        return struct.pack("<65H",*locks,*revs,*launch,*menu,*race,*ceremony)

    def hazard_sound_rules(self):
        # 39C9E/ECC08 and FD7C/FEB8/FF80/ECBC8: driver, listener, size.
        voices = self.read(0xECC08, 64)
        impacts = self.read(0xECBC8, 64)
        if any(not 0 < song < 403 for song in struct.unpack("<64H", voices + impacts)):
            raise ExportError("invalid hazard voice song")
        taunt, laugh = self.read(0xECC88, 32), self.read(0xECC68, 32)
        if any(not 0 < song < 403 for song in struct.unpack("<32H", taunt + laugh)):
            raise ExportError("invalid reaction voice song")
        return (struct.pack("<4s4H", b"HAZ1", 4, 8, 2, 2) + voices + impacts + taunt + laugh +
                self.character_audio_rules())

    def finish_sound_rules(self):
        payload = struct.pack("<4sHH", b"FIN1", 2, 4)
        for address in (0xEE5E4, 0xEE604, 0xEE624, 0xEE644):
            cue = 0
            command, argument = struct.unpack("<II", self.read(address, 8))
            if command == 0x81:
                cue = argument
                address += 8
            wait, delay, start, music, end, unused = struct.unpack("<6I", self.read(address, 24))
            if (wait, start, end, unused) != (0xF1, 0x85, 0, 0) or not 0 < delay <= 65535 or not 0 <= cue < 403 or not 0 < music < 403:
                raise ExportError("invalid finish sound script")
            payload += struct.pack("<3H", cue, delay, music)
        # C38C Battle's two MOV immediate song requests.
        songs = [struct.unpack("<H", self.read(address, 2))[0] for address in (0x4C558, 0x4C560)]
        if any(instruction & 0xFF00 != 0x2000 or not instruction & 255 for instruction in songs):
            raise ExportError("invalid Battle finish song instruction")
        lap = struct.unpack("<H", self.read(0x3B950, 2))[0]
        script = struct.unpack("<8I", self.read(0xEC790, 32))
        if lap & 0xFF00 != 0x2000 or not 0 < lap & 255 < 403:
            raise ExportError("invalid lap sound instruction")
        if script[:3] != (0xF4, 0x803DDCD, 0xF1) or script[4:] != (0xF4, 0x803DDD9, 0, 0) or not 0 < script[3] <= 65535:
            raise ExportError("invalid final lap sound script")
        return payload + struct.pack("<4H", *(instruction & 255 for instruction in songs), lap & 255, script[3])

    def player_rules(self):
        # gMPlayTable: two pointers, track count, padding, priority protection.
        flags = []
        for player in range(11):
            flag = struct.unpack("<H", self.read(0x102414 + player * 12 + 10, 2))[0]
            if flag not in (0, 1):
                raise ExportError("invalid music player priority flag")
            flags.append(flag)
        pcm = struct.unpack("<12I", self.read(0xF3314, 48))
        psg = struct.unpack("<12h", self.read(0xF33E0, 24))
        noise = self.read(0xF33F8, 60)
        if (not all(pcm) or any(a >= b for a, b in zip(pcm, pcm[1:])) or
                any(not -2047 <= value < 0 for value in psg) or
                any(value & 8 or value >> 4 > 13 for value in noise)):
            raise ExportError("invalid audio tuning tables")
        return (struct.pack("<8sHH11B", b"MKSCPRI1", 2, 11, *flags) +
                struct.pack("<12I12h", *pcm, *psg) + noise)

    def sections(self):
        sections = [(68, 1, self.engine_sound_rules()), (69, 1, self.player_rules()),
                    (71, 1, self.hazard_sound_rules()), (72, 1, self.finish_sound_rules())]
        bank_ids = {address: index + 1 for index, address in enumerate(self.banks)}
        for address in self.banks:
            bank_id = bank_ids[address]
            try:
                payload = RomBankFlattener(self).export(address, bank_id)
            except (ExportError, UnmappedKey) as error:
                raise ExportError(f"bank at 0x{address:x}: {error}") from error
            sections.append((17, bank_id, payload))
        for song_id, header, bank in self.songs:
            try:
                payload = export_sequence(self.data, header, self.pointer,
                                          song_id, bank_ids[bank], clocks=self.read(0xF3444, 49),
                                          player_index=struct.unpack("<H", self.read(SONG_TABLE+song_id*8+4, 2))[0])
            except ExportError as error:
                raise ExportError(f"song {song_id}: {error}") from error
            sections.append((16, song_id, payload))
        for (kind, address), sample_id in sorted(self.samples.items()):
            if kind == PCM:
                size = struct.unpack("<I", self.read(address + 12, 4))[0]
                payload = normalize_pcm(self.read(address, 16 + size + 1), sample_id)
            else:
                payload = normalize_psg(self.read(address, 16), sample_id)
            sections.append((18, sample_id, payload))
        return sorted(sections)


class RomBankFlattener(BankFlattener):
    def __init__(self, rom):
        super().__init__(".")
        self.rom = rom

    def tone_count(self, address):
        return self.rom.tone_count(address)

    def raw_tone(self, address, index):
        if not 0 <= index < self.tone_count(address):
            raise UnmappedKey()
        offset = address + index * 12
        return self.rom, self.rom.data, offset, self.rom.read(offset, 12)

    @staticmethod
    def relocation_name(source, offset):
        return source.pointer(offset), 0

    def sample_source(self, source, offset, kind):
        return self.rom.sample_id(kind, source.pointer(offset))

    def mapping_key(self, address, addend, key):
        return self.rom.read(address + addend + key, 1)[0]
