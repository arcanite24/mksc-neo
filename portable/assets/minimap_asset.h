#ifndef MKSC_PORTABLE_MINIMAP_ASSET_H
#define MKSC_PORTABLE_MINIMAP_ASSET_H

#include <stddef.h>
#include <stdint.h>

enum
{
    PORTABLE_MINIMAP_ASSET_OK = 0,
    PORTABLE_MINIMAP_ASSET_INVALID_ARGUMENT = 1,
    PORTABLE_MINIMAP_ASSET_OUT_OF_BOUNDS = 2,
    PORTABLE_MINIMAP_ASSET_CAPACITY = 3,
};

enum
{
    PORTABLE_MINIMAP_WIDTH_TILES = 6,
    PORTABLE_MINIMAP_HEIGHT_TILES = 5,
    PORTABLE_MINIMAP_CELL_COUNT =
        PORTABLE_MINIMAP_WIDTH_TILES * PORTABLE_MINIMAP_HEIGHT_TILES,
    PORTABLE_MINIMAP_MAP_BYTES = PORTABLE_MINIMAP_CELL_COUNT * 2,
    PORTABLE_MINIMAP_TILE_BYTES = 0x240,
};

typedef struct
{
    uint16_t *cells;
    size_t cell_capacity;
    uint8_t *tiles;
    size_t tile_capacity;
} PortableMinimapAssetBuffers;

typedef struct
{
    const uint16_t *cells;
    size_t cell_count;
    size_t width_tiles;
    size_t height_tiles;
    uint32_t source_offset;
    const uint8_t *tiles;
    size_t tile_bytes;
    uint32_t tile_source_offset;
} PortableMinimapAssetView;

/* Import the source's 6x5 route tilemap into caller-owned native cells. */
int portable_minimap_asset_build(const uint8_t *data,
                                size_t data_size,
                                uint32_t source_offset,
                                const PortableMinimapAssetBuffers *buffers,
                                PortableMinimapAssetView *out);

/* Import the map plus the source's 18 route tiles at a separate ROM offset. */
int portable_minimap_asset_build_with_tiles(
    const uint8_t *data,
    size_t data_size,
    uint32_t source_offset,
    uint32_t tile_source_offset,
    const PortableMinimapAssetBuffers *buffers,
    PortableMinimapAssetView *out);

#endif
