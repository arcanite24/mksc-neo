#ifndef MKSC_PORTABLE_TRACK_GEOMETRY_H
#define MKSC_PORTABLE_TRACK_GEOMETRY_H

#include <stddef.h>
#include <stdint.h>

#include "track_asset.h"

typedef enum
{
    PORTABLE_TRACK_GEOMETRY_OK = 0,
    PORTABLE_TRACK_GEOMETRY_INVALID_ARGUMENT,
    PORTABLE_TRACK_GEOMETRY_INVALID_HEADER,
    PORTABLE_TRACK_GEOMETRY_OUT_OF_BOUNDS,
} PortableTrackGeometryStatus;

typedef struct
{
    const uint8_t *track_data;
    size_t track_size;
    uint16_t nr_checkpoints;
    size_t track_base_offset;
    uint16_t checkpoint_table_offset;
    uint8_t grid_width;
} PortableTrackGeometryView;

typedef struct
{
    uint16_t x;
    uint16_t y;
    uint8_t ai_mode;
} PortableTrackCheckpoint;

PortableTrackGeometryStatus portable_track_geometry_init(
    const PortableTrackAsset *asset,
    uint16_t nr_checkpoints,
    PortableTrackGeometryView *out);

PortableTrackGeometryStatus portable_track_geometry_read_checkpoint(
    const PortableTrackGeometryView *view,
    uint32_t index,
    uint8_t row,
    PortableTrackCheckpoint *out);

#endif
