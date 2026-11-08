"""ROM-free connected-strip distance caps and independent discrete flights."""
import copy
import json
import math
from pathlib import Path
import sys
from types import SimpleNamespace as NS
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import mesh_kart_format
import course_3d_launch as launch
import course_3d_gameplay as gameplay


def fixture():
    width,height=64,16
    cells=bytearray(width*height)
    for x in (32,33):
        cells[8*width+x]=1
    for x in (31,11):
        for y in (7,8,9):
            cells[y*width+x]=2
    # Unvisited same-kind strip is a distinct four-neighbor component.
    cells[12*width+44]=1
    source=NS(course_id=70043,map_width=width,map_height=height,
              materials=bytes((0x40,0x10,0x84,0x22)),tilemap=bytes(cells))
    rows=[[(360,64),(32,64),(32,112),(360,112)]]
    return source,rows


def chain_fixture():
    width,height=24,128
    cells=bytearray([3]*(width*height))
    for y in range(height):
        for x in range(8,17):
            cells[y*width+x]=0
        cells[y*width+7]=cells[y*width+17]=2
    for y in (63,79,95):
        for x in range(8,17):
            cells[y*width+x]=1
    for start in (65,81,97):
        for y in range(start,start+4):
            for x in range(8,17):
                cells[y*width+x]=3
    source=NS(course_id=70067,map_width=width,map_height=height,arena=False,
        materials=bytes((0x44,0x10,0x80,0x24)),tilemap=bytes(cells),
        raster_width=12,raster_height=64,raster=bytes(12*64))
    rows=[[(92,328),(108,416),(100,592),(100,728),(100,840),(116,936),
           (168,936),(168,328)]]
    rows += [[(92,328),(100,416),(100,592),(100,728),(100,840),(116,936),
              (168,936),(168,328)]]*2
    source.route_rows=tuple(tuple(NS(x=x,y=y,mode=0) for x,y in row) for row in rows)
    profile=mesh_kart_format.resolved(json.loads((Path(__file__).resolve().parents[3]/'portable/data/mesh-kart.json').read_text()))
    profile['kart_radius_q16']=16384
    profile['ground_snap_q16']=16384
    return source,rows,profile


def qdt(value):
    # Native C division truncates toward zero; use integer magnitudes.
    n=value*4389
    return n//262144 if n>=0 else -((-n)//262144)


def partition_fixture(overlap=False):
    source,_,profile=chain_fixture()
    width,height=48,128
    cells=bytearray(width*height)
    for y in (63,79,95):
        for x in range(8,42): cells[y*width+x]=1
    first_end=70 if overlap else 69
    for y in list(range(65,first_end))+list(range(81,85))+list(range(97,101)):
        for x in range(8,42): cells[y*width+x]=3
    for y in range(66,first_end):
        for x in range(14 if overlap else 26,34): cells[y*width+x]=0
    source.map_width=width
    source.tilemap=bytes(cells)
    source.raster_width=24
    source.raster=bytes(24*64)
    # The long-gap entry straddles two source cells. The short-gap lane is
    # either spatially separate, or enters those same cells diagonally.
    rows=[[(104,328),(104,416),(104,592),(104,728),(104,840),(104,936),
           (360,936),(360,328)]]
    rows.append([(80,328),(80,416),(116,552),(148,608),(148,728),(148,840),
                 (148,936),(360,936),(360,328)] if overlap else
                [(220,328),(220,416),(220,552),(240,608),(240,728),(240,840),
                 (240,936),(360,936),(360,328)])
    source.route_rows=tuple(tuple(NS(x=x,y=y,mode=0) for x,y in row) for row in rows)
    return source,rows,profile


