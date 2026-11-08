#include "mesh_motion.h"
#include "fixed_3d.h"

#define Q30 INT64_C(1073741824)

static int unit_axis(const int32_t axis[3])
{
    const int64_t length = portable_fixed_3d_dot(axis, axis) >> 30;
    return length > Q30 - (Q30 >> 10) && length < Q30 + (Q30 >> 10);
}

int portable_mesh_motion_valid(const PortableMeshMotion *m)
{
    if (!m || m->phase > (1u << 24))
        return 0;
    if (m->sway_link &&
        (m->sway_link > 16u || !unit_axis(m->sway_axis_q30) || m->sway_axis_q30[2] || !m->sway_amplitude ||
         m->sway_amplitude > 16384u || m->sway_period < 2u || m->sway_period > PORTABLE_MESH_MOTION_TICK_LIMIT ||
         m->sway_lag > PORTABLE_MESH_MOTION_TICK_LIMIT || m->sway_length_q16 <= 0 || m->sway_length_q16 > (64 << 16)))
        return 0;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (m->pivot_q16[axis] < -PORTABLE_FIXED_3D_POSITION_LIMIT ||
            m->pivot_q16[axis] > PORTABLE_FIXED_3D_POSITION_LIMIT)
            return 0;
    switch (m->kind)
    {
    case PORTABLE_MESH_MOTION_SPIN:
        return unit_axis(m->axis_q30) && m->rate && m->rate >= -(1 << 26) && m->rate <= (1 << 26) &&
               m->run <= PORTABLE_MESH_MOTION_TICK_LIMIT && m->ramp <= PORTABLE_MESH_MOTION_TICK_LIMIT &&
               m->stop <= PORTABLE_MESH_MOTION_TICK_LIMIT && (m->run || (!m->ramp && !m->stop));
    case PORTABLE_MESH_MOTION_SWING:
        return unit_axis(m->axis_q30) && m->amplitude && m->amplitude <= 16384u && m->period >= 2u &&
               m->period <= PORTABLE_MESH_MOTION_TICK_LIMIT;
    case PORTABLE_MESH_MOTION_ROUTE:
        if (m->point_count < 2u || m->point_count > PORTABLE_MESH_MOTION_POINT_LIMIT ||
            m->wait > PORTABLE_MESH_MOTION_TICK_LIMIT || m->face > 1u)
            return 0;
        for (uint32_t i = 0; i < m->point_count; ++i)
        {
            if (!m->travel[i] || m->travel[i] > PORTABLE_MESH_MOTION_TICK_LIMIT)
                return 0;
            for (unsigned axis = 0; axis < 3; ++axis)
                if (m->points_q16[i][axis] < -PORTABLE_FIXED_3D_POSITION_LIMIT ||
                    m->points_q16[i][axis] > PORTABLE_FIXED_3D_POSITION_LIMIT)
                    return 0;
        }
        return 1;
    default:
        return 0;
    }
}

/* Turn accumulated over the first n ticks of a ramp: tick k adds rate*k/ramp. */
static int64_t ramp_sum(int64_t rate, int64_t n, int64_t ramp)
{
    return ramp ? rate * n * (n + 1) / (2 * ramp) : 0;
}

/* Spin angle in turns<<32 after `tick` ticks. */
static uint32_t spin_angle(const PortableMeshMotion *m, uint32_t tick)
{
    const int64_t rate = m->rate;
    if (!m->run)
        return (uint32_t)((uint64_t)rate * tick);
    const uint32_t half = 2u * m->ramp + m->run + m->stop;
    const uint32_t cycle = tick / half, t = tick % half;
    const int64_t up = ramp_sum(rate, m->ramp, m->ramp), sweep = rate * (int64_t)(m->run + m->ramp);
    int64_t partial;
    if (t <= m->ramp)
        partial = ramp_sum(rate, t, m->ramp);
    else if (t <= m->ramp + m->run)
        partial = up + rate * (int64_t)(t - m->ramp);
    else if (t <= 2u * m->ramp + m->run)
    {
        const int64_t n = t - m->ramp - m->run;
        partial = up + rate * (int64_t)m->run + rate * n - ramp_sum(rate, n, m->ramp);
    }
    else
        partial = sweep;
    return (uint32_t)(uint64_t)((cycle & 1u) ? sweep - partial : partial);
}

/* Horizontal unit heading of route leg i; 0 when it has no horizontal run. */
static int heading(const PortableMeshMotion *m, uint32_t i, int32_t out[3])
{
    const int32_t *a = m->points_q16[i], *b = m->points_q16[(i + 1) % m->point_count];
    const int64_t d[3] = {(int64_t)b[0] - a[0], (int64_t)b[1] - a[1], 0};
    return (d[0] || d[1]) && portable_fixed_3d_normalize(d, out);
}

