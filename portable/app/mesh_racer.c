#include "mesh_racer.h"
#include "mesh_collision_index.h"
#include "fixed_3d.h"
#include <stdio.h>
#include <string.h>
#define U PORTABLE_FIXED_3D_UNIT
static PortableMeshStatus fail(const PortableMeshRacer *s, PortableMeshStatus status, char *error,
                               size_t capacity, const char *detail)
{
    if (error && capacity)
        snprintf(error, capacity,
                 "3D %s course=%u tick=%u: %s pos=%ld,%ld,%ld vel=%ld,%ld,%ld contact=%u/%u (%s)",
                 s && s->mode == 16u  ? "Battle"
                 : s && s->mode == 8u ? "VS"
                 : s && s->mode == 4u ? "Quick"
                 : s && s->mode == 1u ? "GP"
                                      : "TT",
                 s && s->course ? (unsigned)s->course->id : 0u, s ? (unsigned)s->state.tick : 0u,
                 portable_mesh_status_name(status), s ? (long)s->state.kart.position_q16[0] : 0L,
                 s ? (long)s->state.kart.position_q16[1] : 0L,
                 s ? (long)s->state.kart.position_q16[2] : 0L,
                 s ? (long)s->state.kart.velocity_q16[0] : 0L,
                 s ? (long)s->state.kart.velocity_q16[1] : 0L,
                 s ? (long)s->state.kart.velocity_q16[2] : 0L,
                 s ? (unsigned)s->state.kart.contact_triangle : UINT32_MAX,
                 s ? (unsigned)s->state.kart.contact_surface : UINT32_MAX, detail);
    return status;
}
static int course_triangle(const void *context, uint32_t index, PortableMeshTriangle *out)
{
    const PortableCourse3DView *course = context;
    PortableCourse3DCollision row;
    /* Moving rows need the collision index's posed course. */
    if (!portable_course_3d_collision(course, index, &row) || row.owner_object != PORTABLE_COURSE_3D_ABSENT)
        return 0;
    out->surface = row.surface;
    out->respawn = row.respawn;
    for (unsigned i = 0; i < 3u; ++i)
        if (!portable_course_3d_collision_vertex(course, row.vertices[i], out->vertices_q16[i]))
            return 0;
    return 1;
}
void portable_mesh_surface_from_course(const PortableCourse3DSurface *row, PortableMeshSurface *out)
{
    *out = (PortableMeshSurface){.type = row->type, .friction_q16 = row->friction_q16,
                                 .speed_ratio_q16 = row->speed_ratio_q16,
                                 .rolling_resistance_q16 = row->rolling_resistance_q16,
                                 .restitution_q16 = row->restitution_q16, .boost_q16 = row->boost_q16,
                                 .launch_q16 = row->launch_q16, .gravity_ratio_q16 = row->gravity_ratio_q16,
                                 .jump_max_distance_q16 = row->jump_max_distance_q16};
    memcpy(out->cannon_target_q16, row->cannon_target_q16, sizeof out->cannon_target_q16);
    memcpy(out->cannon_heading_q16, row->cannon_heading_q16, sizeof out->cannon_heading_q16);
    out->one_sided = (uint8_t)row->one_sided;
    out->surface_gravity = (uint8_t)row->surface_gravity;
    out->surface_class = (uint8_t)row->surface_class;
    out->trick = (uint8_t)row->trick;
    out->glide = (uint8_t)row->glide;
}
static int course_surface(const void *context, uint32_t index, PortableMeshSurface *out)
{
    PortableCourse3DSurface row;
    if (!portable_course_3d_surface(context, index, &row))
        return 0;
    portable_mesh_surface_from_course(&row, out);
    return 1;
}
static void quaternion_forward(const int32_t q[4], int32_t f[3])
{
    /* Rotate source -Y by the normalized XYZW grid quaternion. */ f[0] =
        (int32_t)(-2 * ((int64_t)q[0] * q[1] - (int64_t)q[2] * q[3]) / U);
    f[1] = (int32_t)(-U + 2 * ((int64_t)q[0] * q[0] + (int64_t)q[2] * q[2]) / U);
    f[2] = (int32_t)(-2 * ((int64_t)q[1] * q[2] + (int64_t)q[0] * q[3]) / U);
}
static int finish_gate(const PortableCourse3DView *course, uint32_t *finish)
{
    const PortableCourse3DTable *table =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('C', 'P', 'N', 'T'));
    if (!table || !table->count || table->count > 1024u)
        return 0;
    uint32_t found = PORTABLE_MESH_ABSENT;
    for (uint32_t i = 0; i < table->count; ++i)
    {
        PortableCourse3DCheckpoint gate;
        if (!portable_course_3d_checkpoint(course, i, &gate))
            return 0;
        if (gate.flags & 1u)
        {
            if (found != PORTABLE_MESH_ABSENT)
                return 0;
            found = i;
        }
    }
    if (found == PORTABLE_MESH_ABSENT)
        return 0;
    *finish = found;
    return 1;
}
static int ordered_gates(const PortableCourse3DView *course, uint32_t *finish)
{
    if (!finish_gate(course, finish))
        return 0;
    const PortableCourse3DTable *table =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('C', 'P', 'N', 'T'));
    uint32_t groups[1024] = {0};
    uint8_t seen[1024] = {0};
    uint32_t last = 0;
    for (uint32_t i = 0; i < table->count; ++i)
    {
        PortableCourse3DCheckpoint gate;
        if (!portable_course_3d_checkpoint(course, i, &gate) || (gate.flags & 2u) ||
            gate.stage >= table->count || (i == *finish && gate.stage != 0u) ||
            (i != *finish && gate.stage == 0u))
            return 0;
        if (seen[gate.stage] && groups[gate.stage] != gate.key_group)
            return 0;
        seen[gate.stage] = 1;
        groups[gate.stage] = gate.key_group;
        if (gate.stage > last)
            last = gate.stage;
    }
    for (uint32_t stage = 0; stage <= last; ++stage)
    {
        if (!seen[stage])
            return 0;
        for (uint32_t prior = 0; prior < stage; ++prior)
            if (groups[prior] == groups[stage])
                return 0;
    }
    for (uint32_t i = 0; i < table->count; ++i)
    {
        PortableCourse3DCheckpoint gate;
        if (!portable_course_3d_checkpoint(course, i, &gate) || !gate.edge_count)
            return 0;
        for (uint32_t e = 0; e < gate.edge_count; ++e)
        {
            uint32_t target;
            PortableCourse3DCheckpoint successor;
            if (!portable_course_3d_checkpoint_edge(course, gate.edge_first + e, &target) ||
                !portable_course_3d_checkpoint(course, target, &successor))
                return 0;
            if (gate.stage == last)
            {
                if (target != *finish)
                    return 0;
            }
            else if (successor.stage != gate.stage + 1u)
                return 0;
        }
    }
    return 1;
}
PortableMeshStatus portable_mesh_racer_init(PortableMeshRacer *session,
                                            const PortableCourse3DView *course,
                                            const PortableMeshKartRules *rules, uint32_t grid,
                                            uint32_t mode, char *error, size_t capacity)
{
    if (!session || !course || (mode != 1u && mode != 2u && mode != 4u && mode != 8u && mode != 16u) ||
        !portable_mesh_kart_rules_valid(rules))
        return fail(NULL, PORTABLE_MESH_INVALID, error, capacity, "missing course/profile");
    PortableMeshRacer next;
    memset(&next, 0, sizeof(next));
    next.course = course;
    next.rules = rules;
    next.spawn_grid = grid;
    next.mode = mode;
    PortableCourse3DMeta meta;
    PortableCourse3DGrid spawn;
    const PortableCourse3DTable *triangles =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('C', 'O', 'L', 'T'));
    if (!portable_course_3d_meta(course, &meta) || !(meta.modes & mode) ||
        !portable_course_3d_has_physics_profile(course, rules->profile_id) || !triangles ||
        !portable_course_3d_grid(course, grid, &spawn))
        return fail(&next, PORTABLE_MESH_INVALID, error, capacity, "course mode/profile/grid");
    for (uint32_t i = 0; i < triangles->count; ++i)
    {
        PortableCourse3DCollision row;
        if (!portable_course_3d_collision(course, i, &row))
            return fail(&next, PORTABLE_MESH_INVALID, error, capacity, "collision row");
        if (row.gravity_zone != PORTABLE_COURSE_3D_ABSENT)
            return fail(&next, PORTABLE_MESH_UNSUPPORTED, error, capacity, "referenced gravity zone");
        PortableCourse3DSurface surface;
        if (!portable_course_3d_surface(course, row.surface, &surface) ||
            surface.friction_q16 > 262144 || surface.speed_ratio_q16 > 262144 ||
            surface.gravity_ratio_q16 > 262144 || surface.rolling_resistance_q16 > 67108864 ||
            surface.launch_q16 > 16777216 ||
            (int64_t)surface.speed_ratio_q16 * rules->forward_speed_q16 / 65536 > 16777216)
            return fail(&next, PORTABLE_MESH_UNSUPPORTED, error, capacity,
                        "surface coefficient outside T1 bounds");
    }
    if (meta.gravity_q16 < 0 || meta.gravity_q16 > 67108864)
        return fail(&next, PORTABLE_MESH_UNSUPPORTED, error, capacity, "gravity outside T1 bounds");
    const PortableCourse3DTable *vertices =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('C', 'O', 'L', 'V'));
    if (!vertices || !vertices->count)
        return fail(&next, PORTABLE_MESH_INVALID, error, capacity, "missing collision positions");
    int32_t minimum = PORTABLE_FIXED_3D_POSITION_LIMIT;
    for (uint32_t i = 0; i < vertices->count; ++i)
    {
        int32_t position[3];
        if (!portable_course_3d_collision_vertex(course, i, position))
            return fail(&next, PORTABLE_MESH_INVALID, error, capacity, "collision position");
        if (position[2] < minimum)
            minimum = position[2];
    }
    int64_t fall_limit = (int64_t)minimum - 20 * 65536;
    /* Optional META fixed `mksc:fall-height`: a kart below this world height
     * is out of the course (water or a void), as authored. */
    PortableCourse3DProperty fall;
    if (portable_course_3d_property_find(course, meta.property_first, meta.property_count,
                                         "mksc:fall-height", &fall))
    {
        if (fall.type != PORTABLE_COURSE_3D_PROPERTY_FIXED)
            return fail(&next, PORTABLE_MESH_INVALID, error, capacity, "mksc:fall-height must be fixed");
        fall_limit = fall.value.fixed_q16;
    }
    next.fall_limit_q16 = fall_limit < -PORTABLE_FIXED_3D_POSITION_LIMIT
                              ? -PORTABLE_FIXED_3D_POSITION_LIMIT
                              : (int32_t)fall_limit;
    uint32_t finish = PORTABLE_MESH_ABSENT;
    if (mode != 16u && !ordered_gates(course, &finish))
        return fail(&next, PORTABLE_MESH_UNSUPPORTED, error, capacity,
                    "ordered lap stages/key groups/start-finish (section finishes unsupported)");
    int32_t forward[3];
    quaternion_forward(spawn.quaternion_q30, forward);
    spawn.position_q16[2] += rules->kart_radius_q16 + rules->ride_height_q16;
    const PortableMeshStatus status =
        portable_mesh_kart_init(&next.state.kart, rules, spawn.position_q16, forward);
    if (status != PORTABLE_MESH_OK)
        return fail(&next, status, error, capacity, "initial pose");
    next.state.phase = PORTABLE_MESH_RACER_COUNTDOWN;
    next.state.countdown_ticks = 180;
    next.state.last_respawn = PORTABLE_MESH_ABSENT;
    next.state.checkpoint = finish;
    memcpy(next.state.previous_position_q16, next.state.kart.position_q16,
           sizeof(next.state.previous_position_q16));
    *session = next;
    if (error && capacity)
        error[0] = 0;
    return PORTABLE_MESH_OK;
}
static int crossed(const PortableCourse3DCheckpoint *gate, const int32_t before[3],
                   const int32_t after[3], int64_t *time)
{
    int32_t a[3], b[3];
    for (unsigned i = 0; i < 3u; ++i)
    {
        a[i] = before[i] - gate->center_q16[i];
        b[i] = after[i] - gate->center_q16[i];
    }
    const int64_t da = portable_fixed_3d_dot(a, gate->normal_q30) / U,
                  db = portable_fixed_3d_dot(b, gate->normal_q30) / U;
    const int forward = da < 0 && db >= 0;
    const int backward = !(gate->flags & 4u) && da > 0 && db <= 0;
    if (!forward && !backward)
        return 0;
    *time = (-da) * 1073741824 / (db - da);
    /* Plane times stay between zero and one; division before multiplication bounds products. */
    int32_t p[3];
    const int64_t denominator = db - da;
    for (unsigned i = 0; i < 3u; ++i)
        p[i] = a[i] + (int32_t)(((int64_t)b[i] - a[i]) * (-da) / denominator);
    const int64_t right = portable_fixed_3d_dot(p, gate->right_q30) / U;
    int32_t up[3];
    up[0] = (int32_t)(((int64_t)gate->normal_q30[1] * gate->right_q30[2] -
                       (int64_t)gate->normal_q30[2] * gate->right_q30[1]) /
                      U);
    up[1] = (int32_t)(((int64_t)gate->normal_q30[2] * gate->right_q30[0] -
                       (int64_t)gate->normal_q30[0] * gate->right_q30[2]) /
                      U);
    up[2] = (int32_t)(((int64_t)gate->normal_q30[0] * gate->right_q30[1] -
                       (int64_t)gate->normal_q30[1] * gate->right_q30[0]) /
                      U);
    const int64_t height = portable_fixed_3d_dot(p, up) / U;
    return right >= -gate->half_width_q16 && right <= gate->half_width_q16 &&
           height >= -gate->half_height_q16 && height <= gate->half_height_q16;
}
int portable_mesh_racer_gate_crossing(const PortableCourse3DCheckpoint *gate,
    const int32_t before[3], const int32_t after[3], int64_t *time)
{
    if (!gate || !before || !after || !time || gate->half_width_q16 <= 0 ||
        gate->half_height_q16 <= 0 || gate->half_width_q16 > PORTABLE_FIXED_3D_POSITION_LIMIT ||
        gate->half_height_q16 > PORTABLE_FIXED_3D_POSITION_LIMIT)
        return 0;
    for (unsigned i = 0; i < 3; ++i)
        if (before[i] < -PORTABLE_FIXED_3D_POSITION_LIMIT || before[i] > PORTABLE_FIXED_3D_POSITION_LIMIT ||
            after[i] < -PORTABLE_FIXED_3D_POSITION_LIMIT || after[i] > PORTABLE_FIXED_3D_POSITION_LIMIT ||
            gate->center_q16[i] < -PORTABLE_FIXED_3D_POSITION_LIMIT || gate->center_q16[i] > PORTABLE_FIXED_3D_POSITION_LIMIT ||
            gate->normal_q30[i] < -U || gate->normal_q30[i] > U ||
            gate->right_q30[i] < -U || gate->right_q30[i] > U)
            return 0;
    const int64_t one = INT64_C(1) << 60, tolerance = INT64_C(1) << 50;
    const int64_t normal_length = portable_fixed_3d_dot(gate->normal_q30, gate->normal_q30);
    const int64_t right_length = portable_fixed_3d_dot(gate->right_q30, gate->right_q30);
    const int64_t perpendicular = portable_fixed_3d_dot(gate->normal_q30, gate->right_q30);
    if (normal_length < one - tolerance || normal_length > one + tolerance ||
        right_length < one - tolerance || right_length > one + tolerance ||
        perpendicular < -tolerance || perpendicular > tolerance || (gate->flags & ~7u))
        return 0;
    int64_t candidate;
    if (!crossed(gate, before, after, &candidate))
        return 0;
    *time = candidate;
    return 1;
}

