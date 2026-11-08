#include "mesh_item_motion.h"
#include "fixed_3d.h"
#include "mesh_query.h"
#include <string.h>
#define Q PORTABLE_FIXED_3D_ONE
#define U PORTABLE_FIXED_3D_UNIT
#define LIMIT PORTABLE_FIXED_3D_POSITION_LIMIT
#define SPEED INT32_C(16777216)
#define SUBSTEPS 256u
#define IMPACTS 8u
#define PASSES 8u
static void fault_begin(PortableMeshItemMotionFault *f, PortableMeshItemMotionFaultPhase phase,
                        const PortableMeshItemMotionState *s, int32_t radius)
{
    memset(f, 0, sizeof *f);
    f->phase = phase;
    f->triangle = UINT32_MAX;
    f->radius_q16 = radius;
    memcpy(f->position_q16, s->position_q16, sizeof f->position_q16);
    memcpy(f->velocity_q16, s->velocity_q16, sizeof f->velocity_q16);
}
static int position_valid(const int32_t p[3])
{
    if (!p)
        return 0;
    for (unsigned i = 0; i < 3u; ++i)
        if (p[i] < -LIMIT || p[i] > LIMIT)
            return 0;
    return 1;
}
static int velocity_valid(const int32_t v[3])
{
    if (!v)
        return 0;
    for (unsigned i = 0; i < 3u; ++i)
        if (v[i] < -SPEED || v[i] > SPEED)
            return 0;
    return 1;
}
static int state_valid(const PortableMeshItemMotionState *s)
{
    if (!s || s->mode > PORTABLE_MESH_ITEM_BANANA || s->grounded > 1 ||
        !position_valid(s->position_q16) || !velocity_valid(s->velocity_q16))
        return 0;
    for (unsigned i = 0; i < 3u; ++i)
        if (s->support_normal_q30[i] < -U || s->support_normal_q30[i] > U)
            return 0;
    const int64_t length = portable_fixed_3d_dot(s->support_normal_q30, s->support_normal_q30);
    return length >= (int64_t)U * U - (int64_t)U * U / 1024 &&
           length <= (int64_t)U * U + (int64_t)U * U / 1024;
}
PortableMeshItemMotionStatus portable_mesh_item_motion_init(PortableMeshItemMotionState *out,
                                                            PortableMeshItemMotionMode mode,
                                                            const int32_t p[3], const int32_t v[3])
{
    if (!out || (mode != PORTABLE_MESH_ITEM_SHELL && mode != PORTABLE_MESH_ITEM_BANANA) ||
        !position_valid(p) || !velocity_valid(v))
        return PORTABLE_MESH_ITEM_MOTION_INVALID;
    PortableMeshItemMotionState s = {0};
    memcpy(s.position_q16, p, sizeof s.position_q16);
    memcpy(s.velocity_q16, v, sizeof s.velocity_q16);
    s.support_normal_q30[2] = U;
    s.mode = (uint8_t)mode;
    s.contact_triangle = s.contact_surface = s.respawn_index = UINT32_MAX;
    *out = s;
    return PORTABLE_MESH_ITEM_MOTION_OK;
}
static int32_t unit_product(int32_t value, int32_t normal)
{
    return (int32_t)((int64_t)value * normal / U);
}
static void project_inward(int32_t vector[3], const int32_t normal[3], int32_t restitution)
{
    const int32_t inward = (int32_t)(portable_fixed_3d_dot(vector, normal) / U);
    if (inward >= 0)
        return;
    const int64_t impulse = -(int64_t)inward * (Q + restitution) / Q;
    for (unsigned i = 0; i < 3u; ++i)
        vector[i] = portable_fixed_3d_clamp((int64_t)vector[i] + impulse * normal[i] / U);
}
static void project_tangent(int32_t vector[3], const int32_t normal[3])
{
    const int32_t along = (int32_t)(portable_fixed_3d_dot(vector, normal) / U);
    for (unsigned i = 0; i < 3u; ++i)
        vector[i] -= unit_product(along, normal[i]);
    /* Truncation can leave a sub-LSB inward component. Face sweeps compare
     * widened plane distances, so that residue repeatedly hits the same face.
     * Move the minimum integer amount along the dominant normal component;
     * exact tangencies and already outward vectors remain byte-identical. */
    const int64_t residual = portable_fixed_3d_dot(vector, normal);
    if (residual < 0)
    {
        unsigned dominant = 0;
        for (unsigned i = 1; i < 3u; ++i)
            if ((int64_t)normal[i] * normal[i] > (int64_t)normal[dominant] * normal[dominant])
                dominant = i;
        const int64_t component =
            normal[dominant] < 0 ? -(int64_t)normal[dominant] : normal[dominant];
        const int32_t correction = (int32_t)((-residual + component - 1) / component);
        vector[dominant] += normal[dominant] < 0 ? -correction : correction;
    }
}
static void clear_support(PortableMeshItemMotionState *s)
{
    s->grounded = 0;
    s->contact_triangle = s->contact_surface = s->respawn_index = UINT32_MAX;
}
static PortableMeshItemMotionStatus
contact(PortableMeshItemMotionState *s, const PortableMeshItemRules *rules, int32_t cutoff,
        const int32_t normal[3], uint32_t triangle, uint32_t surface, uint32_t type,
        uint32_t respawn, int32_t remaining[3], PortableMeshItemMotionEvents *events)
{
    if (type >= 6u && type <= 8u)
        return PORTABLE_MESH_ITEM_MOTION_OUTSIDE;
    const int floor = normal[2] >= cutoff && type != 3u;
    if (floor)
    {
        if (!s->grounded)
            events->landed = 1;
        s->grounded = 1;
        s->contact_triangle = triangle;
        s->contact_surface = surface;
        s->respawn_index = respawn;
        memcpy(s->support_normal_q30, normal, sizeof s->support_normal_q30);
        if (s->mode == PORTABLE_MESH_ITEM_BANANA)
        {
            memset(s->velocity_q16, 0, sizeof s->velocity_q16);
            if (remaining)
                memset(remaining, 0, 3 * sizeof *remaining);
            events->resting = 1;
            return PORTABLE_MESH_ITEM_MOTION_OK;
        }
    }
    const int32_t inward = (int32_t)(portable_fixed_3d_dot(s->velocity_q16, normal) / U);
    int32_t restitution = 0;
    if (!floor && inward < 0)
    {
        if (s->mode == PORTABLE_MESH_ITEM_SHELL)
        {
            if (s->bounce_count >= (uint32_t)rules->shell_max_bounces)
                return PORTABLE_MESH_ITEM_MOTION_BOUNCE_LIMIT;
            ++s->bounce_count;
            restitution = rules->shell_restitution_q16;
        }
        ++events->wall_impacts;
    }
    if (floor)
    {
        project_tangent(s->velocity_q16, normal);
        if (remaining)
            project_tangent(remaining, normal);
    }
    else
    {
        project_inward(s->velocity_q16, normal, restitution);
        if (remaining)
            project_inward(remaining, normal, restitution);
    }
    return velocity_valid(s->velocity_q16) ? PORTABLE_MESH_ITEM_MOTION_OK
                                           : PORTABLE_MESH_ITEM_MOTION_INVALID;
}
static PortableMeshItemMotionStatus overlaps(PortableMeshItemMotionState *s,
                                             const PortableMeshItemRules *rules,
                                             const PortableMeshCourse *course, int32_t cutoff,
                                             PortableMeshItemMotionEvents *events,
                                             PortableMeshItemMotionFault *fault)
{
    fault_begin(fault, PORTABLE_MESH_ITEM_FAULT_OVERLAP, s, rules->actors_radius_q16);
    for (unsigned pass = 0; pass < PASSES; ++pass)
    {
        int pushed = 0;
        uint32_t first = 0, i;
        for (;;)
        {
            int32_t low[3], high[3];
            if (!portable_mesh_query_bounds(s->position_q16, s->position_q16,
                                            (int64_t)rules->actors_radius_q16 + 2, low, high))
                return PORTABLE_MESH_ITEM_MOTION_INVALID;
            const PortableMeshQueryStatus next =
                portable_mesh_query_next_triangle(course, low, high, first, &i);
            if (next == PORTABLE_MESH_QUERY_INVALID)
                return PORTABLE_MESH_ITEM_MOTION_INVALID;
            if (next == PORTABLE_MESH_QUERY_MISS)
                break;
            first = i + 1u;
            PortableMeshOverlapHit hit = {0};
            const PortableMeshQueryStatus q = portable_mesh_query_overlap(
                course, i, s->position_q16, rules->actors_radius_q16, &hit);
            if (q == PORTABLE_MESH_QUERY_INVALID)
                return PORTABLE_MESH_ITEM_MOTION_INVALID;
            if (q == PORTABLE_MESH_QUERY_MISS)
                continue;
            fault->triangle = hit.triangle;
            memcpy(fault->normal_q30, hit.normal_q30, sizeof fault->normal_q30);
            /* Only face-interior upward normals are support. A nearby edge
             * must not make an airborne item attach to the side of a deck. */
            const uint32_t type = hit.face_interior ? hit.type : 3u;
            if (hit.type >= 6u && hit.type <= 8u)
                return PORTABLE_MESH_ITEM_MOTION_OUTSIDE;
            const PortableMeshItemMotionStatus status =
                contact(s, rules, cutoff, hit.normal_q30, hit.triangle, hit.surface, type,
                        hit.respawn, NULL, events);
            if (status != PORTABLE_MESH_ITEM_MOTION_OK)
                return status;
            if (hit.penetration_q16 > 0)
            {
                for (unsigned axis = 0; axis < 3u; ++axis)
                    s->position_q16[axis] +=
                        unit_product(hit.penetration_q16 + 2, hit.normal_q30[axis]);
                if (!position_valid(s->position_q16))
                    return PORTABLE_MESH_ITEM_MOTION_OUTSIDE;
                pushed = 1;
            }
        }
        if (!pushed)
            return PORTABLE_MESH_ITEM_MOTION_OK;
    }
    fault->iteration = PASSES;
    return PORTABLE_MESH_ITEM_MOTION_COLLISION_BUDGET;
}
static PortableMeshItemMotionStatus support(PortableMeshItemMotionState *s,
                                            const PortableMeshItemRules *rules,
                                            const PortableMeshCourse *course, int32_t cutoff,
                                            int32_t drop, PortableMeshItemMotionEvents *events)
{
    PortableMeshSupportHit hit = {0};
    const PortableMeshQueryStatus q = portable_mesh_query_support(
        course, s->position_q16, rules->actors_radius_q16, drop, cutoff, &hit);
    if (q == PORTABLE_MESH_QUERY_INVALID)
        return PORTABLE_MESH_ITEM_MOTION_INVALID;
    if (q == PORTABLE_MESH_QUERY_MISS)
        return PORTABLE_MESH_ITEM_MOTION_OK;
    const PortableMeshItemMotionStatus status =
        contact(s, rules, cutoff, hit.normal_q30, hit.triangle, hit.surface, hit.type, hit.respawn,
                NULL, events);
    if (status == PORTABLE_MESH_ITEM_MOTION_OK)
        memcpy(s->position_q16, hit.center_q16, sizeof s->position_q16);
    return status;
}
static PortableMeshItemMotionStatus
move_step(PortableMeshItemMotionState *s, const PortableMeshItemRules *rules,
          const PortableMeshCourse *course, int32_t cutoff, int32_t remaining[3],
          PortableMeshItemMotionEvents *events, PortableMeshItemMotionFault *fault)
{
    fault_begin(fault, PORTABLE_MESH_ITEM_FAULT_SWEEP, s, rules->actors_radius_q16);
    const uint64_t one = PORTABLE_MESH_QUERY_FRACTION_ONE;
    for (unsigned impact = 0; impact < IMPACTS; ++impact)
    {
        if (!remaining[0] && !remaining[1] && !remaining[2])
            return PORTABLE_MESH_ITEM_MOTION_OK;
        int32_t after[3];
        for (unsigned axis = 0; axis < 3u; ++axis)
        {
            const int64_t value = (int64_t)s->position_q16[axis] + remaining[axis];
            if (value < -LIMIT || value > LIMIT)
                return PORTABLE_MESH_ITEM_MOTION_OUTSIDE;
            after[axis] = (int32_t)value;
        }
        PortableMeshFaceHit hit = {0};
        const PortableMeshQueryStatus q = portable_mesh_query_sweep_faces(
            course, s->position_q16, after, rules->actors_radius_q16, &hit);
        if (q == PORTABLE_MESH_QUERY_INVALID)
            return PORTABLE_MESH_ITEM_MOTION_INVALID;
        if (q == PORTABLE_MESH_QUERY_MISS)
        {
            memcpy(s->position_q16, after, sizeof after);
            return PORTABLE_MESH_ITEM_MOTION_OK;
        }
        fault->triangle = hit.triangle;
        fault->fraction_q32 = hit.fraction_q32;
        memcpy(fault->normal_q30, hit.normal_q30, sizeof fault->normal_q30);
        memcpy(s->position_q16, hit.center_q16, sizeof s->position_q16);
        for (unsigned axis = 0; axis < 3u; ++axis)
            remaining[axis] = (int32_t)((int64_t)remaining[axis] *
                                        (int64_t)(one - hit.fraction_q32) / (int64_t)one);
        const PortableMeshItemMotionStatus status =
            contact(s, rules, cutoff, hit.normal_q30, hit.triangle, hit.surface, hit.type,
                    hit.respawn, remaining, events);
        if (status != PORTABLE_MESH_ITEM_MOTION_OK)
            return status;
        /* Two Q16 LSBs of outward skin prevent rounded zero-time seam hits
         * from consuming the impact budget. Final support removes floor skin. */
        for (unsigned axis = 0; axis < 3u; ++axis)
            s->position_q16[axis] += unit_product(2, hit.normal_q30[axis]);
        if (!position_valid(s->position_q16))
            return PORTABLE_MESH_ITEM_MOTION_OUTSIDE;
    }
    fault->iteration = IMPACTS;
    memcpy(fault->remaining_q16, remaining, sizeof fault->remaining_q16);
    return PORTABLE_MESH_ITEM_MOTION_COLLISION_BUDGET;
}
static PortableMeshItemMotionStatus tick_internal(PortableMeshItemMotionState *state,
                                                  const PortableMeshItemRules *rules,
                                                  const PortableMeshCourse *course, int32_t cutoff,
                                                  PortableMeshItemMotionEvents *out,
                                                  PortableMeshItemMotionFault *fault)
{
    if (!out || !state_valid(state) || !portable_mesh_item_rules_valid(rules) || !course ||
        !course->triangle || !course->surface || course->triangle_count > 262144u ||
        course->gravity_q16 < 0 || course->gravity_q16 > 67108864 || cutoff <= 0 || cutoff > U ||
        state->bounce_count > (uint32_t)rules->shell_max_bounces)
        return PORTABLE_MESH_ITEM_MOTION_INVALID;
    PortableMeshItemMotionState s = *state;
    PortableMeshItemMotionEvents events = {0};
    const int was_grounded = s.grounded;
    clear_support(&s);
    PortableMeshItemMotionStatus status = overlaps(&s, rules, course, cutoff, &events, fault);
    if (status != PORTABLE_MESH_ITEM_MOTION_OK)
        return status;
    if (!s.grounded && was_grounded)
    {
        status = support(&s, rules, course, cutoff,
                         s.mode == PORTABLE_MESH_ITEM_BANANA ? 2 : rules->shell_ground_probe_q16,
                         &events);
        if (status != PORTABLE_MESH_ITEM_MOTION_OK)
            return status;
    }
    if (!s.grounded)
    {
        const int32_t gravity =
            s.mode == PORTABLE_MESH_ITEM_BANANA ? rules->banana_gravity_q16 : course->gravity_q16;
        /* A fall off the course keeps the solver's speed bound until the
         * item leaves the course bounds. */
        const int64_t velocity = (int64_t)s.velocity_q16[2] - portable_fixed_3d_dt(gravity);
        if (velocity > SPEED)
            return PORTABLE_MESH_ITEM_MOTION_INVALID;
        s.velocity_q16[2] = (int32_t)(velocity < -SPEED ? -SPEED : velocity);
    }
    uint64_t distance_squared = 0;
    for (unsigned axis = 0; axis < 3u; ++axis)
    {
        const int64_t distance = portable_fixed_3d_dt(s.velocity_q16[axis]);
        distance_squared += (uint64_t)(distance * distance);
    }
    /* Bound the whole vector, not just its largest axis: a reflection can
     * redistribute travel across axes while preserving its length. */
    uint32_t maximum = (uint32_t)portable_fixed_3d_sqrt(distance_squared);
    if ((uint64_t)maximum * maximum < distance_squared)
        ++maximum;
    const uint32_t unit = (uint32_t)rules->actors_radius_q16 / 2u;
    uint32_t steps = maximum / unit + (maximum % unit != 0);
    if (!steps)
        steps = 1;
    if (steps > SUBSTEPS)
    {
        fault_begin(fault, PORTABLE_MESH_ITEM_FAULT_SUBSTEPS, &s, rules->actors_radius_q16);
        fault->iteration = steps;
        return PORTABLE_MESH_ITEM_MOTION_COLLISION_BUDGET;
    }
    for (uint32_t step = 1; step <= steps; ++step)
    {
        const int grounded = s.grounded;
        clear_support(&s);
        int32_t displacement[3];
        for (unsigned axis = 0; axis < 3u; ++axis)
        {
            /* Distribute the fixed tick exactly for unchanged velocity; after
             * a bounce subsequent substeps use its new velocity, not stale travel. */
            const int64_t numerator = (int64_t)s.velocity_q16[axis] * 4389;
            const int64_t denominator = INT64_C(262144) * steps;
            displacement[axis] =
                (int32_t)(numerator * step / denominator - numerator * (step - 1u) / denominator);
        }
        status = move_step(&s, rules, course, cutoff, displacement, &events, fault);
        if (status != PORTABLE_MESH_ITEM_MOTION_OK)
            return status;
        const int touched_ground = s.grounded;
        clear_support(&s);
        status = overlaps(&s, rules, course, cutoff, &events, fault);
        if (status != PORTABLE_MESH_ITEM_MOTION_OK)
            return status;
        if (s.mode == PORTABLE_MESH_ITEM_BANANA && (grounded || touched_ground || s.grounded))
        {
            status = support(&s, rules, course, cutoff, 2, &events);
            if (status != PORTABLE_MESH_ITEM_MOTION_OK)
                return status;
        }
        if (s.mode == PORTABLE_MESH_ITEM_SHELL &&
            (grounded || touched_ground || s.velocity_q16[2] <= 0))
        {
            status = support(&s, rules, course, cutoff,
                             (grounded || touched_ground) ? rules->shell_ground_probe_q16
                                                          : rules->shell_ground_snap_q16,
                             &events);
            if (status != PORTABLE_MESH_ITEM_MOTION_OK)
                return status;
        }
    }
    if (!state_valid(&s))
        return PORTABLE_MESH_ITEM_MOTION_INVALID;
    if (was_grounded)
        events.landed = 0;
    *state = s;
    *out = events;
    return PORTABLE_MESH_ITEM_MOTION_OK;
}
PortableMeshItemMotionStatus portable_mesh_item_motion_tick_diagnostic(
    PortableMeshItemMotionState *state, const PortableMeshItemRules *rules,
    const PortableMeshCourse *course, int32_t cutoff, PortableMeshItemMotionEvents *events,
    PortableMeshItemMotionFault *fault)
{
    PortableMeshItemMotionFault detail = {0};
    const PortableMeshItemMotionStatus status =
        tick_internal(state, rules, course, cutoff, events, &detail);
    if (status == PORTABLE_MESH_ITEM_MOTION_COLLISION_BUDGET && fault)
        *fault = detail;
    return status;
}
PortableMeshItemMotionStatus portable_mesh_item_motion_tick(PortableMeshItemMotionState *state,
                                                            const PortableMeshItemRules *rules,
                                                            const PortableMeshCourse *course,
                                                            int32_t cutoff,
                                                            PortableMeshItemMotionEvents *events)
{
    return portable_mesh_item_motion_tick_diagnostic(state, rules, course, cutoff, events, NULL);
}
const char *portable_mesh_item_motion_fault_phase_name(PortableMeshItemMotionFaultPhase phase)
{
    switch (phase)
    {
    case PORTABLE_MESH_ITEM_FAULT_OVERLAP:
        return "OVERLAP";
    case PORTABLE_MESH_ITEM_FAULT_SWEEP:
        return "SWEEP";
    case PORTABLE_MESH_ITEM_FAULT_SUBSTEPS:
        return "SUBSTEPS";
    default:
        return "NONE";
    }
}
const char *portable_mesh_item_motion_status_name(PortableMeshItemMotionStatus status)
{
    switch (status)
    {
    case PORTABLE_MESH_ITEM_MOTION_OK:
        return "OK";
    case PORTABLE_MESH_ITEM_MOTION_INVALID:
        return "INVALID";
    case PORTABLE_MESH_ITEM_MOTION_OUTSIDE:
        return "OUTSIDE";
    case PORTABLE_MESH_ITEM_MOTION_COLLISION_BUDGET:
        return "COLLISION_BUDGET";
    case PORTABLE_MESH_ITEM_MOTION_BOUNCE_LIMIT:
        return "BOUNCE_LIMIT";
    default:
        return "UNKNOWN";
    }
}
