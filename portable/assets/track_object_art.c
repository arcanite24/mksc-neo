#include "track_object_art.h"

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

static int span_is_valid(size_t size, size_t offset, size_t length)
{
    return offset <= size && length <= size - offset;
}

static PortableTrackObjectArtStatus decode_lz77(
    const uint8_t *rom,
    size_t rom_size,
    uint32_t source_offset,
    uint8_t *destination,
    size_t destination_capacity,
    uint32_t *out_size)
{
    size_t source;
    size_t produced = 0;
    uint32_t expected;

    if (!span_is_valid(rom_size, source_offset, 4u) ||
        destination == NULL || out_size == NULL)
        return PORTABLE_TRACK_OBJECT_ART_OUT_OF_BOUNDS;
    if (rom[source_offset] != 0x10)
        return PORTABLE_TRACK_OBJECT_ART_FORMAT;
    expected = read_le24(rom + source_offset + 1u);
    if (expected == 0 || expected > destination_capacity)
        return expected == 0 ? PORTABLE_TRACK_OBJECT_ART_FORMAT
                             : PORTABLE_TRACK_OBJECT_ART_CAPACITY;

    source = (size_t)source_offset + 4u;
    while (produced < expected)
    {
        uint8_t flags;
        unsigned bit;

        if (!span_is_valid(rom_size, source, 1u))
            return PORTABLE_TRACK_OBJECT_ART_OUT_OF_BOUNDS;
        flags = rom[source++];
        for (bit = 0; bit < 8u && produced < expected; ++bit)
        {
            if ((flags & (uint8_t)(0x80u >> bit)) == 0)
            {
                if (!span_is_valid(rom_size, source, 1u))
                    return PORTABLE_TRACK_OBJECT_ART_OUT_OF_BOUNDS;
                destination[produced++] = rom[source++];
            }
            else
            {
                uint8_t first;
                uint8_t second;
                size_t distance;
                size_t length;
                size_t copy;

                if (!span_is_valid(rom_size, source, 2u))
                    return PORTABLE_TRACK_OBJECT_ART_OUT_OF_BOUNDS;
                first = rom[source++];
                second = rom[source++];
                distance = (((size_t)first & 0x0fu) << 8) |
                           (size_t)second;
                distance += 1u;
                length = ((size_t)first >> 4) + 3u;
                if (distance > produced || length > expected - produced)
                    return PORTABLE_TRACK_OBJECT_ART_FORMAT;
                for (copy = 0; copy < length; ++copy)
                {
                    destination[produced] = destination[produced - distance];
                    ++produced;
                }
            }
        }
    }
    *out_size = expected;
    return PORTABLE_TRACK_OBJECT_ART_OK;
}

static PortableTrackObjectArtStatus pointer_to_offset(
    size_t rom_size,
    uint32_t pointer,
    uint32_t *out_offset)
{
    uint32_t offset;

    if (pointer < UINT32_C(0x08000000) ||
        pointer >= UINT32_C(0x0a000000) || out_offset == NULL)
        return PORTABLE_TRACK_OBJECT_ART_FORMAT;
    offset = pointer - UINT32_C(0x08000000);
    if (!span_is_valid(rom_size, offset, 2u))
        return PORTABLE_TRACK_OBJECT_ART_OUT_OF_BOUNDS;
    *out_offset = offset;
    return PORTABLE_TRACK_OBJECT_ART_OK;
}

