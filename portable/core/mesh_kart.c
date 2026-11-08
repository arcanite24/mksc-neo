#include "mesh_kart.h"
#include "mesh_query.h"
#include "fixed_3d.h"
#include "mesh_kart_mk8.h"
#include <string.h>
#define Q PORTABLE_FIXED_3D_ONE
#define U PORTABLE_FIXED_3D_UNIT
#define LIMIT PORTABLE_FIXED_3D_POSITION_LIMIT
#define MAX_STEPS 256u
#define MESH_KART_SEAM_Q16 2621 /* 4 cm: floor seams the support probe bridges */
typedef struct
{
    int32_t radius_q16, ride_height_q16;
} MeshKartShape;

int portable_mesh_kart_effects_valid(const PortableMeshKartRules *rules,
                                      const PortableMeshKartEffects *effects)
{
    if (!portable_mesh_kart_rules_valid(rules))
        return 0;
    if (!effects)
        return 1;
    if (effects->speed_ratio_q16 < 3277 || effects->speed_ratio_q16 > Q ||
        effects->body_scale_q16 < 3277 || effects->body_scale_q16 > Q ||
        effects->boost_cap_ratio_q16 < Q || effects->boost_cap_ratio_q16 > 4 * Q ||
        effects->boost_acceleration_q16 < 0 || effects->boost_acceleration_q16 > 1024 * Q ||
        effects->boost_active > 1u || effects->cancel_drift > 1u || effects->inhibit_controls > 1u ||
        effects->star > 1u ||
        (effects->star && (effects->star_cap_ratio_q16 < Q || effects->star_cap_ratio_q16 > 4 * Q ||
                           effects->star_acceleration_q16 < 0 || effects->star_acceleration_q16 > 1024 * Q)))
        return 0;
    int64_t base = (int64_t)rules->forward_speed_q16 * effects->speed_ratio_q16 / Q;
    if (effects->star)
        base = base * effects->star_cap_ratio_q16 / Q;
    if (base > 256 * Q)
        return 0;
    if (base < MESH_KART_MK8_FLOOR_CAP_Q16)
        base = MESH_KART_MK8_FLOOR_CAP_Q16;
    return !effects->boost_active || base * effects->boost_cap_ratio_q16 / Q <= 256 * Q;
}

static int read_surface(const PortableMeshCourse *course, uint32_t index, PortableMeshSurface *out)
{
    if (!course->surface(course->context,index,out))
        return 0;
    if (out->type == 9u)
    {
        /* Cannon: a reachable target and a horizontal heading. */
        int64_t heading = 0;
        for (unsigned i = 0; i < 3u; ++i)
        {
            if (out->cannon_target_q16[i] < -LIMIT || out->cannon_target_q16[i] > LIMIT)
                return 0;
            heading += (int64_t)out->cannon_heading_q16[i] * out->cannon_heading_q16[i];
        }
        if (out->launch_q16 < Q || out->launch_q16 > 256 * Q || out->cannon_heading_q16[2] ||
            heading < (int64_t)Q * Q / 2)
            return 0;
    }
    return out->surface_class < MESH_KART_MK8_CLASSES &&
           out->jump_max_distance_q16 >= 0 && out->jump_max_distance_q16 <= 1024*Q &&
           (!out->jump_max_distance_q16 || (out->type == 5u && out->gravity_ratio_q16 == Q));
}

static const PortableMeshKartEffects *nonidentity_effects(const PortableMeshKartEffects *effects)
{
    return effects && effects->speed_ratio_q16 == Q && effects->body_scale_q16 == Q &&
        !effects->boost_active && !effects->cancel_drift && !effects->inhibit_controls && !effects->star
        ? NULL : effects;
}

static MeshKartShape kart_shape(const PortableMeshKartRules *rules,
                                 const PortableMeshKartEffects *effects)
{
    const MeshKartShape shape = {
        effects ? portable_fixed_3d_mul(rules->kart_radius_q16, effects->body_scale_q16) :
                  rules->kart_radius_q16,
        effects ? portable_fixed_3d_mul(rules->ride_height_q16, effects->body_scale_q16) :
                  rules->ride_height_q16};
    return shape;
}

static int32_t unit_product(int32_t a, int32_t b)
{
    return (int32_t)((int64_t)a * b / U);
}
static const int32_t WORLD_UP[3] = {0, 0, U};
/* Gravity's up; with world up every use below reduces exactly to Z. */
static int nonzero(const int32_t v[3])
{
    return v[0] || v[1] || v[2];
}
/* Gravity's up: a surface-gravity floor's face, else the gravity path's up,
 * else world up (every use below then reduces exactly to Z). */
static const int32_t *gravity_up(const PortableMeshKartState *s)
{
    return nonzero(s->gravity_up_q30) ? s->gravity_up_q30 : nonzero(s->path_up_q30) ? s->path_up_q30 : WORLD_UP;
}
void portable_mesh_kart_gravity_up(const PortableMeshKartState *s, int32_t out_q30[3])
{
    memcpy(out_q30, gravity_up(s), 3u * sizeof(int32_t));
}
static int32_t up_part(const int32_t v[3], const int32_t up[3])
{
    return (int32_t)(portable_fixed_3d_dot(v, up) / U);
}
static void normalize(const int32_t in[3], int32_t out[3])
{
    const int64_t v[3] = {in[0], in[1], in[2]};
    (void)portable_fixed_3d_normalize(v, out);
}
static int32_t fraction(int64_t numerator, int64_t denominator)
{
    if (numerator <= 0)
        return 0;
    if (numerator >= denominator)
        return Q;
    uint64_t n = (uint64_t)numerator, d = (uint64_t)denominator;
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
    return (int32_t)result;
}
static uint64_t distance_squared(const int32_t a[3], const int32_t b[3])
{
    uint64_t sum = 0;
    for (unsigned i = 0; i < 3u; ++i)
    {
        const int64_t d = (int64_t)a[i] - b[i];
        sum += (uint64_t)(d * d);
    }
    return sum;
}
static void cancel_drift(PortableMeshKartState *s)
{
    s->drift_charge = 0;
    s->drift_tier = 0;
    s->drift_direction = 0;
    s->drift_ramp_q16 = 0;
}
/* MK8's split knock accumulators: per axis, knocks of one sign keep the
 * larger and opposite ones add, so simultaneous hits never stack. */
static void knock_combine(int32_t knock[3], const int32_t v[3])
{
    for (unsigned i = 0; i < 3u; ++i)
    {
        const int32_t high = knock[i] > v[i] ? knock[i] : v[i], low = knock[i] < v[i] ? knock[i] : v[i];
        knock[i] = (high > 0 ? high : 0) + (low < 0 ? low : 0);
    }
}
/* A knock holds traction 0.245 above the slide target for its ticks. */
static void knock_hold(PortableMeshKartState *s, int32_t slide, unsigned ticks)
{
    s->traction_hold_ticks = (uint8_t)ticks;
    s->traction_q16 = slide + MESH_KART_MK8_SLIDE_ADD_Q16 > Q ? Q : slide + MESH_KART_MK8_SLIDE_ADD_Q16;
}
/* The kart's side axis, pointing where a positive turn swings the nose. */
static void side_axis(const PortableMeshKartState *s, int32_t out[3])
{
    const int32_t *u = s->up_q30, *f = s->forward_q30;
    const int64_t side[3] = {((int64_t)u[1] * f[2] - (int64_t)u[2] * f[1]) / U,
                             ((int64_t)u[2] * f[0] - (int64_t)u[0] * f[2]) / U,
                             ((int64_t)u[0] * f[1] - (int64_t)u[1] * f[0]) / U};
    if (!portable_fixed_3d_normalize(side, out))
        out[0] = U, out[1] = out[2] = 0;
}
static int32_t response(int32_t rate)
{
    const int32_t n = portable_fixed_3d_dt(rate);
    return n > Q ? Q : n;
}
static void align_ground(PortableMeshKartState *s, const int32_t n[3],
                         const PortableMeshKartRules *r)
{
    const int32_t weight = response(r->normal_alignment_response_q16);
    int32_t up[3];
    for (unsigned i = 0; i < 3u; ++i)
        up[i] = portable_fixed_3d_clamp((int64_t)s->up_q30[i] +
                                        ((int64_t)n[i] - s->up_q30[i]) * weight / Q);
    normalize(up, s->up_q30);
    const int32_t along = (int32_t)(portable_fixed_3d_dot(s->forward_q30, n) / U);
    int32_t forward[3];
    for (unsigned i = 0; i < 3u; ++i)
        forward[i] = s->forward_q30[i] - unit_product(along, n[i]);
    normalize(forward, s->forward_q30);
}
/* Query bounds are rebuilt for every next ID: earlier triangle corrections
 * may move the endpoint into a previously distant, later canonical face. */
static int nearby_triangle(const PortableMeshCourse *course, const int32_t before[3],
                           const int32_t after[3], int64_t extent, uint32_t *first, uint32_t *out)
{
    int32_t low[3], high[3];
    if (!portable_mesh_query_bounds(before, after, extent, low, high))
        return PORTABLE_MESH_QUERY_INVALID;
    const PortableMeshQueryStatus result =
        portable_mesh_query_next_triangle(course, low, high, *first, out);
    if (result == PORTABLE_MESH_QUERY_HIT)
        *first = *out + 1u;
    return result;
}
/* The tick's most head-on wall contact: its normal, and the drive plus
 * knock velocity into it and in all (m/s) before sliding. */
typedef struct
{
    int hit;
    int32_t n[3], approach_q16, motion_q16;
} MeshKartWall;
/* The floor the sweep's last step rests on; the kart aligns to it once a
 * tick, so its rate does not grow with speed (sub-steps) or touching faces. */
typedef struct
{
    int found;
    int32_t n[3];
} MeshKartFloor;
static PortableMeshStatus collide(PortableMeshKartState *s, const PortableMeshKartRules *r,
                                  const MeshKartShape *shape, const PortableMeshCourse *c,
                                  const int32_t up[3], PortableMeshKartEvents *e, uint32_t *cannon,
                                  MeshKartWall *wall, MeshKartFloor *ground)
{
    /* Stable source-table order makes coincident seams and corner pushes deterministic. */
    for (unsigned pass = 0; pass < 8u; ++pass)
    {
        int pushed = 0;
        uint32_t first = 0, ti;
        for (;;)
        {
            const int next = nearby_triangle(c, s->position_q16, s->position_q16,
                                             (int64_t)shape->radius_q16 + shape->ride_height_q16 +
                                                 r->ground_probe_q16 + 2,
                                             &first, &ti);
            if (next == PORTABLE_MESH_QUERY_INVALID)
                return PORTABLE_MESH_INVALID;
            if (next == PORTABLE_MESH_QUERY_MISS)
                break;
            PortableMeshTriangle t;
            PortableMeshSurface surf;
            int32_t n[3], point[3];
            if (!c->triangle(c->context, ti, &t) || !read_surface(c, t.surface, &surf) ||
                !portable_mesh_query_normal(&t, n))
                return PORTABLE_MESH_INVALID;
            int32_t plane_relative[3], plane_point[3];
            for (unsigned axis = 0; axis < 3u; ++axis)
                plane_relative[axis] = s->position_q16[axis] - t.vertices_q16[0][axis];
            const int32_t plane_distance = (int32_t)(portable_fixed_3d_dot(plane_relative, n) / U);
            if (surf.one_sided && plane_distance < 0)
                continue;
            for (unsigned axis = 0; axis < 3u; ++axis)
                plane_point[axis] = s->position_q16[axis] - unit_product(plane_distance, n[axis]);
            const int face_inside = portable_mesh_query_inside(&t, plane_point, n);
            portable_mesh_query_closest(&t, s->position_q16, n, point);
            const uint64_t square = distance_squared(point, s->position_q16);
            const int64_t maximum_radius =
                (int64_t)shape->radius_q16 + shape->ride_height_q16 + r->ground_probe_q16;
            if (square > (uint64_t)(maximum_radius * maximum_radius))
                continue;
            const int32_t length = (int32_t)portable_fixed_3d_sqrt(square);
            if (length)
            {
                int64_t diff[3];
                for (unsigned i = 0; i < 3u; ++i)
                    diff[i] = (int64_t)s->position_q16[i] - point[i];
                if (!portable_fixed_3d_normalize(diff, n))
                    return PORTABLE_MESH_INVALID;
            }
            if (surf.type == 9u)
            {
                /* Cannon triggers never push; touching one fires it. */
                if (length <= shape->radius_q16 && *cannon == PORTABLE_MESH_ABSENT)
                    *cannon = t.surface;
                continue;
            }
            const int floor = face_inside && up_part(n, up) >= r->ground_normal_cutoff_q30 && surf.type != 3u;
            const int32_t radius = shape->radius_q16 + (floor ? shape->ride_height_q16 : 0);
            /* Pulling toward every nearby floor makes valid ramp joints fight each other.
             * Overlap resolution only pushes; a single nearest probe handles downward snap. */
            if (length > radius + 1)
                continue;
            /* An edge or corner under the kart, or the flat top of a wall,
             * supports it like ground (no ride height or alignment), so a
             * kart perched on a ledge can drive off instead of bouncing on
             * it forever. */
            const int support = !floor && up_part(n, up) >= r->ground_normal_cutoff_q30;
            if (floor || support)
            {
                s->grounded = 1;
                s->contact_triangle = ti;
                s->contact_surface = t.surface;
                s->respawn_index = t.respawn;
                if (floor)
                {
                    ground->found = 1;
                    memcpy(ground->n, n, sizeof ground->n);
                }
                if (surf.type >= 6u && surf.type <= 8u)
                    return PORTABLE_MESH_OUTSIDE;
            }
            else
                e->wall = 1;
            const int32_t penetration = radius - length;
            if (!floor && !support)
            {
                int32_t motion[3];
                for (unsigned i = 0; i < 3u; ++i)
                    motion[i] = s->velocity_q16[i] + s->knock_q16[i];
                const int32_t approach = (int32_t)(portable_fixed_3d_dot(motion, n) / U);
                if ((penetration > 1 || approach < 0) && (!wall->hit || approach < wall->approach_q16))
                {
                    wall->hit = 1;
                    memcpy(wall->n, n, sizeof wall->n);
                    wall->approach_q16 = approach;
                    wall->motion_q16 = (int32_t)portable_fixed_3d_sqrt((uint64_t)portable_fixed_3d_dot(motion, motion));
                }
                /* MK8 walls never reflect: the drive and the knock slide along them. */
                const int32_t kn = (int32_t)(portable_fixed_3d_dot(s->knock_q16, n) / U);
                if (kn < 0)
                    for (unsigned i = 0; i < 3u; ++i)
                        s->knock_q16[i] -= unit_product(kn, n[i]);
            }
            if (penetration > 1)
            {
                for (unsigned i = 0; i < 3u; ++i)
                    s->position_q16[i] += unit_product(penetration, n[i]);
                pushed = 1;
            }
            const int32_t vn = (int32_t)(portable_fixed_3d_dot(s->velocity_q16, n) / U);
            if (vn < 0)
                for (unsigned i = 0; i < 3u; ++i)
                    s->velocity_q16[i] -= unit_product(vn, n[i]);
        }
        if (!pushed)
            return PORTABLE_MESH_OK;
    }
    return PORTABLE_MESH_COLLISION_BUDGET;
}
static PortableMeshStatus sweep_faces(const int32_t before[3], int32_t after[3],
                                      const PortableMeshKartRules *r, const MeshKartShape *shape,
                                      const PortableMeshCourse *c, const int32_t up[3])
{
    uint32_t first = 0, i;
    for (;;)
    {
        const int next = nearby_triangle(
            c, before, after, (int64_t)shape->radius_q16 + shape->ride_height_q16 + 2, &first, &i);
        if (next == PORTABLE_MESH_QUERY_INVALID)
            return PORTABLE_MESH_INVALID;
        if (next == PORTABLE_MESH_QUERY_MISS)
            break;
        PortableMeshTriangle triangle;
        PortableMeshSurface surface;
        int32_t n[3], a[3], b[3];
        if (!c->triangle(c->context, i, &triangle) ||
            !read_surface(c, triangle.surface, &surface) ||
            !portable_mesh_query_normal(&triangle, n))
            return PORTABLE_MESH_INVALID;
        if (surface.type == 9u)
            continue;
        for (unsigned axis = 0; axis < 3u; ++axis)
        {
            a[axis] = before[axis] - triangle.vertices_q16[0][axis];
            b[axis] = after[axis] - triangle.vertices_q16[0][axis];
        }
        int32_t da = (int32_t)(portable_fixed_3d_dot(a, n) / U),
                db = (int32_t)(portable_fixed_3d_dot(b, n) / U);
        if (da < 0 && surface.one_sided)
            continue;
        if (da < 0)
        {
            da = -da;
            db = -db;
            for (unsigned axis = 0; axis < 3u; ++axis)
                n[axis] = -n[axis];
        }
        const int32_t radius =
            shape->radius_q16 +
            (up_part(n, up) >= r->ground_normal_cutoff_q30 && surface.type != 3u ? shape->ride_height_q16 : 0);
        if (da < radius || db >= radius || da <= db)
            continue;
        const int32_t time = fraction(da - radius, da - db);
        int32_t point[3];
        for (unsigned axis = 0; axis < 3u; ++axis)
            point[axis] = before[axis] + portable_fixed_3d_mul(after[axis] - before[axis], time) -
                          unit_product(radius, n[axis]);
        if (!portable_mesh_query_inside(&triangle, point, n))
            continue;
        for (unsigned axis = 0; axis < 3u; ++axis)
            after[axis] += unit_product(radius - db, n[axis]);
    }
    return PORTABLE_MESH_OK;
}
static PortableMeshStatus probe_support(PortableMeshKartState *s, const PortableMeshKartRules *r,
                                        const MeshKartShape *shape, const PortableMeshCourse *c,
                                        const int32_t up[3], int32_t maximum_distance)
{
    int32_t nearest = maximum_distance + 1, chosen_normal[3] = {0, 0, 0};
    uint32_t chosen = PORTABLE_MESH_ABSENT, chosen_type = 0;
    PortableMeshTriangle selected = {0};
    uint32_t first = 0, i;
    for (;;)
    {
        /* The legacy probe projects the ORIGINAL center onto the plane;
         * its horizontal reach includes the full possible downward gap. */
        const int next = nearby_triangle(
            c, s->position_q16, s->position_q16,
            (int64_t)shape->radius_q16 + shape->ride_height_q16 + maximum_distance + 2, &first, &i);
        if (next == PORTABLE_MESH_QUERY_INVALID)
            return PORTABLE_MESH_INVALID;
        if (next == PORTABLE_MESH_QUERY_MISS)
            break;
        PortableMeshTriangle t;
        PortableMeshSurface surface;
        int32_t n[3], relative[3], point[3];
        if (!c->triangle(c->context, i, &t) || !read_surface(c, t.surface, &surface) ||
            !portable_mesh_query_normal(&t, n))
            return PORTABLE_MESH_INVALID;
        const int32_t rise = up_part(n, up);
        if (surface.type == 3u || surface.type == 9u || rise < r->ground_normal_cutoff_q30)
            continue;
        for (unsigned a = 0; a < 3u; ++a)
            relative[a] = s->position_q16[a] - t.vertices_q16[0][a];
        const int32_t distance = (int32_t)(portable_fixed_3d_dot(relative, n) / U);
        const int64_t gap =
            ((int64_t)distance - shape->radius_q16 - shape->ride_height_q16) * U / rise;
        /* A kart that sank a little into a face across a crease (the sweep's
         * floor contact missed it on the seam) is lifted onto it. */
        if (gap < -(int64_t)shape->radius_q16 / 2 || gap >= nearest)
            continue;
        for (unsigned a = 0; a < 3u; ++a)
            point[a] = s->position_q16[a] - unit_product(distance, n[a]);
        if (!portable_mesh_query_inside(&t, point, n))
        {
            /* Imported floors (DS collision prisms) leave hairline seams
             * between faces that share no exact edge: a kart over a seam is
             * still on the road, not airborne for a tick. */
            int32_t closest[3];
            portable_mesh_query_closest(&t, point, n, closest);
            int64_t square = 0;
            for (unsigned a = 0; a < 3u; ++a)
                square += ((int64_t)closest[a] - point[a]) * ((int64_t)closest[a] - point[a]);
            if (square > (int64_t)MESH_KART_SEAM_Q16 * MESH_KART_SEAM_Q16)
                continue;
        }
        nearest = (int32_t)gap;
        chosen_type = surface.type;
        chosen = i;
        selected = t;
        memcpy(chosen_normal, n, sizeof(n));
    }
    if (chosen != PORTABLE_MESH_ABSENT)
    {
        if (chosen_type >= 6u && chosen_type <= 8u)
            return PORTABLE_MESH_OUTSIDE;
        for (unsigned axis = 0; axis < 3u; ++axis)
            s->position_q16[axis] -= unit_product(nearest, up[axis]);
        s->grounded = 1;
        s->contact_triangle = chosen;
        s->contact_surface = selected.surface;
        s->respawn_index = selected.respawn;
        const int32_t inward = (int32_t)(portable_fixed_3d_dot(s->velocity_q16, chosen_normal) / U);
        if (inward < 0)
            for (unsigned axis = 0; axis < 3u; ++axis)
                s->velocity_q16[axis] -= unit_product(inward, chosen_normal[axis]);
        align_ground(s, chosen_normal, r);
    }
    return PORTABLE_MESH_OK;
}
/* MK8's alignment factor: how squarely the kart travels along its body,
 * clamped to 0.1..1 and 1 when nearly stopped. */
