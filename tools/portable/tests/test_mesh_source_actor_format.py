"""ROM-free MSA1 wire, imported sine agreement and transactional mod builds."""
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
import mesh_kart_format
import build_asset_pack
import mesh_source_actor_format as actor
import mod_kit
import convert_course_3d as convert
import course_3d_source as source
import course_3d_gameplay as gameplay
import model_format
from course_3d_gltf import AssetIds
from test_course_3d_art import actor_payload, argb_image, haunt_effect
from test_course_3d_source import fixture, COURSE
from test_course_3d_conversion import source_loop

ROOT=Path(__file__).resolve().parents[3]


def profile():
    return dict(profile_id=41,revision=7,sine=[0,32768,-32768]+[i-510 for i in range(1021)])


def motion(sine=None,version=8):
    values=profile()['sine'] if sine is None else sine
    return ({'version':version},struct.pack('<9I1024i',0x31544f4d,1024,64,8,8,8,
            16 if version==1 else 256,10,16,*values))


def crab_source():
    sections=fixture();sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0))
    placements,images=actor_payload(b'CRB1',b'CRA1',4,variant=0x8002,order=29)
    sections[43,COURSE]=({'version':1},placements)
    sections[44,COURSE]=({'version':1},images)
    return source.read_course(sections,COURSE)


def snowman_source():
    sections=fixture();sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0))
    placements,_=actor_payload(b'SNM1',b'SNA1',2,order=30)
    images=struct.pack('<4sI',b'SNA1',2)
    for frame in range(2):
        for lod,side in enumerate((4,2,1,1)):
            image=argb_image(side,side,[0xffd0e8ff-frame*0x101000]*(side*side),
                             anchor=(-3-2*frame,-5-2*frame))
            images+=struct.pack('<2I',lod,len(image))+image
    sections[51,COURSE]=({'version':1},placements)
    sections[52,COURSE]=({'version':1},images)
    return source.read_course(sections,COURSE)


def penguin_source():
    sections=fixture();sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0))
    placements=struct.pack('<4sI',b'PGN1',3)
    for x,y,variant,checkpoint,order in ((1244,1356,1,40,10),
                                       (1692,1364,1,44,11),(1780,1508,0,46,12)):
        placements+=struct.pack('<iiHBB',x*65536,y*65536,variant,checkpoint,order)
    images=struct.pack('<4sI',b'PGA1',6)
    for frame in range(6):
        for lod,side in enumerate((32,16,8,4)):
            image=argb_image(side,side,[0xff243b73+frame*0x100]*(side*side),
                             anchor=(-16-frame,-30-frame))
            images+=struct.pack('<2I',lod,len(image))+image
    sections[53,COURSE]=({'version':1},placements)
    sections[54,COURSE]=({'version':1},images)
    return source.read_course(sections,COURSE)


def mouse_source():
    sections=fixture();sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0))
    placements=struct.pack('<4sI',b'MUS1',8)
    for x,y,variant,checkpoint,order in ((788,700,0x8001,34,10),(860,796,0x8001,35,11),
        (372,1348,0,47,16),(732,1380,1,51,17),(1444,1444,1,58,19),
        (1652,1196,0,61,20),(1732,1060,0,62,21),(1636,940,0,63,22)):
        placements+=struct.pack('<iiHBB',x*65536,y*65536,variant,checkpoint,order)
    images=struct.pack('<4sI',b'MSA1',2)
    for frame in range(2):
        for lod,side in enumerate((32,32,16,8)):
            image=argb_image(side,side,[0xffc28e62+frame*0x100]*(side*side),
                             anchor=(-16 if lod<2 else -side//2,(-28-2*frame) if lod<2 else -side))
            images+=struct.pack('<2I',0 if lod<2 else lod-1,len(image))+image
    sections[73,COURSE]=({'version':1},placements)
    sections[74,COURSE]=({'version':1},images)
    return source.read_course(sections,COURSE)


def piranha_source():
    sections=fixture();sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0))
    placements=struct.pack('<4sI',b'PRN1',7)
    for x,y,checkpoint,order in ((484,316,28,8),(564,444,29,9),(356,500,30,10),
        (812,908,37,13),(1060,1020,40,14),(964,1108,43,15),(1372,1492,64,23)):
        placements+=struct.pack('<iiHBB',x*65536,y*65536,0,checkpoint,order)
    images=struct.pack('<4sI',b'PRA1',5)
    for frame,anchor_y in enumerate((-28,-29,-35,-28,-29)):
        for lod,side in enumerate((32,32,16,8)):
            image=argb_image(side,side,[0xff4fc16b+frame*0x100]*(side*side),
                             anchor=(-16 if lod<2 else -side//2,anchor_y if lod<2 else -side))
            images+=struct.pack('<2I',0 if lod<2 else lod-1,len(image))+image
    sections[49,COURSE]=({'version':1},placements)
    sections[50,COURSE]=({'version':1},images)
    return source.read_course(sections,COURSE)


def seagull_source():
    sections=fixture();sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0))
    placements=struct.pack('<4sI',b'SGL1',2)
    for x,y,checkpoint,order in ((908,1108,52,11),(1196,1092,54,12)):
        placements+=struct.pack('<iiHBB',x*65536,y*65536,0,checkpoint,order)
    images=struct.pack('<4sI',b'SGA1',3)
    for frame in range(3):
        shapes=((32,32,-16,-24),(32,32,-16,-24),(16,16,-8,-13),(8,8,-4,-7)) if frame==0 else (
                (64,32,-32,-56),(32,16,-16,-32),(16,8,-8,-24),(8,4,-4,-16))
        for lod,(width,height,x,y) in enumerate(shapes):
            image=argb_image(width,height,[0xffa8bfd4+frame*256]*(width*height),anchor=(x,y))
            images+=struct.pack('<2I',0 if lod<2 and frame==0 else lod,len(image))+image
    sections[45,COURSE]=({'version':1},placements)
    sections[46,COURSE]=({'version':1},images)
    return source.read_course(sections,COURSE)


def lava_bubble_source():
    sections=fixture();sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0))
    placements=struct.pack('<4sI',b'LVB1',13)
    for variant in range(13):
        placements+=struct.pack('<iiHBB',(532+variant)*65536+123,
            (1084+variant)*65536+321,variant,35+variant,5+variant)
    images=struct.pack('<4sI',b'LBA1',2)
    for frame in range(2):
        for lod,(side,x,y) in enumerate(((32,-16,-24),(16,-8,-12),(8,-4,-6),(8,-4,-6))):
            image=argb_image(side,side,[0xfff36a24+frame*256]*(side*side),anchor=(x,y))
            images+=struct.pack('<2I',min(lod,2),len(image))+image
    sections[39,COURSE]=({'version':2},placements)
    sections[40,COURSE]=({'version':1},images)
    return source.read_course(sections,COURSE)


def cannonball_source():
    sections=fixture();sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0));placements=struct.pack('<4sI',b'CBL1',4)
    for v in range(4):placements+=struct.pack('<iiHBB',(500+v)*65536+11,(600+v)*65536+22,v,30+v,10+v)
    images=struct.pack('<4sI',b'CBA1',7)
    shapes=((16,16,-8,-12),(44,24,-21,-10),(41,25,-21,-12),
            (54,36,-29,-26),(67,43,-34,-30),(49,26,-25,-35),(50,17,-25,-23))
    for frame,(width,height,x,y) in enumerate(shapes):
        for lod in range(4):
            w,h=max(1,width>>lod),max(1,height>>lod)
            image=argb_image(w,h,[0xff34465b+frame*256]*(w*h),
                            anchor=(x>>lod,y>>lod))
            images+=struct.pack('<2I',lod,len(image))+image
    sections[47,COURSE]=({'version':1},placements)
    sections[48,COURSE]=({'version':1},images)
    return source.read_course(sections,COURSE)


def boo_source():
    sections=fixture();sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0));placements=struct.pack('<4sI',b'BOO1',2)
    for v in range(2):placements+=struct.pack('<iiHBB',(500+v)*65536+17,(600+v)*65536+31,v,30+v,10+v)
    images=struct.pack('<4sI',b'BOA1',2)
    for frame in range(2):
        for lod in range(4):
            side=max(1,40>>lod)
            image=argb_image(side,side,[0xfff0e8ff-frame*256]*(side*side),
                            anchor=((-16-8*frame)>>lod,-48>>lod))
            images+=struct.pack('<2I',lod,len(image))+image
    sections[55,COURSE]=({'version':1},placements)
    sections[56,COURSE]=({'version':1},images)
    return source.read_course(sections,COURSE)


def rock_source(variant=0,checkpoint=36):
    sections=fixture();sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0));data=struct.pack('<4I',0x314e4353,5,7,0)
    data+=struct.pack('<iiBBHIBBH',100*65536,200*65536,0,0,256,0x860000,0,0,0)
    for index in range(4):
        data+=struct.pack('<iiBBHIBBH',(500+index)*65536+17,(600+index)*65536+31,
            checkpoint+index,2,224,0xa20000+(variant if index==0 else index),8+index,0,0)
    shapes=((64,64,-32,-60),(32,32,-16,-27),(16,16,-8,-15),
            (16,8,-8,-8),(16,16,-8,-16),(32,16,-16,-19),(48,8,-24,-24))
    for frame,(width,height,x,y) in enumerate(shapes):
        for lod in range(4):
            w,h=max(1,width>>lod),max(1,height>>lod)
            image=argb_image(w,h,[0xff5d4273+frame*256]*(w*h),anchor=(x>>lod,y>>lod))
            data+=struct.pack('<2I',lod,len(image))+image
    sections[28,COURSE]=({'version':4},data)
    return source.read_course(sections,COURSE)


def teepee_source(variant=0,checkpoint=36):
    sections=fixture();sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0));data=struct.pack('<4I',0x314e4353,6,11,0)
    for index in range(6):
        data+=struct.pack('<iiBBHIBBH',(500+index)*65536+17,(600+index)*65536+31,
            checkpoint,0,224,0x030000+variant,index,0,0)
    for frame in range(11):
        width,height,x,y=(32,32,-16,-31) if frame<4 else (24,40,-12-frame%2,-38)
        for lod in range(4):
            w,h=max(1,width>>lod),max(1,height>>lod)
            image=argb_image(w,h,[0xffe6a24b+frame*256]*(w*h),anchor=(x>>lod,y>>lod))
            data+=struct.pack('<2I',lod,len(image))+image
    sections[28,COURSE]=({'version':4},data)
    return source.read_course(sections,COURSE)


def falling_star_source(variant=0):
    sections=fixture();sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0));placements=struct.pack('<4sI',b'STR1',6)
    for index in range(6):
        placements+=struct.pack('<iiHBB',(500+index)*65536+17,(600+index)*65536+31,
            variant if index==0 else index,17+index,index+1)
    images=struct.pack('<4sI',b'SRA1',5)
    for frame,(width,height,x,y) in enumerate(((32,32,-16,-16),(8,8,-6,-3),
            (30,23,-14,-11),(46,40,-22,-24),(16,16,-8,-8))):
        for lod in range(4):
            w,h=max(1,width>>lod),max(1,height>>lod)
            image=argb_image(w,h,[0xffffcc45-frame*256]*(w*h),anchor=(x>>lod,y>>lod))
            images+=struct.pack('<2I',lod,len(image))+image
    sections[77,COURSE]=({'version':1},placements)
    sections[78,COURSE]=({'version':1},images)
    return source.read_course(sections,COURSE)


def thundercloud_source(variant=0):
    sections=fixture();sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0));placements=struct.pack('<4sI',b'TCD1',4)
    for index in range(4):
        placements+=struct.pack('<iiHBB',(500+index)*65536+17,(600+index)*65536+31,
            variant if index==0 else index,11+index,index+1)
    images=struct.pack('<4sI',b'TCA1',72)
    shapes=((64,32,-32,-56),(64,32,-32,-56),(71,37,-32,-58),(64,64,-32,-56))
    for frame in range(72):
        width,height,x,y=shapes[frame%4]
        for lod in range(4):
            w,h=max(1,width>>lod),max(1,height>>lod)
            image=argb_image(w,h,[0xff7484a3+frame*256]*(w*h),anchor=(x>>lod,y>>lod))
            images+=struct.pack('<2I',lod,len(image))+image
    sections[75,COURSE]=({'version':1},placements)
    sections[76,COURSE]=({'version':1},images)
    return source.read_course(sections,COURSE)


def common_prop_source(code=0x060001,checkpoint=17):
    sections=fixture();sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0));data=struct.pack('<4I',0x314e4353,8,9,0)
    rows=((0x010001,0,224),(0x0b0001,1,128),(code,2,224),(0x080000,3,128),
          (0x0a0007,4,256),(0x0c000b,5,192),(0x860000,6,224),(0x980001,8,256))
    for index,(raw,art,affine) in enumerate(rows):
        data+=struct.pack('<iiBBHIBBH',(500+index)*65536+17,(600+index)*65536+31,
            checkpoint if index==2 else index,art,affine,raw,index,0,0)
    for frame in range(9):
        width,height,x,y=8+frame,16+frame,-4-frame,-16-frame
        for lod in range(4):
            w,h=max(1,width>>lod),max(1,height>>lod)
            image=argb_image(w,h,[0xff3fb774+frame*256]*(w*h),anchor=(x>>lod,y>>lod))
            data+=struct.pack('<2I',lod,len(image))+image
    sections[28,COURSE]=({'version':4},data)
    return source.read_course(sections,COURSE)


def mechakoopa_source(variant=0,checkpoint=17):
    sections=fixture();sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0));data=struct.pack('<4I',0x314e4353,3,4,0)
    for index in range(3):
        data+=struct.pack('<iiBBHIBBH',(500+index)*65536+17,(600+index)*65536+31,
            checkpoint if index==0 else index,0,224,0xa10000|(variant if index==0 else index%2),index,0,0)
    for frame in range(4):
        anchor=(-16,-30 if frame%2==0 else -29)
        for lod in range(4):
            side=max(1,32>>lod)
            image=argb_image(side,side,[0xff58c48d+frame*256]*(side*side),
                            anchor=(anchor[0]>>lod,anchor[1]>>lod))
            data+=struct.pack('<2I',lod,len(image))+image
    sections[28,COURSE]=({'version':4},data)
    return source.read_course(sections,COURSE)


def kamek_source(variant=0,checkpoint=78):
    sections=fixture();sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0));data=struct.pack('<4I',0x314e4353,1,2,0)
    data+=struct.pack('<iiBBHIBBH',1812*65536,852*65536,checkpoint,0,224,
                      0xa00000|variant,15,0,0)
    for frame in range(2):
        for lod in range(4):
            side=max(1,32>>lod)
            image=argb_image(side,side,[0xff4b94d6+frame*256]*(side*side),
                            anchor=(-16>>lod,-30>>lod))
            data+=struct.pack('<2I',lod,len(image))+image
    sections[28,COURSE]=({'version':4},data)
    return source.read_course(sections,COURSE)


def pier_boo_source(variant=0,checkpoint=78):
    sections=fixture();sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0));data=struct.pack('<4I',0x314e4353,1,1,0)
    data+=struct.pack('<iiBBHIBBH',1812*65536,852*65536,checkpoint,0,256,
                      0xa30000|variant,15,0,0)
    for lod in range(4):
        side=max(1,32>>lod)
        image=argb_image(side,side,[0xffeee0ff]*(side*side),anchor=(-16>>lod,-27>>lod))
        data+=struct.pack('<2I',lod,len(image))+image
    sections[28,COURSE]=({'version':4},data)
    return source.read_course(sections,COURSE)


