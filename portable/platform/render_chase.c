#include "render_chase.h"
#include "mesh_query.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHASE_ROWS 256u
#define CHASE_LINEAR_ROWS 512u
#define CHASE_SAMPLES 128u
#define CHASE_RADIUS .18
#define CHASE_SPACING .125
/* Modern chase framing: the kart sits in the lower third with the horizon
 * near the upper third. Lengths are metres above or behind the support foot. */
#define CHASE_TRAIL 3.9
#define CHASE_TRAIL_SPEED .65
#define CHASE_TRAIL_BOOST .17
#define CHASE_HEIGHT 1.7
#define CHASE_LOOK_AHEAD 1.9
#define CHASE_LOOK_HEIGHT .85
/* MK8 (0x10de14, 0x10e69c) tilts the view below the kart's forward by
 * 0.15 + 0.1 * kart size, 0.4 airborne; it rises at 0.02 a tick (air 0.008)
 * and falls at 0.1. Ours keeps the resting eye 3.9 m back and 1.7 m up. */
#define CHASE_AIR_PITCH .4
#define CHASE_DIR_RATE .06 /* Heading follow per tick (0x10fe90). */
#define CHASE_UP_RATE .1
#define CHASE_UP_STEP .035 /* Radians a tick at most (0x161ec0). */
#define CHASE_MIN_BOOM .6    /* Shortest horizontal eye-to-target boom. */
#define CHASE_DRIFT_YAW .55 /* Share of the outside-drift angle shown. */
#define CHASE_SWING_LIMIT .65
#define CHASE_FOCAL_MIN .7
/* Optional immersive framing, in metres. Motion is deliberately small:
 * acceleration pitch <= 1.5 degrees, lateral roll <= 1.75 degrees. */
#define IMMERSIVE_TRAIL 3.05
#define IMMERSIVE_HEIGHT 1.25
#define IMMERSIVE_LOOK_HEIGHT .72
#define IMMERSIVE_PITCH ((IMMERSIVE_HEIGHT - IMMERSIVE_LOOK_HEIGHT) / IMMERSIVE_TRAIL)
#define IMMERSIVE_PITCH_LIMIT .025
#define IMMERSIVE_LEAN_LIMIT .03

typedef struct
{
    PortableMeshTriangle triangle;
    int32_t normal[3];
} ChaseTriangle;
typedef struct
{
    ChaseTriangle rows[CHASE_ROWS];
    unsigned count;
} ChaseGeometry;

static PortableRenderChaseStatus failure(PortableRenderChaseStatus status, char *error,
                                         size_t capacity, const char *reason)
{
    if (error && capacity)
        (void)snprintf(error, capacity, "chase camera: %s", reason);
    return status;
}

static int vector_valid(const double v[3])
{
    for (unsigned a = 0; a < 3; ++a)
        if (!isfinite(v[a]) || fabs(v[a]) > 4096.0)
            return 0;
    return 1;
}

