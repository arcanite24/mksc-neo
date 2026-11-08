"""Conservative distance caps for visited, straight source launch approaches.

This is converter planning, not source-unit physics. plan() covers flat geometry
at default gravity and its explicit speed envelope (default16..36m/s; an otherwise
unproved approach that crosses a boost panel first adds its immediate entry impulse).
repair_chains() instead uses authored CPU-route/profile speeds, excluding optional
boost overspeed. Its aligned-takeoff geometry is a conditional feasibility plan,
not a proof of closed-loop controller tracking. If alignment is unproved, the
optional fallback proves retained incoming tangents without changing any anchor.
Aligned edits publish atomically; retained-ray caps publish per component only
when all its approaches succeed. Both attempts allow at most1,048,576 counted
proof steps; actual race acceptance is separate.
Unproved single, curved or unvisited strips keep their authored launch unchanged.
"""
import math

from course_3d_format import DEFAULT_GRAVITY, DEFAULT_GROUND_SNAP_Q16, SOURCE_PANEL_BOOST, source_launch


class LaunchError(ValueError):
    pass


_DT = 4389/262144
_G = DEFAULT_GRAVITY
_LOW = 16.0
_RIDE, _SNAP = .35, DEFAULT_GROUND_SNAP_Q16/65536
_MAX_NODES = 4096
_MAX_ROWS = 4


def _span(a,b,left,top,right,bottom):
    low,high=0.0,1.0
    for start,end,minimum,maximum in ((a[0],b[0],left,right),(a[1],b[1],top,bottom)):
        delta=end-start
        if delta==0:
            if start<minimum or start>maximum:
                return None
        else:
            near,far=sorted(((minimum-start)/delta,(maximum-start)/delta))
            low,high=max(low,near),min(high,far)
    return (low,high) if low<high else None


def _intervals(a,b):
    """Exact positive-length8-unit grid intervals, half-open cell convention."""
    cuts={0.0,1.0}
    for start,end in zip(a,b):
        if start==end:
            continue
        for boundary in range(math.floor(min(start,end)/8)+1,math.ceil(max(start,end)/8)):
            t=(boundary*8-start)/(end-start)
            if 0<t<1:
                cuts.add(t)
    values=sorted(cuts)
    for lo,hi in zip(values,values[1:]):
        mid=(lo+hi)/2
        yield (math.floor((a[0]+(b[0]-a[0])*mid)/8),
               math.floor((a[1]+(b[1]-a[1])*mid)/8)),lo,hi


def _tube_exit_fraction(previous, following, radius):
    """Conservative segment fraction inside a lateral tube, including sign flips."""
    return (radius-abs(previous))/(abs(following)-abs(previous))


