"""ROM-free MCR1 wire, C0 floor geometry and ordinary mod-kit coin authoring."""
import copy
from dataclasses import replace
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import mesh_coin_format as coins
import convert_course_3d as convert
import course_3d_source as source
import mod_kit
from course_3d_gltf import AssetIds
from test_course_3d_source import fixture,COURSE
ROOT=Path(__file__).resolve().parents[3]


class MeshCoinFormatTests(unittest.TestCase):
    def setUp(self):
        scratch=ROOT/'.mksc-tools/portable/runs/t4a';scratch.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(dir=scratch,prefix='coin-format-');self.root=Path(self.temp.name)
    def tearDown(self):self.temp.cleanup()

    def test_exact_wire_curve_and_bounds(self):
        record=coins.default_profile(41);before=copy.deepcopy(record)
        payload=coins.normalize(record)
        self.assertEqual(len(payload),452)
        self.assertEqual(struct.unpack_from('<8I',payload),(0x3152434d,1,452,41,1,13,99,5))
        self.assertEqual(struct.unpack_from('<5I',payload,432),(4,2,1,8,1))
        self.assertEqual(struct.unpack_from('<I',payload,32)[0],58982)
        self.assertEqual(struct.unpack_from('<I',payload,32+5*4)[0],62259)
        self.assertTrue(all(v==65536 for v in struct.unpack_from('<90I',payload,72)))
        self.assertEqual(coins.decode(payload),record)
        self.assertEqual(record,before)
        self.assertEqual(coins.compile(dict(version=1,profiles=[record])),
            [(dict(kind=105,content_id=41,version=1,flags=3),payload)])
        for field,value in (('profile_id',0),('revision',0),('modes',0),('modes',32),('modes',True),
                            ('cap',100),('initial',100),('initial',-1)):
            bad=copy.deepcopy(record);bad[field]=value
            with self.assertRaises(ValueError):coins.normalize(bad)
        for value in (0,3276,65537,True):
            bad=copy.deepcopy(record);bad['speed_ratios_q16'][99]=value
            with self.assertRaises(ValueError):coins.normalize(bad)
        for bad in (payload[:-1],payload+b'\0',b'BAD!'+payload[4:]):
            with self.assertRaises(ValueError):coins.decode(bad)
        bad=copy.deepcopy(record);bad['losses']['crush']=100
        with self.assertRaises(ValueError):coins.normalize(bad)
        with self.assertRaisesRegex(ValueError,'duplicate profile_id'):
            coins.compile(dict(version=1,profiles=[record,record]))
        self.root.joinpath('duplicate.json').write_text('{"version":1,"version":1,"profiles":[]}')
        with self.assertRaisesRegex(ValueError,'duplicate field'):coins.load(self.root/'duplicate.json')

    def test_cin1_unique_cell_center_ground_quad_and_owned_pixels(self):
        sections=fixture();sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0))
        original=source.read_course(sections,COURSE)
        pixels=tuple(0xff123400+i for i in range(64))
        tiles=list(original.tiles_argb);tiles[192]=pixels
        course=replace(original,tiles_argb=tuple(tiles),coins=(source.SourceCoin(16,24,7),
            source.SourceCoin(16,24,99),source.SourceCoin(24,24,8)))
        saved=copy.deepcopy(course);models=convert.Models(self.root,AssetIds());document=dict(objects=[])
        self.assertEqual(convert.coins(course,document,models,.1),2)
        self.assertEqual(len(models.meshes),1)
        first,second=document['objects']
        self.assertEqual(first['position'][:2],[2,.005])
        self.assertAlmostEqual(first['position'][2],-2.8)
        fields={p['key']:p['value'] for p in first['properties']}
        self.assertEqual(fields['coin:id'],3*course.map_width+3)
        self.assertEqual(fields['source:placement'],0)
        self.assertEqual(fields['source:parameter'],7)
        self.assertTrue(all(fields['coin:half-'+axis]==.4 for axis in ('x','y','z')))
        self.assertNotIn('mksc:billboard',fields)
        self.assertEqual(first['model'],second['model'])
        mesh=next(iter(models.meshes.values()))
        self.assertEqual(mesh['vertices'],[[-4,-4,0,0,0],[4,-4,0,1,0],[4,4,0,1,1],[-4,4,0,0,1]])
        self.assertEqual(mesh['triangles'],[[0,1,2,0],[0,2,3,0]])
        self.assertEqual(course,saved)
        self.assertEqual((self.root/next(iter(models.materials.values()))['texture']).read_bytes(),
                         convert.argb_png(8,8,pixels))
        for placement in (source.SourceCoin(17,24,0),source.SourceCoin(64,24,0)):
            with self.assertRaisesRegex(ValueError,'invalid source cell'):
                convert.coins(replace(course,coins=(placement,)),dict(objects=[]),models,.1)

    def test_consumed_coin_road_art_preserves_collision_and_source(self):
        original=source.read_course(fixture(),COURSE)
        tiles=list(original.tiles_argb)
        while len(tiles)<194:tiles.append((0xff223344,)*64)
        tiles[192]=(0xffffcc00,)*64;tiles[193]=(0xff123456,)*64
        material=list(original.materials)
        while len(material)<194:material.append(0)
        material[192]=0x44;material[193]=0x80
        indices=bytes([192,192,193,0])
        course=replace(original,map_width=2,map_height=2,tilemap=indices,
            tiles_argb=tuple(tiles),materials=tuple(material),coins=(source.SourceCoin(0,0,0),))
        saved=copy.deepcopy(course)
        road=convert.coin_road_tiles(course)
        self.assertEqual(road,bytes([193,192,193,0]))
        old=convert.terrain_format.encode(2,2,indices,tiles,material)
        new=convert.terrain_format.encode(2,2,indices,tiles,material,render_tilemap=road)
        self.assertEqual((old.collision_vertices,old.collision_triangles),
                         (new.collision_vertices,new.collision_triangles))
        self.assertNotEqual((old.materials,old.triangles),(new.materials,new.triangles))
        self.assertEqual(course,saved)

    def test_converter_rules_present_without_sine_or_placements(self):
        sections=fixture();sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0))
        course=source.read_course(sections,COURSE)
        with patch.object(convert.track_format,'load_pack',return_value={}), \
             patch.object(convert.source_format,'read_course',return_value=course), \
             patch.object(convert,'convert') as emitted:
            convert.generate(self.root/'unused',self.root/'converted',courses=[COURSE])
        record=coins.load(self.root/'converted/coins.json')['profiles'][0]
        self.assertEqual(emitted.call_args.kwargs['coin_profile'],record['profile_id'])
        self.assertIsNone(emitted.call_args.kwargs['actor_profile'])
        self.assertFalse((self.root/'converted/source-actors.json').exists())
        self.assertEqual(record['modes'],13)

    def test_example_cup_is_reproducible_and_bad_rules_preserve_archive(self):
        path=ROOT/'mods/examples/bridge-course/build.py'
        spec=importlib.util.spec_from_file_location('coin_bridge',path)
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        with self.assertRaisesRegex(ValueError,'coins requires items'):
            bridge.source(self.root/'bad-options',coins=True)
        authored=bridge.source(self.root/'authored',cup=True,items=True,coins=True)
        self.assertFalse((authored/'source-actors.json').exists())
        self.assertEqual(coins.load(authored/'coins.json')['profiles'][0]['modes'],31)
        for path in (authored/'courses3d').glob('*/course.json'):
            doc=json.loads(path.read_text());placed=[o for o in doc['objects'] if o['class_id']==0x20000001]
            self.assertEqual(len(placed),12)
            self.assertEqual([o['position'] for o in placed[:8]],
                             [[18+(i%2)*4,0,-24-(i//2)*1.5] for i in range(8)])
            self.assertEqual([o['position'] for o in placed[8:]],
                             [[20,0,-20],[20,0,-12],[-8,8,20],[8,8,20]])
            props=[{p['key']:p['value'] for p in o['properties']} for o in placed]
            self.assertEqual([p['coin:id'] for p in props],list(range(1,13)))
            self.assertTrue(all(p['coin:half-z']==.4 and 'mksc:billboard' not in p for p in props))
            self.assertIn(dict(key='mksc:coin-rules',type='asset',value=dict(kind=105,id=1)),doc['meta']['properties'])
        folder,archive=mod_kit.build(authored,None,self.root/'mods')
        before=archive.read_bytes();mod_kit.build(authored,None,self.root/'mods')
        self.assertEqual(archive.read_bytes(),before)
        bad=coins.default_profile();bad['speed_ratios_q16'][0]=1
        (authored/'coins.json').write_text(json.dumps(dict(version=1,profiles=[bad])))
        with self.assertRaisesRegex(mod_kit.KitError,'coins.json'):
            mod_kit.build(authored,None,self.root/'mods')
        self.assertEqual(archive.read_bytes(),before)
        # Explicitly disabled coins preserve every prior generated source byte.
        one=bridge.source(self.root/'one',items=True,boos=True)
        two=bridge.source(self.root/'two',items=True,boos=True,coins=False)
        contents=lambda r:{p.relative_to(r):p.read_bytes() for p in r.rglob('*') if p.is_file()}
        self.assertEqual(contents(one),contents(two))


if __name__=='__main__':unittest.main()
