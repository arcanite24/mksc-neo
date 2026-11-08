#ifndef MKSC_PORTABLE_DRIVER_TICK_H
#define MKSC_PORTABLE_DRIVER_TICK_H

#include <stdint.h>

#include "track_geometry.h"

typedef enum
{
    PORTABLE_DRIVER_TICK_CHANGED = 0,
    PORTABLE_DRIVER_TICK_STABLE,
    PORTABLE_DRIVER_TICK_INVALID_ARGUMENT,
    PORTABLE_DRIVER_TICK_GEOMETRY_ERROR,
} PortableDriverTickResult;

/* Native names for the serialized widths used by sub_8039828/sub_8039894. */
typedef struct
{
    uint8_t checkpoint_index;
    int16_t target_x;
    int16_t target_y;
    uint16_t target_heading;
    uint16_t heading;
    uint16_t heading_copy;
    uint16_t heading_target_copy;
} PortableDriverTickState;

PortableDriverTickResult portable_driver_tick(
    const PortableTrackGeometryView *geometry,
    PortableDriverTickState *state,
    uint16_t target_heading);

#endif
