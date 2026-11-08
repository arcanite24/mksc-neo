"""ROM-free compiler/authoring regressions for the C3D1 course contract."""
import copy
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import build_asset_pack
import course_3d_format as course
import mod_kit
import model_format
import png_indexed

ROOT = Path(__file__).resolve().parents[3]
EXAMPLE_SPEC = importlib.util.spec_from_file_location('bridge_example',ROOT/'mods/examples/bridge-course/build.py')
EXAMPLE = importlib.util.module_from_spec(EXAMPLE_SPEC)
EXAMPLE_SPEC.loader.exec_module(EXAMPLE)


def tables(blob):
    count = struct.unpack_from('<I',blob,24)[0]
    result = {}
    for i in range(count):
        tag,offset,rows,stride,flags = struct.unpack_from('<4s4I',blob,64+20*i)
        result[tag.decode()] = (blob[offset:offset+rows*stride],rows,stride,flags,offset)
    return result


class Course3DFormatTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT/'.mksc-tools/portable/runs/t0'
        scratch.mkdir(parents=True,exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=scratch,prefix='compiler-test-')
        self.root = Path(self.temporary.name)
        self.source = EXAMPLE.source(self.root/'bridge-course')
        self.folder = self.source/'courses3d/bridge'
        self.document = course.load(self.folder/'course.json')
        self.gltf = json.loads((self.folder/'scene.gltf').read_text())

    def tearDown(self):
        self.temporary.cleanup()

    def compile(self, document=None):
        return course.compile(self.document if document is None else document,self.folder)

    def save_gltf(self):
        (self.folder/'scene.gltf').write_text(json.dumps(self.gltf))

    def test_default_gravity_and_explicit_author_override(self):
        # META gravity is word21; explicit existing authored values win.
        document=copy.deepcopy(self.document)
        document['meta'].pop('gravity',None)
        def gravity(doc):
            return struct.unpack_from('<i',tables(self.compile(doc)[0][1])['META'][0],84)[0]
        self.assertEqual(gravity(document),course.fixed(course.DEFAULT_GRAVITY))
        document['meta']['gravity']=9.8
        self.assertEqual(gravity(document),course.fixed(9.8))
        document['meta']['gravity']=0
        self.assertEqual(gravity(document),0)

    def test_explicit_authored_launch_is_not_source_adapted(self):
        document=copy.deepcopy(self.document)
        for impulse in (3,6,9):
            document['surfaces'][0].update(type='jump',launch=impulse)
            encoded=tables(self.compile(document)[0][1])['SURF'][0]
            self.assertEqual(struct.unpack_from('<i',encoded,28)[0],impulse*65536)
        self.assertEqual([course.fixed(course.source_launch(k)) for k in (14,2,0)],
                         [365834,731668,1097502])

    def test_example_wire_and_deterministic_modkit_without_base(self):
        sections = self.compile()
        self.assertEqual([(m['kind'],m['content_id']) for m,_ in sections],[(99,65536),(100,65536),(101,65001)])
        blob = sections[0][1]
        self.assertEqual(struct.unpack_from('<4s6I',blob),(b'C3D1',1,len(blob),65536,0,64,len(tables(blob))))
        self.assertEqual(blob[28:64],bytes(36))
        entries = tables(blob)
        expected = {'STRS':1,'META':128,'VERT':32,'TRIS':16,'MATS':48,'COLV':12,'COLT':32,
                    'SURF':48,'CPNT':64,'CPED':4,'RTHD':32,'RTND':32,'RTED':4,'GRID':32}
        last = 64+20*len(entries)
        for tag,(payload,count,stride,flags,offset) in entries.items():
            self.assertEqual(offset%4,0);self.assertGreaterEqual(offset,last)
            last = offset+len(payload)
            if tag in expected:
                self.assertEqual(stride,expected[tag]);self.assertEqual(flags,1)
        self.assertEqual(struct.unpack('<4s9I',sections[1][1]),(b'C3C1',1,1,24,65536,65536,0,15,0,0))
        # Independent authored point (-X, glTF height0, Z-30) -> engine +Y30.
        first = struct.unpack_from('<8i',entries['VERT'][0])
        self.assertEqual(first[:3],(16*65536,30*65536,0))
        self.assertEqual(first[3:6],(0,0,1<<30))
        self.assertEqual(entries['GRID'][1],8)
        self.assertEqual(entries['COLT'][1],entries['TRIS'][1])
        self.assertGreater(max(struct.unpack_from('<i',entries['VERT'][0],i*32+8)[0]
                               for i in range(entries['VERT'][1])),7*65536)
        output = self.root/'mods'
        folder,archive = mod_kit.build(self.source,None,output)
        pack,zip_bytes = (folder/'content.pack').read_bytes(),archive.read_bytes()
        mod_kit.build(self.source,None,output)
        self.assertEqual(pack,(folder/'content.pack').read_bytes())
        self.assertEqual(zip_bytes,archive.read_bytes())
        packed = list(build_asset_pack.read_sections(folder/'content.pack'))
        self.assertEqual(sorted((m['kind'],m['content_id']) for m,_ in packed),
                         [(96,i) for i in range(1,4)] + [(97,i) for i in range(1,7)] +
                         [(98,1),(99,65536),(100,65536),(101,65001),(102,1)])
        self.assertEqual(entries['OBJS'][1],2)
        self.assertEqual(struct.unpack_from('<I',entries['META'][0],88)[0],1)
        # Existing preview-only courses remain valid without a movement profile.
        preview = copy.deepcopy(self.document)
        preview['meta']['physics_profile'] = 0
        self.assertEqual(struct.unpack_from('<I',tables(self.compile(preview)[0][1])['META'][0],88)[0],0)
        self.assertEqual(json.loads((folder/'mod.json').read_text())['targets'],['3d'])

    def test_geometry_welds_shared_primitives_across_materials(self):
        original = tables(self.compile()[0][1])
        primitive = copy.deepcopy(self.gltf['meshes'][0]['primitives'][0])
        primitive['material'] = len(self.gltf['materials'])
        self.gltf['materials'].append(copy.deepcopy(self.gltf['materials'][0]))
        self.gltf['meshes'][0]['primitives'].append(primitive)
        self.save_gltf()
        first = self.compile()[0][1]
        self.assertEqual(first, self.compile()[0][1])
        result = tables(first)
        self.assertEqual(result['VERT'][0], original['VERT'][0])
        self.assertEqual(result['COLV'][0], original['COLV'][0])
        triangle_count = self.gltf['accessors'][primitive['indices']]['count']//3
        self.assertEqual(result['TRIS'][1], original['TRIS'][1]+triangle_count)
        for i in range(triangle_count):
            initial = struct.unpack_from('<4I', original['TRIS'][0], i*16)
            repeated = struct.unpack_from('<4I', result['TRIS'][0], (triangle_count+i)*16)
            self.assertEqual(repeated[:3], initial[:3])
            self.assertEqual(repeated[3], primitive['material'])

    def test_encoded_welding_preserves_seams_and_owner_local_collision(self):
        compiler = course.CourseCompiler(self.document, self.folder)
        compiler.tables['MATS'] = bytearray(2*48)
        compiler.doc['render_nodes'] = ['render']
        compiler.doc['collision_nodes'] = [dict(node='owned', surface=0, owner=0),
                                            dict(node='world', surface=0)]
        compiler.doc['objects'][0].update(position=[4,0,0], quaternion=[0,0,0,1], scale=[1,1,1])
        points = [[0,0,0],[1,0,0],[0,0,1]]
        normals = [[0,1,0]]*3
        uvs = [[0,0]]*3
        triangles = [[0,1,2]]
        def primitives(name):
            if name == 'render':
                yield points, normals, uvs, triangles, 0
                yield points, normals, uvs, triangles, 1
                yield points, [[1,0,0]]*3, uvs, triangles, 0
                yield points, normals, [[1,0],[0,0],[0,0]], triangles, 0
            elif name == 'owned':
                yield [[x+4,y,z] for x,y,z in points], None, None, triangles, 0
            else:
                yield points, None, None, triangles, 0
        compiler.gltf.primitives = primitives
        compiler.geometry()
        self.assertEqual(len(compiler.tables['VERT'])//32, 7)
        tris = [struct.unpack_from('<4I',compiler.tables['TRIS'],i*16) for i in range(4)]
        self.assertEqual(tris, [(0,1,2,0),(0,1,2,1),(3,4,5,0),(6,1,2,0)])
        self.assertEqual(compiler.tables['COLV'], course.pack_words([0,0,0,65536,0,0,0,-65536,0]))
        collisions = [struct.unpack_from('<8I',compiler.tables['COLT'],i*32) for i in range(2)]
        self.assertEqual(collisions[0][:3], (0,1,2))
        self.assertEqual(collisions[1][:3], (0,1,2))
        self.assertEqual((collisions[0][7],collisions[1][7]), (0,course.ABSENT))

    def test_unused_shared_accessor_vertices_do_not_consume_welded_caps(self):
        compiler = course.CourseCompiler(self.document, self.folder)
        compiler.tables['MATS'] = bytearray(48)
        compiler.doc['render_nodes'] = ['render']
        compiler.doc['collision_nodes'] = [dict(node='collision',surface=0)]
        points = [[0,0,0],[1,0,0],[0,0,1]]+[[0,0,0]]*(131072-3)
        # Per-primitive accessors remain bounded. Reusing one at the cap for
        # multiple materials must not count unused or duplicate vertices twice.
        def primitives(_):
            for _ in range(2):
                yield points, None, None, [[0,1,2]], 0
        compiler.gltf.primitives = primitives
        compiler.geometry()
        self.assertEqual(len(compiler.tables['VERT']), 3*32)
        self.assertEqual(len(compiler.tables['COLV']), 3*12)
        self.assertEqual(len(compiler.tables['TRIS']), 2*16)
        self.assertEqual(len(compiler.tables['COLT']), 2*32)

    def test_battle_empty_gates_and_optional_respawn_checkpoint(self):
        arena = copy.deepcopy(self.document)
        arena['meta'].update(modes=['battle'], laps=0)
        arena['checkpoints'] = []
        arena['respawns'] = [dict(position=[0,0,0], checkpoint=None, radius=2)]
        before = copy.deepcopy(arena)
        entries = tables(self.compile(arena)[0][1])
        self.assertEqual(arena, before)
        self.assertEqual(entries['CPNT'][1], 0)
        self.assertEqual(entries['CPED'][1], 0)
        self.assertEqual(entries['CPNT'][3], 1)  # Required directory, empty payload.
        self.assertEqual(struct.unpack_from('<II', entries['META'][0], 12), (16,0))
        self.assertEqual(struct.unpack_from('<I', entries['RESP'][0], 28)[0], course.ABSENT)
        for bad in (0, -1, True, course.ABSENT):
            arena['respawns'][0]['checkpoint'] = bad
            with self.subTest(checkpoint=bad), self.assertRaises(course.CourseError):
                self.compile(arena)
        arena['respawns'][0]['checkpoint'] = None
        arena['meta'].update(modes=['battle','tt'], laps=1)
        with self.assertRaises(course.CourseError):
            self.compile(arena)
        arena['checkpoints'] = copy.deepcopy(self.document['checkpoints'])
        with self.assertRaises(course.CourseError):
            self.compile(arena)  # Dual-mode RESP must bind a real gate.
        arena['respawns'][0]['checkpoint'] = 0
        self.compile(arena)
        arena['meta']['laps'] = 0
        with self.assertRaises(course.CourseError):
            self.compile(arena)
        arena['meta'].update(modes=['battle'], laps=0)
        self.compile(arena)  # Concrete references remain allowed in pure arenas.

    def test_v1_reserved_gameplay_fields_survive_pack_roundtrip(self):
        # These fields are expressible v1 data, not a claim that moving roads,
        # local gravity, section finishes or camera controllers run today.
        baseline = tables(self.compile()[0][1])
        document = copy.deepcopy(self.document)
        document['objects'][0].update(position=[4,2,-6],quaternion=[0,0,0,1],scale=[2,1,4])
        document['collision_nodes'][0].update(owner=0,gravity_zone=0)
        document['zones'][0].update(gravity=[1,-9.8,2],priority=7,event=19,
            properties=[dict(key='example:zone-route',type='route',value=2)])
        document['checkpoints'][0]['flags'] |= 2  # Section-finish flag retained on wire.
        document['cameras'][0].update(position=[1,2,3],target_object=0,route=2,priority=9,
            properties=[dict(key='example:camera-offset',type='vector',value=[3,2,1])])
        document['routes'][2]['properties'] = [dict(key='example:camera-target',type='object',value=0)]
        before = copy.deepcopy(document)
        blob = self.compile(document)[0][1]
        entries = tables(blob)
        self.assertEqual(document,before)
        self.assertEqual(blob,self.compile(document)[0][1])
        self.assertEqual(struct.unpack_from('<I',entries['CPNT'][0],60)[0],
                         document['checkpoints'][0]['flags'])
        self.assertEqual(entries['CPED'][0],baseline['CPED'][0])
        self.assertEqual(entries['RTND'][0],baseline['RTND'][0])
        self.assertEqual(entries['RTED'][0],baseline['RTED'][0])
        owned = 0
        for i in range(entries['COLT'][1]):
            row = struct.unpack_from('<8I',entries['COLT'][0],i*32)
            old = struct.unpack_from('<8I',baseline['COLT'][0],i*32)
            self.assertEqual(row[3:6],old[3:6])
            if row[7] == 0:
                owned += 1
                self.assertEqual(row[6],0)
                for vertex,original in zip(row[:3],old[:3]):
                    local = struct.unpack_from('<3i',entries['COLV'][0],vertex*12)
                    world = struct.unpack_from('<3i',baseline['COLV'][0],original*12)
                    self.assertEqual((2*local[0]+4*65536,4*local[1]+6*65536,
                                      local[2]+2*65536),world)
            else:
                self.assertEqual(row[6:8],old[6:8])
        self.assertGreater(owned,0)
        zone = struct.unpack_from('<20i',entries['ZONE'][0])
        self.assertEqual(zone[10:13],(65536,-2*65536,course.fixed(-9.8)))
        self.assertEqual(zone[15:17],(7,19))
        camera = struct.unpack_from('<16I',entries['CAMR'][0])
        self.assertEqual(camera[:3],(65536,(-3*65536)&0xffffffff,2*65536))
        self.assertEqual(camera[10:14],(2,0,0,9))
        props = {}
        for i in range(entries['PROP'][1]):
            row = struct.unpack_from('<8I',entries['PROP'][0],i*32)
            key = entries['STRS'][0][row[0]:].split(b'\0',1)[0].decode()
            props[key] = row[1:6]
        self.assertEqual(props['example:zone-route'],(6,2,0,0,0))
        self.assertEqual(props['example:camera-target'],(5,0,0,0,0))
        self.assertEqual(props['example:camera-offset'],(7,3*65536,(-65536)&0xffffffff,2*65536,0))
        (self.folder/'course.json').write_text(json.dumps(document))
        folder,_ = mod_kit.build(self.source,None,self.root/'expressive-mod')
        packed = [payload for meta,payload in build_asset_pack.read_sections(folder/'content.pack')
                  if (meta['kind'],meta['content_id']) == (99,document['id'])]
        self.assertEqual(packed,[blob])

    def test_quantization_units_transforms_and_mirrored_winding(self):
        self.assertEqual(course.fixed(.5/65536),1)
        self.assertEqual(course.fixed(-.5/65536),-1)
        self.assertEqual(course.position([1,2,3],'point'),[65536,-196608,131072])
        original = tables(self.compile()[0][1])
        self.gltf['nodes'][0].update(translation=[2,3,4],scale=[-2,1,3])
        self.save_gltf()
        modified = tables(self.compile()[0][1])
        point = struct.unpack_from('<3i',modified['VERT'][0])
        self.assertEqual(point,(-30*65536,86*65536,3*65536))
        self.assertEqual(struct.unpack_from('<3I',original['TRIS'][0]),(0,1,2))
        self.assertEqual(struct.unpack_from('<3I',modified['TRIS'][0]),(0,2,1))
        for bad in (float('inf'),float('nan'),4097):
            with self.assertRaises(course.CourseError):
                course.position([bad,0,0],'position')

    def test_glb_with_embedded_png_matches_external_assets(self):
        expected = self.compile()
        binary = (self.folder/'scene.bin').read_bytes()
        image = (self.folder/'deck.png').read_bytes()
        offset = len(binary)
        self.gltf['buffers'][0] = {'byteLength':len(binary)+len(image)}
        self.gltf['bufferViews'].append({'buffer':0,'byteOffset':offset,'byteLength':len(image)})
        self.gltf['images'][0] = {'bufferView':len(self.gltf['bufferViews'])-1,'mimeType':'image/png'}
        encoded = json.dumps(self.gltf,separators=(',',':')).encode()
        encoded += b' '*((-len(encoded))%4)
        binary += image;binary += bytes((-len(binary))%4)
        body = struct.pack('<II',len(encoded),0x4e4f534a)+encoded+struct.pack('<II',len(binary),0x004e4942)+binary
        (self.folder/'scene.glb').write_bytes(struct.pack('<4sII',b'glTF',2,12+len(body))+body)
        document = copy.deepcopy(self.document);document['scene'] = 'scene.glb'
        self.assertEqual(expected,self.compile(document))

    def test_accessor_indices_unindexed_and_bounds(self):
        for component,code in ((5121,'B'),(5125,'I')):
            gltf = copy.deepcopy(self.gltf)
            data = bytearray((self.folder/'scene.bin').read_bytes())
            primitive = gltf['meshes'][0]['primitives'][0]
            old = gltf['accessors'][primitive['indices']]
            view = gltf['bufferViews'][old['bufferView']]
            indices = struct.unpack_from('<'+'H'*old['count'],data,view['byteOffset'])
            start = len(data);payload = struct.pack('<'+code*len(indices),*indices);data += payload
            gltf['bufferViews'].append({'buffer':0,'byteOffset':start,'byteLength':len(payload)})
            primitive['indices'] = len(gltf['accessors'])
            gltf['accessors'].append({'bufferView':len(gltf['bufferViews'])-1,'componentType':component,'type':'SCALAR','count':len(indices)})
            gltf['buffers'][0]['byteLength'] = len(data)
            (self.folder/'indices.bin').write_bytes(data);gltf['buffers'][0]['uri'] = 'indices.bin'
            (self.folder/'scene.gltf').write_text(json.dumps(gltf))
            self.compile()
        gltf = copy.deepcopy(self.gltf)
        primitive = gltf['meshes'][0]['primitives'][0];del primitive['indices']
        for accessor in primitive['attributes'].values():
            gltf['accessors'][accessor]['count'] = 3
        (self.folder/'scene.gltf').write_text(json.dumps(gltf))
        self.compile()
        for change in (lambda g:g['accessors'][0].update(byteOffset=1),
                       lambda g:g['accessors'][0].update(count=131073),
                       lambda g:g['bufferViews'][0].update(byteStride=4),
                       lambda g:g['accessors'][0].update(sparse={}),
                       lambda g:g['meshes'][0]['primitives'][0].update(mode=5)):
            gltf = copy.deepcopy(self.gltf);change(gltf)
            (self.folder/'scene.gltf').write_text(json.dumps(gltf))
            with self.assertRaises(course.CourseError):self.compile()

    def test_paths_cycles_extensions_and_nonfinite_are_rejected(self):
        for uri in ('../outside.bin','https://example.com/scene.bin','/tmp/scene.bin'):
            self.gltf['buffers'][0]['uri'] = uri;self.save_gltf()
            with self.assertRaises(course.CourseError):self.compile()
        outside = self.root/'outside.bin';outside.write_bytes(b'outside')
        (self.folder/'link.bin').symlink_to(outside)
        self.gltf['buffers'][0]['uri'] = 'link.bin';self.save_gltf()
        with self.assertRaises(course.CourseError):self.compile()
        for change in (lambda g:g.update(extensionsRequired=['UNKNOWN']),
                       lambda g:g['nodes'][0].update(skin=0),
                       lambda g:g['nodes'][0].update(children=[0]),
                       lambda g:g['nodes'][0].update(scale=[0,1,1])):
            gltf = copy.deepcopy(self.gltf);gltf['buffers'][0]['uri'] = 'scene.bin';change(gltf)
            (self.folder/'scene.gltf').write_text(json.dumps(gltf))
            with self.assertRaises(course.CourseError):self.compile()
        (self.folder/'course.json').write_text('{"version":1,"version":1}')
        with self.assertRaisesRegex(course.CourseError,'duplicate'):course.load(self.folder/'course.json')

    def test_gameplay_graphs_flags_and_complete_property_values(self):
        for change in (lambda d:d['checkpoints'][0].update(successors=[]),
                       lambda d:d['routes'][0].update(closed=False),
                       lambda d:d['routes'][0]['nodes'][0].update(successors=[999]),
                       lambda d:d['grid'][0].update(quaternion=[0,0,0,0]),
                       lambda d:d['collision_nodes'][0].update(surface=99),
                       lambda d:d['collision_nodes'][0].update(surface=None),
                       lambda d:d['respawns'][0].update(checkpoint=None),
                       lambda d:d['meta'].update(music={'kind':course.ABSENT,'id':1}),
                       lambda d:d['meta'].update(music={'kind':16,'id':course.ABSENT}),
                       lambda d:d['objects'].append({'class_id':1,'position':[0,0,0],
                           'model':{'kind':course.ABSENT,'id':course.ABSENT}}),
                       lambda d:d['meta']['properties'].append({'key':'test:object','type':'object','value':None}),
                       lambda d:d['meta']['properties'].append({'key':'test:route','type':'route','value':None}),
                       lambda d:d['meta'].update(unknown=1),
                       lambda d:d['meta']['properties'][0].update(required=True),
                       lambda d:d['surfaces'][0].update(restitution=1.5),
                       lambda d:d['checkpoints'][0].update(right=[0,0,1])):
            document = copy.deepcopy(self.document);change(document)
            with self.assertRaises(course.CourseError):self.compile(document)
        document = copy.deepcopy(self.document)
        document['meta']['properties'] += [{'key':'example:integer','type':'integer','value':-5},
            {'key':'example:fixed','type':'fixed','value':1.5},
            {'key':'example:boolean','type':'boolean','value':True},
            {'key':'example:asset','type':'asset','value':{'kind':101,'id':65001}},
            {'key':'example:route','type':'route','value':0},
            {'key':'example:vector','type':'vector','value':[1,2,3]}]
        entries = tables(self.compile(document)[0][1])
        self.assertEqual(entries['PROP'][1],8)
        self.assertEqual(struct.unpack_from('<2I',entries['OBJS'][0],64+56),(7,1))
        self.assertEqual(struct.unpack_from('<I',entries['PROP'][0],32+8)[0],0xfffffffb)

    def test_targets_conflicts_and_original_model_texture_cap(self):
        manifest = json.loads((self.source/'mod.json').read_text())
        for targets in ([],['3d','3d'],['unknown'],['3d',False]):
            with self.assertRaises(mod_kit.KitError):mod_kit.validate_manifest(dict(manifest,targets=targets))
        (self.source/'mod.json').write_text(json.dumps(dict(manifest,targets=['2d'])))
        with self.assertRaisesRegex(mod_kit.KitError,'declare 3d'):mod_kit.build(self.source,None,self.root/'out')
        for width in (1025,4097):
            path = self.folder/'large.png';png_indexed.write(path,width,1,bytes(width))
            with self.assertRaises(model_format.ModelError):model_format._texture(self.folder,'large.png')
            if width == 1025:
                self.assertTrue(model_format._texture(self.folder,'large.png',max_dimension=4096,max_pixels=4000000))
            else:
                with self.assertRaises(model_format.ModelError):model_format._texture(self.folder,'large.png',max_dimension=4096,max_pixels=4000000)

    def test_zero_asset_ids_fail_at_named_source_fields(self):
        cases = [(lambda d:d.update(id=0),'course.id'),
                 (lambda d:d['textures'][0].update(id=0),'texture id')]
        for field in ('preview','minimap','sky'):
            cases.append((lambda d,field=field:d['meta'].update({field:0}),'meta.'+field))
        for field in ('kind','id'):
            value = {'kind':101,'id':65001};value[field]=0
            cases.append((lambda d,value=value:d['meta']['properties'].append(
                {'key':'test:asset','type':'asset','value':value}),'property.asset.'+field))
            cases.append((lambda d,value=value:d['objects'].append(
                {'class_id':1,'position':[0,0,0],'model':value}),'object.model.'+field))
            cases.append((lambda d,value=value:d['meta'].update(music=value),'meta.music.'+field))
        for change,label in cases:
            with self.subTest(field=label):
                document = copy.deepcopy(self.document);change(document)
                with self.assertRaisesRegex(course.CourseError,label):self.compile(document)


if __name__ == '__main__':
    unittest.main()
