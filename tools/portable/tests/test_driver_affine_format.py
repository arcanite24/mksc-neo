"""ROM-free AFR1 wire and transactional source-actor authoring."""
import copy
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import driver_affine_format as affine
import convert_course_3d as convert
import course_3d_source as source
import course_3d_gameplay as gameplay
import mod_kit
from course_3d_gltf import AssetIds
from test_course_3d_art import argb_image
from test_course_3d_source import fixture,COURSE
from test_course_3d_conversion import source_loop
from test_mesh_source_actor_format import motion
ROOT=Path(__file__).resolve().parents[3]


def profile():
    return dict(profile_id=91,tables=[dict(mode=m,rows=[[256,512,7],[320,256,3]])
                                    for m in (1,4,5,6,7)])


def thwomps():
    sections=fixture()
    # This actor-only fixture has no scenery; retain a valid empty SCN1 so
    # independent global-art discovery still validates the mocked course.
    sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0))
    placements=struct.pack('<4sI',b'TWP1',12)
    for v in range(12):placements+=struct.pack('<iiHBB',(300+v)*65536+12,(400+v)*65536+34,v,v,20+v)
    images=struct.pack('<4sI',b'TWA1',1)
    for lod,side in enumerate((64,32,16,8)):
        image=argb_image(side,side,[0xff507c88]*(side*side),anchor=(-side//2,-side*3//4))
        images+=struct.pack('<2I',lod,len(image))+image
    sections[41,COURSE]=({'version':2},placements)
    sections[42,COURSE]=({'version':1},images)
    return source.read_course(sections,COURSE)


class DriverAffineTests(unittest.TestCase):
    def setUp(self):
        scratch=ROOT/'.mksc-tools/portable/runs/t4a';scratch.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(dir=scratch,prefix='affine-');self.root=Path(self.temp.name)
    def tearDown(self):self.temp.cleanup()

    def test_exact_wire_and_collection(self):
        record=profile();before=copy.deepcopy(record);payload=affine.normalize(record)
        self.assertEqual(payload[:22],struct.pack('<4sIIIHHBB',b'AFR1',5,1,2,256,512,7,0))
        self.assertEqual(len(payload),108)
        self.assertEqual(affine.decode(payload,91),record)
        self.assertEqual(affine.compile(dict(version=1,profiles=[record])),
            [(dict(kind=81,content_id=91,version=1,flags=3),payload)])
        self.assertEqual(record,before)
        for value in (0,True,0xffffffff):
            bad=copy.deepcopy(record);bad['profile_id']=value
            with self.assertRaises(ValueError):affine.normalize(bad)
        for row in ([0,256,1],[256,65536,1],[256,256,0],[256,256,128],[True,256,1]):
            bad=copy.deepcopy(record);bad['tables'][2]['rows']=[row]
            with self.assertRaises(ValueError):affine.normalize(bad)
        for raw in (payload[:-1],payload+b'\0',payload[:21]+b'\1'+payload[22:]):
            with self.assertRaises(ValueError):affine.decode(raw,91)
        with self.assertRaisesRegex(ValueError,'duplicate profile_id'):
            affine.compile(dict(version=1,profiles=[record,record]))
        for sections in ({},{(81,91):({'version':2},payload)},
                         {(81,91):({'version':1},payload),(81,92):({'version':1},payload)}):
            with self.assertRaises(ValueError):affine.imported(sections)
        self.assertEqual(affine.imported({(81,91):({'version':1},payload)}),record)

    def test_converter_all_variants_and_explicit_import(self):
        course=thwomps();saved=copy.deepcopy(course)
        models=convert.Models(self.root,AssetIds());document,_=gameplay.derive(source_loop(),scale=.1)
        self.assertEqual(convert.actors(course,document,models,.1),12)
        objects=document['objects'][-12:]
        fields=[{p['key']:p['value'] for p in o['properties']} for o in objects]
        self.assertEqual([p['source:variant'] for p in fields],list(range(12)))
        self.assertEqual([p['source:placement-order'] for p in fields],list(range(20,32)))
        self.assertTrue(all(o['class_id']==0x1002 and p['source:frame-count']==1
                            for o,p in zip(objects,fields)))
        mesh=models.meshes[fields[0]['source:frame:0']['id']]
        self.assertEqual(mesh['vertices'][0],[-32,0,-16,0,1])
        self.assertEqual(mesh['vertices'][2],[32,0,48,1,0])
        self.assertEqual(course,saved)
        payload=affine.normalize(profile());sections={(13,93):motion(),(81,91):({'version':1},payload)}
        with patch.object(convert.track_format,'load_pack',return_value=sections), \
             patch.object(convert.source_format,'read_course',return_value=course), \
             patch.object(convert,'convert') as emitted:
            convert.generate(self.root/'unused',self.root/'converted',courses=[COURSE])
        exported=affine.load(self.root/'converted/actor-affine.json')
        compiled=affine.compile(exported)
        self.assertEqual(compiled[0][1],payload)
        self.assertEqual(emitted.call_args.kwargs['affine_profile'],compiled[0][0]['content_id'])
        with patch.object(convert.track_format,'load_pack',return_value={(13,93):motion()}), \
             patch.object(convert.source_format,'read_course',return_value=course):
            with self.assertRaisesRegex(ValueError,f'course={COURSE} thwomps'):
                convert.generate(self.root/'unused',self.root/'missing',courses=[COURSE])

    def test_original_example_compiles_reproducibly_and_keeps_legacy_bytes(self):
        path=ROOT/'mods/examples/bridge-course/build.py'
        spec=importlib.util.spec_from_file_location('stone_bridge',path)
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        contents=lambda r:{p.relative_to(r):p.read_bytes() for p in r.rglob('*') if p.is_file()}
        for i,flags in enumerate(({},dict(crabs=True),dict(seagulls=True),dict(lava_bubbles=True),
                dict(lava_bubbles=True,lava_bubbles_star=True,items=True))):
            self.assertEqual(contents(bridge.source(self.root/f'old-{i}',**flags)),
                contents(bridge.source(self.root/f'new-{i}',thwomps=False,**flags)))
        for star in (False,True):
            authored=bridge.source(self.root/f'authored-{star}',cup=True,items=True,thwomps=True,thwomps_star=star)
            for path in (authored/'courses3d').glob('*/course.json'):
                doc=json.loads(path.read_text());objects=[o for o in doc['objects'] if o['class_id']==0x1002]
                self.assertEqual(len(objects),1 if star else 2)
                self.assertEqual(objects[0]['position'],[19.5,0,0] if star else [20,0,-12])
                refs=[p for p in doc['meta']['properties'] if p['key']=='mksc:actor-affine-rules']
                self.assertEqual(refs,[dict(key='mksc:actor-affine-rules',type='asset',value=dict(kind=81,id=65081))])
            folder,archive=mod_kit.build(authored,None,self.root/f'mods-{star}')
            before=archive.read_bytes();mod_kit.build(authored,None,self.root/f'mods-{star}')
            self.assertEqual(archive.read_bytes(),before)
        with self.assertRaisesRegex(ValueError,'requires thwomps and items'):
            bridge.source(self.root/'invalid',thwomps_star=True)
        authored=bridge.source(self.root/'bad',thwomps=True)
        folder,archive=mod_kit.build(authored,None,self.root/'atomic')
        before=archive.read_bytes();(authored/'actor-affine.json').write_text('{}')
        with self.assertRaisesRegex(mod_kit.KitError,'actor-affine.json'):
            mod_kit.build(authored,None,self.root/'atomic')
        self.assertEqual(archive.read_bytes(),before)


if __name__=='__main__':unittest.main()