/* Rodrigues: R = c I + s [a]x + (1 - c) a a^T, Q30. */
static void axis_rotation(const int32_t a[3], uint16_t angle, int32_t r[9])
{
    int32_t s, c;
    portable_fixed_3d_sincos(angle, &s, &c);
    const int64_t k = Q30 - c;
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j)
            r[i * 3 + j] = (int32_t)((i == j ? c : 0) + ((((int64_t)a[i] * a[j]) >> 30) * k >> 30));
    const int64_t sx = (int64_t)s * a[0] >> 30, sy = (int64_t)s * a[1] >> 30, sz = (int64_t)s * a[2] >> 30;
    r[1] -= (int32_t)sz;
    r[2] += (int32_t)sy;
    r[3] += (int32_t)sz;
    r[5] -= (int32_t)sx;
    r[6] -= (int32_t)sy;
    r[7] += (int32_t)sx;
}

int portable_mesh_motion_route_within(const PortableMeshMotion *m, uint32_t tick, uint32_t from, uint32_t to)
{
    uint32_t cycle = 0;
    for (uint32_t i = 0; i < m->point_count; ++i)
        cycle += m->travel[i] + m->wait;
    const uint32_t t = (uint32_t)(((uint64_t)tick + m->phase) % cycle);
    return from <= to ? t >= from && t < to : t >= from || t < to;
}

static void rotate(const int32_t r[9], const int64_t v[3], int transpose, int64_t out[3]);

/* Rest-frame displacement of sway link m->sway_link at `tick` (Q16). */
static void sway(const PortableMeshMotion *m, uint32_t tick, int64_t out[3])
{
    uint32_t bend = 0; /* A_m, turns<<16 */
    int64_t side = 0, rise = 0;
    for (uint32_t j = 0; j <= m->sway_link; ++j)
    {
        const uint32_t start = j * m->sway_lag;
        if (tick >= start)
        {
            int32_t s, c;
            const uint32_t t = (tick - start) % m->sway_period;
            portable_fixed_3d_sincos((uint16_t)(((uint64_t)t << 16) / m->sway_period), &s, &c);
            bend += (uint32_t)(int32_t)(((int64_t)m->sway_amplitude * s) >> 30);
        }
        if (j)
        {
            int32_t s, c;
            portable_fixed_3d_sincos((uint16_t)bend, &s, &c);
            side += (int64_t)m->sway_length_q16 * s >> 30;
            rise += (int64_t)m->sway_length_q16 * ((int64_t)c - Q30) >> 30;
        }
    }
    for (unsigned axis = 0; axis < 3; ++axis)
        out[axis] = side * m->sway_axis_q30[axis] >> 30;
    out[2] += rise;
}

