#include "mesh_cpu.h"
#include "fixed_3d.h"
#include "mesh_kart_mk8.h"
#include <stdio.h>
#include <string.h>
#define Q PORTABLE_FIXED_3D_ONE
#define U PORTABLE_FIXED_3D_UNIT

static PortableMeshStatus fail(const PortableMeshRacer *r, char *error, size_t capacity,
                               const char *detail)
{
    if (error && capacity)
        snprintf(error, capacity, "3D CPU course=%u mode=%u tick=%u state=%u: %s",
                 r && r->course ? (unsigned)r->course->id : 0u, r ? (unsigned)r->mode : 0u,
                 r ? (unsigned)r->state.tick : 0u, r ? (unsigned)r->state.phase : 0u, detail);
    return PORTABLE_MESH_UNSUPPORTED;
}
static int prepared(const PortableMeshRacer *r)
{
    return r && r->course && portable_mesh_kart_rules_valid(r->rules) &&
           portable_mesh_kart_state_valid(&r->state.kart);
}
static void foot(const PortableMeshRacer *r, int32_t out[3])
{
    const int64_t height = (int64_t)r->rules->kart_radius_q16 + r->rules->ride_height_q16;
    for (unsigned axis = 0; axis < 3u; ++axis)
        out[axis] =
            r->state.kart.position_q16[axis] - (int32_t)(height * r->state.kart.up_q30[axis] / U);
}
/* Positions are bounded to4096m (plus the <=16m body offset). Their squared
 * XYZ differences fit64bits. Binary division avoids shifting a large dot product. */
