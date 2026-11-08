#include "mesh_item_target.h"
#include "fixed_3d.h"
#include "mesh_query.h"
#include <string.h>

#define Q PORTABLE_FIXED_3D_ONE
#define U PORTABLE_FIXED_3D_UNIT
#define LIMIT PORTABLE_FIXED_3D_POSITION_LIMIT

static int position_valid(const int32_t position[3])
{
    if (!position)
        return 0;
    for (unsigned i = 0; i < 3u; ++i)
        if (position[i] < -LIMIT || position[i] > LIMIT)
            return 0;
    return 1;
}

static int motion_valid(const PortableMeshItemMotionState *motion)
{
    if (!motion || motion->mode != PORTABLE_MESH_ITEM_SHELL || motion->grounded > 1u ||
        !position_valid(motion->position_q16))
        return 0;
    for (unsigned i = 0; i < 3u; ++i)
        if (motion->velocity_q16[i] < -256 * Q || motion->velocity_q16[i] > 256 * Q ||
            motion->support_normal_q30[i] < -U || motion->support_normal_q30[i] > U)
            return 0;
    const int64_t squared = portable_fixed_3d_dot(motion->support_normal_q30,
                                                 motion->support_normal_q30);
    const int64_t unit = (int64_t)U * U;
    return squared >= unit - unit / 1024 && squared <= unit + unit / 1024;
}

static uint64_t squared_distance(const int32_t a[3], const int32_t b[3])
{
    uint64_t sum = 0;
    for (unsigned i = 0; i < 3u; ++i)
    {
        const int64_t delta = (int64_t)a[i] - b[i];
        sum += (uint64_t)(delta * delta);
    }
    return sum;
}

static uint32_t fraction(int64_t numerator, int64_t denominator)
{
    if (numerator <= 0)
        return 0;
    if (numerator >= denominator)
        return Q;
    uint64_t n = (uint64_t)numerator;
    uint32_t result = 0;
    for (unsigned bit = 0; bit < 16u; ++bit)
    {
        n *= 2u;
        result *= 2u;
        if (n >= (uint64_t)denominator)
        {
            n -= (uint64_t)denominator;
            ++result;
        }
    }
    return result;
}

static uint64_t segment_distance(const int32_t point[3], const int32_t a[3], const int32_t b[3],
                                  uint32_t *progress)
{
    int32_t delta[3], relative[3], closest[3];
    for (unsigned i = 0; i < 3u; ++i)
    {
        delta[i] = b[i] - a[i];
        relative[i] = point[i] - a[i];
    }
    const int64_t squared = portable_fixed_3d_dot(delta, delta);
    const uint32_t along = squared ? fraction(portable_fixed_3d_dot(relative, delta), squared) : 0;
    for (unsigned i = 0; i < 3u; ++i)
        closest[i] = a[i] + (int32_t)((int64_t)delta[i] * along / Q);
    if (progress)
        *progress = along;
    return squared_distance(point, closest);
}

static int edge_nodes(const PortableCourse3DView *course, const PortableCourse3DRoute *route,
                        uint32_t from, uint32_t edge, PortableCourse3DRouteNode *a,
                        PortableCourse3DRouteNode *b, uint32_t *to)
{
    return from >= route->node_first && from - route->node_first < route->node_count &&
        portable_course_3d_route_node(course, from, a) && edge >= a->edge_first &&
        edge - a->edge_first < a->edge_count && portable_course_3d_route_edge(course, edge, to) &&
        *to >= route->node_first && *to - route->node_first < route->node_count &&
        portable_course_3d_route_node(course, *to, b) &&
        position_valid(a->position_q16) && position_valid(b->position_q16);
}

