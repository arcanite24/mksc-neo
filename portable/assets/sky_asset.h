#ifndef MKSC_PORTABLE_SKY_ASSET_H
#define MKSC_PORTABLE_SKY_ASSET_H

#include <stddef.h>
#include <stdint.h>

#include "asset_pack.h"
#include "sky_motion.h"

typedef enum
{
    PORTABLE_SKY_ASSET_OK = 0,
    PORTABLE_SKY_ASSET_INVALID_ARGUMENT,
    PORTABLE_SKY_ASSET_OUT_OF_BOUNDS,
    PORTABLE_SKY_ASSET_CAPACITY,
    PORTABLE_SKY_ASSET_FORMAT,
    PORTABLE_SKY_ASSET_UNSUPPORTED
} PortableSkyAssetStatus;

typedef struct
{
    uint8_t *tiles;
    size_t tile_capacity;
    uint16_t *palette;
    size_t palette_capacity;
    uint8_t *front_map;
    size_t front_capacity;
    uint8_t *mid_map;
    size_t mid_capacity;
    uint8_t *back_map;
    size_t back_capacity;
} PortableSkyAssetBuffers;

typedef struct
{
    const uint8_t *tiles;
    size_t tile_bytes;
    const uint16_t *palette;
    size_t palette_count;
    const uint8_t *front_map;
    size_t front_map_bytes;
    const uint8_t *mid_map;
    size_t mid_map_bytes;
    const uint8_t *back_map;
    size_t back_map_bytes;
    size_t front_width_tiles;
    size_t mid_width_tiles;
    size_t back_width_tiles;
    size_t map_height_tiles;
    uint8_t size;
    uint8_t compression_flags;
    uint16_t descriptor;
    uint32_t background_offset;
    uint32_t background_index;
    uint32_t palette_hardware_base;
    const PortableSkyMotionRules *motion_rules;
} PortableSkyAssetView;

/* Validate a packed SKY1 section and decode its optional normalized motion.
 * Both simulation loading and presentation loading use this same decoder. */
PortableSkyAssetStatus portable_sky_asset_open_motion(
    const PortableAssetSectionView *section,
    PortableSkyMotionRules *storage,
    const PortableSkyMotionRules **out_rules);

PortableSkyAssetStatus portable_sky_asset_build(
    const uint8_t *data,
    size_t data_size,
    uint32_t background_index,
    const PortableSkyAssetBuffers *buffers,
    PortableSkyAssetView *out_view);

#endif