static uint32_t fraction(int64_t numerator, int64_t denominator)
{
    if (numerator <= 0)
        return 0;
    if (numerator >= denominator)
        return Q;
    uint64_t n = (uint64_t)numerator;
    const uint64_t d = (uint64_t)denominator;
    uint32_t result = 0;
    for (unsigned i = 0; i < 16u; ++i)
    {
        n *= 2u;
        result *= 2u;
        if (n >= d)
        {
            n -= d;
            ++result;
        }
    }
    return result;
}
static uint64_t distance(const int32_t point[3], const int32_t a[3], const int32_t b[3],
                         uint32_t *along)
{
    int32_t delta[3], relative[3];
    for (unsigned axis = 0; axis < 3u; ++axis)
    {
        delta[axis] = b[axis] - a[axis];
        relative[axis] = point[axis] - a[axis];
    }
    const int64_t length = portable_fixed_3d_dot(delta, delta);
    const uint32_t t = length ? fraction(portable_fixed_3d_dot(relative, delta), length) : 0;
    uint64_t square = 0;
    for (unsigned axis = 0; axis < 3u; ++axis)
    {
        const int64_t d = (int64_t)relative[axis] - (int64_t)delta[axis] * t / Q;
        square += (uint64_t)(d * d);
    }
    if (along)
        *along = t;
    return square;
}
static int segment(const PortableCourse3DView *course, const PortableCourse3DRoute *route,
                   uint32_t from, uint32_t edge, PortableCourse3DRouteNode *a,
                   PortableCourse3DRouteNode *b, uint32_t *to)
{
    return from >= route->node_first && from - route->node_first < route->node_count &&
           portable_course_3d_route_node(course, from, a) && edge >= a->edge_first &&
           edge - a->edge_first < a->edge_count &&
           portable_course_3d_route_edge(course, edge, to) && *to >= route->node_first &&
           *to - route->node_first < route->node_count &&
           portable_course_3d_route_node(course, *to, b);
}
static int route_valid(const PortableCourse3DView *course, uint32_t index,
                       PortableCourse3DRoute *route)
{
    const PortableCourse3DTable *nodes =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('R', 'T', 'N', 'D'));
    const PortableCourse3DTable *edges =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('R', 'T', 'E', 'D'));
    if (!portable_course_3d_route(course, index, route) || route->kind != 0u ||
        route->flags != 1u || !nodes || !edges || !route->node_count ||
        route->node_count > 16384u || route->node_first > nodes->count ||
        route->node_count > nodes->count - route->node_first ||
        route->start_node < route->node_first ||
        route->start_node - route->node_first >= route->node_count)
        return 0;
    for (uint32_t i = route->node_first; i < route->node_first + route->node_count; ++i)
    {
        PortableCourse3DRouteNode a, b;
        if (!portable_course_3d_route_node(course, i, &a) || a.radius_q16 <= 0 || a.speed_q16 < 0 ||
            !a.edge_count || a.edge_first > edges->count ||
            a.edge_count > edges->count - a.edge_first)
            return 0;
        for (uint32_t e = a.edge_first; e < a.edge_first + a.edge_count; ++e)
        {
            uint32_t to;
            if (!segment(course, route, i, e, &a, &b, &to) ||
                !memcmp(a.position_q16, b.position_q16, sizeof a.position_q16))
                return 0;
        }
    }
    return 1;
}
static int acquire(PortableMeshCpuState *state, const PortableMeshRacer *r,
                   const PortableCourse3DRoute *route, const int32_t position[3])
{
    uint64_t best = UINT64_MAX;
    int64_t best_alignment = INT64_MIN;
    uint32_t best_edge = UINT32_MAX;
    for (uint32_t from = route->node_first; from < route->node_first + route->node_count; ++from)
    {
        PortableCourse3DRouteNode a, b;
        if (!portable_course_3d_route_node(r->course, from, &a))
            return 0;
        for (uint32_t e = a.edge_first; e < a.edge_first + a.edge_count; ++e)
        {
            uint32_t to, along;
            if (!segment(r->course, route, from, e, &a, &b, &to))
                return 0;
            const uint64_t square = distance(position, a.position_q16, b.position_q16, &along);
            int64_t raw[3];
            int32_t direction[3];
            for (unsigned axis = 0; axis < 3u; ++axis)
                raw[axis] = (int64_t)b.position_q16[axis] - a.position_q16[axis];
            if (!portable_fixed_3d_normalize(raw, direction))
                return 0;
            const int64_t alignment = portable_fixed_3d_dot(direction, r->state.kart.forward_q30);
            if (square < best ||
                (square == best &&
                 (alignment > best_alignment || (alignment == best_alignment && e < best_edge))))
            {
                best = square;
                best_alignment = alignment;
                best_edge = e;
                state->from_node = from;
                state->to_node = to;
                state->selected_edge = e;
                state->projected_fraction_q16 = along;
            }
        }
    }
    state->lost_ticks = 0;
    return best_edge != UINT32_MAX;
}
static int tuning(PortableMeshCpuState *s, const PortableMeshRacer *r,
                  const PortableCourse3DRoute *route)
{
    s->lookahead_q16 = 2 * Q;
    s->corner_speed_q16 = 3 * Q;
    s->recovery_ticks = 30u;
    /* A failed lookup can mean missing OR malformed. Validate the owned span
     * first so malformed authored controls cannot silently select defaults. */
    if (route->property_count > 16384u ||
        route->property_first > UINT32_MAX - route->property_count)
        return 0;
    for (uint32_t i = 0; i < route->property_count; ++i)
    {
        PortableCourse3DProperty property;
        if (!portable_course_3d_property_at(r->course, route->property_first + i, &property))
            return 0;
    }
    const char *keys[] = {"mksc:cpu-lookahead", "mksc:cpu-corner-speed", "mksc:cpu-recovery-ticks"};
    for (unsigned i = 0; i < 3u; ++i)
    {
        PortableCourse3DProperty p;
        if (!portable_course_3d_property_find(r->course, route->property_first,
                                              route->property_count, keys[i], &p))
            continue;
        if (i == 2u)
        {
            if (p.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER || p.value.integer < 1 ||
                p.value.integer > 600)
                return 0;
            s->recovery_ticks = (uint32_t)p.value.integer;
        }
        else
        {
            if (p.type != PORTABLE_COURSE_3D_PROPERTY_FIXED || p.value.fixed_q16 < 3277 ||
                p.value.fixed_q16 > (i == 0u ? 64 * Q : 256 * Q))
                return 0;
            if (i == 0u)
                s->lookahead_q16 = p.value.fixed_q16;
            else
                s->corner_speed_q16 = p.value.fixed_q16;
        }
    }
    return 1;
}
/* Ground-route progress is an explicit presentation-independent navigation
 * contract. Acquisition still uses XYZ, including for stacked flat routes. */