static int route_valid(const PortableCourse3DView *course, uint32_t index,
                         PortableCourse3DRoute *route)
{
    const PortableCourse3DTable *nodes = portable_course_3d_table(course,
        PORTABLE_COURSE_3D_TAG('R', 'T', 'N', 'D'));
    const PortableCourse3DTable *edges = portable_course_3d_table(course,
        PORTABLE_COURSE_3D_TAG('R', 'T', 'E', 'D'));
    if (!nodes || !edges || !portable_course_3d_route(course, index, route) || route->kind != 1u ||
        route->flags > 1u || !route->node_count || route->node_count > 16384u ||
        route->node_first > nodes->count || route->node_count > nodes->count - route->node_first ||
        route->start_node < route->node_first || route->start_node - route->node_first >= route->node_count ||
        edges->count > 65536u)
        return 0;
    for (uint32_t i = route->node_first; i < route->node_first + route->node_count; ++i)
    {
        PortableCourse3DRouteNode a;
        if (!portable_course_3d_route_node(course, i, &a) || !position_valid(a.position_q16) ||
            a.radius_q16 <= 0 || a.radius_q16 > LIMIT || a.speed_q16 < 0 || a.flags ||
            a.edge_first > edges->count || a.edge_count > edges->count - a.edge_first ||
            (route->flags && !a.edge_count))
            return 0;
        for (uint32_t edge = a.edge_first; edge < a.edge_first + a.edge_count; ++edge)
        {
            PortableCourse3DRouteNode b;
            uint32_t to;
            if (!edge_nodes(course, route, i, edge, &a, &b, &to) ||
                !squared_distance(a.position_q16, b.position_q16))
                return 0;
        }
    }
    return 1;
}

PortableMeshItemTargetStatus portable_mesh_item_target_init(PortableMeshItemTargetCursor *out,
    const PortableCourse3DView *course, uint32_t requested, const PortableMeshItemMotionState *motion)
{
    if (!out || !course || !motion_valid(motion))
        return PORTABLE_MESH_ITEM_TARGET_INVALID;
    const PortableCourse3DTable *routes = portable_course_3d_table(course,
        PORTABLE_COURSE_3D_TAG('R', 'T', 'H', 'D'));
    if (!routes || !routes->count)
        return PORTABLE_MESH_ITEM_TARGET_NO_ROUTE;
    if (routes->count > 4096u || (requested != PORTABLE_MESH_ABSENT && requested >= routes->count))
        return PORTABLE_MESH_ITEM_TARGET_INVALID;
    PortableMeshItemTargetCursor result = {0};
    uint64_t best = UINT64_MAX;
    for (uint32_t index = 0; index < routes->count; ++index)
    {
        if (requested != PORTABLE_MESH_ABSENT && index != requested)
            continue;
        PortableCourse3DRoute route;
        if (!portable_course_3d_route(course, index, &route))
            return PORTABLE_MESH_ITEM_TARGET_INVALID;
        if (route.kind != 1u)
        {
            if (requested != PORTABLE_MESH_ABSENT)
                return PORTABLE_MESH_ITEM_TARGET_INVALID;
            continue;
        }
        if (!route_valid(course, index, &route))
            return PORTABLE_MESH_ITEM_TARGET_INVALID;
        for (uint32_t from = route.node_first; from < route.node_first + route.node_count; ++from)
        {
            PortableCourse3DRouteNode a;
            if (!portable_course_3d_route_node(course, from, &a))
                return PORTABLE_MESH_ITEM_TARGET_INVALID;
            for (uint32_t edge = a.edge_first; edge < a.edge_first + a.edge_count; ++edge)
            {
                PortableCourse3DRouteNode b;
                uint32_t to, along;
                if (!edge_nodes(course, &route, from, edge, &a, &b, &to))
                    return PORTABLE_MESH_ITEM_TARGET_INVALID;
                const uint64_t distance = segment_distance(motion->position_q16, a.position_q16,
                                                            b.position_q16, &along);
                if (distance < best)
                {
                    best = distance;
                    result = (PortableMeshItemTargetCursor){.course_id = course->id, .route = index,
                        .from_node = from, .to_node = to, .selected_edge = edge,
                        .progress_q16 = along, .initialized = 1};
                }
            }
        }
    }
    if (best == UINT64_MAX)
        return PORTABLE_MESH_ITEM_TARGET_NO_ROUTE;
    memcpy(result.previous_position_q16, motion->position_q16, sizeof result.previous_position_q16);
    *out = result;
    return PORTABLE_MESH_ITEM_TARGET_OK;
}