static int32_t alignment(const PortableMeshKartState *s)
{
    uint64_t square = 0;
    for (unsigned i = 0; i < 3u; ++i)
        square += (uint64_t)((int64_t)s->velocity_q16[i] * s->velocity_q16[i]);
    const int64_t speed = (int64_t)portable_fixed_3d_sqrt(square);
    if (speed < MESH_KART_MK8_ALIGN_SPEED_Q16)
        return Q;
    int64_t along = portable_fixed_3d_dot(s->velocity_q16, s->forward_q30) / U;
    along = (along < 0 ? -along : along) * Q / speed;
    return along > Q ? Q : along < MESH_KART_MK8_ALIGN_MIN_Q16 ? MESH_KART_MK8_ALIGN_MIN_Q16 : (int32_t)along;
}
/* MK8 surface parameters (0x1791cc/0x17da94): speed multiplier, slide
 * target, friction (accel, drag and slip), boost-accel scale and steering. */
typedef struct
{
    int32_t speed, slide, friction, boost, steer, offroad;
} MeshKartGround;
static MeshKartGround ground_rules(const PortableMeshSurface *surf, const PortableMeshKartRules *r, int road)
{
    static const int32_t steer[3] = {58982, 52429, 45875}; /* 0.9 / 0.8 / 0.7 */
    if (road) /* air, Star, mushroom and panel boosts drive on road rules */
        return (MeshKartGround){Q, r->traction_on_q16, Q, Q, Q, 0};
    unsigned c = surf->surface_class;
    if (c == MESH_KART_MK8_CLASS_AUTO)
        c = surf->type == 1u ? (surf->speed_ratio_q16 > 39321 ? MESH_KART_MK8_CLASS_DIRT
                                : surf->speed_ratio_q16 > 26214 ? MESH_KART_MK8_CLASS_GRASS
                                                                : MESH_KART_MK8_CLASS_HEAVY)
            : surf->type == 2u ? MESH_KART_MK8_CLASS_SLIPPERY + (surf->friction_q16 >= Q / 2 ? 0u
                                                                 : surf->friction_q16 >= Q / 4 ? 1u : 2u)
                               : MESH_KART_MK8_CLASS_ROAD;
    switch (c)
    {
    case MESH_KART_MK8_CLASS_DIRT:
        return (MeshKartGround){45875, MESH_KART_MK8_OFFROAD_SLIDE_Q16, Q, Q, steer[0], 1};
    case MESH_KART_MK8_CLASS_GRASS:
        return (MeshKartGround){32768, MESH_KART_MK8_OFFROAD_SLIDE_Q16, 64225, Q, steer[1], 1};
    case MESH_KART_MK8_CLASS_HEAVY:
        return (MeshKartGround){19661, MESH_KART_MK8_OFFROAD_SLIDE_Q16, 63570, Q, steer[2], 1};
    case MESH_KART_MK8_CLASS_ROAD:
        return (MeshKartGround){surf->speed_ratio_q16, r->traction_on_q16, Q, Q, Q, 0};
    default:
    {
        /* Slippery classes: speed x rises with the off-road traction stat;
         * friction 1-(1-x)^2, boost scale 1-2(1-x); slide target +0.01 a level. */
        const unsigned ice = c >= MESH_KART_MK8_CLASS_ICE,
                       level = c - (ice ? MESH_KART_MK8_CLASS_ICE : MESH_KART_MK8_CLASS_SLIPPERY);
        const int32_t x = (ice ? MESH_KART_MK8_ICE_X_Q16 : MESH_KART_MK8_SLIPPERY_X_Q16) +
                          portable_fixed_3d_mul(MESH_KART_MK8_SLIPPERY_X_SPAN_Q16, r->traction_level_q16);
        const int32_t loss = Q - (x > Q ? Q : x), boost = Q - 2 * loss;
        return (MeshKartGround){x, r->traction_off_q16 + 655 * (int32_t)level,
                                Q - portable_fixed_3d_mul(loss, loss), boost < 0 ? 0 : boost, steer[level], 0};
    }
    }
}
/* MK8's grounded speed update (0x177d18): steering loss, reverse and
 * over-cap decay, then throttle followed by brake (both may be held), or
 * coast when neither is held, then the boost. */