static PortableMeshStatus checkpoints(PortableMeshRacer *s, PortableMeshRacerTurn *turn)
{
    const PortableCourse3DTable *table =
        portable_course_3d_table(s->course, PORTABLE_COURSE_3D_TAG('C', 'P', 'N', 'T'));
    if (!table || table->count > 1024u)
        return PORTABLE_MESH_INVALID;
    uint8_t visited[1024] = {0};
    int64_t last_time = -1;
    for (uint32_t accepted = 0; accepted < table->count; ++accepted)
    {
        PortableCourse3DCheckpoint previous, chosen = {0};
        uint32_t target = PORTABLE_MESH_ABSENT;
        int64_t best = INT64_MAX;
        if (!portable_course_3d_checkpoint(s->course, s->state.checkpoint, &previous))
            return PORTABLE_MESH_INVALID;
        for (uint32_t edge = 0; edge < previous.edge_count; ++edge)
        {
            uint32_t index;
            PortableCourse3DCheckpoint gate;
            int64_t time;
            if (!portable_course_3d_checkpoint_edge(s->course, previous.edge_first + edge,
                                                    &index) ||
                index >= table->count || !portable_course_3d_checkpoint(s->course, index, &gate))
                return PORTABLE_MESH_INVALID;
            if (visited[index] ||
                !crossed(&gate, s->state.previous_position_q16, s->state.kart.position_q16,
                         &time) ||
                time < last_time || time >= best)
                continue;
            target = index;
            chosen = gate;
            best = time;
        }
        if (target == PORTABLE_MESH_ABSENT)
            break;
        visited[target] = 1;
        last_time = best;
        s->state.checkpoint = target;
        s->state.checkpoint_count = chosen.stage;
        if (chosen.flags & 1u)
        {
            if (s->state.crossing_armed)
            {
                ++s->state.lap;
                s->state.crossing_armed = 0;
            }
            else
                s->state.crossing_armed = 1;
            PortableCourse3DMeta meta;
            if (!portable_course_3d_meta(s->course, &meta))
                return PORTABLE_MESH_INVALID;
            if (s->state.lap >= meta.laps)
            {
                s->state.phase = PORTABLE_MESH_RACER_FINISHED;
                s->state.finish_ticks = s->state.race_ticks;
                turn->finish_fraction_q30 = (uint32_t)best;
                break;
            }
        }
        else
            s->state.crossing_armed = 1;
    }
    return PORTABLE_MESH_OK;
}
static int equivalent_recovery_checkpoint(const PortableCourse3DView *course,
    uint32_t current_index, uint32_t safe_index);