static int successor(const PortableCourse3DView *course, const PortableCourse3DRoute *route,
                      uint32_t node, const int32_t target[3], uint32_t *selected)
{
    PortableCourse3DRouteNode a;
    if (!portable_course_3d_route_node(course, node, &a))
        return -1;
    if (!a.edge_count)
        return 0;
    uint64_t best = UINT64_MAX;
    for (uint32_t edge = a.edge_first; edge < a.edge_first + a.edge_count; ++edge)
    {
        PortableCourse3DRouteNode b;
        uint32_t to;
        if (!edge_nodes(course, route, node, edge, &a, &b, &to))
            return -1;
        const uint64_t distance = target ? squared_distance(b.position_q16, target) : 0;
        if (distance < best)
        {
            best = distance;
            *selected = edge;
        }
    }
    return 1;
}

static PortableMeshItemTargetStatus follow(PortableMeshItemTargetCursor *cursor,
    const PortableCourse3DView *course, const PortableMeshItemMotionState *motion,
    const int32_t target[3], int32_t aim[3])
{
    PortableCourse3DRoute route;
    if (!route_valid(course, cursor->route, &route))
        return PORTABLE_MESH_ITEM_TARGET_INVALID;
    cursor->transition_count = 0;
    for (;;)
    {
        PortableCourse3DRouteNode a, b;
        uint32_t to, progress;
        if (!edge_nodes(course, &route, cursor->from_node, cursor->selected_edge, &a, &b, &to) ||
            to != cursor->to_node)
            return PORTABLE_MESH_ITEM_TARGET_INVALID;
        (void)segment_distance(motion->position_q16, a.position_q16, b.position_q16, &progress);
        const uint64_t reached_distance = segment_distance(b.position_q16,
            cursor->previous_position_q16, motion->position_q16, NULL);
        const uint64_t radius_squared = (uint64_t)((int64_t)b.radius_q16 * b.radius_q16);
        uint32_t edge = 0;
        const int has_next = successor(course, &route, to, target, &edge);
        if (has_next < 0)
            return PORTABLE_MESH_ITEM_TARGET_INVALID;
        int reached = reached_distance <= radius_squared && progress == Q;
        if (!reached && reached_distance <= radius_squared && has_next)
        {
            PortableCourse3DRouteNode next_a, next_b;
            uint32_t next_to, next_progress;
            if (!edge_nodes(course, &route, to, edge, &next_a, &next_b, &next_to))
                return PORTABLE_MESH_ITEM_TARGET_INVALID;
            const uint64_t next_distance = segment_distance(motion->position_q16,
                next_a.position_q16, next_b.position_q16, &next_progress);
            reached = next_progress > 0u && next_distance < segment_distance(motion->position_q16,
                a.position_q16, b.position_q16, NULL);
        }
        if (!reached)
        {
            if (progress > cursor->progress_q16)
                cursor->progress_q16 = progress;
            memcpy(aim, b.position_q16, sizeof b.position_q16);
            return PORTABLE_MESH_ITEM_TARGET_OK;
        }
        if (!has_next)
            return PORTABLE_MESH_ITEM_TARGET_END_ROUTE;
        if (cursor->transition_count == PORTABLE_MESH_ITEM_TARGET_TRANSITIONS)
            return PORTABLE_MESH_ITEM_TARGET_BUDGET;
        PortableCourse3DRouteNode next_a, next_b;
        uint32_t next_to;
        if (!edge_nodes(course, &route, to, edge, &next_a, &next_b, &next_to))
            return PORTABLE_MESH_ITEM_TARGET_INVALID;
        cursor->from_node = to;
        cursor->to_node = next_to;
        cursor->selected_edge = edge;
        cursor->progress_q16 = 0;
        ++cursor->transition_count;
    }
}