static double dot(const double a[3], const double b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
static void cross(const double a[3], const double b[3], double out[3])
{
    const double c[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    memcpy(out, c, sizeof c);
}
static int unit(double v[3])
{
    const double n = sqrt(dot(v, v));
    if (!isfinite(n) || n < 1e-6)
        return 0;
    for (unsigned a = 0; a < 3; ++a)
        v[a] /= n;
    return 1;
}
/* v without its component along unit u, normalized; 0 when nearly parallel. */
static int flatten(const double v[3], const double u[3], double out[3])
{
    const double along = dot(v, u);
    double f[3];
    for (unsigned a = 0; a < 3; ++a)
        f[a] = v[a] - along * u[a];
    if (!unit(f))
        return 0;
    memcpy(out, f, sizeof f);
    return 1;
}

/* Unit from turned toward unit to by share of the angle between them; when
 * they are opposite it rolls about axis (unit, perpendicular to from). */
static void turn_toward(const double from[3], const double to[3], double share, const double axis[3],
                        double out[3])
{
    double turn[3];
    cross(from, to, turn);
    const double angle = atan2(sqrt(dot(turn, turn)), dot(from, to));
    if (angle < 1e-9)
    {
        memcpy(out, to, 3 * sizeof *out);
        return;
    }
    if (!unit(turn))
        memcpy(turn, axis, sizeof turn);
    double ortho[3];
    cross(turn, from, ortho);
    const double c = cos(angle * share), s = sin(angle * share);
    double result[3];
    for (unsigned a = 0; a < 3; ++a)
        result[a] = from[a] * c + ortho[a] * s;
    if (!unit(result))
        memcpy(result, from, sizeof result);
    memcpy(out, result, sizeof result);
}

int portable_render_chase_rig(PortableRenderChaseRig *out, const double eye[3], const double target[3],
                              double scale)
{
    if (!out || !isfinite(scale) || scale < .125 || scale > 4)
        return 0;
    if (!eye || !target)
    {
        *out = (PortableRenderChaseRig){CHASE_TRAIL * scale, CHASE_HEIGHT * scale,
                                        CHASE_LOOK_HEIGHT * scale, CHASE_LOOK_AHEAD * scale};
        return 1;
    }
    const double trail = -eye[1], ahead = target[1], drop = eye[2] - target[2];
    if (!isfinite(trail) || !isfinite(ahead) || !isfinite(drop) || trail < 1 || trail > 8 || ahead < 0 ||
        ahead > 16 || drop < 0 || target[2] < 0 || eye[2] > 16)
        return 0;
    /* The view runs from the eye through the aim; its look point is where it
     * passes over the foot. */
    const double pitch = drop / (trail + ahead);
    *out = (PortableRenderChaseRig){trail, eye[2], eye[2] - pitch * trail, ahead * sqrt(1 + pitch * pitch)};
    return 1;
}

static int rig_valid(const PortableRenderChaseRig *r)
{
    return isfinite(r->trail) && isfinite(r->height) && isfinite(r->look_height) && isfinite(r->look_ahead) &&
           r->trail >= .25 && r->trail <= 8 && r->look_height >= 0 && r->height >= r->look_height &&
           r->height - r->look_height <= r->trail && r->height <= 16 && r->look_ahead >= 0 && r->look_ahead <= 16;
}

static int state_valid(const PortableRenderChase *s)
{
    if (!s || s->valid != 1 || s->discontinuous > 1 || !vector_valid(s->eye) ||
        !vector_valid(s->target) || !vector_valid(s->anchor) || !vector_valid(s->heading) ||
        !vector_valid(s->up) || !isfinite(s->boom) || s->boom < 0 || s->boom > 1 ||
        !isfinite(s->swing) || fabs(s->swing) > CHASE_SWING_LIMIT + 1e-6 ||
        !isfinite(s->focal_scale) || s->focal_scale < CHASE_FOCAL_MIN - 1e-6 ||
        s->focal_scale > 1.000001 ||
        !isfinite(s->distance) || s->distance < .25 || s->distance > 8 || !isfinite(s->pitch) ||
        s->pitch < -1e-6 || s->pitch > 1.000001 || !isfinite(s->shake) || s->shake < 0 || s->shake > .25 ||
        !vector_valid(s->dir) || !vector_valid(s->offset) || !isfinite(s->recover) ||
        s->recover < 0 || s->recover > 1.000001 ||
        !isfinite(s->speed) || s->speed < 0 || s->speed > 2.000001 || s->boosting > 1 ||
        s->spinning > 1 || s->immersive > 1 || !isfinite(s->motion_pitch) ||
        fabs(s->motion_pitch) > IMMERSIVE_PITCH_LIMIT + 1e-6 || !isfinite(s->lean) ||
        fabs(s->lean) > IMMERSIVE_LEAN_LIMIT + 1e-6 || !vector_valid(s->jolt))
        return 0;
    return fabs(dot(s->heading, s->heading) - 1) < 1e-6 && fabs(dot(s->up, s->up) - 1) < 1e-6 &&
           fabs(dot(s->heading, s->up)) < 1e-6 && fabs(dot(s->dir, s->dir) - 1) < 1e-6;
}

static int fixed_position(const double p[3], int32_t q[3])
{
    if (!vector_valid(p))
        return 0;
    for (unsigned a = 0; a < 3; ++a)
        q[a] = (int32_t)llround(p[a] * 65536.0);
    return 1;
}

/* Cache only nearby indexed rows once, rather than invoke content callbacks at
 * every boom sample. Linear geometry is deliberately restricted to small tests
 * and authored courses; large shipped courses provide an immutable index. */
static PortableRenderChaseStatus geometry_open(ChaseGeometry *out, const PortableMeshCourse *course,
                                               const double *const points[], unsigned count)
{
    if (!course || !course->triangle || !course->triangle_count)
        return PORTABLE_RENDER_CHASE_INVALID;
    if (!course->next_triangle && course->triangle_count > CHASE_LINEAR_ROWS)
        return PORTABLE_RENDER_CHASE_BUDGET;
    int32_t low[3], high[3];
    for (unsigned a = 0; a < 3; ++a)
    {
        double lo = points[0][a], hi = lo;
        for (unsigned i = 1; i < count; ++i)
        {
            lo = fmin(lo, points[i][a]);
            hi = fmax(hi, points[i][a]);
        }
        lo -= .25;
        hi += .25;
        low[a] = (int32_t)floor(fmax(-4096, lo) * 65536);
        high[a] = (int32_t)ceil(fmin(4096, hi) * 65536);
    }
    out->count = 0;
    uint32_t first = 0;
    for (;;)
    {
        uint32_t id;
        const PortableMeshQueryStatus status =
            portable_mesh_query_next_triangle(course, low, high, first, &id);
        if (status == PORTABLE_MESH_QUERY_MISS)
            return PORTABLE_RENDER_CHASE_OK;
        if (status != PORTABLE_MESH_QUERY_HIT)
            return PORTABLE_RENDER_CHASE_INVALID;
        PortableMeshTriangle t;
        int32_t normal[3];
        if (!course->triangle(course->context, id, &t) || !portable_mesh_query_normal(&t, normal))
            return PORTABLE_RENDER_CHASE_INVALID;
        int intersects = 1;
        for (unsigned a = 0; a < 3; ++a)
        {
            int32_t lo = t.vertices_q16[0][a], hi = lo;
            for (unsigned v = 0; v < 3; ++v)
            {
                const int32_t q = t.vertices_q16[v][a];
                if (q < -4096 * 65536 || q > 4096 * 65536)
                    return PORTABLE_RENDER_CHASE_INVALID;
                if (q < lo)
                    lo = q;
                if (q > hi)
                    hi = q;
            }
            if (hi < low[a] || lo > high[a])
                intersects = 0;
        }
        if (intersects)
        {
            if (out->count == CHASE_ROWS)
                return PORTABLE_RENDER_CHASE_BUDGET;
            out->rows[out->count].triangle = t;
            memcpy(out->rows[out->count].normal, normal, sizeof normal);
            ++out->count;
        }
        first = id + 1;
    }
}

static int clear_point(const ChaseGeometry *g, const double p[3], double radius)
{
    int32_t q[3];
    if (!fixed_position(p, q))
        return 0;
    for (unsigned i = 0; i < g->count; ++i)
    {
        int32_t closest[3];
        portable_mesh_query_closest(&g->rows[i].triangle, q, g->rows[i].normal, closest);
        double d2 = 0;
        for (unsigned a = 0; a < 3; ++a)
        {
            const double d = ((double)q[a] - closest[a]) / 65536.0;
            d2 += d * d;
        }
        if (d2 <= radius * radius)
            return 0;
    }
    return 1;
}

/* Sample inflation covers the continuous sphere between samples by the
 * distance function's Lipschitz bound, plus Q16 point/closest rounding skin.
 * The look target may begin inside scenery; emerge once, then never cross a
 * later obstruction. This does not assert that the kart's target is empty. */
static PortableRenderChaseStatus clip_segment(const ChaseGeometry *g, const double from[3],
                                              const double to[3], double out[3])
{
    double d2 = 0;
    for (unsigned a = 0; a < 3; ++a)
        d2 += (to[a] - from[a]) * (to[a] - from[a]);
    const double distance = sqrt(d2);
    if (!isfinite(distance) || distance > CHASE_SAMPLES * CHASE_SPACING)
        return PORTABLE_RENDER_CHASE_BUDGET;
    const unsigned steps = distance > 0 ? (unsigned)ceil(distance / CHASE_SPACING) : 1;
    const double radius = CHASE_RADIUS + distance / (2 * steps) + .0001;
    int found = 0;
    for (unsigned i = 0; i <= steps; ++i)
    {
        double p[3];
        for (unsigned a = 0; a < 3; ++a)
            p[a] = from[a] + (to[a] - from[a]) * ((double)i / steps);
        if (!clear_point(g, p, radius))
        {
            if (found)
                break;
            continue;
        }
        memcpy(out, p, sizeof p);
        found = 1;
    }
    return found ? PORTABLE_RENDER_CHASE_OK : PORTABLE_RENDER_CHASE_OBSTRUCTED;
}

PortableRenderChaseStatus portable_render_chase_step(PortableRenderChase *out,
                                                     const PortableRenderChase *before,
                                                     const PortableRenderChaseInput *input,
                                                     const PortableMeshCourse *course, char *error,
                                                     size_t capacity)
{
    if (!out || !before || !input || before->valid > 1 || input->grounded > 1 ||
        input->boosting > 1 || input->cut > 1 || input->dynamic > 1 || input->shake > 1 ||
        input->spinning > 1 || input->immersive > 1 || input->speed_ratio_q16 < 0 || input->speed_ratio_q16 > 2 * 65536 ||
        !vector_valid(input->foot.position) ||
        !vector_valid(input->foot.forward) || !vector_valid(input->foot.up) ||
        !vector_valid(input->gravity_up) ||
        input->outside_drift_q16 < -16384 || input->outside_drift_q16 > 16384 ||
        (input->rig.trail != 0 && !rig_valid(&input->rig)) ||
        (before->valid && !state_valid(before)))
        return failure(PORTABLE_RENDER_CHASE_INVALID, error, capacity, "invalid owner or input");
    if (before->valid && !input->cut && input->tick == before->tick)
    {
        *out = *before;
        return PORTABLE_RENDER_CHASE_OK;
    }
    if (before->valid && !input->cut && input->tick != before->tick + 1u)
        return failure(PORTABLE_RENDER_CHASE_INVALID, error, capacity,
                       "nonconsecutive accepted tick");
    const int reset = !before->valid || input->cut || before->immersive != input->immersive;
    PortableRenderChaseRig rig = input->rig;
    if (rig.trail == 0)
        (void)portable_render_chase_rig(&rig, NULL, NULL, 1);
    /* Speed pull-back and the immersive framing keep their share of the rig. */
    const double size = rig.trail / CHASE_TRAIL;
    PortableRenderChase s = reset ? (PortableRenderChase){0} : *before;
    s.immersive = input->immersive;
    /* MK8 (0x10f4f4): the view's up turns toward gravity's by a tenth of the
     * angle a tick, at most 0.035 rad: banked roads keep a level horizon and
     * kart roll never reaches the camera, while loops, wall rides and
     * anti-gravity ceilings carry the view round. */
    double up[3] = {input->gravity_up[0], input->gravity_up[1], input->gravity_up[2]};
    if (!unit(up))
    {
        up[0] = up[1] = 0;
        up[2] = 1;
    }
    if (!reset)
    {
        double turn[3];
        cross(s.up, up, turn);
        const double angle = atan2(sqrt(dot(turn, turn)), dot(s.up, up));
        const double share = angle > CHASE_UP_STEP / CHASE_UP_RATE ? CHASE_UP_STEP / angle : CHASE_UP_RATE;
        turn_toward(s.up, up, share, s.heading, up);
    }
    memcpy(s.up, up, sizeof up);
    const double speed = fmin(2, input->speed_ratio_q16 / 65536.0);
    /* MK8 (0x10e69c): the look direction eases toward the kart's forward,
     * slope pitch included, so bumps and creases turn the view gently. */
    double forward[3] = {input->foot.forward[0], input->foot.forward[1], input->foot.forward[2]};
    if (!unit(forward))
        return failure(PORTABLE_RENDER_CHASE_INVALID, error, capacity, "invalid kart forward");
    if (reset)
        memcpy(s.dir, forward, sizeof forward);
    else
    {
        for (unsigned a = 0; a < 3; ++a)
            s.dir[a] += CHASE_DIR_RATE * (forward[a] - s.dir[a]);
        if (!unit(s.dir))
            memcpy(s.dir, forward, sizeof forward);
    }
    double heading[3];
    if (!flatten(s.dir, s.up, heading))
    {
        if (reset || !flatten(before->heading, s.up, heading))
            return failure(PORTABLE_RENDER_CHASE_INVALID, error, capacity, "vertical initial heading");
    }
    memcpy(s.heading, heading, sizeof heading);
    /* No positional lag: the camera rides the kart's arc rigidly. */
    memcpy(s.anchor, input->foot.position, sizeof s.anchor);
    /* Travel = forward rotated by -outside_drift about up; show part of it. */
    const double swing = input->dynamic ? fmax(-CHASE_SWING_LIMIT, fmin(CHASE_SWING_LIMIT,
        -input->outside_drift_q16 * (6.283185307179586 / 65536.0) * CHASE_DRIFT_YAW)) : 0;
    s.swing = reset ? swing : s.swing + .1 * (swing - s.swing);
    const double boost = input->dynamic && input->boosting ? 1 : 0;
    const double pace = input->dynamic ? fmin(speed, 1.25) : 0;
    const double focal = input->immersive ? fmax(CHASE_FOCAL_MIN, 1 / (1 + .22 * pace + .15 * boost))
                                          : 1 / (1 + .12 * pace + .14 * boost);
    s.focal_scale = reset ? focal : s.focal_scale + .08 * (focal - s.focal_scale);
    const double trail = input->immersive ? size * (IMMERSIVE_TRAIL + .25 * pace + .5 * boost)
        : rig.trail + size * (CHASE_TRAIL_SPEED * pace + CHASE_TRAIL_BOOST * boost);
    s.distance = reset ? trail : s.distance + .06 * (trail - s.distance);
    /* MK8 pitch: a short hop barely tilts the view; landing restores it. */
    const double rest_pitch = input->immersive ? IMMERSIVE_PITCH : (rig.height - rig.look_height) / rig.trail;
    const double pitch_target = input->grounded ? rest_pitch : input->immersive ? .24 : CHASE_AIR_PITCH;
    const double pitch_rate = !input->grounded ? .008 : pitch_target > s.pitch ? .02 : .1;
    s.pitch = reset ? rest_pitch : s.pitch + (pitch_target - s.pitch) * pitch_rate;
    if (input->immersive && !reset)
    {
        /* Speed changes are observed, never fed back into driving. Acceleration
         * lifts the gaze, braking lowers it; turn rate banks subtly into corners.
         * Derive turn from the lagged heading, not terrain bank or steering input. */
        const double acceleration = input->dynamic ? speed - s.speed : 0;
        const double tilt = fmax(-IMMERSIVE_PITCH_LIMIT,
            fmin(IMMERSIVE_PITCH_LIMIT, -acceleration * .7));
        double turn[3];
        cross(before->heading, heading, turn);
        const double roll = input->dynamic ? fmax(-IMMERSIVE_LEAN_LIMIT,
            fmin(IMMERSIVE_LEAN_LIMIT, dot(turn, s.up) * fmin(speed, 1) * 3)) : 0;
        s.motion_pitch += .1 * (tilt - s.motion_pitch);
        s.lean += .08 * (roll - s.lean);
    }
    /* Shake: landings scale with air time, boosts kick once, hits and wall
     * scrubs jolt. Amplitude decays every accepted tick. */
    double shake = reset ? 0 : s.shake * .82;
    if (input->shake && !reset)
    {
        if (input->grounded && s.air_ticks > 8)
            shake = fmax(shake, fmin(.09, s.air_ticks * .0025));
        if (input->boosting && !s.boosting)
            shake = fmax(shake, .045);
        if (input->spinning && !s.spinning)
            shake = fmax(shake, .12);
        if (s.speed - speed > .12)
            shake = fmax(shake, fmin(.12, (s.speed - speed) * .5));
    }
    if (input->immersive)
        shake = input->shake ? fmin(.045, shake) : 0;
    s.shake = shake < 1e-4 ? 0 : fmin(.25, shake);
    s.air_ticks = input->grounded ? 0 : (uint16_t)(s.air_ticks < 65535u ? s.air_ticks + 1u : 65535u);
    s.boosting = input->boosting;
    s.spinning = input->spinning;
    s.speed = speed < 0 ? 0 : speed;
    double jolt[3] = {0, 0, 0};
    if (s.shake > 0 && !input->immersive)
        for (unsigned a = 0; a < 3; ++a)
        {
            /* Deterministic per-tick noise in [-1,1]. */
            uint32_t h = input->tick * 0x9e3779b9u ^ (a + 1u) * 0x85ebca6bu;
            h ^= h >> 15;
            h *= 0x2c1b3c6du;
            h ^= h >> 12;
            jolt[a] = s.shake * ((h & 0xffffu) / 32767.5 - 1) * (a == 2 ? .6 : 1);
        }
    if (input->immersive)
        for (unsigned a = 0; a < 3; ++a)
        {
            /* Filter a bounded low-frequency impulse, avoiding frame-to-frame
             * white noise. The eye still goes through the ordinary sphere sweep. */
            const double wave = s.shake * sin((input->tick % 12u) * (6.283185307179586 / 12) + a * 2.1);
            s.jolt[a] += .3 * (wave - s.jolt[a]);
            if (!input->shake || (s.shake == 0 && fabs(s.jolt[a]) < 1e-5))
                s.jolt[a] = 0;
            jolt[a] = s.jolt[a];
        }
    /* View = dir swung by the drift share about up, tilted down by pitch
     * along the kart's up (MK8: normalize(dir - p * up)). */
    const double yaw_cos = cos(s.swing), yaw_sin = sin(s.swing);
    double side[3], view[3], kart_up[3] = {input->foot.up[0], input->foot.up[1], input->foot.up[2]};
    if (!unit(kart_up))
        memcpy(kart_up, s.up, sizeof kart_up);
    cross(s.up, s.dir, side);
    if (!unit(side))
        cross(s.up, s.heading, side);
    for (unsigned a = 0; a < 3; ++a)
        view[a] = s.dir[a] * yaw_cos + side[a] * yaw_sin - (s.pitch + s.motion_pitch) * kart_up[a];
    if (!unit(view))
        return failure(PORTABLE_RENDER_CHASE_INVALID, error, capacity, "degenerate view");
    /* The look point rides over the kart; the free eye sits on a sphere
     * round the kart, on the ray back from it along the view. */
    /* The eye rides a sphere round the kart's foot (MK8: 52u). */
    const double radius = s.distance * (input->immersive
        ? hypot(IMMERSIVE_TRAIL, IMMERSIVE_HEIGHT) / IMMERSIVE_TRAIL : hypot(rig.trail, rig.height) / rig.trail);
    const double look_height = input->immersive ? size * IMMERSIVE_LOOK_HEIGHT : rig.look_height;
    double at[3], desired[3], from_foot[3];
    for (unsigned a = 0; a < 3; ++a)
    {
        at[a] = s.anchor[a] + look_height * s.up[a] + jolt[a];
        from_foot[a] = at[a] - s.anchor[a];
    }
    const double along = dot(from_foot, view);
    const double reach = along + sqrt(fmax(0, along * along - dot(from_foot, from_foot) + radius * radius));
    for (unsigned a = 0; a < 3; ++a)
    {
        desired[a] = at[a] - reach * view[a] + .4 * jolt[a];
        s.target[a] = at[a] + rig.look_ahead * view[a];
    }
    ChaseGeometry geometry;
    const double *const bounds[] = {at, s.target, desired, reset ? desired : before->eye};
    PortableRenderChaseStatus status = geometry_open(&geometry, course, bounds, 4u);
    if (status != PORTABLE_RENDER_CHASE_OK)
        return failure(status, error, capacity,
                       status == PORTABLE_RENDER_CHASE_BUDGET ? "nearby triangle budget"
                                                              : "invalid geometry traversal");
    /* A look target ahead may lie inside or beyond a wall the kart faces. */
    double ahead[3];
    if (clip_segment(&geometry, at, s.target, ahead) == PORTABLE_RENDER_CHASE_OK)
        memcpy(s.target, ahead, sizeof ahead);
    else
        memcpy(s.target, at, sizeof at);
    /* MK8 collision (0x110060): scenery pushes the eye in at once; once the
     * free eye is clear again it eases back over about a second, slowly at
     * first (k = 0.01 + 0.99 * recover^2), so skimming walls never pumps. */
    double free_offset[3], clear_eye[3];
    for (unsigned a = 0; a < 3; ++a)
        free_offset[a] = desired[a] - at[a];
    status = clip_segment(&geometry, at, desired, clear_eye);
    if (status == PORTABLE_RENDER_CHASE_OK)
    {
        const int blocked = fabs(clear_eye[0] - desired[0]) + fabs(clear_eye[1] - desired[1]) +
                            fabs(clear_eye[2] - desired[2]) > 1e-9;
        double eye[3];
        if (reset)
        {
            s.recover = blocked ? 0 : 1;
            memcpy(eye, clear_eye, sizeof eye);
        }
        else if (blocked)
        {
            s.recover = fmax(0, s.recover - .02);
            /* Keep the eased eye while it stays clear and inside the clip. */
            double held[3];
            for (unsigned a = 0; a < 3; ++a)
                held[a] = at[a] + s.offset[a];
            double held_len = 0, clear_len = 0;
            for (unsigned a = 0; a < 3; ++a)
            {
                held_len += s.offset[a] * s.offset[a];
                clear_len += (clear_eye[a] - at[a]) * (clear_eye[a] - at[a]);
            }
            if (held_len <= clear_len && clear_point(&geometry, held, CHASE_RADIUS) &&
                clip_segment(&geometry, at, held, eye) == PORTABLE_RENDER_CHASE_OK &&
                fabs(eye[0] - held[0]) + fabs(eye[1] - held[1]) + fabs(eye[2] - held[2]) < 1e-9)
                memcpy(eye, held, sizeof eye);
            else
                memcpy(eye, clear_eye, sizeof eye);
        }
        else
        {
            s.recover = fmin(1, s.recover + 1 / 60.0);
            const double k = .01 + .99 * s.recover * s.recover;
            for (unsigned a = 0; a < 3; ++a)
                eye[a] = at[a] + s.offset[a] + k * (free_offset[a] - s.offset[a]);
            double checked[3];
            if (!clear_point(&geometry, eye, CHASE_RADIUS) ||
                clip_segment(&geometry, at, eye, checked) != PORTABLE_RENDER_CHASE_OK ||
                fabs(checked[0] - eye[0]) + fabs(checked[1] - eye[1]) + fabs(checked[2] - eye[2]) > 1e-9)
                memcpy(eye, clear_eye, sizeof eye);
        }
        memcpy(s.eye, eye, sizeof eye);
        double full = 0, used = 0;
        for (unsigned a = 0; a < 3; ++a)
        {
            s.offset[a] = s.eye[a] - at[a];
            full += free_offset[a] * free_offset[a];
            used += s.offset[a] * s.offset[a];
        }
        s.boom = full > 1e-12 ? fmin(1, sqrt(used / full)) : 1;
    }
    double flat_reach[3];
    for (unsigned a = 0; a < 3; ++a)
        flat_reach[a] = s.eye[a] - at[a];
    const double rise = dot(flat_reach, s.up);
    for (unsigned a = 0; a < 3; ++a)
        flat_reach[a] -= rise * s.up[a];
    if (status == PORTABLE_RENDER_CHASE_OK && sqrt(dot(flat_reach, flat_reach)) < CHASE_MIN_BOOM * size)
    {
        /* Scenery right behind the kart leaves no view direction: climb a
         * short boom over it, as MK8's camera slides up walls. */
        double raised[3], eye[3];
        for (unsigned a = 0; a < 3; ++a)
            raised[a] = at[a] - CHASE_MIN_BOOM * size * heading[a] + rig.height * s.up[a];
        if (clip_segment(&geometry, at, raised, eye) != PORTABLE_RENDER_CHASE_OK)
            memcpy(eye, raised, sizeof eye);
        /* The look point climbs with the eye, so the view keeps its pitch
         * instead of staring down at the kart and losing the horizon. */
        double lifted[3], reached[3];
        for (unsigned a = 0; a < 3; ++a)
        {
            lifted[a] = s.target[a] + eye[a] - s.eye[a];
            s.offset[a] = eye[a] - at[a];
        }
        if (clip_segment(&geometry, at, lifted, reached) == PORTABLE_RENDER_CHASE_OK)
            memcpy(s.target, reached, sizeof reached);
        memcpy(s.eye, eye, sizeof eye);
    }
    if (status == PORTABLE_RENDER_CHASE_OK && !reset)
    {
        double safe[3];
        status = clip_segment(&geometry, before->eye, s.eye, safe);
        if (status == PORTABLE_RENDER_CHASE_BUDGET)
            return failure(status, error, capacity, "camera movement sample budget");
        /* A camera-only cut is preferable to trapping lag behind a roof edge.
         * The new eye is already safe on its own boom. Never interpolate across
         * an obstructed movement segment, even when physics legitimately passes
         * below a roof or around a corner. */
        double difference = 0;
        if (status == PORTABLE_RENDER_CHASE_OK)
            for (unsigned a = 0; a < 3; ++a)
                difference += fabs(safe[a] - s.eye[a]);
        s.discontinuous = (uint8_t)(status != PORTABLE_RENDER_CHASE_OK || difference > 1e-8);
        status = PORTABLE_RENDER_CHASE_OK;
    }
    if (status != PORTABLE_RENDER_CHASE_OK)
        return failure(status, error, capacity,
                       status == PORTABLE_RENDER_CHASE_BUDGET ? "camera segment sample budget"
                                                              : "no clear camera position");
    if (reset)
        s.discontinuous = 1;
    s.tick = input->tick;
    s.valid = 1;
    if (!state_valid(&s))
        return failure(PORTABLE_RENDER_CHASE_INVALID, error, capacity, "invalid staged camera");
    *out = s;
    return PORTABLE_RENDER_CHASE_OK;
}

int portable_render_chase_snapshot(PortableRenderChaseSnapshot *out,
                                   const PortableRenderChase *previous,
                                   const PortableRenderChase *current, uint32_t alpha_q16)
{
    if (!out || !state_valid(previous) || !state_valid(current))
        return 0;
    if (current->discontinuous)
        previous = current;
    const double alpha = (alpha_q16 > 65536 ? 65536 : alpha_q16) / 65536.0;
    PortableRenderChaseSnapshot s = {0};
    double norm = 0;
    for (unsigned a = 0; a < 3; ++a)
    {
        s.position[a] = previous->eye[a] + alpha * (current->eye[a] - previous->eye[a]);
        s.target[a] = previous->target[a] + alpha * (current->target[a] - previous->target[a]);
        s.forward[a] = s.target[a] - s.position[a];
        norm += s.forward[a] * s.forward[a];
    }
    if (norm < 1e-12)
        return 0;
    norm = sqrt(norm);
    for (unsigned a = 0; a < 3; ++a)
        s.forward[a] /= norm;
    /* right = camera-up cross forward; up = forward cross right. */
    double up[3];
    for (unsigned a = 0; a < 3; ++a)
        up[a] = previous->up[a] + alpha * (current->up[a] - previous->up[a]);
    if (!unit(up))
        return 0;
    cross(up, s.forward, s.right);
    if (!unit(s.right))
        return 0;
    cross(s.forward, s.right, s.up);
    const double lean = previous->lean + alpha * (current->lean - previous->lean);
    if (lean != 0)
    {
        const double c = cos(lean), sn = sin(lean);
        for (unsigned a = 0; a < 3; ++a)
        {
            const double right = s.right[a], vertical = s.up[a];
            s.right[a] = right * c - vertical * sn;
            s.up[a] = vertical * c + right * sn;
        }
    }
    s.focal_scale = previous->focal_scale + alpha * (current->focal_scale - previous->focal_scale);
    s.swing = previous->swing + alpha * (current->swing - previous->swing);
    *out = s;
    return 1;
}
