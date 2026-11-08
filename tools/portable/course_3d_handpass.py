"""Versioned, separately authored additive elevation guides.

Guides are static single-valued triangle patches in glTF X/-Z metres. Each
connected boundary is zero, holes and nonconforming seams are refused. A guide
boundary cutting a terrain cell needs a flat zero rim covering that cell: this
prevents the resampled terrain from bleeding beyond the author's patch.
"""
from bisect import bisect_right
from collections.abc import Mapping
from dataclasses import dataclass, fields, is_dataclass
from fractions import Fraction
import hashlib
import math
from pathlib import Path
import re
import struct
import tempfile
import os

import course_3d_format as wire
from course_3d_gltf import Scene, write_json
from course_3d_source import SourceCourse
import course_3d_handpass_mesh as mesh_additions

MAX_COURSES = 256
MAX_MANIFEST_BYTES = 1024*1024
MAX_GUIDE_VERTICES = 16384
MAX_GUIDE_TRIANGLES = 16384
MAX_BUCKET_REFS = 1048576
MAX_WORK = 4194304
Q = 65536


class HandpassError(ValueError):
    """An invalid or unsupported authoring guide; no partial result is returned."""


def _fail(message):
    raise HandpassError('handpass: '+message)


def _number(value):
    if type(value) not in (int, float) or not math.isfinite(value):
        _fail('expected finite number')
    return value


def _q(value):
    return wire.fixed(_number(value), low=-4096, high=4096, label='handpass coordinate')


def _round(value):
    n, d = value.numerator, value.denominator
    return (1 if n >= 0 else -1)*((abs(n)*2+d)//(2*d))


def fingerprint(source):
    """SHA256 of resolved named source data, excluding only its display name.

    Sequence order is meaningful except passthrough sections, sorted by their
    kind/version/content. Pack offsets, input pack ordering and paths are absent.
    """
    if not isinstance(source, SourceCourse):
        _fail('fingerprint requires SourceCourse')
    digest = hashlib.sha256(b'MKSC-HANDPASS-SOURCE1\0')
    def feed(value):
        if value is None:
            digest.update(b'N')
        elif type(value) is int:
            digest.update(b'I'+struct.pack('<q', value))
        elif type(value) is bytes:
            digest.update(b'B'+struct.pack('<Q', len(value))+value)
        elif type(value) is str:
            raw = value.encode('utf8')
            digest.update(b'S'+struct.pack('<Q', len(raw))+raw)
        elif is_dataclass(value):
            digest.update(b'D')
            for field in fields(value):
                if value is source and field.name == 'name':
                    continue
                feed(field.name)
                member = getattr(value, field.name)
                if value is source and field.name == 'passthrough':
                    member = sorted(member, key=lambda s: (s.kind, s.version, s.data))
                feed(member)
        elif isinstance(value, (tuple, list)):
            digest.update(b'L'+struct.pack('<Q', len(value)))
            for member in value:
                feed(member)
        else:
            _fail('unsupported source fingerprint value')
    feed(source)
    return digest.hexdigest()


def _sources(sources, scale):
    _number(scale)
    if not .02 <= scale <= 2:
        _fail('metres_per_source_unit outside .02..2')
    values = list(sources.values() if isinstance(sources, Mapping) else sources)
    if not 1 <= len(values) <= MAX_COURSES:
        _fail('source course count exceeded')
    result = {}
    for source in values:
        if not isinstance(source, SourceCourse) or source.course_id in result:
            _fail('invalid or duplicate source course')
        result[source.course_id] = source
    return result


def _orient(a, b, c):
    return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])


def _inside(p, triangle, strict=False):
    values = [_orient(triangle[i], triangle[(i+1)%3], p) for i in range(3)]
    return all(v > 0 if strict else v >= 0 for v in values)


def _on(p, a, b):
    return (_orient(a,b,p) == 0 and min(a[0],b[0]) <= p[0] <= max(a[0],b[0])
            and min(a[1],b[1]) <= p[1] <= max(a[1],b[1]))


