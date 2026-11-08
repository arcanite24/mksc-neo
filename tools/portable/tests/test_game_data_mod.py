"""Exercise JSON authoring, preserved source metadata and rejected partial mods."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_asset_pack
import game_data_mod
import make_game_data_fixture

ROOT = Path(__file__).resolve().parents[3]
MANIFEST = ROOT / 'mods/examples/data-tuning/mod.json'


class GameDataModTests(unittest.TestCase):
    def fixture(self, root):
        return make_game_data_fixture.fixture(root)

    def test_motion_v7_v8_overrides_preserve_all_imported_rules(self):
        import kart_physics_mod
        import mod_kit
        with tempfile.TemporaryDirectory() as directory:
            original, _, _ = self.fixture(Path(directory))
            tail = struct.pack('<8h', -3, -7, -13, -21, -29, -43, -59, -79)
            v6, v7 = bytearray(original), bytearray(original)+tail
            response=bytes((index*7+13)%256 for index in range(412))
            v8=bytearray(v7)+response
            for version, data in ((6, v6), (7, v7), (8, v8)):
                game_data_mod.motion(data, version, {'150': 75})
                kart_physics_mod.apply(data, {'id': 1, 'time_trial': {'base_speed_q16': 123456}})
            self.assertEqual(v7[:-16], v6)
            self.assertEqual(v8[:-412],v7)
            self.assertEqual(v8[-412:],response)
            self.assertEqual(v7[-16:], tail)
            self.assertEqual(struct.unpack_from('<i', v7, 38108+2*452+416)[0], 825*65536)
            self.assertEqual(v7[38108+2*452+128], 90)
            tables = mod_kit.base_tables({(13, 1): ({'version': 7}, bytes(v7))})
            self.assertEqual(tables['kart_physics-1']['kart_physics'][0]['time_trial']['base_speed_q16'], 123456)
            for malformed in (v7[:-1], v7+bytes(1)):
                with self.assertRaisesRegex(ValueError, 'motion v6/v7'):
                    game_data_mod.motion(malformed, 7, {})
            bad = bytearray(v7);struct.pack_into('<h', bad, len(bad)-2, 1)
            with self.assertRaisesRegex(ValueError, 'deceleration'):
                game_data_mod.motion(bad, 7, {})

    def test_shell_orbits_layer_geometry_and_material_delays(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root);source=root/'orbit.json'
            source.write_text(json.dumps({'shell_orbit_rules':{'id':1,'radius_multiplier':22,'green_delays':[1,2,3]}}))
            game_data_mod.build(root/'base.pack',source,root/'good',MANIFEST)
            self.assertEqual(struct.unpack('<5I',(root/'good/67-1.bin').read_bytes()),(0x3142524f,3,22,1280,8))
            original=(root/'6.bin').read_bytes()
            for course in (1,27):
                material=(root/f'good/6-{course}.bin').read_bytes()
                self.assertEqual(material[1464:1470],bytes([1,2,3])+original[1467:1470])
            for rules in ({'id':1},{'id':2,'angle_step':1},{'id':1,'radius_multiplier':0},
                          {'id':1,'radius_multiplier':256},{'id':1,'angle_step':65536},
                          {'id':1,'animation_period':0},{'id':1,'green_delays':[0,1]},
                          {'id':1,'red_delays':[0,1,2,3]},{'id':1,'red_delays':[0,1,256]}):
                source.write_text(json.dumps({'shell_orbit_rules':rules}))
                with self.assertRaisesRegex(ValueError,'shell_orbit_rules'):
                    game_data_mod.build(root/'base.pack',source,root/'bad',MANIFEST)
                self.assertFalse((root/'bad').exists())

    def test_ui_strings_use_stable_ids_and_reject_overflow(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root);source=root/'strings.json'
            source.write_text(json.dumps({'strings':[{'id':88,'value':'SETUP'}]}))
            game_data_mod.build(root/'base.pack',source,root/'good',MANIFEST)
            self.assertEqual((root/'good/66-88.bin').read_bytes(),b'UIT1'+struct.pack('<I',5)+b'SETUP\0')
            for records in ([{'id':0,'value':'A'}],[{'id':999,'value':'A'}],
                            [{'id':88,'value':'TOO LONG'}],[{'id':88,'value':'lower'}],
                            [{'id':88,'value':'%s'}],[{'id':88,'value':''}],
                            [{'id':88,'value':'A','color_rgb':0}],
                            [{'id':88,'value':'A'},{'id':88,'value':'B'}]):
                source.write_text(json.dumps({'strings':records}))
                with self.assertRaisesRegex(ValueError,'strings'):
                    game_data_mod.build(root/'base.pack',source,root/'bad',MANIFEST)
                self.assertFalse((root/'bad').exists())

    def test_green_rules_preserve_other_fields_and_allow_zero_grace(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root);source=root/'green.json'
            source.write_text(json.dumps({'green_shell_rules':{'id':1,'collision_grace_ticks':0,'launch_speed_bonus':0}}))
            game_data_mod.build(root/'base.pack',source,root/'good',MANIFEST)
            self.assertEqual(struct.unpack('<10I',(root/'good/63-1.bin').read_bytes()),(0x31485347,8,0,100,0,7,16,244,53248,6656))
            for rules in ({'id':1},{'id':2,'max_speed':60},{'id':1,'max_speed':0},{'id':1,'max_speed':256},{'id':1,'wall_contact_budget':8},{'id':1,'release_lock_ticks':32768},{'id':1,'collision_grace_ticks':65536},{'id':1,'destruction_gravity_q16':16777216}):
                source.write_text(json.dumps({'green_shell_rules':rules}))
                with self.assertRaisesRegex(ValueError,'green_shell_rules'):
                    game_data_mod.build(root/'base.pack',source,root/'bad',MANIFEST)
                self.assertFalse((root/'bad').exists())

    def test_banana_rules_preserve_fields_and_reject_lock_after_landing(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root);source=root/'banana.json'
            source.write_text(json.dumps({'banana_rules':{'id':1,'collision_grace_ticks':0}}))
            game_data_mod.build(root/'base.pack',source,root/'good',MANIFEST)
            self.assertEqual(struct.unpack('<11I',(root/'good/65-1.bin').read_bytes()),(827212098, 9, 524288, 32, 106496, 6656, 0, 16, 244, 39936, 6656))
            for rules in [{'id': 1}, {'id': 2, 'cpu_hold_ticks': 60}, {'id': 1, 'cpu_hold_ticks': 0}, {'id': 1, 'cpu_hold_ticks': 32768}, {'id': 1, 'collision_grace_ticks': 65536}, {'id': 1, 'throw_speed_q16': 16777216}, {'id': 1, 'throw_gravity_q16': 16777215}, {'id': 1, 'release_lock_ticks': 32767}, {'id':1,'release_lock_ticks':1000,'throw_velocity_q16':16777215,'throw_gravity_q16':1}]:
                source.write_text(json.dumps({'banana_rules':rules}))
                with self.assertRaisesRegex(ValueError,'banana_rules'):
                    game_data_mod.build(root/'base.pack',source,root/'bad',MANIFEST)
                self.assertFalse((root/'bad/content.pack').exists())

    def test_red_rules_preserve_other_fields_and_allow_zero_grace(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root);source=root/'red.json'
            source.write_text(json.dumps({'red_shell_rules':{'id':1,'collision_grace_ticks':0,'launch_speed_bonus':0}}))
            game_data_mod.build(root/'base.pack',source,root/'good',MANIFEST)
            self.assertEqual(struct.unpack('<18I',(root/'good/64-1.bin').read_bytes()),(0x31485352,16,0,116,0,48,16,244,108,116,126,4,16,96,2700,30,53248,6656))
            for rules in ({'id':1},{'id':2,'max_launch_speed':60},{'id':1,'max_launch_speed':0},{'id':1,'max_launch_speed':256},{'id':1,'route_budget':256},{'id':1,'trap_arm_ticks':8},{'id':1,'trap_lock_ticks':0},{'id':1,'release_lock_ticks':32768},{'id':1,'collision_grace_ticks':65536},{'id':1,'destruction_gravity_q16':16777216}):
                source.write_text(json.dumps({'red_shell_rules':rules}))
                with self.assertRaisesRegex(ValueError,'red_shell_rules'):
                    game_data_mod.build(root/'base.pack',source,root/'bad',MANIFEST)
                self.assertFalse((root/'bad').exists())

    def test_boo_rules_preserve_unspecified_fields_and_bound_phases(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root);source=root/'boo.json'
            source.write_text(json.dumps({'boo_rules':{'id':1,'orbit_ticks':16}}))
            game_data_mod.build(root/'base.pack',source,root/'good',MANIFEST)
            self.assertEqual(struct.unpack('<9I',(root/'good/62-1.bin').read_bytes()),(0x314f4f42,7,360,360,16,1536,1024,135168,12288))
            for rules in ({'id':1},{'id':2,'lock_ticks':60},{'id':1,'lock_ticks':0},{'id':1,'invisibility_ticks':32768},{'id':1,'orbit_ticks':256},{'id':1,'orbit_step':32768},{'id':1,'repel_gravity_q16':16777216}):
                source.write_text(json.dumps({'boo_rules':rules}))
                with self.assertRaisesRegex(ValueError,'boo_rules'):
                    game_data_mod.build(root/'base.pack',source,root/'bad',MANIFEST)
                self.assertFalse((root/'bad').exists())

    def test_status_rules_keep_unspecified_values_and_reject_invalid_timers(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root);source=root/'status.json'
            source.write_text(json.dumps({'item_status_rules':{'id':1,'star_cpu_ticks':60}}))
            game_data_mod.build(root/'base.pack',source,root/'good',MANIFEST)
            self.assertEqual(struct.unpack('<5I',(root/'good/61-1.bin').read_bytes()),(0x31544153,3,372,60,360))
            for rules in ({'id':1},{'id':2,'star_cpu_ticks':60},{'id':1,'star_player_ticks':0},{'id':1,'star_cpu_ticks':32768},{'id':1,'lightning_ticks':47}):
                source.write_text(json.dumps({'item_status_rules':rules}))
                with self.assertRaisesRegex(ValueError,'item_status_rules'):
                    game_data_mod.build(root/'base.pack',source,root/'bad',MANIFEST)
                self.assertFalse((root/'bad').exists())

    def test_text_preserves_canvas_and_rejects_invalid_records_atomically(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root)
            source=root/'text.json'
            source.write_text(json.dumps({'text':[{'id':0x21100,'value':'NEO GP','color_rgb':0xffdd00}]}))
            game_data_mod.build(root/'base.pack',source,root/'good',MANIFEST)
            image=(root/'good/15-135424.bin').read_bytes()
            self.assertEqual(image[:32],(root/'text.bin').read_bytes()[:32])
            pixels=struct.unpack('<1024I',image[32:])
            self.assertEqual(set(pixels),{0,0xffffdd00})
            for record in ({'id':1,'value':'A'},{'id':0x21100,'value':'lower'},
                           {'id':0x21100,'value':'ABCDEFGHIJKLMNOPQ'},
                           {'id':0x21100,'value':''},{'id':0x21100,'value':'A','color_rgb':True}):
                source.write_text(json.dumps({'text':[record]}))
                with self.assertRaisesRegex(ValueError,'text'):
                    game_data_mod.build(root/'base.pack',source,root/'bad',MANIFEST)
                self.assertFalse((root/'bad').exists())

    def test_example_changes_real_table_fields_and_preserves_other_engines(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            motion, material, catalog = self.fixture(root)
            source = ROOT / 'mods/examples/data-tuning/data.json'
            for name in ('one', 'two'):
                game_data_mod.build(root / 'base.pack', source, root / name, MANIFEST)
            self.assertEqual((root / 'one/content.pack').read_bytes(), (root / 'two/content.pack').read_bytes())
            layers = {(meta['kind'], meta['content_id']): data for meta, data in
                      build_asset_pack.read_sections(root / 'one/content.pack')}
            for identity in range(1,12):
                self.assertEqual(struct.unpack_from('<6H',layers[(58,identity)],8),(4103,9,4105,1,0,255))
                self.assertEqual(layers[(58,identity)][20:],bytes(52))
            self.assertEqual(layers[(59,1)][8:16],bytes([18,12,6,2,0,0,0,0]))
            self.assertEqual(layers[(6,27)][288:288+2*384],material[288:288+2*384])
            self.assertEqual(struct.unpack_from('<192H',layers[(6,27)],288+2*384),tuple([4103]*192))
            self.assertEqual(struct.unpack_from('<I',layers[(59,1)],16)[0],0)
            self.assertEqual(struct.unpack('<7I',layers[(60,1)]),(0x3153554d,5,75,12,120,40,117440512))
            changed = layers[(13, 1)]
            preserved_prefix = bytearray(motion[:-452])
            struct.pack_into('<I',preserved_prefix,38056,1200*65536)
            self.assertEqual(changed[:-452],preserved_prefix)
            self.assertEqual(layers[(13,2)][:-452],motion[:-452])
            self.assertEqual(struct.unpack_from('<i', changed, len(changed) - 36)[0], 1200 * 65536)
            self.assertEqual(changed[-452 + 128], 126)
            self.assertEqual(struct.unpack_from('<16h', changed, len(changed) - 32), tuple([-1] * 10 + [1050] * 6))
            self.assertEqual(layers[(6, 1)][:3680], material[:3680])
            self.assertEqual(layers[(6, 1)][3800:], material[3800:])
            self.assertEqual(struct.unpack_from('<6H', layers[(6, 1)], 3680), (4098, 9, 4105, 1, 0, 255))
            rows = [struct.unpack_from('<IBBHI', layers[(37, 1)], 8 + 12 * index) for index in range(4)]
            self.assertEqual([row[0] for row in rows], [27, 29, 25, 24])
            self.assertEqual(rows[0], (27, 0, 0, 23, 7))
            self.assertEqual(rows[3], (24, 0, 3, 20, 4))
            for base in (0x23300, 0x23400, 0x23500):
                self.assertEqual([layers[(15, base + slot)] for slot in range(4)],
                                 [bytes([4]), bytes([3]), bytes([2]), bytes([1])])

    def test_cross_cup_exchange_preserves_metadata_and_moves_all_art(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root)
            game_data_mod.build(root/'base.pack',ROOT/'mods/examples/data-tuning/cross-cup.json',root/'cross',MANIFEST)
            sections={(m['kind'],m['content_id']):d for m,d in build_asset_pack.read_sections(root/'cross/content.pack')}
            rows=[struct.unpack_from('<IBBHI',sections[(37,1)],8+i*12) for i in range(8)]
            self.assertEqual(rows[0],(32,0,0,24,12))
            self.assertEqual(rows[4],(24,1,0,20,4))
            extra=[struct.unpack_from('<IBBHI',sections[(37,2)],8+i*12) for i in range(8)]
            self.assertEqual(extra[0],(68,0,0,44,36))
            self.assertEqual(extra[4],(64,1,0,40,32))
            for image in (0x23300,0x23400,0x23500):
                for slot,source in ((0,4),(4,0),(20,24),(24,20)):
                    self.assertEqual(sections[(15,image+slot)],bytes([source+1]))
            source=root/'bad-cross.json'
            for courses in ([32,25,29,27],[64,25,29,27]):
                source.write_text(json.dumps({'cups':[{'catalog':1,'cup':0,'courses':courses}]}))
                with self.assertRaisesRegex(ValueError,'cups.courses'):
                    game_data_mod.build(root/'base.pack',source,root/'bad-cross',MANIFEST)
                self.assertFalse((root/'bad-cross').exists())

    def test_mushroom_rules_validate_before_writing(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root);source=root/'mushroom.json'
            for rule in ({'id':1},{'id':2,'boost_ticks':1},{'boost_ticks':1},
                         {'id':1,'boost_ticks':0},{'id':1,'golden_actor_ticks':0},
                         {'id':1,'single_actor_ticks':65536},{'id':1,'acceleration_units':32768},
                         {'id':1,'max_speed_q16':0x07e00001},{'id':1,'boost_ticks':True}):
                source.write_text(json.dumps({'mushroom_rules':rule}))
                with self.assertRaisesRegex(ValueError,'mushroom_rules'):
                    game_data_mod.build(root/'base.pack',source,root/'bad-mushroom',MANIFEST)
                self.assertFalse((root/'bad-mushroom').exists())
            source.write_text(json.dumps({'mushroom_rules':{'id':1,'single_actor_ticks':0,'boost_ticks':65535}}))
            game_data_mod.build(root/'base.pack',source,root/'valid-mushroom',MANIFEST)
            _,data=next(iter(build_asset_pack.read_sections(root/'valid-mushroom/content.pack')))
            self.assertEqual(struct.unpack('<7I',data),(0x3153554d,5,65535,0,600,50,0x07e00000))

    def test_scoring_v3_mod_preserves_quality_rules(self):
        import mod_kit
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root)
            metadata=json.loads((root/'base.json').read_text())
            for section in metadata['sections']:
                if section['kind']==59:section['version']=3
            tail=struct.pack('<23i',*range(23))
            data=(root/'59.bin').read_bytes()+struct.pack('<I',2)+tail
            (root/'59.bin').write_bytes(data)
            (root/'base.json').write_text(json.dumps(metadata))
            (root/'base.pack').unlink()
            build_asset_pack.build_pack(root/'base.json',root/'base.pack')
            source=root/'rules.json'
            source.write_text(json.dumps({'gp_rules':{'id':1,'retry_credits':1},
                'gp_points':{'id':1,'awards':[1]*8}}))
            game_data_mod.build(root/'base.pack',source,root/'changed',MANIFEST)
            meta,changed=next(iter(build_asset_pack.read_sections(root/'changed/content.pack')))
            self.assertEqual((meta['version'],len(changed)),(3,112))
            self.assertEqual(changed[20:],tail)
            self.assertEqual(changed[8:16],bytes([1]*8))
            self.assertEqual(struct.unpack_from('<I',changed,16)[0],1)
            tables=mod_kit.base_tables({(59,1):(meta,changed)})
            self.assertEqual(tables['gp_points']['gp_rules']['retry_credits'],1)

    def test_retry_rules_validate_and_preserve_points(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root)
            source=root/'retry.json'
            for credits in (0,1,2,3):
                source.write_text(json.dumps({'gp_rules':{'id':1,'retry_credits':credits}}))
                game_data_mod.build(root/'base.pack',source,root/f'valid{credits}',MANIFEST)
                meta,data=next(iter(build_asset_pack.read_sections(root/f'valid{credits}/content.pack')))
                self.assertEqual((meta['kind'],meta['version'],len(data)),(59,2,20))
                self.assertEqual(data[8:16],bytes([9,6,3,1,0,0,0,0]))
                self.assertEqual(struct.unpack_from('<I',data,16)[0],credits)
            for rule in ({'id':1,'retry_credits':4},{'id':1,'retry_credits':-1},
                         {'id':1,'retry_credits':True},{'id':2,'retry_credits':0},
                         {'id':1},{'retry_credits':0},{'id':1,'retry_credits':0,'unknown':1}):
                source.write_text(json.dumps({'gp_rules':rule}))
                with self.assertRaisesRegex(ValueError,'gp_rules'):
                    game_data_mod.build(root/'base.pack',source,root/'bad-retry',MANIFEST)
                self.assertFalse((root/'bad-retry').exists())

    def test_invalid_kart_physics_is_rejected_before_writing_output(self):
        profiles=[{'id':1}, {'id':1,'engines':[]},
            {'id':1,'time_trial':{'cpu_target_speeds':[0]*160}},
            {'id':1,'time_trial':{'base_speed_q16':2017*65536}},
            {'id':1,'brake':[-32769]+[0]*7},
            {'id':1,'forward_target_response':[1]+[0]*7},
            {'id':1,'engines':[{'id':3,'base_speed_q16':1}]},
            {'id':1,'engines':[{'id':0,'base_speed_q16':1}]*2}]
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.fixture(root)
            source=root/'bad.json'
            for record in profiles:
                source.write_text(json.dumps({'kart_physics':[record]}))
                with self.assertRaisesRegex(ValueError,'kart_physics'):
                    game_data_mod.build(root/'base.pack',source,root/'bad',MANIFEST)
                self.assertFalse((root/'bad').exists())
            source.write_text(json.dumps({'kart_physics':[{'id':999,'time_trial':{'base_speed_q16':1}}]}))
            with self.assertRaisesRegex(ValueError,'kart_physics.id:.*lacks'):
                game_data_mod.build(root/'base.pack',source,root/'bad',MANIFEST)
            self.assertFalse((root/'bad').exists())

    def test_cpu_only_record_patches_last_rank_row_in_v6_material(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            material = struct.pack('<II',0x3154414d,256)+bytes(1432)
            (root/'material.bin').write_bytes(material)
            (root/'base.json').write_text(json.dumps({'sections':[{'kind':6,'content_id':27,'version':6,'path':'material.bin'}]}))
            build_asset_pack.build_pack(root/'base.json',root/'base.pack')
            record={'course':27,'engine':2,'variant':2,'driver_class':7,'items':[4103]*8}
            source=root/'data.json';source.write_text(json.dumps({'cpu_items':[record]}))
            game_data_mod.build(root/'base.pack',source,root/'out',MANIFEST)
            layers=list(build_asset_pack.read_sections(root/'out/content.pack'))
            self.assertEqual(layers[0][1][:-16],material[:-16])
            self.assertEqual(struct.unpack('<8H',layers[0][1][-16:]),tuple([4103]*8))
            source.write_text(json.dumps({'cpu_items':[record,record]}))
            with self.assertRaisesRegex(ValueError,'duplicate record identity'):
                game_data_mod.build(root/'base.pack',source,root/'bad',MANIFEST)
            self.assertFalse((root/'bad').exists())

    def test_runtime_pack_contains_only_the_validated_json(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = ROOT / 'mods/examples/data-tuning/data.json'
            game_data_mod.build_runtime(source, root / 'runtime', MANIFEST)
            sections = list(build_asset_pack.read_sections(root / 'runtime/content.pack'))
            self.assertEqual(len(sections), 1)
            meta, payload = sections[0]
            self.assertEqual((meta['kind'], meta['content_id'], meta['version'], meta['flags']), (57, 1, 1, 3))
            self.assertEqual(payload, source.read_bytes())
            bad = root / 'bad.json'
            for text in ('{}', '{"battle_items":null}'):
                bad.write_text(text)
                with self.assertRaisesRegex(ValueError, 'bad.json:'):
                    game_data_mod.build_runtime(bad, root / 'bad', MANIFEST)
                self.assertFalse((root / 'bad').exists())

    def test_invalid_data_names_file_and_field_without_writing_output(self):
        cases = [('{"cpu_items":[{"course":27,"engine":3,"variant":0,"driver_class":0,"items":[4103,4103,4103,4103,4103,4103,4103,4103]}]}','cpu_items.engine'),
                 ('{"cpu_items":[{"course":27,"engine":2,"variant":0,"driver_class":0,"items":[4105,4103,4103,4103,4103,4103,4103,4103]}]}','unsupported CPU'),
                 ('{"gp_points":{"id":2,"awards":[9,6,3,1,0,0,0,0]}}', 'gp_points.id'),
                 ('{"gp_points":{"id":1,"awards":[64,6,3,1,0,0,0,0]}}', 'gp_points.awards'),
                 ('{"gp_items":[{"id":12,"weights":[]}]}', 'gp_items.id'),
                 ('{"gp_items":[{"id":1,"weights":[]}]}', 'gp_items.weights'),
                 ('{"gp_items":[{"id":1,"weights":[{"item":4096,"weight":1}]},{"id":1,"weights":[{"item":4096,"weight":1}]}]}', 'duplicate'),
                 ('{"engine_speed_percent":{"150":true}}', 'engine_speed_percent.150'),
                 ('{"cups":[{"catalog":1,"cup":0,"courses":[24,24,25,29]}]}', 'courses'),
                 ('{"battle_items":[[],[],[]]}', 'battle_items'),
                 ('{"engine_speed_percent":{},"engine_speed_percent":{}}', 'duplicate'),
                 ('{"unknown":1}', 'data'),
                 ('{"battle_items":[[{"item":4105,"weight":32767},{"item":4098,"weight":1}],[],[]]}', 'total weight')]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            for source, message in cases:
                path = root / 'bad.json'
                path.write_text(source)
                with self.assertRaisesRegex(ValueError, f'bad.json:.*{message}'):
                    game_data_mod.build(root / 'base.pack', path, root / 'bad', MANIFEST)
                self.assertFalse((root / 'bad').exists())
            path.write_text('{"cups":[{"catalog":1,"cup":0,"courses":[24,25,29,999]}]}')
            with self.assertRaisesRegex(ValueError, 'courses: course ID is absent'):
                game_data_mod.build(root / 'base.pack', path, root / 'bad', MANIFEST)
            self.assertFalse((root / 'bad').exists())
