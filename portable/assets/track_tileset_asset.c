#include "track_tileset_asset.h"

#include <string.h>

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int span_is_valid(size_t data_size, size_t offset, size_t length)
{
    return offset <= data_size && length <= data_size - offset;
}

static PortableTrackTilesetStatus decode_lz77(
    const uint8_t *data,
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
        return PORTABLE_TRACK_TILESET_INVALID_ARGUMENT;
    if (data[source_offset] != 0x10)
        return PORTABLE_TRACK_TILESET_FORMAT;
    expected = (uint32_t)data[source_offset + 1] |
               ((uint32_t)data[source_offset + 2] << 8) |
               ((uint32_t)data[source_offset + 3] << 16);
    if (expected == 0)
        return PORTABLE_TRACK_TILESET_FORMAT;
    if (expected > destination_capacity)
        return PORTABLE_TRACK_TILESET_CAPACITY;

    source = source_offset + 4;
    while (produced < expected)
    {
        uint8_t flags;
        unsigned bit;

        if (!span_is_valid(data_size, source, 1))
            return PORTABLE_TRACK_TILESET_OUT_OF_BOUNDS;
        flags = data[source++];
        for (bit = 0; bit < 8 && produced < expected; ++bit)
        {
            if ((flags & (uint8_t)(0x80u >> bit)) == 0)
            {
                if (!span_is_valid(data_size, source, 1))
                    return PORTABLE_TRACK_TILESET_OUT_OF_BOUNDS;
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
                    return PORTABLE_TRACK_TILESET_OUT_OF_BOUNDS;
                first = data[source++];
                second = data[source++];
                distance = (((size_t)first & 0x0fu) << 8) |
                           (size_t)second;
                distance += 1u;
                length = ((size_t)first >> 4) + 3u;
                if (distance > produced || length > expected - produced)
                    return PORTABLE_TRACK_TILESET_FORMAT;
                for (copy = 0; copy < length; ++copy)
                {
                    destination[produced] = destination[produced - distance];
                    ++produced;
                }
            }
        }
    }
    *out_size = produced;
    return PORTABLE_TRACK_TILESET_OK;
}

PortableTrackTilesetStatus portable_track_tileset_asset_build(
    const PortableTrackAsset *asset,
    const PortableTrackTilesetBuffers *buffers,
    PortableTrackTilesetView *out_view)
{
    size_t table_offset;
    size_t palette_offset;
    size_t block;

    if (asset == NULL || buffers == NULL || out_view == NULL ||
        asset->data == NULL)
        return PORTABLE_TRACK_TILESET_INVALID_ARGUMENT;
    if (asset->size < 0x88)
        return PORTABLE_TRACK_TILESET_OUT_OF_BOUNDS;
    if (buffers->tiles == NULL || buffers->palette == NULL ||
        buffers->tile_capacity < PORTABLE_TRACK_TILESET_TILE_BYTES ||
        buffers->palette_capacity < PORTABLE_TRACK_TILESET_PALETTE_COUNT)
        return PORTABLE_TRACK_TILESET_CAPACITY;

    if ((asset->data[3] & 1u) == 0)
        return PORTABLE_TRACK_TILESET_UNSUPPORTED;
    table_offset = (size_t)read_le32(asset->data + 0x80);
    palette_offset = (size_t)read_le32(asset->data + 0x84);
    if (!span_is_valid(asset->size, table_offset,
                       PORTABLE_TRACK_TILESET_BLOCK_COUNT * sizeof(uint16_t)) ||
        !span_is_valid(asset->size, palette_offset,
                       PORTABLE_TRACK_TILESET_PALETTE_COUNT * sizeof(uint16_t)))
        return PORTABLE_TRACK_TILESET_OUT_OF_BOUNDS;

    for (block = 0; block < PORTABLE_TRACK_TILESET_BLOCK_COUNT; ++block)
    {
        size_t relative = (size_t)read_le16(
            asset->data + table_offset + block * sizeof(uint16_t));
        size_t source_offset;
        size_t decoded_size = 0;
        PortableTrackTilesetStatus status;

        if (relative > asset->size - table_offset)
            return PORTABLE_TRACK_TILESET_OUT_OF_BOUNDS;
        source_offset = table_offset + relative;
        status = decode_lz77(
            asset->data, asset->size, source_offset,
            buffers->tiles + block * PORTABLE_TRACK_TILESET_BLOCK_BYTES,
            PORTABLE_TRACK_TILESET_BLOCK_BYTES, &decoded_size);
        if (status != PORTABLE_TRACK_TILESET_OK)
            return status;
        if (decoded_size != PORTABLE_TRACK_TILESET_BLOCK_BYTES)
            return PORTABLE_TRACK_TILESET_FORMAT;
    }

    for (block = 0; block < PORTABLE_TRACK_TILESET_PALETTE_COUNT; ++block)
        buffers->palette[block] = read_le16(
            asset->data + palette_offset + block * sizeof(uint16_t));

    memset(out_view, 0, sizeof(*out_view));
    out_view->tiles = buffers->tiles;
    out_view->tile_bytes = PORTABLE_TRACK_TILESET_TILE_BYTES;
    out_view->palette = buffers->palette;
    out_view->palette_count = PORTABLE_TRACK_TILESET_PALETTE_COUNT;
    out_view->table_offset = (uint32_t)table_offset;
    out_view->palette_offset = (uint32_t)palette_offset;
    out_view->flags = asset->data[3];
    out_view->block_count = PORTABLE_TRACK_TILESET_BLOCK_COUNT;
    return PORTABLE_TRACK_TILESET_OK;
}

PortableTrackTilesetStatus portable_track_tileset_asset_load_common(
    const uint8_t *rom,
    size_t rom_size,
    uint32_t source_offset,
    uint8_t *tiles,
    size_t tile_capacity)
{
    size_t decoded_size = 0;
    PortableTrackTilesetStatus status;

    if (rom == NULL || tiles == NULL)
        return PORTABLE_TRACK_TILESET_INVALID_ARGUMENT;
    if (tile_capacity < PORTABLE_TRACK_TILESET_TILE_BYTES)
        return PORTABLE_TRACK_TILESET_CAPACITY;

    status = decode_lz77(
        rom, rom_size, (size_t)source_offset,
        tiles + PORTABLE_TRACK_TILESET_COMMON_SOURCE_TILE * 64u,
        PORTABLE_TRACK_TILESET_COMMON_SOURCE_BYTES, &decoded_size);
    if (status != PORTABLE_TRACK_TILESET_OK)
        return status;
    if (decoded_size != PORTABLE_TRACK_TILESET_COMMON_SOURCE_BYTES)
        return PORTABLE_TRACK_TILESET_FORMAT;
    return PORTABLE_TRACK_TILESET_OK;
}