static PortableMeshStatus respawn(PortableMeshRacer *s)
{
    int32_t position[3], forward[3];
    uint32_t checkpoint = s->state.checkpoint;
    PortableCourse3DRespawn point;
    if (s->state.last_respawn != PORTABLE_MESH_ABSENT)
    {
        if (!portable_course_3d_respawn(s->course, s->state.last_respawn, &point))
            return PORTABLE_MESH_INVALID;
        if (s->mode != 16u)
        {
            PortableCourse3DCheckpoint current, safe;
            if (!portable_course_3d_checkpoint(s->course, checkpoint, &current) ||
                !portable_course_3d_checkpoint(s->course, point.checkpoint, &safe))
                return PORTABLE_MESH_INVALID;
            const int equivalent = current.stage == safe.stage
                ? equivalent_recovery_checkpoint(s->course, checkpoint, point.checkpoint) : 0;
            if (equivalent < 0)
                return PORTABLE_MESH_INVALID;
            /* A saved anchor may belong to the previous lap. It can rewind
             * earned progress, never award a future stage or another branch. */
            if (safe.stage > current.stage || (safe.stage == current.stage && !equivalent))
                s->state.last_respawn = PORTABLE_MESH_ABSENT;
        }
    }
    if (s->state.last_respawn != PORTABLE_MESH_ABSENT)
    {
        memcpy(position, point.position_q16, sizeof(position));
        quaternion_forward(point.quaternion_q30, forward);
        checkpoint = point.checkpoint;
    }
    else
    {
        PortableCourse3DGrid point;
        if (!portable_course_3d_grid(s->course, s->spawn_grid, &point))
            return PORTABLE_MESH_INVALID;
        memcpy(position, point.position_q16, sizeof(position));
        quaternion_forward(point.quaternion_q30, forward);
        if (s->mode != 16u && !finish_gate(s->course, &checkpoint))
            return PORTABLE_MESH_INVALID;
    }
    position[2] +=
        s->rules->kart_radius_q16 + s->rules->ride_height_q16 + s->rules->respawn_lift_q16;
    PortableMeshStatus status =
        portable_mesh_kart_init(&s->state.kart, s->rules, position, forward);
    if (status != PORTABLE_MESH_OK)
        return status;
    s->state.kart.respawn_ticks = s->rules->respawn_ticks;
    s->state.respawn_grace = s->rules->respawn_grace_ticks;
    s->state.checkpoint = s->mode == 16u ? PORTABLE_MESH_ABSENT : checkpoint;
    if (s->mode == 16u)
    {
        s->state.checkpoint_count = s->state.crossing_armed = 0;
        memcpy(s->state.previous_position_q16, position, sizeof(position));
        return PORTABLE_MESH_OK;
    }
    PortableCourse3DCheckpoint gate;
    if (!portable_course_3d_checkpoint(s->course, checkpoint, &gate))
        return PORTABLE_MESH_INVALID;
    s->state.checkpoint_count = gate.stage;
    s->state.crossing_armed = gate.stage != 0u;
    memcpy(s->state.previous_position_q16, position, sizeof(position));
    return PORTABLE_MESH_OK;
}

