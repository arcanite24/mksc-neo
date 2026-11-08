"""mod_kit exports stock tables, scaffolds and packages mods without game data."""
import json
import hashlib
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile
import zlib
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_asset_pack
import game_data_mod
import mod_kit
import modern_drift_format
import model_format
import copy
import importlib.util
import png_indexed
import track_format
from make_game_data_fixture import fixture
from test_track_format import COURSE, stock_sections, write_pack
from test_shade_driver_mod import EXAMPLE as SHADE, write_base as shade_base
import make_recovered_pack_fixture

ROOT = Path(__file__).resolve().parents[3]


class ModKitTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def test_default_output_matches_native_platform_data_paths_without_writes(self):
        home = self.root / 'home'
        cases = (
            ('darwin', {}, home / 'Library/Application Support/MKSC Neo/Portable/mods'),
            ('linux', {}, home / '.local/share/MKSC Neo/Portable/mods'),
            ('linux', {'XDG_DATA_HOME': str(self.root / 'xdg')},
             self.root / 'xdg/MKSC Neo/Portable/mods'),
            ('win32', {'APPDATA': str(self.root / 'roaming')},
             self.root / 'roaming/MKSC Neo/Portable/mods'),
            ('win32', {}, home / 'AppData/Roaming/MKSC Neo/Portable/mods'),
        )
        with patch.object(Path, 'home', return_value=home):
            for platform, env, expected in cases:
                with self.subTest(platform=platform, env=env), \
                     patch.object(mod_kit.sys, 'platform', platform), \
                     patch.dict(os.environ, env, clear=True):
                    self.assertEqual(mod_kit.default_mods_directory(), expected)
                    self.assertFalse(expected.exists())
                    os.environ['MKSC_DATA_DIR'] = str(self.root / 'override')
                    self.assertEqual(mod_kit.default_mods_directory(), self.root / 'override/mods')
                    self.assertFalse((self.root / 'override').exists())

    def test_cli_build_default_output_and_explicit_output_are_identical(self):
        source = mod_kit.new_mod('lua', 'default-output', self.root / 'source')
        data = self.root / 'player'
        with patch.dict(os.environ, {'MKSC_DATA_DIR': str(data)}):
            self.assertEqual(mod_kit.main(['build', str(source)]), 0)
            first = (data / 'mods/default-output.zip').read_bytes()
            explicit = self.root / 'explicit'
            self.assertEqual(mod_kit.main(['build', str(source), '--output', str(explicit)]), 0)
            self.assertEqual((explicit / 'default-output.zip').read_bytes(), first)
        self.assertEqual(sorted(p.name for p in (data / 'mods').iterdir()),
                         ['default-output', 'default-output.zip'])

    def test_low_poly_cli_default_keeps_generated_sources_outside_catalog(self):
        path = ROOT / 'mods/examples/low-poly/build.py'
        spec = importlib.util.spec_from_file_location('default_low_poly_example', path)
        example = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(example)
        data = self.root / 'example-player'
        with patch.dict(os.environ, {'MKSC_DATA_DIR': str(data)}), \
             patch.object(sys, 'argv', [str(path)]):
            example.main()
        self.assertTrue((data / 'mods/low-poly/content.pack').is_file())
        self.assertTrue((data / 'mods/low-poly.zip').is_file())
        self.assertFalse((data / 'mods/source').exists())
        self.assertTrue((data / 'source/low-poly/models.json').is_file())

    def test_zip_bytes_do_not_depend_on_host_platform(self):
        source = mod_kit.new_mod('lua', 'portable-zip', self.root / 'source')
        archives = []
        for platform in ('darwin', 'linux', 'win32'):
            with self.subTest(platform=platform), patch.object(sys, 'platform', platform):
                folder, archive = mod_kit.build(source, None, self.root / platform)
                archives.append(archive.read_bytes())
                with zipfile.ZipFile(archive) as bundle:
                    self.assertEqual(bundle.namelist(), sorted(bundle.namelist()))
                    for entry in bundle.infolist():
                        self.assertEqual(entry.create_system, 3)
                        self.assertEqual(entry.external_attr, 0o644 << 16)
                        self.assertEqual(entry.date_time, (2026, 1, 1, 0, 0, 0))
                        self.assertEqual(bundle.read(entry), (folder / entry.filename).read_bytes())
        self.assertEqual(archives[0], archives[1])
        self.assertEqual(archives[0], archives[2])

    def model_source(self):
        source = self.root / 'models'
        source.mkdir()
        (source / 'mod.json').write_text(json.dumps({
            'id': 'models', 'name': 'Models', 'version': '1.0.0', 'api_version': 1}))
        document = {'version': 1,
                    'materials': [{'id': 9, 'argb': '#80ABCDEF', 'wrap_u': True}],
                    'meshes': [{'id': 7, 'materials': [9],
                                'vertices': [[-1, 0, 0, 0, 0], [1, 0, 0, 1, 0], [0, 2, 0, .5, 1]],
                                'triangles': [[0, 1, 2, 0]]}],
                    'bindings': [{'kind': 'driver', 'target': 16, 'pose': 3, 'mesh': 7,
                                  'scale': [1, 2, 1], 'pivot': [-.5, 0, 0]},
                                 {'kind': 'scenery', 'target': 128, 'art_bank': 4, 'mesh': 7}]}
        return source, document

    def test_model_build_embeds_optional_wire_and_custom_binding_ids(self):
        source, document = self.model_source()
        (source / 'models.json').write_text(json.dumps(document))
        folder, archive = mod_kit.build(source, None, self.root / 'out')
        sections = {meta['kind']: (meta, data) for meta, data in
                    build_asset_pack.read_sections(folder / 'content.pack')}
        self.assertEqual(set(sections), {96, 97, 98})
        self.assertTrue(all(meta['flags'] == 0 for meta, _ in sections.values()))
        mesh = sections[96][1]
        self.assertEqual(struct.unpack_from('<4sIII', mesh), (b'MSH1', 3, 1, 1))
        self.assertEqual(struct.unpack_from('<5i', mesh, 20), (-65536, 0, 0, 0, 0))
        self.assertEqual(struct.unpack_from('<4H', mesh, 80), (0, 1, 2, 0))
        self.assertEqual(struct.unpack('<4sIII', sections[97][1]), (b'MTL1', 0x80abcdef, 1, 0))
        self.assertEqual(struct.unpack_from('<5I6i', sections[98][1], 8),
                         (1, 16, 0, 3, 7, 65536, 131072, 65536, -32768, 0, 0))
        self.assertEqual(struct.unpack_from('<5I', sections[98][1], 52), (2, 128, 4, 0, 7))
        with zipfile.ZipFile(archive) as zipped:
            self.assertEqual(zipped.read('content.pack'), (folder / 'content.pack').read_bytes())

    def test_model_overlay_registry_version_reaches_built_pack(self):
        source, document = self.model_source()
        document['binding_mode'] = 'overlay'
        expected = model_format.compile(document, source)
        (source / 'models.json').write_text(json.dumps(document))
        folder, archive = mod_kit.build(source, None, self.root / 'out')
        actual = list(build_asset_pack.read_sections(folder / 'content.pack'))
        self.assertEqual([(m['kind'],m['content_id'],m['version'],m['flags'],data)for m,data in actual],
                         sorted((m['kind'],m['content_id'],m['version'],m['flags'],data)for m,data in expected))
        self.assertEqual(next(m['version']for m,_ in actual if m['kind']==98),2)
        with zipfile.ZipFile(archive) as zipped:
            self.assertEqual(zipped.read('content.pack'),(folder / 'content.pack').read_bytes())

    def test_unbound_model_build_preserves_mesh_bytes_without_replacing_registry(self):
        source, document = self.model_source()
        bound = model_format.compile(document, source)
        document['bindings'] = []
        unbound = model_format.compile(document, source)
        self.assertEqual(unbound, [(meta, data) for meta, data in bound if meta['kind'] != 98])
        self.assertEqual({meta['kind'] for meta, _ in unbound}, {96, 97})
        (source / 'models.json').write_text(json.dumps(document))
        folder, archive = mod_kit.build(source, None, self.root / 'out')
        sections = list(build_asset_pack.read_sections(folder / 'content.pack'))
        self.assertEqual({(meta['kind'], meta['content_id']) for meta, _ in sections}, {(96, 7), (97, 9)})
        self.assertTrue(archive.is_file())
        for field in ('meshes', 'materials'):
            invalid = copy.deepcopy(document)
            invalid[field] = []
            with self.subTest(field=field), self.assertRaisesRegex(model_format.ModelError, field):
                model_format.compile(invalid, source)

    def test_model_zero_manifest_ids_are_rejected(self):
        source, document = self.model_source()
        for index in range(2):
            invalid = copy.deepcopy(document)
            invalid['bindings'][index]['target'] = 0
            with self.assertRaisesRegex(model_format.ModelError, 'target'):
                model_format.compile(invalid, source)

    def test_model_obj_png_compile_and_contained_paths(self):
        source, document = self.model_source()
        (source / 'shape.obj').write_text('v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n'
                                        'vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n'
                                        'f -4/-4 -3/-3 -2/-2\nf -4/-4 -2/-2 -1/-1\n')
        png_indexed.write(source / 'paint.png', 2, 1, bytes([1, 0]), [(1, 2, 3), (4, 5, 6)])
        document['materials'][0]['texture'] = 'paint.png'
        document['meshes'][0] = {'id': 7, 'materials': [9], 'obj': 'shape.obj'}
        payloads = {meta['kind']: data for meta, data in model_format.compile(document, source)}
        self.assertEqual(struct.unpack_from('<III', payloads[96], 4), (4, 2, 1))
        self.assertEqual(struct.unpack_from('<4H', payloads[96], 100), (0, 1, 2, 0))
        self.assertEqual(payloads[97][16:24], b'MKSCIMG1')
        self.assertEqual(struct.unpack_from('<2I', payloads[97], 48), (0xff040506, 0xff010203))
        for unsafe in ('../paint.png', '/tmp/paint.png', 'https://example.test/a.png'):
            document['materials'][0]['texture'] = unsafe
            with self.assertRaises(model_format.ModelError):
                model_format.compile(document, source)
        (self.root / 'outside.png').write_bytes((source / 'paint.png').read_bytes())
        try:
            (source / 'outside.png').symlink_to(self.root / 'outside.png')
        except (OSError, NotImplementedError):
            return  # Windows may not grant symlink creation; traversal checks above still run.
        document['materials'][0]['texture'] = 'outside.png'
        with self.assertRaises(model_format.ModelError):
            model_format.compile(document, source)

    def test_model_png_alpha_and_expansion_bounds(self):
        source, document = self.model_source()
        document['materials'][0]['texture'] = 'texture.png'

        def png(color, pixels, palette=b'', transparency=b'', width=2, height=1):
            data = png_indexed.SIGNATURE + png_indexed._chunk(
                b'IHDR', struct.pack('>IIBBBBB', width, height, 8, color, 0, 0, 0))
            if palette:
                data += png_indexed._chunk(b'PLTE', palette)
            if transparency:
                data += png_indexed._chunk(b'tRNS', transparency)
            data += png_indexed._chunk(b'IDAT', zlib.compress(pixels))
            data += png_indexed._chunk(b'IEND', b'')
            (source / 'texture.png').write_bytes(data)

        png(6, bytes([0, 10, 20, 30, 127, 40, 50, 60, 128]))
        wire = model_format.compile(document, source)[0][1]
        self.assertEqual(struct.unpack_from('<2I', wire, 48), (0x7f0a141e, 0x8028323c))
        png(3, bytes([0, 0, 1]), bytes([1, 2, 3, 4, 5, 6]), bytes([0, 128]))
        wire = model_format.compile(document, source)[0][1]
        self.assertEqual(struct.unpack_from('<2I', wire, 48), (0x00010203, 0x80040506))
        png(4, bytes([0, 50, 0, 60, 255]))
        wire = model_format.compile(document, source)[0][1]
        self.assertEqual(struct.unpack_from('<2I', wire, 48), (0x00323232, 0xff3c3c3c))
        png(2, bytes([0, 1, 2, 3, 4, 5, 6]), transparency=struct.pack('>3H', 1, 2, 3))
        wire = model_format.compile(document, source)[0][1]
        self.assertEqual(struct.unpack_from('<2I', wire, 48), (0x00010203, 0xff040506))
        for color, pixels, palette, width in (
                (6, bytes(100000), b'', 2),  # Small header, compressed expansion bomb.
                (3, bytes([0, 2, 0]), bytes([1, 2, 3]), 2),
                (6, bytes(5), b'', 1025),
                (6, bytes([5, 1, 2, 3, 4]), b'', 1)):
            png(color, pixels, palette, width=width)
            with self.assertRaises(model_format.ModelError):
                model_format.compile(document, source)

    def test_model_rejects_nontriangular_obj_and_duplicate_json_fields(self):
        source, document = self.model_source()
        (source / 'bad.obj').write_text('v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3 4\n')
        document['meshes'][0] = {'id': 7, 'materials': [9], 'obj': 'bad.obj'}
        with self.assertRaisesRegex(model_format.ModelError, 'triangulated'):
            model_format.compile(document, source)
        (source / 'models.json').write_text('{"version":1,"version":2}')
        with self.assertRaisesRegex(model_format.ModelError, 'duplicate JSON'):
            model_format.load(source / 'models.json')

    def test_model_author_errors_are_named_and_do_not_publish_pack(self):
        source, document = self.model_source()
        invalid = []
        case = copy.deepcopy(document); case['meshes'][0]['triangles'][0][0] = 3; invalid.append(case)
        case = copy.deepcopy(document); case['meshes'][0]['vertices'][0][0] = float('nan'); invalid.append(case)
        case = copy.deepcopy(document); case['meshes'][0]['vertices'][0][0] = 10**1000; invalid.append(case)
        case = copy.deepcopy(document); case['bindings'][0]['scale'][1] = 0; invalid.append(case)
        case = copy.deepcopy(document); case['bindings'].append(case['bindings'][0]); invalid.append(case)
        case = copy.deepcopy(document); case['materials'][0]['wrap_u'] = 1; invalid.append(case)
        case = copy.deepcopy(document); case['meshes'][0]['mystery'] = 1; invalid.append(case)
        case = copy.deepcopy(document); case['meshes'][0]['vertices'] *= 683; invalid.append(case)
        for case in invalid:
            with self.subTest(case=case.keys()):
                (source / 'models.json').write_text(json.dumps(case))
                with self.assertRaisesRegex(mod_kit.KitError, 'models.json:'):
                    mod_kit.build(source, None, self.root / 'out')
                self.assertFalse((self.root / 'out/models/content.pack').exists())

    def test_dual_course_example_rebuilds_both_domains_from_same_authored_track(self):
        path = ROOT / 'mods/examples/dual-course/build.py'
        spec = importlib.util.spec_from_file_location('dual_course_example',path)
        example = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(example)
        sections = stock_sections()
        sections += [(kind,32,version,data) for kind,cid,version,data in list(sections)
                     if cid==COURSE and kind in track_format.REQUIRED_KINDS]
        base = write_pack(self.root,sections)
        original = base.read_bytes()
        first,first_zip = example.build(base,self.root / 'first/mods')
        second,second_zip = example.build(base,self.root / 'second/mods')
        self.assertEqual(base.read_bytes(),original)
        self.assertEqual((first / 'content.pack').read_bytes(),(second / 'content.pack').read_bytes())
        self.assertEqual(first_zip.read_bytes(),second_zip.read_bytes())
        authored = self.root / 'first/source/dual-course'
        other = self.root / 'second/source/dual-course'
        def files(folder):
            return {p.relative_to(folder).as_posix():p.read_bytes() for p in folder.rglob('*') if p.is_file()}
        self.assertEqual(files(authored),files(other))
        self.assertEqual(json.loads((authored / 'mod.json').read_text())['targets'],['2d','3d'])
        track = json.loads((authored / 'tracks/oval/track.json').read_text())
        course = json.loads((authored / 'courses3d/128/course.json').read_text())
        self.assertEqual(track['id'],128)
        self.assertEqual(course['id'],track['id'])
        self.assertEqual(course['meta']['name'],track['name'])
        built = track_format.load_pack(first / 'content.pack')
        for row in track_format.compile_track(authored / 'tracks/oval',base):
            self.assertEqual(built[row['kind'],row['content_id']][1],row['data'])
        self.assertIn((99,128),built)
        self.assertIn((100,128),built)
        self.assertTrue(any(kind==102 for kind,cid in built))
        self.assertEqual(struct.unpack_from('<I',built[37,4][1],8)[0],128)
        with zipfile.ZipFile(first_zip) as zipped:
            self.assertEqual(zipped.read('content.pack'),(first / 'content.pack').read_bytes())
        before = files(authored)
        with self.assertRaisesRegex(ValueError,'fresh'):
            example.source(base,authored)
        self.assertEqual(files(authored),before)

    def test_independent_dual_target_overlay_examples_build_without_rom(self):
        examples = []
        for name in ('overlay-kart','overlay-scenery'):
            path = ROOT / 'mods/examples' / name / 'build.py'
            spec = importlib.util.spec_from_file_location(name.replace('-','_'),path)
            example = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(example)
            folder, archive = example.build(self.root / 'overlays/mods')
            authored = self.root / 'overlays/source' / name
            self.assertEqual({p.name for p in authored.iterdir()},{'mod.json','models.json'})
            manifest = json.loads((authored / 'mod.json').read_text())
            self.assertEqual(manifest['targets'],['2d','3d'])
            document = json.loads((authored / 'models.json').read_text())
            self.assertEqual(document['binding_mode'],'overlay')
            sections = list(build_asset_pack.read_sections(folder / 'content.pack'))
            self.assertEqual({m['kind'] for m,_ in sections},{96,97,98})
            self.assertTrue(all(m['version']==(2 if m['kind']==98 else 1) for m,_ in sections))
            expected_color = 0xff000000 | example.COLOR[0]<<16 | example.COLOR[1]<<8 | example.COLOR[2]
            material = next(data for meta,data in sections if meta['kind']==97)
            self.assertEqual(struct.unpack_from('<I',material,4)[0],expected_color)
            with zipfile.ZipFile(archive) as zipped:
                self.assertEqual(zipped.read('content.pack'),(folder / 'content.pack').read_bytes())
            examples.append((example,document,sections))
        first,second = examples
        self.assertTrue({m['content_id']for m,_ in first[2]if m['kind']in(96,97)}.isdisjoint(
                        {m['content_id']for m,_ in second[2]if m['kind']in(96,97)}))
        self.assertEqual(first[1]['bindings'],[dict(kind='driver',target=1,mesh=131001)])
        self.assertEqual(second[1]['bindings'],[dict(kind='scenery',target=65536,art_bank=0,pose=0,
                                                  mesh=131002,scale=[1,1,1],pivot=[0,0,0])])
        self.assertEqual(max(v[2] for v in second[1]['meshes'][0]['vertices']),28)
        self.assertEqual(second[1]['bindings'][0]['scale'],[1,1,1])

    def test_bridge_static_scenery_binding_is_opt_in_and_legacy_bytes_match(self):
        path = ROOT / 'mods/examples/bridge-course/build.py'
        spec = importlib.util.spec_from_file_location('bridge_scenery_example',path)
        example = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(example)
        plain = example.source(self.root / 'plain')
        explicit = example.source(self.root / 'explicit',scenery_binding=False)
        def files(folder):
            return {p.relative_to(folder).as_posix():p.read_bytes() for p in folder.rglob('*') if p.is_file()}
        legacy = files(plain)
        self.assertEqual(legacy,files(explicit))
        digest = hashlib.sha256()
        for name,data in sorted(legacy.items()):
            if Path(name).suffix in ('.json','.gltf'):
                data=data.replace(b'\r\n',b'\n')
            digest.update(name.encode()+b'\0'+data+b'\0')
        # Shipping KPH2 revision10 profile source (CPU class ratios, railed decks) with canonical text newlines; absent/false bytes match per host.
        self.assertEqual(digest.hexdigest(),'6279733999eebec7f3a27e30240d6c9ec88c3b2e086ff1699338000d94f1d132')
        bound = example.source(self.root / 'bound',scenery_binding=True)
        old = json.loads((plain / 'courses3d/bridge/course.json').read_text())
        new = json.loads((bound / 'courses3d/bridge/course.json').read_text())
        tree = new['objects'][0]
        self.assertEqual(tree['properties'],[dict(key='mksc:scenery-art-bank',type='integer',value=0)])
        self.assertEqual(tree['class_id'],1)
        self.assertEqual(tree['model'],dict(kind=96,id=2))
        self.assertEqual(tree['scale'],[.15,.15,.15])
        del tree['properties']
        self.assertEqual(old,new)
        changed = files(bound)
        self.assertEqual({name for name in legacy if legacy[name]!=changed[name]},
                         {'courses3d/bridge/course.json'})
        with self.assertRaisesRegex(ValueError,'non-Battle'):
            example.source(self.root / 'battle',battle=True,scenery_binding=True)

    def test_procedural_low_poly_example_compiles_without_base_or_game_art(self):
        path = ROOT / 'mods/examples/low-poly/build.py'
        spec = importlib.util.spec_from_file_location('low_poly_example', path)
        example = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(example)
        folder, archive = example.build(self.root / 'example', 16, 128, 4)
        self.assertTrue(archive.is_file())
        sections = list(build_asset_pack.read_sections(folder / 'content.pack'))
        self.assertEqual(sum(meta['kind'] == 96 for meta, _ in sections), 2)
        binding = next(data for meta, data in sections if meta['kind'] == 98)
        self.assertEqual(struct.unpack_from('<5I', binding, 8), (1, 16, 0, 0, 1))
        self.assertEqual(struct.unpack_from('<5I', binding, 52), (2, 128, 4, 0, 2))

    def test_export_base_writes_valid_stock_fragments(self):
        (self.root / 'base').mkdir()
        fixture(self.root / 'base')
        out = mod_kit.export_base(self.root / 'base/base.pack', self.root / 'export', [27])
        names = {path.stem for path in (out / 'data').glob('*.json')}
        for expected in ('gp_points', 'gp_items', 'mushroom_rules', 'banana_rules', 'red_shell_rules',
                         'shell_orbit_rules', 'cups', 'kart_physics-1', 'cpu_items-27'):
            self.assertIn(expected, names)
        for path in (out / 'data').glob('*.json'):
            game_data_mod.load(path)
        points = json.loads((out / 'data/gp_points.json').read_text())
        self.assertEqual(points['gp_points']['awards'], [9, 6, 3, 1, 0, 0, 0, 0])
        self.assertEqual(points['gp_rules']['retry_credits'], 3)
        mushroom = json.loads((out / 'data/mushroom_rules.json').read_text())['mushroom_rules']
        self.assertEqual((mushroom['boost_ticks'], mushroom['max_speed_q16']), (50, 0x07e00000))
        cups = json.loads((out / 'data/cups.json').read_text())['cups']
        self.assertEqual(cups[0], {'catalog': 1, 'cup': 0, 'courses': [24, 25, 29, 27]})
        strings = json.loads((out / 'strings.json').read_text())['strings']
        self.assertEqual(strings[0], {'id': 1, 'value': '100CC'})
        items = json.loads((out / 'items.json').read_text())['items']
        self.assertEqual(items[7], {'name': 'mushroom', 'id': 0x1007})

    def test_new_and_build_lua_and_data_mods_as_folder_and_zip(self):
        lua = mod_kit.new_mod('lua', 'final-boost', self.root / 'src', name='Final Boost')
        data = mod_kit.new_mod('data', 'faster', self.root / 'src')
        self.assertTrue((lua / 'main.lua').read_text().startswith('-- Final Boost'))
        folder, archive = mod_kit.build(lua, None, self.root / 'out')
        manifest = json.loads((folder / 'mod.json').read_text())
        self.assertEqual(manifest, {'id': 'final-boost', 'name': 'Final Boost', 'version': '1.0.0',
                                    'api_version': 1, 'script': 'main.lua'})
        with zipfile.ZipFile(archive) as bundle:
            self.assertEqual(sorted(bundle.namelist()), ['main.lua', 'mod.json'])
            self.assertEqual(bundle.read('main.lua'), (lua / 'main.lua').read_bytes())
        folder, archive = mod_kit.build(data, None, self.root / 'out')
        sections = list(build_asset_pack.read_sections(folder / 'content.pack'))
        self.assertEqual([(meta['kind'], meta['content_id']) for meta, _ in sections], [(57, 1)])
        self.assertEqual(json.loads((folder / 'mod.json').read_text())['pack'], 'content.pack')
        first = archive.read_bytes()
        mod_kit.build(data, None, self.root / 'out')
        self.assertEqual(archive.read_bytes(), first)  # reproducible zips

    def test_build_modern_drift_profile_roundtrip(self):
        mod = mod_kit.new_mod('lua', 'drift-rules', self.root / 'src')
        (mod / 'main.lua').unlink()
        authored = json.loads((ROOT / 'portable/data/modern-drift.json').read_text())
        authored['revision'] = 7
        authored['charge_ticks'] = [12, 34, 56]
        authored['boost_ticks'] = [9, 17, 25]
        (mod / 'modern-drift.json').write_text(json.dumps(authored))
        folder, archive = mod_kit.build(mod, None, self.root / 'out')
        sections = list(build_asset_pack.read_sections(folder / 'content.pack'))
        self.assertEqual(len(sections), 1)
        meta, payload = sections[0]
        self.assertEqual((meta['kind'], meta['content_id'], meta['version'], meta['flags']),
                         (95, 1, 1, 3))
        self.assertEqual(len(payload), 48)
        self.assertEqual(struct.unpack_from('<III', payload),
                         (0x3152444D, authored['profile_id'], 7))
        self.assertEqual(struct.unpack_from('<6H', payload, 12), (12, 34, 56, 9, 17, 25))
        self.assertEqual(modern_drift_format.decode(payload), authored)
        manifest = json.loads((folder / 'mod.json').read_text())
        self.assertEqual(manifest['pack'], 'content.pack')
        self.assertNotIn('script', manifest)
        first = archive.read_bytes()
        mod_kit.build(mod, None, self.root / 'out')
        self.assertEqual(archive.read_bytes(), first)
        # A failed rebuild never publishes over the last valid installed output.
        invalid = [b'{', b'\xff', b' ' * 16385,
                   json.dumps({**authored, 'charge_ticks': [20, 20, 30]}).encode(),
                   json.dumps({**authored, 'minimum_speed_ratio_256': True}).encode(),
                   json.dumps({**authored, 'boost_ticks': [1, 2, 8193]}).encode(),
                   (json.dumps(authored)[:-1] + ',"version":1}').encode()]
        for data in invalid:
            with self.subTest(data=data[:80]):
                (mod / 'modern-drift.json').write_bytes(data)
                with self.assertRaisesRegex(mod_kit.KitError, 'modern-drift.json:'):
                    mod_kit.build(mod, None, self.root / 'out')
                self.assertEqual(archive.read_bytes(), first)

    def test_build_rejects_invalid_mods_with_named_fields(self):
        mod = mod_kit.new_mod('lua', 'bad', self.root / 'src')
        cases = [({'version': '1.0'}, 'version'), ({'api_version': 2}, 'api_version'),
                 ({'id': 'Bad'}, 'id'), ({'extra': 1}, 'unknown'), ({'depends': ['bad']}, 'depends')]
        original = json.loads((mod / 'mod.json').read_text())
        for change, message in cases:
            (mod / 'mod.json').write_text(json.dumps({**original, **change}))
            with self.assertRaisesRegex(ValueError, message):
                mod_kit.build(mod, None, self.root / 'out')
        (mod / 'mod.json').write_text(json.dumps(original))
        (mod / 'data.json').write_text('{"gp_points": {"id": 1, "awards": [1]}}')
        with self.assertRaisesRegex(ValueError, 'gp_points.awards'):
            mod_kit.build(mod, None, self.root / 'out')
        with self.assertRaisesRegex(ValueError, 'ids use'):
            mod_kit.new_mod('lua', '../x', self.root / 'src')

    def test_water_template_and_sky_palette_edit_build(self):
        sections = stock_sections()
        sections = [(kind, cid, version,
                     data[:10] + bytes([0x22]) + data[11:] if kind == 6 else data)
                    for kind, cid, version, data in sections]
        pack = write_pack(self.root, sections)
        base = track_format.load_pack(pack)
        self.assertEqual(mod_kit.template_problems(base, COURSE), [])
        mod = mod_kit.new_mod('track', 'water-loop', self.root / 'src', base=pack,
                              source_course=COURSE)
        palette_path = mod / 'tracks/water-loop/sky-palette.png'
        width, height, pixels, palette = png_indexed.read(palette_path)
        palette[5] = (0, 248, 0)
        png_indexed.write(palette_path, width, height, pixels, palette)
        folder, _ = mod_kit.build(mod, pack, self.root / 'out')
        built = track_format.load_pack(folder / 'content.pack')
        self.assertEqual(built[(6, 128)][1][10], 0x22)
        sky = built[(9, 128)][1]
        offset = 64 + struct.unpack_from('<I', sky, 4)[0]
        self.assertEqual(struct.unpack_from('<H', sky, offset + 10)[0], 0x83E0)

    def test_shade_preserves_modern_gp_motion_owners(self):
        modern = self.root / 'modern'
        with patch.object(sys, 'argv', ['make_recovered_pack_fixture.py', str(modern)]):
            make_recovered_pack_fixture.main()
        modern_motion = dict(((meta['kind'], meta['content_id']), (meta, bytes(data)))
                             for meta, data in build_asset_pack.read_sections(modern / 'gp-fixture.pack'))[(13, 1)]
        base_dir = self.root / 'shade-base'
        base_dir.mkdir()
        shade_base(base_dir)
        manifest = json.loads((base_dir / 'base.json').read_text())
        meta, donor_motion = modern_motion
        self.assertEqual((meta['version'], len(donor_motion)), (8, 39464 + 428))
        (base_dir / 'modern-motion.bin').write_bytes(donor_motion)
        for section in manifest['sections']:
            if section['kind'] == 13:
                section.update(version=8, path='modern-motion.bin')
        (base_dir / 'base.json').write_text(json.dumps(manifest))
        build_asset_pack.build_pack(base_dir / 'base.json', base_dir / 'base.pack')
        for name in ('one', 'two'):
            SHADE.build(base_dir / 'base.pack', self.root / name)
        sections = {(meta['kind'], meta['content_id']): (meta, bytes(data)) for meta, data in
                    build_asset_pack.read_sections(self.root / 'one/content.pack')}
        self.assertEqual(sections[(13, 16)][0]['version'], 8)
        self.assertEqual(sections[(13, 16)][1], donor_motion)
        self.assertEqual(sections[(13, 16)][1][-428:], donor_motion[-428:])
        physics = json.loads(sections[(57, 1)][1])['kart_physics'][0]
        self.assertEqual(physics['time_trial']['base_speed_q16'], (300 << 16) * 104 // 100)
        self.assertEqual([row['base_speed_q16'] for row in physics['engines']],
                         [(300 << 16) * 104 // 100] * 3)
        self.assertEqual((self.root / 'one/content.pack').read_bytes(),
                         (self.root / 'two/content.pack').read_bytes())

    def test_track_mod_scaffold_builds_custom_cup(self):
        pack = write_pack(self.root, stock_sections())
        problems = mod_kit.template_problems(track_format.load_pack(pack), COURSE)
        self.assertEqual(problems, [])
        mod = mod_kit.new_mod('track', 'loop-land', self.root / 'src', name='Loop Land', base=pack,
                              source_course=COURSE)
        track = json.loads((mod / 'tracks/loop-land/track.json').read_text())
        self.assertEqual((track['id'], track['name']), (128, 'LOOP LAND'))
        folder, _ = mod_kit.build(mod, pack, self.root / 'out')
        built = track_format.load_pack(folder / 'content.pack')
        self.assertTrue(all((kind, 128) in built for kind in track_format.REQUIRED_KINDS))
        rows = mod_kit.catalog_rows(built, 4)
        self.assertEqual([(r['course'], r['cup'], r['track']) for r in rows], [(128, 0, 0)])
        self.assertIn((15, mod_kit.NAME + 44), built)
        self.assertIn((15, mod_kit.CUP_NAME), built)
        with self.assertRaisesRegex(ValueError, 'needs --base'):
            mod_kit.build(mod, None, self.root / 'out')


if __name__ == '__main__':
    unittest.main()
