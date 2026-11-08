"""ROM-free indexed sunset profile boundaries and overlapping palette lanes."""
import copy
import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ambient_rom as ambient
import build_asset_pack
import convert_course_3d as converter
from course_3d_gltf import AssetIds, Scene
import mod_kit
import track_format
from test_course_3d_source import fixture as source_fixture
from test_course_3d_art import sky_payload
import mesh_palette_format as palette

ROOT = Path(__file__).resolve().parents[3]


def script(partial=False):
    builder = ambient.Builder(bytes(256))
    if partial:
        builder.lane([(2, [(ambient.ROAD, 3, struct.pack('<H', 700))]),
                      (3, [(ambient.ROAD, 4, struct.pack('<H', 800))])])
        events = [(1, [(ambient.ROAD, 3, struct.pack('<H', 900 + i)),
                       (ambient.SKY, 1, struct.pack('<H', 1000 + i))]) for i in range(64)]
    else:
        builder.lane([(2, [(ambient.ROAD, 60, struct.pack('<2H', 700 + i, 800 + i))])
                      for i in range(3)])
        events = [(1, [(ambient.ROAD, 0, struct.pack('<32H', *[100 + i] * 32)),
                       (ambient.ROAD, 48, struct.pack('<16H', *[300 + i] * 16)),
                       (ambient.SKY, 0, struct.pack('<48H', *[500 + i] * 48))])
                  for i in range(64)]
    builder.lane(events, stage=ambient.PRE, controller=ambient.SUNSET)
    builder.patch(ambient.OBJECT, 103, struct.pack('<4H', 11, 12, 13, 14), initial=True)
    return builder.build()


class MeshPaletteFormatTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / '.mksc-tools/portable/runs/t4b'
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=scratch, prefix='palette-format-')
        self.root = Path(self.temp.name)
        (self.root / 'ambient.bin').write_bytes(script())
        (self.root / 'road.idx').write_bytes(bytes((0, 1, 63, 255)))
        (self.root / 'sky.idx').write_bytes(bytes((1, 2)))
        self.record = dict(profile_id=31, revision=2,
            road_seed=list(range(64)), sky_seed=list(range(1000, 1064)), ambient='ambient.bin',
            bindings=[dict(kind=101, id=17, bank=0, width=2, height=2, indices='road.idx'),
                      dict(kind=97, id=20, bank=1, width=2, height=1, indices='sky.idx')])

    def tearDown(self):
        self.temp.cleanup()

    def test_scene_extra_identity_preserves_equal_seed_images_without_changing_defaults(self):
        ids=AssetIds();scene=Scene(self.root/'scene')
        pixels=(0xff223344,)*4
        first=scene.texture(2,2,pixels,ids)
        self.assertEqual(first,scene.texture(2,2,pixels,ids,identity_extra=None))
        expected=struct.pack('<2I4I',2,2,*pixels)
        self.assertEqual(scene.texture_mappings[first]['id'],ids.intern(101,expected))
        a=scene.texture(2,2,pixels,ids,identity_extra=bytes((1,2,1,2)))
        b=scene.texture(2,2,pixels,ids,identity_extra=bytes((1,3,1,3)))
        self.assertEqual(len({first,a,b}),3)
        self.assertEqual((scene.directory/scene.document['images'][a]['uri']).read_bytes(),
                         (scene.directory/scene.document['images'][b]['uri']).read_bytes())
        models=converter.Models(self.root/'scene',ids)
        image=converter.art.PixelImage(2,2,0,0,pixels)
        plain=models.material(image)
        self.assertEqual(plain,models.material(converter.replace(image,indices=None)))
        a=models.material(converter.replace(image,indices=b'\1\2\1\2'))
        b=models.material(converter.replace(image,indices=b'\1\3\1\3'))
        self.assertEqual(len({plain,a,b}),3)

    def test_mod_kit_palette_profile_is_reproducible_and_strictly_3d(self):
        source=self.root/'mod-source';source.mkdir()
        for name in ('ambient.bin','road.idx','sky.idx'):
            (source/name).write_bytes((self.root/name).read_bytes())
        manifest=dict(id='palette-fixture',name='Palette fixture',version='1.0.0',api_version=1,targets=['3d'])
        (source/'mod.json').write_text(json.dumps(manifest))
        (source/'palettes.json').write_text(json.dumps(dict(version=1,profiles=[self.record])))
        folder,archive=mod_kit.build(source,None,self.root/'mods')
        before=archive.read_bytes();pack=(folder/'content.pack').read_bytes()
        section=track_format.load_pack(folder/'content.pack')[107,31]
        self.assertEqual(section[0]['flags'],3)
        self.assertEqual(section[1],palette.normalize(self.record,source))
        mod_kit.build(source,None,self.root/'mods')
        self.assertEqual(archive.read_bytes(),before)
        manifest['targets']=['2d'];(source/'mod.json').write_text(json.dumps(manifest))
        with self.assertRaisesRegex(mod_kit.KitError,'palettes.json: manifest targets must declare 3d'):
            mod_kit.build(source,None,self.root/'mods')
        self.assertEqual(archive.read_bytes(),before)
        self.assertEqual((folder/'content.pack').read_bytes(),pack)
        manifest['targets']=['3d'];(source/'mod.json').write_text(json.dumps(manifest))
        (source/'road.idx').write_bytes(b'\x40'*4)
        with self.assertRaisesRegex(mod_kit.KitError,'palettes.json:.*invalid index plane'):
            mod_kit.build(source,None,self.root/'mods')
        self.assertEqual(archive.read_bytes(),before)
        self.assertEqual((folder/'content.pack').read_bytes(),pack)

    def test_source_sunset_conversion_retains_indices_ambient_and_real_binding_ids(self):
        identity=26;sections=source_fixture(identity=identity)
        header,world=sections[5,identity]
        raster=bytes((1,1,0x80,0x7f,2,0x7f,0x83,0x7f,2,3,3,0x7f,0x7f,0x7f,0x7f,0x7f))
        sections[5,identity]=(header,world[:28]+raster+world[44:])
        sections[28,identity]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0))
        road=bytearray(sections[8,identity][1])
        road[24+64:24+128]=bytes([1])*64
        road[24+128:24+192]=bytes([2])*64
        road[24+0xc0*64:24+0xc1*64]=bytes((1,2))*32
        struct.pack_into('<H',road,24+16384+2*2,0x801f)
        sections[8,identity]=(sections[8,identity][0],bytes(road))
        sky=bytearray(sky_payload(2));sky[-32]=4 # SMR1 controller byte8.
        struct.pack_into('<H',sky,64+64+2*2,31)
        sections[9,identity]=({'version':2},bytes(sky))
        ambient_bytes=script(partial=True)
        sections[84,identity]=({'version':1},ambient_bytes)
        sine=[round(math.sin(i*math.tau/1024)*32768) for i in range(1024)]
        sections[13,7]=({'version':1},struct.pack('<9I1024i',0x31544f4d,1024,64,8,8,8,16,10,4,*sine))
        manifest=[]
        for (kind,ident),(header,payload) in sorted(sections.items()):
            name=f'source-{kind}-{ident}.bin';(self.root/name).write_bytes(payload)
            manifest.append(dict(kind=kind,content_id=ident,version=header['version'],path=name))
        definition=self.root/'pack.json';definition.write_text(json.dumps(dict(sections=manifest)))
        base=self.root/'base.pack';build_asset_pack.build_pack(definition,base);original=base.read_bytes()
        saved=copy.deepcopy(sections);outputs=[]
        for attempt in range(2):
            generated=converter.generate(base,self.root/f'converted-{attempt}',courses=[identity])
            record=json.loads((generated/'palettes.json').read_text())['profiles'][0]
            self.assertEqual((generated/record['ambient']).read_bytes(),ambient_bytes)
            self.assertEqual(record['road_seed'][1],record['road_seed'][2])
            self.assertEqual(record['sky_seed'][1],record['sky_seed'][2])
            self.assertEqual(record['sky_seed'][32:],[0]*32)
            document=json.loads((generated/f'courses3d/{identity}/course.json').read_text())
            props={p['key']:p['value'] for p in document['meta']['properties']}
            self.assertEqual(props['mksc:palette-motion'],dict(kind=107,id=record['profile_id']))
            self.assertIn('source:progress-stage-count',props)
            self.assertFalse(any(str(n).startswith('Sky-') for n in document['render_nodes']))
            self.assertEqual(json.loads((generated/'skies.json').read_text())['profiles'][0]['rules']['controller'],4)
            texture_ids={row['id'] for row in document['textures']}
            models=json.loads((generated/'models.json').read_text())
            material_ids={m['id'] for m in models['materials']}
            self.assertEqual({row['id'] for row in record['bindings'] if row['kind']==101},texture_ids)
            self.assertTrue({row['id'] for row in record['bindings'] if row['kind']==97}<=material_ids)
            self.assertEqual({row['bank'] for row in record['bindings']},{0,1})
            coin_objects=[o for o in document['objects'] if o['class_id']==0x20000001]
            self.assertTrue(coin_objects)
            coin_mesh=next(m for m in models['meshes'] if m['id']==coin_objects[0]['model']['id'])
            coin_material=coin_mesh['materials'][0]
            coin_binding=next(b for b in record['bindings'] if b['kind']==97 and b['id']==coin_material)
            self.assertEqual((coin_binding['bank'],coin_binding['width'],coin_binding['height']),(0,8,8))
            self.assertEqual((generated/coin_binding['indices']).read_bytes(),bytes((1,2))*32)
            used_indices=set()
            for binding in record['bindings']:
                indices=(generated/binding['indices']).read_bytes()
                self.assertEqual(len(indices),binding['width']*binding['height'])
                self.assertTrue(all(v<=63 or v==255 for v in indices))
                if binding['kind']==101:used_indices.update(indices)
            self.assertIn(1,used_indices);self.assertIn(2,used_indices)
            folder,archive=mod_kit.build(generated,None,self.root/f'converted-mod-{attempt}')
            packed=track_format.load_pack(folder/'content.pack')
            self.assertEqual(packed[107,record['profile_id']][1],palette.normalize(record,generated))
            self.assertTrue(all((r['kind'],r['id']) in packed for r in record['bindings']))
            files={p.relative_to(generated).as_posix():p.read_bytes() for p in generated.rglob('*') if p.is_file()}
            outputs.append((files,(folder/'content.pack').read_bytes(),archive.read_bytes()))
        self.assertEqual(outputs[0],outputs[1]);self.assertEqual(base.read_bytes(),original)
        self.assertEqual(sections,saved)

    def test_canonical_wire_indices_seeds_and_input_immutability(self):
        before = copy.deepcopy(self.record)
        wire = palette.normalize(self.record, self.root)
        ambient_bytes = (self.root / 'ambient.bin').read_bytes()
        self.assertEqual(struct.unpack_from('<12I', wire),
            (palette.MAGIC, 1, len(wire), 31, 2, 2, len(ambient_bytes), 6, 0, 0, 0, 0))
        self.assertEqual(struct.unpack_from('<128H', wire, 48),
                         tuple(self.record['road_seed'] + self.record['sky_seed']))
        self.assertEqual(struct.unpack_from('<16I', wire, 304),
                         (97, 20, 1, 2, 1, 0, 2, 0, 101, 17, 0, 2, 2, 2, 4, 0))
        self.assertEqual(wire[-6:], bytes((1, 2, 0, 1, 63, 255)))
        self.assertEqual(wire[368:-6], ambient_bytes)
        self.record['bindings'].reverse()
        self.assertEqual(palette.normalize(self.record, self.root), wire)
        self.record['bindings'].reverse()
        self.assertEqual(self.record, before)

    def test_all_sunset_and_wave_combinations_preserve_unpatched_colors(self):
        variants = palette.ambient_variants(script(), self.record['road_seed'], self.record['sky_seed'])
        self.assertEqual(len(variants), 260)
        road_tail = struct.pack('<16H', *range(32, 48))
        sky_tail = struct.pack('<16H', *range(1048, 1064))
        for pair in variants:
            self.assertEqual(len(pair), 256)
            self.assertEqual(pair[64:96], road_tail)
            self.assertEqual(pair[224:], sky_tail)
        for step in (0, 31, 32, 63):
            values = [struct.unpack('<128H', pair) for pair in variants]
            selected = [v for v in values if v[0] == 100 + step and v[64] == 500 + step]
            self.assertEqual({v[60] for v in selected}, {300 + step, 700, 701, 702})

    def test_timer_closure_retains_partial_writes_across_both_orders(self):
        variants = palette.ambient_variants(script(partial=True),
                                          self.record['road_seed'], self.record['sky_seed'])
        values = [struct.unpack('<128H', pair) for pair in variants]
        for step in (0, 31, 63):
            self.assertTrue(any(v[3] == 900 + step and v[4] == 800
                                and v[65] == 1000 + step for v in values))
            self.assertTrue(any(v[3] == 700 and v[4] == 800
                                and v[65] == 1000 + step for v in values))
        self.assertTrue(all(v[5] == 5 and v[69] == 1005 for v in values))

    def test_partial_timer_combinations_fail_the_real_variant_budget(self):
        builder = ambient.Builder(bytes(256))
        builder.lane([(1, [(ambient.ROAD, 40 + i, struct.pack('<H', 700 + i))])
                      for i in range(4)])
        builder.lane([(1, [(ambient.SKY, 1, struct.pack('<H', 900 + i))])
                      for i in range(64)], stage=ambient.PRE, controller=ambient.SUNSET)
        with self.assertRaisesRegex(ValueError, 'variant capacity'):
            palette.ambient_variants(builder.build(), self.record['road_seed'], self.record['sky_seed'])

    def test_rejects_unsupported_ambient_lanes_and_targets(self):
        data = script()
        for offset, value in ((32, 0), (35, 1), (32 + 32, 1), (32 + 32 + 2, 2)):
            bad = bytearray(data)
            bad[offset] = value
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                palette.ambient_variants(bad, self.record['road_seed'], self.record['sky_seed'])
        header = struct.unpack_from('<8I', data)
        patch_base = 32 + header[2] * 32 + header[3] * 8
        bad = bytearray(data)
        bad[patch_base] = ambient.OBJECT
        with self.assertRaisesRegex(ValueError, 'patch target'):
            palette.ambient_variants(bad, self.record['road_seed'], self.record['sky_seed'])
        with self.assertRaises(ValueError):
            palette.ambient_variants(data[:-1], self.record['road_seed'], self.record['sky_seed'])

    def test_bounds_duplicates_paths_and_bad_planes(self):
        for key, value in [('profile_id', 0), ('revision', True), ('road_seed', [0] * 63),
                           ('sky_seed', [0] * 63 + [65536]), ('ambient', '../ambient.bin')]:
            bad = copy.deepcopy(self.record)
            bad[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                palette.normalize(bad, self.root)
        for key, value in [('kind', 98), ('kind', 102), ('bank', 2), ('id', 0), ('width', 1025),
                           ('height', True), ('indices', '/outside.idx')]:
            bad = copy.deepcopy(self.record)
            bad['bindings'][0][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                palette.normalize(bad, self.root)
        bad = copy.deepcopy(self.record)
        bad['bindings'].append(copy.deepcopy(bad['bindings'][0]))
        with self.assertRaisesRegex(ValueError, 'duplicate target'):
            palette.normalize(bad, self.root)
        for data in (bytes((0, 1, 64, 255)), bytes((0, 1, 63)), bytes((0, 1, 63, 255, 0))):
            (self.root / 'road.idx').write_bytes(data)
            with self.assertRaises(ValueError):
                palette.normalize(self.record, self.root)

    def test_profile_document_duplicate_fields_and_escaped_symlink(self):
        document = dict(version=1, profiles=[self.record])
        compiled = palette.compile(document, self.root)
        self.assertEqual(compiled[0][0], dict(kind=107, content_id=31, version=1, flags=3))
        document['profiles'].append(self.record)
        with self.assertRaisesRegex(ValueError, 'duplicate profile_id'):
            palette.compile(document, self.root)
        path = self.root / 'palettes.json'
        path.write_text('{"version":1,"version":1,"profiles":[]}')
        with self.assertRaisesRegex(ValueError, 'duplicate field'):
            palette.load(path)
        try:
            (self.root / 'outside.idx').symlink_to(self.root.parent / 'missing.idx')
        except (OSError, NotImplementedError):
            return  # Windows may forbid symlink creation; other boundaries ran.
        bad = copy.deepcopy(self.record)
        bad['bindings'][0]['indices'] = 'outside.idx'
        with self.assertRaisesRegex(ValueError, 'escaped file'):
            palette.normalize(bad, self.root)


if __name__ == '__main__':
    unittest.main()
