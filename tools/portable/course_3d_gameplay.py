"""Derive modern course gameplay from preserved source raster/route geometry.

The modern ordered gate graph records forward raster progress along the source
AI path. It deliberately does not emulate unrestricted source checkpoint jumps.
"""
import bisect
import math
import heapq
import course_3d_format

import course_3d_arena
import course_3d_launch


class GameplayError(ValueError):
    pass


def region(source, x, y):
    cx, cy = math.floor(x/16), math.floor(y/16)
    if not 0 <= cx < source.raster_width or not 0 <= cy < source.raster_height:
        return 0x7f
    return source.raster[cy*source.raster_width+cx]


def material(source, x, y):
    tx, ty = math.floor(x/8), math.floor(y/8)
    if not 0 <= tx < source.map_width or not 0 <= ty < source.map_height:
        return 0x26
    return source.materials[source.tilemap[ty*source.map_width+tx]]


def position(x, y, scale, height=0):
    return [x*scale,height,-y*scale]


def orientation(dx, dy):
    # Source heading0 faces north (-sourceY); glTF identity faces +Z.
    yaw = math.atan2(dx,-dy)
    return [0,math.sin(yaw/2),0,math.cos(yaw/2)]


def _canonical(source):
    points = []
    for point in source.route_rows[0]:
        value = (point.x,point.y)
        if not points or value != points[-1]:
            points.append(value)
    if len(points)>1 and points[-1] == points[0]:
        points.pop()
    if len(points)<3:
        raise GameplayError(f'course={source.course_id} route: fewer than three distinct positions')
    return points


def _finish(source, points):
    y = source.rules.finish_y_q16/65536
    candidates = []
    for a,b in zip(points,points[1:]+points[:1]):
        if a[1] >= y > b[1]:
            t = (y-a[1])/(b[1]-a[1])
            x = a[0]+(b[0]-a[0])*t
            if region(source,x,y-.01)&0x80:
                candidates.append((abs(x-source.rules.start_x_q16/65536),x))
    if not candidates:
        raise GameplayError(f'course={source.course_id} finish: route misses northbound bit7 crossing')
    x = min(candidates)[1]
    cx = math.floor(x/16)
    left, right = cx, cx
    while left>0 and region(source,(left-.5)*16,y-.01)&0x80:
        left -= 1
    while right+1<source.raster_width and region(source,(right+1.5)*16,y-.01)&0x80:
        right += 1
    return ((left+right+1)*8,y), (right-left+1)*8, (x,y)


def _sample_loop(points, crossing):
    # Start immediately after the selected finish crossing, retaining the source
    # polyline's order even when repeated AI target rows were removed.
    choices = []
    for i,(a,b) in enumerate(zip(points,points[1:]+points[:1])):
        dx,dy = b[0]-a[0],b[1]-a[1]
        length = dx*dx+dy*dy
        t = max(0,min(1,((crossing[0]-a[0])*dx+(crossing[1]-a[1])*dy)/length))
        distance = (a[0]+t*dx-crossing[0])**2+(a[1]+t*dy-crossing[1])**2
        choices.append((distance,i))
    _,edge = min(choices)
    chain = [crossing]+points[edge+1:]+points[:edge+1]+[crossing]
    for a,b in zip(chain,chain[1:]):
        dx,dy = b[0]-a[0],b[1]-a[1]
        length = math.hypot(dx,dy)
        if length <= 1e-8:
            continue
        normal = (dx/length,dy/length)
        steps = max(1,math.ceil(length*2))
        for i in range(1,steps+1):
            t = i/steps
            yield (a[0]+t*dx,a[1]+t*dy),normal


def _road_corridor(code):
    # Off-road remains drivable. Gates must cover legal corner cuts there;
    # recovery surfaces, walls and unrelated progress regions still stop them.
    return not (0x20 <= code < 0x30 or 0x80 <= code < 0x90)


def _corridor_extent(source, point, direction, admitted):
    """Walk exact 8-unit tile intervals; raster boundaries align every two tiles."""
    crossings = []
    steps = []
    for coordinate, component in zip(point, direction):
        if abs(component) < 1e-12:
            crossings.append(math.inf)
            steps.append(math.inf)
            continue
        cell = math.floor(coordinate/8)
        if component < 0 and coordinate == cell*8:
            cell -= 1
        boundary = (cell+1)*8 if component > 0 else cell*8
        crossings.append((boundary-coordinate)/component)
        steps.append(8/abs(component))
    distance = 0.0
    # A straight ray crosses each map row/column at most once. The first
    # outside interval is a recovery surface and terminates the corridor.
    for _ in range(source.map_width+source.map_height+2):
        following = min(crossings)
        middle = (distance+following)/2
        x = point[0]+direction[0]*middle
        y = point[1]+direction[1]*middle
        progress = region(source,x,y)&0x7f
        # Source B98C returns on127 before publishing checkpoint progress:
        # unnamed road retains the previous stage; it is not a terrain wall.
        if (not _road_corridor(material(source,x,y)) or
                (progress != 127 and progress not in admitted)):
            return distance
        distance = following
        for axis in range(2):
            if crossings[axis] <= following:
                crossings[axis] += steps[axis]
    raise GameplayError(f'course={source.course_id} gates: corridor exceeds map bounds')


def _gate_width(source, point, normal, admitted, *, minimum_margin=0):
    if not _road_corridor(material(source,*point)):
        return None
    rx,ry = -normal[1],normal[0]
    # Progress labels divide the driving surface, rather than its width. Admit
    # an adjacent stage on either side of the transition, but stop at a distant
    # bend even when its road tiles happen to touch this perpendicular ray.
    count = len(source.route_rows[0])
    neighboring = {value+offset for value in admitted for offset in (-1,0,1)
                   if 0 <= value+offset < count}
    left = _corridor_extent(source,point,(-rx,-ry),neighboring)
    right = _corridor_extent(source,point,(rx,ry),neighboring)
    if left+right <= 0 or min(left,right) < minimum_margin:
        return None
    shift = (right-left)/2
    center = point[0]+rx*shift,point[1]+ry*shift
    return center,(left+right)/2


def _gate_loop(points, crossing):
    """Exact source segments, rotated at the common northbound finish."""
    closest = min(range(len(points)), key=lambda i:
        _point_segment_distance_squared(crossing,points[i],points[(i+1)%len(points)]))
    chain = [crossing]+points[closest+1:]+points[:closest+1]+[crossing]
    return [(a,b) for a,b in zip(chain,chain[1:]) if a != b]


