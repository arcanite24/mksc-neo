#ifndef MKSC_PORTABLE_TERRAIN_ASSET_H
#define MKSC_PORTABLE_TERRAIN_ASSET_H

#include <stddef.h>
#include <stdint.h>

#include "terrain_query.h"
#include "track_asset.h"

enum
{
    PORTABLE_TERRAIN_ASSET_OK = 0,
    PORTABLE_TERRAIN_ASSET_INVALID_ARGUMENT = 1,
    PORTABLE_TERRAIN_ASSET_INVALID_HEADER = 2,
    PORTABLE_TERRAIN_ASSET_DESTINATION_TOO_SMALL = 3,
    PORTABLE_TERRAIN_ASSET_TRUNCATED = 4,
    PORTABLE_TERRAIN_ASSET_INVALID_STREAM = 5,
};

typedef struct
{
    PortableTerrainView view;
    size_t decoded_buffer_size;
    uint32_t table_offset;
    uint8_t compressed;
    uint8_t block_count;
} PortableTerrainAssetView;

/* Load the source terrain definition at blob +0xd4 into caller-owned bytes. */
int portable_terrain_asset_build(const PortableTrackAsset *asset,
                                 uint8_t *cells,
                                 size_t capacity,
                                 PortableTerrainAssetView *out);

#endif
