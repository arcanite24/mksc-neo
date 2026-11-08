"""ROM-free editable height guide admission and resampling tests."""
from dataclasses import replace
import json
from pathlib import Path, PureWindowsPath
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import mesh_kart_format
import course_3d_handpass as hp
import course_3d_format as wire
import course_3d_source as source
from course_3d_gltf import Scene, write_json
from test_course_3d_source import fixture


class Handpass(unittest.TestCase):
    def setUp(self):
        scratch = Path(__file__).resolve().parents[3]/'.mksc-tools/portable/runs/t9'
        scratch.mkdir(parents=True,exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = source.read_course(fixture(),17)

    def guide(self, points=None, faces=None, transform=None):
        if points is None:
            points = [(x,1 if x == 3.2 and y == 3.2 else 0,-y)
                      for y in (0,3.2,6.4) for x in (0,3.2,6.4)]
            faces = []
            for j in range(2):
                for i in range(2):
                    a = j*3+i
                    faces.extend(((a,a+1,a+4),(a,a+4,a+3)))
        scene = Scene(self.root/'guide')
        scene.material()
        scene.mesh('Elevation',[(*p,0,1,0,0,0) for p in points],[(*t,0) for t in faces])
        if transform:
            scene.document['nodes'][0].update(transform)
        scene.save()
        document = dict(version=1,courses=[dict(course_id=17,source_sha256=hp.fingerprint(self.source),
                         metres_per_source_unit=.1,height_guide=dict(scene='guide/scene.gltf',node='Elevation'))])
        write_json(self.root/'handpass.json',document)
        return document

    def load(self):
        return hp.load(self.root/'handpass.json',[self.source],.1)[17]

    def test_mesh_only_manifest_keeps_a_zero_ground_field(self):
        document=self.guide()
        row=document['courses'][0]
        guide=row.pop('height_guide')
        row['mesh_additions']=[dict(scene=guide['scene'],render_nodes=[guide['node']],
                                    collision_nodes=[],surfaces=[])]
        write_json(self.root/'handpass.json',document)
        field=self.load()
        self.assertFalse(any(field.grid))
        self.assertEqual(field.sample_world(3.2,-3.2),0)
        self.assertEqual(len(field.additions),1)

    def test_real_guide_sampling_and_edge_splits(self):
        self.guide()
        field = self.load()
        self.assertEqual(len(field.grid),81)
        self.assertEqual(field.sample_world(3.2,-3.2),1)
        self.assertEqual(field.sample_world(1.6,-1.6),.5)
        self.assertEqual(field.sample_world(4.8,-1.6),0)
        self.assertEqual(field.sample_world(-1,0),0)
        self.assertEqual(field.elevation_grid[40],1)
        splits = field.edge_parameters((0,0,-3.2),(6.4,0,-3.2))
        self.assertEqual((splits[0],splits[-1]),(0.,1.))
        self.assertIn(.5,splits)
        # Encoded corner rounding may introduce tiny additional grade changes.
        for left,right in zip(splits,splits[1:]):
            middle = (left+right)/2
            expected = (field.sample_world(left*6.4,-3.2)+field.sample_world(right*6.4,-3.2))/2
            self.assertAlmostEqual(field.sample_world(middle*6.4,-3.2),expected,delta=2/65536)
        self.assertEqual(field.edge_parameters((0,0,0),(6.4,0,0)),(0.,1.))
        self.assertEqual(field,self.load())

    def test_transformed_static_node(self):
        self.guide(transform={'translation':[6.4,0,0], 'rotation':[0,2**-.5,0,2**-.5]})
        field = self.load()
        self.assertEqual(field.sample_world(3.2, -3.2),1)

    def test_fingerprint_name_and_section_order(self):
        self.assertEqual(hp.fingerprint(self.source),hp.fingerprint(replace(self.source,name='Other')))
        a = source.SourceSection(90,1,b'a')
        b = source.SourceSection(89,2,b'b')
        self.assertEqual(hp.fingerprint(replace(self.source,passthrough=(a,b))),
                         hp.fingerprint(replace(self.source,passthrough=(b,a))))
        self.assertNotEqual(hp.fingerprint(self.source),hp.fingerprint(replace(self.source,song=self.source.song+1)))
        self.assertNotEqual(hp.fingerprint(self.source),hp.fingerprint(replace(self.source,tilemap=self.source.tilemap[::-1])))

    def test_stale_scale_and_closed_manifest(self):
        document = self.guide()
        for key,value,message in (('source_sha256','0'*64,'fingerprint'),
                                  ('metres_per_source_unit',.2,'scale'),('extra',1,'keys')):
            row = document['courses'][0].copy()
            row[key] = value
            write_json(self.root/'handpass.json',dict(version=1,courses=[row]))
            with self.assertRaisesRegex(hp.HandpassError,message):
                self.load()
        write_json(self.root/'handpass.json',dict(version=1,courses=document['courses']*2))
        with self.assertRaisesRegex(hp.HandpassError,'duplicate'):
            self.load()

    def test_projection_overlap_and_ambiguity(self):
        self.guide([(0,0,0),(6.4,0,0),(0,0,-6.4)],[(0,1,2),(0,1,2)])
        with self.assertRaisesRegex(hp.HandpassError,'overlap'):
            self.load()
        self.guide([(0,0,0),(6.4,0,0),(0,0,-6.4),(0,1,0)],[(0,1,2),(3,1,2)])
        with self.assertRaisesRegex(hp.HandpassError,'single-valued'):
            self.load()

    def test_hole_and_nonzero_boundary(self):
        points = [(x,0,-y) for y in (0,1.6,4.8,6.4) for x in (0,1.6,4.8,6.4)]
        faces = []
        for j in range(3):
            for i in range(3):
                if i == j == 1:
                    continue
                a = j*4+i
                faces.extend(((a,a+1,a+5),(a,a+5,a+4)))
        self.guide(points,faces)
        with self.assertRaisesRegex(hp.HandpassError,'holes'):
            self.load()
        self.guide([(0,1,0),(6.4,0,0),(0,0,-6.4)],[(0,1,2)])
        with self.assertRaisesRegex(hp.HandpassError,'boundary'):
            self.load()

    def test_disconnected_patches_and_degenerate(self):
        self.guide([(0,0,0),(1.6,0,0),(0,0,-1.6),(4.8,0,-4.8),(6.4,0,-4.8),(4.8,0,-6.4)],
                   [(0,1,2),(3,4,5)])
        self.assertFalse(any(self.load().grid))
        self.guide([(0,0,0),(1,0,-1),(2,0,-2)],[(0,1,2)])
        with self.assertRaisesRegex(hp.HandpassError,'degenerate'):
            self.load()

    def test_containment_and_flat_rim(self):
        self.guide([(-1,0,0),(1,0,0),(0,0,-1)],[(0,1,2)])
        with self.assertRaisesRegex(hp.HandpassError,'outside'):
            self.load()
        points = [(x,1 if x == 3.2 and y == 3.2 else 0,-y)
                  for y in (.4,3.2,6) for x in (.4,3.2,6)]
        faces = [(0,1,4),(0,4,3),(1,2,5),(1,5,4),(3,4,7),(3,7,6),(4,5,8),(4,8,7)]
        self.guide(points,faces)
        with self.assertRaisesRegex(hp.HandpassError,'flat zero boundary rim'):
            self.load()

    def test_signed_heights_and_nonconforming_seam(self):
        self.guide()
        path = self.root/'guide/scene.gltf'
        document = json.loads(path.read_text())
        document['nodes'][0]['scale'] = [1,-1,1]
        write_json(path,document)
        field = self.load()
        self.assertEqual(field.sample_world(1.6,-1.6),-.5)
        self.guide([(0,0,0),(3.2,0,0),(0,0,-3.2),
                    (1.6,0,0),(3.2,0,-3.2)],[(0,1,2),(3,4,1)])
        with self.assertRaisesRegex(hp.HandpassError,'nonconforming'):
            self.load()

    def test_manifest_and_source_remain_immutable(self):
        document = self.guide()
        original = (self.root/'handpass.json').read_bytes()
        before = hp.fingerprint(self.source)
        self.load()
        self.assertEqual((self.root/'handpass.json').read_bytes(),original)
        self.assertEqual(hp.fingerprint(self.source),before)
        self.assertEqual(json.loads(original),document)
        with self.assertRaises(hp.HandpassError):
            hp.load(self.root/'handpass.json',[self.source],float('nan'))
        with self.assertRaises(hp.HandpassError):
            self.load().sample_world(float('inf'),0)

    def test_windows_resource_directory_keeps_portable_uri_separators(self):
        folder = self.root/'authored/24'
        folder.mkdir(parents=True)
        asset = folder/'mesh.bin'
        asset.write_bytes(b'bounded resource')
        gltf = object.__new__(wire.Gltf)
        gltf.root = self.root
        gltf.resource_directory = PureWindowsPath('authored/24')
        self.assertEqual(gltf.resource_path('mesh.bin'),asset.resolve())
        for unsafe in ('../mesh.bin','sub\\mesh.bin','\\mesh.bin','C:/mesh.bin','/mesh.bin'):
            with self.subTest(uri=unsafe),self.assertRaises(ValueError):
                gltf.resource_path(unsafe)

    def test_caps_and_local_resource_paths(self):
        self.guide()
        for name in ('MAX_GUIDE_TRIANGLES','MAX_BUCKET_REFS','MAX_WORK','MAX_MANIFEST_BYTES'):
            with patch.object(hp,name,1),self.assertRaises(hp.HandpassError):
                self.load()
        document = self.guide()
        document['courses'][0]['height_guide']['scene'] = '../outside.gltf'
        write_json(self.root/'handpass.json',document)
        with self.assertRaises(hp.HandpassError):
            self.load()
        document = self.guide()
        with tempfile.TemporaryDirectory(dir=self.root.parent) as outside:
            target = Path(outside)/'scene.bin'
            target.write_bytes((self.root/'guide/scene.bin').read_bytes())
            (self.root/'guide/scene.bin').unlink()
            (self.root/'guide/scene.bin').symlink_to(target)
            with self.assertRaises(hp.HandpassError):
                self.load()

    def test_export_roundtrip_reproducible_and_fresh(self):
        manifest = hp.export(self.root/'first',[self.source],.1)
        field = hp.load(manifest,[self.source],.1)[17]
        self.assertFalse(any(field.grid))
        self.assertEqual(field.edge_parameters((0,0,0),(6.4,0,-6.4)),(0.,1.))
        hp.export(self.root/'second',[self.source],.1)
        for file in (self.root/'first').rglob('*'):
            if file.is_file():
                self.assertEqual(file.read_bytes(),(self.root/'second'/file.relative_to(self.root/'first')).read_bytes())
        with self.assertRaisesRegex(hp.HandpassError,'fresh'):
            hp.export(self.root/'first',[self.source],.1)
        large = replace(self.source,map_width=256,map_height=256)
        path = hp.export(self.root/'large',[large],.1)
        document = json.loads((path.parent/'17/scene.gltf').read_text())
        positions = document['meshes'][0]['primitives'][0]['attributes']['POSITION']
        self.assertEqual(document['accessors'][positions]['count'],1089)
        self.assertFalse(any(hp.load(path,[large],.1)[17].grid))


class FlowerCupAuthoredData(unittest.TestCase):
    """Original editable fields; owner data is optional and never copied here."""
    root = Path(__file__).resolve().parents[3]/'mods/handpasses/flower-cup'
    course_order = [32,37,38,31]
    x_longitudinal = {32}
    zero_rim = 8
    bounds = {32:(1328,1472,1704,1832), 37:(112,192,224,432),
              38:(448,528,536,664), 31:(360,456,696,800)}

    @classmethod
    def setUpClass(cls):
        cls.document = json.loads((cls.root/'handpass.json').read_text())
        original = source.read_course(fixture(),17)
        cls.fields = {}
        for row in cls.document['courses']:
            # Geometry admission/resampling needs map dimensions, not owner artwork.
            synthetic = replace(original,course_id=row['course_id'],map_width=256,map_height=256)
            cls.fields[row['course_id']] = hp._field(cls.root,row,synthetic,.1)

    def test_editable_grids_zero_rim_and_distinct_gentle_profiles(self):
        self.assertEqual(self.document['version'],1)
        self.assertEqual([r['course_id'] for r in self.document['courses']],self.course_order)
        crests = []
        for row in self.document['courses']:
            cid = row['course_id']
            with self.subTest(course=cid):
                self.assertEqual(set(row),{'course_id','source_sha256','metres_per_source_unit','height_guide'})
                self.assertRegex(row['source_sha256'],r'^[0-9a-f]{64}$')
                self.assertEqual(row['metres_per_source_unit'],.1)
                self.assertEqual(row['height_guide']['node'],'Elevation')
                primitives = list(wire.Gltf(self.root,row['height_guide']['scene']).primitives('Elevation'))
                self.assertEqual(len(primitives),1)
                points,_,_,faces,_ = primitives[0]
                x0,x1,y0,y1 = self.bounds[cid]
                nx,ny = (x1-x0)//8+1,(y1-y0)//8+1
                self.assertEqual((len(points),len(faces)),(nx*ny,2*(nx-1)*(ny-1)))
                for j in range(ny):
                    for i in range(nx):
                        x,h,z = points[j*nx+i]
                        self.assertAlmostEqual(x,(x0+8*i)*.1,places=4)
                        self.assertAlmostEqual(z,-(y0+8*j)*.1,places=4)
                        self.assertGreaterEqual(h,0)
                        if min(i,nx-1-i,j,ny-1-j)*8 <= self.zero_rim:
                            self.assertEqual(h,0,'declared boundary rim must stay flat')
                        # Longitudinal slopes remain gentle; transverse shoulders lie off lanes.
                        if cid in self.x_longitudinal and i+1<nx:
                            self.assertLessEqual(abs(h-points[j*nx+i+1][1])/.8,.25)
                        if cid not in self.x_longitudinal and j+1<ny:
                            self.assertLessEqual(abs(h-points[(j+1)*nx+i][1])/.8,.3)
                crests.append(max(p[1] for p in points))
                f = self.fields[cid]
                self.assertGreater(max(f.grid)/65536,.6)
                self.assertEqual(f.sample_world((x0-8)*.1,-y0*.1),0)
                self.assertEqual(f.sample_world(x1*.1,-(y1+8)*.1),0)
        self.assertEqual(len(set(round(v,3) for v in crests)),4)

    def test_profiles_have_flat_crossgrade_with_body_margin(self):
        for cid,field in self.fields.items():
            x0,x1,y0,y1 = self.bounds[cid]
            # Plateau interior has no banking: the same longitudinal profile spans lanes.
            for along in range((x0 if cid in self.x_longitudinal else y0)+16,
                               (x1 if cid in self.x_longitudinal else y1)-15,4):
                cross0,cross1 = (y0+self.zero_rim+8,y1-self.zero_rim-8) if cid in self.x_longitudinal else (x0+self.zero_rim+8,x1-self.zero_rim-8)
                expected = field.sample_world(along*.1,-cross0*.1) if cid in self.x_longitudinal else field.sample_world(cross0*.1,-along*.1)
                for cross in range(cross0,cross1+1,4):
                    actual = field.sample_world(along*.1,-cross*.1) if cid in self.x_longitudinal else field.sample_world(cross*.1,-along*.1)
                    self.assertAlmostEqual(actual,expected,delta=2/65536)

    def test_owner_source_identity_and_safe_placement(self):
        pack = Path(__file__).resolve().parents[3]/'.mksc-tools/portable/packs/game.pack'
        if not pack.is_file():
            self.skipTest('optional local owner pack absent; ROM-free guide tests still run')
        import track_format
        import course_3d_gameplay as gameplay
        import course_3d_art as art
        import convert_course_3d as converter
        sections = track_format.load_pack(pack)
        sources = [converter.read_conversion_source(sections,row['course_id'])
                   for row in self.document['courses']]
        before = [hp.fingerprint(s) for s in sources]
        original_bytes = [(self.root/row['height_guide']['scene']).read_bytes() for row in self.document['courses']]
        fields = hp.load(self.root/'handpass.json',sources,.1)
        for s in sources:
            f = fields[s.course_id]
            with self.subTest(course=s.course_id):
                # No hazard, jump, panel or wall cell acquires elevation at any corner.
                for y in range(s.map_height):
                    for x in range(s.map_width):
                        i = y*(s.map_width+1)+x
                        if any(f.grid[k] for k in (i,i+1,i+s.map_width+1,i+s.map_width+2)):
                            self.assertTrue(0x40 <= gameplay.material(s,x*8+4,y*8+4) < 0x80)
                anchors = [(s.rules.start_x_q16/65536,s.rules.start_y_q16/65536)]
                anchors += [(p.x_q16/65536,p.y_q16/65536) for p in s.grid+s.item_boxes]
                anchors += [(p.x+4,p.y+4) for p in s.coins]
                anchors += [(p.x_q16/65536,p.y_q16/65536)
                            for a in art.decode_actors(s) for p in a.placements]
                anchors += [(p.x_q16/65536,p.y_q16/65536) for p in art.decode_scenery(s).placements]
                for x,y in anchors:
                    for dx,dy in ((0,0),(-8,0),(8,0),(0,-8),(0,8)):
                        self.assertEqual(f.sample_world((x+dx)*.1,-(y+dy)*.1),0)
                # Crab 5631C/5619C: 150 half-unit steps outward, then return.
                # The runtime keeps a fixed ground anchor; initial placement alone
                # cannot establish safety. Cover the entire continuous patrol and
                # two-source-unit body with zero terrain cells, not sparse poses.
                for family in art.decode_actors(s):
                    if family.family != 'crabs':
                        continue
                    for p in family.placements:
                        x,y = p.x_q16/65536,p.y_q16/65536
                        vertical = p.variant in (1,0x8001)
                        low_x,low_y = x-2,y-2
                        high_x = x+2+(0 if vertical else 75)
                        high_y = y+2+(75 if vertical else 0)
                        for ty in range(max(0,int(low_y//8)),min(s.map_height,int(high_y//8)+1)):
                            for tx in range(max(0,int(low_x//8)),min(s.map_width,int(high_x//8)+1)):
                                i = ty*(s.map_width+1)+tx
                                self.assertTrue(all(f.grid[k]==0 for k in
                                    (i,i+1,i+s.map_width+1,i+s.map_width+2)),
                                    f'crab variant {p.variant:#x} full patrol/body must stay flat')
                for row in s.route_rows:
                    elevated = []
                    for a,b in zip(row,row[1:]+row[:1]):
                        for j in range(101):
                            x=(a.x+(b.x-a.x)*j/100)*.1
                            y=(a.y+(b.y-a.y)*j/100)*.1
                            h=f.sample_world(x,-y)
                            if h>.01:
                                elevated.append(h)
                                for d in (-.3,.3):
                                    side=f.sample_world(x,-y-d) if s.course_id in self.x_longitudinal else f.sample_world(x+d,-y)
                                    self.assertAlmostEqual(h,side,delta=2/65536)
                    self.assertTrue(elevated,'every retained lane must genuinely climb the field')
                    self.assertGreater(max(elevated),.6)
        self.assertEqual(before,[hp.fingerprint(s) for s in sources])
        self.assertEqual(original_bytes,[(self.root/row['height_guide']['scene']).read_bytes()
                                        for row in self.document['courses']])


class LightningCupAuthoredData(FlowerCupAuthoredData):
    """Reuse all guide, source-identity and body-corridor checks for Lightning."""
    root = Path(__file__).resolve().parents[3]/'mods/handpasses/lightning-cup'
    course_order = [28,40,33,26]
    x_longitudinal = set()
    bounds = {28:(656,776,336,536), 40:(456,560,616,784),
              33:(776,912,1440,1600), 26:(1800,1928,640,808)}


class StarCupAuthoredData(FlowerCupAuthoredData):
    """Original Star guides share every existing source/body admission check."""
    root = Path(__file__).resolve().parents[3]/'mods/handpasses/star-cup'
    course_order = [36,34,30,35]
    x_longitudinal = {34,35}
    zero_rim = 16
    bounds = {36:(96,248,848,1016), 34:(1056,1224,336,456),
              30:(1192,1344,448,616), 35:(1584,1712,176,296)}

    def test_owner_actor_contact_and_capture_footprints_stay_flat(self):
        import course_3d_art as art
        import convert_course_3d as converter
        import track_format
        repo = Path(__file__).resolve().parents[3]
        pack = repo/'.mksc-tools/portable/packs/game.pack'
        if not pack.is_file():
            self.skipTest('optional owner pack absent')
        sections = track_format.load_pack(pack)
        for cid,field in self.fields.items():
            source = converter.read_conversion_source(sections,cid)
            footprints = []
            for family in art.decode_actors(source):
                # Include modern body/contact reserve; plants additionally own
                # their four-tile capture search envelope, not just sprite feet.
                radius = 40 if family.family=='piranhas' else 8
                footprints.extend((p.x_q16/65536,p.y_q16/65536,radius)
                                  for p in family.placements)
            for p in art.decode_scenery(source).placements:
                # Kamek's bob/patrol is not stationary decoration. The broad
                # source-local box also contains both64/128-turn motion legs.
                radius = 128 if p.source_code>>16==0xA0 else 12
                footprints.append((p.x_q16/65536,p.y_q16/65536,radius))
            for x,y,radius in footprints:
                for ty in range(max(0,int((y-radius)//8)),min(source.map_height,int((y+radius)//8)+1)):
                    for tx in range(max(0,int((x-radius)//8)),min(source.map_width,int((x+radius)//8)+1)):
                        i = ty*(source.map_width+1)+tx
                        self.assertTrue(all(field.grid[k]==0 for k in
                            (i,i+1,i+source.map_width+1,i+source.map_width+2)),
                            f'course={cid} actor/contact footprint {(x,y,radius)} must stay flat')

    def test_owner_retained_launch_approach_flight_and_landing_stay_flat(self):
        # Ordinary material alone is insufficient: a dry cell can still be in
        # a source launch's flight/landing corridor. Check actual final routes
        # and emitted caps, including both retained polylines and takeoff rays.
        import math
        import course_3d_gameplay as gameplay
        import course_3d_launch as launch
        import convert_course_3d as converter
        import track_format
        repo = Path(__file__).resolve().parents[3]
        pack = repo/'.mksc-tools/portable/packs/game.pack'
        if not pack.is_file():
            self.skipTest('optional owner pack absent')
        sections = track_format.load_pack(pack)
        physics = mesh_kart_format.resolved(json.loads((repo/'portable/data/mesh-kart.json').read_text()))
        physics['kart_radius_q16'] = min(physics['kart_radius_q16'],round(.1*2.5*65536))
        items = json.loads((repo/'portable/data/mesh-items.json').read_text())['profiles']
        ratio = max(physics['panel_cap_ratio_q16']/65536,
                    *(r/65536 for r in physics['boost_cap_ratio_q16']),
                    *(p[k]['cap_ratio'] for p in items for k in ('mushroom','star')))
        speed = physics['forward_speed_q16']/65536*ratio
        self.assertLessEqual(speed,36)
        gravity = wire.DEFAULT_GRAVITY # Same default as emitted META and launch proofs.
        work = 0
        withdrawn_desert_flight = False
        for cid,field in self.fields.items():
            source = converter.read_conversion_source(sections,cid)
            rows = gameplay._adapt_rows(source,.1)
            caps = launch.plan(source,rows,.1,maximum_speed=speed)
            documents = gameplay.routes(source,.1,adapted=rows)
            rows,chain = launch.repair_chains(source,rows,.1,
                gameplay._RouteClearance(source,.1),documents,physics,existing_caps=caps)
            caps.update(chain)
            changed = []
            for y in range(source.map_height):
                for x in range(source.map_width):
                    i = y*(source.map_width+1)+x
                    if any(field.grid[k] for k in (i,i+1,i+source.map_width+1,i+source.map_width+2)):
                        changed.append((x,y))
            def clear(a,b):
                nonlocal work
                for x,y in changed:
                    work += 1
                    self.assertLessEqual(work,4194304,'bounded authored jump safety work')
                    self.assertIsNone(launch._span(a,b,x*8-8,y*8-8,x*8+16,y*8+16),
                        f'course={cid} elevation cell{x,y} intersects launch approach/flight/landing')
            for row in rows:
                count = len(row)
                for edge,(a,b) in enumerate(zip(row,row[1:]+row[:1])):
                    length = math.dist(a,b)
                    if not length:
                        continue
                    direction = ((b[0]-a[0])/length,(b[1]-a[1])/length)
                    cells = {cell for cell,_,_ in launch._intervals(a,b)}
                    # Include body-only contact with neighboring launch cells.
                    cells = {(x+dx,y+dy) for x,y in cells
                             for dx in (-1,0,1) for dy in (-1,0,1)}
                    for x,y in sorted(cells):
                        code = gameplay.material(source,x*8+4,y*8+4)
                        if not (code<0x20 and code&15 in (0,2,14)):
                            continue
                        interval = launch._span(a,b,x*8-3,y*8-3,x*8+11,y*8+11)
                        if interval is None:
                            continue
                        lo,hi = interval
                        impulse = wire.source_launch(code&15)
                        distance = min(2*impulse*speed/gravity,caps.get((x,y),float('inf')))/.1
                        before,after = lo*length-40,hi*length+distance+16
                        ray_a = tuple(a[k]+direction[k]*before for k in (0,1))
                        ray_b = tuple(a[k]+direction[k]*after for k in (0,1))
                        clear(ray_a,ray_b)
                        # This earlier, dry-ground candidate is a real negative
                        # witness: its terrain would have passed material/anchor
                        # tests, but these same flight guards must reject it.
                        if cid==30 and launch._span(ray_a,ray_b,1584,256,1704,392):
                            withdrawn_desert_flight = True
                        # Cover the connected retained line as well as straight
                        # airborne travel; never assume the pilot follows a ray.
                        for step,remaining,index in (
                                (-1,40+(1-lo)*length,edge),
                                (1,distance+16+hi*length,edge)):
                            for walked in range(count):
                                left,right = row[index],row[(index+1)%count]
                                origin,end = (right,left) if step<0 else (left,right)
                                size = math.dist(origin,end)
                                if size:
                                    stop = tuple(origin[k]+(end[k]-origin[k])*min(1,remaining/size) for k in (0,1))
                                    clear(origin,stop)
                                    remaining -= size
                                if remaining<=0:
                                    break
                                index = (index+step)%count
                            else:
                                self.fail(f'course={cid} launch corridor exceeds closed route')
        self.assertTrue(withdrawn_desert_flight,'withdrawn dry Desert ROI must remain inside a guarded flight')


if __name__ == '__main__':
    unittest.main()