PortableMeshStatus portable_mesh_racer_respawn(PortableMeshRacer *racer, char *error, size_t capacity)
{
    if (!racer || !racer->course || !portable_mesh_kart_rules_valid(racer->rules) ||
        racer->state.phase != PORTABLE_MESH_RACER_RACING)
        return fail(racer, PORTABLE_MESH_INVALID, error, capacity, "recovery owner");
    PortableMeshRacer next = *racer;
    /* Revenge-Bomb retrieval is identity-indexed in source E7F0. A shared
     * floor RESP belongs to ordinary fall recovery, not Bomb re-entry. */
    if (next.mode == 16u)
        next.state.last_respawn = PORTABLE_MESH_ABSENT;
    const PortableMeshStatus status = respawn(&next);
    if (status != PORTABLE_MESH_OK)
        return fail(racer, status, error, capacity, "authored recovery point");
    *racer = next;
    return status;
}

static int gravity_point(const PortableCourse3DView *course, uint32_t zone, uint32_t kind,
                         PortableMeshGravityPoint *out, uint32_t *next, uint8_t *anti)
{
    PortableCourse3DGravityPoint point;
    if (!portable_course_3d_gravity_point(course, zone, &point) || point.kind != kind)
        return 0;
    const int64_t down[3] = {-(int64_t)point.gravity_q16[0], -(int64_t)point.gravity_q16[1],
                             -(int64_t)point.gravity_q16[2]};
    memcpy(out->position_q16, point.position_q16, sizeof out->position_q16);
    out->radius_q16 = point.radius_q16;
    *next = point.next;
    *anti = (uint8_t)point.anti_gravity;
    return portable_fixed_3d_normalize(down, out->up_q30);
}
/* MK8 GravityPath (0x7b839c): the nearest kind-1 ZONE segment around the
 * kart sets its gravity up and, from the segment's first point, whether it
 * drives anti-gravity; off every path it keeps world gravity. */