def plan(source, adapted_rows, scale, *, maximum_speed=36.0):
    """Return {(tile_x,tile_y): maximum_distance_metres} per proved component.

    One Q16-quantized constant is shared by every cell/visited approach in a
    same-code four-neighbor strip. No source object or route is mutated.
    """
    def fail(detail):
        raise LaunchError(f'course={source.course_id} launch planner: {detail}')
    width,height=source.map_width,source.map_height
    if (type(width) is not int or type(height) is not int or not 0<width<=256 or not 0<height<=256
            or isinstance(scale,bool) or not isinstance(scale,(float,int))
            or not math.isfinite(scale) or scale<=0 or max(width,height)*8*scale>4096
            or len(source.tilemap)!=width*height
            or len(adapted_rows)>_MAX_ROWS
            or isinstance(maximum_speed,bool) or not isinstance(maximum_speed,(float,int))
            or not math.isfinite(maximum_speed) or not _LOW<=maximum_speed<=256):
        fail('invalid bounded source/scale/routes')
    codes=[]
    for index in source.tilemap:
        if index>=len(source.materials):
            fail('invalid material reference')
        codes.append(source.materials[index])
    def code(cell):
        x,y=cell
        return codes[y*width+x] if 0<=x<width and 0<=y<height else 0x26
    def launch(value):
        return value<0x20 and value&15 in (0,2,14)
    components=[]
    owners={}
    for y in range(height):
        for x in range(width):
            cell=x,y
            if cell in owners or not launch(code(cell)):
                continue
            owner=len(components)
            cells=[cell]
            owners[cell]=owner
            for cx,cy in cells:
                for other in ((cx-1,cy),(cx+1,cy),(cx,cy-1),(cx,cy+1)):
                    if other not in owners and code(other)==code(cell):
                        owners[other]=owner
                        cells.append(other)
            components.append(cells)
    approaches=[[] for _ in components]
    boosted=[[] for _ in components]
    visit_count=0
    work=0
    def spend():
        nonlocal work
        work+=1
        if work>1048576:
            fail('isolated approach work budget exhausted')
    def neighboring_launch(points,index,entry,owner):
        # Leave connected launch chains to their existing multi-flight owner.
        # A new isolated cap must not suppress its aligned/continuous repair.
        impulse=source_launch(code(components[owner][0])&15)
        for reverse in (False,True):
            # An earlier stronger strip can still own this approach even if
            # the current low-impulse launch cannot reach it in reverse.
            remaining=2*(source_launch(0) if reverse else impulse)*maximum_speed/_G/scale
            previous=entry
            anchors=[entry]
            for step in range(len(points)):
                spend()
                following=points[(index-step if reverse else index+step+1)%len(points)]
                length=math.dist(previous,following)
                if length:
                    end=following if length<=remaining else tuple(
                        previous[k]+(following[k]-previous[k])*remaining/length for k in range(2))
                    for cell,lo,_ in _intervals(previous,end):
                        spend()
                        other=owners.get(cell)
                        if other is not None and other!=owner:
                            target=tuple(previous[k]+(end[k]-previous[k])*lo for k in range(2))
                            dx,dy=target[0]-entry[0],target[1]-entry[1]
                            square=dx*dx+dy*dy
                            axis=0 if abs(dx)>abs(dy) else 1
                            sign=1 if target[axis]>entry[axis] else -1
                            last=0.
                            last_axis=entry[axis]*sign
                            lateral=[entry[1-axis],target[1-axis]]
                            chord=aligned=True
                            for point in anchors+[target]:
                                spend()
                                px,py=point[0]-entry[0],point[1]-entry[1]
                                q=(px*dx+py*dy)/square if square else -1
                                if (q<last or q>1 or
                                        (px*dy-py*dx)**2>square*(.8/scale)**2):
                                    chord=False
                                along=point[axis]*sign
                                if along<last_axis or along>target[axis]*sign:
                                    aligned=False
                                lateral.append(point[1-axis])
                                last_axis=along
                                last=q
                            # Common-axis alignment can own a1.6m-wide tube
                            # even when the entry chord itself exits .8m.
                            if chord or aligned and max(lateral)-min(lateral)<=1.6/scale:
                                return True
                            break
                    remaining-=length
                    if remaining<=0:
                        break
                previous=following
                anchors.append(following)
        return False
    for row in adapted_rows:
        if len(row)>_MAX_NODES:
            fail('route exceeds4096-node bound')
        points=[]
        for p in row:
            if (len(p)!=2 or any(isinstance(v,bool) or not isinstance(v,(int,float))
                    or not math.isfinite(v) for v in p)
                    or not 0<=p[0]<width*8 or not 0<=p[1]<height*8):
                fail('invalid route coordinate')
            p=tuple(p)
            if not points or p!=points[-1]:
                points.append(p)
        if len(points)>1 and points[-1]==points[0]:
            points.pop()
        if len(points)<3:
            continue
        for i,a in enumerate(points):
            b=points[(i+1)%len(points)]
            previous=points[i-1]
            preceding=list(_intervals(previous,a))
            old_owner=owners.get(preceding[-1][0]) if preceding else None
            for cell,lo,hi in _intervals(a,b):
                spend()
                owner=owners.get(cell)
                entering=owner is not None and owner!=old_owner
                old_owner=owner
                if not entering:
                    continue
                entry=(a[0]+(b[0]-a[0])*lo,a[1]+(b[1]-a[1])*lo)
                direction=((b[0]-a[0])/math.dist(a,b),(b[1]-a[1])/math.dist(a,b))
                end=b
                proved=False
                for offset in range(1,len(points)):
                    spend()
                    following=points[(i+offset+1)%len(points)]
                    dx,dy=following[0]-end[0],following[1]-end[1]
                    length=math.hypot(dx,dy)
                    cosine=max(-1,min(1,(dx*direction[0]+dy*direction[1])/length))
                    angle=math.acos(cosine)
                    if angle>=math.radians(15):
                        proved=True
                        break
                    if angle>1e-7:
                        break
                    end=following
                if not proved and not neighboring_launch(points,i,entry,owner):
                    # Retain the incoming horizontal ray; a slightly curved
                    # route does not make airborne velocity follow its nodes.
                    # Bound its first .8m tube exit, rather than extending the
                    # flight to a far outside anchor. Existing straight proofs
                    # above keep their original endpoint and output unchanged.
                    reserve=2*max(width,height)*8*(2**-23)+4/(65536*scale)
                    tube=.8/scale-reserve
                    last=deviation=0.
                    for offset in range(len(points)):
                        spend()
                        anchor=points[(i+offset+1)%len(points)]
                        dx,dy=anchor[0]-entry[0],anchor[1]-entry[1]
                        q=dx*direction[0]+dy*direction[1]
                        outside=abs(dx*direction[1]-dy*direction[0])
                        if q<=last or outside>tube:
                            limit=(last if q<=last else
                                   last+(q-last)*_tube_exit_fraction(deviation,outside,tube))
                            if limit>0:
                                end=tuple(entry[k]+direction[k]*limit for k in range(2))
                                proved=True
                            break
                        last,deviation=q,outside
                visit_count+=1
                if visit_count>16384:
                    fail('launch approach budget exhausted')
                approaches[owner].append((entry,end) if proved else None)
                boosted[owner].append(entry!=a and any(code(cell)<0x20 and code(cell)&15==6
                                                       for cell,_,_ in _intervals(a,entry)))
    result={}
    for owner,cells in enumerate(components):
        visits=approaches[owner]
        if not visits or any(visit is None for visit in visits):
            continue
        impulse=source_launch(code(cells[0])&15)
        # At least one approach must actually need a distance ceiling.
        if all(2*impulse*maximum_speed/_G <= math.dist(*visit)*scale-.5 for visit in visits):
            continue
        upper=min(math.dist(*visit)*scale-.5-maximum_speed*_DT for visit in visits)
        if upper<=0:
            continue
        contexts=[]
        for a,b in visits:
            distance=math.dist(a,b)*scale
            strip_end=0.0
            left_strip=False
            unsupported=False
            for cell,lo,hi in _intervals(a,b):
                strip=owners.get(cell)
                if strip==owner:
                    if left_strip:
                        unsupported=True
                    strip_end=hi*distance
                else:
                    left_strip=True
                    if strip is not None:
                        unsupported=True
            if unsupported:
                contexts=[]
                break
            obstacles=[]
            walls=[]
            for y in range(max(0,math.floor((min(a[1],b[1])-3)/8)),
                           min(height-1,math.floor((max(a[1],b[1])+3)/8))+1):
                for x in range(max(0,math.floor((min(a[0],b[0])-3)/8)),
                               min(width-1,math.floor((max(a[0],b[0])+3)/8))+1):
                    value=code((x,y))
                    if not (0x20<=value<0x30 or 0x80<=value<0x90):
                        continue
                    wall=0x80<=value<0x90
                    expansion=3 if wall else 0
                    span=_span(a,b,x*8-expansion,y*8-expansion,(x+1)*8+expansion,(y+1)*8+expansion)
                    if span:
                        obstacles.append((wall,span[0]*distance,span[1]*distance))
                    if wall:
                        exact=_span(a,b,x*8,y*8,(x+1)*8,(y+1)*8)
                        if exact:
                            walls.append((exact[0]*distance,exact[1]*distance))
            # Require an actual obstacle crossing and nonzero drivable landing
            # corridor beyond its last expanded collision interval.
            if not obstacles or strip_end>max(end for _,_,end in obstacles) or max(end for _,_,end in obstacles)>=distance-.5:
                contexts=[]
                break
            last=max(end for _,_,end in obstacles)
            cuts=sorted({0.0,last}|{max(0,min(last,v)) for pair in walls for v in pair})
            ground=[(a,b) for a,b in zip(cuts,cuts[1:])
                    if not any(lo<=(a+b)/2<=hi for lo,hi in walls)]
            contexts.append((obstacles,ground))
        if not contexts:
            continue
        def safe(distance, slowest):
            speeds=[slowest,maximum_speed]
            crossover=_G*distance/(2*impulse)
            if slowest<crossover<maximum_speed:
                speeds.append(crossover)
            for speed in speeds:
                velocity=min(impulse,_G*distance/(2*speed))
                def height_at(x,delay):
                    t=x/speed-delay
                    # Semi-implicit Euler is below the continuous parabola by
                    # g*dt*t/2; late initial contact adds at most one tick.
                    # Use the slowest admitted travel time for a common error
                    # bound across the entire speed interval; speed-dependent
                    # ceil() discontinuities cannot escape endpoint checks.
                    rounding_time=x/slowest+_DT
                    steps=math.ceil(rounding_time/_DT)+1
                    # One position LSB per step, accumulated velocity-step
                    # truncation, four initial cap-operation LSBs, and one
                    # gravity/coefficient LSB. All bounds are in metres.
                    rounding=(steps+steps*(steps+1)*_DT/2+4*rounding_time+rounding_time**2+2)/65536
                    # Sweeps interpolate the two discrete positions linearly;
                    # the chord lies up to g*dt²/8 below this parabola.
                    return t,velocity*t-_G*t*t/2-_G*_DT*t/2-_G*_DT*_DT/8-rounding
                for obstacles,ground in contexts:
                    for wall,lo,hi in obstacles:
                        required=max(0,4*scale-_RIDE) if wall else 0
                        for x in (lo,hi):
                            for delay in (0,_DT):
                                t,h=height_at(x,delay)
                                if t<=0 or h<=required:
                                    return False
                                if not wall and t>=velocity/_G and h<=_SNAP:
                                    return False
                    # Wall tops are not support. Ordinary ground between/after
                    # walls is: prevent an early descending snap before the
                    # final expanded obstacle has been cleared.
                    for lo,hi in ground:
                        for x in (lo,hi):
                            for delay in (0,_DT):
                                t,h=height_at(x,delay)
                                if t>=velocity/_G and h<=_SNAP:
                                    return False
            return True
        high=math.floor(upper*65536)
        # Credit a crossed panel only where the ordinary envelope is unproved,
        # so every other strip keeps its exact cap.
        floors=[_LOW]+([_LOW+SOURCE_PANEL_BOOST] if all(boosted[owner]) else [])
        slowest=next((floor for floor in floors if safe(high/65536,floor)),None)
        if slowest is None:
            continue
        low=0
        while low<high:
            middle=(low+high)//2
            if safe(middle/65536,slowest):
                high=middle
            else:
                low=middle+1
        for cell in cells:
            result[cell]=high/65536
    return result


