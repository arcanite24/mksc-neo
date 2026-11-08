"""ROM-free converter gameplay, collision partitions and editable glTF roundtrips."""
import copy
import json
import math
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace as NS
import unittest
from unittest.mock import patch
import zlib

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import mesh_kart_format
import convert_course_3d as convert
import course_3d_format as wire
import course_3d_gameplay as gameplay
from course_3d_gltf import AssetIds, Scene, argb_png
import course_3d_terrain as terrain
import model_format
import build_asset_pack
import test_course_3d_source as source_fixture

ROOT=Path(__file__).resolve().parents[3]


def source_loop():
    # A narrow left finish lane and a separate nearby hairpin lane. Raster
    # progress deliberately skips source regions1,2,4,6 along the AI route.
    raster=bytearray([0x7f]*64)
    for y in range(1,5):raster[y*8+2]=0 if y<3 else 7
    for x in range(2,6):raster[x]=3;raster[5*8+x]=7
    for y in range(1,6):raster[y*8+5]=5
    raster[5*8+2]=7
    raster[2*8+2]=0x80
    raster[2*8+4]=6
    raster[2*8+6]=0x87  # Disconnected bit7 patch cannot widen the finish.
    points=[(40,56),(40,8),(64,8),(88,8),(88,48),(88,88),(64,88),(40,88)]
    row=tuple(NS(x=x,y=y,mode=0) for x,y in points)
    return NS(course_id=70000,name='Authored hairpin',catalog_id=1,cup=2,slot=0,
        cup_id=0x10002,song=None,arena=False,width=128,height=128,
        raster_width=8,raster_height=8,raster=bytes(raster),map_width=16,map_height=16,
        tilemap=bytes(256),materials=bytes([0x40]),
        tiles_argb=(tuple([0xff368ace,0xff86ba32]*32),),route_rows=(row,),
        rules=NS(start_x_q16=96*65536,start_y_q16=56*65536,
                 finish_y_q16=48*65536,laps=3),
        grid=tuple(NS(x_q16=(40+i)*65536,y_q16=56*65536,heading=i*16384) for i in range(4)),
        item_boxes=(NS(x_q16=40*65536,y_q16=16*65536,half_extent=2),))


def tables(blob):
    result={}
    for i in range(struct.unpack_from('<I',blob,24)[0]):
        tag,offset,count,stride,flags=struct.unpack_from('<4s4I',blob,64+i*20)
        result[tag.decode()]=(blob[offset:offset+count*stride],count,stride,flags)
    return result


