#ifndef MKSC_PORTABLE_TRACK_RASTER_H
#define MKSC_PORTABLE_TRACK_RASTER_H

#include <stddef.h>
#include <stdint.h>

#include "track_asset.h"

enum
{
    PORTABLE_TRACK_RASTER_OK = 0,
    PORTABLE_TRACK_RASTER_INVALID_ARGUMENT = 1,
    PORTABLE_TRACK_RASTER_INVALID_SECTION = 2,
    PORTABLE_TRACK_RASTER_DESTINATION_TOO_SMALL = 3,
    PORTABLE_TRACK_RASTER_INVALID_COMMAND = 4,
    PORTABLE_TRACK_RASTER_OUT_OF_BOUNDS = 5,
};

typedef struct
{
    uint8_t *cells;
    size_t capacity;
    size_t width;
    size_t height;
    uint8_t command_count;
    uint16_t entry_offset;
} PortableTrackRaster;

int portable_track_raster_build(const PortableTrackAsset *asset,
                                uint8_t *cells,
                                size_t capacity,
                                PortableTrackRaster *out);
int portable_track_raster_query16(const PortableTrackRaster *raster,
                                  int32_t x,
                                  int32_t y,
                                  uint8_t *out_value);

#endif