static int32_t mk8_speed(const PortableMeshKartState *s, const PortableMeshKartRules *r, uint16_t buttons,
                         int32_t cap, int32_t reverse, int32_t base, int32_t boost_cap, int32_t boost_accel,
                         int32_t star_accel, int32_t friction)
{
    int32_t v = s->speed_q16;
    if (!boost_cap) /* a live boost skips MK8's steering loss */
    {
        const int32_t steer = s->steer_filtered_q16 > Q ? Q : s->steer_filtered_q16 < -Q ? -Q : s->steer_filtered_q16;
        int32_t share = portable_fixed_3d_mul(steer, steer), loss = MESH_KART_MK8_STEER_LOSS_Q16;
        if (s->drift_direction)
        {
            const int64_t magnitude = v < 0 ? -(int64_t)v : v;
            const int32_t ratio = base <= 0 || magnitude >= base ? Q : (int32_t)(magnitude * Q / base);
            share = portable_fixed_3d_mul(share, ratio);
            loss = MESH_KART_MK8_DRIFT_STEER_LOSS_Q16;
        }
        v = portable_fixed_3d_mul(v, Q - portable_fixed_3d_mul(Q - loss, share));
    }
    if (v < -MESH_KART_MK8_REVERSE_Q16)
        v = portable_fixed_3d_mul(v, MESH_KART_MK8_REVERSE_COAST_Q16);
    if (v > (cap > boost_cap ? cap : boost_cap))
        v = portable_fixed_3d_mul(v, MESH_KART_MK8_OVER_CAP_Q16);
    const int32_t f = alignment(s);
    if (star_accel)
    {
        /* Star replaces acceleration: straight to its top speed, brake or not. */
        if (v < cap)
            v = v > cap - star_accel ? cap : v + star_accel;
    }
    else
    {
        if (buttons & PORTABLE_MESH_ACCELERATE)
            v = portable_mesh_kart_mk8_drive(v, cap,
                                             s->drift_direction ? MESH_KART_MK8_DRIFT_RATE_Q16
                                                                : r->acceleration_rate_q16,
                                             portable_fixed_3d_dt(r->acceleration_limit_q16), friction, f, 1);
        if (buttons & PORTABLE_MESH_BRAKE)
            v = portable_mesh_kart_mk8_drive(v, reverse, MESH_KART_MK8_BRAKE_RATE_Q16,
                                             MESH_KART_MK8_BRAKE_LIMIT_Q16, friction, f, -1);
        else if (!(buttons & PORTABLE_MESH_ACCELERATE))
            v = portable_fixed_3d_mul(portable_fixed_3d_mul(v, friction),
                                      v > 0 ? MESH_KART_MK8_COAST_Q16 : MESH_KART_MK8_REVERSE_COAST_Q16);
    }
    /* MK8 boosts pull toward their cap from either side. */
    if (boost_cap)
    {
        const int32_t step = portable_fixed_3d_mul(portable_fixed_3d_dt(boost_accel), friction);
        v = v > boost_cap - step ? boost_cap : v + step;
    }
    return v;
}
static int unit_vector_valid(const int32_t vector[3])
{
    uint64_t square = 0;
    for (unsigned i = 0; i < 3u; ++i)
    {
        const int64_t value = vector[i];
        if (value < -U || value > U)
            return 0;
        square += (uint64_t)(value * value);
    }
    const uint64_t one = UINT64_C(1) << 60u, tolerance = UINT64_C(1) << 50u;
    return square >= one - tolerance && square <= one + tolerance;
}
/* MK8 rocket start (0x166c7c). */
void portable_mesh_kart_countdown(PortableMeshKartState *s, uint16_t buttons)
{
    int32_t c = s->rocket_charge_q16;
    if (buttons & PORTABLE_MESH_ACCELERATE)
    {
        c += MESH_KART_MK8_ROCKET_GAIN_Q16 - portable_fixed_3d_mul(c, MESH_KART_MK8_ROCKET_LOSS_Q16);
        if (c > Q)
            c = Q;
    }
    else
        c = portable_fixed_3d_mul(c, MESH_KART_MK8_ROCKET_DECAY_Q16);
    s->rocket_charge_q16 = c;
}
unsigned portable_mesh_kart_rocket_tier(int32_t c)
{
    static const int32_t above[5] = {55378, 57344, 58982, 60293, 61276}; /* 0.845 .. 0.935 */
    if (c > MESH_KART_MK8_BURNOUT_Q16)
        return 6u;
    unsigned tier = 0;
    while (tier < 5u && c > above[tier])
        ++tier;
    return tier;
}
void portable_mesh_kart_go(PortableMeshKartState *s)
{
    static const uint16_t ticks[5] = {10, 20, 30, 45, 70};
    const unsigned tier = portable_mesh_kart_rocket_tier(s->rocket_charge_q16);
    if (tier == 6u)
        s->burnout_ticks = MESH_KART_MK8_BURNOUT_TICKS;
    else if (tier)
        s->rocket_ticks = ticks[tier - 1u];
    s->rocket_charge_q16 = 0;
}
unsigned portable_mesh_kart_rocket_hold_ticks(unsigned tier)
{
    PortableMeshKartState s;
    memset(&s, 0, sizeof s);
    for (unsigned n = 1; n < 400u; ++n)
    {
        portable_mesh_kart_countdown(&s, PORTABLE_MESH_ACCELERATE);
        if (portable_mesh_kart_rocket_tier(s.rocket_charge_q16) == tier)
            return n;
    }
    return 0;
}
uint16_t portable_mesh_kart_rocket_buttons(uint32_t countdown_ticks, unsigned tier)
{
    return countdown_ticks && countdown_ticks <= portable_mesh_kart_rocket_hold_ticks(tier)
               ? PORTABLE_MESH_ACCELERATE : 0u;
}
int portable_mesh_kart_state_valid(const PortableMeshKartState *s)
{
    if (!s || !unit_vector_valid(s->forward_q30) || !unit_vector_valid(s->up_q30))
        return 0;
    for (unsigned i = 0; i < 3u; ++i)
        if (s->position_q16[i] < -LIMIT || s->position_q16[i] > LIMIT ||
            s->velocity_q16[i] < -16777216 || s->velocity_q16[i] > 16777216 ||
            s->knock_q16[i] < -64 * Q || s->knock_q16[i] > 64 * Q || s->bump_push_q16[i] < -64 * Q ||
            s->bump_push_q16[i] > 64 * Q || s->boost_ticks[i] > 8192u)
            return 0;
    return s->previous_buttons <= 15u && s->drift_direction >= -1 && s->drift_direction <= 1 &&
           s->drift_tier <= 3u && s->grounded <= 1u && s->last_boost_tier <= 3u &&
           s->steer_filtered_q16 >= -65536 && s->steer_filtered_q16 <= 65536 &&
           s->outside_drift_q16 >= -16384 && s->outside_drift_q16 <= 16384 &&
           s->speed_q16 >= -16777216 && s->speed_q16 <= 16777216 && s->drift_charge <= 8192u &&
           s->panel_ticks <= 8192u && s->respawn_ticks <= 8192u &&
           s->bump_delay <= MESH_KART_MK8_BUMP_DELAY && s->slip_in <= 1u &&
           s->slip_grace <= MESH_KART_MK8_SLIP_GRACE && s->slip_charge <= MESH_KART_MK8_SLIP_CHARGE &&
           s->slip_ticks <= MESH_KART_MK8_SLIP_TICKS && s->offroad_ticks <= MESH_KART_MK8_OFFROAD_TICKS &&
           s->trick_surface <= 1u && s->trick_kind >= -2 && s->trick_kind <= 2 && s->wall_cooldown <= MESH_KART_MK8_WALL_COOLDOWN &&
           s->wall_yaw_q24 >= -(2 << 24) && s->wall_yaw_q24 <= (2 << 24) && s->cannon_tick <= s->cannon_ticks &&
           s->drift_ramp_q16 >= 0 && s->drift_ramp_q16 <= Q &&
           s->spin_q16 >= -MESH_KART_MK8_SPIN_LIMIT_Q16 && s->spin_q16 <= MESH_KART_MK8_SPIN_LIMIT_Q16 &&
           s->slide_ticks <= MESH_KART_MK8_SLIDE_MAX_TICKS + 1u && s->rocket_charge_q16 >= 0 &&
           s->rocket_charge_q16 <= Q && s->rocket_ticks <= 8192u &&
           s->burnout_ticks <= MESH_KART_MK8_BURNOUT_TICKS && s->traction_q16 >= 0 && s->traction_q16 <= Q &&
           s->traction_hold_ticks <= 30u &&
           (!nonzero(s->gravity_up_q30) || unit_vector_valid(s->gravity_up_q30)) &&
           (!nonzero(s->path_up_q30) || unit_vector_valid(s->path_up_q30)) && s->anti_gravity <= 1u &&
           s->anti_gravity_rate <= MESH_KART_MK8_ANTIGRAVITY_RAMP && s->spin_ticks <= s->spin_length &&
           s->spin_length <= MESH_KART_MK8_SPIN_TICKS_SLOW && s->spin_armed <= 1u && s->spin_sign >= -1 &&
           s->spin_sign <= 1 && s->spin_boost_ticks <= MESH_KART_MK8_SPIN_BOOST_TICKS &&
           s->glide <= 2u && s->glide_path.found <= 1u && s->glide_path.cannon <= 1u &&
           (!s->glide_path.found || (unit_vector_valid(s->glide_path.tangent_q30) &&
                                     unit_vector_valid(s->glide_path.up_q30) && s->glide_path.radius_q16 >= 0)) &&
           (s->glide != 2u || unit_vector_valid(s->glide_heading_q30)) && s->glide_land_q16 >= 0 &&
           s->glide_land_q16 <= Q && s->glide_bank_q16 >= -4 * Q && s->glide_bank_q16 <= 4 * Q &&
           s->glide_bank_velocity_q16 >= -4 * Q && s->glide_bank_velocity_q16 <= 4 * Q &&
           s->glide_roll_q16 >= -4 * Q && s->glide_roll_q16 <= 4 * Q && s->glide_pitch_q16 >= -Q &&
           s->glide_pitch_q16 <= Q &&
           (!s->cannon_ticks || (s->cannon_tick < s->cannon_ticks &&
                                 s->cannon_from_q16[0] >= -LIMIT && s->cannon_from_q16[0] <= LIMIT &&
                                 s->cannon_from_q16[1] >= -LIMIT && s->cannon_from_q16[1] <= LIMIT &&
                                 s->cannon_from_q16[2] >= -LIMIT && s->cannon_from_q16[2] <= LIMIT));
}
int portable_mesh_kart_boosting(const PortableMeshKartState *s)
{
    return s->panel_ticks || s->rocket_ticks || s->slip_ticks || s->trick_boost_ticks || s->spin_boost_ticks ||
           s->boost_ticks[0] ||
           s->boost_ticks[1] || s->boost_ticks[2];
}
static int32_t top_ratio(const PortableMeshKartState *s, const PortableMeshKartRules *r)
{
    return (int32_t)((int64_t)(s->speed_q16 > 0 ? s->speed_q16 : 0) * Q / r->forward_speed_q16);
}
int portable_mesh_kart_drafting(const PortableMeshKartState *f, const PortableMeshKartRules *fr,
                                const PortableMeshKartState *l, const PortableMeshKartRules *lr, unsigned box)
{
    if (!f || !l || f == l || box > 4u || !portable_mesh_kart_state_valid(f) || !portable_mesh_kart_state_valid(l) ||
        !portable_mesh_kart_rules_valid(fr) || !portable_mesh_kart_rules_valid(lr) ||
        top_ratio(f, fr) <= Q / 2 || top_ratio(l, lr) <= Q / 2 || f->cannon_ticks || l->cannon_ticks)
        return 0;
    int32_t half = lr->kart_radius_q16 > 4 * MESH_KART_MK8_UNIT_Q16 ? lr->kart_radius_q16 : 4 * MESH_KART_MK8_UNIT_Q16;
    for (unsigned i = 0; i < box; ++i)
        half = portable_fixed_3d_mul(half, 78643); /* x1.2 a step */
    const int32_t back = (15 + 12 * (int32_t)box) * MESH_KART_MK8_UNIT_Q16;
    const int32_t sensor = fr->kart_radius_q16 + 4 * MESH_KART_MK8_UNIT_Q16; /* half the hull plus 4 units */
    for (unsigned i = 0; i < 3u; ++i)
    {
        const int64_t centre = (int64_t)l->position_q16[i] - unit_product(back, l->forward_q30[i]);
        const int64_t gap = (int64_t)f->position_q16[i] - centre;
        if (gap >= (int64_t)half + sensor || gap <= -((int64_t)half + sensor))
            return 0;
    }
    return 1;
}
PortableMeshStatus portable_mesh_kart_init(PortableMeshKartState *s, const PortableMeshKartRules *r,
                                           const int32_t position[3], const int32_t forward[3])
{
    if (!s || !position || !forward || !portable_mesh_kart_rules_valid(r))
        return PORTABLE_MESH_INVALID;
    for (unsigned i = 0; i < 3u; ++i)
        if (position[i] < -LIMIT || position[i] > LIMIT)
            return PORTABLE_MESH_INVALID;
    PortableMeshKartState next;
    memset(&next, 0, sizeof(next));
    memcpy(next.position_q16, position, sizeof(next.position_q16));
    const int64_t f[3] = {forward[0], forward[1], forward[2]};
    if (!portable_fixed_3d_normalize(f, next.forward_q30))
        return PORTABLE_MESH_INVALID;
    next.up_q30[2] = U;
    next.contact_triangle = next.contact_surface = next.respawn_index = PORTABLE_MESH_ABSENT;
    next.traction_q16 = r->traction_on_q16;
    *s = next;
    return PORTABLE_MESH_OK;
}
static void rotate_direction(const int32_t forward[3], const int32_t up[3], int32_t turn,
                             int32_t out[3])
{
    int32_t sine, cosine;
    portable_fixed_3d_sincos((uint16_t)turn, &sine, &cosine);
    int32_t cross[3], next[3];
    cross[0] = (int32_t)(((int64_t)up[1] * forward[2] - (int64_t)up[2] * forward[1]) / U);
    cross[1] = (int32_t)(((int64_t)up[2] * forward[0] - (int64_t)up[0] * forward[2]) / U);
    cross[2] = (int32_t)(((int64_t)up[0] * forward[1] - (int64_t)up[1] * forward[0]) / U);
    for (unsigned i = 0; i < 3u; ++i)
        next[i] = unit_product(forward[i], cosine) + unit_product(cross[i], sine);
    normalize(next, out);
}
static void travel_direction(const PortableMeshKartState *s, int32_t out[3])
{
    if (!s->outside_drift_q16)
        memcpy(out, s->forward_q30, sizeof s->forward_q30);
    else
        rotate_direction(s->forward_q30, s->up_q30, -s->outside_drift_q16, out);
}
PortableMeshStatus portable_mesh_kart_travel_direction(const PortableMeshKartState *s,
                                                       int32_t out[3])
{
    if (!s || !out || !unit_vector_valid(s->forward_q30) || !unit_vector_valid(s->up_q30) ||
        s->outside_drift_q16 < -16384 || s->outside_drift_q16 > 16384)
        return PORTABLE_MESH_INVALID;
    int32_t result[3];
    travel_direction(s, result);
    memcpy(out, result, sizeof result);
    return PORTABLE_MESH_OK;
}
PortableMeshStatus portable_mesh_kart_project_travel_speed(const PortableMeshKartState *s,
                                                           int32_t *out)
{
    int32_t direction[3];
    if (!out || portable_mesh_kart_travel_direction(s, direction) != PORTABLE_MESH_OK)
        return PORTABLE_MESH_INVALID;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (s->velocity_q16[axis] < -256 * Q || s->velocity_q16[axis] > 256 * Q)
            return PORTABLE_MESH_INVALID;
    const int64_t speed = portable_fixed_3d_dot(s->velocity_q16, direction) / U;
    if (speed < -256 * Q || speed > 256 * Q)
        return PORTABLE_MESH_INVALID;
    *out = (int32_t)speed;
    return PORTABLE_MESH_OK;
}
/* MK8 steering (core.md). The ground turn rate rises with speed in
 * units/frame: 2 x 0.018 x v below 0.5, then quadratically from 0.018 rad a
 * tick at 0.5 to the handling row at 7; reversing it is linear and mirrored.
 * A drift targets the mean of 0.02 and the drift row, plus or minus half
 * the drift row by stick, and reaches it through a damped spring. */
/* Radians a tick (Q16) as the turns-per-second rate the rules use. */
static int32_t radians_rate(int32_t radians_q16)
{
    return (int32_t)(radians_q16 * MESH_KART_MK8_TURNS_PER_SECOND_Q16 / Q);
}
static int32_t units_per_frame(int32_t speed_q16)
{
    const int64_t v = speed_q16 < 0 ? -(int64_t)speed_q16 : speed_q16;
    const int64_t units = v * Q / MESH_KART_MK8_UNIT_SPEED_Q16;
    return units > 64 * Q ? 64 * Q : (int32_t)units;
}
/* Ground turn rate in turns a second (Q16) at full stick. */
int32_t portable_mesh_kart_turn_rate(const PortableMeshKartRules *r, int32_t speed_q16)
{
    const int32_t units = units_per_frame(speed_q16), low = radians_rate(MESH_KART_MK8_TURN_LOW_Q16);
    const int32_t high = r->handling_turn_rate_q16;
    int32_t rate;
    if (units < Q / 2)
        rate = (int32_t)((int64_t)low * units * 2 / Q);
    else if (units >= 7 * Q)
        rate = high;
    else
    {
        const int64_t t = (int64_t)(units - Q / 2) * Q / (13 * Q / 2);
        rate = low + (int32_t)((int64_t)(high - low) * (speed_q16 < 0 ? t : t * t / Q) / Q);
    }
    return speed_q16 < 0 ? -rate : rate;
}
int32_t portable_mesh_kart_min_turn_radius(const PortableMeshKartRules *r)
{
    const int32_t speed = MESH_KART_MK8_UNIT_SPEED_Q16 / 2;
    /* Turns a second to radians a second: 2 pi = 411775 / Q. */
    const int64_t omega = (int64_t)portable_mesh_kart_turn_rate(r, speed) * INT64_C(411775) / Q;
    return omega > 0 ? (int32_t)((int64_t)speed * Q / omega) : INT32_MAX;
}
static int32_t steer(PortableMeshKartState *s, const PortableMeshKartRules *r,
                     PortableMeshKartInput input)
{
    const int32_t raw = (int32_t)input.steering * 2;
    const int32_t weight =
        response(s->drift_direction ? r->drift_response_q16 : r->handling_response_q16);
    s->steer_filtered_q16 += portable_fixed_3d_mul(raw - s->steer_filtered_q16, weight);
    int32_t rate;
    if (s->drift_direction)
    {
        /* MK8 (0x16fcfc, 0x177280, 0x1701c0, 0x177440): the stick turns at
         * half the drift row, which runs from 0.02 at 0.5 units/frame to the
         * handling row at 7 (at most 0.018 outward), around a bias toward the
         * drift side of 0.015 times the squared ramp, full in about 22 ticks.
         * The stick acts at once: outward opens the line, but never
         * straightens a held drift. */
        const int32_t inward = s->steer_filtered_q16 * s->drift_direction;
        const int32_t low = MESH_KART_MK8_DRIFT_TURN_LOW_Q24;
        const int32_t high =
            (int32_t)((int64_t)r->drift_turn_rate_q16 * (1 << 24) / MESH_KART_MK8_TURNS_PER_SECOND_Q16);
        const int64_t units = (int64_t)(s->speed_q16 < 0 ? -(int64_t)s->speed_q16 : s->speed_q16) * Q /
                              MESH_KART_MK8_UNIT_SPEED_Q16;
        int32_t row = units < Q / 2   ? (int32_t)(2 * (int64_t)low * units / Q)
                      : units >= 7 * Q ? high
                                       : (int32_t)(low + ((int64_t)high - low) * (units - Q / 2) / (13 * Q / 2));
        if (inward < 0 && row > MESH_KART_MK8_DRIFT_OUTWARD_Q24)
            row = MESH_KART_MK8_DRIFT_OUTWARD_Q24;
        s->drift_ramp_q16 = s->drift_ramp_q16 > Q - MESH_KART_MK8_DRIFT_RAMP_Q16 ? Q
                                                                                 : s->drift_ramp_q16 + MESH_KART_MK8_DRIFT_RAMP_Q16;
        const int32_t bias = portable_fixed_3d_mul(
            MESH_KART_MK8_DRIFT_BIAS_Q24, portable_fixed_3d_mul(s->drift_ramp_q16, s->drift_ramp_q16));
        const int64_t yaw = s->drift_direction * ((int64_t)portable_fixed_3d_mul(row, inward) / 2 + bias);
        rate = (int32_t)(yaw * MESH_KART_MK8_TURNS_PER_SECOND_Q16 / (1 << 24));
    }
    else
    {
        /* A wall stops a pinned kart every tick; a held accelerator turns it as
         * if at 0.5 units/frame so it can still steer away. Only a genuine
         * backward roll overrides the accelerator's forward intent. */
        int32_t speed = s->speed_q16;
        if ((input.buttons & PORTABLE_MESH_ACCELERATE) && speed > -Q && speed < MESH_KART_MK8_UNIT_SPEED_Q16 / 2)
            speed = MESH_KART_MK8_UNIT_SPEED_Q16 / 2;
        rate = portable_fixed_3d_mul(portable_mesh_kart_turn_rate(r, speed), s->steer_filtered_q16);
    }
    if (!s->grounded)
        rate = portable_fixed_3d_mul(rate, s->glide == 2u ? MESH_KART_MK8_GLIDE_YAW_Q16 : r->air_steering_ratio_q16);
    const int32_t turn = portable_fixed_3d_dt(rate);
    if (turn)
        rotate_direction(s->forward_q30, s->up_q30, turn, s->forward_q30);
    return turn;
}
int portable_mesh_glide_segment(const PortableMeshGravityPoint *a, const PortableMeshGravityPoint *b,
                                const int32_t p[3], uint64_t *distance_squared, int32_t closest_q16[3],
                                int32_t tangent_q30[3], int32_t up_q30[3], int32_t *radius_q16)
{
    if (!a || !b || !p || !distance_squared || !closest_q16 || !tangent_q30 || !up_q30 || !radius_q16)
        return 0;
    int64_t d[3], w[3];
    uint64_t length = 0;
    for (unsigned i = 0; i < 3u; ++i)
    {
        d[i] = (int64_t)b->position_q16[i] - a->position_q16[i];
        w[i] = (int64_t)p[i] - a->position_q16[i];
        length += (uint64_t)(d[i] * d[i]);
    }
    int32_t tangent[3], up[3];
    if (!length || !portable_fixed_3d_normalize(d, tangent))
        return 0;
    const int64_t along = d[0] * w[0] + d[1] * w[1] + d[2] * w[2];
    const int32_t t = along <= 0 ? 0 : (uint64_t)along >= length ? Q
                                     : (int32_t)((uint64_t)along * (uint64_t)Q / length);
    uint64_t square = 0;
    int64_t lerped[3];
    int32_t closest[3];
    for (unsigned i = 0; i < 3u; ++i)
    {
        closest[i] = (int32_t)(a->position_q16[i] + d[i] * t / Q);
        const int64_t off = (int64_t)p[i] - closest[i];
        square += (uint64_t)(off * off);
        lerped[i] = a->up_q30[i] + ((int64_t)b->up_q30[i] - a->up_q30[i]) * t / Q;
    }
    if (!portable_fixed_3d_normalize(lerped, up))
        return 0;
    memcpy(closest_q16, closest, sizeof closest);
    memcpy(tangent_q30, tangent, sizeof tangent);
    memcpy(up_q30, up, sizeof up);
    *radius_q16 = (int32_t)(a->radius_q16 + ((int64_t)b->radius_q16 - a->radius_q16) * t / Q);
    *distance_squared = square;
    return 1;
}
/* MK8 units: metres Q16 to units Q16, and units/frame Q16 to metres/second Q16. */
static int64_t metres_to_units(int64_t metres_q16)
{
    return metres_q16 * Q / MESH_KART_MK8_UNIT_Q16;
}
static int32_t units_to_speed(int64_t units_q16)
{
    return portable_fixed_3d_clamp(units_q16 * MESH_KART_MK8_UNIT_SPEED_Q16 / Q);
}
static int32_t velocity_units(const PortableMeshKartState *s)
{
    return units_per_frame((int32_t)portable_fixed_3d_sqrt(
        (uint64_t)((int64_t)s->velocity_q16[0] * s->velocity_q16[0]) +
        (uint64_t)((int64_t)s->velocity_q16[1] * s->velocity_q16[1]) +
        (uint64_t)((int64_t)s->velocity_q16[2] * s->velocity_q16[2])));
}
/* v^2 (units/frame Q16) times a Q30 coefficient, in units/frame Q16. */
static int64_t glide_square(int32_t v, int64_t coefficient_q30)
{
    return (int64_t)v * v / Q * coefficient_q30 / (INT64_C(1) << 30);
}
static int64_t vector_length(const int64_t v[3])
{
    return (int64_t)portable_fixed_3d_sqrt((uint64_t)(v[0] * v[0]) + (uint64_t)(v[1] * v[1]) +
                                           (uint64_t)(v[2] * v[2]));
}
static void glide_end(PortableMeshKartState *s, PortableMeshKartEvents *e)
{
    if (s->glide == 2u)
        e->glide_end = 1;
    s->glide = 0;
    s->glide_ticks = 0;
    s->glide_land_q16 = s->glide_bank_q16 = s->glide_bank_velocity_q16 = s->glide_roll_q16 = s->glide_pitch_q16 = 0;
    memset(s->glide_heading_q30, 0, sizeof s->glide_heading_q30);
}
/* MK8 glide opening (0x171e4c): at least 6 units/frame (10 on a cannon)
 * along the motion plus 1.1 up the path. */
