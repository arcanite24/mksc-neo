#ifndef MKSC_PORTABLE_COURSE_SCENERY_ASSET_H
#define MKSC_PORTABLE_COURSE_SCENERY_ASSET_H

#include <stddef.h>
#include <stdint.h>
#include "native_image.h"

#define PORTABLE_COURSE_SCENERY_MAX_ART 64u

#define PORTABLE_COURSE_SCENERY_MAX_OBJECTS 64u
#define PORTABLE_COURSE_SCENERY_LOD64_TILE_BYTES 2048u
#define PORTABLE_COURSE_SCENERY_LOD32_TILE_BYTES 512u
#define PORTABLE_COURSE_SCENERY_LOD16_TILE_BYTES 128u
#define PORTABLE_COURSE_SCENERY_LOD8_TILE_BYTES 32u
#define PORTABLE_COURSE_SCENERY_TILE_BYTES 672u
#define PORTABLE_COURSE_SCENERY_ALL_TILE_BYTES \
    (PORTABLE_COURSE_SCENERY_LOD64_TILE_BYTES + \
     PORTABLE_COURSE_SCENERY_TILE_BYTES)
#define PORTABLE_COURSE_SCENERY_PALETTE_COUNT 16u

typedef struct
{
    int32_t position_x_q16;
    int32_t position_y_q16;
    uint8_t source_parameter;
    uint8_t art_index;
    uint16_t affine_q8; /* zero retains the legacy256 default */
    uint32_t source_code;
} PortableCourseSceneryObject;

typedef struct
{
    PortableNativeImage lod[4];
    uint8_t distance_shift[4];
} PortableCourseSceneryArt;

typedef struct
{
    const PortableCourseSceneryObject *objects;
    size_t object_count;
    const uint8_t *tiles;
    const uint16_t *palette;
    const uint8_t *lod32_tiles;
    const uint8_t *lod8_tiles;
    const uint8_t *lod64_tiles;
    /* Optional four little-endian ROM records: 8-byte single-cell
     * descriptor followed by its 32-bit mipmap shift. No guest pointers. */
    const uint8_t *mipmap_cells;
    const PortableCourseSceneryArt *native_art;
    size_t native_art_count;
} PortableCourseSceneryView;

#endif
