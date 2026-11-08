import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parents[1]))
from course_rom import Data, surface_sound_rules
from audio_rom import RomAudio, ROM_BASE, SONG_TABLE, SONG_COUNT
from export_audio_sequence import ExportError, NOTE, LOOP


def fixture():
    data = bytearray(0x400000)
    data[0xAC:0xB0] = b"AMKE"
    # All unused entries point to a zero-track dummy header.
    for song in range(SONG_COUNT):
        struct.pack_into("<I", data, SONG_TABLE + song * 8, ROM_BASE + 0x2000)
    struct.pack_into("<I", data, SONG_TABLE + 8, ROM_BASE + 0x2100)
    struct.pack_into("<BBBBII", data, 0x2100, 1, 0, 10, 0,
                     ROM_BASE + 0x3000, ROM_BASE + 0x2200)
    # Start a PCM voice, play a note, wait, and loop back to the note.
    track = bytes([0xBB, 60, 0xBD, 0, 0xBE, 100, 0xD3, 60, 100,
                   0x84, 0xB2]) + struct.pack("<I", ROM_BASE + 0x2206)
    data[0x2200:0x2200 + len(track)] = track
    for index in range(128):
        struct.pack_into("<BBBBIBBBB", data, 0x3000 + index * 12,
                         1, 60, 0, 0, 2, 0, 0, 15, 0)
    struct.pack_into("<BBBBIBBBB", data, 0x3000,
                     0, 60, 0, 0, ROM_BASE + 0x5000, 255, 0, 255, 0)
    struct.pack_into("<HHIII", data, 0x5000, 0, 0, 8000 * 1024, 0, 3)
    data[0x5010:0x5014] = b"\x00\x7f\x00\x80"
    struct.pack_into("<32H", data, 0xECC08, *range(200,232))
    struct.pack_into("<32H", data, 0xECBC8, *range(232,264))
    struct.pack_into("<16H",data,0xECC88,*range(270,286))
    struct.pack_into("<16H",data,0xECC68,*range(300,316))
    for index, address in enumerate((0xEE5E4, 0xEE604, 0xEE624)):
        struct.pack_into("<8I", data, address, 0x81, 108, 0xF1, 110, 0x85, 5 + index, 0, 0)
    struct.pack_into("<6I", data, 0xEE644, 0xF1, 110, 0x85, 7, 0, 0)
    struct.pack_into("<H", data, 0x4C558, 0x2021)
    struct.pack_into("<H", data, 0x4C560, 0x2022)
    struct.pack_into("<H", data, 0x3B950, 0x206B)
    struct.pack_into("<8I", data, 0xEC790, 0xF4, 0x803DDCD, 0xF1, 172, 0xF4, 0x803DDD9, 0, 0)
    struct.pack_into("<12I",data,0xF3314,*(0x40000000+i*100000 for i in range(12)))
    struct.pack_into("<12h",data,0xF33E0,*(-1800+i*50 for i in range(12)))
    data[0xF33F8:0xF33F8+60]=bytes(i%8 for i in range(60))
    data[0xF3444:0xF3444+49]=bytes(range(49))
    data[0xD92BC:0xD92C4]=bytes((8,0,7,1,6,2,5,3))
    struct.pack_into('<9H',data,0xD92C4,*(0 if i==4 else 100+i for i in range(9)))
    struct.pack_into('<16H',data,0xD92FA,*(v for i in range(8) for v in (140+i,300+i)))
    struct.pack_into('<16H',data,0xD95FC,*(v for i in range(8) for v in (160+i,400+i)))
    struct.pack_into('<9H',data,0xD92E8,*(0 if i==4 else 200+i for i in range(9)))
    struct.pack_into('<8H',data,0xECB78,*range(220,228))
    struct.pack_into('<8I',data,0xDBC48,*range(240,248))
    return data


