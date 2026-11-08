#ifndef MKSC_PORTABLE_ROAD_PALETTE_ASSET_H
#define MKSC_PORTABLE_ROAD_PALETTE_ASSET_H

#include <stddef.h>
#include <stdint.h>

enum
{
    PORTABLE_ROAD_PALETTE_COURSE_COUNT = 48u,
    PORTABLE_ROAD_PALETTE_FIXED_COUNT = 16u,
    PORTABLE_ROAD_PALETTE_COUNT =
        PORTABLE_ROAD_PALETTE_COURSE_COUNT +
        PORTABLE_ROAD_PALETTE_FIXED_COUNT,
    PORTABLE_ROAD_PALETTE_FIXED_BYTES =
        PORTABLE_ROAD_PALETTE_FIXED_COUNT * sizeof(uint16_t),
};

typedef enum
{
    PORTABLE_ROAD_PALETTE_OK = 0,
    PORTABLE_ROAD_PALETTE_INVALID_ARGUMENT,
    PORTABLE_ROAD_PALETTE_OUT_OF_BOUNDS,
    PORTABLE_ROAD_PALETTE_CAPACITY,
} PortableRoadPaletteStatus;

/* Copy the source's fixed palette suffix at ROM symbol _08076DE8. */
PortableRoadPaletteStatus portable_road_palette_load_fixed(
    const uint8_t *rom,
    size_t rom_size,
    uint32_t source_offset,
    uint16_t *destination,
    size_t destination_capacity);

#endif