static int collision_triangle(const void *context, uint32_t index, PortableMeshTriangle *out)
{
    const PortableCourse3DView *course = context;
    PortableCourse3DCollision row;
    if (!portable_course_3d_collision(course, index, &row) || row.gravity_zone != PORTABLE_COURSE_3D_ABSENT)
        return 0;
    for (unsigned i = 0; i < 3u; ++i)
        if (!portable_course_3d_collision_vertex(course, row.vertices[i], out->vertices_q16[i]))
            return 0;
    out->surface = row.surface;
    out->respawn = row.respawn;
    return 1;
}

static int collision_surface(const void *context, uint32_t index, PortableMeshSurface *out)
{
    PortableCourse3DSurface surface;
    if (!portable_course_3d_surface(context, index, &surface))
        return 0;
    *out = (PortableMeshSurface){.type = surface.type};
    return 1;
}

static PortableMeshItemTargetStatus direct_clear(const PortableCourse3DView *course,
    const PortableMeshItemRules *rules, const PortableMeshItemMotionState *motion,
    const int32_t target[3], int same_stage, int *clear, const PortableMeshCourse *geometry)
{
    *clear = 0;
    if (!target || !same_stage || squared_distance(motion->position_q16, target) >
        (uint64_t)((int64_t)rules->shell_homing_range_q16 * rules->shell_homing_range_q16))
        return PORTABLE_MESH_ITEM_TARGET_OK;
    PortableMeshCourse linear;
    if (!geometry)
    {
        const PortableCourse3DTable *triangles = portable_course_3d_table(course,
            PORTABLE_COURSE_3D_TAG('C', 'O', 'L', 'T'));
        if (!triangles || !triangles->count || triangles->count > 262144u)
            return PORTABLE_MESH_ITEM_TARGET_INVALID;
        linear = (PortableMeshCourse){course, triangles->count, 0, collision_triangle, collision_surface, NULL, NULL};
        geometry = &linear;
    }
    PortableMeshFaceHit hit;
    const PortableMeshQueryStatus status = portable_mesh_query_sweep_faces(geometry,
        motion->position_q16, target, rules->actors_radius_q16, &hit);
    if (status == PORTABLE_MESH_QUERY_INVALID)
        return PORTABLE_MESH_ITEM_TARGET_INVALID;
    *clear = status == PORTABLE_MESH_QUERY_MISS;
    return PORTABLE_MESH_ITEM_TARGET_OK;
}

static void tangent(const int64_t vector[3], const PortableMeshItemMotionState *motion,
                      int64_t projected[3])
{
    memcpy(projected, vector, 3u * sizeof *projected);
    if (motion->grounded)
    {
        const int64_t raw_normal[3] = {motion->support_normal_q30[0],
            motion->support_normal_q30[1], motion->support_normal_q30[2]};
        int32_t normal[3];
        (void)portable_fixed_3d_normalize(raw_normal, normal);
        int64_t along = 0;
        for (unsigned i = 0; i < 3u; ++i)
            along += vector[i] * normal[i];
        along /= U;
        for (unsigned i = 0; i < 3u; ++i)
            projected[i] -= along * normal[i] / U;
    }
}

static int direction(const int64_t vector[3], const PortableMeshItemMotionState *motion,
                      int32_t out[3])
{
    int64_t projected[3];
    tangent(vector, motion, projected);
    return portable_fixed_3d_normalize(projected, out);
}

