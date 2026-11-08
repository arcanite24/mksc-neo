#include "mesh_motion.h"
#include "fixed_3d.h"
#include <stdio.h>
#include <string.h>
#define Q PORTABLE_FIXED_3D_ONE
#define U PORTABLE_FIXED_3D_UNIT
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "mesh motion line %d: %s\n", __LINE__, #x);                            \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int near(const int32_t a[3], const int32_t b[3], int32_t tolerance)
{
    for (unsigned axis = 0; axis < 3; ++axis)
        if (a[axis] - b[axis] > tolerance || b[axis] - a[axis] > tolerance)
            return 0;
    return 1;
}

int main(void)
{
    /* A gear cycle (ramp, run, ramp, stop, reverse) matches stepping it tick by tick. */
    PortableMeshMotion gear;
    memset(&gear, 0, sizeof gear);
    gear.kind = PORTABLE_MESH_MOTION_SPIN;
    gear.axis_q30[2] = U;
    gear.pivot_q16[0] = 10 * Q;
    gear.rate = -3000000;
    gear.run = 40;
    gear.ramp = 15;
    gear.stop = 9;
    CHECK(portable_mesh_motion_valid(&gear));
    int64_t angle = 0;
    int32_t direction = 1;
    uint32_t mode = 0, time = 0;
    for (uint32_t tick = 1; tick < 1000; ++tick)
    {
        /* tick k of a ramp adds rate*k/ramp; whole phases then switch. */
        ++time;
        const uint32_t length = mode == 1 ? gear.run : mode == 3 ? gear.stop : gear.ramp;
        angle += direction * (mode == 0   ? (int64_t)gear.rate * time / gear.ramp
                              : mode == 1 ? gear.rate
                              : mode == 2 ? (int64_t)gear.rate * (gear.ramp - time) / gear.ramp
                                          : 0);
        if (time == length)
        {
            time = 0;
            if (mode == 3)
                direction = -direction;
            mode = (mode + 1) % 4;
        }
        PortableMeshPose pose;
        portable_mesh_motion_pose(&gear, tick, &pose);
        int32_t s, c;
        portable_fixed_3d_sincos((uint16_t)((uint64_t)angle >> 16), &s, &c);
        /* Ramps round per tick in the reference; allow a little drift. */
        CHECK(pose.rotation_q30[0] - c < (1 << 20) && c - pose.rotation_q30[0] < (1 << 20));
        CHECK(pose.rotation_q30[3] - s < (1 << 20) && s - pose.rotation_q30[3] < (1 << 20));
    }
    /* Two half cycles return the gear exactly to rest. */
    PortableMeshPose rest;
    portable_mesh_motion_pose(&gear, 2u * (2u * 15u + 40u + 9u), &rest);
    CHECK(rest.rotation_q30[0] == U && rest.rotation_q30[1] == 0);

    /* A point riding a turning platform keeps its rest coordinates. */
    PortableMeshMotion room = gear;
    room.run = room.ramp = room.stop = 0;
    room.rate = 20000000;
    CHECK(portable_mesh_motion_valid(&room));
    const int32_t start[3] = {15 * Q, 2 * Q, 3 * Q};
    int32_t rider[3] = {15 * Q, 2 * Q, 3 * Q}, forward[3] = {U, 0, 0}, velocity[3] = {5 * Q, 0, 0};
    PortableMeshPose before, after;
    portable_mesh_motion_pose(&room, 0, &before);
    for (uint32_t tick = 1; tick <= 300; ++tick)
    {
        portable_mesh_motion_pose(&room, tick, &after);
        portable_mesh_pose_carry(&before, &after, rider, forward, velocity);
        before = after;
    }
    int32_t expected[3];
    portable_mesh_pose_point(&after, start, expected);
    CHECK(near(rider, expected, Q / 64));
    const int64_t length = portable_fixed_3d_dot(forward, forward) >> 30;
    CHECK(length > U - (U >> 12) && length < U + (U >> 12));
    CHECK(forward[0] == after.rotation_q30[0] || near(forward, (int32_t[3]){after.rotation_q30[0],
                                                                         after.rotation_q30[3], 0}, 1 << 18));
    /* Swings stay within their amplitude and return each period. */
    PortableMeshMotion pendulum;
    memset(&pendulum, 0, sizeof pendulum);
    pendulum.kind = PORTABLE_MESH_MOTION_SWING;
    pendulum.axis_q30[1] = U;
    pendulum.amplitude = 8192;
    pendulum.period = 240;
    CHECK(portable_mesh_motion_valid(&pendulum));
    portable_mesh_motion_pose(&pendulum, 60, &after);
    CHECK(after.rotation_q30[0] < 759250000 + (1 << 20) && after.rotation_q30[0] > 759250000 - (1 << 20));
    portable_mesh_motion_pose(&pendulum, 240, &after);
    CHECK(after.rotation_q30[0] == U);

    /* Routes travel each leg, wait, and loop back to point 0. */
    PortableMeshMotion block;
    memset(&block, 0, sizeof block);
    block.kind = PORTABLE_MESH_MOTION_ROUTE;
    block.point_count = 2;
    block.points_q16[0][0] = 4 * Q;
    block.points_q16[1][0] = 14 * Q;
    block.travel[0] = block.travel[1] = 10;
    block.wait = 5;
    CHECK(portable_mesh_motion_valid(&block));
    portable_mesh_motion_pose(&block, 5, &after);
    CHECK(after.offset_q16[0] == 5 * Q);
    portable_mesh_motion_pose(&block, 12, &after);
    CHECK(after.offset_q16[0] == 10 * Q);
    portable_mesh_motion_pose(&block, 20, &after);
    CHECK(after.offset_q16[0] == 5 * Q);
    portable_mesh_motion_pose(&block, 30, &after);
    CHECK(after.offset_q16[0] == 0);
    /* A phase runs the same route ahead of the clock (a following car). */
    block.phase = 7;
    CHECK(portable_mesh_motion_valid(&block));
    portable_mesh_motion_pose(&block, 5, &after);
    CHECK(after.offset_q16[0] == 10 * Q);
    /* Route windows (crossing signals): the cycle is 30 ticks; phase 7 puts
     * tick 5 at 12. A window wrapping past the cycle's end covers both ends. */
    CHECK(portable_mesh_motion_route_within(&block, 5, 12, 13));
    CHECK(!portable_mesh_motion_route_within(&block, 5, 13, 20));
    CHECK(portable_mesh_motion_route_within(&block, 21, 25, 3) && portable_mesh_motion_route_within(&block, 25, 25, 3));
    CHECK(!portable_mesh_motion_route_within(&block, 26, 25, 3) && !portable_mesh_motion_route_within(&block, 17, 25, 3));
    block.phase = (1u << 24) + 1u;
    CHECK(!portable_mesh_motion_valid(&block));
    block.phase = 0;
    block.travel[1] = 0;
    CHECK(!portable_mesh_motion_valid(&block));
    gear.run = 0;
    CHECK(!portable_mesh_motion_valid(&gear));
    /* Chain sway: link 2 of a stack 1 m per link, bending a quarter turn
     * at the sine's peak; links start `lag` ticks apart. */
    PortableMeshPose plain;
    block.travel[1] = 10;
    block.sway_axis_q30[0] = 1 << 30;
    block.sway_amplitude = 16384;
    block.sway_period = 40;
    block.sway_lag = 10;
    block.sway_length_q16 = Q;
    block.sway_link = 2;
    CHECK(portable_mesh_motion_valid(&block));
    portable_mesh_motion_pose(&block, 0, &after); /* link 0 at its zero crossing, links 1 and 2 not started */
    block.sway_link = 0;
    portable_mesh_motion_pose(&block, 0, &plain);
    CHECK(after.offset_q16[0] == plain.offset_q16[0] && after.offset_q16[2] == plain.offset_q16[2]);
    portable_mesh_motion_pose(&block, 10, &plain);
    block.sway_link = 2;
    portable_mesh_motion_pose(&block, 10, &after); /* a_0 peaks, a_1 starts at zero: both links lie over */
    CHECK(after.offset_q16[0] - plain.offset_q16[0] >= 2 * Q - 64 && after.offset_q16[0] - plain.offset_q16[0] <= 2 * Q + 64);
    CHECK(after.offset_q16[2] - plain.offset_q16[2] >= -2 * Q - 64 && after.offset_q16[2] - plain.offset_q16[2] <= -2 * Q + 64);
    block.sway_axis_q30[2] = 1 << 30;
    CHECK(!portable_mesh_motion_valid(&block)); /* the axis must be horizontal */
    block.sway_axis_q30[2] = 0;
    block.sway_link = 17;
    CHECK(!portable_mesh_motion_valid(&block));
    /* Facing routes turn through each node on the corner's bisector, not at
     * once: a square turns 45 degrees at the corner, 90 by mid-leg. */
    PortableMeshMotion square;
    memset(&square, 0, sizeof square);
    square.kind = PORTABLE_MESH_MOTION_ROUTE;
    square.face = 1;
    square.point_count = 4;
    square.points_q16[1][0] = square.points_q16[2][0] = square.points_q16[2][1] = square.points_q16[3][1] = 10 * Q;
    square.travel[0] = square.travel[1] = square.travel[2] = square.travel[3] = 10;
    CHECK(portable_mesh_motion_valid(&square));
    portable_mesh_motion_pose(&square, 10, &after);
    CHECK(after.rotation_q30[0] > 759250000 - (1 << 20) && after.rotation_q30[0] < 759250000 + (1 << 20) &&
          after.rotation_q30[3] > 759250000 - (1 << 20) && after.rotation_q30[3] < 759250000 + (1 << 20));
    portable_mesh_motion_pose(&square, 15, &after);
    CHECK(after.rotation_q30[0] > -(1 << 20) && after.rotation_q30[0] < (1 << 20) && after.rotation_q30[3] > U - (1 << 20));
    portable_mesh_motion_pose(&square, 9, &after);
    CHECK(after.rotation_q30[3] > 0 && after.rotation_q30[3] < 759250000);
    puts("mesh motion: PASS");
    return 0;
}