class _NotPlanned(ValueError):
    """Valid source outside this optional aligned/grounded flight proof."""


def repair_chains(source, rows, scale, clearance, route_documents, profile, *, existing_caps=None):
    """Return an optional repair; unproved flights stay authored.

    Aligned route edits publish atomically. The unchanged-ray fallback can
    publish independent components, but every approach of one component must
    succeed. Not planning a source is not a gameplay safety claim. Malformed inputs and
    exhausted resource budgets still raise named LaunchError diagnostics.
    """
    try:
        return _repair_chains(source,rows,scale,clearance,route_documents,profile,
                              existing_caps=existing_caps)
    except _NotPlanned:
        try:
            return _repair_chains(source,rows,scale,clearance,route_documents,profile,
                                  existing_caps=existing_caps,directed=True)
        except _NotPlanned:
            return rows,{}


def _repair_chains(source, rows, scale, clearance, route_documents, profile, *, existing_caps=None, directed=False):
    """Align/prove successive grounded jumps at authored CPU speeds.

    This proof excludes optional item/panel overspeed. It preserves every route
    anchor and order; only clear, straight takeoff lead-ins are inserted. The
    returned caps use the existing SURF distance property, with no new physics.
    """
    def fail(detail):
        raise LaunchError(f'course={source.course_id} chained launch: {detail}')
    if profile is None or getattr(source,'arena',False):
        return rows,{}
    width,height=source.map_width,source.map_height
    if not (0<width<=256 and 0<height<=256 and len(source.tilemap)==width*height
            and 0<scale and math.isfinite(scale) and len(rows)<=_MAX_ROWS):
        fail('invalid bounded source')
    def material(cell):
        x,y=cell
        if not 0<=x<width or not 0<=y<height:
            return 0x26
        index=source.tilemap[y*width+x]
        if index>=len(source.materials):
            fail('invalid material reference')
        return source.materials[index]
    def is_launch(code):
        return code<0x20 and code&15 in (0,2,14)
    owners,components={},[]
    for y in range(height):
        for x in range(width):
            cell=x,y
            if cell in owners or not is_launch(material(cell)):
                continue
            owner=len(components)
            cells=[cell]
            owners[cell]=owner
            for a,b in cells:
                for other in ((a-1,b),(a+1,b),(a,b-1),(a,b+1)):
                    if other not in owners and material(other)==material(cell):
                        owners[other]=owner
                        cells.append(other)
            components.append(cells)
    cpu=[r for r in route_documents if r['kind']=='cpu']
    if len(cpu)!=len(rows):
        fail('route speed declarations do not match geometry')
    def scalar(name,zero=False):
        value=profile.get(name)
        if type(value) is not int or not (0 if zero else 1)<=value<=67108864:
            fail(f'invalid profile {name}')
        return value/65536
    acceleration=scalar('acceleration_limit_q16')
    braking=max(scalar('brake_deceleration_q16',True),scalar('coast_deceleration_q16',True))
    scalar('handling_response_q16')
    scalar('handling_turn_rate_q16')
    cap=scalar('forward_speed_q16')
    # The ballistic formulas intentionally describe the emitted flat converter
    # geometry. Other valid profiles require a different proof, not silent use
    # of these constants. One LSB admits the authored .35m Q16 rounding.
    if (abs(scalar('ride_height_q16')-_RIDE)>1/65536 or
            scalar('ground_snap_q16')!=_SNAP or
            scalar('terminal_fall_speed_q16')<source_launch(0) or
            scalar('kart_radius_q16')>3*scale):
        fail('profile outside flat ballistic ride/snap/radius/terminal assumptions')
    # Same half-m/s hysteresis as cpu_input, plus one authored acceleration or
    # braking step. This is a nominal route envelope, never boosted top speed.
    visit_rows=[]
    for row_index,points in enumerate(rows):
        if len(points)>_MAX_NODES:
            fail('route node budget exhausted')
        visits=[]
        for edge,a in enumerate(points):
            b=points[(edge+1)%len(points)]
            if a==b:
                fail('zero route edge')
            previous=owners.get(next(reversed(list(_intervals(points[edge-1],a))))[0])
            for cell,lo,hi in _intervals(a,b):
                owner=owners.get(cell)
                if owner is not None and owner!=previous:
                    point=tuple(a[k]+(b[k]-a[k])*lo for k in range(2))
                    axis=0 if abs(b[0]-a[0])>abs(b[1]-a[1]) else 1
                    sign=1 if b[axis]>a[axis] else -1
                    visits.append(dict(owner=owner,edge=edge,point=point,axis=axis,sign=sign))
                    if len(visits)>16384:
                        fail('launch visit budget exhausted')
                previous=owner
        visit_rows.append(visits)
    existing_caps={} if existing_caps is None else existing_caps
    proved={owner for owner,cells in enumerate(components) if all(cell in existing_caps for cell in cells)}
    contexts={}
    takeoffs={}
    handled=set()
    declined=set()
    additions=[{} for _ in rows]
    work=0
    classified=[]
    curved=set()
    for row_index,(points,visits,document) in enumerate(zip(rows,visit_rows,cpu)):
        props={p['key']:p['value'] for p in document.get('properties',[])}
        corner=props.get('mksc:cpu-corner-speed',3)
        lookahead=props.get('mksc:cpu-lookahead',2)
        def finite(value,minimum,maximum):
            return type(value) in (int,float) and math.isfinite(value) and minimum<=value<=maximum
        if not finite(corner,3277/65536,256) or not finite(lookahead,3277/65536,64):
            fail(f'route={row_index} invalid corner speed or lookahead')
        if not document['nodes'] or any(not finite(n['speed'],0,32767+65535/65536)
                                         for n in document['nodes']):
            fail(f'route={row_index} invalid node speed')
        speeds=[min(cap,n['speed'] or cap) for n in document['nodes']]
        low=max(.05,min(corner,*speeds)-.5-braking*_DT)
        high=max(speeds)+.5+acceleration*_DT
        if not (0<low<=high<=256 and math.isfinite(high)):
            fail('invalid authored route speed envelope')
        linked=set()
        links=[]
        for i,first in enumerate(visits):
            following=visits[(i+1)%len(visits)]
            axis,sign=first['axis'],first['sign']
            distance=(following['point'][axis]-first['point'][axis])*sign*scale
            if (first['owner']==following['owner'] or axis!=following['axis'] or
                    sign!=following['sign'] or not 0<distance<=2*source_launch(0)*high/_G):
                continue
            # Two nearby strips on different arms of a folded route are not
            # a chain. Retain forward anchor order and its existing radius tube.
            forward=True
            previous_q=first['point'][axis]*sign
            lateral_low=min(first['point'][1-axis],following['point'][1-axis])
            lateral_high=max(first['point'][1-axis],following['point'][1-axis])
            a,b=first['point'],following['point']
            dx,dy=b[0]-a[0],b[1]-a[1]
            length2=dx*dx+dy*dy
            for step in range(1,len(points)+1):
                index=(first['edge']+step)%len(points)
                if index==(following['edge']+1)%len(points):
                    break
                work+=1
                if work>1048576:
                    fail('chain classification budget exhausted')
                p=points[index]
                q=p[axis]*sign
                lateral_low=min(lateral_low,p[1-axis])
                lateral_high=max(lateral_high,p[1-axis])
                t=max(0,min(1,((p[0]-a[0])*dx+(p[1]-a[1])*dy)/length2))
                if q<previous_q or q>following['point'][axis]*sign or \
                        math.hypot(p[0]-a[0]-t*dx,p[1]-a[1]-t*dy)*scale>.8:
                    forward=False
                    break
                previous_q=q
            if forward and any(0x20<=material(cell)<0x30 for cell,_,_ in
                               _intervals(first['point'],following['point'])):
                pair=(i,(i+1)%len(visits))
                linked.update(pair)
                links.append(pair)
                # A diagonal chord tube does not imply a common constant
                # flight axis. Intersect every retained anchor/entry's .8m
                # lateral interval; leave genuinely curved chains authored.
                if (lateral_high-lateral_low)*scale>1.6+1e-9:
                    curved.update((first['owner'],following['owner']))
        classified.append((low,high,lookahead,linked,links))
    # Every published SURF cap must cover all lanes/approaches of its owner.
    # Aligned guide edits remain atomic; directed rays can decline one owner.
    if curved and not directed:
        raise _NotPlanned('no common straight flight axis across connected approaches')
    selected_owners={visit_rows[row][i]['owner']
                     for row,entry in enumerate(classified) for i in entry[3]}
    for row_index,(points,visits) in enumerate(zip(rows,visit_rows)):
        low,high,lookahead,linked,links=classified[row_index]
        if directed:
            # A shared strip cap also affects isolated approaches on another
            # retained lane. Prove those rays too, or decline the whole repair.
            linked=linked|{i for i,v in enumerate(visits) if v['owner'] in selected_owners}
        previous_axis=None
        # A previously proved continuous flight owns its whole connected chain;
        # never force a new grounded relaunch through its intermediate strips.
        preserved={i for i in linked if visits[i]['owner'] in proved}
        for _ in range(len(links)):
            following=set(preserved)
            for a,b in links:
                work+=1
                if work>1048576:
                    fail('preserved chain budget exhausted')
                if a in preserved or b in preserved:
                    following.update((a,b))
            if following==preserved:
                break
            preserved=following
        linked-=preserved
        for i in sorted(linked):
            visit=visits[i]
            axis,sign=visit['axis'],visit['sign']
            lateral=1-axis
            edge=visit['edge']
            entry=visit['point'][axis]*sign
            # The next retained anchor fixes the flight corridor, rather than
            # a new shortest path or an unrelated nearby strip.
            preferred=points[(edge+1)%len(points)][lateral]
            choices=[preferred]
            if previous_axis is not None and previous_axis[:2]==(axis,sign):
                choices.append(previous_axis[2])
            choices.extend(((points[edge][lateral]+preferred)/2,points[edge][lateral]))
            def context_for(fixed):
                nonlocal work
                extra=[]
                def point(q):
                    value=[0.,0.]
                    value[axis]=q*sign
                    value[lateral]=fixed
                    return tuple(value)
                if directed:
                    # Retain the actual authored incoming direction. A shallow
                    # diagonal over several near-collinear anchors is not a
                    # request for a new sideways turn across the previous gap.
                    start=visit['point']
                    a,b=points[edge],points[(edge+1)%len(points)]
                    length=math.hypot(b[0]-a[0],b[1]-a[1])
                    direction=((b[0]-a[0])/length,(b[1]-a[1])/length)
                    lead=tuple(start[k]-direction[k]*8 for k in range(2))
                    contact=tuple(start[k]-direction[k]/(65536*scale) for k in range(2))
                    if not clearance.clear(lead,contact):
                        raise ValueError(f'route={row_index} strip={visit["owner"]} lacks directed ground contact')
                    def projection(p):
                        dx,dy=p[0]-start[0],p[1]-start[1]
                        return dx*direction[0]+dy*direction[1],abs(dx*direction[1]-dy*direction[0])
                    following=visits[(i+1)%len(visits)]
                    next_distance,deviation=projection(following['point'])
                    limit=math.inf
                    # The next required grounded launch still limits flight
                    # when its approach needs later lateral steering. Ignoring
                    # it would accidentally demand one flight across two gaps.
                    if next_distance>0:
                        limit=next_distance-8
                    last=0.
                    last_deviation=0.
                    for step in range(1,len(points)+1):
                        work+=1
                        if work>1048576:
                            fail('directed route proof budget exhausted')
                        q,deviation=projection(points[(edge+step)%len(points)])
                        if q<=last:
                            limit=min(limit,last)
                            break
                        if deviation*scale>.8:
                            # Absolute perpendicular deviation is convex on
                            # the retained segment. Linear interpolation gives
                            # a conservative first tube exit, even when signed
                            # deviations cross zero; never use the far outside
                            # anchor itself as a landing deadline.
                            fraction=_tube_exit_fraction(last_deviation,deviation,.8/scale)
                            limit=min(limit,last+(q-last)*fraction)
                            break
                        last=q
                        last_deviation=deviation
                    if not math.isfinite(limit) or limit<=3:
                        raise ValueError(f'route={row_index} strip={visit["owner"]} has no directed landing corridor')
                    window=(limit-3)*scale
                    finish=tuple(start[k]+direction[k]*window/scale for k in range(2))
                else:
                    following=visits[(i+1)%len(visits)]
                    next_q=following['point'][axis]*sign
                    limit=math.inf
                    if (following['axis']==axis and following['sign']==sign and next_q>entry):
                        limit=next_q
                    # Stay in the retained anchor tube, or prove landing before
                    # an off-axis target's lookahead region. This is conditional
                    # aligned geometry, not a prediction of closed-loop steering.
                    last=entry
                    for step in range(1,len(points)+1):
                        work+=1
                        if work>1048576:
                            fail('route proof budget exhausted')
                        anchor=points[(edge+step)%len(points)]
                        q=anchor[axis]*sign
                        deviation=abs(anchor[lateral]-fixed)
                        if q<=last:
                            limit=min(limit,last)
                            break
                        if deviation*scale>.8:
                            # Renew grounded steering before an off-axis target;
                            # airborne steering cannot bend horizontal velocity.
                            limit=min(limit,q-lookahead/scale)
                            break
                        last=q
                    if not math.isfinite(limit) or limit<=entry:
                        raise ValueError(f'route={row_index} strip={visit["owner"]} has no finite landing corridor')
                    # Reserve a full final tile of grounded travel before another
                    # required strip, plus the existing body/margin allowance.
                    if (following['axis']==axis and following['sign']==sign and next_q>entry):
                        limit=min(limit,next_q-8)
                    end=limit-3
                    start=point(entry)
                    finish=point(end)
                    inside=point(entry+1/(65536*scale))
                    if owners.get((math.floor(inside[0]/8),math.floor(inside[1]/8)))!=visit['owner']:
                        raise ValueError(f'route={row_index} strip={visit["owner"]} aligned entry misses strip')
                    window=(end-entry)*scale
                obstacles=[]
                wall_intervals=[]
                for y in range(max(0,math.floor((min(start[1],finish[1])-3)/8)),
                               min(height-1,math.floor((max(start[1],finish[1])+3)/8))+1):
                    for x in range(max(0,math.floor((min(start[0],finish[0])-3)/8)),
                                   min(width-1,math.floor((max(start[0],finish[0])+3)/8))+1):
                        code=material((x,y))
                        if not (0x20<=code<0x30 or 0x80<=code<0x90):
                            continue
                        wall=0x80<=code<0x90
                        expansion=3 if wall else 0
                        span=_span(start,finish,x*8-expansion,y*8-expansion,
                                   (x+1)*8+expansion,(y+1)*8+expansion)
                        work+=1
                        if work>1048576:
                            fail('terrain proof budget exhausted')
                        if span:
                            length=window
                            obstacles.append((wall,span[0]*length,span[1]*length))
                            if wall:
                                exact=_span(start,finish,x*8,y*8,(x+1)*8,(y+1)*8)
                                if exact:
                                    wall_intervals.append((exact[0]*length,exact[1]*length))
                dry_takeoff=0.
                if not obstacles:
                    # A dry alternate still constrains when the shared cap
                    # must land. Prove full body support along its ray; an
                    # endpoint alone cannot exclude a thin unsupported strip.
                    margin=3+2*max(width,height)*8*(2**-23)+4/(65536*scale)
                    for y in range(math.floor((min(start[1],finish[1])-margin)/8),
                                   math.floor((max(start[1],finish[1])+margin)/8)+1):
                        for x in range(math.floor((min(start[0],finish[0])-margin)/8),
                                       math.floor((max(start[0],finish[0])+margin)/8)+1):
                            work+=1
                            if work>1048576:
                                fail('dry support proof budget exhausted')
                            span=_span(start,finish,x*8-margin,y*8-margin,
                                       (x+1)*8+margin,(y+1)*8+margin)
                            if not span:
                                continue
                            code=material((x,y))
                            if owners.get((x,y))==visit['owner']:
                                dry_takeoff=max(dry_takeoff,span[1]*window)
                            elif not (0x40<=code<0x80 and code!=0x5a):
                                raise ValueError(f'route={row_index} strip={visit["owner"]} lacks ordinary dry support')
                last_obstacle=max((b for _,a,b in obstacles),default=0.)
                if last_obstacle+3*scale>=window or not clearance.clear(finish,finish):
                    raise ValueError(f'route={row_index} strip={visit["owner"]} has no dry landing window')
                cuts=sorted({0.,last_obstacle}|{max(0,min(last_obstacle,v))
                            for pair in wall_intervals for v in pair})
                ground=[(a,b) for a,b in zip(cuts,cuts[1:])
                        if not any(lo<=(a+b)/2<=hi for lo,hi in wall_intervals)]
                if not obstacles:
                    # Landing back onto an automatic launch could retrigger.
                    # Stay airborne until the body has cleared its whole strip.
                    ground=[(0.,dry_takeoff)]
                if directed:
                    return (low,high,obstacles,ground,window),[]
                # Two lookahead spans of clear aligned geometry, conditional
                # on ordinary controller tracking; no response-time/yaw proof
                # is inferred from the input filter's time constant.
                lead=2*lookahead/scale
                lead_q=entry-lead
                guard_q=entry-8
                lead_edge=None
                for q in (lead_q,guard_q):
                    found=None
                    for back in range(len(points)):
                        work+=1
                        if work>1048576:
                            fail('grounded lead-in budget exhausted')
                        index=(edge-back)%len(points)
                        a,b=points[index],points[(index+1)%len(points)]
                        aq,bq=a[axis]*sign,b[axis]*sign
                        if aq<=q<bq:
                            candidate=point(q)
                            prealigned=any(abs(value[1][lateral]-fixed)<1e-6 and value[0]<=q
                                for value in additions[row_index].get(index,[])+
                                [(q0,p0,sign) for index0,q0,p0 in extra if index0==index])
                            if not (clearance.clear(candidate,point(guard_q)) and
                                    (prealigned or abs(a[lateral]-fixed)<1e-6 or clearance.clear(a,candidate))):
                                break
                            if hasattr(clearance,'labels'):
                                allowed=clearance.neighbors(clearance.labels(a,b))|{127}
                                if not clearance.labels(a,candidate)<=allowed:
                                    break
                            found=index,candidate
                            break
                        if back and bq>=entry:
                            break
                    if found is None:
                        raise ValueError(f'route={row_index} strip={visit["owner"]} lacks a clear grounded lead-in')
                    extra.append((found[0],q,found[1]))
                    if q==lead_q:
                        lead_edge=found[0]
                for step in range(1,len(points)+1):
                    work+=1
                    if work>1048576:
                        fail('anchor proof budget exhausted')
                    index=(lead_edge+step)%len(points)
                    anchor=points[index]
                    if index==(edge+1)%len(points):
                        break
                    if abs(anchor[lateral]-fixed)*scale>.8:
                        raise ValueError(f'route={row_index} strip={visit["owner"]} retained anchor bends takeoff')
                return (low,high,obstacles,ground,window),extra
            context=None
            reason='no aligned corridor'
            for fixed in ([None] if directed else dict.fromkeys(choices)):
                try:
                    context,extra=context_for(fixed)
                    break
                except ValueError as error:
                    if isinstance(error,LaunchError):
                        raise
                    reason=str(error)
            if context is None:
                if directed:
                    declined.add(visit['owner'])
                    continue
                raise _NotPlanned(reason)
            contexts.setdefault(visit['owner'],[]).append(context)
            if directed:
                a,b=points[edge],points[(edge+1)%len(points)]
                length=math.dist(a,b)
                direction=tuple((b[k]-a[k])/length for k in range(2))
                start=visit['point']
            else:
                direction=tuple(sign if k==axis else 0 for k in range(2))
                start=tuple(entry*sign if k==axis else fixed for k in range(2))
            takeoffs.setdefault(visit['owner'],[]).append((start,direction,high))
            for index,q,p in extra:
                additions[row_index].setdefault(index,[]).append((q,p,sign))
            previous_axis=(axis,sign,fixed)
            handled.add((row_index,i))
    caps={}
    for owner,proofs in contexts.items():
        if owner in declined:
            continue
        if any(v['owner']==owner and (row,i) not in handled
               for row,visits in enumerate(visit_rows) for i,v in enumerate(visits)):
            if directed:
                continue
            raise _NotPlanned(f'strip={owner} has an unproved alternate approach')
        impulse=source_launch(material(components[owner][0])&15)
        def solve(proofs):
            def safe(distance,landing):
                for low,high,obstacles,ground,window in proofs:
                    speeds=[low,high]
                    crossover=_G*distance/(2*impulse)
                    if low<crossover<high:
                        speeds.append(crossover)
                    for speed in speeds:
                        velocity=min(impulse,_G*distance/(2*speed))
                        def height_at(x,delay,upper=False):
                            t=x/speed-delay
                            steps=math.ceil((x/low+_DT)/_DT)+1
                            bound=(steps+steps*(steps+1)*_DT/2+4*(x/low+_DT)+(x/low+_DT)**2+2)/65536
                            value=velocity*t-_G*t*t/2-_G*_DT*t/2
                            return t,value+(bound if upper else -bound-_G*_DT*_DT/8)
                        for wall,a,b in obstacles:
                            for x in (a,b):
                                for phase in (0,_DT):
                                    t,h=height_at(x,phase)
                                    if t<=0 or h<=(max(0,4*scale-_RIDE) if wall else 0):
                                        return False
                                    if not wall and t+_DT>=velocity/_G and h<=_SNAP:
                                        return False
                        for a,b in ground:
                            for x in (a,b):
                                for phase in (0,_DT):
                                    t,h=height_at(x,phase)
                                    if t+_DT>=velocity/_G and h<=_SNAP:
                                        return False
                        if landing:
                            for phase in (0,_DT):
                                t,h=height_at(window,phase,True)
                                if t<velocity/_G or h>_SNAP:
                                    return False
                return True
            upper=math.ceil(2*impulse*max(p[1] for p in proofs)/_G*65536)
            ceiling=upper
            if not safe(upper/65536,False):
                raise _NotPlanned(f'strip={owner} cannot clear terrain at authored route speeds')
            lower=0
            while lower<upper:
                middle=(lower+upper)//2
                if safe(middle/65536,False):
                    upper=middle
                else:
                    lower=middle+1
            if not any(p[2] for p in proofs):
                # Start beyond the takeoff-strip clearance minimum, then keep
                # the largest landing-feasible cap, never an invalid zero cap.
                lower=max(1,upper)
                if not safe(lower/65536,True):
                    raise _NotPlanned(f'strip={owner} has no positive dry landing cap')
                upper=ceiling
                while lower<upper:
                    middle=(lower+upper+1)//2
                    if safe(middle/65536,True):
                        lower=middle
                    else:
                        upper=middle-1
                return lower/65536
            if not safe(upper/65536,True):
                raise _NotPlanned(f'strip={owner} cannot land before next required jump or retained bend '
                     f'(distance={upper/65536:.6f}, envelope={min(p[0] for p in proofs):.6f}..'
                     f'{max(p[1] for p in proofs):.6f}, window={min(p[4] for p in proofs):.6f})')
            return upper/65536
        try:
            distance=solve(proofs)
            for cell in components[owner]:
                caps[cell]=distance
            continue
        except _NotPlanned:
            pass
        # Only an incompatible whole-strip cap needs partitioning. Body/phase
        # overlap joins approaches, so a straddling kart cannot select a cap
        # that was proved only for another approach. Other cells stay authored.
        staged={}
        try:
            groups=[]
            extent=max(width*8,height*8,*(abs(v) for start,_,_ in takeoffs[owner] for v in start))
            reserve=2*extent*(2**-23)+4/(65536*scale)
            for proof,(start,direction,high) in zip(proofs,takeoffs[owner]):
                step=high*_DT/scale
                before=tuple(start[k]-direction[k]*step for k in range(2))
                after=tuple(start[k]+direction[k]*step for k in range(2))
                contact=set()
                margin=3+reserve
                for cell in components[owner]:
                    work+=1
                    if work>1048576:
                        fail('takeoff partition budget exhausted')
                    x,y=cell
                    if _span(before,after,x*8-margin,y*8-margin,
                             (x+1)*8+margin,(y+1)*8+margin):
                        contact.add(cell)
                if not contact:
                    raise _NotPlanned(f'strip={owner} has no owned takeoff cells')
                selected=[proof]
                remaining=[]
                for cells,group_proofs in groups:
                    work+=1
                    if work>1048576:
                        fail('takeoff grouping budget exhausted')
                    if contact.intersection(cells):
                        contact.update(cells)
                        selected.extend(group_proofs)
                    else:
                        remaining.append((cells,group_proofs))
                groups=remaining+[(contact,selected)]
            # Stage the whole owner: one failed contact group must not leak caps
            # from a previously solved disjoint group of that same component.
            for cells,group_proofs in groups:
                distance=solve(group_proofs)
                for cell in cells:
                    staged[cell]=distance
        except _NotPlanned:
            if not directed:
                raise
            continue
        caps.update(staged)
    if directed:
        return rows,caps
    repaired=[]
    for points,extra in zip(rows,additions):
        result=[]
        for edge,a in enumerate(points):
            result.append(a)
            # Sort by the original edge's dominant-axis parameter, not radial
            # distance: different admissible flight axes can share this edge.
            b=points[(edge+1)%len(points)]
            axis=0 if abs(b[0]-a[0])>abs(b[1]-a[1]) else 1
            def parameter(value):
                return (value[1][axis]-a[axis])/(b[axis]-a[axis])
            for value in sorted(extra.get(edge,[]),key=parameter):
                p=value[1]
                if p==result[-1] or p==b:
                    continue
                if not 0<parameter(value)<1:
                    fail('aligned insertion outside original edge order')
                result.append(p)
        if len(result)>_MAX_NODES:
            fail('aligned route exceeds4096-node bound')
        repaired.append(result)
    return repaired,caps
