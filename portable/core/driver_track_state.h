#ifndef MKSC_PORTABLE_DRIVER_TRACK_STATE_H
#define MKSC_PORTABLE_DRIVER_TRACK_STATE_H

#include <stdint.h>

#include "terrain_query.h"
#include "track_geometry.h"
#include "track_raster.h"

typedef enum
{
    PORTABLE_DRIVER_TRACK_STATE_OK = 0,
    PORTABLE_DRIVER_TRACK_STATE_INVALID_ARGUMENT = 1,
    PORTABLE_DRIVER_TRACK_STATE_GEOMETRY_ERROR = 2,
    PORTABLE_DRIVER_TRACK_STATE_TRACK_QUERY_ERROR = 3,
    PORTABLE_DRIVER_TRACK_STATE_TERRAIN_QUERY_ERROR = 4,
} PortableDriverTrackStateStatus;

typedef struct
{
    uint32_t requested_checkpoint;
    uint32_t selected_checkpoint;
    uint8_t checkpoint_row;
    PortableTrackCheckpoint checkpoint;
    int32_t world_x;
    int32_t world_y;
    uint8_t track_cell;
    uint8_t terrain_cell;
    uint8_t track_cell_valid;
    uint8_t terrain_cell_valid;
    int track_query_status;
    int terrain_query_status;
} PortableDriverTrackState;

/*
 * Sample the source-shaped checkpoint target plus both accepted map views at
 * one driver position. This is read-only integration; it does not mutate a
 * checkpoint, lap, height or collision state.
 */
PortableDriverTrackStateStatus portable_driver_track_state_sample(
    const PortableTrackGeometryView *geometry,
    const PortableTrackRaster *raster,
    const PortableTerrainView *terrain,
    uint32_t checkpoint_index,
    uint8_t checkpoint_row,
    int32_t world_x,
    int32_t world_y,
    PortableDriverTrackState *out);

#endif
