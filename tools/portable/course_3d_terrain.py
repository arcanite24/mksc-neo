"""Exact bounded tiled terrain for editable glTF/C3D sources; no file I/O.

``encode(width, height, tilemap, tiles, materials)`` takes byte tile indices,
64 row-major ARGB32 pixels per 8x8 tile, and one source material byte per tile.
Only binary source alpha is supported. The immutable result uses glTF metres:
source pixel (x, y) becomes (x * units_per_pixel, elevation, -y * scale).
Texture indices are local; callers assign/deduplicate global IDs by digest and
map collision source material codes to authored C3D SURF coefficients.

Four common 2x2 motifs and fifteen beneficial individual tiles repeat using
global UVs. Uniform tiles need no texture; rare tiles use one compact atlas.
All vertices weld by the exact final Q16/Q30 values, including UV/normal seams.
Raised wall tops retain their source texels. Exposed wall sides use the explicit
wall_color; their height/color are converter presentation, not recovered art.
"""

from collections import Counter
from dataclasses import dataclass
import hashlib
import math
import struct


class TerrainError(ValueError):
    pass


@dataclass(frozen=True)
class Texture:
    width: int
    height: int
    pixels: tuple
    digest: str
    indices: bytes = None  # Optional palette-relative0..63;255 transparent.


@dataclass(frozen=True)
class Material:
    texture: object  # Local texture index, or None for a constant color.
    argb: int
    repeat: bool
    cutout: bool


@dataclass(frozen=True)
class Terrain:
    textures: tuple
    materials: tuple
    vertices: tuple  # (x,y,z,nx,ny,nz,u,v), glTF metres/normalized UVs.
    triangles: tuple  # (a,b,c,material).
    collision_vertices: tuple  # (x,y,z), glTF metres.
    collision_triangles: tuple  # (a,b,c,source material code).


def _integer(value, low, high, name):
    if type(value) is not int or not low <= value <= high:
        raise TerrainError(f'{name}: expected integer {low}..{high}')
    return value


def _number(value, low, high, name):
    if type(value) not in (int, float) or not math.isfinite(value) or not low <= value <= high:
        raise TerrainError(f'{name}: expected finite number {low}..{high}')
    return value


def _q16(value):
    return int(math.floor(abs(value) * 65536 + .5)) * (-1 if value < 0 else 1)


def _rectangles(values, width, height):
    """Stable row-first maximal rectangles; None marks cells owned elsewhere."""
    values = list(values)
    for y in range(height):
        for x in range(width):
            value = values[y * width + x]
            if value is None:
                continue
            w = 1
            while x + w < width and values[y * width + x + w] == value:
                w += 1
            h = 1
            while y + h < height and all(
                    values[(y + h) * width + x + i] == value for i in range(w)):
                h += 1
            for row in range(y, y + h):
                values[row * width + x:row * width + x + w] = [None] * w
            yield x, y, w, h, value