static int ground_route_plane(const PortableMeshRacer *r, const PortableCourse3DRoute *route,
                              int *enabled, int32_t *height)
{
    *enabled = 0;
    if (route->property_count > 16384u ||
        route->property_first > UINT32_MAX - route->property_count)
        return 0;
    for (uint32_t i = 0; i < route->property_count; ++i)
    {
        PortableCourse3DProperty property;
        if (!portable_course_3d_property_at(r->course, route->property_first + i, &property))
            return 0;
        if (!strcmp(property.key, "mksc:cpu-ground-route"))
        {
            if (property.type != PORTABLE_COURSE_3D_PROPERTY_BOOLEAN)
                return 0;
            *enabled = (int)property.value.boolean;
        }
    }
    if (!*enabled)
        return 1;
    PortableCourse3DRouteNode node;
    if (!route->node_count || !portable_course_3d_route_node(r->course, route->node_first, &node))
        return 0;
    *height = node.position_q16[2];
    for (uint32_t i = 1; i < route->node_count; ++i)
        if (route->node_first > UINT32_MAX - i ||
            !portable_course_3d_route_node(r->course, route->node_first + i, &node) ||
            node.position_q16[2] != *height)
            return 0;
    return 1;
}
PortableMeshStatus portable_mesh_cpu_init(PortableMeshCpuState *state, const PortableMeshRacer *r,
                                          uint32_t route_index, char *error, size_t capacity)
{
    if (!state || !prepared(r))
        return fail(r, error, capacity, "invalid initial racer/profile");
    if (!r->rules->brake_deceleration_q16 && !r->rules->coast_deceleration_q16)
        return fail(r, error, capacity, "CPU profile has no braking/coasting authority");
    const PortableCourse3DTable *routes =
        portable_course_3d_table(r->course, PORTABLE_COURSE_3D_TAG('R', 'T', 'H', 'D'));
    if (!routes)
        return fail(r, error, capacity, "missing CPU route");
    if (route_index == PORTABLE_MESH_ABSENT)
        for (uint32_t i = 0; i < routes->count; ++i)
        {
            PortableCourse3DRoute route;
            if (portable_course_3d_route(r->course, i, &route) && route.kind == 0u)
            {
                route_index = i;
                break;
            }
        }
    PortableCourse3DRoute route;
    PortableMeshCpuState next = {0};
    next.course_id = r->course->id;
    next.profile_id = r->rules->profile_id;
    next.profile_revision = r->rules->revision;
    next.route = route_index;
    if (!route_valid(r->course, route_index, &route) || !tuning(&next, r, &route))
        return fail(r, error, capacity, "CPU route geometry/closed graph/tuning");
    int ground_route = 0;
    int32_t ground_height = 0;
    if (!ground_route_plane(r, &route, &ground_route, &ground_height))
        return fail(r, error, capacity, "invalid flat ground-route property/geometry");
    foot(r, next.previous_foot_q16);
    if (!acquire(&next, r, &route, next.previous_foot_q16))
        return fail(r, error, capacity, "CPU route acquisition");
    next.observed_respawn_ticks = r->state.kart.respawn_ticks;
    next.observed_last_respawn = r->state.last_respawn;
    *state = next;
    if (error && capacity)
        error[0] = 0;
    return PORTABLE_MESH_OK;
}

static int32_t node_speed(const PortableCourse3DRouteNode *node, const PortableMeshRacer *r)
{
    return node->speed_q16 > 0 && node->speed_q16 < r->rules->forward_speed_q16
               ? node->speed_q16
               : r->rules->forward_speed_q16;
}
static int next_segment(PortableMeshCpuState *state, const PortableMeshRacer *r,
                        const PortableCourse3DRoute *route)
{
    PortableCourse3DRouteNode a, b;
    uint32_t to;
    const uint32_t from = state->to_node;
    if (!portable_course_3d_route_node(r->course, from, &a) ||
        !segment(r->course, route, from, a.edge_first, &a, &b, &to))
        return 0;
    /* Successor order is authored priority. No hidden random stream is consumed. */
    state->from_node = from;
    state->selected_edge = a.edge_first;
    state->to_node = to;
    state->projected_fraction_q16 = 0;
    ++state->visit_serial;
    return 1;
}
static int arrived(const PortableMeshCpuState *state, const PortableMeshRacer *r,
                   const PortableCourse3DRoute *route, const PortableCourse3DRouteNode *a,
                   const PortableCourse3DRouteNode *b, const int32_t position[3],
                   const int32_t previous_position[3])
{
    /* Radius bounds corner acceptance, not forward progress. Large radii can
     * overlap several short edges; proximity alone must never skip those edges. */
    const uint64_t radius_squared = (uint64_t)((int64_t)b->radius_q16 * b->radius_q16);
    if (distance(b->position_q16, previous_position, position, NULL) > radius_squared)
        return 0;
    int32_t direction[3], relative[3];
    for (unsigned axis = 0; axis < 3u; ++axis)
    {
        direction[axis] = b->position_q16[axis] - a->position_q16[axis];
        relative[axis] = position[axis] - a->position_q16[axis];
    }
    if (portable_fixed_3d_dot(relative, direction) >= portable_fixed_3d_dot(direction, direction))
        return 1;
    PortableCourse3DRouteNode next_a, next_b;
    uint32_t to;
    if (!segment(r->course, route, state->to_node, b->edge_first, &next_a, &next_b, &to))
        return 0;
    for (unsigned axis = 0; axis < 3u; ++axis)
    {
        direction[axis] = next_b.position_q16[axis] - b->position_q16[axis];
        relative[axis] = position[axis] - b->position_q16[axis];
    }
    if (portable_fixed_3d_dot(relative, direction) <= 0)
        return 0;
    return distance(position, b->position_q16, next_b.position_q16, NULL) <
           distance(position, a->position_q16, b->position_q16, NULL);
}
static int32_t slowing_rate(const PortableMeshKartRules *rules)
{
    return rules->brake_deceleration_q16 >= rules->coast_deceleration_q16
               ? rules->brake_deceleration_q16
               : rules->coast_deceleration_q16;
}
static uint16_t slowing_action(const PortableMeshKartRules *rules, const PortableMeshKartState *kart,
                               int32_t steering)
{
    /* No buttons selects authored coasting. A brake flag with a weaker rate
     * would suppress that available slowing in the movement owner; braking
     * past half stick would start MK8's brake slide, so CPUs coast there. */
    return rules->brake_deceleration_q16 > 0 && steering < 16384 && steering > -16384 &&
                   kart->steer_filtered_q16 < Q / 2 && kart->steer_filtered_q16 > -Q / 2 &&
                   rules->brake_deceleration_q16 >= rules->coast_deceleration_q16
               ? PORTABLE_MESH_BRAKE
               : 0;
}
/* Planned share of the authored yaw rate; the rest absorbs steering-filter
 * lag and line error. */
