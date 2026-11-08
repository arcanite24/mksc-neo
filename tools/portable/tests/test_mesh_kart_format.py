"""Authored 3D movement profile wire, rejection and mod publication tests."""
import copy
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_asset_pack
import mesh_kart_format as profile
import mod_kit

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / 'portable/data/mesh-kart.json'


class MeshKartFormatTests(unittest.TestCase):
    def setUp(self):
        self.scratch = ROOT / '.mksc-tools/portable/runs/t1'
        self.scratch.mkdir(parents=True, exist_ok=True)

    def test_imported_default_header(self):
        header = ROOT / 'portable/import/nds/mkds_physics.h'
        expected = profile.c_header(profile.normalize(profile.load(SOURCE)), 'mkds_physics', SOURCE.name)
        self.assertEqual(header.read_text(), expected, 'regenerate with mesh_kart_format.py --c-header')

    def test_authored_default_wire(self):
        record = profile.load(SOURCE)
        data = profile.normalize(record)
        self.assertEqual(len(data), 884)
        self.assertEqual(struct.unpack_from('<8I', data),
                         (0x3148504b, 2, 884, 1, 10, 0, 0, 0))
        # speed_ground is range 19: 22.5 -> ~25.67 m/s, exactly 24 at base level 9.
        self.assertEqual(struct.unpack_from('<2i', data, 32 + 8*19), (1474560, 1682091))
        self.assertEqual(profile.decode(data), record)

    def test_resolved_stock_racer_and_item_envelope(self):
        record = profile.load(SOURCE)
        rules = profile.resolved(record)
        # Base level 9 of 19 at 150cc; the reverse target is MK8's 4 units/frame.
        self.assertEqual(rules['forward_speed_q16'], 24*65536)
        self.assertEqual(rules['reverse_speed_q16'], 824060)
        self.assertEqual((rules['neutral_charge_rate'], rules['inward_charge_rate']), (2, 5))
        self.assertEqual(rules['boost_ticks'], [30, 90, 150])
        self.assertLess(rules['charge_thresholds'][0], rules['charge_thresholds'][1])
        # Engine classes scale only the ground speed.
        slow = profile.resolved(record, 0)
        self.assertEqual(slow['forward_speed_q16'], rules['forward_speed_q16']*52429//65536)
        self.assertEqual({k: v for k, v in slow.items() if 'speed' not in k and 'deceleration' not in k},
                         {k: v for k, v in rules.items() if 'speed' not in k and 'deceleration' not in k})
        # Stock driver points move levels and clamp at 0..19.
        heavy = profile.levels(record, 8)
        self.assertGreater(heavy[profile.STATS.index('weight')], profile.levels(record)[0])
        self.assertEqual(profile.levels(record, 0, [-40*65536]*13), [0]*13)
        # The fastest boost is MK8's 1.2x mushroom/panel cap at 24 m/s.
        items = json.loads((SOURCE.parent/'mesh-items.json').read_text())['profiles']
        ratio = max(rules['panel_cap_ratio_q16']/65536,
                    *(v/65536 for v in rules['boost_cap_ratio_q16']),
                    *(p[k]['cap_ratio'] for p in items for k in ('mushroom','star')))
        self.assertAlmostEqual(rules['forward_speed_q16']/65536*ratio, 28.8, places=3)

    def test_profile_collection_and_legacy_identity(self):
        base = profile.load(SOURCE)
        def variant(identity, speed):
            record = copy.deepcopy(base)
            record['profile_id'] = identity
            record['ranges']['speed_ground'] = [speed*65536]*2
            return record
        variants = [variant(identity, speed) for identity, speed in ((50,12),(100,18),(150,24))]
        legacy = profile.compile(base)
        self.assertEqual(profile.compile({'version':1,'profiles':[base]}), legacy)
        sections = profile.compile({'version':1,'profiles':[base,*variants]})
        self.assertEqual(sections[0], legacy[0])
        self.assertEqual([meta['content_id'] for meta, _ in sections], [1,50,100,150])
        self.assertEqual([profile.decode(data)['ranges']['speed_ground'][0] for _, data in sections[1:]],
                         [12*65536,18*65536,24*65536])
        for document in ({'version':1,'profiles':[]}, {'version':1,'profiles':[base]*257},
                         {'version':1,'profiles':[base,base]}, {'version':2,'profiles':[base]},
                         {'version':1,'profiles':[base],'extra':0},
                         {'version':1,'profiles':[base,dict(base,profile_id=0)]}):
            with self.subTest(document=document), self.assertRaises(ValueError):
                profile.compile(document)

    def test_bridge_class_profiles_are_opt_in(self):
        spec = importlib.util.spec_from_file_location('bridge_class_example',
            ROOT/'mods/examples/bridge-course/build.py')
        example = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(example)
        with tempfile.TemporaryDirectory(dir=self.scratch) as work:
            root = Path(work)
            default = example.source(root/'default')
            selected = example.source(root/'classes', class_profiles=True)
            self.assertEqual((default/'physics.json').read_bytes(), SOURCE.read_bytes())
            default_course = json.loads((default/'courses3d/bridge/course.json').read_text())
            selected_course = json.loads((selected/'courses3d/bridge/course.json').read_text())
            self.assertFalse(any(p['key'].startswith('mksc:physics-')
                                 for p in default_course['meta']['properties']))
            mappings = {p['key']:p['value'] for p in selected_course['meta']['properties']
                        if p['key'].startswith('mksc:physics-')}
            self.assertEqual(mappings,{f'mksc:physics-{c}cc':{'kind':102,'id':c}
                                       for c in (50,100,150)})
            sections = profile.compile(profile.load(selected/'physics.json'))
            self.assertEqual(sections[0],profile.compile(profile.load(SOURCE))[0])
            self.assertEqual([profile.resolved(profile.decode(data), c)['forward_speed_q16']//65536
                              for c, (_, data) in zip((2,0,1,2), sections)],
                             [profile.resolved(profile.load(SOURCE))['forward_speed_q16']//65536,12,18,24])

    def test_bridge_cup_compiles_full_ids_and_distinct_texture_bindings(self):
        spec = importlib.util.spec_from_file_location('bridge_cup_example',
            ROOT/'mods/examples/bridge-course/build.py')
        example = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(example)
        with tempfile.TemporaryDirectory(dir=self.scratch) as work:
            root = Path(work)
            authored = example.source(root/'cup',course_id=0xfffffffc,
                                      class_profiles=True,cup=True)
            rows = [json.loads(path.read_text()) for path in sorted(
                (authored/'courses3d').glob('*/course.json'))]
            rows.sort(key=lambda row:row['meta']['cup_order'])
            self.assertEqual([row['id'] for row in rows],list(range(0xfffffffc,0x100000000)))
            self.assertEqual([row['meta']['cup_order'] for row in rows],[0,1,2,3])
            self.assertEqual(len({row['textures'][0]['id'] for row in rows}),4)
            self.assertTrue(all(row['routes'][0]['properties'][0]['value']==1.5 for row in rows))
            folder,_ = mod_kit.build(authored,None,root/'mods')
            sections = list(build_asset_pack.read_sections(folder/'content.pack'))
            for kind in (99,100):
                self.assertEqual(sorted(meta['content_id'] for meta,_ in sections
                                        if meta['kind']==kind),list(range(0xfffffffc,0x100000000)))
            self.assertEqual(sorted(meta['content_id'] for meta,_ in sections
                                    if meta['kind']==101),[65001,65002,65003,65004])
            for invalid in (0,True,0xfffffffd):
                with self.assertRaisesRegex(ValueError,'uint32'):
                    example.source(root/'invalid',course_id=invalid,cup=True)

    def test_all_field_bounds_and_wrong_types(self):
        record = profile.load(SOURCE)
        for name, _, low, high in profile.RANGES:
            for value in (low-1, high+1, True, 1.0, None):
                edited = copy.deepcopy(record)
                edited['ranges'][name] = [value, value]
                with self.subTest(range=name, value=value), self.assertRaises(ValueError):
                    profile.normalize(edited)
        for name, low, high in profile.SCALARS:
            for value in (low-1, high+1, True, 1.0, None):
                with self.subTest(field=name, value=value), self.assertRaises(ValueError):
                    profile.normalize(dict(record, **{name: value}))
        for change in ({'base_levels_q16': dict(record['base_levels_q16'], weight=20*65536)},
                       {'base_levels_q16': {'weight': 0}},
                       {'driver_points_q16': dict(record['driver_points_q16'], **{'9': {}})},
                       {'driver_points_q16': dict(record['driver_points_q16'], **{'1': {'speed': 1}})},
                       {'class_speed_ratio_q16': [65536]*2},
                       {'class_speed_ratio_q16': [0, 65536, 65536]},
                       {'cpu_speed_ratio_q16': [65536]*2},
                       {'cpu_speed_ratio_q16': [65536, 65536, 65537]}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                profile.normalize(dict(record, **change))
        for edited in (dict(record, unknown=0), {k:v for k,v in record.items() if k!='ranges'}, [], None):
            with self.assertRaises(ValueError):
                profile.normalize(edited)
        for field in ('version', 'profile_id', 'revision'):
            with self.assertRaises(ValueError):
                profile.normalize(dict(record, **{field: 0}))
        with tempfile.TemporaryDirectory(dir=self.scratch) as work:
            path = Path(work)/'physics.json'
            path.write_text('{"version":1,"version":1}')
            with self.assertRaisesRegex(ValueError, 'duplicate'):
                profile.load(path)

    def test_support_reserve_is_independent_of_body_clearance(self):
        record=profile.load(SOURCE)
        record.update(ground_snap_q16=655,ground_probe_q16=6554,
                      terminal_fall_speed_q16=36*65536)
        self.assertGreater(record['kart_radius_q16']+record['ride_height_q16'],6554)
        self.assertEqual(profile.decode(profile.normalize(record)),record)

    def test_cross_field_validation_and_wire_rejection(self):
        record = profile.load(SOURCE)
        def ranges(**changes):
            edited = copy.deepcopy(record)
            edited['ranges'].update(changes)
            return edited
        edits = (ranges(charge_2=record['ranges']['charge_1']),
                 ranges(mt2_ticks=record['ranges']['mt1_ticks']),
                 ranges(speed_ground=[128*65536]*2, mt1_cap=[4*65536]*2),
                 dict(record, ground_probe_q16=0),
                 dict(record, ground_probe_q16=record['ground_snap_q16']-1),
                 dict(record, ground_snap_q16=3*65536))
        for edited in edits:
            with self.assertRaises(ValueError):
                profile.normalize(edited)
        data = profile.normalize(record)
        for offset in (0,4,8,12,16,20,24,28):
            edited = bytearray(data)
            struct.pack_into('<I', edited, offset, 0 if offset < 20 else 1)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                profile.decode(edited)
        for size in (0,883,885):
            with self.assertRaises(ValueError):
                profile.decode(data[:size] if size < 884 else data+b'\0')

    def test_mod_pack_and_failed_build_are_atomic(self):
        with tempfile.TemporaryDirectory(dir=self.scratch) as work:
            root = Path(work)
            source = root/'tuning'
            source.mkdir()
            manifest = {'id':'tuning','name':'Authored tuning','version':'1.0.0',
                        'api_version':1,'targets':['3d']}
            (source/'mod.json').write_text(json.dumps(manifest))
            (source/'physics.json').write_bytes(SOURCE.read_bytes())
            folder, archive = mod_kit.build(source, None, root/'mods')
            original = (folder/'content.pack').read_bytes()
            old_zip = archive.read_bytes()
            sections = list(build_asset_pack.read_sections(folder/'content.pack'))
            self.assertEqual(len(sections), 1)
            self.assertEqual(sections[0][0]['kind'], 102)
            self.assertEqual(sections[0][0]['content_id'], 1)
            self.assertEqual(sections[0][1], profile.normalize(profile.load(SOURCE)))
            collection = {'version':1,'profiles':[profile.load(SOURCE),
                dict(profile.load(SOURCE),profile_id=50,hop_velocity_q16=3*65536)]}
            (source/'physics.json').write_text(json.dumps(collection))
            folder, archive = mod_kit.build(source,None,root/'mods')
            sections = list(build_asset_pack.read_sections(folder/'content.pack'))
            self.assertEqual([meta['content_id'] for meta,_ in sections], [1,50])
            original = (folder/'content.pack').read_bytes()
            old_zip = archive.read_bytes()
            (source/'physics.json').write_text(json.dumps({'version':1,'profiles':collection['profiles']*2}))
            with self.assertRaisesRegex(mod_kit.KitError,'duplicate profile_id'):
                mod_kit.build(source,None,root/'mods')
            self.assertEqual((folder/'content.pack').read_bytes(),original)
            self.assertEqual(archive.read_bytes(),old_zip)
            edited = dict(profile.load(SOURCE), respawn_ticks=0)
            (source/'physics.json').write_text(json.dumps(edited))
            with self.assertRaisesRegex(mod_kit.KitError, 'physics.json.*respawn_ticks'):
                mod_kit.build(source, None, root/'mods')
            self.assertEqual((folder/'content.pack').read_bytes(), original)
            self.assertEqual(archive.read_bytes(), old_zip)
            manifest['targets'] = ['2d']
            (source/'mod.json').write_text(json.dumps(manifest))
            with self.assertRaisesRegex(mod_kit.KitError, 'targets must declare 3d'):
                mod_kit.build(source, None, root/'mods')


if __name__ == '__main__':
    unittest.main()
