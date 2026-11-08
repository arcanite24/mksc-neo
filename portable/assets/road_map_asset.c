#include "road_map_asset.h"
#include "terrain_asset.h"

#include <string.h>

static int span_is_valid(size_t data_size, size_t offset, size_t length)
{
    return offset <= data_size && length <= data_size - offset;
}

static PortableRoadMapStatus decode_lz77(const uint8_t *data,
                                         size_t data_size,
                                         size_t source_offset,
                                         uint8_t *destination,
                                         size_t destination_capacity,
                                         size_t *out_size)
{
    size_t source;
    size_t produced = 0;
    uint32_t expected;

    if (data == NULL || destination == NULL || out_size == NULL ||
        !span_is_valid(data_size, source_offset, 4))
        return PORTABLE_ROAD_MAP_INVALID_ARGUMENT;
    if (data[source_offset] != 0x10)
        return PORTABLE_ROAD_MAP_FORMAT;
    expected = (uint32_t)data[source_offset + 1] |
               ((uint32_t)data[source_offset + 2] << 8) |
               ((uint32_t)data[source_offset + 3] << 16);
    if (expected == 0)
        return PORTABLE_ROAD_MAP_FORMAT;
    if (expected > destination_capacity)
        return PORTABLE_ROAD_MAP_CAPACITY;

    source = source_offset + 4;
    while (produced < expected)
    {
        uint8_t flags;
        unsigned bit;

        if (!span_is_valid(data_size, source, 1))
            return PORTABLE_ROAD_MAP_OUT_OF_BOUNDS;
        flags = data[source++];
        for (bit = 0; bit < 8 && produced < expected; ++bit)
        {
            if ((flags & (uint8_t)(0x80u >> bit)) == 0)
            {
                if (!span_is_valid(data_size, source, 1))
                    return PORTABLE_ROAD_MAP_OUT_OF_BOUNDS;
                destination[produced++] = data[source++];
            }
            else
            {
                uint8_t first;
                uint8_t second;
                size_t distance;
                size_t length;
                size_t copy;

                if (!span_is_valid(data_size, source, 2))
                    return PORTABLE_ROAD_MAP_OUT_OF_BOUNDS;
                first = data[source++];
                second = data[source++];
                distance = (((size_t)first & 0x0fu) << 8) |
                           (size_t)second;
                distance += 1u;
                length = ((size_t)first >> 4) + 3u;
                if (distance > produced || length > expected - produced)
                    return PORTABLE_ROAD_MAP_FORMAT;
                for (copy = 0; copy < length; ++copy)
                {
                    destination[produced] = destination[produced - distance];
                    ++produced;
                }
            }
        }
    }
    *out_size = produced;
    return PORTABLE_ROAD_MAP_OK;
}

PortableRoadMapStatus portable_road_map_asset_build(
    const uint8_t *data,
    size_t data_size,
    uint32_t source_offset,
    uint32_t source_offset_second,
    const PortableRoadMapBuffers *buffers,
    PortableRoadMapView *out_view)
{
    size_t decoded_size_first = 0;
    size_t decoded_size_second = 0;
    PortableRoadMapStatus status;

    if (data == NULL || buffers == NULL || out_view == NULL)
        return PORTABLE_ROAD_MAP_INVALID_ARGUMENT;
    if (buffers->map == NULL || buffers->map_capacity < PORTABLE_ROAD_MAP_BYTES)
        return PORTABLE_ROAD_MAP_CAPACITY;
    status = decode_lz77(data, data_size, (size_t)source_offset, buffers->map,
                         PORTABLE_ROAD_MAP_BLOCK_BYTES, &decoded_size_first);
    if (status != PORTABLE_ROAD_MAP_OK)
        return status;
    if (decoded_size_first != PORTABLE_ROAD_MAP_BLOCK_BYTES)
        return PORTABLE_ROAD_MAP_FORMAT;
    status = decode_lz77(data, data_size, (size_t)source_offset_second,
                         buffers->map + PORTABLE_ROAD_MAP_BLOCK_BYTES,
                         PORTABLE_ROAD_MAP_BLOCK_BYTES, &decoded_size_second);
    if (status != PORTABLE_ROAD_MAP_OK)
        return status;
    if (decoded_size_second != PORTABLE_ROAD_MAP_BLOCK_BYTES)
        return PORTABLE_ROAD_MAP_FORMAT;

    memset(out_view, 0, sizeof(*out_view));
    out_view->map = buffers->map;
    out_view->map_bytes = PORTABLE_ROAD_MAP_BYTES;
    out_view->width_tiles = PORTABLE_ROAD_MAP_WIDTH_TILES;
    out_view->height_tiles = PORTABLE_ROAD_MAP_HEIGHT_TILES;
    out_view->source_offset = source_offset;
    out_view->source_offset_second = source_offset_second;
    return PORTABLE_ROAD_MAP_OK;
}

PortableRoadMapStatus portable_road_map_asset_build_course(
    const PortableTrackAsset *asset,
    const PortableRoadMapBuffers *buffers,
    PortableRoadMapView *out_view)
{
    PortableTerrainAssetView terrain;
    if(!asset || !buffers || !out_view || !buffers->map)
        return PORTABLE_ROAD_MAP_INVALID_ARGUMENT;
    int status=portable_terrain_asset_build(asset,buffers->map,buffers->map_capacity,&terrain);
    if(status==PORTABLE_TERRAIN_ASSET_DESTINATION_TOO_SMALL)return PORTABLE_ROAD_MAP_CAPACITY;
    if(status!=PORTABLE_TERRAIN_ASSET_OK)return PORTABLE_ROAD_MAP_FORMAT;
    *out_view=(PortableRoadMapView){
        terrain.view.cells,terrain.view.width*terrain.view.height,
        terrain.view.width,terrain.view.height,
        asset->source_offset+terrain.table_offset,
        asset->source_offset+terrain.table_offset+(terrain.compressed?32u:0u)};
    return PORTABLE_ROAD_MAP_OK;
}