#define CPU_YAW_SHARE_Q16 55706 /* 0.85 */
/* Pursuit lookahead time: the steering target leads the kart by this much
 * travel, never less than the authored lookahead distance. */
#define CPU_PURSUIT_SECONDS_Q16 22938 /* 0.35 s */
static int64_t planned_yaw_rate(const PortableMeshKartRules *rules, int64_t speed)
{
    /* MK8 full lock at this speed (an accelerating kart turns as if at 0.5
     * units/frame), authored turns/s to Q16 radians/s (2*pi = 411775/Q). */
    const int32_t floor = MESH_KART_MK8_UNIT_SPEED_Q16 / 2;
    const int32_t rate =
        portable_mesh_kart_turn_rate(rules, speed > floor ? (speed < INT32_MAX ? (int32_t)speed : INT32_MAX) : floor);
    return (int64_t)rate * INT64_C(411775) / Q * CPU_YAW_SHARE_Q16 / Q;
}
/* The speed whose full-lock arc has this radius (Q16 m). Turn rate rises
 * with speed, so a few fixed-point steps from the top rate settle it. */
static int64_t arc_speed(const PortableMeshKartRules *rules, int64_t radius)
{
    int64_t speed = INT32_MAX;
    for (unsigned step = 0; step < 4u; ++step)
    {
        const int64_t next = planned_yaw_rate(rules, speed) * radius / Q;
        if (next >= speed)
            break;
        speed = next;
    }
    return speed;
}
/* Fastest speed through the bend between two unit directions: the tightest
 * circular arc tangent to both legs no later than their midpoints, driven at
 * the planned yaw rate (v = omega * R), never below the authored corner floor. */