class RomAudioTests(unittest.TestCase):
    def test_sequence_player_comes_from_song_metadata(self):
        data=fixture()
        struct.pack_into('<H',data,SONG_TABLE+8+4,9)
        sequence=next(payload for kind,_,payload in RomAudio(data).sections() if kind==16)
        self.assertEqual(struct.unpack_from('<H',sequence,10)[0],2)
        self.assertEqual(sequence[17],9)
        old=bytearray(sequence);old[10]=1;old[17]=0
        from export_audio_sequence import export_sequence
        audio=RomAudio(data)
        self.assertEqual(old,export_sequence(data,0x2100,audio.pointer,1,1,
            clocks=data[0xF3444:0xF3444+49]))
        struct.pack_into('<H',data,SONG_TABLE+8+4,11)
        with self.assertRaisesRegex(ExportError,'sequence player'):
            RomAudio(data).sections()

    def test_finish_sound_scripts(self):
        data = fixture()
        payload = RomAudio(data).finish_sound_rules()
        self.assertEqual(len(payload), 40)
        self.assertEqual(struct.unpack_from("<4sHH", payload), (b"FIN1", 2, 4))
        self.assertEqual(struct.unpack_from("<14H", payload, 8),
                         (108, 110, 5, 108, 110, 6, 108, 110, 7, 0, 110, 7, 33, 34))
        self.assertEqual(struct.unpack_from("<2H", payload, 36), (107, 172))
        struct.pack_into("<I", data, 0xEE5F4, 0x84)
        with self.assertRaisesRegex(ExportError, "finish sound script"):
            RomAudio(data).finish_sound_rules()
        data = fixture()
        struct.pack_into("<H", data, 0x4C558, 0)
        with self.assertRaisesRegex(ExportError, "Battle finish"):
            RomAudio(data).finish_sound_rules()

    def test_invalid_lap_sound_script(self):
        for offset, value, message in ((0x3B950, 0, "lap sound instruction"),
                                      (0xEC79C, 0, "final lap sound script"),
                                      (0xEC7A4, 0, "final lap sound script")):
            data = fixture()
            struct.pack_into("<I", data, offset, value)
            with self.assertRaisesRegex(ExportError, message):
                RomAudio(data).finish_sound_rules()

    def test_shared_sequence_and_sample_formats(self):
        first = RomAudio(fixture()).sections()
        self.assertEqual(first, RomAudio(fixture()).sections())
        self.assertEqual([(k, i) for k, i, _ in first], [(16, 1), (17, 1), (18, 1), (68, 1), (69, 1), (71, 1), (72, 1)])
        sequence, bank, sample = [p for k, _, p in first if k < 68]
        count = struct.unpack_from("<I", sequence, 20)[0]
        events = [struct.unpack_from("<IIHBBBBH", sequence, 32 + i * 16)
                  for i in range(count)]
        self.assertTrue(any(e[4] == NOTE and e[5:7] == (60, 100) for e in events))
        self.assertEqual(events[-1][4], LOOP)
        self.assertEqual(events[events[-1][1]][4], NOTE)
        self.assertEqual(struct.unpack_from("<H", bank, 14)[0], 128)
        self.assertEqual(sample[40:], b"\x00\x7f\x00\x80")

    def test_surface_tables_normalize_course_theme_and_restart_spans(self):
        data = fixture()
        base = (70, 76, 74, 71, 71, 71, 77, 75, 81, 71, 71, 77, 75, 72, 137, 137)
        struct.pack_into('<16H', data, 0xEDD40, *base)
        patches = {12: {6: 80, 7: 71, 8: 82}, 6: {1: 77},
                   10: {1: 71}, 0: {0: 76, 7: 76}, 18: {8: 83}, 16: {10: 84}, 7: {}}
        for theme, changes in patches.items():
            payload = surface_sound_rules(Data(data), theme)
            expected = list(base)
            for index, song in changes.items():
                expected[index] = song
            self.assertEqual(len(payload), 60)
            self.assertEqual(struct.unpack_from('<4sHH16H', payload),
                             (b'SRF1', 2, 16, *expected))
            self.assertEqual(tuple(payload[56:]), (theme, 0, 0, 0))
            self.assertEqual(tuple(payload[40:56]), tuple(
                6 if song in (81, 137) else 4 if song in (82, 83) else 0
                for song in expected))

    def test_hazard_voices_preserve_listener_and_size_order(self):
        data = fixture()
        payload = RomAudio(data).hazard_sound_rules()
        self.assertEqual(struct.unpack('<4s4H96H',payload[:204]),
                         (b'HAZ1',4,8,2,2,*range(200,264),*range(270,286),*range(300,316)))
        struct.pack_into('<H',data,0xECBC8+62,403)
        with self.assertRaisesRegex(ExportError,'hazard voice'):
            RomAudio(data).hazard_sound_rules()

    def test_character_audio_import_preserves_independent_maps_and_volumes(self):
        data=fixture();payload=RomAudio(data).hazard_sound_rules()
        self.assertEqual(len(payload),334)
        cue=struct.unpack('<65H',payload[204:])
        self.assertEqual(cue[:8],(108,100,107,101,106,102,105,103))
        self.assertEqual(cue[8:24],tuple(v for i in range(8) for v in (140+i,300+i)))
        self.assertEqual(cue[24:40],tuple(v for i in range(8) for v in (160+i,400+i)))
        self.assertEqual(cue[40:49],tuple(0 if i==4 else 200+i for i in range(9)))
        self.assertEqual(cue[49:57],tuple(range(220,228)))
        self.assertEqual(cue[57:],tuple(range(240,248)))
        for offset,value,width in ((0xD92BC,9,1),(0xD92BD,8,1),(0xD92C4+16,0,2),
                (0xD92FC,1024,2),(0xD95FC,403,2),(0xD92E8,403,2),(0xDBC48,0x100f0,4)):
            changed=bytearray(data)
            struct.pack_into({1:'<B',2:'<H',4:'<I'}[width],changed,offset,value)
            with self.subTest(offset=offset),self.assertRaisesRegex(ExportError,'character audio'):
                RomAudio(changed).hazard_sound_rules()

    def test_player_priority_flags_decode_without_pointers(self):
        data = fixture()
        flags = (0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0)
        for player, flag in enumerate(flags):
            struct.pack_into("<II BB H", data, 0x102414 + player * 12,
                             0xDEADBEEF, 0xABCDEF00, 2, 255, flag)
        payload = RomAudio(data).player_rules()
        self.assertEqual(struct.unpack("<8sHH11B", payload[:23]),
                         (b"MKSCPRI1", 2, 11, *flags))
        self.assertEqual(payload[23:71],data[0xF3314:0xF3344])
        self.assertEqual(payload[71:95],data[0xF33E0:0xF33F8])
        self.assertEqual(payload[95:],data[0xF33F8:0xF3434])
        data[0x102414 + 10] = 2
        with self.assertRaises(ExportError):
            RomAudio(data).player_rules()

    def test_engine_rules_decode_rom_rows_and_skip_volume_padding(self):
        data = fixture()
        profiles = (2048, 16, -128, -48, -32, 0,
                    2560, 20, -160, -60, -40, 0,
                    3072, 24, -192, -72, -48, 0)
        struct.pack_into("<18h", data, 0xEC94C, *profiles)
        for index in range(128):
            struct.pack_into("<HH", data, 0xEC970 + index * 4, 256-index, 0xABCD)
        struct.pack_into("<32H", data, 0xECB88, *range(85, 117))
        struct.pack_into("<4H", data, 0xECB70, 180, 179, 178, 177)
        payload = next(p for k, _, p in RomAudio(data).sections() if k == 68)
        self.assertEqual(len(payload), 380)
        self.assertEqual(struct.unpack_from("<4s6H", payload),
                         (b"ENG1", 3, 8, 16, 4, 6, 4))
        self.assertEqual(struct.unpack_from("<18h", payload, 16), profiles)
        self.assertEqual(struct.unpack_from("<128H", payload, 52),
                         tuple(range(256, 128, -1)))
        self.assertEqual(struct.unpack_from("<32H", payload, 308), tuple(range(85, 117)))
        self.assertEqual(struct.unpack_from("<4H", payload, 372), (180, 179, 178, 177))

    def test_drum_child_in_last_root_slot_is_discovered(self):
        data = fixture()
        struct.pack_into("<BBBBIBBBB", data, 0x3000 + 127 * 12,
                         128, 0, 0, 0, ROM_BASE + 0x4000, 0, 0, 0, 0)
        for key in range(128):
            struct.pack_into("<BBBBIBBBB", data, 0x4000 + key * 12,
                             1, key, 0, 0, 2, 0, 0, 15, 0)
        bank = next(p for k, _, p in RomAudio(data).sections() if k == 17)
        self.assertEqual(struct.unpack_from("<I", bank, 16)[0], 128)

    def test_invalid_source_pointer_fails_with_address(self):
        data = fixture()
        struct.pack_into("<I", data, 0x3004, 0x02000000)
        with self.assertRaisesRegex(ExportError, "invalid ROM audio pointer.*3004"):
            RomAudio(data)

    def test_pcm_size_cannot_read_past_rom(self):
        data = fixture()
        struct.pack_into("<I", data, 0x500C, 0x400000)
        with self.assertRaisesRegex(ExportError, "outside ROM"):
            RomAudio(data).sections()

    def test_wrong_region_or_truncated_rom(self):
        data = fixture()
        data[0xAC:0xB0] = b"AMKP"
        with self.assertRaisesRegex(ExportError, "US revision 0"):
            RomAudio(data)
        with self.assertRaises(ExportError):
            RomAudio(bytes(40))


if __name__ == "__main__":
    unittest.main()
