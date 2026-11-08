#ifndef PORTABLE_MESH_MOTION_H
#define PORTABLE_MESH_MOTION_H
#include <stdint.h>

/* Kinematic course objects (moving platforms, rotating rooms, patrolling
 * hazards): a pure function of the race tick, so every racer, replay and the
 * renderer agree without stored state. Positions are Q16 metres, directions
 * Q30, angles binary turns (1<<32 or 65536 per turn as named). */
enum
{
    PORTABLE_MESH_MOTION_SPIN = 1,  /* about axis through pivot */
    PORTABLE_MESH_MOTION_SWING = 2, /* sine swing about axis through pivot */
    PORTABLE_MESH_MOTION_ROUTE = 3, /* closed polyline translation */
    PORTABLE_MESH_MOTION_POINT_LIMIT = 256,
    PORTABLE_MESH_MOTION_TICK_LIMIT = 65535
};
typedef struct
{
    uint32_t kind;
    /* Ticks the motion runs ahead of the race clock (at most 1<<24), so
     * objects sharing one route or cycle keep fixed spacing. */
    uint32_t phase;
    int32_t pivot_q16[3], axis_q30[3];
    /* Spin: rate turns<<32 per tick (sign = first direction). run 0 spins
     * forever; otherwise each half cycle ramps up over `ramp` ticks, runs
     * `run`, ramps down over `ramp`, holds `stop` and reverses direction. */
    int32_t rate;
    uint32_t run, ramp, stop;
    /* Swing: angle = amplitude (turns<<16, at most a quarter) * sin(2 pi t / period). */
    uint32_t amplitude, period;
    /* Route: from point i the object travels to i+1 (the last to 0) over
     * travel[i] ticks, then waits `wait` ticks; offset is relative to point 0.
     * `face` turns it about world up from leg 0's heading to the current leg's
     * (legs without a horizontal run keep the rest heading). */
    uint32_t point_count, wait, face;
    int32_t points_q16[PORTABLE_MESH_MOTION_POINT_LIMIT][3];
    uint32_t travel[PORTABLE_MESH_MOTION_POINT_LIMIT];
    /* Chain sway (any kind; sway_link 0 = none): link j of a stack bends by
     * a_j = sway_amplitude (turns<<16) * sin(2 pi (t - j lag) / period), zero
     * before j*lag ticks. Link k (at most 16) stands on links 1..k, each
     * sway_length tall and turned by the sum of the bends below and its own,
     * so it moves by sum over m = 1..k of length * (sin A_m axis + (cos A_m - 1)
     * up), A_m = a_0 + ... + a_m, in the rest frame (axis a horizontal unit
     * Q30), then turned with the pose (a swaying stack of Pokey segments). */
    int32_t sway_axis_q30[3], sway_length_q16;
    uint32_t sway_link, sway_amplitude, sway_period, sway_lag;
} PortableMeshMotion;
/* p' = pivot + offset + R (p - pivot). */
typedef struct
{
    int32_t rotation_q30[9], pivot_q16[3], offset_q16[3];
} PortableMeshPose;
int portable_mesh_motion_valid(const PortableMeshMotion *motion);
/* Requires a valid motion. */
void portable_mesh_motion_pose(const PortableMeshMotion *motion, uint32_t tick, PortableMeshPose *out);
/* Whether a valid route motion at `tick` is in [from, to) ticks of its
 * cycle from point 0 (the window wraps when from > to). */
int portable_mesh_motion_route_within(const PortableMeshMotion *motion, uint32_t tick, uint32_t from, uint32_t to);
/* Rest (tick-independent) point to its posed position. */
void portable_mesh_pose_point(const PortableMeshPose *pose, const int32_t in_q16[3], int32_t out_q16[3]);
/* Carries something riding the object from pose `from` to `to`: the point
 * moves rigidly, each nonnull vector (Q30 or Q16, rotation only) turns. */
void portable_mesh_pose_carry(const PortableMeshPose *from, const PortableMeshPose *to, int32_t point_q16[3],
                              int32_t forward_q30[3], int32_t velocity_q16[3]);
#endif