static int32_t corner_limit(const PortableMeshCpuState *state, const PortableMeshRacer *r,
                            const int32_t incoming[3], const int32_t outgoing[3],
                            int64_t incoming_length, int64_t outgoing_length)
{
    const int64_t cosine = portable_fixed_3d_dot(incoming, outgoing) / U / (U / Q);
    const int64_t omega = planned_yaw_rate(r->rules, INT32_MAX);
    if (cosine >= Q - 1 || omega <= 0)
        return INT32_MAX;
    const int64_t cos_half = (int64_t)portable_fixed_3d_sqrt((uint64_t)((Q + cosine) * Q / 2));
    const int64_t sin_half = (int64_t)portable_fixed_3d_sqrt((uint64_t)((Q - cosine) * Q / 2));
    const int64_t leg = (incoming_length < outgoing_length ? incoming_length : outgoing_length) / 2;
    const int64_t radius = sin_half > 0 ? leg * cos_half / sin_half : INT64_MAX / Q;
    const int64_t speed = radius > 4096 * (int64_t)Q ? INT32_MAX : arc_speed(r->rules, radius);
    if (speed >= INT32_MAX)
        return INT32_MAX;
    return speed > state->corner_speed_q16 ? (int32_t)speed : state->corner_speed_q16;
}
static int braking_speed(const PortableMeshCpuState *state, const PortableMeshRacer *r,
                         const PortableCourse3DRoute *route, int32_t *speed)
{
    PortableMeshCpuState cursor = *state;
    PortableCourse3DRouteNode a, b;
    uint32_t to;
    if (!segment(r->course, route, cursor.from_node, cursor.selected_edge, &a, &b, &to))
        return 0;
    int64_t raw[3];
    int32_t direction[3];
    for (unsigned axis = 0; axis < 3u; ++axis)
        raw[axis] = (int64_t)b.position_q16[axis] - a.position_q16[axis];
    if (!portable_fixed_3d_normalize(raw, direction))
        return 0;
    const int64_t length = (int64_t)portable_fixed_3d_sqrt(
        (uint64_t)(raw[0] * raw[0] + raw[1] * raw[1] + raw[2] * raw[2]));
    int64_t distance_to_bend = length * (Q - state->projected_fraction_q16) / Q;
    int64_t incoming_length = length;
    const int64_t velocity = r->state.kart.speed_q16 > 0 ? r->state.kart.speed_q16 : 0;
    /* v²/(2a) has Q16 metre units. Steering lookahead also provides a reaction
     * margin; it must not limit how far ahead braking anticipates a turn. */
    const int64_t deceleration = 2 * (int64_t)slowing_rate(r->rules);
    const int64_t stopping = velocity * velocity / deceleration;
    const int64_t horizon = stopping + state->lookahead_q16;
    for (uint32_t walked = 0; walked < route->node_count && distance_to_bend <= horizon; ++walked)
    {
        if (!next_segment(&cursor, r, route) ||
            !segment(r->course, route, cursor.from_node, cursor.selected_edge, &a, &b, &to))
            return 0;
        for (unsigned axis = 0; axis < 3u; ++axis)
            raw[axis] = (int64_t)b.position_q16[axis] - a.position_q16[axis];
        int32_t following[3];
        if (!portable_fixed_3d_normalize(raw, following))
            return 0;
        const int64_t span = (int64_t)portable_fixed_3d_sqrt(
            (uint64_t)(raw[0] * raw[0] + raw[1] * raw[1] + raw[2] * raw[2]));
        int32_t target = node_speed(&b, r);
        const int32_t corner = corner_limit(state, r, direction, following, incoming_length, span);
        if (corner < target)
            target = corner;
        const int64_t target_squared = (int64_t)target * target;
        const int64_t brake_distance = velocity * velocity > target_squared
                                           ? (velocity * velocity - target_squared) / deceleration
                                           : 0;
        if (distance_to_bend <= brake_distance + state->lookahead_q16 && target < *speed)
            *speed = target;
        distance_to_bend += span;
        incoming_length = span;
        memcpy(direction, following, sizeof direction);
    }
    return 1;
}
static int64_t steering_preview(const PortableMeshCpuState *state, const PortableMeshRacer *r,
                                int32_t authored_speed)
{
    const int64_t excess = (int64_t)r->state.kart.speed_q16 - authored_speed;
    const int64_t response = r->rules->handling_response_q16;
    const int64_t turn_rate = r->rules->handling_turn_rate_q16;
    const int64_t maximum = 64 * (int64_t)Q;
    if (excess <= 0 || response <= 0 || turn_rate <= 0 || state->lookahead_q16 >= maximum)
        return state->lookahead_q16;
    /* CPU steering is approximately2*heading_error near alignment. Convert
     * authored turns/s to radians/s, then preview excess travel during the
     * steering filter lag plus the linearized heading-convergence lag:
     * (v-v_node) * (1/response +1/(2*2pi*turn_rate)). This anticipates forced
     * panel acceleration without changing ordinary authored-speed driving.
     * Validated speed/rate limits keep every intermediate below2^57. */
    const int64_t radians = turn_rate * INT64_C(411775) / Q;
    if (radians <= 0)
        return state->lookahead_q16;
    const int64_t square = (int64_t)Q * Q;
    const int64_t lag = square / response + square / (2 * radians);
    const int64_t distance = state->lookahead_q16 + excess * lag / Q;
    return distance < maximum ? distance : maximum;
}

/* Squared distance (1/256 m units) of corner from the chord origin->target.
 * Lookahead spans stay below 64 m, so shifted local deltas keep every
 * product inside 64 bits. */
