#ifndef MKSC_PORTABLE_ROAD_MAP_ASSET_H
#define MKSC_PORTABLE_ROAD_MAP_ASSET_H

#include <stddef.h>
#include <stdint.h>

#include "track_asset.h"

enum
{
    PORTABLE_ROAD_MAP_BLOCK_BYTES = 0x2000u,
    /* BG2CNT=0xF889 selects screen base 24 (0x0600C000) and affine size 3.
     * The source loader fills C000/E000 with two adjacent 0x2000-byte blocks. */
    PORTABLE_ROAD_MAP_BYTES = PORTABLE_ROAD_MAP_BLOCK_BYTES * 2u,
    PORTABLE_ROAD_MAP_WIDTH_TILES = 128u,
    PORTABLE_ROAD_MAP_HEIGHT_TILES = 128u,
    /* ROM sub_8033A08: sixteen non-overlapping 0x1000-byte slots. */
    PORTABLE_ROAD_COURSE_MAP_STORAGE_BYTES = 0x10000u,
    PORTABLE_ROAD_COURSE_MAP_SLOT_BYTES = 0x1000u,
    PORTABLE_ROAD_COURSE_MAP_DECODE_BYTES = 0x1000u,
    PORTABLE_ROAD_COURSE_MAP_SLOTS = 16u,
};

typedef enum
{
    PORTABLE_ROAD_MAP_OK = 0,
    PORTABLE_ROAD_MAP_INVALID_ARGUMENT,
    PORTABLE_ROAD_MAP_OUT_OF_BOUNDS,
    PORTABLE_ROAD_MAP_CAPACITY,
    PORTABLE_ROAD_MAP_FORMAT,
} PortableRoadMapStatus;

typedef struct
{
    uint8_t *map;
    size_t map_capacity;
} PortableRoadMapBuffers;

typedef struct
{
    const uint8_t *map;
    size_t map_bytes;
    size_t width_tiles;
    size_t height_tiles;
    uint32_t source_offset;
    uint32_t source_offset_second;
} PortableRoadMapView;

/* Build the rolling 128x128 BG tile window from an immutable full world map.
 * World and destination must not overlap. The caller owns both lifetimes.
 * Pose quantization/quarter-turn look-ahead follow sub_8032A18/sub_8033838. */
PortableRoadMapStatus portable_road_map_asset_repack(
    const PortableRoadMapView *world,
    const PortableRoadMapBuffers *window,
    int32_t source_x_q16,
    int32_t source_y_q16,
    int16_t heading,
    const int32_t *sine_table,
    size_t sine_count,
    PortableRoadMapView *io_view);

/* Decode the two source affine-BG streams into one caller-owned map. */
PortableRoadMapStatus portable_road_map_asset_build(
    const uint8_t *data,
    size_t data_size,
    uint32_t source_offset,
    uint32_t source_offset_second,
    const PortableRoadMapBuffers *buffers,
    PortableRoadMapView *out_view);

/* Decode the complete world map using the shared terrain loader. Dimensions
 * are header[4/5]*128 eight-unit tiles, not the smaller rolling BG window. */
PortableRoadMapStatus portable_road_map_asset_build_course(
    const PortableTrackAsset *asset,
    const PortableRoadMapBuffers *buffers,
    PortableRoadMapView *out_view);

#endif