static void glide_open(PortableMeshKartState *s, PortableMeshKartEvents *e)
{
    const int cannon = s->glide_path.cannon;
    const int32_t minimum = cannon ? MESH_KART_MK8_GLIDE_CANNON_MIN_Q16 : MESH_KART_MK8_GLIDE_MIN_Q16;
    int32_t v = velocity_units(s), heading[3];
    const int64_t motion[3] = {s->velocity_q16[0], s->velocity_q16[1], s->velocity_q16[2]};
    if (!portable_fixed_3d_normalize(motion, heading))
        memcpy(heading, s->forward_q30, sizeof heading);
    v = v < minimum ? minimum : v;
    int64_t launched[3];
    for (unsigned i = 0; i < 3u; ++i)
    {
        s->velocity_q16[i] = units_to_speed((int64_t)heading[i] * v / U +
                                            (int64_t)s->glide_path.up_q30[i] * MESH_KART_MK8_GLIDE_OPEN_Q16 / U);
        launched[i] = s->velocity_q16[i];
    }
    if (!portable_fixed_3d_normalize(launched, s->glide_heading_q30))
        memcpy(s->glide_heading_q30, heading, sizeof heading);
    cancel_drift(s);
    s->glide = 2;
    s->glide_ticks = 0;
    s->glide_land_q16 = s->glide_bank_q16 = s->glide_bank_velocity_q16 = s->glide_roll_q16 = s->glide_pitch_q16 = 0;
    e->glided = 1;
}
/* MK8 glide flight (0x172e68, 0x173a4c, 0x173bac, 0x173d3c): the bank
 * spring, stick pitch, path yaw/pitch assists, then lift, drag and path
 * gravity on the heading, path attraction and the out-of-tube push. stick
 * is +Q to dive, -Q to pull up. Sets this tick's velocity and returns how
 * far outside the path's tube the kart is, Q16 of 100 units. */
static int32_t glide_flight(PortableMeshKartState *s, const PortableMeshKartRules *r, int32_t stick)
{
    const int found = s->glide_path.found, cannon = found && s->glide_path.cannon;
    const int32_t *path_up = found ? s->glide_path.up_q30 : WORLD_UP;
    const int32_t abs_stick = stick < 0 ? -stick : stick;
    const int32_t abs_pitch = s->glide_pitch_q16 < 0 ? -s->glide_pitch_q16 : s->glide_pitch_q16;
    const int32_t spring = 3 * portable_fixed_3d_mul(s->steer_filtered_q16, MESH_KART_MK8_GLIDE_BANK_Q16) +
                           portable_fixed_3d_mul(portable_fixed_3d_mul(4 * Q / 5, s->glide_bank_q16),
                                                 abs_stick > abs_pitch ? abs_stick : abs_pitch) -
                           s->glide_bank_q16;
    s->glide_bank_velocity_q16 = portable_fixed_3d_mul(spring, 157 /* 0.0024 */) +
                                 portable_fixed_3d_mul(s->glide_bank_velocity_q16, 62915 /* 0.96 */);
    const int32_t bank = s->glide_bank_q16 + s->glide_bank_velocity_q16;
    s->glide_bank_q16 = bank > 4 * Q ? 4 * Q : bank < -4 * Q ? -4 * Q : bank;
    s->glide_roll_q16 += portable_fixed_3d_mul(s->glide_bank_q16 - s->glide_roll_q16, 3277 /* 0.05 */);
    s->glide_pitch_q16 += portable_fixed_3d_mul(stick - s->glide_pitch_q16, 2621 /* 0.04 */);
    if (cannon)
        s->glide_pitch_q16 = 0;
    int32_t d[3];
    memcpy(d, s->glide_heading_q30, sizeof d);
    int64_t offset[3] = {0, 0, 0}, distance = 0;
    int32_t height_q16 = 0;
    int64_t tube_u = 0;
    if (found)
    {
        const int32_t *t = s->glide_path.tangent_q30;
        int64_t next[3];
        if (cannon)
            for (unsigned i = 0; i < 3u; ++i)
                next[i] = d[i] + ((int64_t)t[i] - d[i]) / 20;
        else
        {
            /* Yaw toward the path about its up by 0.02 of the angle times
             * min(angle/(pi/2), 0.3); pitch toward it by 0.05 of the angle. */
            const int32_t du = up_part(d, path_up), tu = up_part(t, path_up);
            int64_t dh[3], th[3];
            for (unsigned i = 0; i < 3u; ++i)
            {
                dh[i] = d[i] - (int64_t)du * path_up[i] / U;
                th[i] = t[i] - (int64_t)tu * path_up[i] / U;
            }
            int32_t dn[3], tn[3];
            if (portable_fixed_3d_normalize(dh, dn) && portable_fixed_3d_normalize(th, tn))
            {
                const int64_t cross[3] = {((int64_t)dn[1] * tn[2] - (int64_t)dn[2] * tn[1]) / U,
                                          ((int64_t)dn[2] * tn[0] - (int64_t)dn[0] * tn[2]) / U,
                                          ((int64_t)dn[0] * tn[1] - (int64_t)dn[1] * tn[0]) / U};
                /* angle/(pi/2) from the sine; past a right angle it is capped. */
                int64_t share = portable_fixed_3d_dot(dn, tn) <= 0 ? U : vector_length(cross) * 14 / 22;
                share = share > (int64_t)U * 3 / 10 ? (int64_t)U * 3 / 10 : share;
                const int64_t length = vector_length(dh);
                for (unsigned i = 0; i < 3u; ++i)
                    dh[i] += ((int64_t)tn[i] * length / U - dh[i]) * (share / 50) / U;
            }
            const int32_t pitched = du + (tu - du) / 20;
            for (unsigned i = 0; i < 3u; ++i)
                next[i] = dh[i] + (int64_t)pitched * path_up[i] / U;
        }
        int32_t unit[3];
        if (portable_fixed_3d_normalize(next, unit))
            memcpy(d, unit, sizeof d);
        int32_t towards[3];
        for (unsigned i = 0; i < 3u; ++i)
        {
            offset[i] = (int64_t)s->glide_path.closest_q16[i] - s->position_q16[i];
            towards[i] = (int32_t)offset[i];
        }
        distance = vector_length(offset);
        const int32_t radius = s->glide_path.radius_q16 > 0 ? s->glide_path.radius_q16 : Q;
        height_q16 = (int32_t)(-(int64_t)up_part(towards, path_up) * Q / radius);
        tube_u = distance > radius ? metres_to_units(distance - radius) : 0;
    }
    int32_t v = velocity_units(s);
    if (!cannon && v > units_per_frame(r->glide_speed_q16))
        v = portable_fixed_3d_mul(v, MESH_KART_MK8_COAST_Q16);
    int32_t left[3], normal[3];
    side_axis(s, left);
    {
        const int64_t n[3] = {((int64_t)d[1] * left[2] - (int64_t)d[2] * left[1]) / U,
                              ((int64_t)d[2] * left[0] - (int64_t)d[0] * left[2]) / U,
                              ((int64_t)d[0] * left[1] - (int64_t)d[1] * left[0]) / U};
        if (!portable_fixed_3d_normalize(n, normal))
            memcpy(normal, path_up, sizeof normal);
        if (up_part(normal, path_up) < 0)
            for (unsigned i = 0; i < 3u; ++i)
                normal[i] = -normal[i];
    }
    /* Pulling up lifts up to 0.005 v^2 more well below the path, none above it. */
    const int32_t pitch = s->glide_pitch_q16;
    const int32_t climb = height_q16 <= -45875 ? 328 : height_q16 >= 3277 ? 0
                          : 328 - (int32_t)((int64_t)(height_q16 + 45875) * 328 / 49152);
    const int32_t ramp = s->glide_ticks >= 125u ? Q : (int32_t)s->glide_ticks * 524;
    const int64_t c1_q30 = pitch >= 0 ? (int64_t)ramp * MESH_KART_MK8_GLIDE_DIVE_Q30 / Q
                                      : (int64_t)climb * (INT64_C(1) << 14);
    const int64_t lift = cannon ? 0 : glide_square(v, MESH_KART_MK8_GLIDE_LIFT_Q30 - (int64_t)pitch * c1_q30 / Q);
    const int64_t drag = -328 + (int64_t)pitch * glide_square(v, pitch >= 0 ? -1073742 : -193273) / Q;
    const int32_t fall = found ? MESH_KART_MK8_GLIDE_GRAVITY_Q16 : 1966 /* 0.03 */;
    int64_t w[3];
    for (unsigned i = 0; i < 3u; ++i)
        w[i] = (int64_t)d[i] * (v - drag) / U + (int64_t)normal[i] * lift / U - (int64_t)path_up[i] * fall / U;
    if (found)
    {
        /* Attraction toward the path: across it on a cannon, along its up otherwise. */
        const int32_t settle = s->glide_ticks >= 50u ? Q : (int32_t)s->glide_ticks * 1311;
        if (cannon)
        {
            const int64_t along = (offset[0] * s->glide_path.tangent_q30[0] + offset[1] * s->glide_path.tangent_q30[1] +
                                   offset[2] * s->glide_path.tangent_q30[2]) / U;
            const int64_t gain = 1966 - portable_fixed_3d_mul(settle, 1311); /* 0.03 - 0.02 settle */
            for (unsigned i = 0; i < 3u; ++i)
                w[i] += metres_to_units(offset[i] - along * s->glide_path.tangent_q30[i] / U) * gain / Q;
        }
        else
        {
            const int64_t vertical = (offset[0] * path_up[0] + offset[1] * path_up[1] + offset[2] * path_up[2]) / U;
            const int64_t gain = 107374 - (int64_t)settle * 96637 / Q; /* (0.0001 - 0.00009 settle) Q30 */
            for (unsigned i = 0; i < 3u; ++i)
                w[i] += metres_to_units(vertical * path_up[i] / U) * gain / (INT64_C(1) << 30);
        }
        if (tube_u > 0 && distance)
            for (unsigned i = 0; i < 3u; ++i)
                w[i] += offset[i] * tube_u / distance / 100;
    }
    int32_t heading[3];
    if (portable_fixed_3d_normalize(w, heading))
        memcpy(d, heading, sizeof d);
    int64_t speed = vector_length(w);
    if (cannon)
        speed = v + Q > MESH_KART_MK8_GLIDE_CANNON_TOP_Q16 ? MESH_KART_MK8_GLIDE_CANNON_TOP_Q16 : v + Q;
    else if (v < MESH_KART_MK8_GLIDE_MIN_Q16)
        speed += Q / 10;
    speed = speed > 32 * Q ? 32 * Q : speed;
    memcpy(s->glide_heading_q30, d, sizeof d);
    for (unsigned i = 0; i < 3u; ++i)
        s->velocity_q16[i] = units_to_speed((int64_t)d[i] * speed / U);
    s->speed_q16 = units_to_speed(speed);
    /* The kart's up eases toward the path's (0.02 a tick, 0.05 on a
     * cannon) and its nose follows the heading. */
    int32_t eased[3], forward[3];
    for (unsigned i = 0; i < 3u; ++i)
        eased[i] = s->up_q30[i] + portable_fixed_3d_mul(path_up[i] - s->up_q30[i], cannon ? 3277 : 1311);
    if (nonzero(eased))
        normalize(eased, s->up_q30);
    const int32_t across = up_part(d, s->up_q30);
    for (unsigned i = 0; i < 3u; ++i)
        forward[i] = d[i] - unit_product(across, s->up_q30[i]);
    if (nonzero(forward))
        normalize(forward, s->forward_q30);
    const int64_t tube = tube_u * Q / (MESH_KART_MK8_GLIDE_TUBE_U * Q);
    return tube > 16 * Q ? 16 * Q : (int32_t)tube;
}
/* Cannon flight end: the target floor point lifted to the collision sphere. */
static void cannon_end(const PortableMeshSurface *cannon, const MeshKartShape *shape, int32_t out[3])
{
    memcpy(out, cannon->cannon_target_q16, 3u * sizeof(int32_t));
    out[2] += shape->radius_q16 + shape->ride_height_q16;
}
static PortableMeshStatus cannon_fire(PortableMeshKartState *s, const MeshKartShape *shape,
                                      const PortableMeshCourse *c, uint32_t surface)
{
    PortableMeshSurface cannon;
    if (!read_surface(c, surface, &cannon) || cannon.type != 9u)
        return PORTABLE_MESH_INVALID;
    int32_t end[3];
    cannon_end(&cannon, shape, end);
    const uint64_t distance = portable_fixed_3d_sqrt(distance_squared(s->position_q16, end));
    const uint32_t per_tick = (uint32_t)portable_fixed_3d_dt(cannon.launch_q16);
    uint64_t ticks = per_tick ? (distance + per_tick - 1u) / per_tick : 1u;
    ticks = ticks < 1u ? 1u : ticks > 4096u ? 4096u : ticks;
    cancel_drift(s);
    memset(s->gravity_up_q30, 0, sizeof s->gravity_up_q30);
    s->cannon_surface = surface;
    s->cannon_tick = 0;
    s->cannon_ticks = (uint16_t)ticks;
    memcpy(s->cannon_from_q16, s->position_q16, sizeof s->cannon_from_q16);
    return PORTABLE_MESH_OK;
}
/* One flight tick: a straight chord to the target under an arc a quarter of
 * its horizontal length high (2..64 m); the last tick leaves the kart on the
 * exit heading at engine speed, airborne just above the landing floor. */