def _pair(a, b):
    shared = set(a) & set(b)
    if len(shared) == 3:
        _fail('duplicate or overlapping guide triangles')
    if any(_inside(p,b,True) for p in a) or any(_inside(p,a,True) for p in b):
        _fail('overlapping guide triangles')
    for i in range(3):
        p,q = a[i],a[(i+1)%3]
        for j in range(3):
            r,s = b[j],b[(j+1)%3]
            if _orient(p,q,r)*_orient(p,q,s) < 0 and _orient(r,s,p)*_orient(r,s,q) < 0:
                _fail('overlapping guide triangles')
            if any(v not in (x,y) and _on(v,x,y)
                   for v,x,y in ((p,r,s),(q,r,s),(r,p,q),(s,p,q))):
                _fail('nonconforming guide seam')
    if len(shared) == 2:
        p,q = sorted(shared)
        if _orient(p,q,next(v for v in a if v not in shared))*_orient(p,q,next(v for v in b if v not in shared)) > 0:
            _fail('overlapping guide triangles')


@dataclass(frozen=True)
class HeightField:
    course_id: int
    width: int
    height: int
    scale: float
    grid: tuple
    _xs: tuple
    _ys: tuple
    additions: tuple = ()

    @property
    def elevation_grid(self):
        return tuple(v/Q for v in self.grid)

    def _height(self, x, y):
        if not 0 <= x <= self._xs[-1] or not 0 <= y <= self._ys[-1]:
            return Fraction(0)
        i = min(self.width-1,bisect_right(self._xs,x)-1)
        j = min(self.height-1,bisect_right(self._ys,y)-1)
        dx,dy = self._xs[i+1]-self._xs[i],self._ys[j+1]-self._ys[j]
        u,v = x-self._xs[i],y-self._ys[j]
        a = j*(self.width+1)+i
        h0,h1,h2,h3 = (self.grid[k] for k in (a,a+1,a+self.width+2,a+self.width+1))
        if u*dy >= v*dx:
            return ((dx-u)*dy*h0+(u*dy-v*dx)*h1+v*dx*h2)/(dx*dy)
        return ((dy-v)*dx*h0+u*dy*h2+(v*dx-u*dy)*h3)/(dx*dy)

    def sample_world(self, x, z):
        return _round(self._height(Fraction(_q(x)),Fraction(_q(-_number(z)))))/Q

    def edge_parameters(self, a, b):
        """Fractions including endpoints where the encoded elevation slope changes."""
        if len(a) != 3 or len(b) != 3:
            _fail('edge endpoints need three coordinates')
        for value in (*a,*b):
            _number(value)
        x,y = _q(a[0]),_q(-a[2])
        dx,dy = _q(b[0])-x,_q(-b[2])-y
        times = {Fraction(0),Fraction(1)}
        for values,start,delta in ((self._xs,x,dx),(self._ys,y,dy)):
            if delta:
                times.update(t for value in values if 0 < (t:=Fraction(value-start,delta)) < 1)
        base = sorted(times)
        for left,right in zip(base,base[1:]):
            t = (left+right)/2
            px,py = x+t*dx,y+t*dy
            if not 0 <= px <= self._xs[-1] or not 0 <= py <= self._ys[-1]:
                continue
            i,j = min(self.width-1,bisect_right(self._xs,px)-1),min(self.height-1,bisect_right(self._ys,py)-1)
            w,h = self._xs[i+1]-self._xs[i],self._ys[j+1]-self._ys[j]
            delta = dx*h-dy*w
            if delta:
                t = Fraction(-((x-self._xs[i])*h-(y-self._ys[j])*w),delta)
                if left < t < right:
                    times.add(t)
        times = sorted(times)
        heights = [self._height(Fraction(x)+t*dx,Fraction(y)+t*dy) for t in times]
        keep = [times[0]]
        for k in range(1,len(times)-1):
            if (heights[k]-heights[k-1])/(times[k]-times[k-1]) != (heights[k+1]-heights[k])/(times[k+1]-times[k]):
                keep.append(times[k])
        return tuple(float(t) for t in (*keep,times[-1]))