static void rotate_toward(const int32_t current[3], const int32_t desired[3],
                           uint16_t maximum, const PortableMeshItemMotionState *motion, int32_t out[3])
{
    /* One binary-angle unit reserves more than the Q30 CORDIC/normalization
     * rounding error, so the physical turn stays inside the authored budget. */
    const uint16_t angle = maximum > 1u ? (uint16_t)(maximum - 1u) : 0u;
    if (!angle)
    {
        memcpy(out, current, 3u * sizeof *out);
        return;
    }
    int32_t sine, cosine;
    portable_fixed_3d_sincos(angle, &sine, &cosine);
    const int64_t dot = portable_fixed_3d_dot(current, desired) / U;
    if (dot >= cosine)
    {
        memcpy(out, desired, 3u * sizeof *out);
        return;
    }
    int64_t raw[3];
    for (unsigned i = 0; i < 3u; ++i)
        raw[i] = (int64_t)desired[i] - dot * current[i] / U;
    int32_t perpendicular[3];
    if (!portable_fixed_3d_normalize(raw, perpendicular))
    {
        if (motion->grounded)
        {
            const int32_t *normal = motion->support_normal_q30;
            for (unsigned i = 0; i < 3u; ++i)
                raw[i] = ((int64_t)normal[(i + 1u) % 3u] * current[(i + 2u) % 3u] -
                    (int64_t)normal[(i + 2u) % 3u] * current[(i + 1u) % 3u]) / U;
        }
        else
        {
            unsigned axis = 0;
            for (unsigned i = 1; i < 3u; ++i)
                if ((int64_t)current[i] * current[i] < (int64_t)current[axis] * current[axis])
                    axis = i;
            for (unsigned i = 0; i < 3u; ++i)
                raw[i] = (i == axis ? U : 0) - (int64_t)current[axis] * current[i] / U;
        }
        (void)portable_fixed_3d_normalize(raw, perpendicular);
    }
    for (unsigned i = 0; i < 3u; ++i)
        raw[i] = ((int64_t)current[i] * cosine + (int64_t)perpendicular[i] * sine) / U;
    (void)portable_fixed_3d_normalize(raw, out);
}