PortableTrackObjectArtStatus portable_track_object_art_build(
    const uint8_t *rom,
    size_t rom_size,
    const PortableTrackObjectArtBuffers *buffers,
    PortableTrackObjectArtView *out_view)
{
    uint32_t cell_offsets[4];
    uint32_t decoded_primary;
    uint32_t decoded_secondary;
    PortableTrackObjectArtStatus status;
    size_t level;

    if (rom == NULL || buffers == NULL || out_view == NULL)
        return PORTABLE_TRACK_OBJECT_ART_INVALID_ARGUMENT;
    if (buffers->tiles == NULL || buffers->palette == NULL ||
        buffers->tile_capacity < PORTABLE_TRACK_OBJECT_ART_TILE_BYTES ||
        buffers->palette_capacity < PORTABLE_TRACK_OBJECT_ART_PALETTE_COUNT)
        return PORTABLE_TRACK_OBJECT_ART_CAPACITY;
    if (!span_is_valid(rom_size,
                       PORTABLE_TRACK_OBJECT_ART_DESCRIPTOR_OFFSET, 32u) ||
        !span_is_valid(rom_size, PORTABLE_TRACK_OBJECT_ART_PALETTE_OFFSET,
                       PORTABLE_TRACK_OBJECT_ART_PALETTE_COUNT * 2u))
        return PORTABLE_TRACK_OBJECT_ART_OUT_OF_BOUNDS;

    for (level = 0; level < 4u; ++level)
    {
        status = pointer_to_offset(
            rom_size, read_le32(rom + PORTABLE_TRACK_OBJECT_ART_DESCRIPTOR_OFFSET +
                      level * 8u),
            &cell_offsets[level]);
        if (status != PORTABLE_TRACK_OBJECT_ART_OK)
            return status;
    }
    if (read_le16(rom + cell_offsets[0]) != 1u ||
        !span_is_valid(rom_size, cell_offsets[0], 8u))
        return PORTABLE_TRACK_OBJECT_ART_FORMAT;

    status = decode_lz77(
        rom, rom_size, PORTABLE_TRACK_OBJECT_ART_PRIMARY_STREAM_OFFSET,
        buffers->tiles, PORTABLE_TRACK_OBJECT_ART_PRIMARY_TILE_BYTES,
        &decoded_primary);
    if (status != PORTABLE_TRACK_OBJECT_ART_OK ||
        decoded_primary != PORTABLE_TRACK_OBJECT_ART_PRIMARY_TILE_BYTES)
        return status == PORTABLE_TRACK_OBJECT_ART_OK
                   ? PORTABLE_TRACK_OBJECT_ART_FORMAT
                   : status;
    status = decode_lz77(
        rom, rom_size, PORTABLE_TRACK_OBJECT_ART_SECONDARY_STREAM_OFFSET,
        buffers->tiles + PORTABLE_TRACK_OBJECT_ART_SECONDARY_TILE_BASE,
        PORTABLE_TRACK_OBJECT_ART_SECONDARY_TILE_BYTES, &decoded_secondary);
    if (status != PORTABLE_TRACK_OBJECT_ART_OK ||
        decoded_secondary != PORTABLE_TRACK_OBJECT_ART_SECONDARY_TILE_BYTES)
        return status == PORTABLE_TRACK_OBJECT_ART_OK
                   ? PORTABLE_TRACK_OBJECT_ART_FORMAT
                   : status;

    for (level = 0; level < PORTABLE_TRACK_OBJECT_ART_PALETTE_COUNT; ++level)
        buffers->palette[level] = read_le16(
            rom + PORTABLE_TRACK_OBJECT_ART_PALETTE_OFFSET + level * 2u);

    memset(out_view, 0, sizeof(*out_view));
    out_view->tiles = buffers->tiles;
    out_view->tile_bytes = PORTABLE_TRACK_OBJECT_ART_TILE_BYTES;
    out_view->palette = buffers->palette;
    out_view->palette_count = PORTABLE_TRACK_OBJECT_ART_PALETTE_COUNT;
    out_view->cell_data = rom + cell_offsets[0];
    out_view->cell_data_bytes = 8u;
    out_view->descriptor_offset =
        PORTABLE_TRACK_OBJECT_ART_DESCRIPTOR_OFFSET;
    out_view->cell_offset = cell_offsets[0];
    out_view->primary_stream_offset =
        PORTABLE_TRACK_OBJECT_ART_PRIMARY_STREAM_OFFSET;
    out_view->secondary_stream_offset =
        PORTABLE_TRACK_OBJECT_ART_SECONDARY_STREAM_OFFSET;
    out_view->primary_decoded_bytes = decoded_primary;
    out_view->secondary_decoded_bytes = decoded_secondary;
    out_view->level0_cell_offset = cell_offsets[0];
    out_view->level1_cell_offset = cell_offsets[1];
    out_view->level2_cell_offset = cell_offsets[2];
    out_view->level3_cell_offset = cell_offsets[3];
    out_view->level0_shift = rom[PORTABLE_TRACK_OBJECT_ART_DESCRIPTOR_OFFSET +
                                 4u];
    out_view->level1_shift = rom[PORTABLE_TRACK_OBJECT_ART_DESCRIPTOR_OFFSET +
                                 12u];
    out_view->level2_shift = rom[PORTABLE_TRACK_OBJECT_ART_DESCRIPTOR_OFFSET +
                                 20u];
    out_view->level3_shift = rom[PORTABLE_TRACK_OBJECT_ART_DESCRIPTOR_OFFSET +
                                 28u];
    return PORTABLE_TRACK_OBJECT_ART_OK;
}
