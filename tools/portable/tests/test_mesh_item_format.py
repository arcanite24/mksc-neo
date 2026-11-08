"""Authored MIR1 SI wire validation and transactional mod-kit publication."""
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_asset_pack
import course_3d_format as course
import mesh_item_format as item
import mod_kit

ROOT = Path(__file__).resolve().parents[3]


def profile():
    return {
        'profile_id': 41, 'revision': 7,
        'actors': {'capacity': 64, 'radius': .25, 'lifetime_ticks': 600,
                   'owner_grace_ticks': 30, 'release_lock_ticks': 4,
                   'hold_distance': 1, 'hold_height': .25, 'orbit_radius': 1.5,
                   'orbit_turn_rate': .5, 'orbit_delay_0': 0, 'orbit_delay_1': 4, 'orbit_delay_2': 8},
        'shell': {'speed': 12, 'launch_bonus': 2, 'acceleration': 4, 'restitution': .75,
                  'max_bounces': 7, 'homing_turn_rate': .5, 'homing_range': 32,
                  'ground_probe': 1, 'ground_snap': .25, 'trap_arm_ticks': 10,
                  'trap_lifetime_ticks': 600, 'trap_lock_ticks': 10},
        'banana': {'throw_speed': 8, 'throw_up_speed': 4, 'gravity': 9.8},
        'hit': {'spin_ticks': 40, 'invulnerability_ticks': 60, 'speed_ratio': .25, 'impulse': 2},
        'mushroom': {'boost_ticks': 80, 'acceleration': 12, 'cap_ratio': 1.5, 'golden_ticks': 600},
        'star': {'player_ticks': 300, 'cpu_ticks': 200, 'acceleration': 16, 'cap_ratio': 1.5},
        'lightning': {'duration_ticks': 180, 'speed_ratio': .5, 'scale_ratio': .5},
        'boo': {'invisibility_ticks': 180, 'theft_lock_ticks': 30},
        'cpu': {'decision_ticks': 20, 'hold_ticks': 40, 'use_distance': 12, 'backward_distance': 8},
        'roulette': {'preview': [0x1000, 0x1001, 0x1002, 0x1003, 0x1004,
                                 0x1005, 0x1007, 0x1008, 0x1009, 0x100a],
                     'timeline': [2]*27},
        'weights': {'player': [[[0x1000, 3], [0x1007, 1]] for _ in range(8)],
                    'cpu': [[[0x1002, 2], [0x1005, 1]] for _ in range(8)]}}


class MeshItemFormatTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT/'.mksc-tools/portable/runs/t3'
        scratch.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=scratch, prefix='item-format-')
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def test_exact_wire_si_and_rank_tables(self):
        payload = item.normalize(profile())
        self.assertEqual(len(payload), 1396)
        self.assertEqual(struct.unpack_from('<8I', payload),
                         (0x3152494d, 1, 1396, 41, 7, 48, 8, 16))
        # Radius and shell speed have independently fixed wire positions.
        self.assertEqual(struct.unpack_from('<I', payload, 36)[0], 16384)
        self.assertEqual(struct.unpack_from('<I', payload, 80)[0], 12*65536)
        self.assertEqual(struct.unpack_from('<2H', payload, 372), (0x1000, 3))
        self.assertEqual(struct.unpack_from('<2H', payload, 884), (0x1002, 2))
        self.assertEqual(item.normalize(item.decode(payload)), payload)
        self.assertEqual(item.compile({'version': 1, 'profiles': [profile()]}),
                         [({'kind': 103, 'content_id': 41, 'version': 1, 'flags': 3}, payload)])
        self.assertEqual(item.normalize(profile()), payload)

    def test_unknown_nonfinite_boolean_ranges_and_cross_fields(self):
        bad = []
        for group, name, _, _, _ in item.FIELDS:
            value = profile()
            value[group][name] = True
            bad.append(value)
        for number in (float('nan'), float('inf'), -float('inf'), 10**1000, -1):
            value = profile(); value['actors']['radius'] = number; bad.append(value)
        for group, key, value in (('actors', 'capacity', 65), ('shell', 'ground_snap', 2),
                                  ('shell', 'trap_arm_ticks', 3), ('shell', 'trap_lock_ticks', 601),
                                  ('roulette', 'preview', [0x100d]*10),
                                  ('roulette', 'timeline', [0]*27)):
            record = profile(); record[group][key] = value; bad.append(record)
        record = profile(); record['future'] = 1; bad.append(record)
        record = profile(); record['shell']['future'] = 1; bad.append(record)
        for field, value in (('profile_id', 0), ('profile_id', 0xffffffff), ('revision', False)):
            record = profile(); record[field] = value; bad.append(record)
        for record in bad:
            with self.subTest(record=record), self.assertRaises(ValueError):
                item.normalize(record)

    def test_rank_weights_are_bounded_canonical_awards(self):
        for row in ([], [[0x100d, 1]], [[0x100e, 1]], [[0x100f, 1]], [[0x2000, 1]],
                    [[0x1000, 65535], [0x1001, 1]], [[0x1000, 1], [0x1000, 2]],
                    [[0x1000, True]], [[True, 1]], [[0x1000, 0]], [[0x1000, 1]]*17):
            record = profile(); record['weights']['cpu'][7] = row
            with self.subTest(row=row), self.assertRaises(ValueError):
                item.normalize(record)
        record = profile(); record['weights']['player'][0] = [[0x1006, 65535]]
        self.assertEqual(item.decode(item.normalize(record))['weights']['player'][0], [[0x1006, 65535]])
        for table in ([[]]*7, [[]]*9, True):
            record = profile(); record['weights']['player'] = table
            with self.assertRaises(ValueError): item.normalize(record)

    def test_collection_and_noncanonical_wire_reject(self):
        document = {'version': 1, 'profiles': [profile(), dict(profile(), profile_id=42)]}
        self.assertEqual([meta['content_id'] for meta, _ in item.compile(document)], [41, 42])
        for document in ({'version': True, 'profiles': [profile()]},
                         {'version': 1, 'profiles': []}, {'version': 1, 'profiles': [profile()]*257},
                         {'version': 1, 'profiles': [profile(), profile()]},
                         {'version': 1, 'profiles': [profile()], 'extra': 1}):
            with self.assertRaises(ValueError): item.compile(document)
        wire = item.normalize(profile())
        for size in (0, 1395, 1397):
            with self.assertRaises(ValueError):
                item.decode(wire[:size] if size <= len(wire) else wire+b'\0')
        for at, value in ((0, 0), (4, 2), (8, 0), (12, 0), (16, 0), (20, 49), (24, 9), (28, 17)):
            payload = bytearray(wire); struct.pack_into('<I', payload, at, value)
            with self.assertRaises(ValueError): item.decode(payload)

    def test_loader_duplicate_fields_and_limit(self):
        path = self.root/'items.json'
        for text in ('{"version":1,"version":1}', '{"value":NaN}', '{"value":Infinity}', '['*2000):
            path.write_text(text)
            with self.assertRaises(ValueError): item.load(path)
        path.write_bytes(b' '* (item.MAX_FILE+1))
        with self.assertRaisesRegex(ValueError, '1 MiB'): item.load(path)

    def test_course_item_reference_requires_typed_mir_asset(self):
        spec = importlib.util.spec_from_file_location(
            'item_bridge_fixture', ROOT/'mods/examples/bridge-course/build.py')
        example = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(example)
        source = example.source(self.root/'bridge-course')
        folder = source/'courses3d/bridge'
        document = course.load(folder/'course.json')
        properties = document['meta'].setdefault('properties', [])
        reference = {'key': 'mksc:item-rules', 'type': 'asset',
                     'value': {'kind': 103, 'id': 41}}
        properties.append(reference)
        # References may resolve from another mod in the final composed pack.
        self.assertTrue(course.compile(document, folder))
        for kind, value in (('integer', 41), ('asset', {'kind': 102, 'id': 41}),
                            ('asset', {'kind': True, 'id': 41})):
            reference.update(type=kind, value=value)
            with self.assertRaisesRegex(ValueError, 'MIR1'):
                course.compile(document, folder)

    def test_bridge_item_variant_is_explicit_and_builds_with_a_cup(self):
        spec = importlib.util.spec_from_file_location(
            'item_bridge_variant', ROOT/'mods/examples/bridge-course/build.py')
        example = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(example)
        plain = example.source(self.root/'plain')
        self.assertFalse((plain/'items.json').exists())
        authored = example.source(self.root/'item-cup', class_profiles=True, cup=True, items=True)
        document = json.loads((authored/'items.json').read_text())
        self.assertEqual(len(item.compile(document)), 1)
        all_awards = {entry[0] for row in document['profiles'][0]['weights']['player'] for entry in row}
        self.assertEqual(all_awards, set(range(0x1000, 0x100d)) - {0x1006})
        for folder in (authored/'courses3d').iterdir():
            track = json.loads((folder/'course.json').read_text())
            self.assertIn({'key':'mksc:item-rules','type':'asset','value':{'kind':103,'id':1}},
                          track['meta']['properties'])
            self.assertEqual(len(track['items']), 4)
            self.assertTrue(all(placement['respawn_ticks'] == 30 for placement in track['items']))
        showcase_source = example.source(self.root/'item-showcase', items=True)
        showcase_track = json.loads((showcase_source/'courses3d/bridge/course.json').read_text())
        self.assertEqual([{**placement, 'respawn_ticks': 30} for placement in showcase_track['items']],
                         track['items'])
        self.assertTrue(all(placement['respawn_ticks'] == 180 for placement in showcase_track['items']))
        folder, _ = mod_kit.build(authored, None, self.root/'mods')
        sections = list(build_asset_pack.read_sections(folder/'content.pack'))
        self.assertEqual(sum(meta['kind'] == 103 for meta, _ in sections), 1)
        self.assertEqual(sum(meta['kind'] == 100 for meta, _ in sections), 4)

    def test_bridge_cup_weights_balance_ranks_without_changing_showcase(self):
        spec = importlib.util.spec_from_file_location(
            'item_bridge_balance', ROOT/'mods/examples/bridge-course/build.py')
        example = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(example)
        showcase = example.item_profile()
        cup = example.item_profile(cup=True)
        awards = set(range(0x1000, 0x100d)) - {0x1006}
        for domain in ('player', 'cpu'):
            self.assertEqual(len(cup['weights'][domain]), 8)
            for row in showcase['weights'][domain]:
                self.assertEqual(dict(row), {identity: 1 for identity in awards})
            self.assertEqual({identity for row in cup['weights'][domain] for identity, _ in row}, awards)
            for rank, entries in enumerate(cup['weights'][domain]):
                row = dict(entries)
                self.assertEqual(sum(row.values()), 100)
                self.assertEqual(len(row), len(entries))
                self.assertTrue(all(weight > 0 for weight in row.values()))
                disruption = row.get(0x1004, 0) + row.get(0x100c, 0)
                if rank < 5:
                    self.assertEqual(disruption, 0)
                else:
                    self.assertLessEqual(disruption, 5)
            leader = dict(cup['weights'][domain][0])
            self.assertGreaterEqual(sum(leader.get(identity, 0) for identity in (0x1000, 0x1001, 0x1005)), 90)
            rear = dict(cup['weights'][domain][-1])
            self.assertGreaterEqual(sum(rear.get(identity, 0) for identity in (0x1007, 0x1008, 0x1009)), 50)
        self.assertEqual(cup['weights']['player'], cup['weights']['cpu'])
        # Cup policy changes only the authored odds: source-independent motion,
        # durations and the non-cup MIR payload remain exactly the same.
        cup['weights'] = showcase['weights']
        self.assertEqual(item.normalize(cup), item.normalize(showcase))
        independent = example.item_profile(cup=True)
        cpu_weight = independent['weights']['cpu'][0][0][1]
        independent['weights']['player'][0][0][1] += 1
        self.assertEqual(independent['weights']['cpu'][0][0][1], cpu_weight)

    def test_modkit_roundtrip_and_failed_build_keeps_prior_files(self):
        source = self.root/'items'; source.mkdir()
        manifest = {'id': 'items', 'name': 'Item SI rules', 'version': '1.0.0',
                    'api_version': 1, 'targets': ['2d', '3d']}
        (source/'mod.json').write_text(json.dumps(manifest))
        path = source/'items.json'
        path.write_text(json.dumps({'version': 1, 'profiles': [profile()]}))
        folder, archive = mod_kit.build(source, None, self.root/'mods')
        original, original_zip = (folder/'content.pack').read_bytes(), archive.read_bytes()
        sections = list(build_asset_pack.read_sections(folder/'content.pack'))
        self.assertEqual(len(sections), 1)
        self.assertEqual(sections[0][0]['kind'], 103)
        self.assertEqual(sections[0][1], item.normalize(profile()))
        for text in ('{', json.dumps({'version': 1, 'profiles': [dict(profile(), revision=False)]})):
            path.write_text(text)
            with self.assertRaises(mod_kit.KitError): mod_kit.build(source, None, self.root/'mods')
            self.assertEqual((folder/'content.pack').read_bytes(), original)
            self.assertEqual(archive.read_bytes(), original_zip)
        manifest['targets'] = ['2d']; (source/'mod.json').write_text(json.dumps(manifest))
        path.write_text(json.dumps({'version': 1, 'profiles': [profile()]}))
        with self.assertRaisesRegex(mod_kit.KitError, 'declare 3d'):
            mod_kit.build(source, None, self.root/'mods')
        self.assertEqual((folder/'content.pack').read_bytes(), original)
        self.assertEqual(archive.read_bytes(), original_zip)


if __name__ == '__main__':
    unittest.main()
