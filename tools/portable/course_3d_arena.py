"""Bounded terrain-derived Battle patrols; source RUT rows are not edges."""
from collections import deque
from dataclasses import dataclass
import math


class ArenaError(ValueError):
    pass


@dataclass(frozen=True)
class ArenaPatrol:
    points: tuple
    unreachable_items: tuple


_MAX_CELLS = 256 * 256
_MAX_NODES = 4096
_MAX_EDGES = 16384
_DIRECTIONS = ((0,-1),(-1,0),(1,0),(0,1),(-1,-1),(1,-1),(-1,1),(1,1))


def patrol(source, clearance):
    """Return a closed point list and explicit unreachable ITEM row indices.

    The bounded eight-unit lattice is only a navigation workspace. Every edge
    and shortcut is swept through the caller's body-clearance predicate. GRID
    points must connect without crossing hazards. An ITEM above a void may use
    nearby drivable ground within its authored horizontal pickup half-extent.
    ITEMS are optional CPU goals: inaccessible placements remain source data,
    and their indices are returned for the caller's conversion diagnostics.
    No source checkpoint labels, route ordering or jump assumptions are used.
    """
    def fail(detail):
        raise ArenaError(f'course={source.course_id} arena patrol: {detail}')

    width,height = source.map_width,source.map_height
    if (type(width) is not int or type(height) is not int or not 0 < width <= 256
            or not 0 < height <= 256 or width*height > _MAX_CELLS):
        fail('invalid bounded map dimensions')
    if len(source.grid) != 4 or len(source.item_boxes) > 64:
        fail('expected four GRID starts and at most64 ITEM placements')

    def point(cell):
        return ((cell % width + .5)*8,(cell // width + .5)*8)

    free = [bool(clearance.clear(point(cell),point(cell)))
            for cell in range(width*height)]

    def neighbors(cell):
        x,y = cell % width,cell // width
        a = point(cell)
        for dx,dy in _DIRECTIONS:
            nx,ny = x+dx,y+dy
            if 0 <= nx < width and 0 <= ny < height:
                target = ny*width+nx
                if free[target] and clearance.clear(a,point(target)):
                    yield target

    def location(row, label):
        if type(row.x_q16) is not int or type(row.y_q16) is not int:
            fail(f'{label} has invalid coordinates')
        if not (0 <= row.x_q16 < width*8*65536 and
                0 <= row.y_q16 < height*8*65536):
            fail(f'{label} outside map')
        return row.x_q16/65536,row.y_q16/65536

    def candidates(p, radius, connect):
        result = []
        for y in range(max(0,math.floor((p[1]-radius)/8)),
                       min(height-1,math.floor((p[1]+radius)/8))+1):
            for x in range(max(0,math.floor((p[0]-radius)/8)),
                           min(width-1,math.floor((p[0]+radius)/8))+1):
                cell = y*width+x
                distance = math.dist(p,point(cell))
                if (free[cell] and distance <= radius and
                        (not connect or clearance.clear(p,point(cell)))):
                    result.append((distance,cell))
        return sorted(result)

    starts = []
    for i,grid in enumerate(source.grid):
        p = location(grid,f'GRID row{i}')
        choices = candidates(p,16,True)
        if not choices:
            fail(f'GRID row{i} has no safe terrain attachment')
        starts.append((choices[0][1],p))

    # Flood before selecting ITEM attachments, so a nearer isolated platform
    # cannot hide a reachable pickup location on the main component.
    component = {starts[0][0]}
    queue = deque(component)
    while queue:
        for other in neighbors(queue.popleft()):
            if other not in component:
                component.add(other)
                queue.append(other)
    for i,(cell,_) in enumerate(starts):
        if cell not in component:
            fail(f'disconnected GRID row{i}')

    goals = list(starts)
    unreachable_items = []
    for i,item in enumerate(source.item_boxes):
        p = location(item,f'ITEM row{i}')
        radius = item.half_extent
        if type(radius) is not int or not 0 < radius <= 255:
            fail(f'ITEM row{i} has invalid pickup half-extent')
        choices = [(distance,cell) for distance,cell in candidates(p,radius,False)
                   if cell in component]
        if not choices:
            unreachable_items.append(i)
            continue
        cell = choices[0][1]
        # Preserve a safely accessible exact centre; otherwise patrol the
        # reachable pickup fringe without driving into its recovery material.
        target = p if clearance.clear(p,p) and clearance.clear(point(cell),p) else point(cell)
        goals.append((cell,target))

    def connection(start,end):
        parents = {start:None}
        queue = deque([start])
        while queue and end not in parents:
            current = queue.popleft()
            for other in neighbors(current):
                if other not in parents:
                    parents[other] = current
                    queue.append(other)
        if end not in parents:
            fail('connected component changed during path preparation')
        result = []
        current = end
        while current is not None:
            result.append(point(current))
            if len(result) > _MAX_NODES:
                fail('path exceeds4096-node budget')
            current = parents[current]
        return list(reversed(result))

    def simplify(points):
        points = [p for i,p in enumerate(points) if i == 0 or p != points[i-1]]
        result = points[:1]
        index = 0
        while index+1 < len(points):
            following = len(points)-1
            while following > index+1 and not clearance.clear(points[index],points[following]):
                following -= 1
            if not clearance.clear(points[index],points[following]):
                fail('unproved terrain path segment')
            result.append(points[following])
            index = following
        return result

    result = [goals[0][1]]
    for (a,pa),(b,pb) in zip(goals,goals[1:]+goals[:1]):
        segment = simplify([pa]+connection(a,b)+[pb])
        result.extend(segment[1:])
        if len(result) > _MAX_NODES or len(result)-1 > _MAX_EDGES:
            fail('patrol exceeds4096-node/16384-edge budget')
    if result[-1] == result[0]:
        result.pop()
    if len(set(result)) < 2:
        fail('insufficient distinct reachable patrol points')
    return ArenaPatrol(tuple(result),tuple(unreachable_items))