PortableMeshItemTargetStatus portable_mesh_item_target_steer_geometry(PortableMeshItemTargetCursor *next,
    const PortableMeshItemTargetCursor *previous, const PortableCourse3DView *course,
    const PortableMeshItemRules *rules, const PortableMeshItemMotionState *motion,
    const int32_t target[3], int same_stage, int32_t velocity[3], const PortableMeshCourse *geometry)
{
    if (!next || !previous || !course || !velocity || !portable_mesh_item_rules_valid(rules) ||
        !motion_valid(motion) || (target && !position_valid(target)) || previous->initialized > 1u ||
        previous->direct > 1u || (previous->initialized &&
        (previous->course_id != course->id || previous->progress_q16 > Q ||
         previous->transition_count > PORTABLE_MESH_ITEM_TARGET_TRANSITIONS ||
         !position_valid(previous->previous_position_q16))))
        return PORTABLE_MESH_ITEM_TARGET_INVALID;
    PortableMeshItemTargetCursor cursor = *previous;
    PortableMeshItemTargetStatus route_status = PORTABLE_MESH_ITEM_TARGET_OK;
    if (!cursor.initialized)
        route_status = portable_mesh_item_target_init(&cursor, course, PORTABLE_MESH_ABSENT, motion);
    int32_t route_aim[3] = {0};
    if (route_status == PORTABLE_MESH_ITEM_TARGET_OK)
        route_status = follow(&cursor, course, motion, target, route_aim);
    if (route_status == PORTABLE_MESH_ITEM_TARGET_INVALID || route_status == PORTABLE_MESH_ITEM_TARGET_BUDGET)
        return route_status;
    int clear;
    const PortableMeshItemTargetStatus visibility = direct_clear(course, rules, motion, target,
                                                                 same_stage, &clear, geometry);
    if (visibility != PORTABLE_MESH_ITEM_TARGET_OK)
        return visibility;
    int32_t desired[3];
    int64_t raw[3];
    int has_direction = 0;
    if (clear)
    {
        for (unsigned i = 0; i < 3u; ++i)
            raw[i] = (int64_t)target[i] - motion->position_q16[i];
        has_direction = direction(raw, motion, desired);
    }
    cursor.direct = clear && has_direction;
    if (!has_direction)
    {
        if (route_status != PORTABLE_MESH_ITEM_TARGET_OK)
            return clear ? PORTABLE_MESH_ITEM_TARGET_NO_DIRECTION : route_status;
        for (unsigned i = 0; i < 3u; ++i)
            raw[i] = (int64_t)route_aim[i] - motion->position_q16[i];
        if (!direction(raw, motion, desired))
            return PORTABLE_MESH_ITEM_TARGET_NO_DIRECTION;
    }
    for (unsigned i = 0; i < 3u; ++i)
        raw[i] = motion->velocity_q16[i];
    int32_t current[3], heading[3];
    if (!direction(raw, motion, current))
        memcpy(heading, desired, sizeof heading);
    else
        rotate_toward(current, desired,
            (uint16_t)portable_fixed_3d_dt(rules->shell_homing_turn_rate_q16), motion, heading);
    int64_t projected_velocity[3];
    tangent(raw, motion, projected_velocity);
    int64_t squared = 0;
    for (unsigned i = 0; i < 3u; ++i)
        squared += projected_velocity[i] * projected_velocity[i];
    int32_t speed = (int32_t)portable_fixed_3d_sqrt((uint64_t)squared) +
                    portable_fixed_3d_dt(rules->shell_acceleration_q16);
    if (speed > rules->shell_speed_q16)
        speed = rules->shell_speed_q16;
    int32_t result[3];
    for (unsigned i = 0; i < 3u; ++i)
        result[i] = (int32_t)((int64_t)heading[i] * speed / U);
    const uint64_t result_squared = (uint64_t)portable_fixed_3d_dot(result, result);
    const uint64_t cap_squared = (uint64_t)((int64_t)rules->shell_speed_q16 * rules->shell_speed_q16);
    if (result_squared > cap_squared)
    {
        uint64_t length = portable_fixed_3d_sqrt(result_squared);
        if (length * length < result_squared)
            ++length;
        for (unsigned i = 0; i < 3u; ++i)
            result[i] = (int32_t)((int64_t)result[i] * rules->shell_speed_q16 / (int64_t)length);
    }
    memcpy(cursor.previous_position_q16, motion->position_q16, sizeof cursor.previous_position_q16);
    *next = cursor;
    memcpy(velocity, result, sizeof result);
    return PORTABLE_MESH_ITEM_TARGET_OK;
}

const char *portable_mesh_item_target_status_name(PortableMeshItemTargetStatus status)
{
    switch (status)
    {
    case PORTABLE_MESH_ITEM_TARGET_OK: return "ok";
    case PORTABLE_MESH_ITEM_TARGET_NO_ROUTE: return "missing ITEM route";
    case PORTABLE_MESH_ITEM_TARGET_END_ROUTE: return "terminal ITEM route";
    case PORTABLE_MESH_ITEM_TARGET_NO_DIRECTION: return "no tangent direction";
    case PORTABLE_MESH_ITEM_TARGET_INVALID: return "invalid course/route/target state";
    case PORTABLE_MESH_ITEM_TARGET_BUDGET: return "ITEM route transition budget";
    default: return "unknown target status";
    }
}

PortableMeshItemTargetStatus portable_mesh_item_target_steer(PortableMeshItemTargetCursor *next,
    const PortableMeshItemTargetCursor *previous, const PortableCourse3DView *course,
    const PortableMeshItemRules *rules, const PortableMeshItemMotionState *motion,
    const int32_t target[3], int same_stage, int32_t velocity[3])
{
    return portable_mesh_item_target_steer_geometry(next,previous,course,rules,motion,target,
                                                    same_stage,velocity,NULL);
}