static PortableMeshStatus cannon_flight(PortableMeshKartState *s, const MeshKartShape *shape,
                                        const PortableMeshCourse *c, int32_t exit_speed)
{
    PortableMeshSurface cannon;
    if (!read_surface(c, s->cannon_surface, &cannon) || cannon.type != 9u)
        return PORTABLE_MESH_INVALID;
    int32_t end[3], before[3];
    cannon_end(&cannon, shape, end);
    memcpy(before, s->position_q16, sizeof before);
    const uint32_t tick = s->cannon_tick + 1u, ticks = s->cannon_ticks;
    const int64_t dx = (int64_t)end[0] - s->cannon_from_q16[0], dy = (int64_t)end[1] - s->cannon_from_q16[1];
    int64_t height = (int64_t)portable_fixed_3d_sqrt((uint64_t)(dx * dx + dy * dy)) / 4;
    height = height < 2 * Q ? 2 * Q : height > 64 * Q ? 64 * Q : height;
    for (unsigned i = 0; i < 3u; ++i)
        s->position_q16[i] = s->cannon_from_q16[i] +
            (int32_t)(((int64_t)end[i] - s->cannon_from_q16[i]) * tick / ticks);
    s->position_q16[2] += (int32_t)(height * 4 * tick * (ticks - tick) / ((int64_t)ticks * ticks));
    const int32_t tick_rate = portable_fixed_3d_dt(Q);
    for (unsigned i = 0; i < 3u; ++i)
        s->velocity_q16[i] = (int32_t)(((int64_t)s->position_q16[i] - before[i]) * Q / tick_rate);
    const int64_t heading[3] = {tick < ticks ? dx : cannon.cannon_heading_q16[0],
                                tick < ticks ? dy : cannon.cannon_heading_q16[1], 0};
    int32_t forward[3];
    if (portable_fixed_3d_normalize(heading, forward))
        memcpy(s->forward_q30, forward, sizeof forward);
    s->up_q30[0] = s->up_q30[1] = 0;
    s->up_q30[2] = U;
    s->grounded = 0;
    s->contact_triangle = s->contact_surface = PORTABLE_MESH_ABSENT;
    if (tick < ticks)
    {
        s->cannon_tick = (uint16_t)tick;
        return PORTABLE_MESH_OK;
    }
    s->cannon_tick = s->cannon_ticks = 0;
    s->cannon_surface = 0;
    s->speed_q16 = exit_speed;
    s->outside_drift_q16 = 0;
    for (unsigned i = 0; i < 3u; ++i)
        s->velocity_q16[i] = unit_product(exit_speed, s->forward_q30[i]);
    return PORTABLE_MESH_OK;
}
/* MK8 course wall (0x17b918, speed loss 0x17b2fc), in units/frame: with c the
 * incidence cosine (less when the kart faces away), grounded speed keeps
 * (1-c)*0.9 and airborne 0.3+0.69*sin; an outward knock of
 * clamp(s*|d|, 0.2, 2..3) holds 14 ticks; a hit faster than 0.5 kicks the
 * heading away (glancing hits) and starts a 34-tick effect cooldown. */