class ConversionTests(unittest.TestCase):
    def test_handpass_lifts_hidden_route_crest_and_ground_anchors(self):
        # Both original route endpoints are flat; the guide's interior crest
        # must become a real navigation target instead of being skipped.
        field=NS(sample_world=lambda x,z: max(0,2-abs(x-2)),
                 edge_parameters=lambda a,b: [0,.25,.5,.75,1])
        node=lambda x,next:dict(position=[x,.2,0],radius=1.5,speed=16,successors=[next])
        route=dict(kind='cpu',closed=True,nodes=[node(0,1),node(4,0)],properties=[
            dict(key='mksc:cpu-ground-route',type='boolean',value=True),
            dict(key='mksc:route-pilot-continuous',type='boolean',value=True)])
        ground=dict(position=[2,1,0])
        sky=dict(position=[2,0,0],properties=[dict(key='mksc:sky-layer',type='integer',value=0)])
        document=dict(routes=[route],grid=[copy.deepcopy(ground)],items=[copy.deepcopy(ground)],
            respawns=[dict(ground,checkpoint=3,route=0)],cameras=[copy.deepcopy(ground)],
            checkpoints=[dict(center=[2,0,0],stage=3,successors=[4])],
            objects=[copy.deepcopy(ground),sky])
        convert.lift_handpass(document,field,NS(course_id=70001),.1)
        nodes=route['nodes']
        self.assertEqual([n['position'] for n in nodes[:2]],[[0,.2,0],[4,.2,0]])
        self.assertEqual(nodes[0]['successors'],[2])
        self.assertEqual(len(nodes),4)
        self.assertEqual(nodes[2]['successors'],[1])
        self.assertEqual(nodes[1]['successors'],[3])
        self.assertEqual(nodes[-1]['successors'],[0])
        self.assertIn([2,2.2,0],[n['position'] for n in nodes])
        self.assertEqual([p['key'] for p in route['properties']],['mksc:route-pilot-continuous'])
        self.assertTrue(all(n['radius']==.8 for n in nodes[2:]))
        self.assertEqual(document['checkpoints'],[dict(center=[2,2,0],stage=3,successors=[4])])
        for key in ('grid','items','respawns','cameras'):
            self.assertEqual(document[key][0]['position'],[2,3,0])
        self.assertEqual(document['respawns'][0]['checkpoint'],3)
        self.assertEqual(document['objects'][0]['position'],[2,3,0])
        self.assertEqual(sky['position'],[2,0,0])

    def test_handpass_keeps_unaffected_route_encoding_and_branch_indices(self):
        field=NS(sample_world=lambda x,z:0,edge_parameters=lambda a,b:[0,.5,1])
        route=dict(kind='cpu',nodes=[dict(position=[0,.2,0],radius=.8,speed=16,successors=[1,2]),
            dict(position=[1,.2,0],radius=.8,speed=16,successors=[0]),
            dict(position=[0,.2,1],radius=.8,speed=16,successors=[0])],properties=[
                dict(key='mksc:cpu-ground-route',type='boolean',value=True)])
        before=copy.deepcopy(route)
        document=dict(routes=[route],grid=[],items=[],respawns=[],cameras=[],checkpoints=[],objects=[])
        convert.lift_handpass(document,field,NS(course_id=70001),.1)
        self.assertEqual(route,before)

    def test_handpass_coalesces_guides_only_after_wire_encoding(self):
        field=NS(sample_world=lambda x,z:max(0,2-abs(x-2)),
                 edge_parameters=lambda a,b:[0,.25,.25+1e-9,.5,1-1e-9,1])
        route=dict(kind='cpu',nodes=[dict(position=[0,.2,0],radius=.8,speed=16,successors=[1]),
            dict(position=[4,.2,0],radius=.8,speed=16,successors=[0])],properties=[])
        document=dict(routes=[route],grid=[],items=[],respawns=[],cameras=[],checkpoints=[],objects=[])
        convert.lift_handpass(document,field,NS(course_id=70001),.1)
        nodes=route['nodes']
        self.assertEqual(len(nodes),4)
        for node in nodes:
            for successor in node['successors']:
                self.assertNotEqual(wire.position(node['position'],'a'),
                                    wire.position(nodes[successor]['position'],'b'))

    def test_handpass_route_simplification_bounds_all_encoded_breakpoints(self):
        fractions=[i/128 for i in range(129)]
        def height(x,z):return .7*math.sin(math.pi*x/32)**2
        field=NS(sample_world=height,edge_parameters=lambda a,b:fractions)
        original=[dict(position=[0,.25,0],radius=1.1,speed=12,successors=[1]),
                  dict(position=[32,.25,0],radius=1.3,speed=14,successors=[0])]
        route=dict(kind='cpu',nodes=copy.deepcopy(original),properties=[])
        document=dict(routes=[route],grid=[],items=[],respawns=[],cameras=[],checkpoints=[],objects=[])
        convert.lift_handpass(document,field,NS(course_id=70001),.1)
        nodes=route['nodes']
        self.assertLess(len(nodes),30) #254 samples previously became mandatory arrivals.
        for i,old in enumerate(original):
            self.assertEqual({k:v for k,v in nodes[i].items() if k!='successors'},
                             {k:v for k,v in old.items() if k!='successors'})
        for begin,end in ((0,1),(1,0)):
            chain=[begin]
            while chain[-1]!=end:
                chain.append(nodes[chain[-1]]['successors'][0])
                self.assertLessEqual(len(chain),len(nodes))
            positions=[nodes[i]['position'] for i in chain]
            self.assertEqual(positions[0],original[begin]['position'])
            self.assertEqual(positions[-1],original[end]['position'])
            for p in positions:
                self.assertEqual(p[2],0) #Original source XY line preserved.
            for t in fractions:
                x=32*t
                segment=next((a,b) for a,b in zip(positions,positions[1:])
                             if min(a[0],b[0])<=x<=max(a[0],b[0]))
                a,b=segment
                from fractions import Fraction
                u=Fraction(x-a[0])/Fraction(b[0]-a[0])
                ah,bh=wire.position(a,'a')[2],wire.position(b,'b')[2]
                expected=wire.position([x,.25+height(x,0),0],'breakpoint')[2]
                self.assertLessEqual(abs(expected-(ah+(bh-ah)*u)),1024)
        # The operation remains deterministic, including first-maximum ties.
        again=copy.deepcopy(document);again['routes'][0]['nodes']=copy.deepcopy(original)
        convert.lift_handpass(again,field,NS(course_id=70001),.1)
        self.assertEqual(again,document)

    def test_handpass_keeps_sharp_crest_and_names_work_exhaustion(self):
        samples=[i/64 for i in range(65)]
        field=NS(sample_world=lambda x,z:1 if x==2 else 0,
                 edge_parameters=lambda a,b:samples)
        route=dict(kind='cpu',nodes=[dict(position=[0,0,0],radius=.8,speed=16,successors=[1]),
                                    dict(position=[4,0,0],radius=.8,speed=16,successors=[0])],properties=[])
        document=dict(routes=[route],grid=[],items=[],respawns=[],cameras=[],checkpoints=[],objects=[])
        before=copy.deepcopy(document)
        convert.lift_handpass(document,field,NS(course_id=70001),.1)
        self.assertIn([2,1,0],[n['position'] for n in route['nodes']])
        self.assertIn([1.9375,0,0],[n['position'] for n in route['nodes']])
        self.assertIn([2.0625,0,0],[n['position'] for n in route['nodes']])
        with patch.object(convert,'HANDPASS_ROUTE_WORK_LIMIT',8):
            with self.assertRaisesRegex(ValueError,'course=70001 hand pass: route guide work budget'):
                convert.lift_handpass(before,field,NS(course_id=70001),.1)
        self.assertEqual(len(before['routes'][0]['nodes']),2)

    def test_panel_reachable_arrivals_are_body_safe_and_item_routes_unchanged(self):
        points=[(240,80),(240,200),(256,320),(216,440),(120,440),(80,320),(80,80)]
        cells=bytearray(64*64)
        for x in range(30,33): cells[33*64+x]=1
        source=NS(course_id=70033,map_width=64,map_height=64,tilemap=bytes(cells),
                  materials=bytes((0x46,0x16,0x80,0x10)))
        profile=mesh_kart_format.resolved(json.loads((ROOT/'portable/data/mesh-kart.json').read_text()))
        profile['kart_radius_q16']=16384
        before=copy.deepcopy((source,points,profile))
        radii=gameplay._forced_arrival_radii(source,points,.1,profile)
        self.assertEqual(radii[2],1.5)
        self.assertEqual(radii[3],1.5)
        self.assertEqual(radii[5],1.5) # A corner cut can reach the nearby return leg.
        self.assertEqual((source,points,profile),before)
        self.assertEqual(radii,gameplay._forced_arrival_radii(source,points,.1,profile))
        routes=gameplay.routes(source,.1,adapted=[points],route_profile=profile)
        self.assertEqual([n['radius'] for n in routes[0]['nodes']],radii)
        self.assertTrue(all(n['radius']==.8 for n in routes[1]['nodes']))
        self.assertEqual([n['successors'] for n in routes[0]['nodes']],
                         [[(i+1)%len(points)] for i in range(len(points))])
        self.assertEqual([n['position'] for n in routes[0]['nodes']],
                         [gameplay.position(*p,.1) for p in points])
        self.assertTrue(all(n['radius']==.8 for n in
            gameplay.routes(source,.1,adapted=[points])[0]['nodes']))
        # The same broad-floor target cannot grow near a wall, short edge,
        # unrelated nearby segment, or an inserted takeoff guide.
        wall=bytearray(cells);wall[40*64+33]=2
        blocked=copy.copy(source);blocked.tilemap=bytes(wall)
        self.assertEqual(gameplay._forced_arrival_radii(blocked,points,.1,profile)[2],.8)
        short=points[:3]+[(264,320)]+points[3:]
        self.assertEqual(gameplay._forced_arrival_radii(source,short,.1,profile)[2],.8)
        crossing=points[:4]+[(300,440),(270,320),(300,80)]
        self.assertEqual(gameplay._forced_arrival_radii(source,crossing,.1,profile)[2],.8)
        self.assertEqual(gameplay._forced_arrival_radii(source,points,.1,profile,
                         guides={points[2]})[2],.8)
        jump=bytearray(cells);jump[40*64+32]=3
        launched=copy.copy(source);launched.tilemap=bytes(jump)
        self.assertEqual(gameplay._forced_arrival_radii(launched,points,.1,profile)[2],.8)
        edge_panel=copy.copy(source);edge_cells=bytearray(64*64)
        edge_cells[33*64+30]=1 # Centerline misses X240..248, body overlaps it.
        edge_panel.tilemap=bytes(edge_cells)
        self.assertEqual(gameplay._forced_arrival_radii(edge_panel,points,.1,profile)[2],1.5)
        boundary=copy.copy(source);boundary_cells=bytearray(cells)
        boundary_cells[40*64+32]=2
        boundary.tilemap=bytes(boundary_cells)
        far_points=list(points)
        # Course extent4096m: float32 collision coordinates can round by more
        # than one Q16 LSB. Nominal terrain disk clearance is only50micrometres
        # above the1.5m authored lookahead; reserve encoding error before floor.
        far_points[2]=(256-(24+1.50005)/8,320)
        far_profile=dict(profile,panel_boost_ticks=8192)
        safe=gameplay._forced_arrival_radii(boundary,far_points,8,far_profile)[2]
        self.assertGreater(safe,1.498)
        self.assertLess(safe,1.4995)
        encoded_node=round(far_points[2][0]*8*65536)/65536
        encoded_wall=struct.unpack('<f',struct.pack('<f',256*8))[0]
        self.assertLessEqual(encoded_node+safe+24,encoded_wall)
        slack=2*4096*(2**-23)+4/65536
        for maximum in (.800001,.80001):
            barely=list(points)
            barely[2]=(256-(24+maximum+slack)/8,320)
            kept=gameplay._forced_arrival_radii(boundary,barely,8,far_profile)[2]
            self.assertEqual(kept,.8)
            self.assertEqual(round(kept*65536),52429)
        enlarged=list(points)
        enlarged[2]=(256-(24+.80002+slack)/8,320)
        published=gameplay._forced_arrival_radii(boundary,enlarged,8,far_profile)[2]
        self.assertGreater(round(published*65536),52429)
        no_panel=copy.copy(source);no_panel.tilemap=bytes(64*64)
        self.assertEqual(gameplay._forced_arrival_radii(no_panel,points,.1,profile),[.8]*len(points))
        # A long legal timer may reach every node, but walks each cycle at most
        # once per panel edge; malformed data remains a named error.
        long=dict(profile,panel_boost_ticks=8192)
        self.assertTrue(all(.8<=v<=1.5 for v in
            gameplay._forced_arrival_radii(source,points,.1,long)))
        with self.assertRaisesRegex(gameplay.GameplayError,'panel_boost_ticks'):
            gameplay._forced_arrival_radii(source,points,.1,dict(profile,panel_boost_ticks=True))

    def test_panel_reach_does_not_stop_at_long_polyline_detour(self):
        points=[(80,80),(80,240),(400,240),(400,400),(160,400),(160,160)]
        cells=bytearray(64*64)
        cells[20*64+10]=1
        source=NS(course_id=70034,map_width=64,map_height=64,tilemap=bytes(cells),
                  materials=bytes((0x46,0x16)))
        profile=mesh_kart_format.resolved(json.loads((ROOT/'portable/data/mesh-kart.json').read_text()))
        reach=((profile['panel_boost_ticks']+1)*profile['forward_speed_q16']/65536*
               profile['panel_cap_ratio_q16']/65536+3)*(4389/262144)
        # The connected route takes a long rectangular detour, then returns
        # only8m from the genuine panel. Actual corner-cut travel may reach
        # this later node even after an earlier node is beyond panel range.
        self.assertGreater(sum(math.dist(points[i],points[i+1])*.1
                               for i in range(1,5)),reach)
        self.assertLess(math.dist(points[5],(80,160))*.1,reach)
        self.assertGreater(math.dist(points[3],(80,168))*.1,reach)
        before=copy.deepcopy((source,points,profile))
        radii=gameplay._forced_arrival_radii(source,points,.1,profile)
        self.assertEqual(radii[5],1.5)
        self.assertEqual(radii[3],.8) # Geometrically outside every contact segment's reach.
        self.assertEqual(radii,gameplay._forced_arrival_radii(source,points,.1,profile))
        self.assertEqual((source,points,profile),before)
        long=dict(profile,panel_boost_ticks=8192)
        self.assertTrue(all(.8<=v<=1.5 for v in
            gameplay._forced_arrival_radii(source,points,.1,long)))

    def test_launch_landing_arrivals_use_final_caps_without_changing_item_targets(self):
        points=[(80,80),(80,240),(400,240),(400,400),(160,400),(160,160)]
        profile=mesh_kart_format.resolved(json.loads((ROOT/'portable/data/mesh-kart.json').read_text()))
        profile['panel_boost_ticks']=0
        for code in (0x10,0x12,0x1e):
            cells=bytearray(64*64)
            cells[20*64+10]=1
            source=NS(course_id=70035,map_width=64,map_height=64,tilemap=bytes(cells),
                      materials=bytes((0x46,code,0x80)))
            caps={(10,20):8.0}
            before=copy.deepcopy((source,points,profile,caps))
            radii=gameplay._forced_arrival_radii(source,points,.1,profile,jump_limits=caps)
            self.assertEqual(radii[5],1.5) # Dry target8m from the launch contact segment.
            self.assertEqual(radii[3],.8)
            self.assertEqual(gameplay._forced_arrival_radii(source,points,.1,profile,
                jump_limits={(10,20):6.49})[5],.8)
            self.assertEqual(gameplay._forced_arrival_radii(source,points,.1,profile,
                jump_limits={(10,20):6.51})[5],1.5)
            uncapped=gameplay._forced_arrival_radii(source,points,.1,profile)
            # Unproved launches must not widen distant grounded startup or
            # return-leg targets merely because the route is a closed cycle.
            self.assertEqual(uncapped,[.8]*len(points))
            self.assertEqual(gameplay._forced_arrival_radii(source,points,.1,profile,
                jump_limits={}),uncapped)
            legacy_documents=gameplay.routes(source,.1,adapted=[points],route_profile=profile)
            self.assertTrue(all(n['radius']==.8 for route in legacy_documents for n in route['nodes']))
            # Abstention does not suppress the independently bounded panel
            # domain when both kinds share the same converted course.
            mixed=copy.copy(source)
            mixed.materials=source.materials+bytes((0x16,))
            mixed_cells=bytearray(cells);mixed_cells[22*64+10]=3
            mixed.tilemap=bytes(mixed_cells)
            panel_only=copy.copy(mixed)
            panel_cells=bytearray(mixed_cells);panel_cells[20*64+10]=0
            panel_only.tilemap=bytes(panel_cells)
            panel_profile=dict(profile,panel_boost_ticks=60)
            mixed_radii=gameplay._forced_arrival_radii(mixed,points,.1,panel_profile)
            self.assertEqual(mixed_radii,gameplay._forced_arrival_radii(
                panel_only,points,.1,panel_profile))
            self.assertGreater(mixed_radii[5],.8)
            self.assertEqual(gameplay._forced_arrival_radii(source,points,.1,profile,
                guides={points[5]},jump_limits=caps)[5],.8)
            blocked=copy.copy(source)
            wall=bytearray(cells);wall[20*64+20]=2
            blocked.tilemap=bytes(wall)
            self.assertEqual(gameplay._forced_arrival_radii(blocked,points,.1,profile,
                jump_limits=caps)[5],.8)
            documents=gameplay.routes(source,.1,adapted=[points],route_profile=profile,
                                      jump_limits=caps)
            self.assertEqual(documents[0]['nodes'][5]['radius'],1.5)
            self.assertTrue(all(node['radius']==.8 for node in documents[1]['nodes']))
            self.assertEqual((source,points,profile,caps),before)
            self.assertEqual(radii,gameplay._forced_arrival_radii(source,points,.1,profile,
                jump_limits=caps))
            with self.assertRaisesRegex(gameplay.GameplayError,'emitted jump distance'):
                gameplay._forced_arrival_radii(source,points,.1,profile,
                    jump_limits={(10,20):float('inf')})

    def test_unnamed_road_carries_progress_without_granting_future_gates(self):
        source = source_loop()
        source.raster = bytes([0,127,255,2,2,2,2,2]*8)
        mapping = {0:0,1:1,2:2}
        def gate(x,stage):
            return dict(center=[x*.1,0,-.8],normal=[1,0,0],right=[0,0,-1],
                        half_width=1,half_height=20,stage=stage,key_group=stage,
                        flags=5 if stage==0 else 4,successors=[(stage+1)%3])
        path = [((0,8),(64,8))]
        # These apertures appear in order, but both precede the actual0->2
        # high-water transition.127 and0xff cannot grant the second stage.
        graph = [gate(64,0),gate(16,1),gate(40,2)]
        self.assertEqual(gameplay._gate_graph_failure(source,.1,graph,[path],mapping),
                         (0,2))
        graph[2] = gate(56,2)
        self.assertIsNone(gameplay._gate_graph_failure(source,.1,graph,[path],mapping))

    def test_source_high_water_skip_keeps_coincident_stages_ordered(self):
        source = source_loop()
        source.raster = bytes([0,1,3,4,4,4,4,4]*8)
        mapping = {0:0,1:1,2:2,3:3,4:4}
        path = [((0,8),(64,8))]
        self.assertEqual(gameplay._lane_progress(source,path,mapping,4)[0],[0,1,3,3,4])
        def gate(x,stage):
            return dict(center=[x*.1,0,-.8],normal=[1,0,0],right=[0,0,-1],
                        half_width=1,half_height=20,stage=stage,key_group=stage,
                        flags=5 if stage==0 else 4,successors=[(stage+1)%5])
        # Region2 is genuinely absent. The real1->3 boundary represents both
        # thresholds at equal fractions, through two ordered successor edges.
        graph = [gate(64,0),gate(16,1),gate(32,2),gate(32,3),gate(48,4)]
        self.assertIsNone(gameplay._gate_graph_failure(source,.1,graph,[path],mapping))
        before = copy.deepcopy(source)
        candidates = gameplay._lane_candidates(source,.1,
            [(0,8),(64,8),(64,24),(0,24)],(0,8),[0,1,2,3,4])
        self.assertIn(2,candidates)
        self.assertEqual(candidates[2],candidates[3])
        self.assertEqual(source,before)

    def test_late_prior_lane_gate_is_repaired_before_its_successor(self):
        source = source_loop()
        source.raster = bytes(min(x,3) for _ in range(8) for x in range(8))
        def gate(x,stage):
            return dict(center=[x*.1,0,-.8],normal=[1,0,0],right=[0,0,-1],
                        half_width=1,half_height=20,stage=stage,key_group=stage,
                        flags=5 if stage==0 else 4,successors=[(stage+1)%4])
        # The stage1 tangent crosses a second lane only in region2, after
        # that lane's valid stage2 plane. Moving stage2 would hide the cause.
        gates = [gate(64,0),gate(40,1),gate(32,2),gate(48,3)]
        mapping = {0:0,1:1,2:2,3:3}
        failure = gameplay._gate_graph_failure(source,.1,gates,
                                                [[((0,8),(64,8))]],mapping)
        self.assertEqual(failure,(0,1))

    def test_crossed_lane_boundary_replaces_one_lane_tangent(self):
        source = source_loop()
        raster = bytearray(64)
        for y in range(8):
            for x in range(8):
                raster[y*8+x] = (0 if x<4 else 1) if y<5 else (3 if x<4 else 2)
        raster[3*8+1] |= 128
        source.raster = bytes(raster)
        first = [(24,64),(24,16),(72,16),(120,96),(24,96)]
        second = [(24,64),(24,16),(72,16),(72,96),(24,96)]
        rows = [first,second,second]
        source.route_rows = tuple(tuple(NS(x=x,y=y,mode=0) for x,y in row)
                                  for row in rows)
        source.rules.finish_y_q16 = 64*65536
        source.rules.start_x_q16 = 24*65536
        def gate(point,normal,width,stage):
            dx,dy = normal
            return dict(center=gameplay.position(*point,.1),normal=[dx,0,-dy],
                right=[-dy,0,-dx],half_width=width*.1,half_height=20,
                stage=stage,key_group=stage,flags=5 if stage==0 else 4,
                successors=[(stage+1)%4])
        normal = (48/math.hypot(48,80),80/math.hypot(48,80))
        original = [gate((24,64),(0,-1),8,0),gate((64,32),(1,0),32,1),
                    gate((110.4,80),normal,64,2),gate((64,96),(-1,0),32,3)]
        mapping = {0:0,1:1,2:2,3:3}
        paths = [gameplay._gate_loop(row,(24,64)) for row in rows]
        self.assertIsNotNone(gameplay._gate_graph_failure(source,.1,original,paths,mapping))
        respawns = [dict(checkpoint=i) for i in range(4)]
        before = copy.deepcopy((source,rows,original,mapping))
        repaired = gameplay._repair_lane_gates(source,.1,original,respawns,mapping,
                                               [0,1,2,3],rows)
        self.assertIsNone(gameplay._gate_graph_failure(source,.1,repaired,paths,mapping))
        self.assertEqual(sum(bool(g['flags']&1) for g in repaired),1)
        stage = [g for g in repaired if g['stage']==2]
        self.assertEqual(len(stage),1)
        self.assertEqual(stage[0]['normal'],[0,0,-1])
        self.assertAlmostEqual(stage[0]['center'][2],-8)
        self.assertEqual((source,rows,original,mapping),before)

    def test_parallel_gate_recovery_uses_real_indices_without_relabeling_regions(self):
        # Two finite apertures represent one stage; neither spans the gap.
        def gate(x,stage):
            return dict(center=[x,0,-stage],normal=[0,0,-1],right=[-1,0,0],
                        half_width=.5,half_height=20,flags=5 if stage==0 else 4)
        groups = [[gate(0,0)],[gate(-2,1),gate(2,1)],[gate(0,2)]]
        graph,indices = gameplay._gate_graph(groups)
        self.assertEqual(indices,[[0],[1,2],[3]])
        self.assertEqual(graph[0]['successors'],[1,2])
        self.assertEqual(graph[1]['successors'],[3])
        self.assertEqual(graph[2]['successors'],[3])
        self.assertEqual(graph[3]['successors'],[0])
        self.assertEqual([(g['stage'],g['key_group']) for g in graph],
                         [(0,0),(1,1),(1,1),(2,2)])
        source = source_loop()
        mapping = {0:0,3:1,5:2}
        respawns = [dict(checkpoint=i) for i in range(3)]
        # Exercise successful publication's remapping, with a single-stage
        # witness failure followed by genuine parallel candidates.
        with patch.object(gameplay,'_gate_graph_failure',side_effect=[(0,1),(0,1),(0,1),None]), \
             patch.object(gameplay,'_lane_candidates',side_effect=[
                 {3:[groups[1][0]]},{3:[groups[1][1]]}]):
            repaired = gameplay._repair_lane_gates(source,.1,
                [groups[0][0],groups[1][0],groups[2][0]],respawns,mapping,[0,3,5],
                [[(40,56),(40,8),(88,8),(88,88),(40,88)]]*2)
        self.assertEqual(mapping,{0:0,3:1,5:2})
        self.assertEqual([r['checkpoint'] for r in respawns],[0,1,3])
        self.assertEqual(repaired[3]['stage'],2)

    def test_imported_sections_generate_a_reproducible_installable_mod(self):
        sections=source_fixture.fixture()
        identity=source_fixture.COURSE
        header,world=sections[5,identity]
        raster=bytes((1,1,0x80,0x7f,2,0x7f,0x83,0x7f,
                      2,3,3,0x7f,0x7f,0x7f,0x7f,0x7f))
        sections[5,identity]=(header,world[:28]+raster+world[44:])
        sections[28,identity]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0))
        manifest=[]
        for (kind,ident),(metadata,payload) in sorted(sections.items()):
            name=f'{kind}-{ident}.bin'
            (self.root/name).write_bytes(payload)
            manifest.append(dict(kind=kind,content_id=ident,version=metadata['version'],path=name))
        path=self.root/'pack.json'
        path.write_text(json.dumps({'sections':manifest}))
        base=self.root/'base.pack'
        build_asset_pack.build_pack(path,base)
        original=base.read_bytes()
        original_physics=(ROOT/'portable/data/mesh-kart.json').read_bytes()
        outputs=[]
        for attempt in range(2):
            # Exported Blender sources live independently; reapplying their
            # unchanged flat guide must reproduce the exact mod and archive.
            options=({'export_handpass':self.root/'author-guide'} if attempt==0 else
                     {'handpass':self.root/'author-guide/handpass.json'})
            generated=convert.generate(base,self.root/f'source-{attempt}',courses=[identity],**options)
            folder,archive=convert.mod_kit.build(generated,None,self.root/f'mods-{attempt}')
            self.assertEqual(json.loads((folder/'mod.json').read_text())['targets'],['3d'])
            outputs.append(((folder/'content.pack').read_bytes(),archive.read_bytes()))
            self.assertEqual(json.loads((generated/'physics.json').read_text())['kart_radius_q16'],16384)
            document=json.loads((generated/f'courses3d/{identity}/course.json').read_text())
            self.assertEqual(document['meta']['modes'],['tt','quick','vs'])
            self.assertEqual(len(document['checkpoints']),7)
            self.assertEqual(document['meta']['music'],{'kind':16,'id':101})
            models=json.loads((generated/'models.json').read_text())
            self.assertEqual(len(models['meshes']),1)
            self.assertEqual(models['bindings'],[])
            coin_objects=[o for o in document['objects'] if o['class_id']==0x20000001]
            self.assertEqual(len(coin_objects),1)
            self.assertNotIn('mksc:billboard',{p['key']:p['value'] for p in coin_objects[0]['properties']})
            self.assertTrue((generated/'coins.json').is_file())
            self.assertFalse((generated/'source-actors.json').exists())
        self.assertEqual(outputs[0],outputs[1])
        with self.assertRaisesRegex(ValueError,'metre scale mismatch'):
            convert.generate(base,self.root/'stale-source',courses=[identity],scale=.2,
                             handpass=self.root/'author-guide/handpass.json')
        self.assertFalse((self.root/'stale-source').exists())
        self.assertEqual(base.read_bytes(),original)
        self.assertEqual((ROOT/'portable/data/mesh-kart.json').read_bytes(),original_physics)
        for scale in (0,.01,float('nan'),float('inf'),3,True):
            with self.subTest(scale=scale),self.assertRaisesRegex(ValueError,'conversion scale'):
                convert.generate(base,self.root/'invalid-source',courses=[identity],scale=scale)
        self.assertFalse((self.root/'invalid-source').exists())

    def setUp(self):
        scratch=ROOT/'.mksc-tools/portable/runs/t8'
        scratch.mkdir(parents=True,exist_ok=True)
        self.temporary=tempfile.TemporaryDirectory(dir=scratch,prefix='conversion-')
        self.root=Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def test_real_finish_span_and_forward_skipped_region_chain(self):
        source=source_loop();before=copy.deepcopy(source)
        gates,respawns,mapping=gameplay.gates(source,.1)
        self.assertEqual(source.raster,before.raster)
        for actual,expected in zip(gates[0]['center'],[4,0,-4.8]):
            self.assertAlmostEqual(actual,expected)
        self.assertAlmostEqual(gates[0]['half_width'],.8)
        self.assertNotEqual(gates[0]['center'][0],source.rules.start_x_q16/65536*.1)
        self.assertEqual(len(gates),7)
        self.assertEqual([g['stage'] for g in gates],[0,1,1,2,2,3,3])
        self.assertEqual([g['successors'] for g in gates],
                         [[1,2],[3,4],[3,4],[5,6],[5,6],[0],[0]])
        self.assertEqual([g['flags'] for g in gates],[5,4,4,4,4,4,4])
        self.assertEqual(mapping,{0:0,1:0,2:0,3:1,4:1,5:2,6:2,7:3})
        self.assertEqual([r['checkpoint'] for r in respawns],[0,1,3,5])
        # The connected bit7 span ends at sourceX48, before the separate bend.
        self.assertAlmostEqual(gates[0]['center'][0]+gates[0]['half_width'],4.8)
        broken=copy.deepcopy(source)
        broken.raster=bytes(v&0x7f for v in broken.raster)
        with self.assertRaisesRegex(gameplay.GameplayError,'finish'):
            gameplay.gates(broken,.1)

    def test_gate_uses_connected_road_beyond_thin_progress_strip(self):
        source = source_loop()
        # A driving lane spans x16..96; a distant hairpin touches it at x64.
        # Stage1's narrow raster strip ends at48, before the corner-cut path.
        raster = bytearray([127]*64)
        raster[2*8+1:2*8+6] = bytes((0,1,2,6,6))
        source.raster = bytes(raster)
        source.materials = bytes((0x40,0x84,0x22,0x5a))
        tilemap = bytearray([1]*256)
        for y in range(4,6):
            tilemap[y*16+2:y*16+12] = bytes(10)
        source.tilemap = bytes(tilemap)
        original = copy.deepcopy(source)
        center,half = gameplay._gate_width(source,(40,40),(0,-1),{0,1})
        self.assertEqual(center,(40,40))
        self.assertEqual(half,24)
        # A normal steered corner path crosses at56, outside stage1's raster
        # slice but inside the connected road. It must still reach the gate.
        a,b = (56,42),(56,38)
        fraction = (40-a[1])/(b[1]-a[1])
        crossing = a[0]+fraction*(b[0]-a[0])
        self.assertLessEqual(abs(crossing-center[0]),half)
        self.assertGreater(crossing,48)
        self.assertEqual(center[0]+half,64) # Never extends into distant stage6.
        self.assertEqual(source,original)
        # Material boundaries win even when their progress labels are nearby.
        for blocked in (1,2):
            with self.subTest(material=source.materials[blocked]):
                changed = copy.deepcopy(source)
                cells = bytearray(changed.tilemap)
                cells[5*16+7] = blocked # x56..64 wall or water.
                changed.tilemap = bytes(cells)
                shifted,span = gameplay._gate_width(changed,(40,40),(0,-1),{0,1})
                self.assertEqual(shifted[0]+span,56)
        # Slower off-road is legal ground, including beyond a thin stage strip.
        changed = copy.deepcopy(source)
        cells = bytearray(changed.tilemap)
        cells[5*16+7:5*16+10] = bytes([3]*3)
        changed.tilemap = bytes(cells)
        shifted,span = gameplay._gate_width(changed,(40,40),(0,-1),{0,1})
        self.assertEqual(shifted[0]+span,64) # Still stops at unrelated stage6.
        self.assertLessEqual(abs(60-shifted[0]),span)

    def test_gate_tile_edge_extent_does_not_cross_a_narrow_wall(self):
        source = source_loop()
        source.raster = bytes([1]*64)
        source.materials = bytes((0x4e,0x84))
        cells = bytearray([1]*256)
        cells[5*16+7] = 0 # Only x56..64 is road; route lies on its left edge.
        source.tilemap = bytes(cells)
        center,half = gameplay._gate_width(source,(56,40),(0,-1),{0,1})
        self.assertEqual((center,half),((60,40),4))
        self.assertEqual(center[0]-half,56)
        self.assertEqual(center[0]+half,64)
        self.assertIsNone(gameplay._gate_width(source,(40,40),(0,-1),{0,1}))

    def test_gate_width_crosses_unnamed_road_but_not_other_stages_or_walls(self):
        source = source_loop()
        source.materials = bytes((0x40,0x84,0x22))
        raster = bytearray([6]*64)
        raster[2*8+2:2*8+6] = bytes((1,0x7f,0xff,1))
        source.raster = bytes(raster)
        saved = copy.deepcopy(source)
        center,half = gameplay._gate_width(source,(40,40),(0,-1),{0,1})
        self.assertEqual((center,half),((64,40),32))
        self.assertEqual((center[0]-half,center[0]+half),(32,96))
        self.assertEqual(source,saved)
        # Both masked127 forms are unnamed. A different NUMBERED stage
        # terminates the same connected road, even though its material stays40.
        raster[2*8+4] = 6
        source.raster = bytes(raster)
        center,half = gameplay._gate_width(source,(40,40),(0,-1),{0,1})
        self.assertEqual(center[0]+half,64)
        for code in (1,2):
            changed = copy.deepcopy(saved)
            cells = bytearray(changed.tilemap)
            cells[5*16+7] = code # Physical barrier inside the unnamed strip.
            changed.tilemap = bytes(cells)
            center,half = gameplay._gate_width(changed,(40,40),(0,-1),{0,1})
            self.assertEqual(center[0]+half,56)

    def test_ordinary_raster_fronts_prevent_oblique_order_inversion(self):
        source=NS(course_id=70070,map_width=16,map_height=16,
                  raster_width=8,raster_height=8,
                  route_rows=(tuple(NS(mode=1) for _ in range(3)),),
                  materials=bytes((0x4e,)),tilemap=bytes(256),
                  raster=bytes(min(x//2,2) for y in range(8) for x in range(8)))
        saved=copy.deepcopy(source)
        def plane(x,dy=0,finish=False):
            norm=math.hypot(1,dy)
            return dict(center=[x*.1,0,-3.2],normal=[1/norm,0,-dy/norm],
                right=[-dy/norm,0,-1/norm],half_width=20,
                half_height=20,flags=5 if finish else 4)
        first,second=plane(32,-.5),plane(64,.5)
        ray=((0,100),(120,100))
        # Both planes cover the ray, but their intersection inverted their
        # crossing order away from the retained center lane.
        self.assertLess(gameplay._gate_hit(second,*ray,.1),
                        gameplay._gate_hit(first,*ray,.1))
        canonical,_=gameplay._gate_graph([[plane(120,finish=True)],[first],[second]])
        paths=[[((0,y),(120,y))] for y in (32,100)]
        mapping={0:0,1:1,2:2}
        self.assertIsNotNone(gameplay._gate_graph_failure(source,.1,canonical,paths,mapping))
        fronts={i:gameplay._raster_landing_fronts(source,.1,
                    (x-.25,32),(x+.25,32),i-1,i)
                for i,x in ((1,32),(2,64))}
        self.assertEqual([fronts[i][0]['center'][0] for i in (1,2)],[3.2,6.4])
        respawns=[dict(checkpoint=i) for i in range(3)]
        with patch.object(gameplay,'_finish',return_value=(None,None,(0,32))):
            added=gameplay._add_landing_fronts(source,.1,canonical,respawns,
                mapping,[0,1,2],[[(0,32),(120,32)]],fronts)
        self.assertEqual(len(added),5)
        self.assertIsNone(gameplay._gate_graph_failure(source,.1,added,paths,mapping))
        self.assertEqual([r['checkpoint'] for r in respawns],[0,1,3])
        self.assertEqual(source,saved)

    def test_airborne_named_landing_front_covers_more_than_oblique_lane_gate(self):
        # Named region1 begins across a lava gap. An oblique first-dry
        # aperture represents the retained lane but excludes another genuine
        # airborne source-region entrance farther along its finite front.
        source=NS(course_id=70035,map_width=16,map_height=16,
                  raster_width=8,raster_height=8,
                  route_rows=(tuple(NS(mode=3) for _ in range(3)),))
        source.materials=bytes((0x44,0x24,0x80))
        source.raster=bytes(min(x//2,2) for y in range(8) for x in range(8))
        source.tilemap=bytes(1 if 32<=x*8<48 else 0
                             for y in range(16) for x in range(16))
        saved=copy.deepcopy(source)
        front=gameplay._raster_landing_fronts(source,.1,(31.5,64),(32.5,64),0,1)
        self.assertEqual(len(front),1)
        self.assertEqual(front[0]['normal'],[1,0,0])
        self.assertEqual(front[0]['center'],[3.2,0,-6.4])
        self.assertEqual(front[0]['half_width'],6.4)
        with self.assertRaisesRegex(gameplay.GameplayError,'course=70035.*budget'):
            gameplay._raster_landing_fronts(source,.1,(31.5,64),(32.5,64),0,1,budget=[0])
        self.assertEqual(source,saved)
        normal=(2/math.sqrt(5),1/math.sqrt(5))
        oblique=gameplay._lane_gate(source,.1,1,(52,64),normal)
        self.assertIsNotNone(oblique)
        ray=((31,100),(65,100))
        self.assertIsNone(gameplay._gate_hit(oblique,*ray,.1))
        self.assertIsNotNone(gameplay._gate_hit(front[0],*ray,.1))
        # Every retained diagonal and the second physical ray acquire the
        # same stage before its successor, with the actual source high-water.
        def end_gate(x,stage):
            return dict(center=[x*.1,0,-6.4],normal=[1,0,0],right=[0,0,-1],
                half_width=6.4,half_height=20,flags=5 if stage==0 else 4)
        graph,_=gameplay._gate_graph([[end_gate(120,0)],
            [oblique,front[0]],[end_gate(96,2)]])
        paths=[[((0,y),(120,y))] for y in (48,64,100)]
        self.assertIsNone(gameplay._gate_graph_failure(source,.1,graph,paths,{0:0,1:1,2:2}))
        # Publication adds alternatives even when the old retained lane
        # already passes. RESP remains the representative canonical gate.
        canonical,_=gameplay._gate_graph([[end_gate(120,0)],[oblique],[end_gate(96,2)]])
        respawns=[dict(checkpoint=i) for i in range(3)]
        rows=[[(0,64),(120,64)]]
        with patch.object(gameplay,'_finish',return_value=(None,None,(0,64))):
            added=gameplay._add_landing_fronts(source,.1,canonical,respawns,
                {0:0,1:1,2:2},[0,1,2],rows,{1:front})
            self.assertEqual(len(added),4)
            self.assertEqual([r['checkpoint'] for r in respawns],[0,1,3])
            self.assertIsNone(gameplay._gate_graph_failure(source,.1,added,paths,{0:0,1:1,2:2}))
            # A retained-lane order failure declines the whole optional
            # publication, leaving canonical references untouched.
            unchanged=[dict(checkpoint=i) for i in range(3)]
            with patch.object(gameplay,'_gate_graph_failure',return_value=(0,1)):
                self.assertIs(gameplay._add_landing_fronts(source,.1,canonical,
                    unchanged,{0:0,1:1,2:2},[0,1,2],rows,{1:front}),canonical)
            self.assertEqual(unchanged,[dict(checkpoint=i) for i in range(3)])
        self.assertEqual(source,saved)

    def test_landing_fronts_rotate_across_walls_but_stop_at_unrelated_regions_and_height_guards(self):
        for turn in range(4):
            def rotate(p):
                x,y=p
                for _ in range(turn): x,y=128-y,x
                return x,y
            def inverse(p):
                x,y=p
                for _ in range(turn): x,y=y,128-x
                return x,y
            raster=[];tiles=[]
            for y in range(8):
                for x in range(8):
                    a,b=inverse(((x+.5)*16,(y+.5)*16))
                    raster.append(0 if a<32 else 1)
            for y in range(16):
                for x in range(16):
                    a,b=inverse(((x+.5)*8,(y+.5)*8))
                    tiles.append(2 if 48<=b<56 else 1 if 32<=a<48 else 0)
            source=NS(course_id=70035,map_width=16,map_height=16,
                raster_width=8,raster_height=8,raster=bytes(raster),
                tilemap=bytes(tiles),materials=bytes((0x44,0x24,0x80)),
                route_rows=(tuple(NS(mode=3) for _ in range(2)),))
            before,after=rotate((31.5,64)),rotate((32.5,64))
            front=gameplay._raster_landing_fronts(source,.1,before,after,0,1)
            self.assertEqual(len(front),1)
            self.assertTrue(any(gameplay._gate_hit(g,*map(rotate,((31,40),(33,40))),.1)
                                is not None for g in front))
            self.assertTrue(any(gameplay._gate_hit(g,*map(rotate,((31,52),(33,52))),.1)
                                is not None for g in front))
            self.assertTrue(any(gameplay._gate_hit(g,*map(rotate,((31,72),(33,72))),.1)
                                is not None for g in front))
            self.assertEqual(gameplay._raster_landing_fronts(source,.1,after,before,1,1),[])
            source.route_rows[0][1].mode=128
            self.assertEqual(gameplay._raster_landing_fronts(source,.1,before,after,0,1),[])
        # Adjacent numbered roads cannot extend a front merely because their
        # terrain is flat. A guarded mode or wrong previous stage also fails.
        source.route_rows[0][1].mode=3
        self.assertEqual(gameplay._raster_landing_fronts(source,.1,before,after,7,1),[])

    def test_named_wall_front_accepts_encoded_airborne_center_without_changing_terrain(self):
        # Source69's13->14 wall front: X160..176,Y832. Its center may
        # legitimately cross above the wall; material does not gate progress.
        source=NS(course_id=70069,map_width=32,map_height=112,
            raster_width=16,raster_height=56,
            raster=bytes((13 if y<52 else 14) if x==10 else 127
                         for y in range(56) for x in range(16)),
            tilemap=bytes(32*112),materials=bytes((0x80,)),
            route_rows=tuple(tuple(NS(mode=1) for _ in range(15)) for _ in range(3)))
        saved=copy.deepcopy(source)
        fronts=gameplay._raster_landing_fronts(source,.1,(172,831.5),(172,832.5),13,14)
        self.assertEqual(len(fronts),1)
        self.assertEqual(fronts[0]['center'],[16.8,0,-83.2])
        self.assertEqual(fronts[0]['half_width'],.8)
        # _gate_hit uses encoded Q16/Q30, just as the runtime crossing path.
        ray=((173.00826,831.8),(173.00826,832.8))
        self.assertIsNotNone(gameplay._gate_hit(fronts[0],*ray,.1,body_margin=0))
        self.assertIsNone(gameplay._gate_hit(fronts[0],ray[1],ray[0],.1,body_margin=0))
        self.assertIsNone(gameplay._gate_hit(fronts[0],(177,831.8),(177,832.8),.1,body_margin=0))
        self.assertEqual(source,saved)
        for row in range(3):
            blocked=copy.deepcopy(source);blocked.route_rows[row][14].mode=128
            self.assertEqual(gameplay._raster_landing_fronts(blocked,.1,
                (172,831.5),(172,832.5),13,14),[])
        unrelated=copy.deepcopy(source);raster=bytearray(unrelated.raster)
        raster[52*16+10]=15;unrelated.raster=bytes(raster)
        self.assertEqual(gameplay._raster_landing_fronts(unrelated,.1,
            (172,831.5),(172,832.5),13,14),[])

    def test_named_wall_comb_gets_connected_body_clear_interior_in_four_directions(self):
        for turn in range(4):
            def rotate(p):
                x,y=p
                for _ in range(turn): x,y=128-y,x
                return x,y
            def inverse(p):
                x,y=p
                for _ in range(turn): x,y=y,128-x
                return x,y
            def fixture(control=None):
                raster=[];tiles=[]
                for y in range(8):
                    for x in range(8):
                        a,b=inverse(((x+.5)*16,(y+.5)*16))
                        raster.append(0 if a<32 else 2 if control=='unrelated' and a>=40 else 1)
                for y in range(16):
                    for x in range(16):
                        a,b=inverse(((x+.5)*8,(y+.5)*8))
                        wall=32<=a<40 and int(b//8)%2==1
                        if control=='persistent': wall=a>=32 and int(b//8)%2==1
                        tiles.append(2 if wall else 1 if control=='disconnected' and 40<=a<48 else 0)
                return NS(course_id=70076,map_width=16,map_height=16,
                    raster_width=8,raster_height=8,raster=bytes(raster),
                    tilemap=bytes(tiles),materials=bytes((0x4e,0x24,0x84)),
                    route_rows=(tuple(NS(mode=0) for _ in range(3)),))
            source=fixture()
            saved=copy.deepcopy(source)
            before,after=rotate((31.5,64)),rotate((32.5,64))
            remaining=[16] # Permit seed scan, then exhaust in first interior layer.
            def exhausted():
                remaining[0]-=1
                if remaining[0]<0:
                    raise gameplay.GameplayError('course=70076 interior work budget exhausted')
            # Exercise the interior layer counter itself, not only front scan.
            with self.assertRaisesRegex(gameplay.GameplayError,'interior work budget'):
                gameplay._interior_landing_fronts(source,.1,turn%2,
                    1 if turn<2 else -1,32 if turn<2 else 96,0,16,1,exhausted)
            fronts=gameplay._raster_landing_fronts(source,.1,before,after,0,1,include_air=False)
            self.assertEqual(len(fronts),1)
            a,b=map(rotate,((31,12),(60,12)))
            direction=tuple(y-x for x,y in zip(rotate((0,0)),rotate((1,0))))
            dx,dy=direction
            slits=[dict(center=gameplay.position(*rotate((32,y)),.1),
                normal=[dx,0,-dy],right=[-dy,0,-dx],half_width=.4,
                half_height=20,flags=4) for y in range(4,128,16)]
            self.assertTrue(all(gameplay._gate_hit(g,a,b,.1) is None for g in slits))
            self.assertIsNotNone(gameplay._gate_hit(fronts[0],a,b,.1))
            center=fronts[0]['center']
            point=(center[0]/.1,-center[2]/.1)
            # The new plane lies inside floor, beyond the comb with fullbody
            # normal clearance, rather than stretching an old slit overwall.
            original=inverse(point)
            self.assertGreater(original[0],43)
            self.assertLess(original[0],44)
            self.assertAlmostEqual(original[1],64)
            self.assertLess(fronts[0]['half_width'],6.1)
            self.assertEqual(source,saved)
            for control in ('persistent','unrelated','disconnected'):
                blocked=fixture(control)
                self.assertEqual(gameplay._raster_landing_fronts(blocked,.1,before,after,0,1,
                                 include_air=False),[],control)
            source.route_rows[0][1].mode=128
            self.assertEqual(gameplay._raster_landing_fronts(source,.1,before,after,0,1,
                             include_air=False),[])

    def test_local_unnamed_corner_witnesses_two_thresholds_without_global_propagation(self):
        for orientation in range(8):
            turn=orientation%4
            reflected=orientation>=4
            def rotate(p):
                x,y=p
                if reflected: x=64-x
                for _ in range(turn): x,y=64-y,x
                return x,y
            def inverse(p):
                x,y=p
                for _ in range(turn): x,y=y,64-x
                if reflected: x=64-x
                return x,y
            raster=[];tiles=[]
            for y in range(4):
                for x in range(4):
                    a,b=inverse(((x+.5)*16,(y+.5)*16))
                    raster.append({(1,1):1,(1,2):2,(2,2):3}.get((int(a//16),int(b//16)),127))
            for y in range(8):
                for x in range(8):
                    a,b=inverse(((x+.5)*8,(y+.5)*8))
                    tiles.append(1 if (int(a//8),int(b//8)) in ((3,3),(3,4)) else 0)
            source=NS(course_id=70069,map_width=8,map_height=8,raster_width=4,
                raster_height=4,raster=bytes(raster),tilemap=bytes(tiles),
                materials=bytes((0x56,0x80,0x24)),
                route_rows=(tuple(NS(mode=128 if i==2 else 1) for i in range(4)),)*3)
            mapping={raw:raw for raw in range(4)}
            saved=copy.deepcopy(source)
            fronts=gameplay._unnamed_corner_fronts(source,.1,mapping,[0,1,2,3])
            self.assertEqual(set(fronts),{2,3})
            self.assertEqual(len(fronts[2]),1)
            self.assertEqual(fronts[2],fronts[3])
            self.assertEqual(fronts[2][0]['half_width'],.8)
            # Actual69 crossing relative to its corner: center is only1.29
            # source units from the adjacent wall, but source progress uses
            # center/target-mode permission, not a planar body-clear disk.
            a,b=map(rotate,((33.297515,31.891327),(33.270355,32.484894)))
            hit=gameplay._gate_hit(fronts[2][0],a,b,.1,body_margin=0)
            self.assertIsNotNone(hit)
            self.assertEqual(hit,gameplay._gate_hit(fronts[3][0],a,b,.1,body_margin=0))
            self.assertIsNone(gameplay._gate_hit(fronts[2][0],a,b,.1))
            self.assertIsNone(gameplay._gate_hit(fronts[2][0],b,a,.1,body_margin=0))
            # The other material-tile half belongs to this same finite raster
            # side; neither the old half nor an adjacent raster side suffices.
            far=tuple(map(rotate,((45.297515,31.891327),(45.270355,32.484894))))
            self.assertIsNotNone(gameplay._gate_hit(fronts[2][0],*far,.1,body_margin=0))
            outside=tuple(map(rotate,((49,31.9),(49,32.5))))
            self.assertIsNone(gameplay._gate_hit(fronts[2][0],*outside,.1,body_margin=0))
            self.assertEqual(source,saved)
            with self.assertRaisesRegex(gameplay.GameplayError,'unnamed corner work budget'):
                gameplay._unnamed_corner_fronts(source,.1,mapping,[0,1,2,3],budget=[0])
            # Target C controls a genuine skip. Intermediate B may be height
            # restricted (above), but a restricted C forbids both alternatives.
            source.route_rows[0][3].mode=128
            self.assertEqual(gameplay._unnamed_corner_fronts(source,.1,mapping,[0,1,2,3]),{})
            source.route_rows[0][3].mode=1
            for replacement in (127,0,3,129):
                bad=list(source.raster)
                x,y=rotate((24,24));bad[int(y//16)*4+int(x//16)]=replacement
                if replacement==127:
                    x,y=rotate((8,8));remote=int(y//16)*4+int(x//16)
                    self.assertEqual(bad[remote],127)
                    bad[remote]=1
                    self.assertEqual(bad.count(1),1)
                    # This isolated prior cell borders the same connected
                    # neutral perimeter, but shares no target-front corner.
                    for point in ((24,8),(8,24)):
                        x,y=rotate(point)
                        self.assertEqual(bad[int(y//16)*4+int(x//16)],127)
                altered=copy.deepcopy(source);altered.raster=bytes(bad)
                # A remote copy of A cannot authorize this corner's neutral
                # component, nor can a finish-bit or unordered local label.
                self.assertEqual(gameplay._unnamed_corner_fronts(altered,.1,mapping,[0,1,2,3]),{})
            self.assertEqual(gameplay._unnamed_corner_fronts(source,.1,{1:1,2:1,3:2},[0,1,3]),{})
            altered=copy.deepcopy(source);raster=list(altered.raster)
            x,y=rotate((40,24));raster[int(y//16)*4+int(x//16)]=255
            altered.raster=bytes(raster)
            self.assertEqual(gameplay._unnamed_corner_fronts(altered,.1,mapping,[0,1,2,3]),{})
            for blocked in (1,2):
                for half in range(2):
                    for depth in (28,36):
                        altered=copy.deepcopy(source);cells=list(altered.tilemap)
                        x,y=rotate((36+half*8,depth))
                        cells[int(y//8)*8+int(x//8)]=blocked
                        altered.tilemap=bytes(cells)
                        remaining=gameplay._unnamed_corner_fronts(altered,.1,mapping,[0,1,2,3])
                        self.assertEqual(set(remaining),{2,3})
                        self.assertEqual(len(remaining[2]),1)
                        self.assertEqual(remaining[2][0]['half_width'],.4)
                        near_hit=gameplay._gate_hit(remaining[2][0],a,b,.1,body_margin=0)
                        far_hit=gameplay._gate_hit(remaining[2][0],*far,.1,body_margin=0)
                        self.assertEqual(near_hit is None,half==0)
                        self.assertEqual(far_hit is None,half==1)
                # Both halves blocked, even on opposite sides, admit nothing.
                altered=copy.deepcopy(source);cells=list(altered.tilemap)
                for cell in ((36,28),(44,36)):
                    x,y=rotate(cell);cells[int(y//8)*8+int(x//8)]=blocked
                altered.tilemap=bytes(cells)
                self.assertEqual(gameplay._unnamed_corner_fronts(altered,.1,mapping,[0,1,2,3]),{})
            # A different future named target cannot inherit the local C front.
            altered=copy.deepcopy(source);raster=list(altered.raster)
            x,y=rotate((40,40));raster[int(y//16)*4+int(x//16)]=4
            altered.raster=bytes(raster)
            self.assertEqual(gameplay._unnamed_corner_fronts(altered,.1,mapping,[0,1,2,3]),{})

    def test_gate_defers_blocked_transition_until_driving_surface(self):
        source = source_loop()
        # Region3 starts on the top edge at y16. A wall clips the source AI
        # segment there and includes its corner at(40,8); road resumes atx48.
        source.materials = bytes((0x40,0x84))
        cells = bytearray(source.tilemap)
        cells[1*16+5] = 1 # sourceX40..48, sourceY8..16.
        source.tilemap = bytes(cells)
        gates,respawns,mapping = gameplay.gates(source,.1,canonical=gameplay._canonical(source))
        self.assertEqual(len(gates),7)
        self.assertEqual(mapping,{0:0,1:0,2:0,3:1,4:1,5:2,6:2,7:3})
        self.assertTrue(all(gameplay._road_corridor(gameplay.material(source,
            point['position'][0]/.1,-point['position'][2]/.1)) for point in respawns))
        self.assertEqual(gates[1]['normal'],[1,0,0])
        # The next segment leaves the wall atx48, so its first road crossing
        # carries the eastbound gate instead of a plane through the wall.
        self.assertAlmostEqual(gates[1]['center'][0],4.8)

    def test_recovery_at_a_landing_edge_reserves_dry_body_support(self):
        for code in (0x24,0x10,0x16):
            with self.subTest(material=code):
                source=source_loop()
                source.materials=bytes((0x40,code))
                cells=bytearray(source.tilemap)
                cells[4*16+11]=1 # x88..96,y32..40, immediately before the sample.
                source.tilemap=bytes(cells)
                saved=copy.deepcopy(source)
                samples=[((88,40),(0,1)),((40,88),(0,-1))]
                with patch.object(gameplay,'_sample_loop',return_value=iter(samples)):
                    gates,respawns,mapping=gameplay.gates(source,.1,
                        canonical=gameplay._canonical(source))
                self.assertEqual(len(gates),3)
                self.assertEqual(respawns[1]['checkpoint'],1)
                self.assertEqual(mapping[5],1)
                # Center-only support at y40 leaves the collider over lava,
                # or on a ramp/panel that can relaunch it during recovery.
                x,_,z=respawns[1]['position']
                self.assertAlmostEqual(x,8.8)
                self.assertGreaterEqual(-z,4.3)
                self.assertEqual(source,saved)

    def test_recovery_avoids_a_dry_shelf_after_its_required_launch(self):
        source=source_loop()
        source.materials=bytes((0x40,0x10,0x24))
        cells=bytearray(source.tilemap)
        cells[7*16+11]=1 # Launch y56..64.
        for y in range(9,13):
            cells[y*16+11]=2 # Lava y72..104 after an8-unit dry shelf.
        source.tilemap=bytes(cells)
        saved=copy.deepcopy(source)
        samples=[((88,64),(0,1)),((40,88),(0,-1))]
        with patch.object(gameplay,'_sample_loop',return_value=iter(samples)):
            gates,respawns,mapping=gameplay.gates(source,.1,
                canonical=gameplay._canonical(source))
        self.assertEqual(len(gates),3)
        self.assertEqual(mapping[5],1)
        self.assertEqual(respawns[1]['checkpoint'],1)
        x,_,z=respawns[1]['position']
        self.assertAlmostEqual(x,8.8)
        # Back up before the required launch, preserving progress; never
        # strand a stationary kart beyond its ramp facing the next lava gap.
        self.assertLess(-z,5.6)
        self.assertLessEqual(-z+3+.3,5.6+1e-9)
        self.assertEqual(source,saved)

    def test_first_dry_jump_sample_waits_for_body_wide_aperture(self):
        source = source_loop()
        source.materials = bytes((0x40,0x24))
        cells = bytearray(source.tilemap)
        cells[4*16+6] = 1 # Lava x48..56,y32..40 beside the landing edge.
        source.tilemap = bytes(cells)
        raster = bytearray(source.raster)
        raster[2*8+3] = 3
        source.raster = bytes(raster)
        direction = (math.sqrt(.5),-math.sqrt(.5))
        edge,interior = (56.25,40),(60.25,36)
        self.assertIsNotNone(gameplay._gate_width(source,edge,direction,{0,3}))
        self.assertIsNone(gameplay._gate_width(source,edge,direction,{0,3},minimum_margin=3))
        self.assertIsNotNone(gameplay._gate_width(source,interior,direction,{3},minimum_margin=3))
        samples = [(edge,direction),(interior,direction),((88,40),(0,1))]
        saved = copy.deepcopy(source)
        # Isolate gate placement from route repair: a proved flight may have
        # its first dry stage sample only a fraction of a body from the shore.
        with patch.object(gameplay,'_sample_loop',return_value=iter(samples)):
            gates,respawns,mapping = gameplay.gates(source,.1,canonical=gameplay._canonical(source))
        self.assertEqual(len(gates),3)
        self.assertEqual(mapping[3],1)
        gate = gates[1]
        center = gate['center'][0]/.1,-gate['center'][2]/.1
        self.assertLess(sum((edge[i]-center[i])*direction[i] for i in range(2)),-5)
        self.assertEqual(respawns[1]['position'],gameplay.position(*interior,.1))
        self.assertEqual(source,saved)
        # If that region never provides a body-wide crossing, report it;
        # accepting its successor must not silently remove an authored stage.
        with patch.object(gameplay,'_sample_loop',return_value=iter([samples[0],samples[2]])):
            with self.assertRaisesRegex(gameplay.GameplayError,'forward region 3.*body-wide'):
                gameplay.gates(source,.1,canonical=gameplay._canonical(source))

    def test_route_repair_preserves_anchors_and_clear_body_corners(self):
        source = source_loop()
        source.raster = bytes([1]*64)
        source.materials = bytes((0x40,0x84,0x5a))
        cells = bytearray([1]*256)
        for i in range(2,13):
            for x,y in ((i,2),(i,12),(2,i),(12,i)):
                cells[y*16+x] = 0
        cells[2*16+7] = 2 # Grass remains driveable, unlike gate-span borders.
        source.tilemap = bytes(cells)
        anchors = ((16,20),(100,28),(100,100),(20,100))
        second = ((20,20),(100,32),(100,100),(20,100))
        source.route_rows = tuple(tuple(NS(x=x,y=y,mode=0) for x,y in row)
                                 for row in (anchors,second,tuple(reversed(second))))
        before = copy.deepcopy(source)
        routes = gameplay.routes(source,.1)
        self.assertEqual(routes,gameplay.routes(source,.1))
        self.assertEqual(source,before)
        self.assertEqual([row['kind'] for row in routes],['cpu','cpu','cpu','item'])
        for route in routes[:3]:
            self.assertIn({'key':'mksc:route-pilot-continuous','type':'boolean','value':True},
                          route['properties'])
            self.assertIn({'key':'mksc:cpu-ground-route','type':'boolean','value':True},
                          route['properties'])
        self.assertNotIn('mksc:route-pilot-continuous',
                         {prop['key'] for prop in routes[-1]['properties']})
        self.assertEqual(routes[-1]['nodes'],routes[0]['nodes'])
        for row,expected in zip(routes[:3],(((20,20),*anchors[1:]),second,tuple(reversed(second)))):
            points=[(round(node['position'][0]/.1,6),round(-node['position'][2]/.1,6))
                    for node in row['nodes']]
            self.assertGreater(len(points),len(expected))
            indices=[points.index(point) for point in expected]
            self.assertEqual(indices,sorted(indices))
            for i,(a,b) in enumerate(zip(points,points[1:]+points[:1])):
                self.assertEqual(row['nodes'][i]['successors'],[(i+1)%len(points)])
                # Independent disk sampling verifies the whole path, including
                # inserted corner segments, against the actual8-unit walls.
                steps=max(1,math.ceil(math.dist(a,b)*2))
                for step in range(steps+1):
                    x=a[0]+(b[0]-a[0])*step/steps
                    y=a[1]+(b[1]-a[1])*step/steps
                    for angle in range(16):
                        px=x+2.99*math.cos(angle*math.tau/16)
                        py=y+2.99*math.sin(angle*math.tau/16)
                        tile=cells[math.floor(py/8)*16+math.floor(px/8)]
                        self.assertIn(tile,(0,2))
        broken=copy.deepcopy(source)
        blocked=bytearray(cells)
        blocked[7*16:8*16]=bytes([1]*16)
        broken.tilemap=bytes(blocked)
        saved=copy.deepcopy(broken)
        with self.assertRaisesRegex(gameplay.GameplayError,'course=70000 route=0 edge=1: local clearance'):
            gameplay.routes(broken,.1)
        self.assertEqual(broken,saved)

    def test_only_launch_backed_reachable_gap_keeps_its_source_connector(self):
        source=source_loop()
        source.raster=bytes([1]*64)
        source.materials=bytes((0x40,0x84,0x22,0x10))
        cells=bytearray([1]*256)
        for x in range(2,13):
            cells[2*16+x]=2 if 6 <= x < 10 else 3 if x==4 else 0
        source.tilemap=bytes(cells)
        a,b=(20,20),(100,20)
        before=copy.deepcopy(source)
        adapted=gameplay._RouteClearance(source,.1)
        self.assertFalse(adapted.clear(a,b))
        self.assertTrue(adapted.jump_connector(a,b))
        self.assertEqual(adapted.repair(a,b,a,b,0,0),[a,b])
        self.assertEqual(source,before)
        # Same source gap at1m/unit is46m from launch to safe landing, beyond
        # the analytic16m/s × (2×9m/s /default gravity) range even before tick margin.
        self.assertFalse(gameplay._RouteClearance(source,1).jump_connector(a,b))
        self.assertFalse(adapted.jump_connector(a,(70,20))) # no safe landing.
        wall=copy.deepcopy(source)
        wall_cells=bytearray(cells)
        wall_cells[2*16+10]=1 # Source four-unit barrier is.4m at this scale.
        wall.tilemap=bytes(wall_cells)
        self.assertTrue(gameplay._RouteClearance(wall,.1).jump_connector(a,b))
        self.assertFalse(gameplay._RouteClearance(wall,.1).jump_connector((20,18),(100,18)))
        with patch.object(gameplay,'converted_wall_height',return_value=6):
            self.assertFalse(gameplay._RouteClearance(wall,.1).jump_connector(a,b))
        for offset,replacement in ((4,0),(3,1)):
            with self.subTest(no_launch_or_wall=offset):
                changed=copy.deepcopy(source)
                changed_cells=bytearray(cells)
                changed_cells[2*16+offset]=replacement
                changed.tilemap=bytes(changed_cells)
                context=gameplay._RouteClearance(changed,.1)
                self.assertFalse(context.jump_connector(a,b))
                with self.assertRaisesRegex(gameplay.GameplayError,'route=0 edge=0: local clearance'):
                    context.repair(a,b,a,b,0,0)

    def test_height_preserving_launch_clears_two_barrier_retained_connector(self):
        # Exact shape of the newly exposed Ribbon Road failure, ROM-free:
        # a launch followed by two .4m walls along a long, shallow diagonal.
        source=source_loop()
        source.map_width,source.map_height=256,128
        source.materials=bytes((0x40,0x10,0x80))
        cells=bytearray(256*128)
        for x in range(130,133): cells[76*256+x]=1
        for x in (117,129):
            for y in (75,76): cells[y*256+x]=2
        source.tilemap=bytes(cells)
        before=copy.deepcopy(source)
        a,b=(1160,616),(560,592)
        proof=gameplay._RouteClearance(source,.1)
        self.assertTrue(proof.jump_connector(a,b))
        with patch.object(wire,'source_launch',side_effect=lambda kind:{0:9,2:6,14:3}[kind]):
            self.assertFalse(proof.jump_connector(a,b))
        for kind,old in ((0,9),(2,6),(14,3)):
            impulse=convert.surface(kind)['launch']
            self.assertEqual(impulse,wire.source_launch(kind))
            # Flight time stays the P3 calibration's; the apex only rises.
            p3=old*math.sqrt(wire.P3_GRAVITY/9.8)
            self.assertAlmostEqual(impulse/wire.DEFAULT_GRAVITY,p3/wire.P3_GRAVITY,places=5)
            self.assertGreater(impulse**2/(2*wire.DEFAULT_GRAVITY),old**2/(2*9.8))
        self.assertEqual(source,before)

    def test_recovery_flight_uses_center_and_descending_snap_height(self):
        source=source_loop()
        source.raster=bytes([1]*64)
        source.materials=bytes((0x40,0x84,0x24,0x10,0x0e))
        cells=bytearray([1]*256)
        for x in range(2,16):
            cells[2*16+x]=2 if 5 <= x < 9 else 3 if x==4 else 0
        source.tilemap=bytes(cells)
        # The8-unit launch at32..40 directly borders lava at40..72. Center
        # reaches lava after.05s; even the two-tick late launch remains airborne.
        phase=2*4389/262144
        self.assertGreater(9*(.8/16-phase)-wire.DEFAULT_GRAVITY/2*(.8/16-phase)**2,0)
        context=gameplay._RouteClearance(source,.1)
        self.assertTrue(context.jump_connector((20,20),(100,20)))
        # The adapted lowest launch clears a short gap but approaches the shore
        # under the stronger authored gravity; the longer gap exceeds its flight.
        for end,expected in ((11,True),(12,False)):
            with self.subTest(recovery_end=end*8):
                changed=copy.deepcopy(source)
                strip=bytearray(cells)
                for x in range(2,16):
                    strip[2*16+x]=2 if 5 <= x < end else 4 if x==4 else 0
                changed.tilemap=bytes(strip)
                proof=gameplay._RouteClearance(changed,.1)
                self.assertEqual(proof.jump_connector((20,20),(124,20)),expected)
        late=(88-32)*.1/16+phase
        height=wire.source_launch(14)*late-wire.DEFAULT_GRAVITY*late*late/2
        self.assertGreater(late,wire.source_launch(14)/wire.DEFAULT_GRAVITY)
        self.assertGreater(height,wire.DEFAULT_GROUND_SNAP_Q16/65536)
        self.assertLess(height,.25)
        # The new reserve preserves the short descending flight; the old
        # quarter-metre reserve would snap into its recovery floor.
        strip=bytearray(cells)
        for x in range(2,16):
            strip[2*16+x]=2 if 5<=x<11 else 4 if x==4 else 0
        short=copy.deepcopy(source)
        short.tilemap=bytes(strip)
        with patch.object(wire,'DEFAULT_GROUND_SNAP_Q16',16384):
            self.assertFalse(gameplay._RouteClearance(short,.1).jump_connector((20,20),(124,20)))
        self.assertFalse(gameplay._RouteClearance(source,1).jump_connector((20,20),(100,20)))

    def test_barrier_height_and_proof_share_source_scale(self):
        for scale in (.02,.1,.5,2):
            self.assertEqual(gameplay.converted_wall_height(scale),4*scale)
        source=source_loop()
        source.raster=bytes([1]*64)
        source.materials=bytes((0x40,0x84,0x10))
        cells=bytearray([1]*256)
        for x in range(2,13):
            cells[2*16+x]=2 if x==4 else 1 if x==6 else 0
        source.tilemap=bytes(cells)
        context=gameplay._RouteClearance(source,.1)
        self.assertTrue(context.jump_connector((20,20),(100,20)))
        # The same trajectory cannot clear an invented1.6m wall at this close
        # entry time. The proof follows geometry rather than ignoring walls.
        with patch.object(gameplay,'converted_wall_height',return_value=1.6):
            self.assertFalse(context.jump_connector((20,20),(100,20)))

    def test_arena_uses_safe_patrol_and_retains_unreachable_item(self):
        source=source_loop()
        source.arena=True
        source.raster=bytes([127]*64)
        source.route_rows=((NS(x=-999,y=-999,mode=0),),)*3
        source.materials=bytes((0x40,0x26))
        cells=bytearray([1]*256)
        for y in range(2,14):
            cells[y*16+2:y*16+14]=bytes(12)
        source.tilemap=bytes(cells)
        source.item_boxes=(NS(x_q16=68*65536,y_q16=68*65536,half_extent=4),
                           NS(x_q16=2*65536,y_q16=2*65536,half_extent=1))
        before=copy.deepcopy(source)
        document,_=gameplay.derive(source,scale=.1)
        self.assertEqual(source,before)
        self.assertEqual([row['kind'] for row in document['routes']],['cpu','item'])
        properties=document['routes'][0]['properties']
        self.assertIn({'key':'source:patrol-unreachable-items','type':'string','value':'1'},properties)
        self.assertEqual(len(document['items']),2)
        self.assertEqual(document['items'][1]['position'],[.2,1,-.2])
        for node in document['routes'][0]['nodes']:
            self.assertGreaterEqual(node['position'][0],1.6)
            self.assertLessEqual(node['position'][0],11.2)

    def test_clearance_progress_includes_short_raster_corner_intervals(self):
        source=source_loop()
        raster=bytearray(64)
        raster[1]=8
        source.raster=bytes(raster)
        context=gameplay._RouteClearance(source,.1)
        # Endpoints lie in(0,0)/(1,1); the short middle interval visits(1,0).
        self.assertEqual(context.labels((15,14),(17,17)),{0,8})
        self.assertEqual(context.labels((17,17),(15,14)),{0,8})
        # Boundary ownership follows floor: x16 belongs to the right-hand cell.
        self.assertEqual(context.labels((16,1),(16,15)),{8})
        self.assertEqual(context.labels((15,1),(15,15)),{0})

    def test_arena_has_no_gates_and_keeps_four_grid_orientations(self):
        source=source_loop();source.arena=True
        document,regions=gameplay.derive(source,scale=.1)
        self.assertEqual(document['meta']['modes'],['battle'])
        self.assertEqual(document['meta']['laps'],0)
        self.assertEqual(document['checkpoints'],[])
        self.assertEqual(set(regions),{0})
        self.assertEqual(len(document['grid']),4)
        for i,(grid,respawn) in enumerate(zip(document['grid'],document['respawns'])):
            expected=[0,math.sin(i*math.pi/4),0,math.cos(i*math.pi/4)]
            # Quaternions q and -q encode the same authored orientation.
            dot=sum(a*b for a,b in zip(grid['quaternion'],expected))
            self.assertAlmostEqual(abs(dot),1)
            self.assertIsNone(respawn['checkpoint'])
            self.assertEqual(respawn['quaternion'],grid['quaternion'])
            self.assertEqual(respawn['position'],grid['position'])

    def test_progress_provenance_is_opt_in_and_uses_original_stage_labels(self):
        source=source_loop()
        before=copy.deepcopy(source)
        default=gameplay.derive(source)
        explicit=gameplay.derive(source,progress_provenance=False)
        self.assertEqual(json.dumps(default),json.dumps(explicit))
        document,regions=gameplay.derive(source,progress_provenance=True)
        properties=document['meta']['properties']
        provenance=[p for p in properties if p['key'].startswith('source:progress-')]
        self.assertEqual(provenance,[
            dict(key='source:progress-stage-count',type='integer',value=4),
            *[dict(key=f'source:progress-raw:{stage}',type='integer',value=raw)
              for stage,raw in enumerate((0,3,5,7))]])
        # Genuine landing alternatives create seven CPNT rows for four stages.
        # Neither those row indices nor the four RESP indices are raw progress.
        self.assertEqual(len(document['checkpoints']),7)
        self.assertEqual(sorted({g['stage'] for g in document['checkpoints']}),[0,1,2,3])
        self.assertGreater(sum(g['stage']==1 for g in document['checkpoints']),1)
        self.assertEqual(len(document['respawns']),4)
        document['meta']['properties']=[p for p in properties if p not in provenance]
        self.assertEqual(json.dumps((document,regions)),json.dumps(default))
        self.assertEqual(source,before)
        # Opting in does not invent an ordered race table for a Battle arena.
        source.arena=True
        self.assertEqual(gameplay.derive(source,progress_provenance=True),gameplay.derive(source))
        with self.assertRaisesRegex(gameplay.GameplayError,'progress provenance: expected boolean'):
            gameplay.derive(source,progress_provenance=1)

    def test_progress_provenance_is_not_published_on_late_gate_failure(self):
        output=[99]
        with patch.object(gameplay,'_add_landing_fronts',
                          side_effect=gameplay.GameplayError('late landing failure')):
            with self.assertRaisesRegex(gameplay.GameplayError,'late landing failure'):
                gameplay.gates(source_loop(),.1,progress_raw=output)
        self.assertEqual(output,[99])
        gameplay.gates(source_loop(),.1,progress_raw=output)
        self.assertEqual(output,[0,3,5,7])

    def test_collision_partition_and_gltf_roundtrip(self):
        source=source_loop();document,regions=gameplay.derive(source,scale=.1)
        mesh=terrain.encode(source.map_width,source.map_height,source.tilemap,
            source.tiles_argb,source.materials,units_per_pixel=.1,collision_regions=regions)
        scene=Scene(self.root);ids=AssetIds()
        convert.terrain_scene(source,mesh,scene,document,regions,ids,.1)
        document['textures']=scene.texture_mappings
        scene.save()
        first=wire.compile(document,self.root)
        self.assertEqual(first,wire.compile(document,self.root))
        decoded=tables(first[0][1]);vertices=decoded['COLV'][0]
        referenced=set()
        for offset in range(0,len(decoded['COLT'][0]),32):
            row=struct.unpack_from('<8I',decoded['COLT'][0],offset)
            referenced.add(row[5])
            positions=[struct.unpack_from('<3i',vertices,index*12) for index in row[:3]]
            x=sum(p[0] for p in positions)/3/65536/.1
            y=sum(p[1] for p in positions)/3/65536/.1
            tx=max(0,min(15,math.floor(x/8)));ty=max(0,min(15,math.floor(y/8)))
            self.assertEqual(row[5],regions[ty*16+tx])
            self.assertLess(row[5],len(document['respawns']))
        self.assertGreater(len(referenced),1)
        self.assertEqual(decoded['CPNT'][1],7)
        self.assertEqual(decoded['RESP'][1],4)
        self.assertTrue(any(metadata['kind']==101 for metadata,_ in first))

    def test_jump_distance_partitions_independent_launch_strips(self):
        source=source_loop();document,regions=gameplay.derive(source,scale=.1)
        source.materials=bytes((0x40,0x10,0x84))
        source.tiles_argb=source.tiles_argb*3
        cells=bytearray(source.tilemap)
        for x in (5,9,12):
            cells[5*16+x]=1
        cells[5*16+4]=2 # Exposed wall face borders the capped jump cell.
        source.tilemap=bytes(cells)
        mesh=terrain.encode(source.map_width,source.map_height,source.tilemap,
            source.tiles_argb,source.materials,units_per_pixel=.1,collision_regions=regions)
        scene=Scene(self.root);ids=AssetIds()
        ceilings={(5,5):12.5,(9,5):20}
        convert.terrain_scene(source,mesh,scene,document,regions,ids,.1,ceilings)
        document['textures']=scene.texture_mappings
        scene.save()
        compiled=wire.compile(document,self.root)
        decoded=tables(compiled[0][1]);vertices=decoded['COLV'][0]
        found={}
        for offset in range(0,len(decoded['COLT'][0]),32):
            row=struct.unpack_from('<8I',decoded['COLT'][0],offset)
            surface=document['surfaces'][row[3]]
            properties={p['key']:p['value'] for p in surface['properties']}
            if properties['source:material']!=0x10:
                self.assertNotIn('mksc:jump-max-distance',properties)
                continue
            positions=[struct.unpack_from('<3i',vertices,index*12) for index in row[:3]]
            x=sum(p[0] for p in positions)/3/65536/.1
            y=sum(p[1] for p in positions)/3/65536/.1
            cell=(math.floor(x/8),math.floor(y/8))
            self.assertEqual(properties.get('mksc:jump-max-distance'),ceilings.get(cell))
            found[cell]=properties.get('mksc:jump-max-distance')
        self.assertEqual(found,{(5,5):12.5,(9,5):20,(12,5):None})

    def test_adjacent_launch_caps_partition_collision_before_centroid_surface_assignment(self):
        source=source_loop()
        document,_=gameplay.derive(source,scale=.1)
        source.materials=bytes((0x40,0x10))
        source.tiles_argb=source.tiles_argb*2
        cells=bytearray(source.tilemap)
        for x in range(5,10): cells[5*16+x]=1
        source.tilemap=bytes(cells)
        regions=[0]*256 # Every launch cell has identical material and RESP.
        ceilings={(5,5):8,(6,5):12,(7,5):12+1e-8}
        original=copy.deepcopy((source,document,regions,ceilings))
        partitions=convert._collision_partitions(source,regions,ceilings)
        self.assertNotEqual(partitions[5*16+5],partitions[5*16+6])
        self.assertEqual(partitions[5*16+6],partitions[5*16+7]) # Same encoded Q16.
        self.assertNotEqual(partitions[5*16+7],partitions[5*16+8])
        self.assertEqual(partitions[5*16+8],partitions[5*16+9]) # Uncapped stays distinct.
        self.assertEqual(partitions,convert._collision_partitions(source,regions,ceilings))
        base=terrain.encode(16,16,source.tilemap,source.tiles_argb,source.materials,
                            units_per_pixel=.1,collision_regions=regions)
        split=terrain.encode(16,16,source.tilemap,source.tiles_argb,source.materials,
                             units_per_pixel=.1,collision_regions=partitions)
        for name in ('textures','materials','vertices','triangles'):
            self.assertEqual(getattr(base,name),getattr(split,name))
        def derive(*args,**kwargs):
            kwargs['jump_limits'].update(ceilings)
            return copy.deepcopy(document),regions[:]
        with patch.object(convert.gameplay,'derive',side_effect=derive), \
             patch.object(convert.art,'decode_sky',return_value=None), \
             patch.object(convert,'scenery',return_value=0),patch.object(convert,'sky'), \
             patch.object(convert,'actors',return_value=0):
            emitted=convert.convert(source,self.root,AssetIds(),{},scale=.1)
        compiled=wire.compile(emitted,self.root)
        decoded=tables(compiled[0][1])
        vertices=decoded['COLV'][0]
        def cap(surface):
            fields=struct.unpack_from('<12I',decoded['SURF'][0],surface*48)
            for index in range(fields[10],fields[10]+fields[11]):
                key,kind,value=struct.unpack_from('<3I',decoded['PROP'][0],index*32)
                name=decoded['STRS'][0][key:].split(b'\0',1)[0]
                if name==b'mksc:jump-max-distance':
                    self.assertEqual(kind,1)
                    return value
            return None
        triangles=[]
        for offset in range(0,len(decoded['COLT'][0]),32):
            row=struct.unpack_from('<8I',decoded['COLT'][0],offset)
            if struct.unpack_from('<I',decoded['SURF'][0],row[3]*48)[0]!=5:
                continue
            self.assertEqual(row[5],0) # Merge-only IDs never leak into RESP.
            points=[struct.unpack_from('<3i',vertices,index*12) for index in row[:3]]
            actual=cap(row[3]);triangles.append((points,actual))
            low=min(p[0] for p in points)/65536/.1
            high=max(p[0] for p in points)/65536/.1
            for x in range(math.floor((low+1e-3)/8),math.ceil((high-1e-3)/8)):
                expected=ceilings.get((x,5))
                self.assertEqual(actual,None if expected is None else wire.fixed(expected))
        self.assertGreaterEqual(len(triangles),6)
        # Runtime-equivalent COLT->SURF->PROP lookup at every original cell
        # center must return its exact encoded cap, including uncapped cells.
        for x in range(5,10):
            point=(round((x+.5)*8*.1*65536),round(5.5*8*.1*65536))
            found=[]
            for vertices,actual in triangles:
                crosses=[]
                for a,b in zip(vertices,vertices[1:]+vertices[:1]):
                    crosses.append((b[0]-a[0])*(point[1]-a[1])-
                                   (b[1]-a[1])*(point[0]-a[0]))
                if min(crosses)>=0 or max(crosses)<=0: found.append(actual)
            self.assertTrue(found)
            expected=ceilings.get((x,5))
            self.assertTrue(all(value==(None if expected is None else wire.fixed(expected))
                                for value in found))
        self.assertEqual((source,document,regions,ceilings),original)

    def test_argb_exact_png_shared_ids_and_render_seams(self):
        pixels=(0x00112233,0x7f204060,0xff01fe80,0xff804020)
        png=argb_png(2,2,pixels);at=8;compressed=bytearray()
        while at<len(png):
            size=struct.unpack_from('>I',png,at)[0];kind=png[at+4:at+8]
            payload=png[at+8:at+8+size]
            self.assertEqual(struct.unpack_from('>I',png,at+8+size)[0],zlib.crc32(kind+payload)&0xffffffff)
            if kind==b'IDAT':compressed.extend(payload)
            at+=12+size
        self.assertEqual(zlib.decompress(compressed),bytes((0,0x11,0x22,0x33,0,
            0x20,0x40,0x60,0x7f,0,1,0xfe,0x80,0xff,0x80,0x40,0x20,0xff)))
        ids=AssetIds();a=Scene(self.root/'a');b=Scene(self.root/'b')
        self.assertEqual(a.texture(2,2,pixels,ids),a.texture(2,2,pixels,ids))
        b.texture(2,2,pixels,ids)
        self.assertEqual(a.texture_mappings[0]['id'],b.texture_mappings[0]['id'])
        self.assertEqual(len(a.document['images']),1)
        self.assertEqual(len(ids.payloads),1)
        b.texture(2,2,tuple(reversed(pixels)),ids)
        self.assertEqual(len(ids.payloads),2)
        # Mesh compaction must preserve UV seams even across material groups.
        a.material();a.material()
        vertices=[(0,0,0,0,1,0,0,0),(1,0,0,0,1,0,1,0),(0,0,1,0,1,0,0,1),
                  (0,0,0,0,1,0,.5,.5),(999,0,0,0,1,0,0,0)]
        a.mesh('seams',vertices,[(0,1,2,0),(3,1,2,1)])
        a.save()
        loaded=wire.Gltf(self.root/'a','scene.gltf')
        primitives=list(loaded.primitives('seams'))
        self.assertEqual([len(p[0]) for p in primitives],[3,3])
        self.assertEqual(primitives[0][0][0],primitives[1][0][0])
        self.assertNotEqual(primitives[0][2][0],primitives[1][2][0])

    def test_sky_crop_remap_horizon_wrap_and_inward_geometry(self):
        class CapturingScene(Scene):
            def __init__(self, directory):
                super().__init__(directory)
                self.draws = []

            def mesh(self, name, vertices, triangles):
                self.draws.append((name, tuple(vertices), tuple(triangles)))
                return super().mesh(name, vertices, triangles)

        art = convert.art
        front = [0x00112233] * 24
        front[5] = 0xffff0000
        front[15] = 0xff000000
        back = [0x00445566] * 64
        back[2*8+1] = 0xff00ff00
        back[5*8+6] = 0xff0000ff
        images = (art.PixelImage(4,6,0,0,tuple(front)),
                  art.PixelImage(8,8,0,0,tuple(back)),
                  art.PixelImage(4,2,0,0,(0,)*8))
        motion = art.SkyMotion(controller=0,layer_mask=7,blend_mode=0,blend_a=16,blend_b=0,
            yaw_shift=(7,5,6),x_bias=(8,-4,0),y_bias=(10,-2,0),split_y=0,split_layer=0,
            split_yaw_shift=0,split_x_bias=0,lightning_hide_layer=0,layer_source=(1,0,2),road_mode=0)
        decoded = art.SkyArt(tuple(art.SkyLayer(str(i),image,1,1,b'')
            for i,image in enumerate(images)),motion,(0x001f,),0,0,0,0,0,0,b'')
        source = source_loop()
        identities = AssetIds()
        scene = CapturingScene(self.root/'sky-a')
        document = {'render_nodes':[],'meta':{}}
        with patch.object(art,'decode_sky',return_value=decoded):
            convert.sky(source,scene,document,identities,.1)
        self.assertEqual(document['render_nodes'],['Sky-0','Sky-1'])
        self.assertEqual(document['meta']['sky_rgba'],0xff0000ff)
        self.assertEqual(len(scene.document['images']),2)
        expected_images = ((8,4,tuple(back[16:48])),(4,3,tuple(front[4:16])))
        for image,expected in zip(scene.document['images'],expected_images):
            payload = model_format._texture_bytes((scene.directory/image['uri']).read_bytes(),'sky')
            self.assertEqual(struct.unpack_from('<HH',payload,12),expected[:2])
            self.assertEqual(struct.unpack_from(f'<{len(expected[2])}I',payload,32),expected[2])
        for material in scene.document['materials']:
            self.assertEqual((material['alphaMode'],material['alphaCutoff']),('MASK',1/65536))
            texture = scene.document['textures'][material['pbrMetallicRoughness']['baseColorTexture']['index']]
            self.assertEqual(scene.document['samplers'][texture['sampler']],
                             {'wrapS':10497,'wrapT':33071})
        # Logical0 samples remapped bank1 width8, while its own biases/shift
        # determine the horizon and U phase. Transparent rows crop vertically only.
        vertices = scene.draws[0][1]
        self.assertAlmostEqual(vertices[0][6],16)
        self.assertAlmostEqual(vertices[-1][6]-vertices[1][6],64)
        self.assertAlmostEqual(vertices[0][1]*136/1024+6,84)
        self.assertAlmostEqual(vertices[1][1]*136/1024+2,84)
        vertices = scene.draws[1][1]
        self.assertAlmostEqual(vertices[0][6],29)
        self.assertAlmostEqual(vertices[-1][6]-vertices[1][6],512)
        self.assertAlmostEqual(vertices[0][1]*136/1032+4,72)
        self.assertAlmostEqual(vertices[1][1]*136/1032+1,72)
        center = source.width*.1/2, -source.height*.1/2
        for _,vertices,triangles in scene.draws:
            for a,b,c,_ in triangles:
                p,q,r = [vertices[i] for i in (a,b,c)]
                ab,ac = [q[i]-p[i] for i in range(3)],[r[i]-p[i] for i in range(3)]
                cross = (ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0])
                self.assertGreater(cross[0]*(center[0]-p[0])+cross[2]*(center[1]-p[2]),0)
        other = CapturingScene(self.root/'sky-b')
        with patch.object(art,'decode_sky',return_value=decoded):
            convert.sky(source,other,{'render_nodes':[],'meta':{}},identities,.1)
        self.assertEqual(scene.texture_mappings,other.texture_mappings)
        self.assertEqual(len(identities.payloads),2)

    def test_sky_identity_uses_exact_wire_vertices_across_libm_ulps(self):
        art=convert.art
        # Actual Mac/WSL witness: the raw doubles differed, the MSH1 words did not.
        witness=(987.5624264756395,987.5624264756397)
        words=[model_format.quantize_vertex([x,0,0,0,0]) for x in witness]
        self.assertEqual(words[0],words[1])
        self.assertEqual(words[0][0],64720891)
        raw=dict(version=1,materials=[dict(id=1,argb=0xffffffff)],bindings=[],meshes=[
            dict(id=2,materials=[1],vertices=[[witness[0],0,0,0,0],[0,1,0,0,1],[0,0,1,1,1]],
                 triangles=[[0,1,2,0]])])
        canonical=copy.deepcopy(raw)
        canonical['meshes'][0]['vertices']=[[q/65536 for q in model_format.quantize_vertex(v)]
                                           for v in raw['meshes'][0]['vertices']]
        self.assertEqual(model_format.compile(raw,self.root),model_format.compile(canonical,self.root))
        original_sin,original_cos=math.sin,math.cos
        image=art.PixelImage(8,4,0,0,(0xff123456,)*32,bytes([1]*32))
        source=source_loop()
        def generate(controller,mode):
            directory=self.root/f'canonical-sky-{controller}-{mode}'
            directory.mkdir()
            identities=AssetIds();models=convert.Models(directory,identities)
            motion=art.SkyMotion(controller=controller,layer_mask=7,blend_mode=0,blend_a=0,blend_b=0,
                yaw_shift=(7,7,8),x_bias=(0,0,0),y_bias=(38,38,38),split_y=-1,split_layer=0,
                split_yaw_shift=0,split_x_bias=0,lightning_hide_layer=0,layer_source=(0,1,2),road_mode=0)
            decoded=art.SkyArt(tuple(art.SkyLayer(str(i),image,1,1,b'') for i in range(3)),
                motion,(0x001f,),0,0,0,0,0,0,b'')
            doc=dict(render_nodes=[],objects=[],meta={})
            def perturbed(function,value):
                result=function(value)
                if mode in ('above','below'):
                    for _ in range(3):
                        result=math.nextafter(result,math.inf if mode=='above' else -math.inf)
                elif mode=='changed':
                    result+=2/65536/1024 # Deliberate two-Q16-LSB geometry change.
                return result
            with patch.object(convert.math,'sin',side_effect=lambda v:perturbed(original_sin,v)), \
                 patch.object(convert.math,'cos',side_effect=lambda v:perturbed(original_cos,v)):
                convert.sky(source,Scene(directory/'course'),doc,identities,.1,models=models,
                            profiles={},sine=[0]*1024,decoded=decoded,palette_bindings=[])
            models.save()
            emitted=(directory/'models.json').read_bytes()
            payloads=model_format.compile(json.loads(emitted),directory)
            return emitted,doc,[row for row in payloads if row[0]['kind']==96]
        for controller in (1,2,4):
            baseline=generate(controller,'native')
            with self.subTest(controller=controller):
                self.assertEqual(baseline,generate(controller,'above'))
                self.assertEqual(baseline,generate(controller,'below'))
                changed=generate(controller,'changed')
                self.assertNotEqual([o['model'] for o in baseline[1]['objects']],
                                    [o['model'] for o in changed[1]['objects']])
                self.assertNotEqual([p for _,p in baseline[2]],[p for _,p in changed[2]])

    def test_clock_sky_models_preserve_logical_remap_crop_and_immutable_identity(self):
        art=convert.art
        front=art.PixelImage(4,6,0,0,tuple([0]*4+[0xff102030]*12+[0]*8))
        back=art.PixelImage(8,8,0,0,tuple([0]*16+[0xff000000]*32+[0]*16))
        empty=art.PixelImage(4,2,0,0,(0,)*8)
        motion=art.SkyMotion(controller=1,layer_mask=7,blend_mode=0,blend_a=0,blend_b=0,
            yaw_shift=(7,5,6),x_bias=(8,-4,0),y_bias=(10,-2,0),split_y=-1,split_layer=0,
            split_yaw_shift=0,split_x_bias=0,lightning_hide_layer=0,layer_source=(1,2,0),road_mode=0)
        decoded=art.SkyArt(tuple(art.SkyLayer(str(i),im,1,1,b'') for i,im in enumerate((front,back,empty))),
            motion,(0x001f,),0,0,0,0,0,0,b'')
        source=source_loop();before=copy.deepcopy((source,decoded))
        identities=AssetIds();directory=self.root/'clock-sky';directory.mkdir()
        models=convert.Models(directory,identities);profiles={};sine=[0]*1024
        doc=dict(render_nodes=['Terrain'],objects=[],meta={})
        with patch.object(art,'decode_sky',return_value=decoded):
            convert.sky(source,Scene(directory/'course'),doc,identities,.1,
                        models=models,profiles=profiles,sine=sine)
        self.assertEqual((source,decoded),before)
        self.assertEqual(doc['render_nodes'],['Terrain']) # No duplicate static sky.
        self.assertEqual([o['properties'][0]['value'] for o in doc['objects']],[0,2])
        self.assertEqual(len(profiles),1)
        profile=next(iter(profiles.values()))
        self.assertEqual(profile['layers'],[dict(layer=0,logical_width=8,radius_q16=1024*65536),
                                          dict(layer=2,logical_width=4,radius_q16=1040*65536)])
        self.assertEqual(profile['rules']['layer_source'],[1,2,0])
        self.assertFalse(profile['entry_delay'])
        self.assertEqual(doc['meta']['properties'][0]['value'],dict(kind=106,id=profile['profile_id']))
        for obj,first,last,horizon,radius in zip(doc['objects'],(2,1),(6,4),(84,74),(1024,1040)):
            self.assertEqual(obj['position'],[source.width*.1/2,0,-source.height*.1/2])
            mesh=models.meshes[obj['model']['id']];verts=mesh['vertices']
            self.assertEqual(verts[0][2],model_format.quantize_vertex(
                [0,0,(horizon-last)*radius/136,0,0])[2]/65536)
            self.assertEqual(verts[1][2],model_format.quantize_vertex(
                [0,0,(horizon-first)*radius/136,0,0])[2]/65536)
            for a,b,c,_ in mesh['triangles']:
                p,q,r=[verts[i] for i in (a,b,c)]
                ab,ac=[q[i]-p[i] for i in range(3)],[r[i]-p[i] for i in range(3)]
                cross=(ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2])
                self.assertGreater(-cross[0]*p[0]-cross[1]*p[1],0)
            material=models.materials[mesh['materials'][0]]
            self.assertEqual((material['wrap_u'],material['wrap_v']),(True,False))
        first_mesh=models.meshes[doc['objects'][0]['model']['id']]
        self.assertEqual(first_mesh['vertices'][0][3],16)
        self.assertEqual(first_mesh['vertices'][-1][3]-first_mesh['vertices'][0][3],64)
        clamp=models.material(art.PixelImage(8,4,0,0,back.pixels[16:48]))
        self.assertNotEqual(clamp,first_mesh['materials'][0])
        self.assertEqual(models.material(art.PixelImage(8,4,0,0,back.pixels[16:48])),clamp)
        models.save()
        payloads=model_format.compile(json.loads((directory/'models.json').read_text()),directory)
        self.assertTrue(any(header['kind']==96 for header,_ in payloads))
        repeat=dict(render_nodes=['Terrain'],objects=[],meta={})
        with patch.object(art,'decode_sky',return_value=decoded):
            convert.sky(source,Scene(directory/'repeat'),repeat,identities,.1,
                        models=models,profiles=profiles,sine=sine)
        self.assertEqual(repeat,doc);self.assertEqual(len(profiles),1)
        other_source=copy.deepcopy(source);other_source.width*=2;other_source.height*=2
        moved=dict(render_nodes=[],objects=[],meta={})
        with patch.object(art,'decode_sky',return_value=decoded):
            convert.sky(other_source,Scene(directory/'moved'),moved,identities,.1,
                        models=models,profiles=profiles,sine=sine)
        self.assertEqual([o['model'] for o in moved['objects']],[o['model'] for o in doc['objects']])
        self.assertEqual(len(profiles),1)
        with patch.object(art,'decode_sky',return_value=convert.replace(decoded,
                motion=convert.replace(motion,controller=3))):
            rise_profiles={}
            convert.sky(source,Scene(directory/'rise'),dict(render_nodes=[],objects=[],meta={}),
                        identities,.1,models=models,profiles=rise_profiles,sine=sine)
        self.assertTrue(next(iter(rise_profiles.values()))['entry_delay'])
        with patch.object(art,'decode_sky',return_value=decoded),self.assertRaisesRegex(ValueError,'course=.*sky controller=1'):
            convert.sky(source,Scene(directory/'missing'),dict(render_nodes=[],objects=[],meta={}),identities,.1)

    def test_speed_pan_preserves_front_once_middle_double_yaw_repeats(self):
        art=convert.art
        image=art.PixelImage(8,4,0,0,(0xff264879,)*32)
        motion=art.SkyMotion(controller=2,yaw_shift=(7,7,8),layer_mask=7,
            blend_mode=0,blend_a=0,blend_b=0,x_bias=(8,-4,0),y_bias=(38,38,38),
            split_y=-1,split_layer=0,split_yaw_shift=0,split_x_bias=0,
            lightning_hide_layer=0,layer_source=(2,0,1),road_mode=0)
        decoded=art.SkyArt(tuple(art.SkyLayer(str(i),image,1,1,b'') for i in range(3)),
            motion,(0x001f,),0,0,0,0,0,0,b'')
        source=source_loop();before=copy.deepcopy((source,decoded))
        directory=self.root/'speed-pan';directory.mkdir()
        identities=AssetIds();models=convert.Models(directory,identities);profiles={}
        doc=dict(render_nodes=[],objects=[],meta={})
        convert.sky(source,Scene(directory/'course'),doc,identities,.1,
                    models=models,profiles=profiles,sine=[0]*1024,decoded=decoded)
        self.assertEqual(doc['render_nodes'],[])
        self.assertEqual([o['properties'][0]['value'] for o in doc['objects']],[0,1,2])
        for obj,repeat,bias in zip(doc['objects'],(1,128,32),(8,-4,0)):
            vertices=models.meshes[obj['model']['id']]['vertices']
            self.assertEqual(vertices[0][3],(120+bias)/8)
            self.assertEqual(vertices[-1][3]-vertices[0][3],repeat)
        record=next(iter(profiles.values()))
        self.assertEqual(record['rules']['layer_source'],[2,0,1])
        self.assertFalse(record['entry_delay'])
        self.assertEqual((source,decoded),before)

    def test_generated_clock_sky_profile_is_conditional_and_reproducible(self):
        sections=source_fixture.fixture();identity=source_fixture.COURSE
        header,world=sections[5,identity]
        raster=bytes((1,1,0x80,0x7f,2,0x7f,0x83,0x7f,2,3,3,0x7f,0x7f,0x7f,0x7f,0x7f))
        sections[5,identity]=(header,world[:28]+raster+world[44:])
        sections[28,identity]=({'version':4},struct.pack('<4I',0x314e4353,0,0,0))
        sine=[round(math.sin(i*math.tau/1024)*32768) for i in range(1024)]
        motion_bytes=struct.pack('<9I1024i',0x31544f4d,1024,64,8,8,8,16,10,4,*sine)
        for ident in (3,9):sections[13,ident]=({'version':1},motion_bytes)
        manifest=[]
        for (kind,ident),(metadata,payload) in sorted(sections.items()):
            name=f'sky-{kind}-{ident}.bin';(self.root/name).write_bytes(payload)
            manifest.append(dict(kind=kind,content_id=ident,version=metadata['version'],path=name))
        path=self.root/'sky-pack.json';path.write_text(json.dumps(dict(sections=manifest)))
        base=self.root/'sky-base.pack';build_asset_pack.build_pack(path,base);original=base.read_bytes()
        image=convert.art.PixelImage(8,4,0,0,(0xff264879,)*32)
        motion=convert.art.SkyMotion(controller=6,yaw_shift=(7,7,8),layer_mask=1,
            blend_mode=0,blend_a=0,blend_b=0,x_bias=(0,0,0),y_bias=(38,38,38),
            split_y=-1,split_layer=0,split_yaw_shift=0,split_x_bias=0,
            lightning_hide_layer=0,layer_source=(0,1,2),road_mode=0)
        decoded=convert.art.SkyArt(tuple(convert.art.SkyLayer(str(i),image,1,1,b'') for i in range(3)),
            motion,(0x001f,),0,0,0,0,0,0,b'')
        results=[]
        for attempt in range(2):
            with patch.object(convert.art,'decode_sky',return_value=decoded):
                generated=convert.generate(base,self.root/f'clock-generated-{attempt}',courses=[identity])
            rules=json.loads((generated/'skies.json').read_text())
            self.assertEqual(rules['profiles'][0]['sine'],sine)
            self.assertFalse((generated/'source-actors.json').exists())
            document=json.loads((generated/f'courses3d/{identity}/course.json').read_text())
            self.assertFalse(any(p['key'].startswith('source:progress-') for p in document['meta']['properties']))
            self.assertEqual(len([o for o in document['objects'] if any(
                p['key']=='mksc:sky-layer' for p in o['properties'])]),1)
            self.assertFalse(any(str(node).startswith('Sky-') for node in document['render_nodes']))
            folder,archive=convert.mod_kit.build(generated,None,self.root/f'clock-mods-{attempt}')
            results.append(((generated/'skies.json').read_bytes(),(folder/'content.pack').read_bytes(),archive.read_bytes()))
        self.assertEqual(results[0],results[1]);self.assertEqual(base.read_bytes(),original)
        for controller in (2,5):
            outputs=[]
            for attempt in range(2):
                with patch.object(convert.art,'decode_sky',return_value=convert.replace(decoded,
                        motion=convert.replace(motion,controller=controller))):
                    generated=convert.generate(base,self.root/f'progress-{controller}-{attempt}',courses=[identity])
                rules=json.loads((generated/'skies.json').read_text())
                self.assertEqual(rules['profiles'][0]['rules']['controller'],controller)
                self.assertEqual(rules['profiles'][0]['sine'],sine)
                self.assertFalse((generated/'source-actors.json').exists())
                document=json.loads((generated/f'courses3d/{identity}/course.json').read_text())
                props={p['key']:p['value'] for p in document['meta']['properties']}
                count=props['source:progress-stage-count']
                raw=[props[f'source:progress-raw:{i}'] for i in range(count)]
                self.assertEqual(raw[0],0)
                self.assertTrue(all(a<b for a,b in zip(raw,raw[1:])))
                self.assertEqual(set(g['stage'] for g in document['checkpoints']),set(range(count)))
                self.assertFalse(any(str(n).startswith('Sky-') for n in document['render_nodes']))
                objects=[o for o in document['objects'] if any(p['key']=='mksc:sky-layer' for p in o['properties'])]
                self.assertEqual(len(objects),1)
                models=json.loads((generated/'models.json').read_text())
                mesh=next(m for m in models['meshes'] if m['id']==objects[0]['model']['id'])
                self.assertEqual(mesh['vertices'][-1][3]-mesh['vertices'][0][3],1 if controller==2 else 64)
                folder,archive=convert.mod_kit.build(generated,None,self.root/f'progress-mod-{controller}-{attempt}')
                outputs.append(((generated/'skies.json').read_bytes(),(folder/'content.pack').read_bytes(),archive.read_bytes()))
            self.assertEqual(outputs[0],outputs[1])
        self.assertEqual(base.read_bytes(),original)
        for controller in (0,):
            with patch.object(convert.art,'decode_sky',return_value=convert.replace(decoded,
                    motion=convert.replace(motion,controller=controller))):
                generated=convert.generate(base,self.root/f'static-generated-{controller}',courses=[identity])
            self.assertFalse((generated/'skies.json').exists())
            document=json.loads((generated/f'courses3d/{identity}/course.json').read_text())
            self.assertIn('Sky-0',document['render_nodes'])
            self.assertFalse(any(p['key'].startswith('source:progress-') for p in document['meta']['properties']))

    def test_scenery_full_anchor_inverse_scale_and_unbound_model_payload(self):
        art = convert.art
        pixels = (0xff000000,0,0xff36b9de,0,0)*9
        image = art.PixelImage(5,9,-3,-7,pixels)
        bank = art.SceneryBank((image,),(0,),0)
        placements = tuple(art.SceneryPlacement(20*65536,30*65536,7,0,affine,
            0xb0000,index,2*65536,(0,)) for index,affine in enumerate((128,256)))
        decoded = art.SceneryArt(placements,(bank,))
        models = convert.Models(self.root,AssetIds())
        document = {'objects':[]}
        with patch.object(art,'decode_scenery',return_value=decoded):
            convert.scenery(source_loop(),document,models,.1)
        self.assertEqual(len(models.meshes),1)
        self.assertEqual(len(models.materials),1)
        mesh = next(iter(models.meshes.values()))
        self.assertEqual(mesh['vertices'],[[-3,0,-2,0,1],[2,0,-2,1,1],
                                           [2,0,7,1,0],[-3,0,7,0,0]])
        for placed,factor in zip(document['objects'],(.2,.1)):
            self.assertEqual(placed['position'],[2,.2,-3])
            self.assertEqual(placed['scale'],[factor]*3)
            self.assertEqual(placed['model'],{'kind':96,'id':mesh['id']})
            self.assertIn({'key':'mksc:billboard','type':'boolean','value':True},placed['properties'])
        models.save()
        authored = model_format.load(self.root/'models.json')
        self.assertEqual(authored['bindings'],[])
        payloads = {meta['kind']:data for meta,data in model_format.compile(authored,self.root)}
        self.assertEqual(set(payloads),{96,97})
        self.assertEqual(struct.unpack_from('<5i',payloads[96],20),(-3*65536,0,-2*65536,0,65536))
        material = payloads[97]
        self.assertEqual(struct.unpack_from('<HH',material,16+12),(5,9))
        self.assertEqual(struct.unpack_from('<45I',material,16+32),pixels)


if __name__=='__main__':
    unittest.main()