def _gate_hit(gate, a, b, scale, *, body_margin=None):
    # Match the encoded Q16/Q30 crossing test, including C's signed division.
    # Route geometry is immutable; no simulation or unbounded stepping occurs.
    def div(n,d):
        return (abs(n)//abs(d))*(-1 if (n<0)!=(d<0) else 1)
    u = 1<<30
    encode = course_3d_format.fixed
    center = course_3d_format.position(gate['center'],'gate.center')
    normal = course_3d_format.vector(gate['normal'],'gate.normal',True)
    right = course_3d_format.vector(gate['right'],'gate.right',True)
    before = [x-y for x,y in zip(course_3d_format.position(
        position(*a,scale),'route.before'),center)]
    after = [x-y for x,y in zip(course_3d_format.position(
        position(*b,scale),'route.after'),center)]
    da = div(sum(x*y for x,y in zip(before,normal)),u)
    db = div(sum(x*y for x,y in zip(after,normal)),u)
    if not da < 0 <= db:
        return None
    relative = [x+div((y-x)*(-da),db-da) for x,y in zip(before,after)]
    offset = div(sum(x*y for x,y in zip(relative,right)),u)
    # The runtime admits centers. Converter feasibility additionally reserves
    # the same body margin as corridor construction on both aperture edges.
    if body_margin is None:
        body_margin=_ROUTE_CLEARANCE
    if abs(offset)>encode(gate['half_width'])-encode(body_margin*scale):
        return None
    time = div((-da)*u,db-da)
    point = (a[0]+(b[0]-a[0])*time/u,a[1]+(b[1]-a[1])*time/u)
    return time,point


def _lane_gate(source, scale, raw, point, normal):
    corridor = _gate_width(source,point,normal,{raw-1,raw},
                           minimum_margin=_ROUTE_CLEARANCE)
    if corridor is None:
        return None
    center,half = corridor
    dx,dy = normal
    return dict(center=position(*center,scale),normal=[dx,0,-dy],
                right=[-dy,0,-dx],half_width=half*scale,half_height=20,
                flags=4)


def _lane_candidates(source, scale, points, crossing, raws):
    """Find dry, body-wide witnesses; 127 and lower crossed roads carry progress."""
    wanted = set(raws[1:])
    candidates = {}
    previous_point = crossing
    high = 0
    for point,normal in _sample_loop(points,crossing):
        raw = region(source,*point)&127
        if raw != 127 and raw>high:
            gate = _lane_gate(source,scale,raw,point,normal)
            if gate is not None:
                crossed = [target for target in sorted(wanted) if high<target<=raw]
                if crossed:
                    options = []
                    # Prefer actual raster boundary normals over extrapolating
                    # one lane's tangent across a differently directed lane.
                    for axis in range(2):
                        before,after = previous_point[axis],point[axis]
                        if before == after or math.floor(before/16)==math.floor(after/16):
                            continue
                        boundary = (math.floor(after/16) if after>before else
                                    math.floor(before/16))*16
                        t = (boundary-before)/(after-before)
                        at = tuple(previous_point[j]+(point[j]-previous_point[j])*t
                                   for j in range(2))
                        direction = tuple((1 if after>before else -1) if j==axis else 0
                                          for j in range(2))
                        candidate = _lane_gate(source,scale,raw,at,direction)
                        if candidate is not None:
                            options.append(candidate)
                    options.append(gate)
                    # Source progress publishes a high-water value, not a
                    # requirement to touch every intervening raster label.
                    # A genuine 21->23 transition witnesses both ordered
                    # thresholds at this SAME aperture, never a bypass edge.
                    for target in crossed:
                        candidates[target] = options
                high = raw
        previous_point = point
    return candidates


def _interior_landing_fronts(source, scale, axis, sign, boundary, low, high, raw, spend):
    """First body-clear floor section joining this front's physical slits.

    Connectivity advances only through the exact named component. A wall or
    recovery tile cannot bridge slits, and an isolated floor pocket has no
    source-front seed. No whole-map flood or later-stage detour is involved.
    """
    other=1-axis
    normal=[0,0]
    normal[axis]=sign
    def point(depth,cell):
        p=[0.0,0.0]
        p[axis],p[other]=boundary+sign*(depth*8+4),(cell+.5)*8
        return p
    def floor(depth,cell):
        spend()
        p=point(depth,cell)
        return region(source,*p)&127 == raw and 0x40 <= material(source,*p) < 0x80
    frontier={}
    component=0
    previous=False
    for cell in range(low,high):
        admitted=floor(0,cell)
        if admitted and not previous:
            component+=1
        if admitted:
            frontier[cell]={component}
        previous=admitted
    if component<2:
        return []
    if not any(0x20 <= material(source,*point(0,cell)) < 0x30 or
               0x80 <= material(source,*point(0,cell)) < 0x90
               for cell in range(min(frontier),max(frontier)+1)):
        return []
    # Positions cross float32 glTF and Q16 encoding. Reserve the same bounded
    # distance error as arrival geometry, expressed back in source units.
    margin=_ROUTE_CLEARANCE+2*max(source.map_width,source.map_height)*8*(2**-23)+4/(65536*scale)
    if margin>=4:
        return []
    limit=source.map_width if axis==0 else source.map_height
    for depth in range(1,limit):
        available={cell for cell in range(low,high) if floor(depth,cell)}
        following={}
        candidates=[]
        cell=low
        while cell<high:
            if cell not in available:
                cell+=1
                continue
            first=cell
            origins=set()
            while cell<high and cell in available:
                spend()
                origins.update(frontier.get(cell,()))
                # Two distinct seeds already prove a join. Keep constant
                # per-cell union work regardless of the number of comb slits.
                if len(origins)>2:
                    origins=set(sorted(origins)[:2])
                cell+=1
            if not origins:
                continue
            for connected in range(first,cell):
                following[connected]=origins
            # A persistent dividing wall never joins the source slits. Keep
            # its separate front apertures rather than spanning that wall.
            if len(origins)<2 or (cell-first)*8<=2*margin:
                continue
            center=[0.0,0.0]
            center[axis]=boundary+sign*(depth*8+margin)
            center[other]=(first+cell)*4
            dx,dy=normal
            candidates.append(dict(center=position(*center,scale),normal=[dx,0,-dy],
                right=[-dy,0,-dx],half_width=((cell-first)*4-margin)*scale,
                half_height=20,flags=4))
        if candidates:
            return candidates
        if not following:
            return []
        frontier=following
    return []


def _raster_landing_fronts(source, scale, before, after, previous, raw, *, budget=None,
                           include_air=True):
    """Finite source progress fronts, including height-permitted air gaps.

    B98C publishes named raster progress without a direction/ground guard.
    Keep modern forward order, but do not replace a real air entrance with
    only a later oblique dry-road aperture. Mode-bit7 height guards cannot be
    represented by this unconditional air aperture, so retain their old gates.
    """
    if not all(raw < len(row) and not (row[raw].mode & 128)
               for row in source.route_rows):
        return []
    result = []
    budget = [1048576] if budget is None else budget
    def spend():
        budget[0] -= 1
        if budget[0] < 0:
            raise GameplayError(f'course={source.course_id} gates: landing front work budget exhausted')
    for axis in range(2):
        start,end = before[axis],after[axis]
        if start == end or math.floor(start/16) == math.floor(end/16):
            continue
        sign = 1 if end > start else -1
        boundary = (math.floor(end/16) if sign > 0 else math.floor(start/16))*16
        fraction = (boundary-start)/(end-start)
        if not 0 <= fraction <= 1:
            continue
        other = 1-axis
        coordinate = before[other]+(after[other]-before[other])*fraction
        seed = math.floor(coordinate/8)
        limit = source.map_height if axis == 0 else source.map_width
        def probes(cell):
            ahead = [0.0,0.0]
            ahead[axis],ahead[other] = boundary+sign*4,(cell+.5)*8
            behind = list(ahead)
            behind[axis] = boundary-sign*4
            return ahead,behind
        def logical(cell):
            spend()
            if not 0 <= cell < limit:
                return False
            ahead,behind = probes(cell)
            return region(source,*ahead)&127 == raw and region(source,*behind)&127 == previous
        if not logical(seed):
            continue
        low,high = seed,seed+1
        while low > 0 and logical(low-1):
            low -= 1
        while high < limit and logical(high):
            high += 1
        # B98C samples named progress independently of terrain material.
        # An airborne center may enter above a wall; collision and safe RESP
        # remain separate from this finite, height-permitted logical front.
        if include_air:
            center = [0.0,0.0]
            center[axis],center[other] = boundary,(low+high)*4
            normal = [0,0]
            normal[axis] = sign
            dx,dy = normal
            result.append(dict(center=position(*center,scale),normal=[dx,0,-dy],
                right=[-dy,0,-dx],half_width=(high-low)*4*scale,
                half_height=20,flags=4))
        result.extend(_interior_landing_fronts(source,scale,axis,sign,boundary,
                                                low,high,raw,spend))
    return result


def _unnamed_corner_fronts(source, scale, mapping, raws, *, budget=None):
    """Local A/127/B/C corner witnesses for consecutive source thresholds.

    Accepted A is the prior authority;127 grants nothing. Entry into C really
    publishes C in B98C, so the same finite front witnesses ordered B and C.
    This is logical center progress, not a claim of body-clear wall passage.
    """
    budget=[1048576] if budget is None else budget
    found={}
    total=0
    for y in range(16,source.map_height*8,16):
        for x in range(16,source.map_width*8,16):
            for axis in range(2):
                other=1-axis
                for nx,ny in ((-1,-1),(-1,1),(1,-1),(1,1)):
                    budget[0]-=1
                    if budget[0]<0:
                        raise GameplayError(f'course={source.course_id} gates: unnamed corner work budget exhausted')
                    neutral=(nx,ny)
                    prior=list(neutral);prior[other]*=-1
                    target=list(neutral);target[axis]*=-1
                    middle=(-nx,-ny)
                    def at(offset):
                        return x+offset[0]*4,y+offset[1]*4
                    values=[region(source,*at(offset)) for offset in (prior,neutral,middle,target)]
                    a,n,b,c=values
                    # Literal127 only: bit7 variants and finish-bit cells
                    # stay authored under this deliberately narrow rule, as do
                    # unordered/skipped raw labels. No finish-wrap inference.
                    if n!=127 or not 0<a<127 or b!=a+1 or c!=a+2 or c>=127:
                        continue
                    stages=[mapping.get(raw) for raw in (a,b,c)]
                    if (any(stage is None or stage>=len(raws) for stage in stages) or
                            any(raws[stage]!=raw for stage,raw in zip(stages,(a,b,c))) or
                            stages[1]!=stages[0]+1 or stages[2]!=stages[1]+1):
                        continue
                    if not all(c<len(row) and not row[c].mode&128 for row in source.route_rows):
                        continue
                    # One raster-cell side is16 units, split into two8-unit
                    # material tiles. Validate each finite half independently;
                    # a wall/recovery tile must not hide its clear neighbor.
                    valid=[]
                    for half in range(2):
                        budget[0]-=1
                        if budget[0]<0:
                            raise GameplayError(f'course={source.course_id} gates: unnamed corner work budget exhausted')
                        probes=[]
                        for offset in (neutral,target):
                            point=list(at(offset))
                            point[other]+=neutral[other]*half*8
                            probes.append(point)
                        valid.append(all(region(source,*point)==label and
                            _road_corridor(material(source,*point))
                            for point,label in zip(probes,(127,c))))
                    spans=([(0,2)] if all(valid) else
                           [(half,half+1) for half,clear in enumerate(valid) if clear])
                    for first,last in spans:
                        center=[x,y]
                        center[other]+=neutral[other]*(first+last)*4
                        normal=[0,0];normal[axis]=-neutral[axis]
                        dx,dy=normal
                        gate=dict(center=position(*center,scale),normal=[dx,0,-dy],
                            right=[-dy,0,-dx],half_width=(last-first)*4*scale,
                            half_height=20,flags=4)
                        key=(tuple(center),tuple(normal))
                        for raw in (b,c):
                            owned=found.setdefault(raw,{})
                            if key not in owned:
                                total+=1
                                if total>1024:
                                    raise GameplayError(f'course={source.course_id} gates: unnamed corner table budget exhausted')
                                owned[key]=gate
    return {raw:list(gates.values()) for raw,gates in found.items()}


def _add_landing_fronts(source, scale, graph, respawns, mapping, raws, rows, fronts):
    if not fronts:
        return graph
    groups = [[] for _ in raws]
    for gate in graph:
        groups[gate['stage']].append(gate)
    changed = False
    for stage,raw in enumerate(raws[1:],1):
        for candidate in fronts.get(raw,[]):
            if not any(all(old.get(key) == value for key,value in candidate.items())
                       for old in groups[stage]):
                groups[stage].append(candidate)
                changed = True
    if not changed:
        return graph
    if sum(map(len,groups)) > 1024:
        raise GameplayError(f'course={source.course_id} gates: landing front table budget exhausted')
    candidate,indices = _gate_graph(groups)
    if sum(len(g['successors']) for g in candidate) > 16384:
        raise GameplayError(f'course={source.course_id} gates: landing front edge budget exhausted')
    paths = [_gate_loop(points,_finish(source,points)[2]) for points in rows]
    # Optional parallel fronts may not admit a retained lane's unrelated early
    # crossing or reorder its successors. Decline the complete addition if so.
    if _gate_graph_failure(source,scale,candidate,paths,mapping) is not None:
        return graph
    for stage,respawn in enumerate(respawns):
        respawn['checkpoint'] = indices[stage][0]
    return candidate


def _gate_graph(groups):
    indices,output = [],[]
    for stage,alternatives in enumerate(groups):
        indices.append(list(range(len(output),len(output)+len(alternatives))))
        for gate in alternatives:
            output.append(dict(gate,stage=stage,key_group=stage))
    for stage,ids in enumerate(indices):
        for index in ids:
            output[index]['successors'] = indices[(stage+1)%len(groups)]
    return output,indices


def _lane_progress(source, path, mapping, final_stage):
    """First source high-water witness for every ordered threshold.

    Enumerate exact raster intervals so narrow diagonal cells cannot disappear
    between samples. Lower crossed roads and127 retain prior progress.
    """
    upper = [0]+[None]*final_stage
    intervals = []
    high = 0
    for a,b in path:
        cuts = {0.0,1.0}
        for start,end in zip(a,b):
            if start != end:
                for cell in range(math.floor(min(start,end)/16)+1,
                                  math.floor(max(start,end)/16)+1):
                    t = (cell*16-start)/(end-start)
                    if 0<t<1:
                        cuts.add(t)
        times = sorted(cuts)
        # The shared finish point lies exactly on its half-open raster edge;
        # begin in the first positive-length AFTER-finish interval, not the
        # preceding lap's final label at that mathematical boundary.
        segment = []
        for start,end in zip(times,times[1:]):
            t = (start+end)/2
            raw = region(source,a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t)&127
            reached = high if raw==127 else mapping.get(raw,0)
            if reached>high:
                for stage in range(high+1,min(reached,final_stage)+1):
                    upper[stage] = reached
                high = reached
            segment.append((start,high))
        intervals.append(segment)
    return upper,intervals


def _gate_graph_failure(source, scale, graph, paths, mapping):
    """Bounded runtime-order traversal, rejecting unrelated early road crossings."""
    final_stage = max(gate['stage'] for gate in graph)
    for lane,path in enumerate(paths):
        upper,progress = _lane_progress(source,path,mapping,final_stage)
        if None in upper:
            return lane,upper.index(None)
        current,accepted = 0,[]
        for segment,(a,b) in enumerate(path):
            visited = set()
            last_time = -1
            for _ in range(len(graph)):
                hits = []
                for index in graph[current]['successors']:
                    hit = _gate_hit(graph[index],a,b,scale)
                    if hit is not None and index not in visited and hit[0]>=last_time:
                        hits.append((hit[0],index,hit[1]))
                if not hits:
                    break
                time,index,point = min(hits,key=lambda hit:(hit[0],hit[1]))
                stage = graph[index]['stage']
                raw = region(source,*point)&127
                if stage:
                    if raw==127:
                        # Unnamed road does NOT reset or grant source progress.
                        # Use this lane's retained high-water at the crossing,
                        # cached once from exact raster intervals above.
                        starts = [entry[0] for entry in progress[segment]]
                        interval = max(0,bisect.bisect_right(starts,time/(1<<30))-1)
                        witnessed = progress[segment][interval][1]
                    else:
                        witnessed = mapping.get(raw,0)
                    # A tangent extrapolated across another lane can cross
                    # either too early OR after the next stage's aperture.
                    # Repair that causal gate rather than moving its successor
                    # backward or accepting a future region out of order.
                    if not stage-1 <= witnessed <= upper[stage]:
                        return lane,stage
                accepted.append(stage)
                visited.add(index)
                last_time,current = time,index
        expected = list(range(1,final_stage+1))+[0]
        if accepted != expected:
            for actual,wanted in zip(accepted,expected):
                if actual != wanted:
                    return lane,wanted
            return lane,expected[min(len(accepted),len(expected)-1)]
    return None


def _repair_lane_gates(source, scale, canonical, respawns, mapping, raws, rows):
    paths = [_gate_loop(points,_finish(source,points)[2]) for points in rows]
    if _gate_graph_failure(source,scale,canonical,paths,mapping) is None:
        return canonical
    lane_candidates = [_lane_candidates(source,scale,points,_finish(source,points)[2],raws)
                       for points in rows]
    options = [[[gate]] for gate in canonical]
    for stage,raw in enumerate(raws[1:],1):
        seen = {repr(canonical[stage])}
        for lane in lane_candidates:
            for candidate in lane.get(raw,[]):
                key = repr(candidate)
                if key not in seen:
                    options[stage].append([candidate])
                    seen.add(key)
        # Different boundary components can share an ordered stage. No gate
        # spans water or another bend merely to join the three source lanes.
        alternatives = []
        for lane in lane_candidates:
            candidates = lane.get(raw,[])
            if candidates and candidates[0] not in alternatives:
                alternatives.append(candidates[0])
        if alternatives:
            options[stage].append(alternatives)
    selected = [0]*len(options)
    for _ in range(sum(len(option) for option in options)):
        groups = [options[stage][choice] for stage,choice in enumerate(selected)]
        graph,indices = _gate_graph(groups)
        failure = _gate_graph_failure(source,scale,graph,paths,mapping)
        if failure is None:
            if len(graph)>1024 or sum(len(g['successors']) for g in graph)>16384:
                raise GameplayError(f'course={source.course_id} gates: lane graph exceeds table caps')
            # Collision regions still index RESP, not CPNT or stage. Keep one
            # canonical safe recovery per stage and remap only its gate index.
            for stage,respawn in enumerate(respawns):
                respawn['checkpoint'] = indices[stage][0]
            return graph
        lane,stage = failure
        if stage==0 or selected[stage]+1>=len(options[stage]):
            raise GameplayError(f'course={source.course_id} gates: route={lane} stage={stage} '
                                'has no ordered body-wide lane aperture')
        selected[stage] += 1
    raise GameplayError(f'course={source.course_id} gates: lane repair budget exhausted')


def _recovery_floor(source, point, end=None):
    """Reserve dry body support and a clear grounded exit before forced launches."""
    end=point if end is None else end
    radius=_ROUTE_CLEARANCE
    for ty in range(math.floor((min(point[1],end[1])-radius)/8),
                    math.floor((max(point[1],end[1])+radius)/8)+1):
        for tx in range(math.floor((min(point[0],end[0])-radius)/8),
                        math.floor((max(point[0],end[0])+radius)/8)+1):
            if _segment_rectangle_distance_squared(point,end,tx*8,ty*8)>=radius*radius:
                continue
            code=material(source,(tx+.5)*8,(ty+.5)*8)
            if not (0x40<=code<0x80 or code<0x20 and code&15 not in (0,2,6,14)):
                return False
    return True


def gates(source, scale, *, canonical=None, adapted=None, progress_raw=None):
    """Build gates; optionally publish original raw labels by stage on success.

    Parallel apertures retain one stage label; wire gate/RESP indices and CPU
    guide nodes never index this table. The existing three-value return stays
    unchanged, and the optional collector is untouched if any repair fails.
    """
    points = (_adapt_route(source,_canonical(source),0,scale)
              if canonical is None else canonical)
    center,width,crossing = _finish(source,points)
    path = list(_sample_loop(points,crossing))
    count = len(source.route_rows[0])
    accepted = [(0,center,(0,-1),width,crossing)]
    deferred = set()
    landing_fronts = {}
    front_budget = [1048576]
    previous_point = crossing
    previous,high = 0,0
    for point,normal in path:
        raw = region(source,*point)&0x7f
        if raw == 0x7f or raw >= count:
            previous_point = point
            continue
        if high < raw:
            if region(source,*previous_point)&127 != raw:
                candidates = _raster_landing_fronts(source,scale,previous_point,point,high,raw,
                    budget=front_budget)
                saved = landing_fronts.setdefault(raw,[])
                for candidate in candidates:
                    if candidate not in saved:
                        saved.append(candidate)
            corridor = _gate_width(source,point,normal,{previous,raw},
                                   minimum_margin=_ROUTE_CLEARANCE)
            # Source AI segments can clip a blocked tile between valid targets.
            # Wait for driving surface before authoring a gate on that segment.
            if corridor is not None:
                boundary,half = corridor
                accepted.append((raw,boundary,normal,half,point))
                high = raw
                deferred.discard(raw)
            elif _gate_width(source,point,normal,{previous,raw}) is not None:
                # A first dry sample after a jump may hug the recovery edge:
                # it is not a useful aperture until the route has body-wide
                # clearance on both sides. Never silently drop such a stage.
                deferred.add(raw)
        previous = raw
        previous_point = point
    if deferred:
        raise GameplayError(f'course={source.course_id} gates: forward region {min(deferred)} '
                            'has no body-wide gate crossing')
    if len(accepted)<3:
        raise GameplayError(f'course={source.course_id} gates: insufficient forward raster progress')
    result,respawns = [],[]
    for index,(_,center,normal,half,route_point) in enumerate(accepted):
        dx,dy = normal
        result.append(dict(center=position(*center,scale),normal=[dx,0,-dy],
                           right=[-dy,0,-dx],half_width=half*scale,half_height=20,
                           key_group=index,stage=index,flags=5 if index==0 else 4,
                           successors=[(index+1)%len(accepted)]))
        # A safe point can lie slightly before a boundary; retain the accepted
        # stage so recovery cannot skip any successor gate or award a free lap.
        safe = None
        # Reserve two authored lookahead spans of ordinary floor so a
        # stationary recovery can steer/accelerate before a ramp or hazard.
        # Prefer earlier ground over a later shelf across an unjumped gap.
        runup=3/scale
        for sign in (-1,1):
            for distance in range(0,129):
                candidate = route_point[0]+dx*distance*sign,route_point[1]+dy*distance*sign
                end=candidate[0]+dx*runup,candidate[1]+dy*runup
                if _recovery_floor(source,candidate,end):
                    safe = candidate
                    break
            if safe is not None:
                break
        if safe is None:
            raise GameplayError(f'course={source.course_id} respawn: no ground near gate {index}')
        respawns.append(dict(position=position(*safe,scale),quaternion=orientation(dx,dy),
                             checkpoint=index,radius=4,route=0))
    # Capture provenance before repairs add parallel or landing apertures.
    source_regions = [row[0] for row in accepted]
    mapping = {raw:max(0,bisect.bisect_right(source_regions,raw)-1) for raw in range(count)}
    rows = ([points] if len(source.route_rows)==1 else
            _adapt_rows(source,scale) if adapted is None else adapted)
    if len(source.route_rows)>1:
        result = _repair_lane_gates(source,scale,result,respawns,mapping,source_regions,rows)
    for raw,fronts in _unnamed_corner_fronts(source,scale,mapping,source_regions,
                                              budget=front_budget).items():
        saved=landing_fronts.setdefault(raw,[])
        saved.extend(gate for gate in fronts if gate not in saved)
    result = _add_landing_fronts(source,scale,result,respawns,mapping,source_regions,
                                  rows,landing_fronts)
    if progress_raw is not None:
        progress_raw[:] = source_regions
    return result,respawns,mapping


# Source AI targets are point-like. Converted karts need body clearance without
# changing the road artwork or allowing a repair to jump to a nearby hairpin.
def converted_wall_height(scale):
    """Source CPU terrain stops blocking once kart height exceeds four units."""
    return 4*scale


_ROUTE_CLEARANCE = 3.0
_ROUTE_SEARCH_MARGIN = 32
_ROUTE_SEARCH_BUDGET = 4096
_ROUTE_NODE_LIMIT = 4096


def _point_segment_distance_squared(point, a, b):
    dx,dy = b[0]-a[0],b[1]-a[1]
    length = dx*dx+dy*dy
    along = 0 if length == 0 else max(0,min(1,
        ((point[0]-a[0])*dx+(point[1]-a[1])*dy)/length))
    return (point[0]-a[0]-along*dx)**2+(point[1]-a[1]-along*dy)**2


def _segment_rectangle_interval(a, b, left, top, right, bottom):
    low,high = 0.0,1.0
    for start,end,minimum,maximum in ((a[0],b[0],left,right),(a[1],b[1],top,bottom)):
        delta = end-start
        if delta == 0:
            if start < minimum or start > maximum:
                return None
        else:
            near,far = sorted(((minimum-start)/delta,(maximum-start)/delta))
            low,high = max(low,near),min(high,far)
    return (low,high) if low <= high else None


def _segment_rectangle_distance_squared(a, b, left, top):
    right,bottom = left+8,top+8
    if _segment_rectangle_interval(a,b,left,top,right,bottom) is not None:
        return 0.0
    distance = min((max(left-x,0,x-right)**2+max(top-y,0,y-bottom)**2)
                   for x,y in (a,b))
    return min(distance,*( _point_segment_distance_squared(corner,a,b)
        for corner in ((left,top),(right,top),(left,bottom),(right,bottom))))


class _RouteClearance:
    def __init__(self, source, scale):
        self.source = source
        self.scale = scale
        self.clear_cache = {}

    def clear(self, a, b):
        key = tuple(sorted((a,b)))
        if key in self.clear_cache:
            return self.clear_cache[key]
        radius = _ROUTE_CLEARANCE
        source = self.source
        for y in range(math.floor((min(a[1],b[1])-radius)/8),
                       math.floor((max(a[1],b[1])+radius)/8)+1):
            for x in range(math.floor((min(a[0],b[0])-radius)/8),
                           math.floor((max(a[0],b[0])+radius)/8)+1):
                code = material(source,(x+.5)*8,(y+.5)*8)
                blocked = 0x80 <= code < 0x90 or 0x20 <= code < 0x30
                if blocked and \
                        _segment_rectangle_distance_squared(a,b,x*8,y*8) < radius*radius:
                    self.clear_cache[key] = False
                    return False
        self.clear_cache[key] = True
        return True

    def jump_connector(self, a, b, panel_credit=False):
        # Flat converted geometry uses the compiler's default modern gravity,
        # .35m suspension ride height and height-preserving source launch surfaces.
        # Walls include body/margin; recovery floors use the center projection
        # tested by core probe_support, rather than horizontal body overhang.
        if not self.clear(a,a) or not self.clear(b,b):
            return False
        distance = math.dist(a,b)
        if distance == 0:
            return False
        intervals = []
        radius = _ROUTE_CLEARANCE
        for y in range(math.floor((min(a[1],b[1])-radius)/8),
                       math.floor((max(a[1],b[1])+radius)/8)+1):
            for x in range(math.floor((min(a[0],b[0])-radius)/8),
                           math.floor((max(a[0],b[0])+radius)/8)+1):
                code = material(self.source,(x+.5)*8,(y+.5)*8)
                if not (0x20 <= code < 0x30 or 0x80 <= code < 0x90):
                    continue
                expansion = radius if 0x80 <= code < 0x90 else 0
                span = _segment_rectangle_interval(a,b,x*8-expansion,y*8-expansion,
                                                   (x+1)*8+expansion,(y+1)*8+expansion)
                if span is not None:
                    intervals.append((code,span[0]*distance,span[1]*distance))
        if not intervals:
            return False
        first_block = min(span[1] for span in intervals)
        steps = max(1,math.ceil(distance))
        launch = None
        speed,gravity = 16,course_3d_format.DEFAULT_GRAVITY
        for i in range(steps+1):
            along = distance*i/steps
            if along >= first_block:
                break
            point = (a[0]+(b[0]-a[0])*i/steps,a[1]+(b[1]-a[1])*i/steps)
            code = material(self.source,*point)
            if code < 0x20 and code&15 in (0,2,14) and self.clear(point,point):
                launch = (along,course_3d_format.source_launch(code&15))
                break # The first encountered launch makes the kart airborne.
            if panel_credit and code < 0x20 and code&15 == 6 and self.clear(point,point):
                # A crossed panel's immediate entry impulse is guaranteed; its
                # later timed acceleration is conservatively left out.
                speed = 16+course_3d_format.SOURCE_PANEL_BOOST
        if launch is None:
            return False
        phase_margin = 2*4389/262144
        last_block = max(span[2] for span in intervals)
        landing = (last_block-launch[0])*self.scale/speed+phase_margin
        if landing >= 2*launch[1]/gravity:
            return False
        for code,entry,exit in intervals:
            early = (entry-launch[0])*self.scale/speed-phase_margin
            late = (exit-launch[0])*self.scale/speed+phase_margin
            if early <= 0:
                return False
            height = min(launch[1]*t-gravity*t*t/2 for t in (early,late))
            # Water/recovery needs an airborne body; walls additionally need
            # suspension-bottom clearance above their actual generated height.
            if height <= 0:
                return False
            if 0x80 <= code < 0x90:
                if height+.35 < converted_wall_height(self.scale)+.05:
                    return False
            elif late > launch[1]/gravity:
                # Descending support probes snap within the generated airborne
                # ground-snap distance; recovery floors there are not safe air.
                descending_height = launch[1]*late-gravity*late*late/2
                if descending_height <= course_3d_format.DEFAULT_GROUND_SNAP_Q16/65536:
                    return False
        return True

    def labels(self, a, b):
        # Enumerate every positive-length raster interval, even a tiny sliver
        # beside a diagonal grid corner. Exact boundary points use region()'s
        # half-open floor convention; a line on a grid edge stays on that side.
        cuts = {0.0,1.0}
        for start,end in zip(a,b):
            if start == end:
                continue
            low,high = sorted((start,end))
            for cell in range(math.floor(low/16)+1,math.floor(high/16)+1):
                fraction = (cell*16-start)/(end-start)
                if 0 < fraction < 1:
                    cuts.add(fraction)
        times = sorted(cuts)
        times += [(x+y)/2 for x,y in zip(times,times[1:])]
        return {region(self.source,a[0]+(b[0]-a[0])*t,
                       a[1]+(b[1]-a[1])*t)&127 for t in times}

    def neighbors(self, labels):
        if self.source.arena:
            return set(range(128))
        count = len(self.source.route_rows[0])
        return {label+offset for label in labels if label != 127
                for offset in (-1,0,1) if 0 <= label+offset < count}

    def allowed(self, point, labels):
        return region(self.source,*point)&127 in labels

    def anchor(self, point, labels, row, node):
        if self.clear(point,point) and self.allowed(point,labels):
            return point
        candidates = []
        margin = _ROUTE_SEARCH_MARGIN
        for y in range(math.floor((point[1]-margin)/8),math.floor((point[1]+margin)/8)+1):
            for x in range(math.floor((point[0]-margin)/8),math.floor((point[0]+margin)/8)+1):
                target = ((x+.5)*8,(y+.5)*8)
                distance = math.dist(point,target)
                if distance <= margin and self.allowed(target,labels) and self.clear(target,target):
                    candidates.append((distance,y,x,target))
        if not candidates:
            raise GameplayError(f'course={self.source.course_id} route={row} node={node}: '
                                'no local driving clearance')
        return min(candidates)[3]

    def repair(self, a, b, original_a, original_b, row, edge):
        labels = self.neighbors(self.labels(original_a,original_b))
        def valid_segment(start, end):
            return self.clear(start,end) and self.labels(start,end) <= labels
        direct_labels = self.labels(a,b)
        # Unnumbered raster127 can legitimately lie on an authored segment.
        # Retain that exact segment after physical proof, but never search an
        # unnumbered background to invent a detour onto another bend.
        original_direct = a == original_a and b == original_b
        if valid_segment(a,b) or (original_direct and self.clear(a,b) and
                                 direct_labels-{127} <= labels):
            return [a,b]
        if original_direct and direct_labels-{127} <= labels and self.jump_connector(a,b):
            return [a,b]
        # Long source edges can span a river bend outside a fixed tile halo.
        # Keep the search local to that edge, with an absolute128-unit bound.
        margin = min(128,max(_ROUTE_SEARCH_MARGIN,math.ceil(math.dist(original_a,original_b)/2)))
        left = math.floor((min(original_a[0],original_b[0])-margin)/8)
        right = math.floor((max(original_a[0],original_b[0])+margin)/8)
        top = math.floor((min(original_a[1],original_b[1])-margin)/8)
        bottom = math.floor((max(original_a[1],original_b[1])+margin)/8)
        def center(cell):
            return ((cell[0]+.5)*8,(cell[1]+.5)*8)
        def admitted(cell):
            return (left <= cell[0] <= right and top <= cell[1] <= bottom
                    and self.allowed(center(cell),labels) and self.clear(center(cell),center(cell)))
        costs,parents,queue = {},{},[]
        origin = (math.floor(a[0]/8),math.floor(a[1]/8))
        for y in range(origin[1]-1,origin[1]+2):
            for x in range(origin[0]-1,origin[0]+2):
                cell = (x,y)
                if admitted(cell) and valid_segment(a,center(cell)):
                    cost = math.dist(a,center(cell))
                    costs[cell] = cost
                    parents[cell] = None
                    heapq.heappush(queue,(cost+math.dist(center(cell),b),cost,y,x))
        visited = set()
        goal = None
        while queue and len(visited) < _ROUTE_SEARCH_BUDGET:
            _,cost,y,x = heapq.heappop(queue)
            cell = (x,y)
            if cell in visited or cost != costs[cell]:
                continue
            visited.add(cell)
            here = center(cell)
            if math.dist(here,b) <= 16 and valid_segment(here,b):
                goal = cell
                break
            for dx,dy in ((0,-1),(-1,0),(1,0),(0,1),(-1,-1),(1,-1),(-1,1),(1,1)):
                target = (x+dx,y+dy)
                if target in visited or not admitted(target) or not valid_segment(here,center(target)):
                    continue
                distance = cost+math.hypot(dx,dy)*8
                if distance < costs.get(target,math.inf):
                    costs[target] = distance
                    parents[target] = cell
                    heapq.heappush(queue,(distance+math.dist(center(target),b),distance,target[1],target[0]))
        if goal is None and original_direct and direct_labels-{127} <= labels and \
                self.jump_connector(a,b,panel_credit=True):
            # Only a jump otherwise unreachable at the ordinary floor relies on
            # its approach panel, so proved routes keep their exact detours.
            return [a,b]
        if goal is None:
            raise GameplayError(f'course={self.source.course_id} route={row} edge={edge}: '
                                'local clearance path unavailable or search budget exhausted')
        path = [b]
        while goal is not None:
            path.append(center(goal))
            goal = parents[goal]
        path.append(a)
        path.reverse()
        # Remove only inserted detour points. Original projected anchors remain
        # in order, and every retained connection has the same clearance proof.
        compact = [a]
        index = 0
        while index < len(path)-1:
            following = len(path)-1
            while following > index+1 and not valid_segment(path[index],path[following]):
                following -= 1
            compact.append(path[following])
            index = following
        return compact


def _adapt_route(source, points, row, scale):
    clearance = _RouteClearance(source,scale)
    anchors = []
    for i,point in enumerate(points):
        labels = clearance.neighbors({region(source,*point)&127})
        if not labels:
            labels = clearance.neighbors({region(source,*points[i-1])&127,
                region(source,*points[(i+1)%len(points)])&127})
        anchors.append(clearance.anchor(point,labels,row,i))
    result = []
    for i,a in enumerate(anchors):
        b = anchors[(i+1)%len(anchors)]
        path = clearance.repair(a,b,points[i],points[(i+1)%len(points)],row,i)
        for point in path[:-1]:
            if not result or result[-1] != point:
                result.append(point)
        if len(result) > _ROUTE_NODE_LIMIT:
            raise GameplayError(f'course={source.course_id} route={row}: clearance node budget exhausted')
    if len(result)>1 and result[0] == result[-1]:
        result.pop()
    return result


def _adapt_rows(source, scale, *, unreachable_items=None):
    if source.arena:
        plan = course_3d_arena.patrol(source,_RouteClearance(source,scale))
        if unreachable_items is not None:
            unreachable_items.extend(plan.unreachable_items)
        return [plan.points]
    rows = []
    for row_index,row in enumerate(source.route_rows):
        points = []
        for point in row:
            value = (point.x,point.y)
            if not points or value != points[-1]:
                points.append(value)
        if len(points)>1 and points[-1] == points[0]:
            points.pop()
        if len(points)>=2:
            rows.append(_adapt_route(source,points,row_index,scale))
    if not rows:
        raise GameplayError(f'course={source.course_id} routes: no nonzero closed AI route')
    return rows


_CPU_LOOKAHEAD = 1.5


def _forced_arrival_radii(source, points, scale, profile, guides=(), jump_limits=None):
    """Body-safe arrival disks within automatic panel/launch reach."""
    radii=[.8]*len(points)
    if profile is None:
        return radii
    def fail(detail):
        raise GameplayError(f'course={source.course_id} forced arrival: {detail}')
    def field(key,maximum):
        value=profile.get(key)
        if type(value) is not int or not 0<=value<=maximum:
            fail(f'invalid profile {key}')
        return value
    ticks=field('panel_boost_ticks',8192)
    forward=field('forward_speed_q16',256*65536)/65536
    ratio=field('panel_cap_ratio_q16',4*65536)/65536
    radius=field('kart_radius_q16',64*65536)/65536
    if not (0<forward*ratio<=256 and radius>0 and 0<scale and math.isfinite(scale)
            and 2<=len(points)<=_ROUTE_NODE_LIMIT):
        fail('invalid bounded route/profile')
    # Latest panel contact refreshes its timer. One contact-step allowance and
    # the emitted panel immediate horizontal impulse last at most one extra step
    # before the ordinary cap is applied again. No source pixel speeds enter.
    reach=((ticks+1)*forward*ratio+course_3d_format.SOURCE_PANEL_BOOST)*(4389/262144)
    lengths=[math.dist(a,points[(i+1)%len(points)])*scale for i,a in enumerate(points)]
    if any(not math.isfinite(v) or v<=0 for v in lengths):
        fail('invalid route edge')
    work=0
    def spend():
        nonlocal work
        work+=1
        if work>4194304:
            fail('work budget exhausted')
    body=max(radius,_ROUTE_CLEARANCE*scale)
    # RTND JSON positions round to Q16; terrain vertices first pass through
    # glTF float32 and then Q16. Two coordinate-wise float32 epsilon bounds
    # plus four Q16 LSBs cover node/vertex distance error, including near the
    # format's4096m domain. Downward radius quantization alone is insufficient.
    world_extent=max(source.map_width*8,source.map_height*8,
                     *(abs(v) for point in points for v in point))*scale
    encoding_slack=2*world_extent*(2**-23)+4/65536
    neighborhood=math.ceil(body/(8*scale))
    reachable=set()
    for edge,a in enumerate(points):
        b=points[(edge+1)%len(points)]
        contacts=[]
        seen=set()
        for cell,lo,hi in course_3d_launch._intervals(a,b):
            spend()
            for y in range(max(0,cell[1]-neighborhood),
                           min(source.map_height-1,cell[1]+neighborhood)+1):
                for x in range(max(0,cell[0]-neighborhood),
                               min(source.map_width-1,cell[0]+neighborhood)+1):
                    spend()
                    if (x,y) in seen:
                        continue
                    seen.add((x,y))
                    code=material(source,(x+.5)*8,(y+.5)*8)
                    kind=code&15
                    if code<0x20 and (kind in (0,2,14) or kind==6 and ticks):
                        # Conservative body overlap also includes flat-support
                        # edge ownership and last-contact rounding. It only
                        # enlarges the set whose safe disks must be checked.
                        r=body/scale
                        span=course_3d_launch._span(a,b,x*8-r,y*8-r,
                                                   (x+1)*8+r,(y+1)*8+r)
                        if span:
                            contact_reach=reach
                            if kind in (0,2,14):
                                # Widening is optional: without a proved finite
                                # emitted flight domain retain the source disk,
                                # rather than changing the entire connected lap.
                                if jump_limits is None or (x,y) not in jump_limits:
                                    continue
                                contact_reach=jump_limits[x,y]
                                if (type(contact_reach) not in (int,float) or
                                        not math.isfinite(contact_reach) or
                                        not 0<contact_reach<=1024):
                                    fail('invalid emitted jump distance')
                            segment=tuple(tuple(a[k]+(b[k]-a[k])*t
                                for k in range(2)) for t in span)
                            contacts.append((*segment,contact_reach))
        if not contacts:
            continue
        node=(edge+1)%len(points)
        # A physical kart can cut a route corner. Polyline distance is an
        # upper bound on its travel, so it cannot exclude forced travel. Keep
        # the connected cycle walk, but admit by distance to any body-overlap
        # contact segment; even a later node can return spatially near it.
        for _ in range(len(points)):
            spend()
            for start,end,contact_reach in contacts:
                spend()
                distance=math.sqrt(_point_segment_distance_squared(points[node],start,end))*scale
                # The controller may arrive at the edge of the widest allowed
                # disk without reaching its center before the timer expires.
                if distance<=contact_reach+_CPU_LOOKAHEAD+encoding_slack:
                    reachable.add(node)
                    break
            node=(node+1)%len(points)
    guides=set(guides)
    for node in sorted(reachable):
        p=points[node]
        if p in guides:
            continue
        # Radius cannot swallow neighboring corners or an unrelated nearby
        # segment. Actual progression still requires the controller's existing
        # forward/connected-edge test; CPNT authority is entirely independent.
        maximum=min(_CPU_LOOKAHEAD,lengths[node-1]/4-encoding_slack,
                    lengths[node]/4-encoding_slack)
        if maximum<=.8:
            continue
        for edge,a in enumerate(points):
            spend()
            if edge in ((node-1)%len(points),node):
                continue
            maximum=min(maximum,math.sqrt(_point_segment_distance_squared(
                p,a,points[(edge+1)%len(points)]))*scale/2-encoding_slack)
        if maximum<=.8:
            continue
        extent=(maximum+body+encoding_slack)/scale
        for y in range(math.floor((p[1]-extent)/8),math.floor((p[1]+extent)/8)+1):
            for x in range(math.floor((p[0]-extent)/8),math.floor((p[0]+extent)/8)+1):
                spend()
                code=material(source,(x+.5)*8,(y+.5)*8)
                # Only ordinary floor supports a wider arrival disk. Launch,
                # panel, recovery and wall boundaries retain narrow targets.
                if 0x40<=code<0x80 and code!=0x5a:
                    continue
                dx=max(x*8-p[0],p[0]-(x+1)*8,0)
                dy=max(y*8-p[1],p[1]-(y+1)*8,0)
                maximum=min(maximum,math.hypot(dx,dy)*scale-body-encoding_slack)
        if maximum>.8:
            encoded=math.floor(maximum*65536)
            # The ordinary JSON .8 baseline rounds to52429, not52428.
            # Flooring a barely larger real value must never shrink that
            # encoded target, nor claim an enlargement with identical bytes.
            if encoded>round(.8*65536):
                radii[node]=encoded/65536
    return radii


def routes(source, scale, *, adapted=None, unreachable_items=(), route_profile=None, guide_rows=None,
           jump_limits=None):
    notices = list(unreachable_items)
    if adapted is None:
        adapted = _adapt_rows(source,scale,unreachable_items=notices)
    result = []
    for row,points in enumerate(adapted):
        radii=_forced_arrival_radii(source,points,scale,route_profile,
            () if guide_rows is None else guide_rows[row],jump_limits)
        result.append(dict(kind='cpu',closed=True,nodes=[dict(
            position=position(*point,scale),radius=radii[i],speed=16,
            successors=[(i+1)%len(points)]) for i,point in enumerate(points)],properties=[
                {'key':'mksc:route-pilot-continuous','type':'boolean','value':True},
                {'key':'mksc:cpu-ground-route','type':'boolean','value':True},
                {'key':'mksc:cpu-lookahead','type':'fixed','value':_CPU_LOOKAHEAD},
                {'key':'mksc:cpu-corner-speed','type':'fixed','value':5}]))
    if notices:
        result[0]['properties'].append({'key':'source:patrol-unreachable-items','type':'string',
                                        'value':','.join(map(str,notices))})
    item = dict(result[0],kind='item',properties=[],
                nodes=[dict(node,radius=.8) for node in result[0]['nodes']])
    result.append(item)
    return result


def derive(source, *, scale=.1, physics_profile=1, item_profile=None, jump_limits=None, maximum_speed=36,
           route_profile=None, progress_provenance=False):
    """Derive editable gameplay; opt in to stage-to-original-raw META labels.

    Default documents and the (document, collision_regions) return are unchanged.
    Arenas have no ordered stages and therefore emit no provenance properties.
    """
    if not isinstance(progress_provenance,bool):
        raise GameplayError(f'course={source.course_id} progress provenance: expected boolean')
    unreachable_items = []
    adapted = _adapt_rows(source,scale,unreachable_items=unreachable_items)
    # Preserve proven continuous flights before considering nominal chained
    # takeoffs. New guide points must not change their proof inputs or caps.
    original_limits=(course_3d_launch.plan(source,adapted,scale,maximum_speed=maximum_speed)
                     if jump_limits is not None else {})
    chain_limits={}
    retained=adapted
    if route_profile is not None and not source.arena:
        authored=routes(source,scale,adapted=adapted)
        adapted,chain_limits=course_3d_launch.repair_chains(source,adapted,scale,
            _RouteClearance(source,scale),authored,route_profile,existing_caps=original_limits)
        if chain_limits and jump_limits is None:
            raise GameplayError(f'course={source.course_id} chained launch: distance-cap output required')
    if jump_limits is not None:
        jump_limits.update(original_limits)
        jump_limits.update(chain_limits)
    progress_raw = []
    gate_options = {'progress_raw':progress_raw} if progress_provenance else {}
    checkpoint,respawns,mapping = (([],[],{}) if source.arena else
                                  gates(source,scale,canonical=adapted[0],adapted=adapted,
                                        **gate_options))
    metadata = dict(name=source.name,author='MKSC Neo converter',
                    cup_name=f'Converted cup {source.catalog_id}:{source.cup}',
                    modes=['battle'] if source.arena else ['gp','tt','quick','vs'],
                    laps=0 if source.arena else source.rules.laps,
                    cup_id=source.cup_id,cup_order=source.slot,
                    sort_key=(source.catalog_id<<16)|(source.cup<<8)|source.slot,
                    physics_profile=physics_profile,properties=[
                        {'key':'source:course','type':'integer','value':source.course_id},
                        {'key':'source:metres-per-unit','type':'fixed','value':scale}])
    if progress_raw:
        metadata['properties'].append({'key':'source:progress-stage-count',
                                       'type':'integer','value':len(progress_raw)})
        metadata['properties'].extend(
            {'key':f'source:progress-raw:{stage}','type':'integer','value':raw}
            for stage,raw in enumerate(progress_raw))
    if source.song is not None:
        metadata['music'] = {'kind':16,'id':source.song}
    if item_profile is not None:
        metadata['properties'].append({'key':'mksc:item-rules','type':'asset',
                                       'value':{'kind':103,'id':item_profile}})
    grids = [dict(position=position(p.x_q16/65536,p.y_q16/65536,scale,.2),
                  quaternion=orientation(math.sin(p.heading*math.tau/65536),
                                         -math.cos(p.heading*math.tau/65536))) for p in source.grid]
    document = dict(version=1,id=source.course_id,scene='scene.gltf',render_nodes=[],
                    collision_nodes=[],textures=[],meta=metadata,surfaces=[],checkpoints=checkpoint,
                    routes=routes(source,scale,adapted=adapted,unreachable_items=unreachable_items,
                        route_profile=route_profile,jump_limits=jump_limits,
                        guide_rows=[set(row)-set(original) for row,original in zip(adapted,retained)]),grid=grids,items=[dict(
                        position=position(p.x_q16/65536,p.y_q16/65536,scale,1),
                        radius=max(.5,p.half_extent*scale),respawn_ticks=180) for p in source.item_boxes],
                    objects=[],respawns=respawns,cameras=[dict(
                        position=position(source.rules.start_x_q16/65536+30,
                                          source.rules.start_y_q16/65536+30,scale,12),
                        near=.1,far=1024,fov=10923,priority=1)])
    if source.arena:
        document['respawns'] = [dict(position=grid['position'],quaternion=grid['quaternion'],
                                     checkpoint=None,radius=8,route=0) for grid in grids]
    regions = tuple(mapping.get(region(source,(x+.5)*8,(y+.5)*8)&0x7f,0)
                    for y in range(source.map_height) for x in range(source.map_width))
    return document,regions