static void wall_response(PortableMeshKartState *s, const MeshKartWall *w, int32_t slide, int grounded)
{
    const int32_t d = (int32_t)((int64_t)w->approach_q16 * Q / MESH_KART_MK8_UNIT_SPEED_Q16);
    const int32_t u = (int32_t)(portable_fixed_3d_dot(w->n, s->forward_q30) / U / 16384);
    int32_t c = w->motion_q16 ? (int32_t)(-(int64_t)w->approach_q16 * Q / w->motion_q16) : 0;
    if (u > 0)
        c -= u / 2;
    const int32_t cc = c < 0 ? 0 : c > Q ? Q : c;
    const int32_t keep = grounded ? portable_fixed_3d_mul(Q - cc, MESH_KART_MK8_WALL_KEEP_Q16)
                                  : MESH_KART_MK8_WALL_AIR_Q16 +
                                        portable_fixed_3d_mul(MESH_KART_MK8_WALL_AIR_SPAN_Q16,
                                                              (int32_t)portable_fixed_3d_sqrt((uint64_t)((int64_t)Q * Q - (int64_t)cc * cc)));
    /* The engine speed takes the loss; the slid motion keeps its sideways part. */
    int32_t travel[3];
    travel_direction(s, travel);
    const int32_t along = (int32_t)(portable_fixed_3d_dot(s->velocity_q16, travel) / U);
    s->speed_q16 = portable_fixed_3d_mul(s->speed_q16, keep);
    for (unsigned i = 0; i < 3u; ++i)
        s->velocity_q16[i] += unit_product(s->speed_q16 - along, travel[i]);
    const int away = d < 0 && u > 0 && s->speed_q16 > 0, glance = c < (grounded ? 45875 : 58982);
    const int32_t into = d < 0 && u > 0 ? (away ? d - u : portable_fixed_3d_mul(d, 45875)) : d;
    const int32_t scale = glance ? (away ? Q : 3 * Q / 2) : Q / 2, high = away ? 3 * Q : 2 * Q;
    const int32_t raw = portable_fixed_3d_mul(-into, scale);
    const int32_t knock = raw < Q / 5 ? Q / 5 : raw > high ? high : raw;
    int32_t side[3], push[3];
    side_axis(s, side);
    if (raw > 0)
    {
        /* Only the old knock's sideways part survives a fresh hit. */
        const int32_t lateral = (int32_t)(portable_fixed_3d_dot(s->knock_q16, side) / U);
        for (unsigned i = 0; i < 3u; ++i)
            s->knock_q16[i] = unit_product(lateral, side[i]);
    }
    for (unsigned i = 0; i < 3u; ++i)
        push[i] = unit_product(portable_fixed_3d_mul(knock, MESH_KART_MK8_UNIT_SPEED_Q16), w->n[i]);
    knock_combine(s->knock_q16, push);
    knock_hold(s, slide, MESH_KART_MK8_WALL_KNOCK_TICKS);
    if (d >= MESH_KART_MK8_WALL_EVENT_Q16 || s->wall_cooldown)
        return;
    s->wall_cooldown = MESH_KART_MK8_WALL_COOLDOWN;
    if (!glance)
        return;
    const int32_t low = grounded ? Q / 50 : 3 * Q / 100;
    int32_t kick = portable_fixed_3d_mul(983 /* 0.015 */, portable_fixed_3d_mul(units_per_frame(w->motion_q16), c));
    kick = kick < low ? low : kick > 2 * Q ? 2 * Q : kick;
    /* Turn the nose toward the wall normal, the other way when facing away. */
    const int toward = portable_fixed_3d_dot(w->n, side) >= 0;
    s->wall_yaw_q24 = (toward == (u <= 0) ? kick : -kick) * 256;
}
PortableMeshStatus portable_mesh_kart_tick_effects(PortableMeshKartState *state,
    const PortableMeshKartRules *r, const PortableMeshCourse *c, PortableMeshKartInput input,
    const PortableMeshKartEffects *effects, PortableMeshKartEvents *events)
{
    if (!state || !events || !c || !c->triangle || !c->surface || !c->triangle_count ||
        !portable_mesh_kart_effects_valid(r, effects))
        return PORTABLE_MESH_INVALID;
    effects = nonidentity_effects(effects);
    if (!portable_mesh_kart_state_valid(state) || state->drift_charge > r->charge_thresholds[2] ||
        input.buttons > 15u)
        return PORTABLE_MESH_INVALID;
    PortableMeshKartState s = *state;
    PortableMeshKartEvents e = {0};
    /* MK8 anti-gravity (0x175998, 0x176fd8) drives the PRSA/PRTA rows. */
    PortableMeshKartRules anti;
    if (s.anti_gravity)
    {
        anti = *r;
        anti.forward_speed_q16 = r->antigravity_speed_q16;
        anti.handling_turn_rate_q16 = r->antigravity_turn_rate_q16;
        anti.drift_turn_rate_q16 = r->antigravity_drift_turn_rate_q16;
        anti.spin_gain_q16 = r->antigravity_spin_gain_q16;
        r = &anti;
    }
    s.anti_gravity_rate = s.anti_gravity ? (uint8_t)(s.anti_gravity_rate < MESH_KART_MK8_ANTIGRAVITY_RAMP
                                                         ? s.anti_gravity_rate + 1u : s.anti_gravity_rate)
                          : (uint8_t)(s.anti_gravity_rate ? s.anti_gravity_rate - 1u : 0u);
    const MeshKartShape shape = kart_shape(r, effects);
    int32_t forward_cap = effects ?
        portable_fixed_3d_mul(r->forward_speed_q16, effects->speed_ratio_q16) : r->forward_speed_q16;
    const int star = effects && effects->star;
    if (star)
        forward_cap = portable_fixed_3d_mul(forward_cap, effects->star_cap_ratio_q16);
    const int32_t reverse_cap = effects ?
        portable_fixed_3d_mul(r->reverse_speed_q16, effects->speed_ratio_q16) : r->reverse_speed_q16;
    const uint16_t raw_buttons = input.buttons;
    if (s.cannon_ticks)
    {
        /* Flight ignores input and collision; clocks, air time included, wait for landing. */
        const PortableMeshStatus flown = cannon_flight(&s, &shape, c, forward_cap);
        if (flown != PORTABLE_MESH_OK)
            return flown;
        s.previous_buttons = raw_buttons;
        if (!portable_mesh_kart_state_valid(&s))
            return PORTABLE_MESH_INVALID;
        *state = s;
        *events = e;
        return PORTABLE_MESH_OK;
    }
    const int cancel = effects && effects->cancel_drift;
    if (cancel)
    {
        cancel_drift(&s);
        memset(s.boost_ticks, 0, sizeof s.boost_ticks);
        s.panel_ticks = s.rocket_ticks = s.slip_ticks = s.trick_boost_ticks = s.spin_boost_ticks = 0;
        s.spin_armed = 0;
    }
    /* MK8 slipstream charge (0x167158): in the strip refills a 15-tick grace;
     * charging blocked while the boost runs; the 91st tick fires it. */
    if (s.slip_in)
        s.slip_grace = MESH_KART_MK8_SLIP_GRACE;
    else if (s.slip_grace)
        --s.slip_grace;
    if ((s.slip_in || s.slip_grace) && !s.slip_ticks)
    {
        if (s.slip_charge++ >= MESH_KART_MK8_SLIP_CHARGE)
        {
            s.slip_ticks = MESH_KART_MK8_SLIP_TICKS;
            s.slip_charge = 0;
        }
    }
    else
        s.slip_charge = 0;
    s.slip_in = 0;
    if (s.burnout_ticks)
    {
        --s.burnout_ticks;
        input.buttons &= (uint16_t)~(PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP);
    }
    if (effects && effects->inhibit_controls)
    {
        input.steering = 0;
        input.buttons &= PORTABLE_MESH_RESCUE;
        s.steer_filtered_q16 = 0;
        cancel_drift(&s);
    }
    if (c->carry && s.grounded && s.contact_triangle != PORTABLE_MESH_ABSENT &&
        !c->carry(c->context, s.contact_triangle, s.position_q16, s.forward_q30, s.velocity_q16))
        return PORTABLE_MESH_INVALID;
    const int was_grounded = s.grounded;
    const uint32_t prior_surface = s.contact_surface;
    PortableMeshSurface surf = {0};
    surf.speed_ratio_q16 = surf.gravity_ratio_q16 = Q;
    if (s.contact_surface != PORTABLE_MESH_ABSENT &&
        !read_surface(c, s.contact_surface, &surf))
        return PORTABLE_MESH_INVALID;
    if (c->gravity_q16 < 0 || c->gravity_q16 > 67108864 || surf.friction_q16 < 0 ||
        surf.friction_q16 > 262144 || surf.speed_ratio_q16 < 0 || surf.speed_ratio_q16 > 262144 ||
        surf.gravity_ratio_q16 < 0 || surf.gravity_ratio_q16 > 262144 ||
        surf.rolling_resistance_q16 < 0 || surf.rolling_resistance_q16 > 67108864 ||
        surf.launch_q16 < 0 || surf.launch_q16 > 16777216)
        return PORTABLE_MESH_INVALID;
    /* MK8 glider: a glide ramp arms it, landing short disarms it; 15
     * airborne ticks (5 on a cannon path) facing the path open it. While
     * gliding Hop dives and Brake pulls up. */
    if (s.grounded && s.glide != 2u)
        s.glide = (uint8_t)(surf.glide && !cancel);
    else if (s.glide == 1u &&
             s.air_ticks > (s.glide_path.cannon ? MESH_KART_MK8_GLIDE_CANNON_AIR_TICKS : MESH_KART_MK8_GLIDE_AIR_TICKS))
    {
        if (s.glide_path.found && !cancel &&
            portable_fixed_3d_dot(s.forward_q30, s.glide_path.tangent_q30) / U >= MESH_KART_MK8_GLIDE_ALIGN_Q30)
            glide_open(&s, &e);
        else
            s.glide = 0;
    }
    else if (s.glide == 2u && cancel)
        glide_end(&s, &e);
    int32_t stick = 0;
    if (s.glide == 2u)
    {
        stick = (input.buttons & PORTABLE_MESH_HOP) ? Q : (input.buttons & PORTABLE_MESH_BRAKE) ? -Q : 0;
        input.buttons &= (uint16_t)~PORTABLE_MESH_HOP;
    }
    int32_t turned = 0;
    if (!effects || !effects->inhibit_controls)
        turned = steer(&s, r, input);
    int flying = 0;
    if (s.glide == 2u)
    {
        if (turned)
            rotate_direction(s.glide_heading_q30, s.up_q30, turned, s.glide_heading_q30);
        /* Contact after the grace accumulates; past 0.3 the kart lands. */
        if (s.grounded && s.glide_ticks >= MESH_KART_MK8_GLIDE_GRACE)
        {
            s.glide_land_q16 += 3932 /* 0.06 */ + portable_fixed_3d_mul(s.glide_pitch_q16,
                                                                          s.glide_pitch_q16 >= 0 ? 2621 : 1966);
            if (s.glide_land_q16 > MESH_KART_MK8_GLIDE_LAND_Q16)
                glide_end(&s, &e);
        }
        else
            flying = 1;
    }
    if (s.wall_yaw_q24)
    {
        /* MK8's wall kick adds to the heading rate and eases 8% a tick. */
        const int32_t turn =
            portable_fixed_3d_dt((int32_t)(s.wall_yaw_q24 * MESH_KART_MK8_TURNS_PER_SECOND_Q16 / (1 << 24)));
        if (turn)
            rotate_direction(s.forward_q30, s.up_q30, turn, s.forward_q30);
        s.wall_yaw_q24 -= portable_fixed_3d_mul(s.wall_yaw_q24, MESH_KART_MK8_WALL_KICK_DECAY_Q16);
        if (s.wall_yaw_q24 > -16777 && s.wall_yaw_q24 < 16777) /* below 0.001 rad */
            s.wall_yaw_q24 = 0;
    }
    const uint16_t fresh = (uint16_t)(input.buttons & (uint16_t)~s.previous_buttons);
    s.previous_buttons = raw_buttons;
    if (s.trick_age && s.trick_age < UINT8_MAX)
        ++s.trick_age;
    /* MK8 trick (0x179cfc): a hop off a trick ramp, on it or up to 7 ticks
     * after leaving it, at 0.65 of top speed, 20+ ticks after the last one.
     * A launching surface replaces the hop's own lift, as MK8's springs do. */
    int launching = 0;
    if (!cancel && (fresh & PORTABLE_MESH_HOP) && s.trick_surface &&
        (s.grounded || s.air_ticks <= MESH_KART_MK8_TRICK_AIR_TICKS) &&
        (!s.trick_age || s.trick_age > MESH_KART_MK8_TRICK_LAND_TICKS) &&
        top_ratio(&s, r) >= MESH_KART_MK8_TRICK_SPEED_Q16)
    {
        const int side = input.steering > MESH_KART_MK8_SIDE_DEAD_ZONE ? 1
                         : input.steering < -MESH_KART_MK8_SIDE_DEAD_ZONE ? -1 : 0;
        s.trick_kind = (int8_t)(side ? 2 * side : 1);
        s.trick_age = 1;
        e.tricked = 1;
        launching = !s.grounded || (surf.type == 5u && surf.launch_q16 > 0);
    }
    if ((fresh & PORTABLE_MESH_HOP) && s.grounded && e.tricked)
    {
        if (!launching)
            for (unsigned i = 0; i < 3u; ++i)
                s.velocity_q16[i] += unit_product(r->hop_velocity_q16, s.up_q30[i]);
        e.hopped = !launching;
        if (!launching)
            s.grounded = 0;
    }
    else if ((fresh & PORTABLE_MESH_HOP) && s.grounded)
    {
        for (unsigned i = 0; i < 3u; ++i)
            s.velocity_q16[i] += unit_product(r->hop_velocity_q16, s.up_q30[i]);
        s.grounded = 0;
        e.hopped = 1;
        if (!cancel &&
            (input.steering > MESH_KART_MK8_SIDE_DEAD_ZONE || input.steering < -MESH_KART_MK8_SIDE_DEAD_ZONE) &&
            s.speed_q16 >
                portable_fixed_3d_mul(forward_cap, r->minimum_drift_speed_ratio_q16))
            s.drift_direction = input.steering < 0 ? -1 : 1;
    }
    /* Hop first, then steer: holding Hop in the air commits the drift side. */
    else if (!cancel && !s.grounded && !s.drift_direction &&
             (input.buttons & (PORTABLE_MESH_HOP | PORTABLE_MESH_ACCELERATE)) ==
                 (PORTABLE_MESH_HOP | PORTABLE_MESH_ACCELERATE) &&
             (input.steering > MESH_KART_MK8_SIDE_DEAD_ZONE || input.steering < -MESH_KART_MK8_SIDE_DEAD_ZONE) &&
             s.speed_q16 > portable_fixed_3d_mul(forward_cap, r->minimum_drift_speed_ratio_q16))
        s.drift_direction = input.steering < 0 ? -1 : 1;
    if (s.drift_direction)
    {
        /* MK8 0x16fcfc continues/charges with Accel + Hop even while braking.
         * Losing throttle or reaching the drift speed gate cancels the
         * charge; Brake itself never releases a mini-turbo or starts a slide. */
        if (!(input.buttons & PORTABLE_MESH_ACCELERATE) ||
            s.speed_q16 <= portable_fixed_3d_mul(forward_cap, r->minimum_drift_speed_ratio_q16))
            cancel_drift(&s);
        else if (!(input.buttons & PORTABLE_MESH_HOP))
        {
            if (s.grounded && s.drift_tier)
            {
                const unsigned t = s.drift_tier - 1u;
                if (s.boost_ticks[t] < r->boost_ticks[t])
                    s.boost_ticks[t] = r->boost_ticks[t];
                e.released = 1;
            }
            cancel_drift(&s);
        }
        else if (s.grounded)
        {
            const uint32_t rate = (input.steering * s.drift_direction >= MESH_KART_MK8_INWARD_STICK)
                                      ? r->inward_charge_rate
                                      : r->neutral_charge_rate;
            s.drift_charge = rate >= r->charge_thresholds[2] - s.drift_charge
                                 ? r->charge_thresholds[2]
                                 : s.drift_charge + rate;
            uint8_t tier = 0;
            for (unsigned i = 0; i < 3u; ++i)
                if (s.drift_charge >= r->charge_thresholds[i])
                    tier = (uint8_t)(i + 1u);
            e.tier_changed = tier != s.drift_tier;
            s.drift_tier = tier;
        }
    }
    /* MK8 spin turn: Hop held on the ground with no drift side yet, steering
     * fills a decaying accumulator; past ±30 a drift starts without a hop. */
    const int boosting = portable_mesh_kart_boosting(&s) || (effects && effects->boost_active);
    if (!cancel && s.grounded && !s.drift_direction && !boosting &&
        (input.buttons & (PORTABLE_MESH_HOP | PORTABLE_MESH_ACCELERATE)) ==
            (PORTABLE_MESH_HOP | PORTABLE_MESH_ACCELERATE) &&
        2 * (int64_t)s.speed_q16 > forward_cap)
    {
        s.spin_q16 = portable_fixed_3d_mul(s.spin_q16, MESH_KART_MK8_SPIN_DECAY_Q16) +
                     portable_fixed_3d_mul((int32_t)input.steering * 2, r->spin_gain_q16);
        if (s.spin_q16 > MESH_KART_MK8_SPIN_LIMIT_Q16 || s.spin_q16 < -MESH_KART_MK8_SPIN_LIMIT_Q16)
        {
            s.drift_direction = s.spin_q16 < 0 ? -1 : 1;
            s.spin_q16 = 0;
        }
    }
    else
        s.spin_q16 = 0;
    /* MK8 brake slide: braking into a hard turn at speed loosens grip and
     * swings the tail for 15 ticks per unit/frame (40 to 80). */
    int slide_ended = 0;
    if (s.slide_ticks)
    {
        if (!s.grounded || units_per_frame(s.speed_q16) < Q)
            s.slide_ticks = 0;
        else
            --s.slide_ticks;
        slide_ended = !s.slide_ticks;
    }
    else if (!cancel && s.grounded && !s.drift_direction && (input.buttons & PORTABLE_MESH_BRAKE) &&
             (s.steer_filtered_q16 >= Q / 2 || s.steer_filtered_q16 <= -Q / 2) &&
             s.speed_q16 >= portable_fixed_3d_mul(forward_cap, MESH_KART_MK8_SLIDE_SPEED_Q16))
    {
        const uint32_t ticks = (uint32_t)units_per_frame(s.speed_q16) * 15u / (uint32_t)Q;
        /* MK8 counts D+0xac down and leaves when it passes below zero, so a
         * slide of N lasts N+1 ticks. */
        s.slide_ticks = (uint8_t)(1u + (ticks < MESH_KART_MK8_SLIDE_MIN_TICKS ? MESH_KART_MK8_SLIDE_MIN_TICKS
                                        : ticks > MESH_KART_MK8_SLIDE_MAX_TICKS ? MESH_KART_MK8_SLIDE_MAX_TICKS
                                                                               : ticks));
    }
    if (!cancel && s.grounded && surf.type == 4u)
    {
        if (s.panel_ticks < r->panel_boost_ticks)
            s.panel_ticks = r->panel_boost_ticks;
        e.panel = 1;
    }
    if (effects && (int64_t)forward_cap * surf.speed_ratio_q16 / Q > 256 * Q)
        return PORTABLE_MESH_INVALID;
    const MeshKartGround ground = ground_rules(&surf, r, star || (effects && effects->boost_active && !cancel) ||
                                                            s.panel_ticks);
    const int32_t cap = portable_fixed_3d_mul(forward_cap, ground.speed);
    /* MK8's boost owner: every live slot counts down and the highest applies:
     * mushroom (11) > dash panel (10) > rocket start (7) > MT3..MT1 (5..3).
     * Dash and mushroom caps scale the larger of top speed and 7.6 u/f. */
    int32_t boost_cap = 0, boost_accel = 0;
    const int32_t floor_cap = forward_cap > MESH_KART_MK8_FLOOR_CAP_Q16 ? forward_cap : MESH_KART_MK8_FLOOR_CAP_Q16;
    s.last_boost_tier = 0;
    if (s.spin_boost_ticks)
    {
        /* Slot 2, below the mini-turbos: the anti-gravity spin boost. */
        boost_cap = portable_fixed_3d_mul(cap, MESH_KART_MK8_ROCKET_CAP_Q16);
        boost_accel = portable_fixed_3d_mul(MESH_KART_MK8_ROCKET_ACCEL_Q16, ground.boost);
    }
    for (unsigned i = 0; i < 3u; ++i)
        if (s.boost_ticks[i])
        {
            boost_cap = portable_fixed_3d_mul(cap, r->boost_cap_ratio_q16[i]);
            boost_accel = portable_fixed_3d_mul(r->boost_acceleration_q16[i], ground.boost);
            s.last_boost_tier = (uint8_t)(i + 1u);
        }
    if (s.trick_boost_ticks)
    {
        boost_cap = portable_fixed_3d_mul(cap, r->trick_cap_ratio_q16);
        boost_accel = portable_fixed_3d_mul(r->trick_acceleration_q16, ground.boost);
    }
    if (s.rocket_ticks)
    {
        boost_cap = portable_fixed_3d_mul(cap, MESH_KART_MK8_ROCKET_CAP_Q16);
        boost_accel = portable_fixed_3d_mul(MESH_KART_MK8_ROCKET_ACCEL_Q16, ground.boost);
    }
    /* Slot 8, above the rocket: off-road for 12 ticks cancels it. */
    s.offroad_ticks = s.grounded && ground.offroad
                          ? (uint8_t)(s.offroad_ticks < MESH_KART_MK8_OFFROAD_TICKS ? s.offroad_ticks + 1u
                                                                                  : MESH_KART_MK8_OFFROAD_TICKS)
                          : 0u;
    if (s.offroad_ticks >= MESH_KART_MK8_OFFROAD_TICKS)
        s.slip_ticks = 0;
    if (s.slip_ticks)
    {
        boost_cap = portable_fixed_3d_mul(cap, MESH_KART_MK8_SLIP_CAP_Q16);
        boost_accel = portable_fixed_3d_mul(MESH_KART_MK8_SLIP_ACCEL_Q16, ground.boost);
    }
    if (s.panel_ticks)
    {
        boost_cap = portable_fixed_3d_mul(floor_cap, r->panel_cap_ratio_q16);
        boost_accel = r->panel_acceleration_q16;
    }
    if (effects && effects->boost_active && !cancel)
    {
        boost_cap = portable_fixed_3d_mul(floor_cap, effects->boost_cap_ratio_q16);
        boost_accel = effects->boost_acceleration_q16;
    }
    /* MK8 slip (0x1778b8): a knock's hold keeps traction T raised; otherwise T
     * eases 0.005 a tick toward the surface's slide target and drifting,
     * brake slides and burnouts add to it. Sideways slip and the knock keep
     * friction*min(T, 0.99) a tick; the knock stays horizontal. */
    int32_t traction = s.traction_q16;
    /* Leaving the brake slide is SetSlide(0.245, 5): grip stays loose 5 ticks. */
    if (slide_ended)
        knock_hold(&s, ground.slide, MESH_KART_MK8_SLIDE_EXIT_TICKS);
    if (s.traction_hold_ticks)
        --s.traction_hold_ticks;
    else
    {
        s.traction_q16 += s.traction_q16 < ground.slide - MESH_KART_MK8_TRACTION_STEP_Q16 ? MESH_KART_MK8_TRACTION_STEP_Q16
                          : s.traction_q16 > ground.slide + MESH_KART_MK8_TRACTION_STEP_Q16 ? -MESH_KART_MK8_TRACTION_STEP_Q16
                                                                                          : ground.slide - s.traction_q16;
        traction = s.traction_q16 + (s.drift_direction ? MESH_KART_MK8_DRIFT_SLIDE_Q16 : 0) +
                   (s.slide_ticks ? MESH_KART_MK8_SLIDE_ADD_Q16 : 0) +
                   (s.burnout_ticks ? MESH_KART_MK8_BURNOUT_SLIDE_Q16 : 0);
    }
    if (traction > MESH_KART_MK8_TRACTION_CAP_Q16)
        traction = MESH_KART_MK8_TRACTION_CAP_Q16;
    const int32_t kept = portable_fixed_3d_mul(ground.friction, traction);
    for (unsigned i = 0; i < 3u; ++i)
        s.knock_q16[i] = portable_fixed_3d_mul(s.knock_q16[i], kept);
    const int32_t knock_up = up_part(s.knock_q16, s.up_q30);
    for (unsigned i = 0; i < 3u; ++i)
        s.knock_q16[i] -= unit_product(knock_up, s.up_q30[i]);
    /* A bump's push lands three ticks after contact and holds 12. */
    if (s.bump_delay && !--s.bump_delay)
    {
        knock_combine(s.knock_q16, s.bump_push_q16);
        knock_hold(&s, ground.slide, MESH_KART_MK8_BUMP_TICKS);
        memset(s.bump_push_q16, 0, sizeof s.bump_push_q16);
    }
    if (s.grounded && !flying)
    {
        s.speed_q16 = mk8_speed(&s, r, input.buttons, cap, portable_fixed_3d_mul(reverse_cap, ground.speed),
                                forward_cap, boost_cap, boost_accel,
                                star ? portable_fixed_3d_dt(effects->star_acceleration_q16) : 0, ground.friction);
        const int32_t rolling = portable_fixed_3d_dt(surf.rolling_resistance_q16);
        if (s.speed_q16 > 0)
            s.speed_q16 = s.speed_q16 > rolling ? s.speed_q16 - rolling : 0;
        const int32_t outside_target = s.drift_direction * r->outside_drift_angle_q16;
        s.outside_drift_q16 += portable_fixed_3d_mul(outside_target - s.outside_drift_q16,
                                                     response(r->outside_drift_recovery_q16));
        int32_t travel[3];
        travel_direction(&s, travel);
        /* Grip removes lateral slip; it must not attenuate the engine's
         * longitudinal acceleration, particularly while climbing a slope. */
        const int32_t along_travel = (int32_t)(portable_fixed_3d_dot(s.velocity_q16, travel) / U);
        uint64_t slip = 0;
        for (unsigned i = 0; i < 3u; ++i)
        {
            const int32_t transverse = s.velocity_q16[i] - unit_product(along_travel, travel[i]);
            slip += (uint64_t)((int64_t)transverse * transverse);
            s.velocity_q16[i] = unit_product(s.speed_q16, travel[i]) + portable_fixed_3d_mul(transverse, kept);
        }
        if (s.slide_ticks && s.steer_filtered_q16)
        {
            int32_t yaw = portable_fixed_3d_mul(units_per_frame((int32_t)portable_fixed_3d_sqrt(slip)),
                                                portable_fixed_3d_mul(MESH_KART_MK8_SLIDE_YAW_GAIN_Q16, ground.steer));
            if (yaw > MESH_KART_MK8_SLIDE_YAW_LIMIT_Q16)
                yaw = MESH_KART_MK8_SLIDE_YAW_LIMIT_Q16;
            const int32_t turn = portable_fixed_3d_dt(radians_rate(s.steer_filtered_q16 < 0 ? -yaw : yaw));
            if (turn)
                rotate_direction(s.forward_q30, s.up_q30, turn, s.forward_q30);
        }
    }
    else if (!flying && s.air_ticks >= MESH_KART_MK8_AIR_TICKS)
    {
        /* MK8 drags a kart that has flown 20 ticks: speed and the motion
         * across gravity lose 0.5% a tick; the fall is untouched. */
        const int32_t *g = gravity_up(&s);
        const int32_t rising = up_part(s.velocity_q16, g);
        for (unsigned i = 0; i < 3u; ++i)
        {
            const int32_t vertical = unit_product(rising, g[i]);
            s.velocity_q16[i] =
                vertical + portable_fixed_3d_mul(s.velocity_q16[i] - vertical, MESH_KART_MK8_AIR_Q16);
        }
        s.speed_q16 = portable_fixed_3d_mul(s.speed_q16, MESH_KART_MK8_AIR_Q16);
    }
    if (flying)
    {
        const int32_t tube = glide_flight(&s, r, stick);
        if (!s.glide_path.cannon && tube >= Q && s.glide_ticks >= MESH_KART_MK8_GLIDE_GRACE)
        {
            s.glide_land_q16 += 3932 + portable_fixed_3d_mul(s.glide_pitch_q16, s.glide_pitch_q16 >= 0 ? 2621 : 1966);
            if (s.glide_land_q16 > MESH_KART_MK8_GLIDE_LAND_Q16)
            {
                glide_end(&s, &e);
            }
        }
    }
    /* A surface launch leaves supported traction available for this turn.
     * Manual hops already became airborne above and retain their old ordering. */
    if (s.grounded && surf.type == 5u && surf.launch_q16 > 0)
    {
        int32_t launch = surf.launch_q16;
        if (surf.jump_max_distance_q16 && c->gravity_q16 > 0)
        {
            const uint64_t horizontal_squared =
                (uint64_t)((int64_t)s.velocity_q16[0]*s.velocity_q16[0]) +
                (uint64_t)((int64_t)s.velocity_q16[1]*s.velocity_q16[1]);
            const uint64_t horizontal = portable_fixed_3d_sqrt(horizontal_squared);
            if (horizontal)
            {
                const int64_t ceiling = (int64_t)c->gravity_q16*surf.jump_max_distance_q16 /
                                        (2*(int64_t)horizontal);
                if (ceiling < launch)
                    launch = (int32_t)ceiling;
            }
        }
        for (unsigned i = 0; i < 3u; ++i)
            s.velocity_q16[i] += unit_product(launch, s.up_q30[i]);
        s.grounded = 0;
        e.hopped = 1;
    }
    const int32_t gravity = portable_fixed_3d_mul(c->gravity_q16, surf.gravity_ratio_q16);
    int32_t up[3];
    memcpy(up, gravity_up(&s), sizeof up);
    const int32_t fall = portable_fixed_3d_dt(gravity);
    /* MK8 (0x175ebc): on the floor, gravity pushes into it along its normal,
     * so slopes neither roll a kart downhill nor slow its climb. */
    int32_t pull[3];
    memcpy(pull, up, sizeof pull);
    if (s.grounded && s.contact_triangle != PORTABLE_MESH_ABSENT)
    {
        PortableMeshTriangle floor;
        if (!c->triangle(c->context, s.contact_triangle, &floor) ||
            !portable_mesh_query_normal(&floor, pull))
            return PORTABLE_MESH_INVALID;
    }
    for (unsigned i = 0; i < 3u && !flying; ++i)
        s.velocity_q16[i] -= unit_product(fall, pull[i]);
    const int32_t rising = up_part(s.velocity_q16, up);
    if (!flying && rising < -r->terminal_fall_speed_q16)
        for (unsigned i = 0; i < 3u; ++i)
            s.velocity_q16[i] += unit_product(-r->terminal_fall_speed_q16 - rising, up[i]);
    int32_t displacement[3];
    uint32_t maximum = 0;
    for (unsigned i = 0; i < 3u; ++i)
    {
        if (s.velocity_q16[i] < -16777216 || s.velocity_q16[i] > 16777216)
            return PORTABLE_MESH_INVALID;
        displacement[i] = portable_fixed_3d_dt(s.velocity_q16[i] + s.knock_q16[i]);
        const uint32_t magnitude =
            (uint32_t)(displacement[i] < 0 ? -displacement[i] : displacement[i]);
        if (magnitude > maximum)
            maximum = magnitude;
    }
    const uint32_t unit = (uint32_t)(shape.radius_q16 / 2 ? shape.radius_q16 / 2 : 1);
    uint32_t steps = maximum / unit + (maximum % unit != 0);
    if (!steps)
        steps = 1;
    if (steps > MAX_STEPS)
        return PORTABLE_MESH_COLLISION_BUDGET;
    int32_t prior[3] = {0, 0, 0}, start[3];
    memcpy(start, s.position_q16, sizeof start);
    uint32_t cannon = PORTABLE_MESH_ABSENT;
    MeshKartWall wall = {0};
    MeshKartFloor floor_contact = {0};
    s.grounded = 0;
    s.contact_triangle = s.contact_surface = PORTABLE_MESH_ABSENT;
    for (uint32_t step = 1; step <= steps; ++step)
    {
        int32_t before[3];
        memcpy(before, s.position_q16, sizeof(before));
        s.grounded = 0;
        s.contact_triangle = s.contact_surface = PORTABLE_MESH_ABSENT;
        for (unsigned i = 0; i < 3u; ++i)
        {
            const int32_t cumulative = (int32_t)((int64_t)displacement[i] * step / steps);
            s.position_q16[i] += cumulative - prior[i];
            prior[i] = cumulative;
            if (s.position_q16[i] < -LIMIT || s.position_q16[i] > LIMIT)
                return PORTABLE_MESH_OUTSIDE;
        }
        const PortableMeshStatus swept = sweep_faces(before, s.position_q16, r, &shape, c, up);
        if (swept != PORTABLE_MESH_OK)
            return swept;
        floor_contact.found = 0;
        const PortableMeshStatus status = collide(&s, r, &shape, c, up, &e, &cannon, &wall, &floor_contact);
        if (status != PORTABLE_MESH_OK)
            return status;
        if (cannon != PORTABLE_MESH_ABSENT)
            break;
    }
    if (floor_contact.found && s.grounded)
        align_ground(&s, floor_contact.n, r);
    if (cannon != PORTABLE_MESH_ABSENT)
    {
        const PortableMeshStatus fired = cannon_fire(&s, &shape, c, cannon);
        if (fired != PORTABLE_MESH_OK)
            return fired;
        e.cannon = 1;
    }
    else if (wall.hit)
        wall_response(&s, &wall, ground.slide, s.grounded || was_grounded);
    /* A kart wedged in place (walls cancel its motion, a bounce keeps it
     * rising) probes as if grounded, so a floor under it can take it back. */
    const int wedged = !memcmp(start, s.position_q16, sizeof start);
    /* Leaving the floor is judged against the floor's own normal (pull), not
     * gravity's up, and a crease under ~5 degrees keeps the kart on the road:
     * a faceted climbing curve is ridden, not hopped face to face. */
    const int32_t departing = up_part(s.velocity_q16, pull);
    const int32_t crease = was_grounded ? (s.speed_q16 < 0 ? -s.speed_q16 : s.speed_q16) / 12 : 0;
    if (!s.grounded && !e.hopped && !e.cannon && (departing <= crease || wedged))
    {
            const PortableMeshStatus status = probe_support(
            &s, r, &shape, c, up, was_grounded || wedged ? r->ground_probe_q16 : r->ground_snap_q16);
        if (status != PORTABLE_MESH_OK)
            return status;
    }
    if (s.grounded && s.contact_surface != prior_surface)
    {
        PortableMeshSurface landed_surface;
        if (!read_surface(c, s.contact_surface, &landed_surface))
            return PORTABLE_MESH_INVALID;
        if (!cancel && landed_surface.type == 4u && landed_surface.boost_q16 > 0)
        {
            if (landed_surface.boost_q16 > 16777216)
                return PORTABLE_MESH_INVALID;
            for (unsigned i = 0; i < 3u; ++i)
                s.velocity_q16[i] += unit_product(landed_surface.boost_q16, s.forward_q30[i]);
        }
    }
    if (s.grounded)
    {
        /* Surface-gravity floors pull the kart toward their face; any other
         * floor restores world gravity. Airborne karts keep the last up. */
        PortableMeshTriangle floor;
        PortableMeshSurface floor_surface;
        int32_t normal[3];
        if (!c->triangle(c->context, s.contact_triangle, &floor) ||
            !read_surface(c, s.contact_surface, &floor_surface) ||
            !portable_mesh_query_normal(&floor, normal))
            return PORTABLE_MESH_INVALID;
        if (floor_surface.surface_gravity)
            memcpy(s.gravity_up_q30, normal, sizeof normal);
        else
            memset(s.gravity_up_q30, 0, sizeof s.gravity_up_q30);
        s.trick_surface = floor_surface.trick == 1u || (!floor_surface.trick && floor_surface.type == 5u);
        /* MK8 trick boost (0x167410): landing 20+ ticks after the trick. */
        if (!was_grounded && s.trick_kind)
        {
            if (s.trick_age >= MESH_KART_MK8_TRICK_LAND_TICKS)
            {
                s.trick_boost_ticks = (uint8_t)r->trick_ticks;
                e.trick_boost = 1;
            }
            s.trick_kind = 0;
        }
        /* MK8 landing (0x17da94): after 20+ air ticks grip stays raised
         * 5..30 ticks, longer for longer flights. */
        if (s.air_ticks >= 20u)
        {
            const uint32_t t = s.air_ticks;
            s.traction_hold_ticks = (uint8_t)(t < 11u ? 5u : t < 80u ? (t - 10u) * 25u / 70u + 5u : 30u);
            s.traction_q16 = s.traction_q16 + MESH_KART_MK8_SLIDE_ADD_Q16 > Q ? Q
                             : s.traction_q16 + MESH_KART_MK8_SLIDE_ADD_Q16;
        }
        s.air_ticks = 0;
        e.landed = !was_grounded;
        if (portable_mesh_kart_project_travel_speed(&s, &s.speed_q16) != PORTABLE_MESH_OK)
            return PORTABLE_MESH_INVALID;
    }
    else
    {
        if (s.air_ticks < UINT32_MAX)
            ++s.air_ticks;
        /* MK8 (0x176d44): 11 ticks into a flight the kart's up relaxes 0.04
         * a tick toward gravity's up. */
        if (s.air_ticks >= MESH_KART_MK8_AIR_UP_TICKS && s.glide != 2u)
        {
            const int32_t *g = gravity_up(&s);
            int32_t relaxed[3];
            for (unsigned i = 0; i < 3u; ++i)
                relaxed[i] = s.up_q30[i] + portable_fixed_3d_mul(g[i] - s.up_q30[i], MESH_KART_MK8_AIR_UP_Q16);
            if (nonzero(relaxed))
            {
                normalize(relaxed, s.up_q30);
                const int32_t along = up_part(s.forward_q30, s.up_q30);
                int32_t forward[3];
                for (unsigned i = 0; i < 3u; ++i)
                    forward[i] = s.forward_q30[i] - unit_product(along, s.up_q30[i]);
                if (nonzero(forward))
                    normalize(forward, s.forward_q30);
            }
        }
    }
    /* MK8 spin timer (0x17e2a0): an armed spin ends in the slot-2 boost
     * unless the kart sits fully off-road. */
    if (s.spin_ticks && !--s.spin_ticks)
    {
        if (s.spin_armed && !cancel && s.offroad_ticks < MESH_KART_MK8_OFFROAD_TICKS)
        {
            s.spin_boost_ticks = MESH_KART_MK8_SPIN_BOOST_TICKS;
            e.spin_boost = 1;
        }
        s.spin_armed = 0;
    }
    for (unsigned i = 0; i < 3u; ++i)
        if (s.boost_ticks[i])
            --s.boost_ticks[i];
    if (s.panel_ticks)
        --s.panel_ticks;
    if (s.rocket_ticks)
        --s.rocket_ticks;
    if (s.slip_ticks)
        --s.slip_ticks;
    if (s.trick_boost_ticks)
        --s.trick_boost_ticks;
    if (s.spin_boost_ticks && !e.spin_boost)
        --s.spin_boost_ticks;
    if (s.wall_cooldown)
        --s.wall_cooldown;
    if (s.glide == 2u && s.glide_ticks < UINT16_MAX)
        ++s.glide_ticks;
    if (!portable_mesh_kart_state_valid(&s))
        return PORTABLE_MESH_INVALID;
    *state = s;
    *events = e;
    return PORTABLE_MESH_OK;
}
#define HAZARD_KNOCK_Q16 393216 /* 6 m/s */
int portable_mesh_kart_hazard(PortableMeshKartState *state, const PortableMeshKartRules *rules,
                              const int32_t center_q16[3], int32_t radius_q16)
{
    if (!state || !center_q16 || !portable_mesh_kart_state_valid(state) ||
        !portable_mesh_kart_rules_valid(rules) || radius_q16 <= 0 || radius_q16 > 64 * Q)
        return -1;
    if (state->cannon_ticks)
        return 0;
    PortableMeshKartState s = *state;
    int64_t delta[3];
    uint64_t square = 0;
    for (unsigned i = 0; i < 3u; ++i)
    {
        if (center_q16[i] < -LIMIT || center_q16[i] > LIMIT)
            return -1;
        delta[i] = (int64_t)s.position_q16[i] - center_q16[i];
        square += (uint64_t)(delta[i] * delta[i]);
    }
    const int64_t reach = (int64_t)radius_q16 + rules->kart_radius_q16;
    if (square >= (uint64_t)(reach * reach))
        return 0;
    int32_t n[3];
    if (!portable_fixed_3d_normalize(delta, n))
        for (unsigned i = 0; i < 3u; ++i)
            n[i] = -s.forward_q30[i];
    int32_t shove[3] = {n[0], n[1], n[2]};
    if (s.grounded)
    {
        /* A grounded kart leaves along the floor: a contact point above it
         * (a vehicle's box) would otherwise push it down through the road. */
        const int64_t along = portable_fixed_3d_dot(n, s.up_q30) / U;
        int64_t level[3];
        for (unsigned i = 0; i < 3u; ++i)
            level[i] = n[i] - along * s.up_q30[i] / U;
        if (!portable_fixed_3d_normalize(level, shove))
            for (unsigned i = 0; i < 3u; ++i)
                shove[i] = -s.forward_q30[i];
    }
    const int32_t push = (int32_t)(reach - (int64_t)portable_fixed_3d_sqrt(square));
    for (unsigned i = 0; i < 3u; ++i)
    {
        s.position_q16[i] += unit_product(push, shove[i]);
        s.velocity_q16[i] /= 2;
    }
    /* MK8's object impulse (0x185fc8): the kart stops driving into the
     * sphere and the knock carries it out. */
    const int32_t inward = (int32_t)(portable_fixed_3d_dot(s.velocity_q16, shove) / U);
    int32_t knock[3];
    for (unsigned i = 0; i < 3u; ++i)
    {
        if (inward < 0)
            s.velocity_q16[i] -= unit_product(inward, shove[i]);
        knock[i] = unit_product(HAZARD_KNOCK_Q16, shove[i]);
    }
    knock_combine(s.knock_q16, knock);
    knock_hold(&s, s.traction_q16, MESH_KART_MK8_HAZARD_TICKS);
    s.drift_charge = 0;
    s.drift_direction = 0;
    s.drift_tier = 0;
    s.outside_drift_q16 = 0;
    if ((s.grounded && portable_mesh_kart_project_travel_speed(&s, &s.speed_q16) != PORTABLE_MESH_OK) ||
        !portable_mesh_kart_state_valid(&s))
        return -1;
    *state = s;
    return 1;
}
#define TUMBLE_POP_Q16 262144 /* 4 m/s */
int portable_mesh_kart_hazard_reaction(PortableMeshKartState *state, unsigned reaction)
{
    if (!state || reaction >= PORTABLE_MESH_HAZARD_REACTIONS || !portable_mesh_kart_state_valid(state))
        return -1;
    PortableMeshKartState s = *state;
    if (reaction == PORTABLE_MESH_HAZARD_TUMBLE)
    {
        for (unsigned i = 0; i < 3u; ++i)
            s.velocity_q16[i] += unit_product(TUMBLE_POP_Q16, s.up_q30[i]);
        s.grounded = 0;
    }
    else if (reaction == PORTABLE_MESH_HAZARD_SQUASH)
    {
        memset(s.velocity_q16, 0, sizeof s.velocity_q16);
        memset(s.knock_q16, 0, sizeof s.knock_q16);
        s.speed_q16 = 0;
    }
    if (!portable_mesh_kart_state_valid(&s))
        return -1;
    *state = s;
    return 0;
}
/* MK8 bump timer (0x17aa50): an idle timer runs 50 ticks, 40 at 0.6 of top
 * speed or more, 34 while boosting; armed only when the bump may spin. */
