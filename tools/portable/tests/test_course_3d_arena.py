"""ROM-free arena patrol topology and full-body clearance regressions."""
import copy
from pathlib import Path
import sys
from types import SimpleNamespace as NS
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import course_3d_arena as arena
import course_3d_gameplay as gameplay


def fixture():
    cells=bytearray([1]*16*16)
    for y in range(1,15):
        for x in range(1,15):
            cells[y*16+x]=0
    grid=tuple(NS(x_q16=x*65536,y_q16=y*65536) for x,y in
               ((20,20),(108,20),(20,108),(108,108)))
    return NS(course_id=90001,map_width=16,map_height=16,tilemap=bytes(cells),
              materials=bytes((0x40,0x26,0x84)),grid=grid,item_boxes=(),
              route_rows=((NS(x=-999,y=-999),),))


class ArenaPatrolTests(unittest.TestCase):
    def run_patrol(self,source):
        clearance=gameplay._RouteClearance(source,.1)
        plan=arena.patrol(source,clearance)
        result=plan.points
        self.assertGreaterEqual(len(result),2)
        self.assertLessEqual(len(result),4096)
        for a,b in zip(result,result[1:]+result[:1]):
            self.assertTrue(clearance.clear(a,b),(a,b))
        return result

    def test_starts_items_deterministic_and_unsafe_rut_ignored(self):
        source=fixture()
        source.item_boxes=(NS(x_q16=68*65536,y_q16=68*65536,half_extent=4),)
        saved=copy.deepcopy(source)
        result=self.run_patrol(source)
        for row in source.grid+source.item_boxes:
            self.assertIn((row.x_q16/65536,row.y_q16/65536),result)
        self.assertEqual(result,self.run_patrol(source))
        self.assertEqual(source,saved)
        source.route_rows=()
        self.assertEqual(result,self.run_patrol(source))

    def test_narrow_wall_requires_actual_turn_and_swept_corner_clearance(self):
        source=fixture()
        cells=bytearray(source.tilemap)
        # Almost complete vertical divider; paths must turn through the bottom
        # opening. A diagonal cannot clip either end of the wall.
        for y in range(1,12):
            cells[y*16+7]=2
        source.tilemap=bytes(cells)
        result=self.run_patrol(source)
        self.assertTrue(any(y>=100 for x,y in result if 48<=x<=80))
        self.assertGreater(len(result),4)

    def test_required_item_near_void_uses_pickup_fringe(self):
        source=fixture()
        source.item_boxes=(NS(x_q16=2*65536,y_q16=68*65536,half_extent=16),)
        result=self.run_patrol(source)
        self.assertNotIn((2,68),result)
        self.assertTrue(any((x-2)**2+(y-68)**2<=16**2 for x,y in result))

    def test_disconnected_grid_rejects_but_unreachable_items_remain_explicit(self):
        source=fixture()
        cells=bytearray(source.tilemap)
        for y in range(16):
            cells[y*16+7]=2
        source.tilemap=bytes(cells)
        with self.assertRaisesRegex(arena.ArenaError,'course=90001.*disconnected GRID'):
            self.run_patrol(source)
        source=fixture()
        source.item_boxes=(NS(x_q16=2*65536,y_q16=2*65536,half_extent=1),)
        saved=copy.deepcopy(source)
        plan=arena.patrol(source,gameplay._RouteClearance(source,.1))
        self.assertEqual(plan.unreachable_items,(0,))
        self.assertIsInstance(plan.points,tuple)
        self.assertEqual(source,saved) # Actual ITEM placements are not removed.
        self.assertEqual(plan,arena.patrol(source,gameplay._RouteClearance(source,.1)))
        for row in source.grid:
            self.assertIn((row.x_q16/65536,row.y_q16/65536),plan.points)
        self.run_patrol(source)

    def test_bounded_invalid_input(self):
        source=fixture();source.map_width=257
        with self.assertRaisesRegex(arena.ArenaError,'bounded map'):
            self.run_patrol(source)
        source=fixture();source.grid=source.grid[:3]
        with self.assertRaisesRegex(arena.ArenaError,'four GRID'):
            self.run_patrol(source)
        source=fixture();source.grid[0].x_q16=-1
        with self.assertRaisesRegex(arena.ArenaError,'GRID row0 outside'):
            self.run_patrol(source)


if __name__=='__main__':
    unittest.main()