static void gravity_path(const PortableCourse3DView *course, PortableMeshKartState *kart)
{
    const PortableCourse3DTable *zones = portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('Z', 'O', 'N', 'E'));
    uint64_t best = UINT64_MAX;
    memset(kart->path_up_q30, 0, sizeof kart->path_up_q30);
    kart->anti_gravity = 0;
    for (uint32_t zone = 0; zones && zone < zones->count; ++zone)
    {
        PortableMeshGravityPoint from, to;
        uint32_t next, after;
        uint8_t anti, unused;
        uint64_t distance;
        int32_t up[3];
        if (!gravity_point(course, zone, 1u, &from, &next, &anti) || next == PORTABLE_COURSE_3D_ABSENT ||
            !gravity_point(course, next, 1u, &to, &after, &unused) ||
            !portable_mesh_gravity_segment(&from, &to, kart->position_q16, &distance, up) || distance >= best)
            continue;
        best = distance;
        memcpy(kart->path_up_q30, up, sizeof up);
        kart->anti_gravity = anti;
    }
}
/* MK8 GlidePath: the nearest kind-2 ZONE segment, whose first point says
 * whether it is a cannon path, steers a glide; off every path none does. */
static void glide_path(const PortableCourse3DView *course, PortableMeshKartState *kart)
{
    const PortableCourse3DTable *zones = portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('Z', 'O', 'N', 'E'));
    uint64_t best = UINT64_MAX;
    memset(&kart->glide_path, 0, sizeof kart->glide_path);
    for (uint32_t zone = 0; zones && zone < zones->count; ++zone)
    {
        PortableMeshGravityPoint from, to;
        uint32_t next, after;
        uint8_t cannon, unused;
        uint64_t distance;
        int32_t closest[3], tangent[3], up[3], radius;
        if (!gravity_point(course, zone, 2u, &from, &next, &cannon) || next == PORTABLE_COURSE_3D_ABSENT ||
            !gravity_point(course, next, 2u, &to, &after, &unused) ||
            !portable_mesh_glide_segment(&from, &to, kart->position_q16, &distance, closest, tangent, up, &radius) ||
            distance >= best)
            continue;
        best = distance;
        memcpy(kart->glide_path.closest_q16, closest, sizeof closest);
        memcpy(kart->glide_path.tangent_q30, tangent, sizeof tangent);
        memcpy(kart->glide_path.up_q30, up, sizeof up);
        kart->glide_path.radius_q16 = radius;
        kart->glide_path.found = 1;
        kart->glide_path.cannon = cannon;
    }
}
static PortableMeshStatus move_effects(PortableMeshRacer *session, PortableMeshKartInput input,
    const PortableMeshKartEffects *effects, PortableMeshRacerTurn *turn,
    char *error, size_t capacity)
{
    if (!session || !turn || !session->course || input.buttons > 15u ||
        !portable_mesh_kart_rules_valid(session->rules))
        return fail(session, PORTABLE_MESH_INVALID, error, capacity, "unprepared session");
    memset(turn, 0, sizeof(*turn));
    turn->finish_fraction_q30 = UINT32_MAX;
    PortableMeshRacerState *s = &session->state;
    ++s->tick;
    if (s->phase == PORTABLE_MESH_RACER_COUNTDOWN)
    {
        if (s->countdown_ticks)
            --s->countdown_ticks;
        portable_mesh_kart_countdown(&s->kart, input.buttons);
        if (!s->countdown_ticks)
        {
            s->phase = PORTABLE_MESH_RACER_RACING;
            portable_mesh_kart_go(&s->kart);
        }
        s->kart.previous_buttons = input.buttons;
    }
    else if (s->phase == PORTABLE_MESH_RACER_RACING)
    {
        ++s->race_ticks;
        if (s->kart.respawn_ticks)
        {
            --s->kart.respawn_ticks;
            s->kart.previous_buttons = input.buttons;
        }
        else
        {
            PortableMeshCourse linear;
            const PortableMeshCourse *geometry = session->geometry;
            if (!geometry)
            {
                PortableCourse3DMeta meta;
                const PortableCourse3DTable *triangles = portable_course_3d_table(
                    session->course, PORTABLE_COURSE_3D_TAG('C', 'O', 'L', 'T'));
                if (!portable_course_3d_meta(session->course, &meta) || !triangles)
                    return fail(session, PORTABLE_MESH_INVALID, error, capacity, "mesh metadata");
                linear = (PortableMeshCourse){session->course, triangles->count, meta.gravity_q16,
                                               course_triangle, course_surface, NULL, NULL};
                geometry = &linear;
            }
            PortableMeshCollisionPose posed;
            geometry = portable_mesh_collision_index_at(geometry, s->tick, &posed);
            const int rescue = (input.buttons & PORTABLE_MESH_RESCUE) &&
                               !(s->kart.previous_buttons & PORTABLE_MESH_RESCUE);
            memcpy(s->previous_position_q16, s->kart.position_q16,
                   sizeof(s->previous_position_q16));
            gravity_path(session->course, &s->kart);
            glide_path(session->course, &s->kart);
            PortableMeshStatus status = rescue
                                            ? PORTABLE_MESH_OUTSIDE
                                            : portable_mesh_kart_tick_effects(&s->kart, session->rules,
                                                                      geometry, input, effects, &turn->events);
            if (status == PORTABLE_MESH_OUTSIDE ||
                s->kart.position_q16[2] < session->fall_limit_q16 || s->kart.air_ticks > 600u)
                status = respawn(session);
            else if (status == PORTABLE_MESH_OK)
            {
                turn->publish = 1;
                turn->contact = s->respawn_grace == 0u;
                int32_t centers[PORTABLE_MESH_COLLISION_HAZARD_LIMIT][3], radii[PORTABLE_MESH_COLLISION_HAZARD_LIMIT];
                uint8_t reactions[PORTABLE_MESH_COLLISION_HAZARD_LIMIT];
                const uint32_t hazards =
                    turn->contact ? portable_mesh_collision_hazards_near(geometry, s->tick, s->kart.position_q16, centers, radii,
                                                                         reactions) : 0u;
                for (uint32_t h = 0; h < hazards && !turn->events.hazard; ++h)
                {
                    if (!radii[h])
                        continue;
                    if (reactions[h] == PORTABLE_MESH_HAZARD_SPIN_BUMPER)
                    {
                        if (portable_mesh_kart_spin_bumper(&s->kart, session->rules, centers[h], radii[h]) < 0)
                            return fail(session, PORTABLE_MESH_INVALID, error, capacity, "spin bumper contact");
                        continue;
                    }
                    const int struck = portable_mesh_kart_hazard(&s->kart, session->rules, centers[h], radii[h]);
                    if (struck < 0 || (struck && portable_mesh_kart_hazard_reaction(&s->kart, reactions[h]) < 0))
                        return fail(session, PORTABLE_MESH_INVALID, error, capacity, "hazard contact");
                    turn->events.hazard = (uint8_t)struck;
                    turn->events.hazard_reaction = struck ? reactions[h] : 0u;
                }
            }
            if (status != PORTABLE_MESH_OK)
                return fail(session, status, error, capacity, "physics/checkpoint");
        }
    }
    if (error && capacity)
        error[0] = 0;
    return PORTABLE_MESH_OK;
}
PortableMeshStatus portable_mesh_racer_move_effects(PortableMeshRacer *session,
    PortableMeshKartInput input, const PortableMeshKartEffects *effects,
    PortableMeshRacerTurn *turn, char *error, size_t capacity)
{
    if (!session || !turn || !portable_mesh_kart_effects_valid(session->rules, effects))
        return fail(session, PORTABLE_MESH_INVALID, error, capacity, "invalid dynamics effects");
    PortableMeshRacer next;
    memcpy(&next, session, sizeof next);
    PortableMeshRacerTurn result;
    const PortableMeshStatus status = move_effects(&next, input, effects, &result, error, capacity);
    if (status == PORTABLE_MESH_OK)
    {
        memcpy(session, &next, sizeof next);
        memcpy(turn, &result, sizeof result);
    }
    return status;
}

