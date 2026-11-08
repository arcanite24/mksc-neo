#include "driver_pose.h"

#include <limits.h>

static int32_t add_wrap32(int32_t left, int32_t right)
{
    const uint32_t bits = (uint32_t)left + (uint32_t)right;

    if (bits < UINT32_C(0x80000000))
        return (int32_t)bits;
    return (int32_t)(-((int64_t)(UINT64_C(0x100000000) - bits)));
}

static int32_t world_floor_q16(int32_t value)
{
    const int64_t widened = value;

    if (widened >= 0)
        return (int32_t)(widened / INT64_C(0x10000));
    return (int32_t)(-((-widened + INT64_C(0xffff)) / INT64_C(0x10000)));
}

int portable_driver_pose_tick(const PortableTrackRaster *raster,
                              PortableDriverPoseState *state)
{
    int32_t world_x;
    int32_t world_y;
    uint8_t cell;
    int result;

    if (raster == NULL || raster->cells == NULL || state == NULL)
        return PORTABLE_DRIVER_POSE_INVALID_ARGUMENT;

    state->position_x = add_wrap32(
        state->position_x, (int32_t)state->component_x * INT32_C(0x100));
    state->position_y = add_wrap32(
        state->position_y, (int32_t)state->component_y * INT32_C(0x100));
    world_x = world_floor_q16(state->position_x);
    world_y = world_floor_q16(state->position_y);
    state->sampled_world_x = world_x;
    state->sampled_world_y = world_y;

    result = portable_track_raster_query16(raster, world_x, world_y, &cell);
    if (result != PORTABLE_TRACK_RASTER_OK)
    {
        state->track_cell_valid = 0;
        return result == PORTABLE_TRACK_RASTER_OUT_OF_BOUNDS
                   ? PORTABLE_DRIVER_POSE_TRACK_OUT_OF_BOUNDS
                   : PORTABLE_DRIVER_POSE_INVALID_ARGUMENT;
    }
    state->track_cell = cell;
    state->track_cell_valid = 1;
    return PORTABLE_DRIVER_POSE_OK;
}
