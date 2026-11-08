#include "minimap_asset.h"

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

int portable_minimap_asset_build_with_tiles(
    const uint8_t *data,
    size_t data_size,
    uint32_t source_offset,
    uint32_t tile_source_offset,
    const PortableMinimapAssetBuffers *buffers,
    PortableMinimapAssetView *out)
{
    const size_t offset = (size_t)source_offset;
    const size_t tile_offset = (size_t)tile_source_offset;

    if (data == NULL || buffers == NULL || out == NULL ||
        buffers->cells == NULL)
        return PORTABLE_MINIMAP_ASSET_INVALID_ARGUMENT;
    if (offset > data_size || data_size - offset < PORTABLE_MINIMAP_MAP_BYTES)
        return PORTABLE_MINIMAP_ASSET_OUT_OF_BOUNDS;
    if (buffers->cell_capacity < PORTABLE_MINIMAP_CELL_COUNT)
        return PORTABLE_MINIMAP_ASSET_CAPACITY;
    if (buffers->tiles == NULL || buffers->tile_capacity <
                                    PORTABLE_MINIMAP_TILE_BYTES)
        return PORTABLE_MINIMAP_ASSET_CAPACITY;
    if (tile_offset > data_size ||
        data_size - tile_offset < PORTABLE_MINIMAP_TILE_BYTES)
        return PORTABLE_MINIMAP_ASSET_OUT_OF_BOUNDS;

    for (size_t index = 0; index < PORTABLE_MINIMAP_CELL_COUNT; ++index)
        buffers->cells[index] = read_le16(data + offset + index * 2);
    for (size_t index = 0; index < PORTABLE_MINIMAP_TILE_BYTES; ++index)
        buffers->tiles[index] = data[tile_offset + index];
    out->cells = buffers->cells;
    out->cell_count = PORTABLE_MINIMAP_CELL_COUNT;
    out->width_tiles = PORTABLE_MINIMAP_WIDTH_TILES;
    out->height_tiles = PORTABLE_MINIMAP_HEIGHT_TILES;
    out->source_offset = source_offset;
    out->tiles = buffers->tiles;
    out->tile_bytes = PORTABLE_MINIMAP_TILE_BYTES;
    out->tile_source_offset = tile_source_offset;
    return PORTABLE_MINIMAP_ASSET_OK;
}

int portable_minimap_asset_build(const uint8_t *data,
                                size_t data_size,
                                uint32_t source_offset,
                                const PortableMinimapAssetBuffers *buffers,
                                PortableMinimapAssetView *out)
{
    const size_t offset = (size_t)source_offset;

    if (data == NULL || buffers == NULL || out == NULL ||
        buffers->cells == NULL)
        return PORTABLE_MINIMAP_ASSET_INVALID_ARGUMENT;
    if (offset > data_size || data_size - offset < PORTABLE_MINIMAP_MAP_BYTES)
        return PORTABLE_MINIMAP_ASSET_OUT_OF_BOUNDS;
    if (buffers->cell_capacity < PORTABLE_MINIMAP_CELL_COUNT)
        return PORTABLE_MINIMAP_ASSET_CAPACITY;

    for (size_t index = 0; index < PORTABLE_MINIMAP_CELL_COUNT; ++index)
        buffers->cells[index] = read_le16(data + offset + index * 2);
    out->cells = buffers->cells;
    out->cell_count = PORTABLE_MINIMAP_CELL_COUNT;
    out->width_tiles = PORTABLE_MINIMAP_WIDTH_TILES;
    out->height_tiles = PORTABLE_MINIMAP_HEIGHT_TILES;
    out->source_offset = source_offset;
    out->tiles = NULL;
    out->tile_bytes = 0;
    out->tile_source_offset = 0;
    return PORTABLE_MINIMAP_ASSET_OK;
}
