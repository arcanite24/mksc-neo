"""ROM-free exact texel and geometry oracles for the compact terrain encoder."""
import copy
import hashlib
import math
import struct
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import course_3d_terrain as terrain


def patterned(seed):
    return tuple(0 if (x + y + seed) % 11 == 0 else
                 0xff000000 | ((x * 29 + seed) % 256) << 16 |
                 ((y * 31 + seed * 3) % 256) << 8 | seed
                 for y in range(8) for x in range(8))


def weights(point, a, b, c):
    x, y = point
    ax, ay = a[0], a[2]
    bx, by = b[0], b[2]
    cx, cy = c[0], c[2]
    denominator = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
    if not denominator:
        return None
    wa = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / denominator
    wb = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / denominator
    result = wa, wb, 1 - wa - wb
    return result if min(result) >= -1e-7 else None


def sample(result, pixel_x, pixel_y, scale, *, indices=False):
    """Independent nearest-texture evaluation of an emitted horizontal face."""
    point = (pixel_x * scale, -pixel_y * scale)
    for a, b, c, material in result.triangles:
        vertices = [result.vertices[i] for i in (a, b, c)]
        if vertices[0][4] <= 0:
            continue
        barycentric = weights(point, *vertices)
        if barycentric is None:
            continue
        paint = result.materials[material]
        if paint.texture is None:
            return paint.argb
        texture = result.textures[paint.texture]
        uv = [sum(w * v[axis] for w, v in zip(barycentric, vertices)) for axis in (6, 7)]
        if paint.repeat:
            uv = [v - math.floor(v) for v in uv]
        xy = [max(0, min(size - 1, math.floor(value * size)))
              for value, size in zip(uv, (texture.width, texture.height))]
        plane=texture.indices if indices else texture.pixels
        return plane[xy[1] * texture.width + xy[0]]
    raise AssertionError(f'no top geometry at source pixel {pixel_x},{pixel_y}')