def _field(root, row, source, scale):
    xs = tuple(_q(i*8*scale) for i in range(source.map_width+1))
    ys = tuple(_q(i*8*scale) for i in range(source.map_height+1))
    reserve = math.ceil(2*max(xs[-1],ys[-1])*2**-23)+4
    triangles, heights, edges, buckets = [], {}, {}, {}
    work = refs = vertices = 0
    def spend(count=1):
        nonlocal work
        work += count
        if work > MAX_WORK:
            _fail('guide work budget exceeded')
    def cell(values, value):
        return max(0,min(len(values)-2,bisect_right(values,value)-1))
    gltf = wire.Gltf(root,row['height_guide']['scene'])
    for points,_,_,faces,_ in gltf.primitives(row['height_guide']['node']):
        vertices += len(points)
        if vertices > MAX_GUIDE_VERTICES or len(triangles)+len(faces) > MAX_GUIDE_TRIANGLES:
            _fail('guide vertex/triangle cap exceeded')
        encoded = []
        for x,h,z in points:
            x,y,h = _q(x),_q(-z),_q(h)
            if not -reserve <= x <= xs[-1]+reserve or not -reserve <= y <= ys[-1]+reserve:
                _fail('guide projection outside course bounds')
            p = (x,y)
            if p in heights and heights[p] != h:
                _fail('guide is not single-valued')
            heights[p] = h
            encoded.append(p)
        for face in faces:
            tri = tuple(encoded[i] for i in face)
            area = _orient(*tri)
            if not area:
                _fail('degenerate projected guide triangle')
            if area < 0:
                tri = (tri[0],tri[2],tri[1])
            index = len(triangles)
            lowx,highx = cell(xs,min(p[0] for p in tri)),cell(xs,max(p[0] for p in tri))
            lowy,highy = cell(ys,min(p[1] for p in tri)),cell(ys,max(p[1] for p in tri))
            refs += (highx-lowx+1)*(highy-lowy+1)
            if refs > MAX_BUCKET_REFS:
                _fail('guide bucket reference budget exceeded')
            triangles.append((tri,(lowx,lowy)))
            for j in range(lowy,highy+1):
                for i in range(lowx,highx+1):
                    buckets.setdefault((i,j),[]).append(index)
            for a,b in zip(tri,tri[1:]+tri[:1]):
                edges.setdefault(tuple(sorted((a,b))),[]).append((a,b))
    if not triangles:
        _fail('empty guide')
    for bucket,indices in buckets.items():
        for n,a in enumerate(indices):
            for b in indices[n+1:]:
                spend()
                ta,la = triangles[a]
                tb,lb = triangles[b]
                if bucket == (max(la[0],lb[0]),max(la[1],lb[1])):
                    _pair(ta,tb)
    boundary = {}
    incoming = set()
    for uses in edges.values():
        if len(uses) > 2:
            _fail('nonmanifold guide edge')
        if len(uses) == 1:
            a,b = uses[0]
            if heights[a] or heights[b]:
                _fail('guide boundary must have zero height')
            if a in boundary or b in incoming:
                _fail('nonmanifold guide boundary')
            boundary[a] = b
            incoming.add(b)
    if set(boundary) != incoming:
        _fail('open guide boundary')
    unseen = set(boundary)
    while unseen:
        start = min(unseen)
        current,area = start,0
        while True:
            if current not in unseen:
                _fail('ambiguous guide boundary')
            unseen.remove(current)
            following = boundary[current]
            area += current[0]*following[1]-following[0]*current[1]
            current = following
            if current == start:
                break
        if area <= 0:
            _fail('guide holes are unsupported')
    grid = []
    for y in ys:
        for x in xs:
            found = None
            for index in buckets.get((cell(xs,x),cell(ys,y)),()):
                spend()
                tri,_ = triangles[index]
                if _inside((x,y),tri):
                    area = _orient(*tri)
                    value = Fraction(sum(_orient(tri[(i+1)%3],tri[(i+2)%3],(x,y))*heights[tri[i]] for i in range(3)),area)
                    if found is not None and value != found:
                        _fail('ambiguous guide height')
                    found = value
            grid.append(0 if found is None else _round(found))
    for a,b in boundary.items():
        times = {Fraction(0),Fraction(1)}
        for values,axis in ((xs,0),(ys,1)):
            delta = b[axis]-a[axis]
            if delta:
                times.update(t for v in values if 0 < (t:=Fraction(v-a[axis],delta)) < 1)
        times = sorted(times)
        for left,right in zip(times,times[1:]):
            spend()
            t = (left+right)/2
            x,y = a[0]+t*(b[0]-a[0]),a[1]+t*(b[1]-a[1])
            i,j = cell(xs,x),cell(ys,y)
            if xs[i] < x < xs[i+1] and ys[j] < y < ys[j+1]:
                k = j*len(xs)+i
                if any(grid[n] for n in (k,k+1,k+len(xs),k+len(xs)+1)):
                    _fail('guide needs a flat zero boundary rim on crossed terrain cells')
    return HeightField(source.course_id,source.map_width,source.map_height,scale,tuple(grid),xs,ys)