static void spin_start(PortableMeshKartState *s, const PortableMeshKartRules *r, const PortableMeshKartEffects *e,
                       int armed, int sign)
{
    if (s->spin_ticks)
        return;
    const int32_t ratio = (int32_t)((int64_t)(s->speed_q16 < 0 ? -s->speed_q16 : s->speed_q16) * Q /
                                    r->forward_speed_q16);
    s->spin_length = (uint8_t)(portable_mesh_kart_boosting(s) || (e && e->boost_active)
                                   ? MESH_KART_MK8_SPIN_TICKS_BOOST
                               : ratio < MESH_KART_MK8_SPIN_FAST_Q16 ? MESH_KART_MK8_SPIN_TICKS_SLOW
                                                                     : MESH_KART_MK8_SPIN_TICKS_FAST);
    s->spin_ticks = s->spin_length;
    s->spin_armed = (uint8_t)(armed && !(e && e->star) && !s->spin_boost_ticks);
    s->spin_sign = (int8_t)(s->spin_armed ? sign : 0);
}
int32_t portable_mesh_kart_spin_turn(const PortableMeshKartState *s)
{
    if (!s || !s->spin_armed || !s->spin_length || s->spin_ticks > s->spin_length)
        return 0;
    /* Smoothstep over the timer; MK8's curve object is runtime-built. */
    const int32_t x = (int32_t)((int64_t)(s->spin_length - s->spin_ticks) * Q / s->spin_length);
    const int32_t eased = portable_fixed_3d_mul(portable_fixed_3d_mul(x, x), 3 * Q - 2 * x);
    return s->spin_sign < 0 ? -eased : eased;
}
int portable_mesh_gravity_segment(const PortableMeshGravityPoint *a, const PortableMeshGravityPoint *b,
                                  const int32_t p[3], uint64_t *distance_squared, int32_t up_q30[3])
{
    if (!a || !b || !p || !distance_squared || !up_q30)
        return 0;
    int64_t d[3], w[3];
    uint64_t length = 0;
    for (unsigned i = 0; i < 3u; ++i)
    {
        d[i] = (int64_t)b->position_q16[i] - a->position_q16[i];
        w[i] = (int64_t)p[i] - a->position_q16[i];
        length += (uint64_t)(d[i] * d[i]);
    }
    if (!length)
        return 0;
    const int64_t along = d[0] * w[0] + d[1] * w[1] + d[2] * w[2];
    if (along < 0 || (uint64_t)along > length)
        return 0;
    const int32_t t = (int32_t)((uint64_t)along * (uint64_t)Q / length);
    uint64_t square = 0;
    for (unsigned i = 0; i < 3u; ++i)
    {
        const int64_t off = w[i] - d[i] * t / Q;
        square += (uint64_t)(off * off);
    }
    const int64_t radius = a->radius_q16 + ((int64_t)b->radius_q16 - a->radius_q16) * t / Q;
    if (square > (uint64_t)(radius * radius))
        return 0;
    int64_t up[3];
    for (unsigned i = 0; i < 3u; ++i)
        up[i] = a->up_q30[i] + ((int64_t)b->up_q30[i] - a->up_q30[i]) * t / Q;
    if (!portable_fixed_3d_normalize(up, up_q30))
        return 0;
    *distance_squared = square;
    return 1;
}
int portable_mesh_kart_spin_bumper(PortableMeshKartState *state, const PortableMeshKartRules *rules,
                                   const int32_t center_q16[3], int32_t radius_q16)
{
    if (!state || !center_q16 || !portable_mesh_kart_state_valid(state) ||
        !portable_mesh_kart_rules_valid(rules) || radius_q16 <= 0 || radius_q16 > 64 * Q)
        return -1;
    if (state->cannon_ticks)
        return 0;
    PortableMeshKartState s = *state;
    int64_t delta[3];
    uint64_t square = 0;
    for (unsigned i = 0; i < 3u; ++i)
    {
        if (center_q16[i] < -LIMIT || center_q16[i] > LIMIT)
            return -1;
        delta[i] = (int64_t)s.position_q16[i] - center_q16[i];
        square += (uint64_t)(delta[i] * delta[i]);
    }
    const int64_t reach = (int64_t)radius_q16 + rules->kart_radius_q16;
    if (square >= (uint64_t)(reach * reach))
        return 0;
    int32_t n[3];
    if (!portable_fixed_3d_normalize(delta, n))
        for (unsigned i = 0; i < 3u; ++i)
            n[i] = -s.forward_q30[i];
    const int32_t push = (int32_t)(reach - (int64_t)portable_fixed_3d_sqrt(square));
    for (unsigned i = 0; i < 3u; ++i)
        s.position_q16[i] += unit_product(push, n[i]);
    const int32_t closing = (int32_t)(portable_fixed_3d_dot(s.velocity_q16, n) / U);
    if (closing < 0 && !s.spin_ticks)
    {
        /* 0x17aa50: clamp(|v.n|, 2, 6) units/frame times 0.3, outward. */
        int32_t speed = units_per_frame(-closing);
        speed = speed < 2 * Q ? 2 * Q : speed > 6 * Q ? 6 * Q : speed;
        const int32_t impulse = portable_fixed_3d_mul(portable_fixed_3d_mul(speed, MESH_KART_MK8_BUMPER_SCALE_Q16),
                                                      MESH_KART_MK8_UNIT_SPEED_Q16);
        int32_t knock[3], side[3];
        for (unsigned i = 0; i < 3u; ++i)
        {
            s.velocity_q16[i] -= unit_product(closing, n[i]);
            knock[i] = unit_product(impulse, n[i]);
        }
        knock_combine(s.knock_q16, knock);
        knock_hold(&s, s.traction_q16, MESH_KART_MK8_BUMPER_TICKS);
        side_axis(&s, side);
        spin_start(&s, rules, NULL, s.anti_gravity, portable_fixed_3d_dot(n, side) <= 0 ? 1 : -1);
    }
    if ((s.grounded && portable_mesh_kart_project_travel_speed(&s, &s.speed_q16) != PORTABLE_MESH_OK) ||
        !portable_mesh_kart_state_valid(&s))
        return -1;
    *state = s;
    return 1;
}
/* MK8 bump weight (0x17a248): the stopped-to-top-speed lerp, or the boost
 * column while any boost runs. */