PortableMeshStatus portable_mesh_racer_move(PortableMeshRacer *session, PortableMeshKartInput input,
    PortableMeshRacerTurn *turn, char *error, size_t capacity)
{
    return portable_mesh_racer_move_effects(session, input, NULL, turn, error, capacity);
}

/* Parallel finite apertures share recovery authority only when restoring the
 * canonical checkpoint also preserves the authored next-checkpoint choices. */
static int equivalent_recovery_checkpoint(const PortableCourse3DView *course,
    uint32_t current_index, uint32_t safe_index)
{
    if (current_index == safe_index)
        return 1;
    PortableCourse3DCheckpoint current, safe;
    if (!portable_course_3d_checkpoint(course, current_index, &current) ||
        !portable_course_3d_checkpoint(course, safe_index, &safe))
        return -1;
    if (current.stage != safe.stage || current.key_group != safe.key_group)
        return 0;
    const PortableCourse3DTable *checkpoints =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('C', 'P', 'N', 'T'));
    if (!checkpoints || checkpoints->count > 1024u ||
        current.edge_count > 16384u || safe.edge_count > 16384u)
        return -1;
    uint8_t targets[2][128] = {{0}};
    const PortableCourse3DCheckpoint *gates[2] = {&current, &safe};
    for (unsigned side = 0; side < 2u; ++side)
        for (uint32_t edge = 0; edge < gates[side]->edge_count; ++edge)
        {
            uint32_t target;
            if (gates[side]->edge_first > UINT32_MAX - edge ||
                !portable_course_3d_checkpoint_edge(course, gates[side]->edge_first + edge,
                                                    &target) ||
                target >= checkpoints->count)
                return -1;
            targets[side][target / 8u] |= (uint8_t)(1u << (target % 8u));
        }
    return memcmp(targets[0], targets[1], sizeof targets[0]) == 0;
}