class _Builder:
    def __init__(self, scale, wall_height, elevation=None):
        self.elevation = elevation
        self.scale = scale
        self.wall_height = wall_height
        self.textures, self.materials, self.vertices, self.triangles = [], [], [], []
        self.collision_vertices, self.collision_triangles = [], []
        self.texture_lookup, self.material_lookup = {}, {}
        self.vertex_lookup, self.collision_lookup = {}, {}

    def texture(self, width, height, pixels, indices=None):
        pixels = tuple(pixels)
        indices = bytes(indices) if indices is not None else None
        key = (width,height,pixels) if indices is None else (width,height,pixels,indices)
        if key not in self.texture_lookup:
            if len(self.textures) == 128:
                raise TerrainError('textures: C3D limit 128 exceeded')
            data = struct.pack('<II', width, height) + struct.pack(f'<{len(pixels)}I', *pixels)
            if indices is not None:data+=b'IDX1'+indices
            self.texture_lookup[key] = len(self.textures)
            self.textures.append(Texture(width,height,pixels,hashlib.sha256(data).hexdigest(),indices))
        return self.texture_lookup[key]

    def material(self, texture=None, argb=0xffffffff, repeat=False):
        cutout = (argb >> 24) == 0 if texture is None else any(
            pixel >> 24 == 0 for pixel in self.textures[texture].pixels)
        value = Material(texture, argb, repeat, cutout)
        if value not in self.material_lookup:
            if len(self.materials) == 256:
                raise TerrainError('materials: C3D limit 256 exceeded')
            self.material_lookup[value] = len(self.materials)
            self.materials.append(value)
        return self.material_lookup[value]

    def point(self, x, y, height):
        result = (_q16(x * 8 * self.scale),
                  _q16(height) + (self.elevation[y][x] if self.elevation else 0),
                  _q16(-y * 8 * self.scale))
        if any(abs(value) > 4096 * 65536 for value in result):
            raise TerrainError('position: outside C3D +/-4096 metre bounds')
        return result

    def quad(self, points, normal, uvs, material, source_material=None, *, triangle_only=False):
        if normal is None:
            # Facets use the same encoded 0->2 diagonal as collision. Separate
            # normals preserve the crease of a nonplanar cell (no smoothing).
            for a, b, c in ((0, 1, 2), (0, 2, 3)):
                ab = [points[b][i] - points[a][i] for i in range(3)]
                ac = [points[c][i] - points[a][i] for i in range(3)]
                cross = (ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2],
                         ab[0]*ac[1]-ab[1]*ac[0])
                length = math.sqrt(sum(v*v for v in cross))
                if not length:
                    raise TerrainError('elevation_grid: degenerate encoded cell')
                face = tuple(v/length for v in cross)
                self.quad([points[i] for i in (a,b,c,c)], face,
                          [uvs[i] for i in (a,b,c,c)], material, triangle_only=True)
            return
        indices = []
        collision = source_material is not None
        lookup = self.collision_lookup if collision else self.vertex_lookup
        output = self.collision_vertices if collision else self.vertices
        for point, uv in zip(points, uvs):
            key = point if collision else point + tuple(int(n * 1073741824) for n in normal) + tuple(
                _q16(v) for v in uv)
            if key not in lookup:
                if len(output) == 131072:
                    name = 'collision vertices' if collision else 'render vertices'
                    raise TerrainError(f'{name}: C3D limit 131072 exceeded')
                lookup[key] = len(output)
                if collision:
                    output.append(tuple(v / 65536 for v in key))
                else:
                    output.append(tuple(v / 65536 for v in key[:3]) +
                                  tuple(v / 1073741824 for v in key[3:6]) +
                                  tuple(v / 65536 for v in key[6:]))
            indices.append(lookup[key])
        triangles = self.collision_triangles if collision else self.triangles
        tag = source_material if collision else material
        for a, b, c in (((0, 1, 2),) if triangle_only else ((0, 1, 2), (0, 2, 3))):
            if len(triangles) == 262144:
                raise TerrainError('triangles: C3D limit 262144 exceeded')
            triangles.append((indices[a], indices[b], indices[c], tag))


def _walls(builder, width, height, codes, color):
    wall = lambda code: code is not None and 0x80 <= code <= 0x8f
    material = None
    for horizontal in (True, False):
        for fixed in range((height if horizontal else width) + 1):
            length = width if horizontal else height
            previous, start = None, 0
            for step in range(length + 1):
                side = None
                if step < length:
                    x, y = (step, fixed) if horizontal else (fixed, step)
                    before = (codes[(y - 1) * width + x] if y else None) if horizontal else (
                        codes[y * width + x - 1] if x else None)
                    after = (codes[y * width + x] if y < height else None) if horizontal else (
                        codes[y * width + x] if x < width else None)
                    if wall(before) != wall(after):
                        side = (before if wall(before) else after, wall(before))
                        if builder.elevation:
                            end = (x+1,y) if horizontal else (x,y+1)
                            lo, hi = builder.elevation[y][x], builder.elevation[end[1]][end[0]]
                            side += ((lo, hi, step if lo != hi else None),)
                if side == previous:
                    continue
                if previous is not None:
                    if material is None:
                        material = builder.material(argb=color)
                    a, b = ((start, fixed), (step, fixed)) if horizontal else (
                        (fixed, start), (fixed, step))
                    points = [builder.point(*a, 0), builder.point(*b, 0),
                              builder.point(*b, builder.wall_height), builder.point(*a, builder.wall_height)]
                    outward = -1 if horizontal and previous[1] else 1
                    if not horizontal:
                        outward = 1 if previous[1] else -1
                    if outward < 0:
                        points.reverse()
                    normal = (0, 0, outward) if horizontal else (outward, 0, 0)
                    uv = ((0, 0),) * 4
                    builder.quad(points, normal, uv, material)
                    builder.quad(points, normal, uv, material, previous[0])
                previous, start = side, step