static int32_t bump_weight(const PortableMeshKartState *s, const PortableMeshKartRules *r,
                           const PortableMeshKartEffects *e, int32_t ratio)
{
    if (portable_mesh_kart_boosting(s) || (e && e->boost_active))
        return r->weight_q16[2];
    return r->weight_q16[0] + portable_fixed_3d_mul(ratio < 0 ? 0 : ratio > Q ? Q : ratio,
                                                    r->weight_q16[1] - r->weight_q16[0]);
}
static int32_t speed_ratio(const PortableMeshKartState *s, const PortableMeshKartRules *r)
{
    const int64_t ratio = (int64_t)s->speed_q16 * Q / r->forward_speed_q16;
    return ratio > 4 * Q ? 4 * Q : ratio < -4 * Q ? -4 * Q : (int32_t)ratio;
}
/* MK8 bump core for kart s struck by o, dn pointing from o to s: the side
 * axis (whichever kart's sideways axis lies closer to dn) carries a push of
 * (0.7+0.3t)*2.8*g units/frame, g o's share of the summed weights from 1/21
 * to 1, softened when both crawl; it lands three ticks later. */
static void bump(PortableMeshKartState *s, const PortableMeshKartRules *sr, const PortableMeshKartEffects *se,
                 const PortableMeshKartState *o, const PortableMeshKartRules *orr, const PortableMeshKartEffects *oe,
                 const int32_t dn[3], int spin_sign)
{
    int32_t own[3], other[3], side[3];
    side_axis(s, own);
    side_axis(o, other);
    const int64_t a_own = portable_fixed_3d_dot(dn, own) / U, a_other = portable_fixed_3d_dot(dn, other) / U;
    const int use_own = (a_own < 0 ? -a_own : a_own) >= (a_other < 0 ? -a_other : a_other);
    const int64_t along = use_own ? a_own : a_other;
    for (unsigned i = 0; i < 3u; ++i)
        side[i] = along < 0 ? -(use_own ? own : other)[i] : (use_own ? own : other)[i];
    const int32_t a = (int32_t)((along < 0 ? -along : along) / 16384);
    int32_t t = a > 45875 ? Q - (int32_t)((int64_t)(a - 45875) * 10 / 3) : (int32_t)((int64_t)a * 10 / 7);
    t = t < 0 ? 0 : t > Q ? Q : t;
    const int32_t rs = speed_ratio(s, sr), ro = speed_ratio(o, orr);
    const int32_t ws = bump_weight(s, sr, se, rs), wo = bump_weight(o, orr, oe, ro);
    const int32_t share = (int32_t)((int64_t)wo * Q / ((int64_t)ws + wo));
    int32_t g = (int32_t)((int64_t)(share - Q / 21) * 21 / 19);
    g = g < 0 ? 0 : g > Q ? Q : g;
    /* MK8 anti-gravity (0x17a68c): a kart above 0.2 of top speed pushes at
     * 2.5 instead of 2.8 and spins. */
    const int spin = s->anti_gravity && (rs < 0 ? -rs : rs) > MESH_KART_MK8_SPIN_SPEED_Q16;
    int32_t push = portable_fixed_3d_mul(portable_fixed_3d_mul(7 * Q / 10 + portable_fixed_3d_mul(3 * Q / 10, t),
                                                               spin ? MESH_KART_MK8_BUMP_PUSH_ANTI_Q16
                                                                    : MESH_KART_MK8_BUMP_PUSH_Q16), g);
    if (rs < Q / 2 && ro < Q / 2)
    {
        const int32_t m = rs > ro ? rs : ro;
        push = portable_fixed_3d_mul(push, m <= 0 ? Q / 10 : Q / 10 + portable_fixed_3d_mul(9 * Q / 5, m));
    }
    push = push > MESH_KART_MK8_BUMP_PUSH_LIMIT_Q16 ? MESH_KART_MK8_BUMP_PUSH_LIMIT_Q16 : push;
    if (!s->bump_delay)
        s->bump_delay = MESH_KART_MK8_BUMP_DELAY;
    for (unsigned i = 0; i < 3u; ++i)
        s->bump_push_q16[i] = portable_fixed_3d_mul(unit_product(Q / 10, dn[i]) + unit_product(push, side[i]),
                                                    MESH_KART_MK8_UNIT_SPEED_Q16);
    spin_start(s, sr, se, spin, spin_sign);
}
static PortableMeshStatus contact_shapes(PortableMeshKartState *a, const PortableMeshKartRules *ar,
                                         const MeshKartShape *as, const PortableMeshKartEffects *ae,
                                         PortableMeshKartState *b, const PortableMeshKartRules *br,
                                         const MeshKartShape *bs, const PortableMeshKartEffects *be)
{
    if (!a || !b || a == b || !portable_mesh_kart_state_valid(a) ||
        !portable_mesh_kart_state_valid(b) || !portable_mesh_kart_rules_valid(ar) ||
        !portable_mesh_kart_rules_valid(br))
        return PORTABLE_MESH_INVALID;
    if (a->cannon_ticks || b->cannon_ticks)
        return PORTABLE_MESH_OK; /* cannon flight passes through other karts */
    int64_t delta[3];
    uint64_t square = 0;
    for (unsigned i = 0; i < 3u; ++i)
    {
        delta[i] = (int64_t)b->position_q16[i] - a->position_q16[i];
        square += (uint64_t)(delta[i] * delta[i]);
    }
    const int64_t radius = (int64_t)as->radius_q16 + bs->radius_q16;
    if (square >= (uint64_t)(radius * radius))
        return PORTABLE_MESH_OK;
    int32_t n[3];
    if (!portable_fixed_3d_normalize(delta, n))
    {
        n[0] = U;
        n[1] = n[2] = 0;
    }
    const int32_t depth = (int32_t)radius - (int32_t)portable_fixed_3d_sqrt(square);
    /* Grounded karts shove each other along the road, never into or off it. */
    int32_t shove[3] = {n[0], n[1], n[2]}, back[3];
    if (a->grounded && b->grounded)
    {
        const int64_t along = portable_fixed_3d_dot(n, a->up_q30) / U;
        int64_t level[3];
        for (unsigned i = 0; i < 3u; ++i)
            level[i] = n[i] - along * a->up_q30[i] / U;
        if (!portable_fixed_3d_normalize(level, shove))
            memcpy(shove, n, sizeof shove);
    }
    for (unsigned i = 0; i < 3u; ++i)
        back[i] = -shove[i];
    /* MK8 pairing (0x167f34): equal karts split the overlap; a Star kart
     * holds still and the other takes it all, unbumped (items own the hit). */
    const int strong_a = ae && ae->star, strong_b = be && be->star;
    const int fa = strong_b ? (strong_a ? 1 : 2) : !strong_a, fb = strong_a ? (strong_b ? 1 : 2) : !strong_b;
    for (unsigned i = 0; i < 3u; ++i)
    {
        a->position_q16[i] -= unit_product(depth * fa / 2, n[i]);
        b->position_q16[i] += unit_product(depth * fb / 2, n[i]);
    }
    /* MK8 (0x167f34): both karts spin the same way, the sign of b->a along
     * whichever kart's side axis it lies closer to. */
    int32_t side_a[3], side_b[3];
    side_axis(a, side_a);
    side_axis(b, side_b);
    const int64_t pa = portable_fixed_3d_dot(back, side_a), pb = portable_fixed_3d_dot(back, side_b);
    const int spin_sign = ((pa < 0 ? -pa : pa) >= (pb < 0 ? -pb : pb) ? pa : pb) >= 0 ? 1 : -1;
    if (fa == 1)
        bump(a, ar, ae, b, br, be, back, spin_sign);
    if (fb == 1)
        bump(b, br, be, a, ar, ae, shove, spin_sign);
    return PORTABLE_MESH_OK;
}
PortableMeshStatus portable_mesh_kart_tick(PortableMeshKartState *state,
    const PortableMeshKartRules *rules, const PortableMeshCourse *course,
    PortableMeshKartInput input, PortableMeshKartEvents *events)
{
    return portable_mesh_kart_tick_effects(state, rules, course, input, NULL, events);
}

PortableMeshStatus portable_mesh_karts_contact_effects(PortableMeshKartState *a,
    const PortableMeshKartRules *ar, const PortableMeshKartEffects *ae,
    PortableMeshKartState *b, const PortableMeshKartRules *br, const PortableMeshKartEffects *be)
{
    if (!a || !b || a == b || !portable_mesh_kart_effects_valid(ar, ae) ||
        !portable_mesh_kart_effects_valid(br, be))
        return PORTABLE_MESH_INVALID;
    ae = nonidentity_effects(ae);
    be = nonidentity_effects(be);
    const MeshKartShape as = kart_shape(ar, ae), bs = kart_shape(br, be);
    PortableMeshKartState next_a = *a, next_b = *b;
    const PortableMeshStatus status = contact_shapes(&next_a, ar, &as, ae, &next_b, br, &bs, be);
    if (status != PORTABLE_MESH_OK)
        return status;
    if (!portable_mesh_kart_state_valid(&next_a) || !portable_mesh_kart_state_valid(&next_b))
        return PORTABLE_MESH_INVALID;
    *a = next_a;
    *b = next_b;
    return PORTABLE_MESH_OK;
}

PortableMeshStatus portable_mesh_karts_contact(PortableMeshKartState *a,
    const PortableMeshKartRules *ar, PortableMeshKartState *b, const PortableMeshKartRules *br)
{
    return portable_mesh_karts_contact_effects(a, ar, NULL, b, br, NULL);
}

const char *portable_mesh_status_name(PortableMeshStatus s)
{
    switch (s)
    {
    case PORTABLE_MESH_OK:
        return "ok";
    case PORTABLE_MESH_INVALID:
        return "invalid state/profile/mesh";
    case PORTABLE_MESH_UNSUPPORTED:
        return "unsupported mesh feature";
    case PORTABLE_MESH_COLLISION_BUDGET:
        return "collision iteration budget";
    case PORTABLE_MESH_OUTSIDE:
        return "outside driveable mesh";
    default:
        return "unknown mesh status";
    }
}