void portable_mesh_motion_pose(const PortableMeshMotion *m, uint32_t tick, PortableMeshPose *out)
{
    tick += m->phase;
    for (unsigned i = 0; i < 9; ++i)
        out->rotation_q30[i] = i % 4 ? 0 : (int32_t)Q30;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        out->pivot_q16[axis] = m->pivot_q16[axis];
        out->offset_q16[axis] = 0;
    }
    if (m->kind == PORTABLE_MESH_MOTION_SPIN)
        axis_rotation(m->axis_q30, (uint16_t)(spin_angle(m, tick) >> 16), out->rotation_q30);
    else if (m->kind == PORTABLE_MESH_MOTION_SWING)
    {
        int32_t s, c;
        portable_fixed_3d_sincos((uint16_t)(((uint64_t)(tick % m->period) << 16) / m->period), &s, &c);
        axis_rotation(m->axis_q30, (uint16_t)(int32_t)(((int64_t)m->amplitude * s) >> 30), out->rotation_q30);
    }
    else
    {
        uint32_t cycle = 0;
        for (uint32_t i = 0; i < m->point_count; ++i)
            cycle += m->travel[i] + m->wait;
        uint32_t t = tick % cycle, i = 0;
        while (t >= m->travel[i] + m->wait)
            t -= m->travel[i++] + m->wait;
        const int32_t *a = m->points_q16[i], *b = m->points_q16[(i + 1) % m->point_count];
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            const int64_t along = t < m->travel[i] ? ((int64_t)b[axis] - a[axis]) * t / m->travel[i]
                                                   : (int64_t)b[axis] - a[axis];
            out->offset_q16[axis] = (int32_t)((int64_t)a[axis] + along - m->points_q16[0][axis]);
        }
        int32_t from[3], to[3];
        if (m->face && heading(m, 0, from) && heading(m, i, to))
        {
            /* Turn smoothly through each node: the heading runs from the
             * bisector of the corner behind to the bisector of the one ahead
             * (as MK64 eases vehicle yaw) instead of snapping at the node. */
            int32_t side[3];
            const uint32_t u = t < m->travel[i] ? (uint32_t)(((uint64_t)t << 16) / m->travel[i]) : 65536u;
            const uint32_t next = u < 32768u ? (i + m->point_count - 1u) % m->point_count : (i + 1u) % m->point_count;
            if (heading(m, next, side))
            {
                const int64_t w = u < 32768u ? 32768 - (int64_t)u : (int64_t)u - 32768; /* side weight, Q16 */
                const int64_t blend[3] = {((int64_t)to[0] * (65536 - w) + (int64_t)side[0] * w) >> 16,
                                          ((int64_t)to[1] * (65536 - w) + (int64_t)side[1] * w) >> 16, 0};
                if (!portable_fixed_3d_normalize(blend, to))
                    heading(m, i, to);
            }
            const int32_t c = (int32_t)(((int64_t)from[0] * to[0] + (int64_t)from[1] * to[1]) >> 30);
            const int32_t s = (int32_t)(((int64_t)from[0] * to[1] - (int64_t)from[1] * to[0]) >> 30);
            out->rotation_q30[0] = c;
            out->rotation_q30[1] = -s;
            out->rotation_q30[3] = s;
            out->rotation_q30[4] = c;
        }
    }
    if (m->sway_link)
    {
        int64_t rest[3], turned[3];
        sway(m, tick, rest);
        rotate(out->rotation_q30, rest, 0, turned);
        for (unsigned axis = 0; axis < 3; ++axis)
            out->offset_q16[axis] = portable_fixed_3d_clamp((int64_t)out->offset_q16[axis] + turned[axis]);
    }
}

static void rotate(const int32_t r[9], const int64_t v[3], int transpose, int64_t out[3])
{
    for (unsigned i = 0; i < 3; ++i)
    {
        int64_t sum = 0;
        for (unsigned j = 0; j < 3; ++j)
            sum += (int64_t)(transpose ? r[j * 3 + i] : r[i * 3 + j]) * v[j];
        out[i] = sum >> 30;
    }
}

void portable_mesh_pose_point(const PortableMeshPose *pose, const int32_t in[3], int32_t out[3])
{
    int64_t d[3], turned[3];
    for (unsigned axis = 0; axis < 3; ++axis)
        d[axis] = (int64_t)in[axis] - pose->pivot_q16[axis];
    rotate(pose->rotation_q30, d, 0, turned);
    for (unsigned axis = 0; axis < 3; ++axis)
        out[axis] = portable_fixed_3d_clamp(turned[axis] + pose->pivot_q16[axis] + pose->offset_q16[axis]);
}

static void turn_vector(const PortableMeshPose *from, const PortableMeshPose *to, int32_t v[3])
{
    const int64_t in[3] = {v[0], v[1], v[2]};
    int64_t rest[3], out[3];
    rotate(from->rotation_q30, in, 1, rest);
    rotate(to->rotation_q30, rest, 0, out);
    for (unsigned axis = 0; axis < 3; ++axis)
        v[axis] = portable_fixed_3d_clamp(out[axis]);
}

void portable_mesh_pose_carry(const PortableMeshPose *from, const PortableMeshPose *to, int32_t point[3],
                              int32_t forward[3], int32_t velocity[3])
{
    int64_t d[3], rest[3];
    for (unsigned axis = 0; axis < 3; ++axis)
        d[axis] = (int64_t)point[axis] - from->pivot_q16[axis] - from->offset_q16[axis];
    rotate(from->rotation_q30, d, 1, rest);
    int32_t at_rest[3];
    for (unsigned axis = 0; axis < 3; ++axis)
        at_rest[axis] = portable_fixed_3d_clamp(rest[axis] + from->pivot_q16[axis]);
    portable_mesh_pose_point(to, at_rest, point);
    if (forward)
    {
        turn_vector(from, to, forward);
        const int64_t wide[3] = {forward[0], forward[1], forward[2]};
        int32_t unit[3];
        if (portable_fixed_3d_normalize(wide, unit))
            for (unsigned axis = 0; axis < 3; ++axis)
                forward[axis] = unit[axis];
    }
    if (velocity)
        turn_vector(from, to, velocity);
}
