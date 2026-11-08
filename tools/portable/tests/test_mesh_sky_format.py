"""MSK1 authoring and malformed-data boundaries, independently of a ROM."""
import copy
import importlib.util
import json
import zipfile
from pathlib import Path
import struct
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import mesh_sky_format as sky
import mod_kit
import track_format
ROOT=Path(__file__).resolve().parents[3]


def profile():
    return dict(profile_id=41,revision=7,entry_delay=True,
        rules=dict(controller=6,yaw_shift=[7,7,8],layer_mask=7,blend_mode=0,blend_a=0,blend_b=0,
            x_bias=[0,-96,0],y_bias=[38,32,38],split_y=-1,split_layer=0,split_yaw_shift=0,
            split_x_bias=0,lightning_hide_layer=2,road_mode=0,layer_source=[0,1,2]),
        layers=[dict(layer=i,logical_width=256+i*256,radius_q16=(1024+i*8)*65536) for i in range(3)],
        sine=[32768 if i==256 else -32768 if i==768 else 0 for i in range(1024)])


class MeshSkyFormatTests(unittest.TestCase):
    def test_wire_retains_signed_rules_and_logical_layer_remapping(self):
        record=profile();before=copy.deepcopy(record);wire=sky.normalize(record)
        self.assertEqual(len(wire),4244)
        self.assertEqual(struct.unpack_from('<8I',wire),(sky.MAGIC,1,4244,41,7,7,1,1024))
        self.assertEqual(struct.unpack_from('<i',wire,32+9*4),(-96,))
        self.assertEqual(struct.unpack_from('<i',wire,32+14*4),(-1,))
        self.assertEqual(struct.unpack_from('<i',wire,148+768*4),(-32768,))
        self.assertEqual(sky.decode(wire),record);self.assertEqual(record,before)
        record['rules']['layer_source']=[2,0,2]
        record['layers'].pop(1)
        self.assertEqual(sky.decode(sky.normalize(record)),record)
        self.assertEqual(struct.unpack_from('<2I',sky.normalize(record),132),(0,0))

    def test_progress_controllers_preserve_the_v1_wire_and_inputs(self):
        original=profile();baseline=sky.normalize(original)
        for controller in (2,4,5):
            record=copy.deepcopy(original);record['rules']['controller']=controller
            before=copy.deepcopy(record)
            payload=sky.normalize(record)
            self.assertEqual(sky.decode(payload),record)
            self.assertEqual(record,before)
            expected=bytearray(baseline);struct.pack_into('<I',expected,32,controller)
            self.assertEqual(payload,expected)

    def test_rejects_unsupported_controller_and_unbounded_fields(self):
        record=profile()
        for field,value in [('profile_id',0),('profile_id',0xffffffff),('revision',0),
                            ('entry_delay',1),('sine',[0]*1023)]:
            bad=copy.deepcopy(record);bad[field]=value
            with self.subTest(field=field,value=str(value)[:20]),self.assertRaises(ValueError):sky.normalize(bad)
        for field,values in [('controller',[8,True]),('yaw_shift',[[0,0,16]]),
                             ('split_y',[-2,160]),('x_bias',[[0,-32769,0]]),
                             ('layer_source',[[0,1,3]]),('blend_a',[17])]:
            for value in values:
                bad=copy.deepcopy(record);bad['rules'][field]=value
                with self.subTest(field=field,value=value),self.assertRaises(ValueError):sky.normalize(bad)
        for field,value in [('logical_width',0),('logical_width',4097),('radius_q16',0),
                            ('radius_q16',2048*65536+1),('layer',3)]:
            bad=copy.deepcopy(record);bad['layers'][0][field]=value
            with self.subTest(field=field,value=value),self.assertRaises(ValueError):sky.normalize(bad)
        bad=copy.deepcopy(record);bad['sine'][-1]=-32769
        with self.assertRaises(ValueError):sky.normalize(bad)
        bad=copy.deepcopy(record);bad['layers'].append(bad['layers'][0])
        with self.assertRaises(ValueError):sky.normalize(bad)
        bad=copy.deepcopy(record);bad['rules']['layer_mask']=1
        with self.assertRaises(ValueError):sky.normalize(bad)

    def test_original_bridge_sky_is_bounded_optional_and_builds_without_items(self):
        spec=importlib.util.spec_from_file_location('sky_bridge',ROOT/'mods/examples/bridge-course/build.py')
        bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
        scratch=ROOT/'.mksc-tools/portable/runs/t4b1';scratch.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            root=Path(directory)
            plain=bridge.source(root/'plain');off=bridge.source(root/'off',sky_motion=False)
            contents=lambda r:{p.relative_to(r):p.read_bytes() for p in r.rglob('*') if p.is_file()}
            self.assertEqual(contents(plain),contents(off))
            authored=bridge.source(root/'authored',sky_motion=True)
            self.assertFalse((authored/'items.json').exists())
            self.assertFalse((authored/'source-actors.json').exists())
            record=sky.load(authored/'skies.json')['profiles'][0]
            self.assertEqual(record['rules']['controller'],1)
            self.assertEqual(record['layers'],[dict(layer=0,logical_width=256,radius_q16=1024*65536)])
            self.assertEqual(len(record['sine']),1024)
            self.assertEqual(record['sine'][256],32768)
            self.assertEqual(record['sine'][768],-32768)
            models=json.loads((authored/'models.json').read_text())
            material=next(m for m in models['materials'] if m['id']==300)
            self.assertTrue(material['wrap_u']);self.assertFalse(material['wrap_v'])
            mesh=next(m for m in models['meshes'] if m['id']==300)
            self.assertEqual(len(mesh['vertices']),130);self.assertEqual(len(mesh['triangles']),128)
            self.assertEqual(mesh['vertices'][0][-2:],[0,1]);self.assertEqual(mesh['vertices'][-1][-2:],[2,0])
            course=json.loads(next((authored/'courses3d').glob('*/course.json')).read_text())
            self.assertIn(dict(key='mksc:sky-motion',type='asset',value=dict(kind=106,id=1)),course['meta']['properties'])
            placed=[o for o in course['objects'] if any(p['key']=='mksc:sky-layer' for p in o.get('properties',[]))]
            self.assertEqual(len(placed),1)
            self.assertEqual(placed[0]['model'],dict(kind=96,id=300))
            self.assertEqual(placed[0]['properties'],[dict(key='mksc:sky-layer',type='integer',value=0)])
            folder,archive=mod_kit.build(authored,None,root/'mods')
            self.assertEqual(track_format.load_pack(folder/'content.pack')[106,1][1],sky.normalize(record))
            before=archive.read_bytes();mod_kit.build(authored,None,root/'mods')
            self.assertEqual(archive.read_bytes(),before)

    def test_mod_kit_reproducible_archive_and_transactional_rejection(self):
        scratch=ROOT/'.mksc-tools/portable/runs/t4b1';scratch.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            root=Path(directory);authored=root/'source';authored.mkdir()
            manifest=dict(id='clock-skies',name='Clock skies',version='1.0.0',api_version=1,targets=['3d'])
            (authored/'mod.json').write_text(json.dumps(manifest))
            document=dict(version=1,profiles=[profile()])
            (authored/'skies.json').write_text(json.dumps(document))
            folder,archive=mod_kit.build(authored,None,root/'mods')
            sections=track_format.load_pack(folder/'content.pack')
            self.assertEqual(sections[106,41][1],sky.normalize(profile()))
            self.assertEqual(sections[106,41][0]['flags'],3)
            before=archive.read_bytes();files={p.name:p.read_bytes() for p in folder.iterdir() if p.is_file()}
            mod_kit.build(authored,None,root/'mods')
            self.assertEqual(archive.read_bytes(),before)
            with zipfile.ZipFile(archive) as zipped:
                self.assertEqual(zipped.read('content.pack'),files['content.pack'])
            for controller in (8,):
                document['profiles'][0]['rules']['controller']=controller
                (authored/'skies.json').write_text(json.dumps(document))
                with self.assertRaisesRegex(mod_kit.KitError,'skies.json:.*controller'):
                    mod_kit.build(authored,None,root/'mods')
                self.assertEqual(archive.read_bytes(),before)
                self.assertEqual({p.name:p.read_bytes() for p in folder.iterdir() if p.is_file()},files)
            (authored/'skies.json').write_text(json.dumps(dict(version=1,profiles=[profile()])))
            manifest['targets']=['2d'];(authored/'mod.json').write_text(json.dumps(manifest))
            with self.assertRaisesRegex(mod_kit.KitError,'skies.json: manifest targets must declare 3d'):
                mod_kit.build(authored,None,root/'mods')
            self.assertEqual(archive.read_bytes(),before)

    def test_malformed_wire_and_duplicate_declarations_fail_before_publication(self):
        wire=sky.normalize(profile())
        for offset,value in [(0,0),(4,2),(8,4240),(12,0),(16,0),(20,8),(24,2),(28,1023),
                             (32,8),(124,0),(128,0),(148,32769)]:
            bad=bytearray(wire);struct.pack_into('<I',bad,offset,value)
            with self.subTest(offset=offset),self.assertRaises(ValueError):sky.decode(bad)
        for bad in (wire[:-1],wire+b'\0'):
            with self.assertRaises(ValueError):sky.decode(bad)
        document=dict(version=1,profiles=[profile()]);before=copy.deepcopy(document)
        self.assertEqual(sky.compile(document),[(dict(kind=106,content_id=41,version=1,flags=3),wire)])
        self.assertEqual(document,before)
        document['profiles'].append(profile())
        with self.assertRaisesRegex(ValueError,'duplicate'):sky.compile(document)
        scratch=ROOT/'.mksc-tools/portable/runs/t4b1';scratch.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            path=Path(directory)/'skies.json';path.write_text('{"version":1,"version":1,"profiles":[]}')
            with self.assertRaisesRegex(ValueError,'duplicate'):sky.load(path)


if __name__=='__main__':unittest.main()
