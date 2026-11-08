#ifndef MKSC_PORTABLE_DRIVER_POSE_H
#define MKSC_PORTABLE_DRIVER_POSE_H

#include <stdint.h>

#include "track_raster.h"

enum
{
    PORTABLE_DRIVER_POSE_OK = 0,
    PORTABLE_DRIVER_POSE_INVALID_ARGUMENT = 1,
    PORTABLE_DRIVER_POSE_TRACK_OUT_OF_BOUNDS = 2,
};

typedef struct
{
    int32_t position_x;
    int32_t position_y;
    int32_t position_z;
    int16_t component_x;
    int16_t component_y;
    int32_t sampled_world_x;
    int32_t sampled_world_y;
    uint8_t track_cell;
    uint8_t track_cell_valid;
} PortableDriverPoseState;

int portable_driver_pose_tick(const PortableTrackRaster *raster,
                              PortableDriverPoseState *state);

#endif