def load(manifest, sources, scale):
    """Validate every selected overlay before returning immutable height fields."""
    available = _sources(sources,scale)
    manifest = Path(manifest)
    root = manifest.parent.resolve()
    try:
        manifest.resolve().relative_to(root)
        if manifest.stat().st_size > MAX_MANIFEST_BYTES:
            _fail('manifest size cap exceeded')
        document = wire.load(manifest)
        if set(document) != {'version','courses'} or type(document['version']) is not int or document['version'] != 1:
            _fail('manifest requires version1 and courses')
        rows = document['courses']
        if type(rows) is not list or not 1 <= len(rows) <= MAX_COURSES:
            _fail('invalid manifest course count')
        result = {}
        for row in rows:
            required={'course_id','source_sha256','metres_per_source_unit'}
            if type(row) is not dict or not required <= set(row) or \
                set(row)-required-{'height_guide','mesh_additions'} or \
                not ('height_guide' in row or 'mesh_additions' in row):
                _fail('invalid course manifest keys')
            identity = row['course_id']
            if type(identity) is not int or identity not in available or identity in result:
                _fail('unknown or duplicate course ID')
            source = available[identity]
            if type(row['source_sha256']) is not str or not re.fullmatch('[0-9a-f]{64}',row['source_sha256']) or row['source_sha256'] != fingerprint(source):
                _fail(f'course {identity}: stale source fingerprint')
            if _number(row['metres_per_source_unit']) != scale:
                _fail(f'course {identity}: metre scale mismatch')
            if 'height_guide' in row:
                guide = row['height_guide']
                if type(guide) is not dict or set(guide) != {'scene','node'} or type(guide['node']) is not str or not 1 <= len(guide['node']) <= 256:
                    _fail(f'course {identity}: invalid height guide')
                field = _field(root,row,source,scale)
            else:
                field = HeightField(identity,source.map_width,source.map_height,scale,
                    (0,)*((source.map_width+1)*(source.map_height+1)),
                    tuple(_q(i*8*scale) for i in range(source.map_width+1)),
                    tuple(_q(i*8*scale) for i in range(source.map_height+1)))
            additions=mesh_additions.load(root,row['mesh_additions']) if 'mesh_additions' in row else ()
            result[identity] = HeightField(field.course_id,field.width,field.height,field.scale,
                                           field.grid,field._xs,field._ys,additions)
        return result
    except (OSError, ValueError, KeyError, TypeError, IndexError) as exc:
        if isinstance(exc,HandpassError):
            raise
        raise HandpassError(f'handpass {manifest}: {exc}') from exc


def export(directory, sources, scale):
    """Create a fresh editable flat guide set at eight-tile authoring spacing.

    The final map edges are always included. Native tile corners are resampled
    on load, so a 256-tile course needs only 1089 authoring vertices.
    """
    available = _sources(sources,scale)
    directory = Path(directory)
    if directory.exists() or directory.is_symlink():
        _fail('export directory must be fresh')
    directory.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.handpass-',dir=directory.parent) as temporary:
        stage = Path(temporary)/'output'
        stage.mkdir()
        rows = []
        for identity,source in sorted(available.items()):
            xs = list(range(0,source.map_width,8))+[source.map_width]
            ys = list(range(0,source.map_height,8))+[source.map_height]
            scene = Scene(stage/str(identity))
            scene.material()
            vertices = [(x*8*scale,0,-y*8*scale,0,1,0,0,0) for y in ys for x in xs]
            faces = []
            for j in range(len(ys)-1):
                for i in range(len(xs)-1):
                    a = j*len(xs)+i
                    b,c,d = a+1,a+len(xs)+1,a+len(xs)
                    faces.extend(((a,b,c,0),(a,c,d,0)))
            scene.mesh('Elevation',vertices,faces)
            scene.save()
            rows.append(dict(course_id=identity,source_sha256=fingerprint(source),metres_per_source_unit=scale,
                             height_guide=dict(scene=f'{identity}/scene.gltf',node='Elevation')))
        write_json(stage/'handpass.json',dict(version=1,courses=rows))
        os.replace(stage,directory)
    return directory/'handpass.json'