class TerrainTests(unittest.TestCase):
    def exact_pixels(self, width, height, cells, tiles, codes, **options):
        result = terrain.encode(width, height, cells, tiles, codes, **options)
        scale = options.get('units_per_pixel', .1)
        for y in range(height * 8):
            for x in range(width * 8):
                expected = tiles[cells[(y // 8) * width + x // 8]][(y % 8) * 8 + x % 8]
                self.assertEqual(sample(result, x + .5, y + .5, scale), expected, (x, y))
        return result

    def test_elevation_crest_subdivides_merged_quad_and_matches_collision(self):
        args = 2,2,bytes(4),[[0xff123456]*64],[0x40]
        flat=terrain.encode(*args)
        grid=[[0,0,0],[0,.75,0],[0,0,0]]
        saved=copy.deepcopy(grid)
        raised=terrain.encode(*args,elevation_grid=[v for row in grid for v in row])
        self.assertEqual(len(flat.triangles),2)
        self.assertEqual(len(raised.triangles),8)
        self.assertIn((terrain._q16(.8)/65536,.75,-terrain._q16(.8)/65536),
                      raised.collision_vertices)
        render_faces=[tuple(raised.vertices[i][:3] for i in t[:3]) for t in raised.triangles]
        collision_faces=[tuple(raised.collision_vertices[i] for i in t[:3])
                         for t in raised.collision_triangles]
        self.assertEqual(render_faces,collision_faces)
        self.assertEqual(grid,saved)
        for triangle in raised.triangles:
            a,b,c=[raised.vertices[i] for i in triangle[:3]]
            ab=[b[i]-a[i] for i in range(3)];ac=[c[i]-a[i] for i in range(3)]
            cross=(ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0])
            length=math.sqrt(sum(v*v for v in cross))
            for vertex in (a,b,c):
                for n,expected in zip(vertex[3:6],cross):
                    self.assertAlmostEqual(n,expected/length,delta=2/1073741824)
        self.assertGreater(len({v[3:6] for v in raised.vertices}),1)
        # A genuinely nonplanar single cell also has two independent normals.
        folded=terrain.encode(1,1,bytes(1),args[3],args[4],elevation_grid=[0,0,0,1])
        self.assertEqual(len({v[3:6] for v in folded.vertices}),2)

    def test_local_crest_keeps_large_flat_remainder_compact(self):
        width=height=64
        grid=[0.0]*((width+1)*(height+1))
        grid[32*(width+1)+32]=.75
        args=width,height,bytes(width*height),[[0xff123456]*64],[0x40]
        raised=terrain.encode(*args,elevation_grid=grid)
        self.assertLess(len(raised.collision_triangles),300)
        self.assertLess(len(raised.triangles),300)
        self.assertEqual(len(raised.collision_triangles),len(raised.triangles))
        self.assertEqual(max(v[1] for v in raised.collision_vertices),.75)
        self.assertEqual(raised,terrain.encode(*args,elevation_grid=tuple(grid)))
        # Every sloped triangle remains one original tile cell; compact flat
        # rectangles cannot span the raised sample and erase its crest.
        for a,b,c,_ in raised.collision_triangles:
            vertices=[raised.collision_vertices[i] for i in (a,b,c)]
            if len({v[1] for v in vertices})>1:
                self.assertLessEqual(max(v[0] for v in vertices)-min(v[0] for v in vertices),.80002)
                self.assertLessEqual(max(v[2] for v in vertices)-min(v[2] for v in vertices),.80002)
        self.assertEqual([tuple(raised.vertices[i][:3] for i in t[:3]) for t in raised.triangles],
                         [tuple(raised.collision_vertices[i] for i in t[:3])
                          for t in raised.collision_triangles])

    def test_elevation_preserves_texels_indices_and_material_identity(self):
        planes=[bytes(255 if (x+y+i)%7==0 else 1+(x+2*y+i)%4
                      for y in range(8) for x in range(8)) for i in range(4)]
        tiles=[tuple(0 if v==255 else 0xff234567 for v in plane) for plane in planes]
        cases=[(4,4,bytes((0,1,0,1,2,3,2,3)*2),dict(native_limit=0,motif_limit=1)),
               (3,3,bytes((0,1,2,3,2,1,0,3,1)),dict(native_limit=0,motif_limit=0)),
               (2,2,bytes(4),dict(native_limit=1,motif_limit=0))]
        for w,h,cells,options in cases:
            with self.subTest(size=(w,h),options=options):
                grid=[[0]*(w+1) for _ in range(h+1)];grid[1][1]=.123456
                flat=terrain.encode(w,h,cells,tiles,[0x40]*4,indexed_tiles=planes,**options)
                raised=terrain.encode(w,h,cells,tiles,[0x40]*4,indexed_tiles=planes,
                                      elevation_grid=[v for row in grid for v in row],**options)
                self.assertEqual((flat.textures,flat.materials),(raised.textures,raised.materials))
                for y in range(h*8):
                    for x in range(w*8):
                        self.assertEqual(sample(flat,x+.5,y+.5,.1,indices=True),
                                         sample(raised,x+.5,y+.5,.1,indices=True))
                self.assertIn(terrain._q16(.123456)/65536,{v[1] for v in raised.vertices})

    def test_elevation_wall_tops_and_exposed_sides_follow_ground(self):
        args=2,1,bytes(2),[[0xff123456]*64],[0x80]
        grid=[[0,.5,0],[0,.5,0]]
        result=terrain.encode(*args,elevation_grid=[v for row in grid for v in row],wall_height=1.5)
        self.assertEqual(len(result.triangles),16) # two tops, six exposed spans
        self.assertEqual(len(result.collision_triangles),16)
        self.assertEqual({t[3] for t in result.collision_triangles},{0x80})
        # Ridge exists in both ground and top along the two long boundaries.
        for z in (0,-terrain._q16(.8)/65536):
            at_ridge={v[1] for v in result.collision_vertices
                      if v[0]==terrain._q16(.8)/65536 and v[2]==z}
            self.assertEqual(at_ridge,{.5,2})
        rf=[tuple(result.vertices[i][:3] for i in t[:3]) for t in result.triangles]
        cf=[tuple(result.collision_vertices[i] for i in t[:3]) for t in result.collision_triangles]
        self.assertEqual(rf,cf)

    def test_elevation_none_and_encoded_zero_are_exact_and_invalid_is_named(self):
        args=3,2,bytes((0,1,0,1,0,1)),[patterned(1),patterned(2)],[0x40,0x80]
        original=terrain.encode(*args)
        self.assertEqual(original,terrain.encode(*args,elevation_grid=None))
        for zero in (0,1e-8,-1e-8):
            self.assertEqual(original,terrain.encode(*args,elevation_grid=[zero]*12))
        for bad in ([],[0]*8,[0]*9,[True]*12,
                    [float('nan')]*12,[float('inf')]*12,[4097]*12,[[0]*4]*3):
            with self.subTest(bad=str(bad)),self.assertRaisesRegex(terrain.TerrainError,'elevation_grid'):
                terrain.encode(*args,elevation_grid=bad)
        with self.assertRaisesRegex(terrain.TerrainError,'4096'):
            terrain.encode(1,1,bytes(1),[[0xff123456]*64],[0x80],
                           elevation_grid=[4096]*4)

    def test_elevation_faceted_excess_fails_existing_geometry_cap(self):
        grid=[[((x+y)%2)*.1 for x in range(257)] for y in range(257)]
        saved=tuple(tuple(row) for row in grid)
        with self.assertRaisesRegex(terrain.TerrainError,'render vertices.*131072'):
            terrain.encode(256,256,bytes(256*256),[[0xff123456]*64],[0x40],elevation_grid=[v for row in grid for v in row])
        self.assertEqual(tuple(tuple(row) for row in grid),saved)

    def test_indexed_solids_keep_distinct_identity_and_white_material(self):
        color=0xff34789a
        tiles=[[color]*64,[color]*64,[0]*64]
        planes=[bytearray([1]*64),bytearray([2]*64),bytearray([255]*64)]
        cells=bytearray((0,1,2));saved=copy.deepcopy((tiles,planes,cells))
        result=terrain.encode(3,1,cells,tiles,[0x40]*3,indexed_tiles=planes)
        self.assertEqual(len(result.textures),3)
        self.assertEqual([(t.width,t.height,t.indices) for t in result.textures],
                         [(1,1,b'\1'),(1,1,b'\2'),(1,1,b'\xff')])
        self.assertTrue(all(m.argb==0xffffffff and m.texture is not None for m in result.materials))
        self.assertEqual([sample(result,x*8+.5,.5,.1,indices=True) for x in range(3)],[1,2,255])
        self.assertEqual([sample(result,x*8+.5,.5,.1) for x in range(3)],[color,color,0])
        self.assertNotEqual(result.textures[0].digest,result.textures[1].digest)
        self.assertEqual((tiles,planes,cells),saved)
        planes[0][0]=3;tiles[0][0]=0;cells[0]=2
        self.assertEqual(sample(result,.5,.5,.1,indices=True),1)
        self.assertEqual(sample(result,.5,.5,.1),color)

    def test_uniform_seed_rgb_cannot_collapse_varying_palette_indices(self):
        tiles=[(0xff223344,)*64]*2
        planes=[bytes(1 if x%2==0 else other for y in range(8) for x in range(8))
                for other in (2,3)]
        result=terrain.encode(2,1,b'\0\1',tiles,[0x40]*2,indexed_tiles=planes,
                              native_limit=0,motif_limit=0)
        self.assertEqual(len(result.textures),1)
        self.assertEqual((result.textures[0].width,result.textures[0].height),(128,8))
        self.assertEqual(sample(result,1.5,.5,.1,indices=True),2)
        self.assertEqual(sample(result,9.5,.5,.1,indices=True),3)
        self.assertEqual(result.textures[0].indices[:16],planes[0][:8]+planes[1][:8])
        self.assertEqual(result.textures[0].indices[16:128],bytes([255])*112)
        self.assertEqual(result.textures[0].pixels[16:128],(0,)*112)
        # The opt-out path still deduplicates those identical RGBA tiles.
        plain=terrain.encode(2,1,b'\0\1',tiles,[0x40]*2)
        self.assertEqual(len(plain.triangles),2)
        self.assertFalse(plain.textures)

    def test_index_planes_follow_native_motif_and_atlas_uvs_exactly(self):
        color=0xff34789a
        planes=[bytes(255 if (x+y+i)%7==0 else 1+(x+2*y+i)%4
                      for y in range(8) for x in range(8)) for i in range(4)]
        tiles=[tuple(0 if v==255 else color for v in plane) for plane in planes]
        for width,height,cells,options,shape in [
                (2,2,bytes((0,)*4),dict(native_limit=1,motif_limit=0),(8,8)),
                (4,4,bytes((0,1,0,1,2,3,2,3)*2),dict(native_limit=0,motif_limit=1),(16,16)),
                (3,3,bytes((0,1,2,3,2,1,0,3,1)),dict(native_limit=0,motif_limit=0),(128,8))]:
            with self.subTest(shape=shape):
                original=copy.deepcopy((tiles,planes,cells))
                result=terrain.encode(width,height,cells,tiles,[0x40]*4,indexed_tiles=planes,**options)
                self.assertEqual((result.textures[0].width,result.textures[0].height),shape)
                self.assertTrue(all(t.indices is not None for t in result.textures))
                for y in range(height*8):
                    for x in range(width*8):
                        tile=cells[(y//8)*width+x//8];pixel=(y%8)*8+x%8
                        self.assertEqual(sample(result,x+.5,y+.5,.1,indices=True),planes[tile][pixel])
                        self.assertEqual(sample(result,x+.5,y+.5,.1),tiles[tile][pixel])
                self.assertEqual((tiles,planes,cells),original)
                # A palette with distinct seed RGBs must choose identical index-
                # aware geometry: equal RGB must never enable extra welding.
                distinct=[tuple(0 if v==255 else 0xff000000|v for v in plane) for plane in planes]
                reference=terrain.encode(width,height,cells,distinct,[0x40]*4,indexed_tiles=planes,**options)
                self.assertEqual(result.vertices,reference.vertices)
                self.assertEqual(result.triangles,reference.triangles)
                self.assertEqual(result.collision_triangles,reference.collision_triangles)

    def test_indexed_validation_and_default_texture_bytes_remain_stable(self):
        tiles=[patterned(1)];plane=bytes(255 if p>>24==0 else 1 for p in tiles[0])
        plain=terrain.encode(1,1,b'\0',tiles,[0x40],motif_limit=0)
        self.assertEqual(plain,terrain.encode(1,1,b'\0',tiles,[0x40],motif_limit=0,indexed_tiles=None))
        for texture in plain.textures:
            self.assertIsNone(texture.indices)
            expected=struct.pack('<II',texture.width,texture.height)+struct.pack(f'<{len(texture.pixels)}I',*texture.pixels)
            self.assertEqual(texture.digest,hashlib.sha256(expected).hexdigest())
        for bad in ([],[plane[:-1]],[bytes([64])*64],[bytes([0])*64],[[True]*64]):
            with self.subTest(bad=str(bad)[:20]),self.assertRaises(terrain.TerrainError):
                terrain.encode(1,1,b'\0',tiles,[0x40],indexed_tiles=bad)

    def test_repeating_tile_and_shared_material_seams(self):
        tiles = [patterned(1), patterned(2)]
        repeated = self.exact_pixels(4, 2, bytes([0] * 8), tiles, [0x40, 0x5a], motif_limit=0)
        self.assertEqual((len(repeated.vertices), len(repeated.triangles), len(repeated.textures)), (4, 2, 1))
        adjacent = self.exact_pixels(2, 1, b'\0\1', tiles, [0x40, 0x5a], motif_limit=0)
        self.assertEqual(len(adjacent.vertices), 6)  # Global UVs weld across different materials.
        self.assertEqual({t[3] for t in adjacent.collision_triangles}, {0x40, 0x5a})

    def test_exact_two_by_two_motif_and_odd_atlas_fallback(self):
        tiles = [patterned(i) for i in range(1, 5)]
        cells = bytes([0, 1, 0, 1, 2, 3, 2, 3] * 2)
        motif = self.exact_pixels(4, 4, cells, tiles, [0x40] * 4, native_limit=0, motif_limit=1)
        self.assertEqual((len(motif.vertices), len(motif.triangles)), (4, 2))
        self.assertEqual((motif.textures[0].width, motif.textures[0].height), (16, 16))
        fallback = self.exact_pixels(3, 3, bytes([0, 1, 2, 3, 2, 1, 0, 3, 1]), tiles,
                                     [0x40] * 4, native_limit=0, motif_limit=0)
        self.assertEqual(len(fallback.textures), 1)
        self.assertFalse(fallback.materials[0].repeat)
        self.assertTrue(fallback.materials[0].cutout)

    def test_solids_opaque_black_and_source_ownership(self):
        tiles = [[color] * 64 for color in (0xff000000, 0xffffffff, 0)]
        cells = bytearray([0, 1, 2])
        result = self.exact_pixels(3, 1, cells, tiles, [0x40] * 3)
        self.assertFalse(result.textures)
        self.assertEqual(len(result.vertices), 8)  # Constant UVs share all horizontal corners.
        self.assertEqual({m.argb for m in result.materials}, {0xff000000, 0xffffffff, 0})
        saved = result
        tiles[0][0] = 0xffff0000
        cells[0] = 2
        self.assertEqual(sample(saved, .5, .5, .1), 0xff000000)
        pattern = [list(patterned(3))]
        owned = terrain.encode(1, 1, b'\0', pattern, [0x40])
        old = owned.textures[0].pixels
        pattern[0][:] = [0] * 64
        self.assertEqual(owned.textures[0].pixels, old)

    def test_raised_wall_winding_collision_and_no_internal_faces(self):
        result = self.exact_pixels(2, 1, b'\0\0', [[0xff102030] * 64], [0x80], wall_height=1.5)
        # One top plus four perimeter sides, not a side between adjacent walls.
        self.assertEqual((len(result.triangles), len(result.collision_triangles)), (10, 10))
        self.assertEqual({v[1] for v in result.vertices}, {0, 1.5})
        self.assertEqual({t[3] for t in result.collision_triangles}, {0x80})
        self.assertEqual({v[3:6] for v in result.vertices},
                         {(0, 1, 0), (1, 0, 0), (-1, 0, 0), (0, 0, 1), (0, 0, -1)})
        for a, b, c, _ in result.triangles:
            p, q, r = [result.vertices[i] for i in (a, b, c)]
            ab, ac = [q[i] - p[i] for i in range(3)], [r[i] - p[i] for i in range(3)]
            cross = [ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2],
                     ab[0] * ac[1] - ab[1] * ac[0]]
            self.assertGreater(sum(cross[i] * p[i + 3] for i in range(3)), 0)
        flat = terrain.encode(2, 1, b'\0\0', [[0xff102030] * 64], [0x80], wall_height=0)
        self.assertEqual(len(flat.triangles), 2)

    def test_deterministic_dedup_and_named_invalid_inputs(self):
        tile = patterned(7)
        first = terrain.encode(2, 1, b'\0\1', [tile, tile], [0x40, 0x5a])
        self.assertEqual(first, terrain.encode(2, 1, b'\0\1', [tile, tile], [0x40, 0x5a]))
        self.assertEqual((len(first.textures), len(first.vertices)), (1, 4))
        self.assertEqual(len(first.textures[0].digest), 64)
        for args, kwargs, message in (
                ((1, 1, b'\1', [tile], [0x40]), {}, 'tilemap index'),
                ((1, 1, b'\0', [tile[:-1]], [0x40]), {}, '64 ARGB'),
                ((1, 1, b'\0', [[0x7fffffff] * 64], [0x40]), {}, 'partial alpha'),
                ((1, 1, b'\0', [tile], [0x40]), {'units_per_pixel': float('nan')}, 'finite'),
                ((1, 1, b'\0', [tile], [0x40]), {'motif_limit': 127, 'native_limit': 1}, 'dictionary'),
                ((1, 1, b'\0', [tile], [0x40]), {'wall_height': 1e-10}, 'Q16'),
                ((1, 1, None, [tile], [0x40]), {}, 'sequences')):
            with self.subTest(message=message), self.assertRaisesRegex(terrain.TerrainError, message):
                terrain.encode(*args, **kwargs)

    def test_unmergeable_atlas_excess_fails_named_cap(self):
        cells = bytes((x + y) % 2 for y in range(256) for x in range(256))
        tiles = [patterned(1), patterned(2)]
        before = tuple(tuple(tile) for tile in tiles)
        with self.assertRaisesRegex(terrain.TerrainError, 'render vertices.*131072'):
            terrain.encode(256, 256, cells, tiles, [0x40, 0x40],
                           motif_limit=0, native_limit=0)
        self.assertEqual(tuple(tuple(tile) for tile in tiles), before)

    def test_collision_regions_split_only_floor_faces(self):
        args = 2, 1, b'\0\0', [[0xff102030] * 64], [0x40]
        original = terrain.encode(*args)
        split = terrain.encode(*args, collision_regions=[7, 1 << 80])
        self.assertEqual((original.vertices, original.triangles, original.materials, original.textures),
                         (split.vertices, split.triangles, split.materials, split.textures))
        self.assertEqual((len(original.collision_vertices), len(original.collision_triangles)), (4, 2))
        self.assertEqual((len(split.collision_vertices), len(split.collision_triangles)), (6, 4))
        boundary = min(v[0] for v in split.collision_vertices if v[0] > 0)
        sides = set()
        for a, b, c, code in split.collision_triangles:
            xs = [split.collision_vertices[i][0] for i in (a, b, c)]
            self.assertTrue(max(xs) <= boundary or min(xs) >= boundary)
            sides.add(sum(xs) / 3 < boundary)
            self.assertEqual(code, 0x40)
        self.assertEqual(sides, {False, True})
        self.assertEqual(original, terrain.encode(*args, collision_regions=[None, None]))
        wall_args = 2, 1, b'\0\0', [[0xff102030] * 64], [0x80]
        wall = terrain.encode(*wall_args)
        wall_split = terrain.encode(*wall_args, collision_regions=[0, 1])
        self.assertEqual((wall.vertices, wall.triangles), (wall_split.vertices, wall_split.triangles))
        # Only the top splits; the four exposed wall sides retain their ownership.
        def faces(result, start):
            return [(tuple(result.collision_vertices[i] for i in triangle[:3]), triangle[3])
                    for triangle in result.collision_triangles[start:]]
        self.assertEqual(faces(wall, 2), faces(wall_split, 4))
        for invalid in ([0], [-1, 0], [True, 0], ['x', None]):
            with self.subTest(regions=invalid), self.assertRaisesRegex(terrain.TerrainError, 'collision_regions'):
                terrain.encode(*args, collision_regions=invalid)


if __name__ == '__main__':
    unittest.main()