static int corner_outside(const int32_t origin[3], const int32_t target[3],
                          const int32_t corner[3], int32_t radius)
{
    int64_t chord[3], offset[3];
    for (unsigned axis = 0; axis < 3u; ++axis)
    {
        chord[axis] = ((int64_t)target[axis] - origin[axis]) / 256;
        offset[axis] = ((int64_t)corner[axis] - origin[axis]) / 256;
    }
    const int64_t cross[3] = {chord[1] * offset[2] - chord[2] * offset[1],
                              chord[2] * offset[0] - chord[0] * offset[2],
                              chord[0] * offset[1] - chord[1] * offset[0]};
    const int64_t length = chord[0] * chord[0] + chord[1] * chord[1] + chord[2] * chord[2];
    const int64_t limit = (int64_t)radius / 256;
    if (length <= 0)
        return 0;
    /* |cross|^2 / |chord|^2 > radius^2, compared via square roots to stay bounded. */
    const uint64_t area = portable_fixed_3d_sqrt(
        (uint64_t)(cross[0] / 4096 * (cross[0] / 4096) + cross[1] / 4096 * (cross[1] / 4096) +
                   cross[2] / 4096 * (cross[2] / 4096)));
    const uint64_t span = portable_fixed_3d_sqrt((uint64_t)length);
    return area * 4096 > span * (uint64_t)limit;
}
static int target_for_route(const PortableMeshCpuState *state, const PortableMeshRacer *r,
                            const PortableCourse3DRoute *route, const int32_t position[3],
                            int32_t target[3], int32_t *target_speed)
{
    PortableCourse3DRouteNode a, b;
    uint32_t to;
    if (!segment(r->course, route, state->from_node, state->selected_edge, &a, &b, &to))
        return 0;
    int32_t direction[3];
    int64_t raw[3];
    for (unsigned axis = 0; axis < 3u; ++axis)
        raw[axis] = (int64_t)b.position_q16[axis] - a.position_q16[axis];
    if (!portable_fixed_3d_normalize(raw, direction))
        return 0;
    const int64_t length = (int64_t)portable_fixed_3d_sqrt(
        (uint64_t)(raw[0] * raw[0] + raw[1] * raw[1] + raw[2] * raw[2]));
    const int64_t remaining = length * (Q - state->projected_fraction_q16) / Q;
    int32_t speed = node_speed(&b, r);
    int64_t ahead = steering_preview(state, r, speed);
    const int64_t velocity = r->state.kart.speed_q16 > 0 ? r->state.kart.speed_q16 : 0;
    if (velocity * CPU_PURSUIT_SECONDS_Q16 / Q > ahead)
        ahead = velocity * CPU_PURSUIT_SECONDS_Q16 / Q;
    if (remaining >= ahead)
    {
        const uint32_t t = state->projected_fraction_q16 + (uint32_t)(ahead * Q / length);
        for (unsigned axis = 0; axis < 3u; ++axis)
            target[axis] =
                a.position_q16[axis] +
                (int32_t)((int64_t)(b.position_q16[axis] - a.position_q16[axis]) * t / Q);
    }
    else
    {
        memcpy(target, b.position_q16, sizeof b.position_q16);
        ahead -= remaining;
        /* Extended steering preview must not enlarge the authored speed
         * preview. Keep its original segment-boundary and braking semantics. */
        int64_t speed_ahead = (int64_t)state->lookahead_q16 - remaining;
        int64_t incoming_length = length;
        PortableMeshCpuState look = *state;
        /* Traverse connected lookahead edges, never scan unrelated stacked roads. */
        for (uint32_t walked = 0; walked < route->node_count && ahead > 0; ++walked)
        {
            if (!next_segment(&look, r, route) ||
                !segment(r->course, route, look.from_node, look.selected_edge, &a, &b, &to))
                return 0;
            for (unsigned axis = 0; axis < 3u; ++axis)
                raw[axis] = (int64_t)b.position_q16[axis] - a.position_q16[axis];
            int32_t following[3];
            if (!portable_fixed_3d_normalize(raw, following))
                return 0;
            const int64_t span = (int64_t)portable_fixed_3d_sqrt(
                (uint64_t)(raw[0] * raw[0] + raw[1] * raw[1] + raw[2] * raw[2]));
            if (speed_ahead > 0)
            {
                const int32_t corner =
                    corner_limit(state, r, direction, following, incoming_length, span);
                if (corner < speed)
                    speed = corner;
                if (node_speed(&b, r) < speed)
                    speed = node_speed(&b, r);
            }
            incoming_length = span;
            int64_t travel = ahead < span ? ahead : span;
            for (unsigned axis = 0; axis < 3u; ++axis)
                target[axis] = a.position_q16[axis] + (int32_t)(travel * following[axis] / U);
            /* Pursuit may cut a bend only within the corner's authored arrival
             * radius; narrow launch and panel targets keep the line. */
            if (corner_outside(position, target, a.position_q16, a.radius_q16))
            {
                int64_t inside = 0, outside = travel;
                for (unsigned step = 0; step < 12u; ++step)
                {
                    const int64_t middle = (inside + outside) / 2;
                    for (unsigned axis = 0; axis < 3u; ++axis)
                        target[axis] =
                            a.position_q16[axis] + (int32_t)(middle * following[axis] / U);
                    if (corner_outside(position, target, a.position_q16, a.radius_q16))
                        outside = middle;
                    else
                        inside = middle;
                }
                for (unsigned axis = 0; axis < 3u; ++axis)
                    target[axis] = a.position_q16[axis] + (int32_t)(inside * following[axis] / U);
                ahead = 0;
                break;
            }
            ahead -= travel;
            speed_ahead -= travel;
            memcpy(direction, following, sizeof direction);
        }
    }
    if (!braking_speed(state, r, route, &speed))
        return 0;
    *target_speed = speed;
    return 1;
}
PortableMeshStatus portable_mesh_cpu_input(PortableMeshCpuState *next,
                                           const PortableMeshCpuState *previous,
                                           const PortableMeshRacer *r, PortableMeshKartInput *input,
                                           char *error, size_t capacity)
{
    if (!next || !previous || !input || !prepared(r) || previous->course_id != r->course->id ||
        previous->profile_id != r->rules->profile_id ||
        previous->profile_revision != r->rules->revision || previous->projected_fraction_q16 > Q ||
        previous->lookahead_q16 < 3277 || previous->lookahead_q16 > 64 * Q ||
        previous->corner_speed_q16 < 3277 || previous->corner_speed_q16 > 256 * Q ||
        !previous->recovery_ticks || previous->recovery_ticks > 600u)
        return fail(r, error, capacity, "invalid controller/racer identity or tuning");
    if (!slowing_rate(r->rules))
        return fail(r, error, capacity, "CPU profile has no braking/coasting authority");
    for (unsigned axis = 0; axis < 3u; ++axis)
        if (previous->previous_foot_q16[axis] < -4112 * Q ||
            previous->previous_foot_q16[axis] > 4112 * Q)
            return fail(r, error, capacity, "invalid previous CPU foot");
    PortableMeshCpuState state = *previous;
    PortableMeshKartInput control = {0, 0};
    int32_t position[3];
    foot(r, position);
    PortableCourse3DRoute route;
    if (!portable_course_3d_route(r->course, state.route, &route) || route.kind != 0u ||
        route.flags != 1u || !route.node_count || route.node_count > 16384u)
        return fail(r, error, capacity, "invalid current CPU route");
    if (r->state.kart.respawn_ticks && !state.observed_respawn_ticks)
    {
        uint32_t recovery_route = state.route;
        if (r->state.last_respawn != PORTABLE_MESH_ABSENT)
        {
            PortableCourse3DRespawn point;
            if (!portable_course_3d_respawn(r->course, r->state.last_respawn, &point))
                return fail(r, error, capacity, "invalid accepted respawn");
            if (point.route != PORTABLE_MESH_ABSENT)
                recovery_route = point.route;
        }
        if (portable_mesh_cpu_init(&state, r, recovery_route, error, capacity) != PORTABLE_MESH_OK)
            return PORTABLE_MESH_UNSUPPORTED;
        if (!portable_course_3d_route(r->course, state.route, &route))
            return fail(r, error, capacity, "invalid recovery CPU route");
    }
    state.observed_respawn_ticks = r->state.kart.respawn_ticks;
    state.observed_last_respawn = r->state.last_respawn;
    int ground_route = 0;
    int32_t ground_height = 0;
    if (!ground_route_plane(r, &route, &ground_route, &ground_height))
        return fail(r, error, capacity, "invalid flat ground-route property/geometry");
    int32_t progress_position[3], previous_position[3];
    memcpy(progress_position, position, sizeof progress_position);
    memcpy(previous_position, state.previous_foot_q16, sizeof previous_position);
    if (ground_route && !r->state.kart.grounded)
    {
        progress_position[2] = ground_height;
        previous_position[2] = ground_height;
    }
    if (r->state.phase == PORTABLE_MESH_RACER_RACING && !r->state.kart.respawn_ticks)
    {
        PortableCourse3DRouteNode a, b;
        uint32_t to;
        if (!segment(r->course, &route, state.from_node, state.selected_edge, &a, &b, &to) ||
            to != state.to_node)
            return fail(r, error, capacity, "invalid connected CPU edge");
        uint64_t square =
            distance(progress_position, a.position_q16, b.position_q16, &state.projected_fraction_q16);
        const int32_t radius = a.radius_q16 > b.radius_q16 ? a.radius_q16 : b.radius_q16;
        if (square > (uint64_t)((int64_t)radius * radius))
        {
            if (state.lost_ticks < state.recovery_ticks)
                ++state.lost_ticks;
            if (state.lost_ticks >= state.recovery_ticks && !acquire(&state, r, &route, position))
                return fail(r, error, capacity, "lost CPU route recovery");
        }
        else
            state.lost_ticks = 0;
        const uint32_t initial_from = state.from_node;
        for (uint32_t advances = 0; advances < route.node_count; ++advances)
        {
            if (!segment(r->course, &route, state.from_node, state.selected_edge, &a, &b, &to))
                return fail(r, error, capacity, "invalid successor CPU edge");
            distance(progress_position, a.position_q16, b.position_q16, &state.projected_fraction_q16);
            if (!arrived(&state, r, &route, &a, &b, progress_position, previous_position))
                break;
            if (!next_segment(&state, r, &route))
                return fail(r, error, capacity, "missing CPU successor");
            if (state.from_node == initial_from)
                break;
        }
        if (!segment(r->course, &route, state.from_node, state.selected_edge, &a, &b, &to))
            return fail(r, error, capacity, "invalid target CPU edge");
        distance(progress_position, a.position_q16, b.position_q16, &state.projected_fraction_q16);
        int32_t target[3], speed;
        if (!target_for_route(&state, r, &route, position, target, &speed))
            return fail(r, error, capacity, "invalid connected lookahead geometry");
        int64_t raw[3];
        for (unsigned axis = 0; axis < 3u; ++axis)
            raw[axis] = (int64_t)target[axis] - position[axis];
        const int64_t normal =
            (raw[0] * r->state.kart.up_q30[0] + raw[1] * r->state.kart.up_q30[1] +
             raw[2] * r->state.kart.up_q30[2]) /
            U;
        for (unsigned axis = 0; axis < 3u; ++axis)
            raw[axis] -= normal * r->state.kart.up_q30[axis] / U;
        int32_t desired[3];
        if (!portable_fixed_3d_normalize(raw, desired))
            control.buttons = slowing_action(r->rules, &r->state.kart, 0);
        else
        {
            const PortableMeshKartState *kart = &r->state.kart;
            int32_t right[3];
            right[0] = (int32_t)(((int64_t)kart->up_q30[1] * kart->forward_q30[2] -
                                  (int64_t)kart->up_q30[2] * kart->forward_q30[1]) /
                                 U);
            right[1] = (int32_t)(((int64_t)kart->up_q30[2] * kart->forward_q30[0] -
                                  (int64_t)kart->up_q30[0] * kart->forward_q30[2]) /
                                 U);
            right[2] = (int32_t)(((int64_t)kart->up_q30[0] * kart->forward_q30[1] -
                                  (int64_t)kart->up_q30[1] * kart->forward_q30[0]) /
                                 U);
            const int64_t sine = portable_fixed_3d_dot(right, desired) / U,
                          cosine = portable_fixed_3d_dot(kart->forward_q30, desired) / U;
            /* Pure pursuit: the arc through the target needs curvature
             * 2*sin(a)/D, so yaw rate 2*v*sin(a)/D of the authored full-lock
             * rate. Low speed keeps the plain heading gain. */
            const int64_t reach = (int64_t)portable_fixed_3d_sqrt(
                (uint64_t)(raw[0] * raw[0] + raw[1] * raw[1] + raw[2] * raw[2]));
            const int64_t velocity = kart->speed_q16 > Q ? kart->speed_q16 : Q;
            const int64_t omega = planned_yaw_rate(r->rules, velocity) * Q / CPU_YAW_SHARE_Q16;
            const int64_t sine_q16 = sine / (U / Q);
            int64_t steering = sine * 65534 / U;
            if (omega > 0 && reach > Q / 4)
            {
                const int64_t yaw = 2 * sine_q16 * velocity / reach;
                const int64_t pursuit = yaw * 32767 / omega;
                if ((pursuit < 0 ? -pursuit : pursuit) > (steering < 0 ? -steering : steering))
                    steering = pursuit;
                /* Above the planned yaw rate the arc cannot be held: slow
                 * to the speed whose arc it is, floored at the corner speed. */
                const int64_t needed = 2 * (sine_q16 < 0 ? -sine_q16 : sine_q16);
                if (cosine > 0 && needed > 0)
                {
                    const int64_t hold = arc_speed(r->rules, reach * Q / needed);
                    const int32_t limit = hold > state.corner_speed_q16 ?
                        (hold < INT32_MAX ? (int32_t)hold : INT32_MAX) : state.corner_speed_q16;
                    if (limit < speed)
                        speed = limit;
                }
            }
            if (cosine < 0)
            {
                steering = sine < 0 ? -32767 : 32767;
                if (speed > state.corner_speed_q16)
                    speed = state.corner_speed_q16;
            }
            control.steering = (int16_t)(steering > 32767    ? 32767
                                         : steering < -32767 ? -32767
                                                             : steering);
            /* A CPU drives its class share of every planned speed. */
            speed = portable_fixed_3d_mul(speed, r->rules->cpu_pace_ratio_q16);
            control.buttons = kart->speed_q16 > speed + Q / 2 ? slowing_action(r->rules, kart, control.steering)
                                                              : PORTABLE_MESH_ACCELERATE;
        }
    }
    if (r->state.phase == PORTABLE_MESH_RACER_COUNTDOWN)
    {
        /* MK8 CPUs start on a preset rocket tier; their grid slot picks it. */
        const unsigned tier = 1u + (((uint32_t)position[0] ^ (uint32_t)position[1]) >> 16) % 5u;
        control.buttons = portable_mesh_kart_rocket_buttons(r->state.countdown_ticks, tier);
    }
    memcpy(state.previous_foot_q16, position, sizeof position);
    *next = state;
    *input = control;
    if (error && capacity)
        error[0] = 0;
    return PORTABLE_MESH_OK;
}