PortableMeshStatus portable_mesh_racer_publish(PortableMeshRacer *session,
                                               PortableMeshRacerTurn *turn, char *error,
                                               size_t capacity)
{
    if (!session || !turn)
        return fail(session, PORTABLE_MESH_INVALID, error, capacity, "unprepared publication");
    PortableMeshRacerState *s = &session->state;
    PortableMeshStatus status = PORTABLE_MESH_OK;
    if (turn->publish)
    {
        if (s->respawn_grace)
            --s->respawn_grace;
        /* Grace protects against contacts, not ordinary gate crossings. The
         * teleport and stationary penalty never publish a movement sweep. */
        if (session->mode != 16u)
            status = checkpoints(session, turn);
        if (status == PORTABLE_MESH_OK && s->kart.grounded &&
            s->kart.respawn_index != PORTABLE_MESH_ABSENT)
        {
            PortableCourse3DRespawn safe;
            if (!portable_course_3d_respawn(session->course, s->kart.respawn_index, &safe))
                status = PORTABLE_MESH_INVALID;
            else
            {
                const int equivalent = session->mode == 16u ? 1 :
                    equivalent_recovery_checkpoint(session->course, s->checkpoint,
                                                   safe.checkpoint);
                if (equivalent < 0)
                    status = PORTABLE_MESH_INVALID;
                else if (equivalent)
                    s->last_respawn = s->kart.respawn_index;
            }
        }
    }
    if (status != PORTABLE_MESH_OK)
        return fail(session, status, error, capacity, "physics/checkpoint");
    if (error && capacity)
        error[0] = 0;
    return PORTABLE_MESH_OK;
}