class LaunchPlanTests(unittest.TestCase):
    # These geometric boundary fixtures explicitly describe the original
    # Earth-gravity adaptation. Keep their discrete oracles meaningful when
    # newly authored courses select a different default.
    def setUp(self):
        for name,value in (('_G',9.8),('_SNAP',.25),('source_launch',lambda kind:{0:9,2:6,14:3}[kind])):
            patcher=patch.object(launch,name,value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def test_dry_alternate_constrains_shared_cap_and_all_dry_group_stays_positive(self):
        source,rows,profile=partition_fixture()
        cells=bytearray(source.tilemap)
        for y in range(65,69):
            for x in range(26,34): cells[y*source.map_width+x]=0
        source.tilemap=bytes(cells)
        saved=copy.deepcopy((source,rows,profile))
        documents=gameplay.routes(source,.1,adapted=rows)
        repaired,caps=launch._repair_chains(source,rows,.1,
            gameplay._RouteClearance(source,.1),documents,profile,directed=True)
        self.assertEqual(repaired,rows)
        # The dry approach's early bend cannot accept the wet lane's cap;
        # partitioning must solve a strictly positive all-dry cell group.
        self.assertGreater(caps[27,63],0)
        self.assertLess(caps[27,63],caps[12,63])
        distance=caps[27,63]
        low=5-.5-max(profile['brake_deceleration_q16'],profile['coast_deceleration_q16'])/65536*launch._DT
        high=16+.5+profile['acceleration_limit_q16']/65536*launch._DT
        for speed in (low,5,8,12,16,high):
            for phase in range(9):
                y=50.4+speed*launch._DT*phase/8
                vz=math.floor(min(9,9.8*distance/(2*speed))*65536)
                height=0
                for _ in range(200):
                    vz-=qdt(round(9.8*65536));height+=qdt(vz)
                    y+=speed*launch._DT
                    if vz<=0 and height<=65536/4:
                        # The .3m reserved endpoint margin absorbs the final
                        # discrete step after the analytic landing window.
                        self.assertGreater(y,51.5) # Body clears the automatic takeoff strip.
                        self.assertLess(y,57.44)
                        break
                else:
                    self.fail('dry alternate failed to land before its retained bend')
        self.assertEqual((source,rows,profile),saved)
        # A longer dry deadline permits one shared value: the alternate is
        # retained as a landing constraint rather than silently dropped.
        longer=copy.deepcopy(rows);longer[1][3]=(240,700)
        _,shared=launch._repair_chains(source,longer,.1,
            gameplay._RouteClearance(source,.1),gameplay.routes(source,.1,adapted=longer),
            profile,directed=True)
        self.assertEqual(shared[27,63],shared[12,63])
        extended=copy.deepcopy(source)
        repeated=bytearray(cells)
        for y in range(64,73): repeated[y*source.map_width+27]=1
        extended.tilemap=bytes(repeated)
        unchanged,partial=launch.repair_chains(extended,rows,.1,
            gameplay._RouteClearance(extended,.1),documents,profile)
        self.assertIs(unchanged,rows)
        self.assertFalse(any(y<=73 for x,y in partial))
        self.assertTrue(any(y==79 for x,y in partial))
        # A thin nonordinary support row invalidates this entire owner,
        # including otherwise valid wet-lane cells, but not later components.
        invalid=bytearray(cells)
        source.materials+=bytes((0x5a,))
        invalid[70*source.map_width+27]=4
        source.tilemap=bytes(invalid)
        unchanged,partial=launch.repair_chains(source,rows,.1,
            gameplay._RouteClearance(source,.1),documents,profile)
        self.assertIs(unchanged,rows)
        self.assertFalse(any(y==63 for x,y in partial))
        self.assertTrue(any(y==79 for x,y in partial))

    def test_disjoint_takeoff_cells_admit_independent_safe_caps(self):
        source,rows,profile=partition_fixture()
        saved=copy.deepcopy((source,rows,profile))
        documents=gameplay.routes(source,.1,adapted=rows)
        repaired,caps=launch._repair_chains(source,rows,.1,
            gameplay._RouteClearance(source,.1),documents,profile,directed=True)
        self.assertEqual(repaired,rows)
        self.assertEqual(caps[12,63],caps[13,63]) # A body straddling the tile edge sees one cap.
        self.assertGreater(caps[12,63],caps[27,63])
        self.assertNotIn((20,63),caps) # No proof changes the unvisited middle of the strip.
        low=5-.5-max(profile['brake_deceleration_q16'],profile['coast_deceleration_q16'])/65536*launch._DT
        high=16+.5+profile['acceleration_limit_q16']/65536*launch._DT
        for cell,water_end,landing_end in ((12,552,624),(27,528,571.4)):
            distance=caps[cell,63]
            for speed in (low,5,8,12,16,high):
                for phase in range(9):
                    y=round(50.4*65536)+round(qdt(round(speed*65536))*phase/8)
                    vz=math.floor(min(9,9.8*distance/(2*speed))*65536)
                    height=0
                    for _ in range(200):
                        vz-=qdt(round(9.8*65536));height+=qdt(vz)
                        y+=qdt(round(speed*65536))
                        if vz<=0 and height<=65536/4:
                            self.assertGreater(y/65536,water_end*.1)
                            self.assertLess(y/65536,landing_end*.1)
                            break
                    else:
                        self.fail('partitioned flight failed to land in its dry window')
        self.assertEqual((source,rows,profile),saved)
        self.assertEqual((repaired,caps),launch._repair_chains(source,rows,.1,
            gameplay._RouteClearance(source,.1),documents,profile,directed=True))

    def test_overlapping_body_contacts_refuse_incompatible_caps_atomically(self):
        source,rows,profile=partition_fixture(overlap=True)
        saved=copy.deepcopy((source,rows,profile))
        # Each real approach can clear its own gap and land before its bend.
        for row in rows:
            _,caps=launch._repair_chains(source,[row],.1,
                gameplay._RouteClearance(source,.1),gameplay.routes(source,.1,adapted=[row]),
                profile,directed=True)
            self.assertTrue(any(y==63 for x,y in caps))
        # Their takeoff bodies share cells, so two independently valid but
        # incompatible values must never be published for that shared surface.
        documents=gameplay.routes(source,.1,adapted=rows)
        unchanged,partial=launch._repair_chains(source,rows,.1,
            gameplay._RouteClearance(source,.1),documents,profile,directed=True)
        self.assertIs(unchanged,rows)
        self.assertFalse(any(y==63 for x,y in partial))
        self.assertTrue(any(y==79 for x,y in partial))
        self.assertEqual(launch.repair_chains(source,rows,.1,
            gameplay._RouteClearance(source,.1),documents,profile),(rows,partial))
        self.assertEqual((source,rows,profile),saved)

    def test_chained_strips_align_and_land_before_next_grounded_launch(self):
        source,rows,profile=chain_fixture()
        saved=copy.deepcopy((source,rows,profile))
        # New isolated-ray admission must not preempt the chain owner's
        # existing aligned/continuous-flight planning or its guide placement.
        self.assertEqual(launch.plan(source,rows,.1),{})
        documents=gameplay.routes(source,.1,adapted=rows)
        repaired,caps=launch.repair_chains(source,rows,.1,
            gameplay._RouteClearance(source,.1),documents,profile)
        self.assertEqual((source,rows,profile),saved)
        self.assertEqual((repaired,caps),launch.repair_chains(source,rows,.1,
            gameplay._RouteClearance(source,.1),documents,profile))
        self.assertEqual(len(caps),27)
        for original,aligned in zip(rows,repaired):
            self.assertEqual([p for p in aligned if p in original],original)
            self.assertTrue(all(0<=x<192 and 0<=y<1024 for x,y in aligned))
            # At least3m of genuinely straight ground before takeoff, rather
            # than retaining a diagonal heading and widening the next gate.
            lead=[p for p in aligned if 440<p[1]<504]
            self.assertGreaterEqual(len(lead),2)
            self.assertTrue(all(x==100 for x,y in lead))
        # Original diagonal launch escapes a72-unit road during its flight.
        self.assertLess(100-3.359*(68-50.4)/15.044/.1,64)
        # Independent fixed-point integration at nominal route speeds/phases;
        # the flight's vz is NEVER reset merely because another node exists.
        gravity=round(9.8*65536)
        for strip,end in ((63,552),(79,680),(95,808)):
            distance=caps[8,strip]
            self.assertEqual({caps[x,strip] for x in range(8,17)},{distance})
            low=5-.5-max(profile['brake_deceleration_q16'],profile['coast_deceleration_q16'])/65536*4389/262144
            high=16+.5+profile['acceleration_limit_q16']/65536*4389/262144
            for speed in (low,5,8,12,16,high):
                for phase in range(9):
                    vz=min(9*65536,math.floor(9.8*distance/(2*speed)*65536))
                    y=strip*8*.1+speed*4389/262144*phase/8
                    h=0
                    for tick in range(200):
                        vz-=qdt(gravity)
                        h+=qdt(vz)
                        y+=speed*4389/262144
                        code=gameplay.material(source,100,y/.1)
                        if vz<=0 and h<=65536/4:
                            self.assertEqual(code,0x44,(strip,speed,phase,y,h))
                            self.assertGreater(y,end*.1)
                            self.assertLess(y,(strip*8+128)*.1)
                            break
                    else:
                        self.fail('flight never reached its positive dry landing window')

    def test_directed_shallow_diagonal_caps_have_discrete_dry_landings(self):
        source,_,profile=chain_fixture()
        rows=[[(92,328),(96,416),(101.28,592),(105.36,728),
               (108.72,840),(116,936),(168,936),(168,328)]]*3
        source.route_rows=tuple(tuple(NS(x=x,y=y,mode=0) for x,y in row) for row in rows)
        original=copy.deepcopy(rows)
        repaired,caps=launch._repair_chains(source,rows,.1,
            gameplay._RouteClearance(source,.1),gameplay.routes(source,.1,adapted=rows),
            profile,directed=True)
        self.assertEqual(repaired,original) # No new steering target or guide.
        self.assertEqual(len(caps),27)
        low=5-.5-max(profile['brake_deceleration_q16'],profile['coast_deceleration_q16'])/65536*launch._DT
        high=16+.5+profile['acceleration_limit_q16']/65536*launch._DT
        norm=math.hypot(.03,1)
        for strip,water_end,next_strip in ((63,552,632),(79,680,760),(95,808,936)):
            entry=strip*8
            distance=caps[8,strip]
            for speed in (low,5,8,12,16,high):
                vx=round(speed*.03/norm*65536)
                vy=round(speed/norm*65536)
                actual=math.hypot(vx,vy)/65536
                for phase in range(9):
                    x=round((96+.03*(entry-416))*.1*65536)+round(qdt(vx)*phase/8)
                    y=round(entry*.1*65536)+round(qdt(vy)*phase/8)
                    vz=math.floor(min(9,9.8*distance/(2*actual))*65536)
                    h=0
                    for tick in range(200):
                        vz-=qdt(round(9.8*65536))
                        h+=qdt(vz)
                        x+=qdt(vx)
                        y+=qdt(vy)
                        if vz<=0 and h<=65536/4:
                            self.assertEqual(gameplay.material(source,x/65536/.1,y/65536/.1),0x44)
                            self.assertGreater(y/65536,water_end*.1)
                            self.assertLess(y/65536,(next_strip-8)*.1)
                            break
                    else:
                        self.fail('directed discrete flight never landed')

    def test_next_lateral_launch_and_true_tube_exit_bound_directed_flight(self):
        source,_,profile=chain_fixture()
        rows=[[(100,328),(100,456),(108,584),(88,712),(76,840),
               (100,936),(168,936),(168,328)]]*3
        source.route_rows=tuple(tuple(NS(x=x,y=y,mode=0) for x,y in row) for row in rows)
        saved=copy.deepcopy(rows)
        repaired,caps=launch._repair_chains(source,rows,.1,
            gameplay._RouteClearance(source,.1),gameplay.routes(source,.1,adapted=rows),
            profile,directed=True)
        self.assertEqual(repaired,saved)
        self.assertEqual(len(caps),27)
        # First takeoff's next launch is more than .8m off its incoming ray.
        # It must still land after ONE gap, rather than demand clearing both.
        norm=math.hypot(8,128)
        entry=(103,504)
        next_entry=(100.5,632)
        perpendicular=abs((next_entry[0]-entry[0])*128-(next_entry[1]-entry[1])*8)/norm
        self.assertGreater(perpendicular*.1,.8)
        end_projection=((108-entry[0])*8+(584-entry[1])*128)/norm
        outside_projection=((88-entry[0])*8+(712-entry[1])*128)/norm
        outside_deviation=abs((88-entry[0])*128-(712-entry[1])*8)/norm
        tube_end=(end_projection+(outside_projection-end_projection)*8/outside_deviation)*.1-.3
        low=5-.5-max(profile['brake_deceleration_q16'],profile['coast_deceleration_q16'])/65536*launch._DT
        high=16+.5+profile['acceleration_limit_q16']/65536*launch._DT
        for speed in (low,5,8,12,16,high):
            vx,vy=round(speed*8/norm*65536),round(speed*128/norm*65536)
            actual=math.hypot(vx,vy)/65536
            for phase in range(9):
                x=round(entry[0]*.1*65536)+round(qdt(vx)*phase/8)
                y=round(entry[1]*.1*65536)+round(qdt(vy)*phase/8)
                vz=math.floor(min(9,9.8*caps[8,63]/(2*actual))*65536)
                h=0
                for _ in range(200):
                    vz-=qdt(round(9.8*65536)); h+=qdt(vz)
                    x+=qdt(vx); y+=qdt(vy)
                    if vz<=0 and h<=65536/4:
                        self.assertEqual(gameplay.material(source,x/65536/.1,y/65536/.1),0x44)
                        self.assertGreater(y/65536,55.2)
                        distance=math.hypot(x/65536-entry[0]*.1,y/65536-entry[1]*.1)
                        self.assertLess(distance,tube_end)
                        self.assertLess(y/65536,(632-8)*.1)
                        break
                else:
                    self.fail('lateral-next-launch flight did not land')
        # Triangle inequality bounds |interpolated deviation| even for signed
        # crossings; no far outside anchor may become the flight deadline.
        for before,after in ((7,20),(-7,-20),(-7,20),(7,-20),(0,20)):
            fraction=launch._tube_exit_fraction(before,after,8)
            self.assertGreaterEqual(fraction,0)
            self.assertLessEqual(fraction,1)
            for step in range(17):
                t=fraction*step/16
                self.assertLessEqual(abs(before+(after-before)*t),8+1e-12)

    def test_chain_negative_horizontal_axis_keeps_anchor_order(self):
        source,rows,profile=chain_fixture()
        cells=bytearray(len(source.tilemap))
        for y in range(source.map_height):
            for x in range(source.map_width):
                cells[x*128+127-y]=source.tilemap[y*24+x]
        source.map_width,source.map_height=128,24
        source.tilemap=bytes(cells)
        rows=[[(1024-y,x) for x,y in row] for row in rows]
        source.route_rows=tuple(tuple(NS(x=x,y=y,mode=0) for x,y in row) for row in rows)
        repaired,caps=launch.repair_chains(source,rows,.1,
            gameplay._RouteClearance(source,.1),gameplay.routes(source,.1,adapted=rows),profile)
        self.assertEqual(len(caps),27)
        for before,after in zip(rows,repaired):
            self.assertEqual([p for p in after if p in before],before)

    def test_northbound_chain_reuses_flight_axis_before_late_bend(self):
        source,_,profile=chain_fixture()
        source.tilemap=b''.join(source.tilemap[y*24:(y+1)*24] for y in reversed(range(128)))
        rows=[[(100,696),(100,608),(100,432),(100,288),(84,152),(84,88),
               (168,88),(168,696)]]*3
        source.route_rows=tuple(tuple(NS(x=x,y=y,mode=0) for x,y in row) for row in rows)
        original=copy.deepcopy(rows)
        repaired,caps=launch.repair_chains(source,rows,.1,
            gameplay._RouteClearance(source,.1),gameplay.routes(source,.1,adapted=rows),profile)
        self.assertEqual(rows,original)
        self.assertEqual(len(caps),27)
        for row in repaired:
            self.assertEqual([p for p in row if p in original[0]],original[0])
            # The final target bends west AFTER the jump. A new axis84 lead-in
            # would cross the previous lava gap; preserve the proved100 axis.
            self.assertTrue(all(x==100 for x,y in row if 264<y<288))
        altered=dict(profile,ground_snap_q16=32768)
        with self.assertRaisesRegex(launch.LaunchError,'ballistic ride/snap'):
            launch.repair_chains(source,rows,.1,gameplay._RouteClearance(source,.1),
                gameplay.routes(source,.1,adapted=rows),altered)
        altered=dict(profile,handling_turn_rate_q16=0)
        with self.assertRaisesRegex(launch.LaunchError,'handling_turn_rate'):
            launch.repair_chains(source,rows,.1,gameplay._RouteClearance(source,.1),
                gameplay.routes(source,.1,adapted=rows),altered)

    def test_discrete_apex_boundary_and_previously_proved_chain(self):
        source,rows,profile=chain_fixture()
        documents=gameplay.routes(source,.1,adapted=rows)
        for route in documents:
            if route['kind']=='cpu':
                for node in route['nodes']:
                    node['speed']=36
        profile['forward_speed_q16']=36*65536
        _,caps=launch.repair_chains(source,rows,.1,gameplay._RouteClearance(source,.1),documents,profile)
        speed=36+.5+profile['acceleration_limit_q16']/65536*4389/262144
        distance=caps[8,63]
        shallow=False
        for phase in range(9):
            vz=math.floor(9.8*distance/(2*speed)*65536)
            y=50.4+speed*4389/262144*phase/8
            h=0
            for tick in range(200):
                vz-=qdt(round(9.8*65536))
                h+=qdt(vz)
                y+=speed*4389/262144
                code=gameplay.material(source,100,y/.1)
                shallow |= code==0x24 and vz>0 and 0<h<65536/4
                if vz<=0 and h<=65536/4:
                    self.assertEqual(code,0x44,(phase,y,h,vz))
                    self.assertGreater(y,55.2)
                    self.assertLess(y,62.4)
                    break
            else:
                self.fail('boundary flight did not land')
        self.assertTrue(shallow) # Ascending low hops are legal; descending snap is not.
        existing={(x,63):12. for x in range(8,17)}
        untouched,new=launch.repair_chains(source,rows,.1,gameplay._RouteClearance(source,.1),
            documents,profile,existing_caps=existing)
        self.assertEqual(untouched,rows)
        self.assertEqual(new,{})

    def test_curved_alternate_lane_fallback_preserves_all_anchors(self):
        source,rows,profile=chain_fixture()
        # One lane stays in a diagonal chord tube, but its two launch entries
        # are 2.4m apart laterally: no common .8m constant-flight-axis tube.
        # The other lanes remain straight and share all three SURF components.
        rows[1]=[(112,328),(112,416),(112,520),(88,632),(88,728),
                 (88,840),(112,936),(168,936),(168,328)]
        source.route_rows=tuple(tuple(NS(x=x,y=y,mode=0) for x,y in row) for row in rows)
        original=copy.deepcopy(rows)
        documents=gameplay.routes(source,.1,adapted=rows)
        repaired,caps=launch.repair_chains(source,rows,.1,
            gameplay._RouteClearance(source,.1),documents,profile)
        self.assertEqual(repaired,original)
        # Its first owner's real bend exits the tube too soon on one lane;
        # other owners remain independently provable without any guide edits.
        self.assertFalse(any(y==63 for x,y in caps))
        self.assertTrue(any(y==79 for x,y in caps))
        # Already-proved single/continuous-flight caps remain caller-owned;
        # this classification must neither erase them nor manufacture new caps.
        existing={(x,63):12. for x in range(8,17)}
        before=dict(existing)
        self.assertEqual(launch.repair_chains(source,rows,.1,
            gameplay._RouteClearance(source,.1),documents,profile,
            existing_caps=existing),(original,{}))
        self.assertEqual(existing,before)

    def test_unproved_chain_returns_authored_without_partial_changes(self):
        source,rows,profile=chain_fixture()
        documents=gameplay.routes(source,.1,adapted=rows)
        for route in documents:
            if route['kind']=='cpu':
                for node in route['nodes']:
                    node['speed']=128
        profile['forward_speed_q16']=128*65536
        self.assertEqual(launch.repair_chains(source,rows,.1,
            gameplay._RouteClearance(source,.1),documents,profile),(rows,{}))
        source,rows,profile=chain_fixture()
        cells=bytearray(source.tilemap)
        for x in range(8,17):
            cells[62*24+x]=3
        source.tilemap=bytes(cells)
        unchanged,partial=launch.repair_chains(source,rows,.1,
            gameplay._RouteClearance(source,.1),gameplay.routes(source,.1,adapted=rows),profile)
        self.assertIs(unchanged,rows)
        self.assertFalse(any(y==63 for x,y in partial))
        self.assertTrue(any(y==79 for x,y in partial))

    def test_directed_owner_decline_does_not_swallow_work_budget_errors(self):
        source,rows,profile=chain_fixture()
        documents=gameplay.routes(source,.1,adapted=rows)
        saved=copy.deepcopy((source,rows,documents,profile))
        # A named resource failure inside a flight context is not optional
        # geometric nonadmission, even after other owners were staged.
        error=launch.LaunchError('course=70067 chained launch: terrain proof budget exhausted')
        with patch.object(launch,'_span',side_effect=error):
            with self.assertRaisesRegex(launch.LaunchError,'course=70067.*budget exhausted'):
                launch._repair_chains(source,rows,.1,gameplay._RouteClearance(source,.1),
                                     documents,profile,directed=True)
        self.assertEqual((source,rows,documents,profile),saved)

    def test_late_unproved_chain_rolls_back_early_guides_and_invalid_stays_error(self):
        source,rows,profile=chain_fixture()
        # Remove the final dry island. Earlier flights remain provable, but
        # the final strip has no grounded landing before its retained bend.
        cells=bytearray(source.tilemap)
        for y in range(101,117):
            for x in range(8,17):
                cells[y*24+x]=3
        source.tilemap=bytes(cells)
        before=copy.deepcopy(rows)
        documents=gameplay.routes(source,.1,adapted=rows)
        # The aligned attempt still rejects all its staged guide edits.
        with self.assertRaises(launch._NotPlanned):
            launch._repair_chains(source,rows,.1,gameplay._RouteClearance(source,.1),
                                 documents,profile)
        unchanged,partial=launch.repair_chains(source,rows,.1,
            gameplay._RouteClearance(source,.1),documents,profile)
        self.assertIs(unchanged,rows)
        self.assertTrue(any(y==63 for x,y in partial))
        self.assertTrue(any(y==79 for x,y in partial))
        self.assertFalse(any(y==95 for x,y in partial))
        pristine,_,_=chain_fixture()
        _,baseline=launch._repair_chains(pristine,rows,.1,
            gameplay._RouteClearance(pristine,.1),documents,profile,directed=True)
        self.assertEqual(partial,{cell:value for cell,value in baseline.items() if cell[1]!=95})
        self.assertEqual(rows,before)
        malformed=dict(profile,handling_turn_rate_q16=0)
        with self.assertRaisesRegex(launch.LaunchError,'handling_turn_rate'):
            launch.repair_chains(source,rows,.1,gameplay._RouteClearance(source,.1),
                gameplay.routes(source,.1,adapted=rows),malformed)
        too_many=[rows[0]*600]
        with self.assertRaisesRegex(launch.LaunchError,'route node budget'):
            launch.repair_chains(source,too_many,.1,gameplay._RouteClearance(source,.1),
                [gameplay.routes(source,.1,adapted=rows)[0]],profile)

    def test_invalid_route_speeds_and_properties_do_not_decline_silently(self):
        source,rows,profile=chain_fixture()
        original=gameplay.routes(source,.1,adapted=rows)
        for key,value in (('mksc:cpu-lookahead',float('nan')),
                          ('mksc:cpu-corner-speed',float('inf')),
                          ('mksc:cpu-lookahead',0),('mksc:cpu-corner-speed',True)):
            documents=copy.deepcopy(original)
            documents[0]['properties']=[dict(key=key,value=value)]
            with self.subTest(key=key,value=value):
                with self.assertRaisesRegex(launch.LaunchError,'course=70067.*invalid corner speed or lookahead'):
                    launch.repair_chains(source,rows,.1,gameplay._RouteClearance(source,.1),documents,profile)
        for value in (float('nan'),float('inf'),-1,True):
            documents=copy.deepcopy(original)
            documents[0]['nodes'][0]['speed']=value
            with self.subTest(speed=value):
                with self.assertRaisesRegex(launch.LaunchError,'course=70067.*invalid node speed'):
                    launch.repair_chains(source,rows,.1,gameplay._RouteClearance(source,.1),documents,profile)

    def test_connected_strip_deterministic_and_discrete_phase_clearance(self):
        source,rows=fixture()
        saved=copy.deepcopy((source,rows))
        result=launch.plan(source,rows,.1,maximum_speed=36)
        self.assertEqual(set(result),{(32,8),(33,8)})
        distance=result[32,8]
        self.assertEqual(distance,result[33,8])
        self.assertEqual(distance*65536,round(distance*65536))
        self.assertGreater(distance,21)
        self.assertLess(distance,23)
        self.assertEqual(result,launch.plan(source,rows,.1,maximum_speed=36))
        self.assertEqual((source,rows),saved)
        # Independent integer semi-implicit flight oracle. Test every 1m/s
        # speed and nine launch phases; wall collision uses its expanded
        # body interval, while descending ground may snap only after the
        # last expanded obstacle. No tolerance/continuous parabola blessing.
        gravity=round(9.8*65536)
        dt=4389/262144
        for speed in range(16,37):
            for phase in range(9):
                v=min(9*65536,math.floor(9.8*distance/(2*speed)*65536))
                x=speed*dt*phase/8
                height=0
                previous_x=x
                landed=False
                for tick in range(200):
                    previous_h=height
                    previous_x=x
                    v-=qdt(gravity)
                    height+=qdt(v)
                    x+=speed*dt
                    for lo,hi in ((1.3,2.7),(17.3,18.7)):
                        if previous_x<=hi and x>=lo:
                            contacts=(max(previous_x,lo),min(x,hi))
                            samples=[previous_h+(height-previous_h)*(p-previous_x)/(x-previous_x)
                                     for p in contacts]
                            self.assertGreater(min(samples)/65536+.35,.4)
                    # No support on the actual wall top. Before/after a wall,
                    # a snap at .25 must not occur while its rear body overlaps.
                    wall=(1.6<=x<=2.4 or 17.6<=x<=18.4)
                    if v<=0 and height<=65536/4 and not wall:
                        self.assertGreater(x,18.7)
                        self.assertLess(x,24)
                        landed=True
                        break
                self.assertTrue(landed,(speed,phase))

    def test_unsafe_water_or_higher_wall_leaves_strip_unmodified(self):
        source,rows=fixture()
        # Recovery starts just after launch, before enough upward clearance;
        # it also occupies the would-be landing area.
        cells=bytearray(source.tilemap)
        for x in range(4,32):
            cells[8*64+x]=3
        source.tilemap=bytes(cells)
        self.assertEqual(launch.plan(source,rows,.1),{})
        source,rows=fixture()
        # Larger physical scale makes the same wall/gap impossible within
        # the finite launch impulse at the supported speed range.
        self.assertEqual(launch.plan(source,rows,1),{})

    def test_isolated_retained_ray_admits_shallow_lanes_without_changing_straight_cap(self):
        source,rows=fixture()
        curved=[(360,64),(180,64),(32,60),(32,112),(360,112)]
        saved=copy.deepcopy((source,rows,curved))
        straight=launch.plan(source,rows,.1)
        caps=launch.plan(source,rows+[curved],.1)
        self.assertTrue(caps)
        self.assertEqual(caps,straight)
        distance=caps[32,8]
        crossover=9.8*distance/18
        speeds=[16,24,36]+([crossover] if 16<crossover<36 else [])
        for speed in speeds:
            for phase in range(9):
                velocity=math.floor(min(9,9.8*distance/(2*speed))*65536)
                x=272*.1-speed*launch._DT*phase/8
                height=0
                for _ in range(200):
                    velocity-=qdt(round(9.8*65536))
                    height+=qdt(velocity)
                    x-=speed*launch._DT
                    # Both wall intervals use the same unmodified .4m source
                    # geometry. A real landing must clear the final wall and
                    # happen before the retained bend, not just its plane.
                    if velocity<=0 and height<=65536/4 and not (8.8<=x<=9.6):
                        self.assertLess(x,8.5)
                        self.assertGreater(x,3.2)
                        break
                else:
                    self.fail('retained-ray flight did not land before its bend')
        self.assertEqual((source,rows,curved),saved)
        self.assertEqual(caps,launch.plan(source,rows+[curved],.1))

    def test_isolation_guard_preserves_stronger_incoming_and_common_axis_chains(self):
        width,height=48,160
        for common_axis in (False,True):
            cells=bytearray(width*height)
            for x in range(8,33):
                cells[63*width+x]=1
                cells[95*width+x]=1 if common_axis else 2
                cells[97*width+x]=3
            source=NS(course_id=70044,map_width=width,map_height=height,
                materials=bytes((0x44,0x10,0x1e,0x80)),tilemap=bytes(cells))
            middle=[(116,632),(100,720)] if common_axis else []
            rows=[[(100,328),(100,416)]+middle+[(100,860),(104,968),
                   (140,1040),(300,1040),(300,328)]]
            saved=copy.deepcopy((source,rows))
            if not common_axis:
                # Incoming9m/s launch can reach25.6m; current3m/s launch's
                # own22m horizon cannot be used to exclude that ownership.
                self.assertGreater(25.6,2*3*36/9.8)
                self.assertLess(25.6,2*9*36/9.8)
            else:
                # A1.6m lateral bulge returns before takeoff: it escapes the
                # entry chord's .8m tube but fits a common axis at +.8m.
                self.assertEqual((116-100)*.1,1.6)
            self.assertEqual(launch.plan(source,rows,.1),{})
            self.assertEqual((source,rows),saved)
            # With the earlier launch removed, the exact same last approach
            # and collision geometry are genuinely isolated and provable.
            for x in range(8,33): cells[63*width+x]=0
            source.tilemap=bytes(cells)
            caps=launch.plan(source,rows,.1)
            self.assertTrue(caps)
            self.assertEqual({y for x,y in caps},{95})

    def test_unvisited_early_curved_and_no_overshoot_strips_unchanged(self):
        source,rows=fixture()
        self.assertEqual(launch.plan(source,[],.1),{})
        curved=[[(360,64),(268,64),(236,56),(32,56),(32,112),(360,112)]]
        self.assertEqual(launch.plan(source,curved,.1),{})
        # One unsafe alternate approach refuses the entire shared component.
        self.assertEqual(launch.plan(source,rows+curved,.1),{})
        #3m/s launch cannot overshoot a24m bend even at36m/s.
        source.materials=bytes((0x40,0x1e,0x84,0x22))
        self.assertEqual(launch.plan(source,rows,.1),{})

    def test_speed_envelope_and_malformed_bounds(self):
        source,rows=fixture()
        low=launch.plan(source,rows,.1,maximum_speed=30)
        high=launch.plan(source,rows,.1,maximum_speed=36)
        self.assertLess(low[32,8],high[32,8])
        for scale in (True,float('nan'),float('inf'),0,100):
            with self.subTest(scale=scale):
                with self.assertRaisesRegex(launch.LaunchError,'course=70043'):
                    launch.plan(source,rows,scale)
        for speed in (False,0,15,float('inf'),300):
            with self.assertRaises(launch.LaunchError):
                launch.plan(source,rows,.1,maximum_speed=speed)
        with self.assertRaises(launch.LaunchError):
            launch.plan(source,[[(0,0),(1,float('nan')),(2,2)]],.1)




class ModernLaunchDefaultsTests(unittest.TestCase):
    def test_p3_chain_admits_encoded_default_and_preserves_source_anchors(self):
        source,rows,profile=chain_fixture()
        profile['ground_snap_q16']=655
        # Keep the P3 proof's original nominal speed envelope explicit.
        profile.update(acceleration_limit_q16=12*65536,
                       brake_deceleration_q16=24*65536,coast_deceleration_q16=3*65536)
        documents=gameplay.routes(source,.1,adapted=rows)
        saved=copy.deepcopy((source,rows,profile,documents))
        repaired,caps=launch.repair_chains(source,rows,.1,
            gameplay._RouteClearance(source,.1),documents,profile)
        self.assertTrue(caps)
        for original,planned in zip(rows,repaired):
            cursor=iter(planned)
            self.assertTrue(all(any(point==candidate for candidate in cursor) for point in original))
        self.assertEqual((source,rows,profile,documents),saved)
        with self.assertRaisesRegex(launch.LaunchError,'ballistic.*assumptions'):
            launch.repair_chains(source,rows,.1,gameplay._RouteClearance(source,.1),
                documents,dict(profile,ground_snap_q16=16384))

    def test_p4_wider_nominal_envelope_plans_chain_transactionally(self):
        # MK8 gravity's higher converted apex admits the wider envelope too.
        source,rows,profile=chain_fixture()
        profile['ground_snap_q16']=655
        documents=gameplay.routes(source,.1,adapted=rows)
        saved=copy.deepcopy((source,rows,profile,documents))
        repaired,caps=launch.repair_chains(source,rows,.1,
            gameplay._RouteClearance(source,.1),documents,profile)
        self.assertTrue(caps)
        for original,planned in zip(rows,repaired):
            cursor=iter(planned)
            self.assertTrue(all(any(point==candidate for candidate in cursor) for point in original))
        self.assertEqual((source,rows,profile,documents),saved)

    def test_new_default_cap_has_discrete_phase_safe_flight(self):
        source,rows=fixture()
        cells=bytearray(source.tilemap)
        # Remove the distant barrier: it is beyond the new low-speed range.
        # The near barrier remains a real body-clearance constraint.
        for y in (7,8,9): cells[y*64+11]=0
        source.tilemap=bytes(cells)
        saved=copy.deepcopy((source,rows))
        caps=launch.plan(source,rows,.1)
        self.assertEqual(set(caps),{(32,8),(33,8)})
        self.assertEqual((source,rows),saved)
        distance=caps[32,8]
        self.assertEqual(launch._G,gameplay.course_3d_format.DEFAULT_GRAVITY)
        self.assertEqual(launch._SNAP,655/65536)
        for speed in range(16,37):
            for phase in range(9):
                velocity=math.floor(min(launch.source_launch(0),launch._G*distance/(2*speed))*65536)
                x=speed*launch._DT*phase/8
                height=0
                landed=False
                for tick in range(150):
                    old_x,old_h=x,height
                    velocity-=qdt(round(launch._G*65536))
                    height+=qdt(velocity)
                    x+=speed*launch._DT
                    if old_x<=2.7 and x>=1.3:
                        for along in (max(old_x,1.3),min(x,2.7)):
                            h=old_h+(height-old_h)*(along-old_x)/(x-old_x)
                            self.assertGreater(h/65536+.35,.4)
                    if velocity<=0 and height<=655 and not 1.6<=x<=2.4:
                        self.assertGreater(x,2.7)
                        self.assertLess(x,24)
                        landed=True
                        break
                self.assertTrue(landed,(speed,phase))


class ModernGravityLaunchTests(unittest.TestCase):
    # Shipped default gravity and height-preserving source impulses.
    def test_crossed_boost_panel_raises_slowest_approach_and_caps_overshoot(self):
        # A panel crossed before the strip guarantees its entry impulse, so
        # the fast flight gets a ceiling below the second wall.
        source,rows=fixture()
        source.materials+=bytes((0x16,))
        cells=bytearray(source.tilemap)
        for x in (38,39):
            cells[8*64+x]=4
        source.tilemap=bytes(cells)
        caps=launch.plan(source,rows,.1)
        self.assertEqual(set(caps),{(32,8),(33,8)})
        self.assertTrue(all(0<value<(272-32)*.1-.5 for value in caps.values()),caps)
        clearance=gameplay._RouteClearance(source,.1)
        self.assertFalse(clearance.jump_connector((360,64),(32,64)))
        self.assertTrue(clearance.jump_connector((360,64),(32,64),panel_credit=True))


if __name__=='__main__':
    unittest.main()