class SourceActorFormatTests(unittest.TestCase):
    def setUp(self):
        scratch=ROOT/'.mksc-tools/portable/runs/g54'
        scratch.mkdir(parents=True,exist_ok=True)
        self.temporary=tempfile.TemporaryDirectory(dir=scratch,prefix='source-actor-')
        self.root=Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def test_wire_exact_signed_q15_and_collection(self):
        record=profile();saved=copy.deepcopy(record)
        payload=actor.normalize(record)
        self.assertEqual(len(payload),4128)
        self.assertEqual(struct.unpack_from('<8I',payload),(0x3141534d,1,4128,41,7,1024,0,0))
        self.assertEqual(struct.unpack_from('<3i',payload,32),(0,32768,-32768))
        self.assertEqual(actor.decode(payload),record)
        self.assertEqual(record,saved)
        self.assertEqual(actor.compile(dict(version=1,profiles=[record])),
            [(dict(kind=104,content_id=41,version=1,flags=3),payload)])
        for key,value in [('profile_id',0),('profile_id',0xffffffff),('revision',False),('sine',[0]*1023),
                          ('sine',[32769]*1024),('sine',[True]*1024),('extra',1)]:
            bad=copy.deepcopy(record);bad[key]=value
            with self.assertRaises(ValueError):actor.normalize(bad)
        for offset,value in [(0,0),(4,2),(8,4124),(12,0),(12,0xffffffff),(16,0),(20,1023),(24,1),(28,1),(32,32769)]:
            bad=bytearray(payload);struct.pack_into('<I',bad,offset,value)
            with self.assertRaises(ValueError):actor.decode(bad)
        for bad in [payload[:-1],payload+b'\0']:
            with self.assertRaises(ValueError):actor.decode(bad)
        for records in [[],[record]*2,[record]*257]:
            with self.assertRaises(ValueError):actor.compile(dict(version=1,profiles=records))

    def test_profile_boundary_and_rejected_id_preserve_publication(self):
        record=profile();record['profile_id']=0xfffffffe
        self.assertEqual(actor.decode(actor.normalize(record)),record)
        directory=self.root/'id-source';directory.mkdir()
        (directory/'mod.json').write_text(json.dumps(dict(id='id-test',name='ID Test',
            version='1.0.0',api_version=1,targets=['3d'])))
        rules=directory/'source-actors.json'
        rules.write_text(json.dumps(dict(version=1,profiles=[profile()])))
        _,archive=mod_kit.build(directory,None,self.root/'id-mods')
        before=archive.read_bytes()
        bad=profile();bad['profile_id']=0xffffffff;saved=copy.deepcopy(bad)
        rules.write_text(json.dumps(dict(version=1,profiles=[bad])))
        with self.assertRaisesRegex(mod_kit.KitError,'source-actors.json'):
            mod_kit.build(directory,None,self.root/'id-mods')
        self.assertEqual(archive.read_bytes(),before)
        self.assertEqual(bad,saved)

    def test_imported_motion_prefix_requires_agreement_without_stock_id(self):
        sections={(13,93):motion(),(13,314):motion(version=1)}
        saved=copy.deepcopy(sections)
        self.assertEqual(actor.imported_sine(sections),profile()['sine'])
        self.assertEqual(sections,saved)
        changed=profile()['sine'];changed[-1]+=1
        with self.assertRaisesRegex(ValueError,'disagree'):
            actor.imported_sine({**sections,(13,314):motion(changed)})
        for bad in [({},b''),({'version':9},motion()[1]),
                    ({'version':8},motion()[1][:-1]),({'version':8},b'BAD!'+motion()[1][4:])]:
            with self.assertRaisesRegex(ValueError,'MOT1'):
                actor.imported_sine({(13,93):bad})
        with self.assertRaisesRegex(ValueError,'no imported MOT1'):actor.imported_sine({})

    def test_modkit_repeat_and_invalid_update_preserves_publication(self):
        directory=self.root/'source';directory.mkdir()
        (directory/'mod.json').write_text(json.dumps(dict(id='source-test',name='Source Test',
            version='1.0.0',api_version=1,targets=['3d'])))
        rules=directory/'source-actors.json'
        rules.write_text(json.dumps(dict(version=1,profiles=[profile()])))
        folder,archive=mod_kit.build(directory,None,self.root/'mods')
        first=archive.read_bytes()
        sections=list(build_asset_pack.read_sections(folder/'content.pack'))
        self.assertEqual([(h['kind'],h['content_id']) for h,_ in sections],[(104,41)])
        self.assertEqual(actor.decode(bytes(sections[0][1])),profile())
        mod_kit.build(directory,None,self.root/'mods')
        self.assertEqual(archive.read_bytes(),first)
        bad=profile();bad['sine'][1023]=-32769
        rules.write_text(json.dumps(dict(version=1,profiles=[bad])))
        with self.assertRaisesRegex(mod_kit.KitError,'source-actors.json'):
            mod_kit.build(directory,None,self.root/'mods')
        self.assertEqual(archive.read_bytes(),first)

    def test_crab_objects_preserve_source_fields_frames_and_anchors(self):
        course=crab_source();saved=copy.deepcopy(course)
        models=convert.Models(self.root,AssetIds())
        document,_=gameplay.derive(source_loop(),scale=.1)
        self.assertEqual(convert.actors(course,document,models,.1),1)
        placed=document['objects'][-1]
        props={p['key']:p['value'] for p in placed['properties']}
        self.assertEqual(props['mksc:controller'],'crabs')
        self.assertEqual([props['source:'+key] for key in
            ('x-q16','y-q16','variant','checkpoint','placement-order','frame-count')],
            [12*65536+7,20*65536+9,0x8002,127,29,4])
        self.assertEqual(placed['position'],[(12+7/65536)*.1,0,-(20+9/65536)*.1])
        self.assertEqual(placed['scale'],[.1]*3)
        ids=[props[f'source:frame:{i}']['id'] for i in range(4)]
        self.assertEqual(len(set(ids)),4)
        self.assertEqual(placed['model'],dict(kind=96,id=ids[2]))
        # Native anchor(-3,-5),full4x4: local feet span Z1..5, not a centered quad.
        self.assertEqual(models.meshes[ids[0]]['vertices'][0],[-3,0,1,0,1])
        self.assertEqual(models.meshes[ids[0]]['vertices'][2],[1,0,5,1,0])
        models.save()
        compiled=model_format.compile(model_format.load(self.root/'models.json'),self.root)
        self.assertEqual(sum(h['kind']==96 for h,_ in compiled),4)
        self.assertEqual(course,saved)

    def test_unknown_scenery_rejects_before_emitting_a_static_substitute(self):
        original=common_prop_source()
        section=next(s for s in original.passthrough if s.kind==28)
        for code in (0x040000,0x050000,0x090000,0x0e0000,0x950000,0x960000,
                     0x990000,0x010002,0x0b0002,0x020001):
            data=bytearray(section.data)
            struct.pack_into('<I',data,16+7*20+12,code)
            changed=replace(section,data=bytes(data))
            course=replace(original,passthrough=tuple(changed if s is section else s
                                                     for s in original.passthrough))
            saved=copy.deepcopy(course);document=dict(objects=[dict(keep=True)])
            models=convert.Models(self.root,AssetIds())
            with self.subTest(code=hex(code)),self.assertRaisesRegex(ValueError,
                    rf'course={COURSE} SCN1 code=0x{code:08x} placement=7: (unsupported source controller|invalid inert variant)'):
                convert.scenery(course,document,models,.1)
            self.assertEqual(document,dict(objects=[dict(keep=True)]))
            self.assertFalse(models.meshes);self.assertFalse(models.materials)
            self.assertFalse(list(self.root.iterdir()))
            self.assertEqual(course,saved)

    def test_source_proven_inert_scenery_remains_static(self):
        original=common_prop_source()
        section=next(s for s in original.passthrough if s.kind==28)
        for code in (0x010000,0x010001,0x020000,0x070000,0x0b0000,0x0b0001,
                     0x850000,0x8c0000,0x900000,0x9a0000,0x9b0000):
            data=bytearray(section.data);struct.pack_into('<I',data,16+7*20+12,code)
            changed=replace(section,data=bytes(data))
            course=replace(original,passthrough=tuple(changed if s is section else s
                                                     for s in original.passthrough))
            document=dict(objects=[])
            with self.subTest(code=hex(code)):
                self.assertEqual(convert.scenery(course,document,convert.Models(self.root,AssetIds()),.1),5)
                placed=document['objects'][-1]
                self.assertEqual(placed['class_id'],code)
                self.assertFalse(any(p['key']=='mksc:controller' for p in placed['properties']))

    def test_new_nonempty_actor_family_cannot_silently_disappear(self):
        course=crab_source();saved=copy.deepcopy(course)
        known=convert.art.decode_actors(course)[0]
        unknown=replace(known,family='future_hazard')
        document=dict(objects=[dict(keep=True)]);models=convert.Models(self.root,AssetIds())
        with patch.object(convert.art,'decode_actors',return_value=(known,unknown)), \
             self.assertRaisesRegex(ValueError,rf'course={COURSE} future_hazard: unsupported actor controller'):
            convert.actors(course,document,models,.1)
        self.assertEqual(document,dict(objects=[dict(keep=True)]))
        self.assertFalse(models.meshes);self.assertFalse(models.materials)
        self.assertFalse(list(self.root.iterdir()));self.assertEqual(course,saved)
        with patch.object(convert.art,'decode_actors',return_value=(replace(unknown,placements=()),)):
            self.assertEqual(convert.actors(course,document,models,.1),0)

    def test_convert_links_rules_and_existing_scale_without_duplicate_keys(self):
        from course_3d_format import compile as compile_course
        import course_3d_art
        actors=course_3d_art.decode_actors(crab_source())
        original=source_loop()
        with patch.object(convert.art,'decode_actors',return_value=actors), \
             patch.object(convert,'scenery',return_value=0),patch.object(convert,'sky'), \
             patch.object(convert.art,'decode_sky',return_value=None):
            document=convert.convert(original,self.root,AssetIds(),convert.Models(self.root,AssetIds()),
                                     scale=.1,actor_profile=41)
        properties=document['meta']['properties']
        self.assertEqual([p for p in properties if p['key']=='mksc:actor-rules'],
            [dict(key='mksc:actor-rules',type='asset',value=dict(kind=104,id=41))])
        self.assertEqual(len([p for p in properties if p['key']=='source:metres-per-unit']),1)
        self.assertTrue(any(p['key']=='mksc:controller' for p in document['objects'][-1]['properties']))
        self.assertTrue(compile_course(document,self.root))

    def test_original_bridge_crabs_are_self_contained_and_optional(self):
        spec=importlib.util.spec_from_file_location('source_actor_bridge',
            ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        plain=bridge.source(self.root/'plain')
        explicit=bridge.source(self.root/'explicit',crabs=False)
        contents=lambda root:{p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()}
        self.assertEqual(contents(plain),contents(explicit))
        self.assertFalse((plain/'source-actors.json').exists())
        authored=bridge.source(self.root/'crabs',cup=True,items=True,crabs=True)
        record=actor.load(authored/'source-actors.json')['profiles'][0]
        self.assertEqual([record['sine'][i] for i in (0,256,512,768)],
                         [0,32768,0,-32768])
        self.assertEqual(len(actor.normalize(record)),4128)
        courses=sorted((authored/'courses3d').glob('*/course.json'))
        self.assertEqual(len(courses),4)
        for path in courses:
            document=json.loads(path.read_text())
            props={p['key']:p['value'] for p in document['meta']['properties']}
            self.assertEqual(props['mksc:actor-rules'],dict(kind=104,id=1))
            self.assertEqual(props['source:metres-per-unit'],.1)
            crabs=[placed for placed in document['objects'] if any(
                p['key']=='mksc:controller' and p['value']=='crabs' for p in placed.get('properties',[]))]
            self.assertEqual(len(crabs),1)
            self.assertEqual(crabs[0]['position'],[20,0,-6])
            self.assertEqual(crabs[0]['scale'],[.1]*3)
            fields={p['key']:p['value'] for p in crabs[0]['properties']}
            self.assertEqual(fields['source:variant'],1)
            self.assertEqual([fields[f'source:frame:{i}'] for i in range(4)],
                             [dict(kind=96,id=i) for i in range(10,14)])
        models=json.loads((authored/'models.json').read_text())
        frames=[m for m in models['meshes'] if m['id'] in range(10,14)]
        self.assertEqual(len(frames),4)
        self.assertTrue(all(min(v[2] for v in m['vertices'])==0 for m in frames))
        self.assertNotEqual(frames[0]['vertices'],frames[1]['vertices'])
        folder,archive=mod_kit.build(authored,None,self.root/'mods')
        payloads=list(build_asset_pack.read_sections(folder/'content.pack'))
        self.assertEqual(sum(h['kind']==104 for h,_ in payloads),1)
        self.assertEqual(sum(h['kind']==99 for h,_ in payloads),4)
        previous=archive.read_bytes()
        mod_kit.build(authored,None,self.root/'mods')
        self.assertEqual(archive.read_bytes(),previous)

    def test_ground_follow_bridge_is_explicit_and_preserves_legacy_sources(self):
        import importlib.util
        spec=importlib.util.spec_from_file_location('bridge_ground_example',ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        plain=bridge.source(self.root/'legacy',items=True,crabs=True)
        explicit=bridge.source(self.root/'explicit-legacy',items=True,crabs=True,ground_follow=False)
        contents=lambda root:{p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()}
        self.assertEqual(contents(plain),contents(explicit))
        authored=bridge.source(self.root/'ground',cup=True,items=True,crabs=True,ground_follow=True)
        for path in (authored/'courses3d').glob('*/course.json'):
            document=json.loads(path.read_text())
            props={p['key']:p['value'] for p in document['meta']['properties']}
            self.assertEqual(props['mksc:actor-ground-follow'],1)
            crabs=[o for o in document['objects'] if o['class_id']==0x1003]
            self.assertEqual(len(crabs),1)
            self.assertEqual(crabs[0]['position'],[20,4,8])
        folder,archive=mod_kit.build(authored,None,self.root/'ground-mods')
        saved=archive.read_bytes();mod_kit.build(authored,None,self.root/'ground-mods')
        self.assertEqual(archive.read_bytes(),saved)
        self.assertTrue((folder/'content.pack').is_file())
        for options in ({},{'crabs':True,'items':True,'battle':True}):
            with self.assertRaisesRegex(ValueError,'ground_follow requires'):
                bridge.source(self.root/'invalid-ground',ground_follow=True,**options)

    def test_snowman_converter_preserves_two_distinct_anchors_and_requires_rules(self):
        course=snowman_source();saved=copy.deepcopy(course)
        models=convert.Models(self.root,AssetIds())
        document,_=gameplay.derive(source_loop(),scale=.1)
        self.assertEqual(convert.actors(course,document,models,.1),1)
        placed=document['objects'][-1]
        fields={p['key']:p['value'] for p in placed['properties']}
        self.assertEqual(placed['class_id'],0x1012)
        self.assertEqual(fields['mksc:controller'],'snowmen')
        self.assertEqual([fields['source:'+key] for key in
            ('variant','checkpoint','placement-order','frame-count')],[0,127,30,2])
        ids=[fields[f'source:frame:{i}']['id'] for i in range(2)]
        self.assertEqual(placed['model'],dict(kind=96,id=ids[0]))
        self.assertEqual(models.meshes[ids[0]]['vertices'][0],[-3,0,1,0,1])
        self.assertEqual(models.meshes[ids[1]]['vertices'][0],[-5,0,3,0,1])
        self.assertEqual(course,saved)
        with patch.object(convert.track_format,'load_pack',return_value={(13,93):motion()}), \
             patch.object(convert.source_format,'read_course',return_value=course), \
             patch.object(convert,'convert') as emitted:
            convert.generate(self.root/'unused',self.root/'snow-source',courses=[COURSE])
        rules=actor.load(self.root/'snow-source/source-actors.json')['profiles'][0]
        self.assertEqual(emitted.call_args.kwargs['actor_profile'],rules['profile_id'])
        self.assertEqual(rules['sine'],profile()['sine'])
        with patch.object(convert.track_format,'load_pack',return_value={}), \
             patch.object(convert.source_format,'read_course',return_value=course):
            with self.assertRaisesRegex(ValueError,'no imported MOT1'):
                convert.generate(self.root/'unused',self.root/'missing-rules',courses=[COURSE])

    def test_snowman_example_mixed_order_frames_and_optional_bytes(self):
        spec=importlib.util.spec_from_file_location('snowman_bridge',
            ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        contents=lambda root:{p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()}
        for crabs in (False,True):
            old=bridge.source(self.root/f'old-{crabs}',crabs=crabs)
            explicit=bridge.source(self.root/f'explicit-{crabs}',crabs=crabs,snowmen=False)
            self.assertEqual(contents(old),contents(explicit))
        for crabs in (False,True):
            authored=bridge.source(self.root/f'snow-{crabs}',cup=True,items=True,crabs=crabs,snowmen=True)
            for path in (authored/'courses3d').glob('*/course.json'):
                doc=json.loads(path.read_text())
                meta=[p for p in doc['meta']['properties'] if p['key']=='mksc:actor-rules']
                self.assertEqual(meta,[dict(key='mksc:actor-rules',type='asset',value=dict(kind=104,id=1))])
                actors=[p for p in doc['objects'] if any(
                    prop['key']=='mksc:controller' for prop in p.get('properties',[]))]
                self.assertEqual(len(actors),1+int(crabs))
                fields=[{prop['key']:prop['value'] for prop in p['properties']} for p in actors]
                self.assertEqual([row['source:placement-order'] for row in fields],list(range(1+int(crabs))))
                snow=actors[-1];props=fields[-1]
                self.assertEqual(snow['position'],[20,0,-12 if crabs else -6])
                self.assertEqual(snow['class_id'],0x1012)
                self.assertEqual(props['mksc:controller'],'snowmen')
                self.assertEqual(props['source:variant'],0)
                self.assertEqual(props['source:frame-count'],2)
                self.assertEqual([props[f'source:frame:{i}'] for i in range(2)],
                                 [dict(kind=96,id=20),dict(kind=96,id=21)])
            models=json.loads((authored/'models.json').read_text())
            frames=[m for m in models['meshes'] if m['id'] in (20,21)]
            self.assertEqual([min(v[2] for v in m['vertices']) for m in frames],[0,0])
            self.assertGreater(max(v[2] for v in frames[0]['vertices']),
                               max(v[2] for v in frames[1]['vertices']))
            folder,archive=mod_kit.build(authored,None,self.root/f'mods-{crabs}')
            old=archive.read_bytes()
            mod_kit.build(authored,None,self.root/f'mods-{crabs}')
            self.assertEqual(archive.read_bytes(),old)
            payloads=list(build_asset_pack.read_sections(folder/'content.pack'))
            self.assertEqual(sum(h['kind']==104 for h,_ in payloads),1)
            self.assertEqual(sum(h['kind']==99 for h,_ in payloads),4)

    def test_penguin_converter_three_placements_six_full_native_anchors(self):
        course=penguin_source();saved=copy.deepcopy(course)
        models=convert.Models(self.root,AssetIds())
        document,_=gameplay.derive(source_loop(),scale=.1)
        self.assertEqual(convert.actors(course,document,models,.1),3)
        placed=document['objects'][-3:]
        ids=None
        for obj,expected in zip(placed,((1244,1356,1,40,10),
                                        (1692,1364,1,44,11),(1780,1508,0,46,12))):
            fields={p['key']:p['value'] for p in obj['properties']}
            self.assertEqual(obj['class_id'],0x1016)
            self.assertEqual(fields['mksc:controller'],'penguins')
            self.assertEqual(fields['mksc:controller-version'],1)
            self.assertEqual([fields['source:'+key] for key in
                ('x-q16','y-q16','variant','checkpoint','placement-order','frame-count')],
                [expected[0]*65536,expected[1]*65536,*expected[2:],6])
            frame_ids=[fields[f'source:frame:{i}']['id'] for i in range(6)]
            if ids is None: ids=frame_ids
            self.assertEqual(frame_ids,ids)
            self.assertEqual(obj['model'],dict(kind=96,id=ids[0]))
            self.assertEqual(obj['position'],[expected[0]*.1,0,-expected[1]*.1])
        self.assertEqual(len(set(ids)),6)
        for i,identity in enumerate(ids):
            vertices=models.meshes[identity]['vertices']
            self.assertEqual(vertices[0],[-16-i,0,-2+i,0,1])
            self.assertEqual(vertices[2],[16-i,0,30+i,1,0])
        models.save()
        compiled=model_format.compile(model_format.load(self.root/'models.json'),self.root)
        self.assertEqual(sum(h['kind']==96 for h,_ in compiled),6)
        self.assertEqual(course,saved)
        with patch.object(convert.track_format,'load_pack',return_value={(13,93):motion()}), \
             patch.object(convert.source_format,'read_course',return_value=course), \
             patch.object(convert,'convert') as emitted:
            convert.generate(self.root/'unused',self.root/'penguin-source',courses=[COURSE])
        rules=actor.load(self.root/'penguin-source/source-actors.json')['profiles'][0]
        self.assertEqual(emitted.call_args.kwargs['actor_profile'],rules['profile_id'])
        self.assertEqual(rules['sine'],profile()['sine'])
        with patch.object(convert.track_format,'load_pack',return_value={}), \
             patch.object(convert.source_format,'read_course',return_value=course):
            with self.assertRaisesRegex(ValueError,'no imported MOT1'):
                convert.generate(self.root/'unused',self.root/'missing-penguin-rules',courses=[COURSE])

    def test_penguin_fixture_optional_bytes_mixed_order_and_cup_reproducibility(self):
        spec=importlib.util.spec_from_file_location('penguin_bridge',
            ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        contents=lambda root:{p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()}
        for crabs,snowmen in ((False,False),(True,False),(False,True),(True,True)):
            plain=bridge.source(self.root/f'plain-{crabs}-{snowmen}',crabs=crabs,snowmen=snowmen)
            explicit=bridge.source(self.root/f'explicit-{crabs}-{snowmen}',
                                   crabs=crabs,snowmen=snowmen,penguins=False)
            self.assertEqual(contents(plain),contents(explicit))
        for mixed in (False,True):
            authored=bridge.source(self.root/f'penguins-{mixed}',cup=True,items=True,
                                   crabs=mixed,snowmen=mixed,penguins=True)
            for path in (authored/'courses3d').glob('*/course.json'):
                doc=json.loads(path.read_text())
                meta=[p for p in doc['meta']['properties'] if p['key']=='mksc:actor-rules']
                self.assertEqual(meta,[dict(key='mksc:actor-rules',type='asset',value=dict(kind=104,id=1))])
                actors=[obj for obj in doc['objects'] if any(
                    p['key']=='mksc:controller' for p in obj.get('properties',[]))]
                self.assertEqual(len(actors),3 if mixed else 1)
                fields=[{p['key']:p['value'] for p in obj['properties']} for obj in actors]
                self.assertEqual([f['source:placement-order'] for f in fields],list(range(len(actors))))
                self.assertEqual(actors[-1]['position'],[-20,0,-16] if mixed else [20,0,-6])
                self.assertEqual(actors[-1]['class_id'],0x1016)
                self.assertEqual(fields[-1]['mksc:controller'],'penguins')
                self.assertEqual(fields[-1]['source:frame-count'],6)
                self.assertEqual([fields[-1][f'source:frame:{i}'] for i in range(6)],
                                 [dict(kind=96,id=i) for i in range(30,36)])
            models=json.loads((authored/'models.json').read_text())
            frames=[m for m in models['meshes'] if 30<=m['id']<36]
            self.assertEqual(len(frames),6)
            self.assertTrue(all(min(v[2] for v in m['vertices'])==0 for m in frames))
            self.assertEqual(len({json.dumps(m['vertices']) for m in frames}),6)
            folder,archive=mod_kit.build(authored,None,self.root/f'penguin-mods-{mixed}')
            original=archive.read_bytes()
            mod_kit.build(authored,None,self.root/f'penguin-mods-{mixed}')
            self.assertEqual(archive.read_bytes(),original)
        battle=bridge.source(self.root/'penguin-battle',battle=True,penguins=True)
        doc=json.loads((battle/'courses3d/bridge/course.json').read_text())
        self.assertEqual(doc['objects'][-1]['position'],[-7.25,0,-6])

    def test_penguin_star_fixture_uses_only_ordinary_authored_pickups(self):
        spec=importlib.util.spec_from_file_location('penguin_star_bridge',
            ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        for kwargs in ({},{'penguins':True},{'items':True}):
            with self.assertRaisesRegex(ValueError,'requires penguins and items'):
                bridge.source(self.root/'invalid-star',penguins_star=True,**kwargs)
            self.assertFalse((self.root/'invalid-star').exists())
        authored=bridge.source(self.root/'star',cup=True,items=True,penguins=True,penguins_star=True)
        rules=json.loads((authored/'items.json').read_text())['profiles'][0]
        self.assertEqual(rules['weights'],{domain:[[[item,1]] for _ in range(8)]
                         for domain,item in (('player',0x100a),('cpu',0x1007))})
        for path in (authored/'courses3d').glob('*/course.json'):
            doc=json.loads(path.read_text())
            self.assertEqual(doc['items'][0]['position'],[22,1,-28.5])
            self.assertEqual(doc['objects'][-1]['position'],[20,0,-2])
            self.assertEqual([box['respawn_ticks'] for box in doc['items'][:2]],[3600,3600])
        folder,archive=bridge.build(self.root/'star-mods',items=True,penguins=True,penguins_star=True)
        self.assertTrue(archive.is_file())
        self.assertTrue((self.root/'source/bridge-course-penguins-star').is_dir())

    def test_mouse_converter_eight_source_placements_and_native_frame_anchors(self):
        course=mouse_source();saved=copy.deepcopy(course)
        models=convert.Models(self.root,AssetIds())
        document,_=gameplay.derive(source_loop(),scale=.1)
        self.assertEqual(convert.actors(course,document,models,.1),8)
        actors=document['objects'][-8:]
        fields=[{p['key']:p['value'] for p in obj['properties']} for obj in actors]
        self.assertEqual([p['source:variant'] for p in fields],[0x8001,0x8001,0,1,1,0,0,0])
        self.assertEqual([p['source:checkpoint'] for p in fields],[34,35,47,51,58,61,62,63])
        self.assertEqual([p['source:placement-order'] for p in fields],[10,11,16,17,19,20,21,22])
        ids=[fields[0][f'source:frame:{i}']['id'] for i in range(2)]
        for obj,props in zip(actors,fields):
            self.assertEqual(obj['class_id'],0x1015)
            self.assertEqual(props['mksc:controller'],'mice')
            self.assertEqual(props['mksc:controller-version'],1)
            self.assertEqual(props['source:frame-count'],2)
            self.assertEqual([props[f'source:frame:{i}']['id'] for i in range(2)],ids)
            self.assertEqual(obj['model'],dict(kind=96,id=ids[0]))
        self.assertEqual(models.meshes[ids[0]]['vertices'][0],[-16,0,-4,0,1])
        self.assertEqual(models.meshes[ids[0]]['vertices'][2],[16,0,28,1,0])
        self.assertEqual(models.meshes[ids[1]]['vertices'][0],[-16,0,-2,0,1])
        self.assertEqual(models.meshes[ids[1]]['vertices'][2],[16,0,30,1,0])
        self.assertEqual(course,saved)
        with patch.object(convert.track_format,'load_pack',return_value={(13,93):motion()}), \
             patch.object(convert.source_format,'read_course',return_value=course), \
             patch.object(convert,'convert') as emitted:
            convert.generate(self.root/'unused',self.root/'mouse-source',courses=[COURSE])
        rules=actor.load(self.root/'mouse-source/source-actors.json')['profiles'][0]
        self.assertEqual(emitted.call_args.kwargs['actor_profile'],rules['profile_id'])
        self.assertEqual(rules['sine'],profile()['sine'])
        with patch.object(convert.track_format,'load_pack',return_value={}), \
             patch.object(convert.source_format,'read_course',return_value=course):
            with self.assertRaisesRegex(ValueError,'no imported MOT1'):
                convert.generate(self.root/'unused',self.root/'missing-mouse-rules',courses=[COURSE])

    def test_mouse_fixture_three_variants_on_distinct_legs_and_optional_bytes(self):
        spec=importlib.util.spec_from_file_location('mouse_bridge',ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        contents=lambda root:{p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()}
        for flags in ({},{'crabs':True},{'snowmen':True},{'penguins':True},
                      {'penguins':True,'penguins_star':True,'items':True}):
            plain=bridge.source(self.root/'previous',**flags)
            explicit=bridge.source(self.root/'explicit',mice=False,**flags)
            self.assertEqual(contents(plain),contents(explicit))
        for mixed in (False,True):
            authored=bridge.source(self.root/f'mice-{mixed}',cup=True,items=True,
                crabs=mixed,snowmen=mixed,penguins=mixed,mice=True)
            for path in (authored/'courses3d').glob('*/course.json'):
                doc=json.loads(path.read_text())
                actors=[obj for obj in doc['objects'] if any(
                    p['key']=='mksc:controller' for p in obj.get('properties',[]))]
                fields=[{p['key']:p['value'] for p in obj['properties']} for obj in actors]
                self.assertEqual([p['source:placement-order'] for p in fields],list(range(len(actors))))
                mice=actors[-3:];props=fields[-3:]
                self.assertEqual([p['source:variant'] for p in props],[0x8001,0,1])
                self.assertEqual([m['position'] for m in mice],
                    [[0,8,20] if mixed else [20,0,-6],[-4,0,-26],[-20,0,-18]])
                self.assertTrue(all(m['class_id']==0x1015 for m in mice))
                self.assertTrue(all(p['source:frame-count']==2 for p in props))
                self.assertEqual([props[0][f'source:frame:{i}'] for i in range(2)],
                                 [dict(kind=96,id=40),dict(kind=96,id=41)])
                self.assertEqual(sum(p['key']=='mksc:actor-rules' for p in doc['meta']['properties']),1)
            models=json.loads((authored/'models.json').read_text())
            frames=[m for m in models['meshes'] if m['id'] in (40,41)]
            self.assertEqual(len(frames),2)
            self.assertTrue(all(min(v[2] for v in m['vertices'])==0 for m in frames))
            self.assertNotEqual(frames[0]['vertices'],frames[1]['vertices'])
            folder,archive=mod_kit.build(authored,None,self.root/f'mouse-mods-{mixed}')
            before=archive.read_bytes()
            mod_kit.build(authored,None,self.root/f'mouse-mods-{mixed}')
            self.assertEqual(archive.read_bytes(),before)
        battle=bridge.source(self.root/'mouse-battle',battle=True,mice=True)
        doc=json.loads((battle/'courses3d/bridge/course.json').read_text())
        self.assertEqual([m['position'] for m in doc['objects']],
                         [[-7.25,0,-6],[-4,0,8],[8,0,-4]])

    def test_mouse_star_fixture_ordinary_weights_and_hopper_approach(self):
        spec=importlib.util.spec_from_file_location('mouse_star_bridge',ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        for kwargs in ({},{'mice':True},{'items':True}):
            with self.assertRaisesRegex(ValueError,'requires mice and items'):
                bridge.source(self.root/'invalid-mouse-star',mice_star=True,**kwargs)
            self.assertFalse((self.root/'invalid-mouse-star').exists())
        folder,archive=bridge.build(self.root/'star-mouse-mods',cup=True,
                                    items=True,mice=True,mice_star=True)
        authored=self.root/'source/bridge-cup-mice-star'
        self.assertTrue(archive.is_file())
        rules=json.loads((authored/'items.json').read_text())['profiles'][0]
        self.assertEqual(rules['weights'],{domain:[[[item,1]] for _ in range(8)]
                         for domain,item in (('player',0x100a),('cpu',0x1007))})
        for path in (authored/'courses3d').glob('*/course.json'):
            doc=json.loads(path.read_text())
            self.assertEqual(doc['items'][0]['position'],[22,1,-28.5])
            self.assertEqual([box['respawn_ticks'] for box in doc['items'][:2]],[3600,3600])
            self.assertEqual(doc['objects'][-3]['position'],[20,0,-2])
            self.assertEqual(doc['objects'][-2]['position'],[-4,0,-26])
            self.assertEqual(doc['objects'][-1]['position'],[-20,0,-18])

    def test_piranha_source_frames_anchors_and_rules(self):
        course=piranha_source();saved=copy.deepcopy(course)
        models=convert.Models(self.root,AssetIds())
        document,_=gameplay.derive(source_loop(),scale=.1)
        self.assertEqual(convert.actors(course,document,models,.1),7)
        placed=document['objects'][-7:]
        props=[{p['key']:p['value'] for p in obj['properties']} for obj in placed]
        self.assertEqual([p['source:checkpoint'] for p in props],[28,29,30,37,40,43,64])
        self.assertEqual([p['source:placement-order'] for p in props],[8,9,10,13,14,15,23])
        self.assertTrue(all(p['source:variant']==0 and p['source:frame-count']==5 for p in props))
        self.assertTrue(all(obj['class_id']==0x1009 for obj in placed))
        ids=[props[0][f'source:frame:{i}']['id'] for i in range(5)]
        for frame,y in enumerate((-28,-29,-35,-28,-29)):
            vertices=models.meshes[ids[frame]]['vertices']
            self.assertEqual(vertices[0],[-16,0,-32-y,0,1])
            self.assertEqual(vertices[2],[16,0,-y,1,0])
        self.assertEqual(course,saved)
        with patch.object(convert.track_format,'load_pack',return_value={(13,93):motion()}), \
             patch.object(convert.source_format,'read_course',return_value=course), \
             patch.object(convert,'convert') as emitted:
            convert.generate(self.root/'unused',self.root/'plant-source',courses=[COURSE])
        rules=actor.load(self.root/'plant-source/source-actors.json')['profiles'][0]
        self.assertEqual(emitted.call_args.kwargs['actor_profile'],rules['profile_id'])

    def test_capture_material_without_actor_is_ground_with_explicit_si_launch(self):
        self.assertEqual(convert.surface(42),dict(type='road',friction=1,speed_ratio=1,properties=[
            dict(key='source:material',type='integer',value=42),
            dict(key='mksc:capture',type='boolean',value=True)]))
        self.assertEqual(convert.surface(38)['type'],'out-of-bounds')
        self.assertEqual(convert.surface(34)['type'],'water')
        self.assertEqual(convert.surface(36)['type'],'lava')
        self.assertEqual(convert.surface(0x44)['type'],'road')
        self.assertFalse(gameplay._road_corridor(42))
        source_course=source_loop()
        source_course.materials=bytes((0x40,42))
        source_course.tilemap=source_course.tilemap[:-1]+bytes((1,))
        source_course.tiles_argb+=(tuple([0xff4fc16b]*64),)
        directory=self.root/'capture-course';directory.mkdir()
        identities=AssetIds();models=convert.Models(self.root,identities)
        physics=mesh_kart_format.resolved(json.loads((ROOT/'portable/data/mesh-kart.json').read_text()))
        physics['kart_radius_q16']=16384
        with patch.object(convert.art,'decode_actors',return_value=()), \
             patch.object(convert,'scenery',return_value=0),patch.object(convert,'sky'), \
             patch.object(convert.art,'decode_sky',return_value=None):
            doc=convert.convert(source_course,directory,identities,models,
                                route_profile=physics)
        fields={p['key']:p['value'] for p in doc['meta']['properties']}
        self.assertEqual(fields['mksc:capture-forward-speed'],24)
        self.assertAlmostEqual(fields['mksc:capture-up-speed'],17.544953292321715,delta=1/65536)
        self.assertNotIn('mksc:actor-rules',fields)
        self.assertTrue(any(s['type']=='road' and any(p['key']=='mksc:capture'
            for p in s.get('properties',[])) for s in doc['surfaces']))

    def test_piranha_fixture_disjoint_capture_floor_and_optional_bytes(self):
        spec=importlib.util.spec_from_file_location('plant_bridge',ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        contents=lambda root:{p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()}
        for index,flags in enumerate(({},dict(crabs=True),dict(snowmen=True),dict(penguins=True),
                                     dict(mice=True),dict(mice=True,mice_star=True,items=True))):
            before=bridge.source(self.root/f'plant-before-{index}',**flags)
            explicit=bridge.source(self.root/f'plant-off-{index}',piranhas=False,**flags)
            self.assertEqual(contents(before),contents(explicit))
        for star in (False,True):
            authored=bridge.source(self.root/f'plants-{star}',cup=True,items=True,
                                   piranhas=True,piranhas_star=star)
            for path in (authored/'courses3d').glob('*/course.json'):
                doc=json.loads(path.read_text())
                fields={p['key']:p['value'] for p in doc['meta']['properties']}
                self.assertEqual((fields['mksc:capture-forward-speed'],fields['mksc:capture-up-speed']),(12,3.6))
                plants=doc['objects'][-2:]
                self.assertEqual([obj['position'] for obj in plants],
                                 [[19.5 if star else 20,0,-2 if star else -6],[-20,0,-12]])
                self.assertTrue(all(obj['class_id']==0x1009 for obj in plants))
                props=[{p['key']:p['value'] for p in obj['properties']} for obj in plants]
                self.assertEqual([p['source:placement-order'] for p in props],[0,1])
                self.assertTrue(all(p['mksc:controller']=='piranhas' and
                    p['source:frame-count']==5 and p['source:variant']==0 for p in props))
                self.assertEqual([props[0][f'source:frame:{i}'] for i in range(5)],
                                 [dict(kind=96,id=i) for i in range(50,55)])
                self.assertEqual(doc['collision_nodes'][-1],dict(node='CaptureCollision',surface=2,respawn=0))
                self.assertEqual(doc['surfaces'][2]['type'],'road')
            course=authored/'courses3d/bridge'
            gltf=json.loads((course/'scene.gltf').read_text())
            binary=(course/'scene.bin').read_bytes()
            def positions(name):
                node=next(n for n in gltf['nodes'] if n['name']==name)
                primitive=gltf['meshes'][node['mesh']]['primitives'][0]
                accessor=gltf['accessors'][primitive['attributes']['POSITION']]
                view=gltf['bufferViews'][accessor['bufferView']]
                first=view.get('byteOffset',0)+accessor.get('byteOffset',0)
                return [struct.unpack_from('<3f',binary,first+i*view['byteStride'])
                        for i in range(accessor['count'])]
            deck=positions('DeckRender');patch=positions('CaptureRender')
            self.assertEqual(deck,positions('DeckCollision'))
            self.assertEqual(patch,positions('CaptureCollision'))
            self.assertEqual(len(patch),4)
            low_x,high_x=min(v[0] for v in patch),max(v[0] for v in patch)
            low_z,high_z=min(v[2] for v in patch),max(v[2] for v in patch)
            self.assertGreater(low_x,16)
            self.assertLess(high_x,24)
            self.assertGreater(low_z,-30)
            self.assertLess(high_z,0)
            self.assertAlmostEqual((low_x+high_x)/2,19.5 if star else 20,places=5)
            for i in range(0,len(deck),4):
                quad=deck[i:i+4]
                if any(v[1]!=0 for v in quad):continue
                self.assertTrue(max(v[0] for v in quad)<=low_x or min(v[0] for v in quad)>=high_x or
                                max(v[2] for v in quad)<=low_z or min(v[2] for v in quad)>=high_z)
            sand=next(m for m in gltf['materials'] if m['name']=='Warm capture sand')
            self.assertEqual(sand['pbrMetallicRoughness']['baseColorFactor'],[211/255,150/255,75/255,1])
            self.assertIn('CaptureRender',doc['render_nodes'])
            models=json.loads((authored/'models.json').read_text())
            frames=[m for m in models['meshes'] if 50<=m['id']<55]
            self.assertEqual(len(frames),5)
            self.assertEqual(len({json.dumps(m['vertices']) for m in frames}),5)
            self.assertTrue(all(min(v[2] for v in m['vertices'])==0 for m in frames))
            folder,archive=mod_kit.build(authored,None,self.root/f'plant-mods-{star}')
            old=archive.read_bytes();mod_kit.build(authored,None,self.root/f'plant-mods-{star}')
            self.assertEqual(archive.read_bytes(),old)
        deck=[[(-4,0,-10),(-4,0,10),(4,0,10),(4,0,-10)]]
        ordinary,capture=bridge.capture_floor(deck,[0,0,0],False)
        area=lambda q:(max(v[0] for v in q)-min(v[0] for v in q))*(max(v[2] for v in q)-min(v[2] for v in q))
        self.assertAlmostEqual(sum(map(area,ordinary+capture)),160)
        self.assertAlmostEqual(area(capture[0]),5.6**2)
        for q in ordinary:
            self.assertTrue(max(v[0] for v in q)<=-2.8 or min(v[0] for v in q)>=2.8 or
                            max(v[2] for v in q)<=-2.8 or min(v[2] for v in q)>=2.8)
        for kwargs in ({},{'piranhas':True},{'items':True}):
            with self.assertRaisesRegex(ValueError,'requires piranhas and items'):
                bridge.source(self.root/'bad-plant-star',piranhas_star=True,**kwargs)
            self.assertFalse((self.root/'bad-plant-star').exists())

    def test_seagull_converter_source_native_flight_anchors_and_rules(self):
        course=seagull_source();saved=copy.deepcopy(course)
        models=convert.Models(self.root,AssetIds())
        document,_=gameplay.derive(source_loop(),scale=.1)
        self.assertEqual(convert.actors(course,document,models,.1),2)
        placed=document['objects'][-2:]
        fields=[{p['key']:p['value'] for p in obj['properties']} for obj in placed]
        self.assertEqual([p['source:checkpoint'] for p in fields],[52,54])
        self.assertEqual([p['source:placement-order'] for p in fields],[11,12])
        self.assertEqual([p['source:x-q16'] for p in fields],[908*65536,1196*65536])
        self.assertEqual([p['source:y-q16'] for p in fields],[1108*65536,1092*65536])
        self.assertTrue(all(obj['class_id']==0x100c for obj in placed))
        self.assertTrue(all(p['mksc:controller']=='seagulls' and p['mksc:controller-version']==1
            and p['source:variant']==0 and p['source:frame-count']==3 for p in fields))
        ids=[fields[0][f'source:frame:{i}']['id'] for i in range(3)]
        self.assertEqual(len(set(ids)),3)
        self.assertEqual(models.meshes[ids[0]]['vertices'][0],[-16,0,-8,0,1])
        self.assertEqual(models.meshes[ids[0]]['vertices'][2],[16,0,24,1,0])
        for index in (1,2):
            self.assertEqual(models.meshes[ids[index]]['vertices'][0],[-32,0,24,0,1])
            self.assertEqual(models.meshes[ids[index]]['vertices'][2],[32,0,56,1,0])
        self.assertEqual(course,saved)
        with patch.object(convert.track_format,'load_pack',return_value={(13,93):motion()}), \
             patch.object(convert.source_format,'read_course',return_value=course), \
             patch.object(convert,'convert') as emitted:
            convert.generate(self.root/'unused',self.root/'bird-source',courses=[COURSE])
        rules=actor.load(self.root/'bird-source/source-actors.json')['profiles'][0]
        self.assertEqual(emitted.call_args.kwargs['actor_profile'],rules['profile_id'])
        with patch.object(convert.track_format,'load_pack',return_value={}), \
             patch.object(convert.source_format,'read_course',return_value=course):
            with self.assertRaisesRegex(ValueError,'no imported MOT1'):
                convert.generate(self.root/'unused',self.root/'missing-bird-rules',courses=[COURSE])

    def test_seagull_fixture_optional_mixed_order_and_star(self):
        spec=importlib.util.spec_from_file_location('bird_bridge',ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        contents=lambda root:{p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()}
        for index,flags in enumerate(({},dict(crabs=True),dict(snowmen=True),dict(penguins=True),dict(mice=True),
                                     dict(piranhas=True,items=True),dict(piranhas=True,piranhas_star=True,items=True))):
            before=bridge.source(self.root/f'bird-before-{index}',**flags)
            explicit=bridge.source(self.root/f'bird-off-{index}',seagulls=False,**flags)
            self.assertEqual(contents(before),contents(explicit))
        for mixed in (False,True):
            authored=bridge.source(self.root/f'birds-{mixed}',cup=True,items=True,seagulls=True,
                                   crabs=mixed,snowmen=mixed,penguins=mixed,mice=mixed,piranhas=mixed)
            for path in (authored/'courses3d').glob('*/course.json'):
                doc=json.loads(path.read_text())
                actors=[obj for obj in doc['objects'] if any(
                    p['key']=='mksc:controller' for p in obj.get('properties',[]))]
                fields=[{p['key']:p['value'] for p in obj['properties']} for obj in actors]
                self.assertEqual([p['source:placement-order'] for p in fields],list(range(len(actors))))
                bird=actors[-1];props=fields[-1]
                self.assertEqual(bird['position'],[-8,8,20] if mixed else [20,0,-6])
                self.assertEqual(bird['class_id'],0x100c)
                self.assertEqual(props['mksc:controller'],'seagulls')
                self.assertEqual(props['source:variant'],0)
                self.assertEqual([props[f'source:frame:{i}'] for i in range(3)],
                                 [dict(kind=96,id=i) for i in range(60,63)])
                self.assertEqual(sum(p['key']=='mksc:actor-rules' for p in doc['meta']['properties']),1)
            models=json.loads((authored/'models.json').read_text())
            frames=[m for m in models['meshes'] if 60<=m['id']<63]
            self.assertEqual(len(frames),3)
            self.assertEqual(len({json.dumps(m['vertices']) for m in frames}),3)
            self.assertTrue(all(min(v[2] for v in m['vertices'])==0 for m in frames))
            folder,archive=mod_kit.build(authored,None,self.root/f'bird-mods-{mixed}')
            old=archive.read_bytes();mod_kit.build(authored,None,self.root/f'bird-mods-{mixed}')
            self.assertEqual(archive.read_bytes(),old)
        battle=bridge.source(self.root/'bird-battle',battle=True,seagulls=True)
        self.assertEqual(json.loads((battle/'courses3d/bridge/course.json').read_text())['objects'][-1]['position'],
                         [-7.25,0,-6])
        for kwargs in ({},{'seagulls':True},{'items':True}):
            with self.assertRaisesRegex(ValueError,'requires seagulls and items'):
                bridge.source(self.root/'invalid-bird-star',seagulls_star=True,**kwargs)
        folder,archive=bridge.build(self.root/'bird-star-mods',items=True,seagulls=True,seagulls_star=True)
        authored=self.root/'source/bridge-course-seagulls-star'
        doc=json.loads((authored/'courses3d/bridge/course.json').read_text())
        self.assertEqual(doc['objects'][-1]['position'],[19.5,0,-2])
        self.assertEqual(doc['items'][0]['position'],[22,1,-28.5])
        self.assertEqual([box['respawn_ticks'] for box in doc['items'][:2]],[3600,3600])
        weights=json.loads((authored/'items.json').read_text())['profiles'][0]['weights']
        self.assertEqual(weights,{domain:[[[item,1]] for _ in range(8)]
                         for domain,item in (('player',0x100a),('cpu',0x1007))})
        self.assertTrue(archive.is_file())

    def test_lava_bubble_converter_preserves_native_anchors_variants_and_rules(self):
        course=lava_bubble_source();saved=copy.deepcopy(course)
        models=convert.Models(self.root,AssetIds())
        document,_=gameplay.derive(source_loop(),scale=.1)
        self.assertEqual(convert.actors(course,document,models,.1),13)
        objects=document['objects'][-13:]
        fields=[{p['key']:p['value'] for p in obj['properties']} for obj in objects]
        self.assertEqual([p['source:variant'] for p in fields],list(range(13)))
        self.assertEqual([p['source:placement-order'] for p in fields],list(range(5,18)))
        self.assertEqual([p['source:checkpoint'] for p in fields],list(range(35,48)))
        self.assertEqual(fields[0]['source:x-q16'],532*65536+123)
        self.assertEqual(fields[0]['source:y-q16'],1084*65536+321)
        self.assertTrue(all(o['class_id']==0x1004 for o in objects))
        self.assertTrue(all(p['mksc:controller']=='lava_bubbles' and
            p['source:frame-count']==2 for p in fields))
        ids=[fields[0][f'source:frame:{i}']['id'] for i in range(2)]
        self.assertEqual(len(set(ids)),2)
        for identity in ids:
            self.assertEqual(models.meshes[identity]['vertices'][0],[-16,0,-8,0,1])
            self.assertEqual(models.meshes[identity]['vertices'][2],[16,0,24,1,0])
        self.assertEqual(course,saved)
        with patch.object(convert.track_format,'load_pack',return_value={(13,93):motion()}), \
             patch.object(convert.source_format,'read_course',return_value=course), \
             patch.object(convert,'convert') as emitted:
            convert.generate(self.root/'unused',self.root/'bubble-source',courses=[COURSE])
        rules=actor.load(self.root/'bubble-source/source-actors.json')['profiles'][0]
        self.assertEqual(emitted.call_args.kwargs['actor_profile'],rules['profile_id'])
        with patch.object(convert.track_format,'load_pack',return_value={}), \
             patch.object(convert.source_format,'read_course',return_value=course):
            with self.assertRaisesRegex(ValueError,'no imported MOT1'):
                convert.generate(self.root/'unused',self.root/'missing-bubble-rules',courses=[COURSE])

    def test_lava_fixture_optional_mixed_cup_and_star(self):
        spec=importlib.util.spec_from_file_location('bubble_bridge',ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        contents=lambda root:{p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()}
        for index,flags in enumerate(({},dict(crabs=True),dict(snowmen=True),dict(penguins=True),
                dict(mice=True),dict(piranhas=True,items=True),dict(seagulls=True,items=True))):
            before=bridge.source(self.root/f'bubble-before-{index}',**flags)
            explicit=bridge.source(self.root/f'bubble-off-{index}',lava_bubbles=False,**flags)
            self.assertEqual(contents(before),contents(explicit))
        for mixed in (False,True):
            authored=bridge.source(self.root/f'bubbles-{mixed}',cup=True,items=True,lava_bubbles=True,
                crabs=mixed,snowmen=mixed,penguins=mixed,mice=mixed,piranhas=mixed,seagulls=mixed)
            for path in (authored/'courses3d').glob('*/course.json'):
                doc=json.loads(path.read_text())
                objects=[o for o in doc['objects'] if any(p['key']=='mksc:controller'
                         for p in o.get('properties',[]))]
                fields=[{p['key']:p['value'] for p in o['properties']} for o in objects]
                self.assertEqual([p['source:placement-order'] for p in fields],list(range(len(objects))))
                self.assertEqual(objects[-1]['position'],[-8,8,20] if mixed else [20,0,-4.5])
                self.assertEqual(fields[-1]['source:variant'],3 if mixed else 0)
                self.assertEqual([fields[-1][f'source:frame:{i}'] for i in range(2)],
                                 [dict(kind=96,id=70),dict(kind=96,id=71)])
                self.assertEqual(sum(p['key']=='mksc:actor-rules' for p in doc['meta']['properties']),1)
            models=json.loads((authored/'models.json').read_text())
            frames=[m for m in models['meshes'] if 70<=m['id']<=71]
            self.assertEqual(len(frames),2)
            self.assertTrue(all(min(v[2] for v in m['vertices'])==0 for m in frames))
            folder,archive=mod_kit.build(authored,None,self.root/f'bubble-mods-{mixed}')
            old=archive.read_bytes();mod_kit.build(authored,None,self.root/f'bubble-mods-{mixed}')
            self.assertEqual(archive.read_bytes(),old)
        for kwargs in ({},{'lava_bubbles':True},{'items':True}):
            with self.assertRaisesRegex(ValueError,'requires lava_bubbles and items'):
                bridge.source(self.root/'bad-bubble-star',lava_bubbles_star=True,**kwargs)
        folder,archive=bridge.build(self.root/'bubble-star-mods',items=True,lava_bubbles=True,lava_bubbles_star=True)
        authored=self.root/'source/bridge-course-lava-bubbles-star'
        doc=json.loads((authored/'courses3d/bridge/course.json').read_text())
        self.assertEqual(doc['objects'][-1]['position'],[19.5,0,0])
        self.assertEqual(next(p['value'] for p in doc['objects'][-1]['properties']
                             if p['key']=='source:variant'),5)
        self.assertEqual(doc['items'][0]['position'],[22,1,-28.5])
        self.assertEqual([b['respawn_ticks'] for b in doc['items'][:2]],[3600,3600])
        battle=bridge.source(self.root/'bubble-battle',battle=True,lava_bubbles=True)
        battle_object=json.loads((battle/'courses3d/bridge/course.json').read_text())['objects'][-1]
        self.assertEqual(battle_object['position'],[-7.25,0,-6])
        self.assertEqual(next(p['value'] for p in battle_object['properties']
                             if p['key']=='source:variant'),1)

    def test_cannonball_converter_full_frames_and_variants(self):
        course=cannonball_source();saved=copy.deepcopy(course)
        models=convert.Models(self.root,AssetIds());document,_=gameplay.derive(source_loop(),scale=.1)
        self.assertEqual(convert.actors(course,document,models,.1),4)
        fields=[{p['key']:p['value'] for p in o['properties']} for o in document['objects'][-4:]]
        self.assertEqual([p['source:variant'] for p in fields],list(range(4)))
        self.assertEqual([p['source:placement-order'] for p in fields],list(range(10,14)))
        self.assertEqual([p['source:checkpoint'] for p in fields],list(range(30,34)))
        self.assertEqual(fields[0]['source:x-q16'],500*65536+11)
        self.assertTrue(all(o['class_id']==0x1013 for o in document['objects'][-4:]))
        ids=[fields[0][f'source:frame:{i}']['id'] for i in range(7)]
        self.assertEqual(len(set(ids)),7)
        expected=((-8,-4,8,12),(-21,-14,23,10),(-21,-13,20,12),
                  (-29,-10,25,26),(-34,-13,33,30),(-25,9,24,35),(-25,6,25,23))
        for identity,(left,bottom,right,top) in zip(ids,expected):
            self.assertEqual(models.meshes[identity]['vertices'][0],[left,0,bottom,0,1])
            self.assertEqual(models.meshes[identity]['vertices'][2],[right,0,top,1,0])
        self.assertEqual(course,saved)
        with patch.object(convert.track_format,'load_pack',return_value={(13,93):motion()}), \
             patch.object(convert.source_format,'read_course',return_value=course), \
             patch.object(convert,'convert') as emitted:
            convert.generate(self.root/'unused',self.root/'cannon-source',courses=[COURSE])
        self.assertIsNotNone(emitted.call_args.kwargs['actor_profile'])
        self.assertIsNone(emitted.call_args.kwargs['affine_profile'])
        self.assertFalse((self.root/'cannon-source/actor-affine.json').exists())

    def test_cannonball_fixture_mixed_order_and_star(self):
        spec=importlib.util.spec_from_file_location('cannon_bridge',ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        for mixed in (False,True):
            authored=bridge.source(self.root/f'cannon-{mixed}',cup=True,items=True,cannonballs=True,
                crabs=mixed,snowmen=mixed,penguins=mixed,mice=mixed,piranhas=mixed,
                seagulls=mixed,lava_bubbles=mixed,thwomps=mixed)
            for path in (authored/'courses3d').glob('*/course.json'):
                doc=json.loads(path.read_text());objects=[o for o in doc['objects'] if any(
                    p['key']=='mksc:controller' for p in o.get('properties',[]))]
                fields=[{p['key']:p['value'] for p in o['properties']} for o in objects]
                self.assertEqual([p['source:placement-order'] for p in fields],list(range(len(objects))))
                self.assertEqual(objects[-2]['position'],[-8,8,20] if mixed else [20,0,-10])
                self.assertEqual(objects[-1]['position'],[8,8,20] if mixed else [19.5,0,-1])
                self.assertEqual([p['source:variant'] for p in fields[-2:]],[3,0])
                self.assertEqual([fields[-1][f'source:frame:{i}'] for i in range(7)],
                                 [dict(kind=96,id=i) for i in range(90,97)])
            models=json.loads((authored/'models.json').read_text())
            frames=[m for m in models['meshes'] if 90<=m['id']<=96]
            self.assertEqual(len(frames),7)
            self.assertTrue(all(min(v[2] for v in m['vertices'])==0 for m in frames))
            folder,archive=mod_kit.build(authored,None,self.root/f'cannon-mods-{mixed}')
            before=archive.read_bytes();mod_kit.build(authored,None,self.root/f'cannon-mods-{mixed}')
            self.assertEqual(archive.read_bytes(),before)
        for kwargs in ({},{'cannonballs':True},{'items':True}):
            with self.assertRaisesRegex(ValueError,'requires cannonballs and items'):
                bridge.source(self.root/'invalid-cannon',cannonballs_star=True,**kwargs)
        bridge.build(self.root/'cannon-star-mods',items=True,cannonballs=True,cannonballs_star=True)
        root=self.root/'source/bridge-course-cannonballs-star'
        doc=json.loads((root/'courses3d/bridge/course.json').read_text())
        self.assertEqual(doc['objects'][-1]['position'],[19.5,0,-1])
        self.assertEqual(next(p['value'] for p in doc['objects'][-1]['properties']
                             if p['key']=='source:variant'),0)
        self.assertEqual(doc['items'][0]['position'],[22,1,-28.5])
        self.assertEqual([b['respawn_ticks'] for b in doc['items'][:2]],[3600,3600])
        self.assertFalse((root/'actor-affine.json').exists())

    def test_boo_converter_preserves_both_cell_anchors_and_variants(self):
        course=boo_source();saved=copy.deepcopy(course)
        models=convert.Models(self.root,AssetIds());document,_=gameplay.derive(source_loop(),scale=.1)
        self.assertEqual(convert.actors(course,document,models,.1),2)
        objects=document['objects'][-2:]
        fields=[{p['key']:p['value'] for p in o['properties']} for o in objects]
        self.assertEqual([p['source:variant'] for p in fields],[0,1])
        self.assertEqual([p['source:placement-order'] for p in fields],[10,11])
        self.assertEqual([p['source:checkpoint'] for p in fields],[30,31])
        self.assertEqual(fields[0]['source:x-q16'],500*65536+17)
        self.assertTrue(all(o['class_id']==0x1007 and p['source:frame-count']==2
                            for o,p in zip(objects,fields)))
        for frame,(left,right) in enumerate(((-16,24),(-24,16))):
            mesh=models.meshes[fields[0][f'source:frame:{frame}']['id']]
            self.assertEqual(mesh['vertices'][0],[left,0,8,0,1])
            self.assertEqual(mesh['vertices'][2],[right,0,48,1,0])
        self.assertEqual(course,saved)
        with patch.object(convert.track_format,'load_pack',return_value={(13,93):motion()}), \
             patch.object(convert.source_format,'read_course',return_value=course), \
             patch.object(convert,'convert') as emitted:
            convert.generate(self.root/'unused',self.root/'boo-source',courses=[COURSE])
        self.assertIsNotNone(emitted.call_args.kwargs['actor_profile'])
        self.assertIsNone(emitted.call_args.kwargs['affine_profile'])

    def test_boo_fixture_original_cells_mixed_order_and_star(self):
        spec=importlib.util.spec_from_file_location('boo_bridge',ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        for mixed in (False,True):
            authored=bridge.source(self.root/f'boo-{mixed}',cup=True,items=True,boos=True,
                crabs=mixed,snowmen=mixed,penguins=mixed,mice=mixed,piranhas=mixed,
                seagulls=mixed,lava_bubbles=mixed,thwomps=mixed,cannonballs=mixed)
            for path in (authored/'courses3d').glob('*/course.json'):
                doc=json.loads(path.read_text());objects=[o for o in doc['objects'] if any(
                    p['key']=='mksc:controller' for p in o.get('properties',[]))]
                fields=[{p['key']:p['value'] for p in o['properties']} for o in objects]
                self.assertEqual([p['source:placement-order'] for p in fields],list(range(len(objects))))
                self.assertEqual(objects[-2]['position'],[-8,8,20] if mixed else [20,0,-10])
                self.assertEqual(objects[-1]['position'],[8,8,20] if mixed else [19.5,0,-1])
                self.assertEqual([p['source:variant'] for p in fields[-2:]],[0,1])
                self.assertEqual([fields[-1][f'source:frame:{i}'] for i in range(2)],
                                 [dict(kind=96,id=110),dict(kind=96,id=111)])
            models=json.loads((authored/'models.json').read_text())
            frames=[m for m in models['meshes'] if 110<=m['id']<=111]
            self.assertEqual(len(frames),2)
            self.assertNotEqual(frames[0]['vertices'],frames[1]['vertices'])
            self.assertTrue(all(min(v[2] for v in m['vertices'])==0 for m in frames))
            folder,archive=mod_kit.build(authored,None,self.root/f'boo-mods-{mixed}')
            before=archive.read_bytes();mod_kit.build(authored,None,self.root/f'boo-mods-{mixed}')
            self.assertEqual(archive.read_bytes(),before)
        for kwargs in ({},{'boos':True},{'items':True}):
            with self.assertRaisesRegex(ValueError,'requires boos and items'):
                bridge.source(self.root/'invalid-boo',boos_star=True,**kwargs)
        bridge.build(self.root/'boo-star-mods',items=True,boos=True,boos_star=True)
        root=self.root/'source/bridge-course-boos-star'
        doc=json.loads((root/'courses3d/bridge/course.json').read_text())
        ghosts=[o for o in doc['objects'] if o['class_id']==0x1007]
        self.assertEqual(len(ghosts),1)
        self.assertEqual(ghosts[0]['position'],[19.5,0,-1])
        self.assertEqual(doc['items'][0]['position'],[22,1,-28.5])
        self.assertEqual([b['respawn_ticks'] for b in doc['items'][:2]],[3600,3600])
        self.assertFalse((root/'actor-affine.json').exists())
        arena=bridge.source(self.root/'boo-battle',battle=True,boos=True)
        doc=json.loads((arena/'courses3d/bridge/course.json').read_text())
        self.assertEqual(doc['objects'][-2]['position'],[-7.1,0,-4])

    def test_rocks_replace_static_scenery_preserve_all_source_fields_and_anchors(self):
        course=rock_source();saved=copy.deepcopy(course)
        models=convert.Models(self.root,AssetIds());document,_=gameplay.derive(source_loop(),scale=.1)
        self.assertEqual(convert.scenery(course,document,models,.1),5)
        self.assertEqual(len(document['objects']),5)
        rocks=[o for o in document['objects'] if o['class_id']==0x101d]
        self.assertEqual(len(rocks),4)
        self.assertFalse(any(o['class_id']>>16==0xa2 for o in document['objects']))
        fields=[{p['key']:p['value'] for p in o['properties']} for o in rocks]
        self.assertEqual([p['source:variant'] for p in fields],list(range(4)))
        self.assertEqual([p['source:checkpoint'] for p in fields],list(range(36,40)))
        self.assertEqual([p['source:placement-order'] for p in fields],list(range(8,12)))
        self.assertEqual(fields[0]['source:x-q16'],500*65536+17)
        self.assertTrue(all(o['scale']==[.1]*3 and p['source:affine-q8']==224
                            for o,p in zip(rocks,fields)))
        expected=((-8,-1,8,15),(-8,0,8,8),(-8,0,8,16),(-16,3,16,19),(-24,16,24,24))
        for frame,(left,bottom,right,top) in enumerate(expected):
            mesh=models.meshes[fields[0][f'source:frame:{frame}']['id']]
            self.assertEqual(mesh['vertices'][0],[left,0,bottom,0,1])
            self.assertEqual(mesh['vertices'][2],[right,0,top,1,0])
        self.assertEqual(len(models.meshes),7)
        self.assertEqual(course,saved)
        for bad in (rock_source(variant=4),rock_source(checkpoint=128)):
            with self.assertRaisesRegex(ValueError,'rocks: invalid variant/checkpoint'):
                convert.scenery(bad,dict(objects=[]),models,.1)
        with patch.object(convert.track_format,'load_pack',return_value={(13,93):motion()}), \
             patch.object(convert.source_format,'read_course',return_value=course), \
             patch.object(convert,'convert') as emitted:
            convert.generate(self.root/'unused',self.root/'rocks-source',courses=[COURSE])
        self.assertIsNotNone(emitted.call_args.kwargs['actor_profile'])
        self.assertTrue((self.root/'rocks-source/source-actors.json').is_file())
        directory=self.root/'rock-export';directory.mkdir()
        decoded=convert.art.decode_scenery(course)
        with patch.object(convert.art,'decode_scenery',return_value=decoded), \
             patch.object(convert.art,'decode_actors',return_value=()),patch.object(convert,'sky'), \
             patch.object(convert.art,'decode_sky',return_value=None):
            emitted=convert.convert(source_loop(),directory,AssetIds(),
                convert.Models(directory,AssetIds()),actor_profile=41)
        self.assertEqual(sum(o['class_id']==0x101d for o in emitted['objects']),4)
        self.assertEqual([p for p in emitted['meta']['properties'] if p['key']=='mksc:actor-rules'],
                         [dict(key='mksc:actor-rules',type='asset',value=dict(kind=104,id=41))])
        with patch.object(convert.track_format,'load_pack',return_value={}), \
             patch.object(convert.source_format,'read_course',return_value=course):
            with self.assertRaisesRegex(ValueError,'no imported MOT1'):
                convert.generate(self.root/'unused',self.root/'missing-rock-rules',courses=[COURSE])

    def test_rock_fixture_all_variants_mixed_and_star(self):
        spec=importlib.util.spec_from_file_location('rock_bridge',ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        for star in (False,True):
            authored=bridge.source(self.root/f'rocks-{star}',cup=True,items=True,rocks=True,rocks_star=star)
            for path in (authored/'courses3d').glob('*/course.json'):
                doc=json.loads(path.read_text());rocks=[o for o in doc['objects'] if o['class_id']==0x101d]
                props=[{p['key']:p['value'] for p in o['properties']} for o in rocks]
                self.assertEqual(len(rocks),4)
                self.assertEqual({p['source:variant'] for p in props},set(range(4)))
                self.assertEqual([p['source:placement-order'] for p in props],list(range(4)))
                self.assertTrue(all(p['source:affine-q8']==256 and p['source:frame-count']==5 for p in props))
                self.assertEqual(rocks[0]['position'],[19.5,0,-1] if star else [19.5,0,0])
                if star:self.assertEqual(doc['items'][0]['position'],[22,1,-28.5])
            folder,archive=mod_kit.build(authored,None,self.root/f'rock-mods-{star}')
            before=archive.read_bytes();mod_kit.build(authored,None,self.root/f'rock-mods-{star}')
            self.assertEqual(archive.read_bytes(),before)
        mixed=bridge.source(self.root/'mixed-rocks',items=True,crabs=True,boos=True,rocks=True)
        objects=json.loads((mixed/'courses3d/bridge/course.json').read_text())['objects']
        orders=[next(p['value'] for p in o['properties'] if p['key']=='source:placement-order')
                for o in objects if any(p['key']=='mksc:controller' for p in o.get('properties',[]))]
        self.assertEqual(orders,list(range(7)))
        for kwargs in ({},{'rocks':True},{'items':True}):
            with self.assertRaisesRegex(ValueError,'requires rocks and items'):
                bridge.source(self.root/'invalid-rocks',rocks_star=True,**kwargs)

    def test_teepees_replace_static_once_all_frames_anchors_and_rules(self):
        course=teepee_source();saved=copy.deepcopy(course)
        models=convert.Models(self.root,AssetIds());document=dict(objects=[])
        self.assertEqual(convert.scenery(course,document,models,.1),6)
        self.assertEqual(len(document['objects']),6)
        self.assertTrue(all(o['class_id']==0x1018 for o in document['objects']))
        decoded=convert.art.decode_scenery(course)
        for index,obj in enumerate(document['objects']):
            props={p['key']:p['value'] for p in obj['properties']}
            self.assertEqual((props['mksc:controller'],props['source:variant']),('teepees',0))
            self.assertEqual(props['source:placement-order'],index)
            self.assertEqual(props['source:checkpoint'],36)
            self.assertEqual(props['source:x-q16'],(500+index)*65536+17)
            self.assertEqual(props['source:y-q16'],(600+index)*65536+31)
            self.assertEqual(props['source:frame-count'],11)
            self.assertEqual(props['source:affine-q8'],224)
            self.assertEqual(obj['scale'],[.1]*3)
            for frame,bank in enumerate(decoded.banks):
                ref=props[f'source:frame:{frame}'];self.assertEqual(ref['kind'],96)
                mesh=models.meshes[ref['id']];image=bank.image
                self.assertEqual(mesh['vertices'][0][:3],[image.anchor_x,0,-(image.anchor_y+image.height)])
                self.assertEqual(mesh['vertices'][2][:3],[image.anchor_x+image.width,0,-image.anchor_y])
        self.assertEqual(course,saved)
        for bad in (teepee_source(variant=1),teepee_source(checkpoint=128)):
            with self.assertRaisesRegex(ValueError,'teepees: invalid variant/checkpoint'):
                convert.scenery(bad,dict(objects=[]),models,.1)
        with patch.object(convert.track_format,'load_pack',return_value={(13,93):motion()}), \
             patch.object(convert.source_format,'read_course',return_value=course), \
             patch.object(convert,'convert') as emitted:
            convert.generate(self.root/'unused',self.root/'teepee-source',courses=[COURSE])
        self.assertIsNotNone(emitted.call_args.kwargs['actor_profile'])
        self.assertTrue((self.root/'teepee-source/source-actors.json').exists())
        with patch.object(convert.track_format,'load_pack',return_value={}), \
             patch.object(convert.source_format,'read_course',return_value=course):
            with self.assertRaisesRegex(ValueError,'no imported MOT1'):
                convert.generate(self.root/'unused',self.root/'teepee-no-rules',courses=[COURSE])

    def test_teepee_fixture_dense_original_cells_cup_star_battle_and_mixed(self):
        spec=importlib.util.spec_from_file_location('teepee_bridge',ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        for star in (False,True):
            authored=bridge.source(self.root/f'teepee-{star}',cup=True,items=True,
                teepees=True,teepees_star=star,coins=True)
            self.assertTrue((authored/'source-actors.json').exists())
            self.assertFalse((authored/'actor-affine.json').exists())
            models=json.loads((authored/'models.json').read_text())
            cells={mesh['id']:mesh for mesh in models['meshes'] if 140<=mesh['id']<=150}
            self.assertEqual(len(cells),11)
            self.assertTrue(all(min(v[2] for v in mesh['vertices'])==0 for mesh in cells.values()))
            for path in (authored/'courses3d').glob('*/course.json'):
                doc=json.loads(path.read_text());tents=[o for o in doc['objects'] if o['class_id']==0x1018]
                self.assertEqual(len(tents),1);tent=tents[0]
                self.assertEqual(tent['position'],[19.5,0,-1] if star else [20,0,-6])
                props={p['key']:p['value'] for p in tent['properties']}
                self.assertEqual(props['source:frame-count'],11)
                self.assertEqual([props[f'source:frame:{i}']['id'] for i in range(11)],list(range(140,151)))
                self.assertEqual(props['source:placement-order'],0)
                if star:
                    self.assertEqual(doc['grid'][7]['position'],[22,.2,-28.5])
                    self.assertEqual([g['position'] for g in doc['grid'][:7]],
                                     [[-22+(i%2)*4,.2,-12-(i//2)*1.5] for i in range(7)])
                    self.assertTrue(all(g['quaternion']==[0,1,0,0] for g in doc['grid'][:7]))
                    self.assertEqual(doc['items'][0]['position'],[22,1,-28.5])
                    self.assertEqual(doc['items'][0]['respawn_ticks'],3600)
            folder,archive=mod_kit.build(authored,None,self.root/f'teepee-mods-{star}')
            before=archive.read_bytes();mod_kit.build(authored,None,self.root/f'teepee-mods-{star}')
            self.assertEqual(archive.read_bytes(),before)
        mixed=bridge.source(self.root/'mixed-teepee',items=True,crabs=True,rocks=True,teepees=True)
        doc=json.loads((mixed/'courses3d/bridge/course.json').read_text())
        tent=next(o for o in doc['objects'] if o['class_id']==0x1018)
        props={p['key']:p['value'] for p in tent['properties']}
        self.assertEqual(props['source:placement-order'],5)
        self.assertEqual(tent['position'],[-20,0,-16])
        arena=bridge.source(self.root/'teepee-arena',items=True,battle=True,teepees=True)
        doc=json.loads((arena/'courses3d/bridge/course.json').read_text())
        self.assertEqual(next(o['position'] for o in doc['objects'] if o['class_id']==0x1018),[-7.1,0,-4])
        with self.assertRaisesRegex(ValueError,'teepees requires items'):
            bridge.source(self.root/'bad-teepee',teepees=True)
        with self.assertRaisesRegex(ValueError,'teepees_star requires teepees and items'):
            bridge.source(self.root/'bad-teepee-star',teepees_star=True,items=True)

    def test_falling_star_variants_native_anchors_and_explicit_rules(self):
        course=falling_star_source();saved=copy.deepcopy(course)
        models=convert.Models(self.root,AssetIds());doc=dict(objects=[])
        self.assertEqual(convert.actors(course,doc,models,.1),6)
        stars=[o for o in doc['objects'] if o['class_id']==0x1011]
        self.assertEqual(len(stars),6)
        family=next(f for f in convert.art.decode_actors(course) if f.family=='falling_stars')
        for index,obj in enumerate(stars):
            props={p['key']:p['value'] for p in obj['properties']}
            self.assertEqual(props['mksc:controller'],'falling_stars')
            self.assertEqual(props['source:variant'],index)
            self.assertEqual(props['source:x-q16'],(500+index)*65536+17)
            self.assertEqual(props['source:checkpoint'],17+index)
            self.assertEqual(props['source:placement-order'],index+1)
            self.assertEqual(props['source:frame-count'],5)
            for frame,bank in enumerate(family.banks):
                ref=props[f'source:frame:{frame}'];image=bank.image
                mesh=models.meshes[ref['id']]
                self.assertEqual(mesh['vertices'][0][:3],[image.anchor_x,0,-image.anchor_y-image.height])
                self.assertEqual(mesh['vertices'][2][:3],[image.anchor_x+image.width,0,-image.anchor_y])
        self.assertEqual(course,saved)
        with self.assertRaisesRegex(ValueError,'variant'):
            convert.actors(falling_star_source(variant=6),dict(objects=[]),models,.1)
        with patch.object(convert.track_format,'load_pack',return_value={(13,93):motion()}), \
             patch.object(convert.source_format,'read_course',return_value=course), \
             patch.object(convert,'convert') as emitted:
            convert.generate(self.root/'unused',self.root/'falling-source',courses=[COURSE])
        self.assertIsNotNone(emitted.call_args.kwargs['actor_profile'])
        self.assertTrue((self.root/'falling-source/source-actors.json').exists())

    def test_falling_star_original_fixture_frames_modes_and_reproducibility(self):
        spec=importlib.util.spec_from_file_location('falling_bridge',ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        for star in (False,True):
            authored=bridge.source(self.root/f'falling-{star}',cup=True,items=True,coins=True,
                falling_stars=True,falling_stars_star=star)
            models=json.loads((authored/'models.json').read_text())
            self.assertEqual([m['id'] for m in models['meshes'] if 160<=m['id']<=164],list(range(160,165)))
            for path in (authored/'courses3d').glob('*/course.json'):
                doc=json.loads(path.read_text());stars=[o for o in doc['objects'] if o['class_id']==0x1011]
                self.assertEqual(len(stars),2)
                self.assertEqual(stars[0]['position'],[19.5,.4,.8] if star else [20,0,-1])
                for index,obj in enumerate(stars):
                    props={p['key']:p['value'] for p in obj['properties']}
                    self.assertEqual(props['source:variant'],0 if index==0 else 3)
                    self.assertEqual(props['source:placement-order'],index)
                    self.assertEqual([props[f'source:frame:{i}']['id'] for i in range(5)],list(range(160,165)))
                if star:
                    self.assertEqual(doc['grid'][7]['position'],[22,.2,-28.5])
                    self.assertTrue(all(g['position'][0]<0 for g in doc['grid'][:7]))
                    self.assertEqual(doc['items'][0]['position'],[22,1,-28.5])
            folder,archive=mod_kit.build(authored,None,self.root/f'falling-mods-{star}')
            before=archive.read_bytes();mod_kit.build(authored,None,self.root/f'falling-mods-{star}')
            self.assertEqual(archive.read_bytes(),before)
        mixed=bridge.source(self.root/'mixed-falling',items=True,teepees=True,falling_stars=True)
        doc=json.loads((mixed/'courses3d/bridge/course.json').read_text())
        orders=[{p['key']:p['value'] for p in o.get('properties',[])} for o in doc['objects']]
        self.assertEqual([p['source:placement-order'] for p in orders if 'source:placement-order' in p],[0,1,2])
        with self.assertRaisesRegex(ValueError,'falling_stars requires items'):
            bridge.source(self.root/'bad-falling',falling_stars=True)
        with self.assertRaisesRegex(ValueError,'falling_stars_star requires falling_stars and items'):
            bridge.source(self.root/'bad-falling-star',items=True,falling_stars_star=True)

    def test_thundercloud_complete_phase_frames_source_anchors_and_rules(self):
        course=thundercloud_source();saved=copy.deepcopy(course)
        models=convert.Models(self.root,AssetIds());doc=dict(objects=[])
        self.assertEqual(convert.actors(course,doc,models,.1),4)
        family=next(f for f in convert.art.decode_actors(course) if f.family=='thunderclouds')
        self.assertEqual(family.palette_phases,9)
        clouds=[o for o in doc['objects'] if o['class_id']==0x1010]
        self.assertEqual(len(clouds),4)
        for index,obj in enumerate(clouds):
            props={p['key']:p['value'] for p in obj['properties']}
            self.assertEqual(props['mksc:controller'],'thunderclouds')
            self.assertEqual(props['source:variant'],index)
            self.assertEqual(props['source:checkpoint'],11+index)
            self.assertEqual(props['source:placement-order'],index+1)
            self.assertEqual(props['source:frame-count'],72)
            self.assertEqual(props['source:x-q16'],(500+index)*65536+17)
            self.assertEqual(obj['model'],props['source:frame:4'])
            for frame,bank in enumerate(family.banks):
                image=bank.image;ref=props[f'source:frame:{frame}'];mesh=models.meshes[ref['id']]
                self.assertEqual(mesh['vertices'][0][:3],[image.anchor_x,0,-image.anchor_y-image.height])
                self.assertEqual(mesh['vertices'][2][:3],[image.anchor_x+image.width,0,-image.anchor_y])
        self.assertEqual(course,saved)
        self.assertEqual(len(models.meshes),72)
        with self.assertRaisesRegex(ValueError,'variant'):
            convert.actors(thundercloud_source(variant=4),dict(objects=[]),models,.1)
        with patch.object(convert.track_format,'load_pack',return_value={(13,93):motion()}), \
             patch.object(convert.source_format,'read_course',return_value=course), \
             patch.object(convert,'convert') as emitted:
            convert.generate(self.root/'unused',self.root/'cloud-source',courses=[COURSE])
        self.assertIsNotNone(emitted.call_args.kwargs['actor_profile'])
        self.assertTrue((self.root/'cloud-source/source-actors.json').exists())

    def test_thundercloud_original_phase_fixture_reproducible_and_mixed(self):
        spec=importlib.util.spec_from_file_location('cloud_bridge',ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        for star in (False,True):
            authored=bridge.source(self.root/f'cloud-{star}',cup=True,items=True,coins=True,
                thunderclouds=True,thunderclouds_star=star)
            models=json.loads((authored/'models.json').read_text())
            cells={m['id']:m for m in models['meshes'] if 180<=m['id']<=251}
            self.assertEqual(set(cells),set(range(180,252)))
            self.assertTrue(all(min(v[2] for v in cells[180+phase*8+3]['vertices'])==0
                                for phase in range(9)))
            for path in (authored/'courses3d').glob('*/course.json'):
                doc=json.loads(path.read_text());clouds=[o for o in doc['objects'] if o['class_id']==0x1010]
                self.assertEqual(len(clouds),4)
                self.assertEqual(clouds[0]['position'],[18.5,10,21] if star else [20,0,0])
                for index,obj in enumerate(clouds):
                    props={p['key']:p['value'] for p in obj['properties']}
                    self.assertEqual(props['source:variant'],index)
                    self.assertEqual(props['source:placement-order'],index)
                    self.assertEqual(props['source:frame-count'],72)
                    self.assertEqual([props[f'source:frame:{i}']['id'] for i in range(72)],list(range(180,252)))
                    self.assertEqual(obj['model'],dict(kind=96,id=184))
                if star:
                    self.assertEqual(doc['grid'][7]['position'],[22,.2,-28.5])
                    self.assertTrue(all(g['position'][0]<0 for g in doc['grid'][:7]))
            folder,archive=mod_kit.build(authored,None,self.root/f'cloud-mods-{star}')
            before=archive.read_bytes();mod_kit.build(authored,None,self.root/f'cloud-mods-{star}')
            self.assertEqual(archive.read_bytes(),before)
        mixed=bridge.source(self.root/'mixed-cloud',items=True,teepees=True,falling_stars=True,thunderclouds=True)
        doc=json.loads((mixed/'courses3d/bridge/course.json').read_text())
        props=[{p['key']:p['value'] for p in o.get('properties',[])} for o in doc['objects']]
        self.assertEqual([p['source:placement-order'] for p in props if 'source:placement-order' in p],list(range(7)))
        with self.assertRaisesRegex(ValueError,'thunderclouds requires items'):
            bridge.source(self.root/'bad-cloud',thunderclouds=True)
        with self.assertRaisesRegex(ValueError,'thunderclouds_star requires thunderclouds and items'):
            bridge.source(self.root/'bad-cloud-star',items=True,thunderclouds_star=True)

    def test_pier_boo_three_anchored_frames_explicit_profile_and_missing_art(self):
        course=pier_boo_source();saved=copy.deepcopy(course)
        images=convert.art.decode_pier_haunt(haunt_effect())
        models=convert.Models(self.root,AssetIds(),images);document=dict(objects=[])
        self.assertEqual(convert.scenery(course,document,models,.1),1)
        obj=document['objects'][0];props={p['key']:p['value'] for p in obj['properties']}
        self.assertEqual(obj['class_id'],0x1008)
        self.assertEqual(obj['scale'],[.1]*3)
        self.assertEqual(props['mksc:controller'],'pier_boos')
        self.assertEqual(props['source:variant'],0)
        self.assertEqual(props['source:checkpoint'],78)
        self.assertEqual(props['source:placement-order'],15)
        self.assertEqual(props['source:affine-q8'],256)
        self.assertEqual(props['source:frame-count'],3)
        for frame in range(3):
            model=models.meshes[props[f'source:frame:{frame}']['id']]
            self.assertEqual(min(v[2] for v in model['vertices']),-5 if frame==0 else -16)
            self.assertEqual(max(v[2] for v in model['vertices']),27 if frame==0 else 16)
        self.assertEqual(course,saved)
        with self.assertRaisesRegex(ValueError,'missing global FXC1'):
            convert.scenery(course,dict(objects=[]),convert.Models(self.root,AssetIds()),.1)
        with self.assertRaisesRegex(ValueError,'pier_boos: invalid'):
            convert.pier_boo_placements(pier_boo_source(1))
        with self.assertRaisesRegex(ValueError,'pier_boos: invalid'):
            convert.pier_boo_placements(pier_boo_source(checkpoint=128))
        with patch.object(convert.track_format,'load_pack',return_value={(13,93):motion(),(19,1):haunt_effect()}), \
             patch.object(convert.source_format,'read_course',return_value=course), \
             patch.object(convert,'convert') as emitted:
            convert.generate(self.root/'unused',self.root/'pier-source',courses=[COURSE])
        self.assertIsNotNone(emitted.call_args.kwargs['actor_profile'])
        self.assertTrue((self.root/'pier-source/source-actors.json').exists())

    def test_pier_boo_original_fixture_three_cells_seed_and_reproducibility(self):
        spec=importlib.util.spec_from_file_location('pier_bridge',ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        for star,battle in ((False,False),(True,False),(False,True)):
            authored=bridge.source(self.root/f'pier-{star}-{battle}',cup=not battle,battle=battle,
                                  items=True,coins=True,pier_boos=True,pier_boos_star=star)
            models=json.loads((authored/'models.json').read_text())
            cells={m['id']:m for m in models['meshes'] if 290<=m['id']<=292}
            self.assertEqual(len(cells),3)
            for frame in range(3):
                self.assertEqual(min(v[2] for v in cells[290+frame]['vertices']),-5 if frame==0 else -16)
                self.assertEqual(max(v[2] for v in cells[290+frame]['vertices']),27 if frame==0 else 16)
            self.assertEqual([[v[0],v[2]] for v in cells[292]['vertices']],
                             [[-v[0],v[2]] for v in cells[291]['vertices']])
            for path in (authored/'courses3d').glob('*/course.json'):
                doc=json.loads(path.read_text());actors=[o for o in doc['objects'] if o['class_id']==0x1008]
                self.assertEqual(len(actors),1)
                self.assertEqual(actors[0]['position'],[-7.1,0,-6] if battle else
                                 [19.5,0,.8] if star else [22,0,-28.5])
                props={p['key']:p['value'] for p in actors[0]['properties']}
                self.assertEqual(props['source:frame-count'],3)
                self.assertEqual(props['source:affine-q8'],256)
                self.assertEqual([props[f'source:frame:{i}']['id'] for i in range(3)],[290,291,292])
                meta={p['key']:p['value'] for p in doc['meta']['properties']}
                self.assertEqual(meta['mksc:actor-visual-seed'],0)
            _,archive=mod_kit.build(authored,None,self.root/f'pier-mod-{star}-{battle}')
            before=archive.read_bytes();mod_kit.build(authored,None,self.root/f'pier-mod-{star}-{battle}')
            self.assertEqual(archive.read_bytes(),before)
        tt=bridge.source(self.root/'pier-tt-stable',items=True,coins=True,pier_boos=True)
        document=json.loads((tt/'courses3d/bridge/course.json').read_text())
        prop=next(o for o in document['objects'] if o['class_id']==0x1008)
        self.assertEqual(prop['position'],[19.6,0,-1])
        with self.assertRaisesRegex(ValueError,'pier_boos requires items'):
            bridge.source(self.root/'bad-pier',pier_boos=True)
        with self.assertRaisesRegex(ValueError,'pier_boos_star requires'):
            bridge.source(self.root/'bad-pier-star',items=True,pier_boos_star=True)

    def test_pier_boo_repel_uses_ordinary_start_star_and_unchanged_grids(self):
        spec=importlib.util.spec_from_file_location('pier_repel_bridge',ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        normal=bridge.source(self.root/'pier-normal-cup',cup=True,items=True,coins=True,pier_boos=True)
        authored=bridge.source(self.root/'pier-repel-cup',cup=True,items=True,coins=True,pier_boos=True,pier_boos_repel=True)
        previous=json.loads((normal/'courses3d/bridge/course.json').read_text())
        document=json.loads((authored/'courses3d/bridge/course.json').read_text())
        self.assertEqual(document['grid'],previous['grid'])
        self.assertEqual(document['objects'],previous['objects'])
        self.assertEqual(document['items'][0]['position'],[22,1,-28.5])
        self.assertEqual(document['items'][1]['position'],[22,1,-20])
        self.assertTrue(all(row['respawn_ticks']==3600 for row in document['items'][:2]))
        rules=json.loads((authored/'items.json').read_text())['profiles'][0]
        self.assertEqual(rules['weights']['player'],[[[0x100a,1]] for _ in range(8)])
        self.assertEqual(rules['weights']['cpu'],[[[0x1007,1]] for _ in range(8)])
        baseline=json.loads((normal/'items.json').read_text())['profiles'][0]
        self.assertEqual(rules['star'],baseline['star'])
        _,archive=mod_kit.build(authored,None,self.root/'pier-repel-mod')
        before=archive.read_bytes();mod_kit.build(authored,None,self.root/'pier-repel-mod')
        self.assertEqual(archive.read_bytes(),before)
        for flags in ({},{'cup':True},{'cup':True,'items':True,'pier_boos':True,'pier_boos_star':True}):
            with self.assertRaisesRegex(ValueError,'pier_boos_repel requires'):
                bridge.source(self.root/'bad-pier-repel',pier_boos_repel=True,**flags)

    def test_kamek_required_controller_exact_native_anchors_and_profile(self):
        course=kamek_source();saved=copy.deepcopy(course)
        models=convert.Models(self.root,AssetIds());document=dict(objects=[])
        self.assertEqual(convert.scenery(course,document,models,.1),1)
        self.assertEqual(len(document['objects']),1)
        obj=document['objects'][0];props={p['key']:p['value'] for p in obj['properties']}
        self.assertEqual(obj['class_id'],0x101b)
        self.assertEqual(obj['scale'],[.1]*3)
        self.assertEqual(props['mksc:controller'],'kamek')
        self.assertEqual(props['source:variant'],0)
        self.assertEqual(props['source:x-q16'],1812*65536)
        self.assertEqual(props['source:y-q16'],852*65536)
        self.assertEqual(props['source:checkpoint'],78)
        self.assertEqual(props['source:placement-order'],15)
        self.assertEqual(props['source:affine-q8'],224)
        self.assertEqual(props['source:frame-count'],2)
        for frame in range(2):
            model=models.meshes[props[f'source:frame:{frame}']['id']]
            self.assertEqual(min(v[2] for v in model['vertices']),-2)
            self.assertEqual(max(v[2] for v in model['vertices']),30)
            self.assertEqual(min(v[0] for v in model['vertices']),-16)
        self.assertEqual(len(convert.kamek_placements(course)),1)
        self.assertEqual(course,saved)
        for bad in (1,65535):
            with self.assertRaisesRegex(ValueError,'kamek: invalid'):
                convert.scenery(kamek_source(bad),dict(objects=[]),models,.1)
        with self.assertRaisesRegex(ValueError,'kamek: invalid'):
            convert.kamek_placements(kamek_source(checkpoint=128))
        with patch.object(convert.track_format,'load_pack',return_value={(13,93):motion()}), \
             patch.object(convert.source_format,'read_course',return_value=course), \
             patch.object(convert,'convert') as emitted:
            convert.generate(self.root/'unused',self.root/'kamek-source',courses=[COURSE])
        self.assertIsNotNone(emitted.call_args.kwargs['actor_profile'])
        self.assertTrue((self.root/'kamek-source/source-actors.json').exists())

    def test_kamek_original_fixture_flags_models_and_reproducibility(self):
        spec=importlib.util.spec_from_file_location('kamek_bridge',ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        for star,battle in ((False,False),(True,False),(False,True)):
            authored=bridge.source(self.root/f'kamek-{star}-{battle}',cup=not battle,battle=battle,
                                  items=True,coins=True,kamek=True,kamek_star=star)
            models=json.loads((authored/'models.json').read_text())
            cells=[m for m in models['meshes'] if 288<=m['id']<=289]
            self.assertEqual(len(cells),2)
            self.assertTrue(all(min(v[2] for v in m['vertices'])==0 for m in cells))
            self.assertNotEqual(cells[0]['vertices'],cells[1]['vertices'])
            self.assertTrue((authored/'source-actors.json').exists())
            for path in (authored/'courses3d').glob('*/course.json'):
                doc=json.loads(path.read_text());actors=[o for o in doc['objects'] if o['class_id']==0x101b]
                self.assertEqual(len(actors),1)
                self.assertEqual(actors[0]['position'],[-9.8,0,-6] if battle else
                                 [21.65625,0,.8] if star else [21.2,0,-1])
                props={p['key']:p['value'] for p in actors[0]['properties']}
                self.assertEqual(props['source:variant'],0)
                self.assertEqual(props['source:placement-order'],0)
                self.assertEqual(props['source:affine-q8'],224)
                self.assertEqual([props[f'source:frame:{i}']['id'] for i in range(2)],[288,289])
                if star:self.assertTrue(all(g['position'][0]<0 for g in doc['grid'][:7]))
            _,archive=mod_kit.build(authored,None,self.root/f'kamek-mod-{star}-{battle}')
            before=archive.read_bytes();mod_kit.build(authored,None,self.root/f'kamek-mod-{star}-{battle}')
            self.assertEqual(archive.read_bytes(),before)
        with self.assertRaisesRegex(ValueError,'kamek requires items'):
            bridge.source(self.root/'bad-kamek',kamek=True)
        with self.assertRaisesRegex(ValueError,'kamek_star requires'):
            bridge.source(self.root/'bad-kamek-star',items=True,kamek_star=True)

    def test_mechakoopa_required_controller_anchors_and_profile(self):
        course=mechakoopa_source();saved=copy.deepcopy(course)
        models=convert.Models(self.root,AssetIds());document=dict(objects=[])
        self.assertEqual(convert.scenery(course,document,models,.1),3)
        self.assertEqual(len(document['objects']),3)
        for index,obj in enumerate(document['objects']):
            props={p['key']:p['value'] for p in obj['properties']}
            self.assertEqual(obj['class_id'],0x101c)
            self.assertEqual(obj['scale'],[.1]*3)
            self.assertEqual(props['mksc:controller'],'mechakoopas')
            self.assertEqual(props['source:variant'],index%2)
            self.assertEqual(props['source:x-q16'],(500+index)*65536+17)
            self.assertEqual(props['source:y-q16'],(600+index)*65536+31)
            self.assertEqual(props['source:checkpoint'],17 if index==0 else index)
            self.assertEqual(props['source:placement-order'],index)
            self.assertEqual(props['source:affine-q8'],224)
            self.assertEqual(props['source:frame-count'],4)
            for frame in range(4):
                model=models.meshes[props[f'source:frame:{frame}']['id']]
                self.assertEqual(min(v[2] for v in model['vertices']),-2 if frame%2==0 else -3)
                self.assertEqual(max(v[2] for v in model['vertices']),30 if frame%2==0 else 29)
                self.assertEqual(min(v[0] for v in model['vertices']),-16)
        self.assertEqual(course,saved)
        for bad in (2,65535):
            with self.assertRaisesRegex(ValueError,'mechakoopas: invalid'):
                convert.scenery(mechakoopa_source(bad),dict(objects=[]),convert.Models(self.root,AssetIds()),.1)
        with self.assertRaisesRegex(ValueError,'mechakoopas: invalid'):
            convert.mechakoopa_placements(mechakoopa_source(checkpoint=128))
        self.assertEqual(len(convert.mechakoopa_placements(course)),3)
        with patch.object(convert.track_format,'load_pack',return_value={(13,93):motion()}), \
             patch.object(convert.source_format,'read_course',return_value=course), \
             patch.object(convert,'convert') as emitted:
            convert.generate(self.root/'unused',self.root/'mecha-source',courses=[COURSE])
        self.assertIsNotNone(emitted.call_args.kwargs['actor_profile'])
        self.assertTrue((self.root/'mecha-source/source-actors.json').exists())
        # No borrowed source dimensions are rewritten by runtime affine sizing.
        self.assertEqual(course,saved)

    def test_mechakoopa_original_fixture_both_variants_and_reproducibility(self):
        spec=importlib.util.spec_from_file_location('mecha_bridge',ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        for star,battle in ((False,False),(True,False),(False,True)):
            authored=bridge.source(self.root/f'mecha-{star}-{battle}',cup=not battle,battle=battle,
                items=True,coins=True,mechakoopas=True,mechakoopas_star=star)
            models=json.loads((authored/'models.json').read_text())
            cells=[m for m in models['meshes'] if 280<=m['id']<=287]
            self.assertEqual(len(cells),8)
            self.assertTrue(all(min(v[2] for v in m['vertices'])==0 for m in cells))
            self.assertTrue((authored/'source-actors.json').exists())
            for path in (authored/'courses3d').glob('*/course.json'):
                doc=json.loads(path.read_text());actors=[o for o in doc['objects'] if o['class_id']==0x101c]
                self.assertEqual(len(actors),2)
                self.assertEqual(actors[0]['position'],[-2.3,0,-4] if battle else
                                 [23.25,.4,.8] if star else [24.4,0,-1])
                self.assertEqual(actors[1]['position'],[8,0,0] if battle else
                                 [-20 if star else -25.6,0,-16])
                for index,obj in enumerate(actors):
                    props={p['key']:p['value'] for p in obj['properties']}
                    self.assertEqual(props['source:variant'],1-index)
                    self.assertEqual(props['source:frame-count'],4)
                    self.assertEqual(props['source:placement-order'],index)
                    self.assertEqual(props['source:affine-q8'],224)
                    self.assertEqual([props[f'source:frame:{i}']['id'] for i in range(4)],
                                     list(range(280+index*4,284+index*4)))
                if star:self.assertTrue(all(g['position'][0]<0 for g in doc['grid'][:7]))
            _,archive=mod_kit.build(authored,None,self.root/f'mecha-mod-{star}-{battle}')
            before=archive.read_bytes();mod_kit.build(authored,None,self.root/f'mecha-mod-{star}-{battle}')
            self.assertEqual(archive.read_bytes(),before)
        with self.assertRaisesRegex(ValueError,'mechakoopas requires items'):
            bridge.source(self.root/'bad-mecha',mechakoopas=True)
        with self.assertRaisesRegex(ValueError,'mechakoopas_star requires'):
            bridge.source(self.root/'bad-mecha-star',items=True,mechakoopas_star=True)

    def test_common_prop_required_raw_codes_frames_affine_and_inert_scenery(self):
        course=common_prop_source();saved=copy.deepcopy(course)
        doc=dict(objects=[]);models=convert.Models(self.root,AssetIds())
        self.assertEqual(convert.scenery(course,doc,models,.1),6)
        self.assertEqual(len(doc['objects']),8)
        self.assertEqual([o['class_id'] for o in doc['objects'][:2]],[0x010001,0x0b0001])
        self.assertEqual(doc['objects'][1]['scale'],[.2]*3)
        placements=convert.art.decode_scenery(course).placements
        decoded=convert.art.decode_scenery(course)
        for obj,placement in zip(doc['objects'][2:],placements[2:]):
            props={p['key']:p['value'] for p in obj['properties']}
            self.assertEqual(obj['class_id'],0x1019)
            self.assertEqual(props['mksc:controller'],'common_props')
            self.assertEqual(props['source:code'],placement.source_code)
            self.assertEqual(props['source:variant'],placement.source_code&65535)
            self.assertEqual(props['source:x-q16'],placement.x_q16)
            self.assertEqual(props['source:checkpoint'],placement.parameter)
            self.assertEqual(props['source:placement-order'],placement.placement_order)
            self.assertEqual(props['source:affine-q8'],placement.affine_q8)
            self.assertEqual(obj['scale'],[.1]*3)
            self.assertEqual(props['source:frame-count'],2 if placement.source_code==0x860000 else 1)
            for frame,index in enumerate(placement.frame_bank_indices):
                image=decoded.banks[index].image;ref=props[f'source:frame:{frame}']
                mesh=models.meshes[ref['id']]
                self.assertEqual(mesh['vertices'][0][:3],[image.anchor_x,0,-image.anchor_y-image.height])
                self.assertEqual(mesh['vertices'][2][:3],[image.anchor_x+image.width,0,-image.anchor_y])
        self.assertEqual(course,saved)
        for code in (0x060002,0x080001,0x0a0008,0x0c000c,0x860001,0x980002):
            with self.assertRaisesRegex(ValueError,'common_props: invalid'):
                convert.scenery(common_prop_source(code=code),dict(objects=[]),models,.1)
        with self.assertRaisesRegex(ValueError,'common_props: invalid'):
            convert.scenery(common_prop_source(checkpoint=128),dict(objects=[]),models,.1)
        with patch.object(convert.track_format,'load_pack',return_value={(13,93):motion()}), \
             patch.object(convert.source_format,'read_course',return_value=course), \
             patch.object(convert,'convert') as emitted:
            convert.generate(self.root/'unused',self.root/'common-source',courses=[COURSE])
        self.assertIsNotNone(emitted.call_args.kwargs['actor_profile'])
        self.assertTrue((self.root/'common-source/source-actors.json').exists())

    def test_common_prop_original_fixture_classes_umbrella_and_mixed_order(self):
        spec=importlib.util.spec_from_file_location('common_bridge',ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        codes=[0x060001,0x080000,0x0a0007,0x0c000b,0x860000,0x980001]
        for star in (False,True):
            authored=bridge.source(self.root/f'common-{star}',cup=True,items=True,coins=True,
                common_props=True,common_props_star=star)
            models=json.loads((authored/'models.json').read_text())
            cells={m['id']:m for m in models['meshes'] if 270<=m['id']<=276}
            self.assertEqual(len(cells),7)
            self.assertGreater(max(v[0] for v in cells[275]['vertices']),
                               max(v[0] for v in cells[274]['vertices']))
            for path in (authored/'courses3d').glob('*/course.json'):
                doc=json.loads(path.read_text());props=[o for o in doc['objects'] if o['class_id']==0x1019]
                self.assertEqual(len(props),6)
                self.assertEqual(props[4]['position'],[19.5,.4,.8] if star else [19.5,0,0])
                for index,obj in enumerate(props):
                    values={p['key']:p['value'] for p in obj['properties']}
                    self.assertEqual(values['source:code'],codes[index])
                    self.assertEqual(values['source:placement-order'],index)
                    self.assertEqual(values['source:affine-q8'],256)
                    self.assertEqual(values['source:frame-count'],2 if index==4 else 1)
                    if index==4:
                        self.assertEqual([values[f'source:frame:{i}']['id'] for i in (0,1)],[274,275])
                if star:
                    self.assertEqual(doc['grid'][7]['position'],[22,.2,-28.5])
                    self.assertTrue(all(g['position'][0]<0 for g in doc['grid'][:7]))
            folder,archive=mod_kit.build(authored,None,self.root/f'common-mods-{star}')
            before=archive.read_bytes();mod_kit.build(authored,None,self.root/f'common-mods-{star}')
            self.assertEqual(archive.read_bytes(),before)
        mixed=bridge.source(self.root/'mixed-common',items=True,teepees=True,falling_stars=True,
                            thunderclouds=True,common_props=True)
        doc=json.loads((mixed/'courses3d/bridge/course.json').read_text())
        props=[{p['key']:p['value'] for p in o.get('properties',[])} for o in doc['objects']]
        self.assertEqual([p['source:placement-order'] for p in props if 'source:placement-order' in p],list(range(13)))
        with self.assertRaisesRegex(ValueError,'common_props requires items'):
            bridge.source(self.root/'bad-common',common_props=True)
        with self.assertRaisesRegex(ValueError,'common_props_star requires common_props and items'):
            bridge.source(self.root/'bad-common-star',items=True,common_props_star=True)

    def test_generate_rules_only_for_crabs_and_is_reproducible(self):
        course=crab_source()
        captured=[]
        with patch.object(convert.track_format,'load_pack',return_value={(13,93):motion()}), \
             patch.object(convert.source_format,'read_course',return_value=course), \
             patch.object(convert,'convert',side_effect=lambda *args,**kw:captured.append(kw)):
            for name in ('one','two'):
                convert.generate(self.root/'unused.pack',self.root/name,courses=[COURSE])
        one=self.root/'one/source-actors.json'
        self.assertEqual(one.read_bytes(),(self.root/'two/source-actors.json').read_bytes())
        record=actor.load(one)['profiles'][0]
        self.assertEqual(record['sine'],profile()['sine'])
        self.assertTrue(all(kw['actor_profile']==record['profile_id'] for kw in captured))
        empty_sections=fixture();empty_sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0))
        empty_sections[28,COURSE]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0))
        empty=source.read_course(empty_sections,COURSE)
        with patch.object(convert.track_format,'load_pack',return_value={}), \
             patch.object(convert.source_format,'read_course',return_value=empty), \
             patch.object(convert,'convert'):
            convert.generate(self.root/'unused.pack',self.root/'empty',courses=[COURSE])
        self.assertFalse((self.root/'empty/source-actors.json').exists())


class ConvertedGpCheckpointTests(unittest.TestCase):
    def setUp(self):
        scratch=ROOT/'.mksc-tools/portable/runs/g54'
        scratch.mkdir(parents=True,exist_ok=True)
        self.temporary=tempfile.TemporaryDirectory(dir=scratch,prefix='gp-checkpoint-')
        self.addCleanup(self.temporary.cleanup)
        self.work=Path(self.temporary.name)
        self.cup=dict(id=65538,courses=[28,40,33,26])
        (self.work/'race').mkdir()
        (self.work/'save/mesh3d').mkdir(parents=True)
        self.log=self.work/'race/race.log'
        self.log.write_text(''.join(f'3D GP race: cup=65538 index={i} course={course}\n'
                                   for i,course in enumerate(self.cup['courses']))+
                            '3D GP save: status=ok updated=1 cup=65538 rank=1 points=36\n'
                            '3D GP complete: cup=65538 rank=1 points=36 podium=2,6,5\n'
                            'Menu tour: PASS\n')
        self.save=self.work/'save/mesh3d/completed.cup'
        saved=bytearray(128);saved[:8]=b'MKSC3CUP'
        struct.pack_into('<5I',saved,8,1,128,65538,0,2)
        struct.pack_into('<4I',saved,32,*self.cup['courses'])
        struct.pack_into('<I',saved,80,36)
        struct.pack_into('<4I',saved,84,1000,2000,3000,4000)
        self.save.write_bytes(saved)

    def checkpoint(self):
        from test_mesh_time_trial_routes import converted_gp_saved_checkpoint
        return converted_gp_saved_checkpoint(self.work,self.cup)

    def test_completed_cup_is_admitted_without_mutating_its_save(self):
        before=self.save.read_bytes()
        self.assertEqual(self.checkpoint(),(self.save,before))
        self.assertEqual(self.save.read_bytes(),before)

    def test_reload_rejects_missing_or_unfinished_race_evidence(self):
        completed=self.log.read_text()
        for missing in completed.splitlines():
            with self.subTest(missing=missing):
                self.log.write_text(completed.replace(missing+'\n',''))
                with self.assertRaises(AssertionError):self.checkpoint()
        self.log.unlink()
        with self.assertRaises(AssertionError):self.checkpoint()

    def test_reload_rejects_stale_or_unqualified_progress(self):
        before=self.save.read_bytes()
        for offset,value in ((16,65539),(24,3),(32,99),(80,35),(84,0),(100,4)):
            with self.subTest(offset=offset,value=value):
                stale=bytearray(before);struct.pack_into('<I',stale,offset,value)
                self.save.write_bytes(stale)
                with self.assertRaises(AssertionError):self.checkpoint()
                self.assertEqual(self.save.read_bytes(),stale)

    def test_reload_retry_discards_only_previous_reload_output(self):
        from test_mesh_time_trial_routes import verify_converted_gp_reload
        from unittest.mock import Mock
        before=self.save.read_bytes();race_log=self.log.read_bytes()
        runner=Mock()
        def run(name,arguments,**common):
            self.assertFalse((self.work/'relaunch').exists())
            self.assertFalse((self.work/'relaunch-frames').exists())
            (self.work/'relaunch/screens').mkdir(parents=True)
            (self.work/'relaunch-frames').mkdir()
            return self.work/name,('3D GP load: cup=65538 status=ok\n'
                '3D renderer: GPU driver=metal\nGPU UI compositor: driver=metal full_canvas_upload=0\n')
        runner.run.side_effect=run
        for attempt in range(2):
            verify_converted_gp_reload(runner,'unused.pack',self.work,'DA',[],{},self.cup,2)
            self.assertEqual(self.save.read_bytes(),before)
            self.assertEqual(self.log.read_bytes(),race_log)
        self.assertEqual(runner.run.call_count,2)


class SourceActorContactOracleTests(unittest.TestCase):
    def test_converted_crabs_require_primary_hit_on_a_retained_placement(self):
        from test_mesh_time_trial_routes import verify_crab_actor_turns
        definition={25:dict(count=7,profile_id=41,revision=7,
                           first=dict(x_q16=0,y_q16=0,variant=0))}
        initial=('3D source actors start: course=25 crabs=7 profile=41 revision=7\n'
                 '3D source actor state: course=25 tick=1 x=32768 y=0 art=0 checksum=0000000000000001\n'
                 '3D source actor state: course=25 tick=6 x=196608 y=0 art=1 checksum=0000000000000002\n')
        contact='3D source actor contact: course=25 tick=2119 hit=1 protected=0 actors=0000000000000040\n'
        verify_crab_actor_turns(initial+contact,{25},definition)
        for invalid in ('',contact.replace('hit=1','hit=2'),
                        contact.replace('course=25','course=26'),
                        contact.replace('0000000000000040','0000000000000000'),
                        contact.replace('0000000000000040','0000000000000080')):
            with self.subTest(contact=invalid),self.assertRaises(AssertionError):
                verify_crab_actor_turns(initial+invalid,{25},definition)


class SourceActorClockOracleTests(unittest.TestCase):
    def test_lava_cycle_requires_an_observed_hide_after_the_zero_height_turn(self):
        from test_mesh_time_trial_routes import verify_lava_actor_turns

        def state(tick,phase,timer,art,flags,height):
            return (f'3D source lava state: course=35 tick={tick} index=0 '
                    f'phase={phase} timer={timer} art={art} flags={flags} '
                    f'x=0 y=0 height={height} vx=0 vy=0 vz=0 checksum=0000000000000001\n')

        initial=state(1,2,64,0,0,0)
        def cycle(start):
            return ''.join(state(start+63-timer,3,timer,int(timer<=31),3,1)
                           for timer in range(63,0,-1))+state(start+63,0,90,1,3,0)

        emerged=initial+cycle(2)
        hidden=state(66,1,89,1,0,0)
        complete=emerged+hidden
        verify_lava_actor_turns(complete,{35},require_contact=False)
        # A race may end on the last collidable turn of a later emergence.
        # That tail is not another proved cycle, but cannot undo the earlier one.
        verify_lava_actor_turns(complete+cycle(156),{35},require_contact=False)
        for invalid in (emerged,
                        emerged+state(67,1,88,1,0,0),
                        emerged+state(66,1,89,1,3,0),
                        complete.replace('timer=32 art=0','timer=32 art=1'),
                        complete.replace('timer=31 art=1','timer=31 art=0'),
                        complete.replace('phase=0 timer=90 art=1 flags=3',
                                         'phase=0 timer=90 art=1 flags=1')):
            with self.subTest(log=invalid),self.assertRaises(AssertionError):
                verify_lava_actor_turns(invalid,{35},require_contact=False)

    def test_lava_decay_refresh_and_exact_primary_recovery(self):
        from test_mesh_time_trial_routes import verify_lava_burn_clock
        contact=[('35','2170','1','0','1','0','1')]
        natural=''.join(f'3D source lava burn: course=35 tick={t} racer=0 remaining={max(0,2420-t)}\n'
                        for t in (2170,2171,2200,2201,2420))
        verify_lava_burn_clock(natural,contact)
        recovery='3D coin state: course=35 tick=2201 racer=0 before=1 after=0 lost=1 causes=2 picked=0000000000000000\n'
        cleared=natural.replace('tick=2201 racer=0 remaining=219','tick=2201 racer=0 remaining=0')
        verify_lava_burn_clock(cleared+recovery,contact)
        for invalid in (cleared,cleared+recovery.replace('racer=0','racer=1'),
                        cleared+recovery.replace('course=35','course=34'),
                        cleared+recovery.replace('tick=2201','tick=2202'),
                        cleared+recovery.replace('causes=2','causes=1'),
                        cleared.replace('remaining=220','remaining=219')+recovery):
            with self.subTest(log=invalid),self.assertRaises(AssertionError):
                verify_lava_burn_clock(invalid,contact)
        # Battle has no coins; the host's burn-clear line names the recovery.
        named='3D source lava burn clear: course=35 tick=2201 racer=0 rescue=1 bomb=0 respawn=0\n'
        verify_lava_burn_clock(cleared+named,contact)
        with self.assertRaises(AssertionError):
            verify_lava_burn_clock(cleared+named.replace('rescue=1','rescue=0'),contact)
        refreshed=('3D source lava burn: course=35 tick=2170 racer=0 remaining=250\n'
                   '3D source lava burn: course=35 tick=2171 racer=0 remaining=249\n'
                   '3D source lava burn: course=35 tick=2172 racer=0 remaining=250\n'
                   '3D source lava burn: course=35 tick=2173 racer=0 remaining=249\n')
        verify_lava_burn_clock(refreshed,contact+[('35','2172','1','0','1','0','1')])


if __name__=='__main__':unittest.main()
