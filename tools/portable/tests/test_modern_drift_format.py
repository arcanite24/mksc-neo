"""Authored drift content has one strict wire representation and source owner."""
import copy
import json
from pathlib import Path
import re
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_asset_pack
import modern_drift_format as drift

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / 'portable/data/modern-drift.json'


class ModernDriftFormatTests(unittest.TestCase):
    def test_authored_default_wire_and_round_trip(self):
        record = drift.load(SOURCE)
        payload = drift.normalize(record)
        self.assertEqual(payload, struct.pack(
            '<III3H3H3HH3iI', 0x3152444D, 0x4D440001, 1,
            30, 90, 180, 20, 40, 60, 288, 304, 320, 128,
            3 * 65536, 4 * 65536, 5 * 65536, 0))
        self.assertEqual(drift.decode(payload), record)

    def test_unknown_missing_duplicate_and_wrong_types(self):
        record = drift.load(SOURCE)
        for edited in (dict(record, typo=1), {key: value for key, value in record.items()
                                            if key != 'revision'}, [], None):
            with self.assertRaises(ValueError):
                drift.normalize(edited)
        for field in ('version', 'profile_id', 'revision', 'minimum_speed_ratio_256'):
            with self.subTest(field=field), self.assertRaises(ValueError):
                drift.normalize(dict(record, **{field: True}))
        for field in ('charge_ticks', 'boost_ticks', 'cap_ratio_256', 'acceleration_q16'):
            for value in ([1, 2], [1, 2, 3, 4], '123', [True, 90, 180]):
                with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                    drift.normalize(dict(record, **{field: value}))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'duplicate.json'
            path.write_text('{"version":1,"version":1}')
            with self.assertRaisesRegex(ValueError, 'duplicate'):
                drift.load(path)
            path.write_text(' ' * 16385)
            with self.assertRaisesRegex(ValueError, '16384'):
                drift.load(path)

    def test_bounds_and_strict_charge_order(self):
        record = drift.load(SOURCE)
        bounds = {'profile_id': (1, 0xFFFFFFFF), 'revision': (1, 0xFFFFFFFF),
                  'minimum_speed_ratio_256': (1, 256), 'boost_ticks': (1, 8192),
                  'cap_ratio_256': (256, 1024), 'acceleration_q16': (1, 8388608)}
        for field, (low, high) in bounds.items():
            for value in (low - 1, high + 1):
                edited = copy.deepcopy(record)
                edited[field] = [value] * 3 if isinstance(edited[field], list) else value
                with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                    drift.normalize(edited)
            for value in (low, high):
                edited = copy.deepcopy(record)
                edited[field] = [value] * 3 if isinstance(edited[field], list) else value
                self.assertEqual(drift.decode(drift.normalize(edited)), edited)
        for values in ([1, 1, 3], [3, 2, 1], [0, 1, 2], [1, 2, 8193]):
            with self.assertRaises(ValueError):
                drift.normalize(dict(record, charge_ticks=values))
        self.assertEqual(drift.decode(drift.normalize(dict(record, charge_ticks=[1, 2, 8192])))
                         ['charge_ticks'], [1, 2, 8192])

    def test_wire_rejects_signed_acceleration_and_reserved_bytes(self):
        payload = drift.normalize(drift.load(SOURCE))
        for offset, value in ((0, 0), (44, 1), (32, 0xFFFFFFFF), (4, 0)):
            edited = bytearray(payload)
            struct.pack_into('<I', edited, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                drift.decode(edited)
        for edited in (payload[:-1], payload + b'\0'):
            with self.assertRaises(ValueError):
                drift.decode(edited)

    def test_compiler_header_and_mod_section_are_same_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary, header = root / 'generated/profile.bin', root / 'generated/profile.h'
            self.assertEqual(drift.main([str(SOURCE), '--binary', str(binary),
                                         '--header', str(header)]), 0)
            payload = binary.read_bytes()
            header_bytes = bytes(int(value, 16) for value in re.findall(r'0x([0-9a-f]{2})',
                                                                        header.read_text()))
            self.assertEqual(header_bytes, payload)
            manifest = root / 'sections.json'
            manifest.write_text(json.dumps({'sections': [{
                'kind': drift.KIND, 'content_id': 1, 'version': drift.VERSION,
                'flags': 2, 'path': 'generated/profile.bin',
            }]}))
            pack = root / 'content.pack'
            build_asset_pack.build_pack(manifest, pack)
            rows = list(build_asset_pack.read_sections(pack))
            self.assertEqual(len(rows), 1)
            self.assertEqual((rows[0][0]['kind'], rows[0][0]['content_id'], rows[0][0]['flags']),
                             (95, 1, 2))
            self.assertEqual(rows[0][1], payload)
            custom = dict(drift.load(SOURCE), boost_ticks=[21, 41, 61], revision=2)
            self.assertNotEqual(drift.normalize(custom), payload)
            self.assertEqual(drift.decode(drift.normalize(custom)), custom)

    def test_air_v2_tail_and_legacy_prefix_are_explicit(self):
        record = drift.load(ROOT / 'portable/data/modern-air.json')
        payload = drift.normalize(record)
        self.assertEqual(len(payload), 64)
        self.assertEqual(struct.unpack_from('<HHHHiHBB', payload, 48),
                         (2, 45, 30, 304, 262144, 128, 3, 0))
        original = dict(record, version=1)
        del original['air']
        self.assertEqual(payload[:48], drift.normalize(original))
        self.assertEqual(drift.decode(payload), record)
        self.assertIn('portable_modern_air_payload[64]', drift.c_header(payload))
        self.assertIn('portable_modern_drift_payload[48]', drift.c_header(payload[:48]))
        for offset, value in ((44, 1), (63, 1), (62, 0), (62, 4)):
            bad = bytearray(payload)
            bad[offset] = value
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                drift.decode(bad)
        for size in (49, 63, 65):
            bad = payload[:size] if size < 64 else payload + b'\0'
            with self.assertRaises(ValueError):
                drift.decode(bad)

    def test_air_fields_bounds_and_disabled_v2_rejected(self):
        record = drift.load(ROOT / 'portable/data/modern-air.json')
        for air in ({}, dict(record['air'], unknown=1), None):
            with self.assertRaises(ValueError):
                drift.normalize(dict(record, air=air))
        for field, low, high in (
                ('minimum_air_ticks', 1, 8192), ('trick_window_ticks', 2, 8192),
                ('boost_ticks', 1, 8192), ('cap_ratio_256', 256, 1024),
                ('acceleration_q16', 1, 8388608), ('minimum_speed_ratio_256', 1, 256),
                ('launch_mask', 1, 3)):
            for value in (low - 1, high + 1, True):
                bad = copy.deepcopy(record)
                bad['air'][field] = value
                with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                    drift.normalize(bad)
        bad = copy.deepcopy(record)
        bad['air']['minimum_air_ticks'] = 46
        with self.assertRaisesRegex(ValueError, 'trick_window_ticks'):
            drift.normalize(bad)
        for key in record['air']:
            bad = copy.deepcopy(record)
            del bad['air'][key]
            with self.assertRaises(ValueError):
                drift.normalize(bad)
        for version in (0, 3, True):
            with self.assertRaises(ValueError):
                drift.normalize(dict(record, version=version))
        with self.assertRaises(ValueError):
            drift.normalize(dict(drift.load(SOURCE), air=record['air']))

    def test_mod_kit_emits_v2_and_header_preserves_all_bytes(self):
        import mod_kit
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'air-profile'
            source.mkdir()
            record = drift.load(ROOT / 'portable/data/modern-air.json')
            (source / 'modern-drift.json').write_text(json.dumps(record))
            (source / 'mod.json').write_text(json.dumps(dict(
                id='air-profile', name='Authored Air', version='1.0.0', api_version=1)))
            folder, _ = mod_kit.build(source, None, root / 'mods')
            rows = list(build_asset_pack.read_sections(folder / 'content.pack'))
            self.assertEqual(len(rows), 1)
            self.assertEqual((rows[0][0]['kind'], rows[0][0]['version']), (95, 2))
            payload = drift.normalize(record)
            self.assertEqual(rows[0][1], payload)
            header_bytes = bytes(int(value, 16) for value in re.findall(
                r'0x([0-9a-f]{2})', drift.c_header(payload)))
            self.assertEqual(header_bytes, payload)


if __name__ == '__main__':
    unittest.main()
