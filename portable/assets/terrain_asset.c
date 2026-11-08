#include "terrain_asset.h"

#include <stdint.h>
#include <string.h>

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t read_le24(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16);
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int decode_lz77(const uint8_t *source,
                       size_t source_size,
                       uint8_t *destination,
                       size_t destination_capacity,
                       size_t *out_size)
{
    size_t source_cursor;
    size_t destination_cursor;
    size_t decompressed_size;

    if (source == NULL || destination == NULL || out_size == NULL)
        return PORTABLE_TERRAIN_ASSET_INVALID_ARGUMENT;
    if (source_size < 4 || source[0] != 0x10)
        return PORTABLE_TERRAIN_ASSET_INVALID_HEADER;
    decompressed_size = read_le24(source + 1);
    if (decompressed_size > destination_capacity)
        return PORTABLE_TERRAIN_ASSET_DESTINATION_TOO_SMALL;

    source_cursor = 4;
    destination_cursor = 0;
    while (destination_cursor < decompressed_size)
    {
        uint8_t flags;
        int bit;

        if (source_cursor >= source_size)
            return PORTABLE_TERRAIN_ASSET_TRUNCATED;
        flags = source[source_cursor++];
        for (bit = 7; bit >= 0 && destination_cursor < decompressed_size;
             --bit)
        {
            if ((flags & (uint8_t)(1u << bit)) == 0)
            {
                if (source_cursor >= source_size)
                    return PORTABLE_TERRAIN_ASSET_TRUNCATED;
                destination[destination_cursor++] = source[source_cursor++];
            }
            else
            {
                uint8_t first;
                uint8_t second;
                size_t length;
                size_t displacement;
                size_t copy;

                if (source_size - source_cursor < 2)
                    return PORTABLE_TERRAIN_ASSET_TRUNCATED;
                first = source[source_cursor++];
                second = source[source_cursor++];
                length = (size_t)(first >> 4) + 3u;
                displacement = ((size_t)(first & 0x0fu) << 8) | second;
                if (displacement >= destination_cursor)
                    return PORTABLE_TERRAIN_ASSET_INVALID_STREAM;
                for (copy = 0; copy < length &&
                                destination_cursor < decompressed_size;
                     ++copy)
                {
                    destination[destination_cursor] =
                        destination[destination_cursor - displacement - 1u];
                    ++destination_cursor;
                }
            }
        }
    }
    *out_size = decompressed_size;
    return PORTABLE_TERRAIN_ASSET_OK;
}

int portable_terrain_asset_build(const PortableTrackAsset *asset,
                                 uint8_t *cells,
                                 size_t capacity,
                                 PortableTerrainAssetView *out)
{
    size_t row_stride;
    size_t height;
    size_t view_size;
    uint32_t table_offset;
    int compressed;

    if (asset == NULL || asset->data == NULL || cells == NULL || out == NULL)
        return PORTABLE_TERRAIN_ASSET_INVALID_ARGUMENT;
    if (asset->size < 0xd8 || asset->data[4] == 0 || asset->data[5] == 0)
        return PORTABLE_TERRAIN_ASSET_INVALID_HEADER;

    /* sub_8032CA0 doubles field_2c (header width << 6) for 8-unit tiles. */
    row_stride = (size_t)asset->data[4] * 128u;
    height = (size_t)asset->data[5] * 128u;
    if (row_stride == 0 || height == 0 || height > SIZE_MAX / row_stride)
        return PORTABLE_TERRAIN_ASSET_INVALID_HEADER;
    view_size = row_stride * height;
    table_offset = read_le32(asset->data + 0xd4);
    if (table_offset > asset->size || asset->size - table_offset < 4)
        return PORTABLE_TERRAIN_ASSET_INVALID_HEADER;

    compressed = (asset->data[3] & 2u) != 0;
    memset(out, 0, sizeof(*out));
    out->table_offset = table_offset;
    out->compressed = (uint8_t)compressed;
    out->view.cells = cells;
    out->view.row_stride = row_stride;
    out->view.width = row_stride;
    out->view.height = height;

    if (compressed)
    {
        const size_t destination_stride = 0x1000;
        const size_t block_count = 16;
        size_t index;

        if (asset->size - table_offset < block_count * 2u ||
            capacity < destination_stride * block_count ||
            view_size > destination_stride * block_count)
            return asset->size - table_offset < block_count * 2u
                       ? PORTABLE_TERRAIN_ASSET_INVALID_HEADER
                       : PORTABLE_TERRAIN_ASSET_DESTINATION_TOO_SMALL;
        memset(cells,0,destination_stride*block_count);
        out->block_count = (uint8_t)block_count;
        out->decoded_buffer_size = destination_stride * block_count;
        for (index = 0; index < block_count; ++index)
        {
            const uint16_t relative_offset =
                read_le16(asset->data + table_offset + index * 2u);
            size_t decoded_size;
            int status;

            if (relative_offset == 0)
                continue;
            if ((size_t)relative_offset > asset->size - table_offset)
                return PORTABLE_TERRAIN_ASSET_INVALID_HEADER;
            status = decode_lz77(
                asset->data + table_offset + relative_offset,
                asset->size - table_offset - relative_offset,
                cells + index * destination_stride,
                destination_stride, &decoded_size);
            if (status != PORTABLE_TERRAIN_ASSET_OK)
                return status;
            if (decoded_size > destination_stride)
                return PORTABLE_TERRAIN_ASSET_INVALID_STREAM;
        }
    }
    else
    {
        size_t decoded_size;
        int status = decode_lz77(asset->data + table_offset,
                                 asset->size - table_offset, cells, capacity,
                                 &decoded_size);

        if (status != PORTABLE_TERRAIN_ASSET_OK)
            return status;
        if (decoded_size < view_size)
            return PORTABLE_TERRAIN_ASSET_INVALID_STREAM;
        out->block_count = 1;
        out->decoded_buffer_size = decoded_size;
    }
    return PORTABLE_TERRAIN_ASSET_OK;
}