def encode(width, height, tilemap, tiles, materials, *, units_per_pixel=.1,
           wall_height=1.6, wall_color=0xff606060, motif_limit=4, native_limit=15,
           collision_regions=None, render_tilemap=None, indexed_tiles=None, elevation_grid=None):
    """Return owned immutable geometry; invalid/cap-exceeding inputs raise TerrainError.

    Up to 256x256 cells and 256 RGBA tiles. Dictionary limits are configurable,
    but their sum plus the fallback atlas must not exceed 128 textures. Output
    includes raised wall tops and exposed sides for source wall codes80..8f.
    Optional render_tilemap changes only top artwork; source material heights,
    collision and wall sides still use tilemap.
    Collision triangles retain source codes; this function invents no physics.
    Optional collision_regions has one nonnegative integer/None per map cell;
    unequal IDs prevent floor collision merging, without changing render data
    or wall sides. IDs are caller-owned grouping keys, not emitted metadata.
    Optional indexed_tiles supplies64 palette-relative indices0..63/255 per
    RGBA tile. Index255 must match transparent RGBA. Texture identity includes
    the complete index plane, even when seed colors are equal. Indexed solids
    use1x1 textures with white material tint; defaults retain constant colors.
    Optional elevation_grid contains (height+1)*(width+1) row-major additive
    metre values, rounded once to Q16. Variable-height merged rectangles subdivide
    into source tile cells; render/collision share their encoded corners and
    diagonal. None or an all-zero encoded field retains exact original output.
    """
    _integer(width, 1, 256, 'width')
    _integer(height, 1, 256, 'height')
    _number(units_per_pixel, 1 / 65536, 2, 'units_per_pixel')
    _number(wall_height, 0, 4096, 'wall_height')
    if wall_height and not _q16(wall_height):
        raise TerrainError('wall_height: rounds to zero in Q16')
    _integer(wall_color, 0, 0xffffffff, 'wall_color')
    if wall_color >> 24 not in (0, 255):
        raise TerrainError('wall_color: alpha must be zero or 255')
    _integer(motif_limit, 0, 127, 'motif_limit')
    _integer(native_limit, 0, 127, 'native_limit')
    if motif_limit + native_limit > 127:
        raise TerrainError('dictionary: reserve one of 128 textures for the fallback atlas')
    sequence = (tuple, list, bytes, bytearray, memoryview)
    if not all(isinstance(value, sequence) for value in (tilemap, tiles, materials)):
        raise TerrainError('terrain: expected tilemap/tiles/material sequences')
    if len(tilemap) != width * height or not 1 <= len(tiles) <= 256 or len(materials) != len(tiles):
        raise TerrainError('terrain: tilemap/tiles/material lengths disagree')
    regions = None
    if collision_regions is not None:
        if not isinstance(collision_regions, sequence) or len(collision_regions) != width * height:
            raise TerrainError('collision_regions: expected one ID per map cell')
        regions = tuple(collision_regions)
        if any(value is not None and (type(value) is not int or value < 0) for value in regions):
            raise TerrainError('collision_regions: expected nonnegative integer or None')
    if render_tilemap is not None:
        if not isinstance(render_tilemap, sequence) or len(render_tilemap) != width * height:
            raise TerrainError('render_tilemap: expected one tile index per map cell')
        render_tilemap = tuple(_integer(v, 0, len(tiles)-1, 'render tile index') for v in render_tilemap)
    if indexed_tiles is not None and (not isinstance(indexed_tiles,sequence) or len(indexed_tiles)!=len(tiles)):
        raise TerrainError('indexed_tiles: expected one64-index plane per tile')
    elevation = None
    if elevation_grid is not None:
        if not isinstance(elevation_grid, (tuple,list)) or len(elevation_grid) != (height+1)*(width+1):
            raise TerrainError('elevation_grid: expected (height+1)*(width+1) row-major metre values')
        encoded = tuple(_q16(_number(v,-4096,4096,'elevation_grid')) for v in elevation_grid)
        elevation = tuple(encoded[y*(width+1):(y+1)*(width+1)] for y in range(height+1))
        if not any(any(row) for row in elevation):
            elevation = None
    images, index_images, canonical, tile_ids, solids = [], [], {}, [], {}
    for index, tile in enumerate(tiles):
        if not isinstance(tile, sequence) or len(tile) != 64:
            raise TerrainError(f'tiles[{index}]: expected 64 ARGB pixels')
        image = tuple(_integer(p, 0, 0xffffffff, f'tiles[{index}].pixel') for p in tile)
        if any(p >> 24 not in (0, 255) for p in image):
            raise TerrainError(f'tiles[{index}]: partial alpha unsupported')
        plane=None
        if indexed_tiles is not None:
            values=indexed_tiles[index]
            if not isinstance(values,sequence) or len(values)!=64:
                raise TerrainError(f'indexed_tiles[{index}]: expected64 indices')
            if any(type(v) is not int or (not 0<=v<=63 and v!=255) for v in values):
                raise TerrainError(f'indexed_tiles[{index}]: expected palette index0..63 or255')
            plane=bytes(values)
            if any((v==255)!=(pixel>>24==0) for v,pixel in zip(plane,image)):
                raise TerrainError(f'indexed_tiles[{index}]: transparency disagrees with RGBA')
        key=image if plane is None else (image,plane)
        if key not in canonical:
            canonical[key] = len(images)
            images.append(image)
            index_images.append(plane)
        identity = canonical[key]
        tile_ids.append(identity)
        if len(set(image)) == 1 and (plane is None or len(set(plane))==1):
            solids[identity] = image[0] if plane is None else (image[0],plane[0])
    source_materials = tuple(_integer(v, 0, 255, 'material') for v in materials)
    source = tuple(_integer(v, 0, len(tiles) - 1, 'tilemap index') for v in tilemap)
    cells = tuple(tile_ids[v] for v in (source if render_tilemap is None else render_tilemap))
    codes = tuple(source_materials[v] for v in source)
    heights = tuple(wall_height if 0x80 <= c <= 0x8f else 0 for c in codes)
    frequency = Counter(cells)
    rectangles = Counter(v for *_, v in _rectangles(cells, width, height))
    native = set(sorted((v for v in frequency if v not in solids), key=lambda v: (
        -(frequency[v] * 64 + (frequency[v] - rectangles[v]) * 96 - 288), v))[:native_limit])
    patterns = []
    for y in range(0, height - 1, 2):
        for x in range(0, width - 1, 2):
            indices = (y * width + x, y * width + x + 1, (y + 1) * width + x, (y + 1) * width + x + 1)
            pattern = tuple(cells[i] for i in indices)
            patterns.append(pattern if len(set(pattern)) > 1 and len({heights[i] for i in indices}) == 1 else None)
    frequency = Counter(v for v in patterns if v is not None)
    rectangles = Counter(v for *_, v in _rectangles(patterns, width // 2, height // 2))
    selected = set(sorted(frequency, key=lambda v: (-(frequency[v] * 4 - rectangles[v]) * 160, v))[:motif_limit])
    builder = _Builder(units_per_pixel, wall_height, elevation)
    occupied = bytearray(width * height)

    paints = {}

    def paint(kind, key):
        identity = kind, 0 if kind == 'atlas' else key
        if identity in paints:
            return paints[identity]
        if kind == 'solid':
            material = (builder.material(argb=key) if indexed_tiles is None else
                        builder.material(builder.texture(1,1,(key[0],),(key[1],))))
        elif kind in ('tile', 'motif'):
            size = 8 if kind == 'tile' else 16
            pixels = images[key] if kind == 'tile' else tuple(
                images[key[(row // 8) * 2 + col // 8]][(row % 8) * 8 + col % 8]
                for row in range(16) for col in range(16))
            indices = None if indexed_tiles is None else (index_images[key] if kind=='tile' else bytes(
                index_images[key[(row//8)*2+col//8]][(row%8)*8+col%8]
                for row in range(16) for col in range(16)))
            material = builder.material(builder.texture(size,size,pixels,indices),repeat=True)
        else:
            atlas_height = ((len(images) + 15) // 16) * 8
            pixels = [0] * (128 * atlas_height)
            indices = bytearray([255])*len(pixels) if indexed_tiles is not None else None
            for tile, image in enumerate(images):
                for row in range(8):
                    start = ((tile // 16) * 8 + row) * 128 + (tile % 16) * 8
                    pixels[start:start + 8] = image[row * 8:row * 8 + 8]
                    if indices is not None:
                        indices[start:start+8]=index_images[tile][row*8:row*8+8]
            material = builder.material(builder.texture(128,atlas_height,pixels,indices))
        paints[identity] = material
        return material

    def pieces(x,y,w,h):
        if not elevation:
            yield x,y,w,h
            return
        flat, variable = [], []
        for j in range(y,y+h):
            for i in range(x,x+w):
                value = elevation[j][i]
                if (value == elevation[j][i+1] == elevation[j+1][i] == elevation[j+1][i+1]):
                    flat.append(value)
                else:
                    flat.append(None)
                    variable.append((i,j,1,1))
        # Retain compact unaffected plateaus; a local crest must not expand
        # the whole material rectangle into thousands of flat tile faces.
        for px,py,pw,ph,_ in _rectangles(flat,w,h):
            yield x+px,y+py,pw,ph
        yield from variable

    def top(x, y, w, h, kind, key):
        material = paint(kind, key)
        if kind == 'solid':
            uv = ((0, 0),) * 4
        elif kind in ('tile', 'motif'):
            divisor = 1 if kind == 'tile' else 2
            uv = tuple(((x + dx) / divisor, (y + dy) / divisor)
                       for dx, dy in ((0, 0), (w, 0), (w, h), (0, h)))
        else:
            atlas_height = ((len(images) + 15) // 16) * 8
            uv = tuple(((key % 16 + dx) / 16, (key // 16 + dy) * 8 / atlas_height)
                       for dx, dy in ((0, 0), (1, 0), (1, 1), (0, 1)))
        for px,py,pw,ph in pieces(x,y,w,h):
            points = [builder.point(px+dx,py+dy,heights[y*width+x])
                      for dx,dy in ((0,0),(pw,0),(pw,ph),(0,ph))]
            # Interpolate original UVs, preserving atlas and global-repeat
            # identities even when a formerly merged motif is subdivided.
            local_uv = tuple(tuple(uv[0][axis] + (px+dx-x)/w*(uv[1][axis]-uv[0][axis]) +
                                   (py+dy-y)/h*(uv[3][axis]-uv[0][axis]) for axis in (0,1))
                             for dx,dy in ((0,0),(pw,0),(pw,ph),(0,ph)))
            builder.quad(points, None if elevation else (0,1,0),
                         uv if (px,py,pw,ph)==(x,y,w,h) else local_uv, material)

    for x, y, w, h, pattern in _rectangles(
            [v if v in selected else None for v in patterns], width // 2, height // 2):
        top(x * 2, y * 2, w * 2, h * 2, 'motif', pattern)
        for row in range(y * 2, (y + h) * 2):
            occupied[row * width + x * 2:row * width + (x + w) * 2] = b'\1' * (w * 2)
    remaining = [None if occupied[i] else ('solid', solids[v], heights[i]) if v in solids else
                 ('tile', v, heights[i]) if v in native else None for i, v in enumerate(cells)]
    for x, y, w, h, value in _rectangles(remaining, width, height):
        top(x, y, w, h, value[0], value[1])
    for i, value in enumerate(cells):
        if not occupied[i] and value not in solids and value not in native:
            top(i % width, i // width, 1, 1, 'atlas', value)
    collision_cells = codes if regions is None else tuple(zip(codes, regions))
    for x, y, w, h, value in _rectangles(collision_cells, width, height):
        code = value if regions is None else value[0]
        for px,py,pw,ph in pieces(x,y,w,h):
            points = [builder.point(px+dx,py+dy,heights[y*width+x])
                      for dx,dy in ((0,0),(pw,0),(pw,ph),(0,ph))]
            builder.quad(points, (0,1,0), ((0,0),)*4, None, code)
    if wall_height:
        _walls(builder, width, height, codes, wall_color)
    return Terrain(tuple(builder.textures), tuple(builder.materials), tuple(builder.vertices),
                   tuple(builder.triangles), tuple(builder.collision_vertices), tuple(builder.collision_triangles))
