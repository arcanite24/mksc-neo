#include "track_raster.h"

#include <limits.h>
#include <string.h>

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static int write_cell(PortableTrackRaster *raster,
                      int64_t x,
                      int64_t y,
                      uint8_t value)
{
    if (x < 0 || y < 0 || (uint64_t)x >= raster->width ||
        (uint64_t)y >= raster->height)
        return 1;
    raster->cells[(size_t)y * raster->width + (size_t)x] = value;
    return 1;
}

static int write_command(PortableTrackRaster *raster,
                         const uint8_t *entry,
                         uint8_t value)
{
    const uint8_t type = entry[0];
    const int64_t x = read_le16(entry + 1);
    const int64_t y = read_le16(entry + 3);
    const size_t extent = (size_t)read_le16(entry + 5) + 1u;

    if (type > 4)
        return 0;

    if (type == 0)
    {
        const size_t height = (size_t)read_le16(entry + 7) + 1u;

        for (size_t row = 0; row < height; ++row)
        {
            for (size_t column = 0; column < extent; ++column)
                write_cell(raster, x + (int64_t)column,
                           y + (int64_t)row, value);
        }
        return 1;
    }

    for (size_t row = 0; row < extent; ++row)
    {
        const size_t row_width = extent - row;
        int64_t row_y = y;
        int direction = 1;

        if (type >= 3)
            row_y -= (int64_t)row;
        else
            row_y += (int64_t)row;
        if (type == 2 || type == 3)
            direction = -1;

        for (size_t column = 0; column < row_width; ++column)
        {
            const int64_t column_x =
                x + (int64_t)direction * (int64_t)column;
            write_cell(raster, column_x, row_y, value);
        }
    }
    return 1;
}

static int32_t arithmetic_shift4(int64_t value)
{
    if (value >= 0)
        return (int32_t)(value / 16);
    return (int32_t)(-((-value + 15) / 16));
}

int portable_track_raster_build(const PortableTrackAsset *asset,
                                uint8_t *cells,
                                size_t capacity,
                                PortableTrackRaster *out)
{
    uint32_t command_offset;
    const uint8_t *commands;
    uint8_t command_count;
    uint16_t entry_offset;
    size_t width;
    size_t cell_count;

    if (asset == NULL || asset->data == NULL || out == NULL ||
        cells == NULL || asset->size < 0xD0)
        return PORTABLE_TRACK_RASTER_INVALID_ARGUMENT;

    command_offset = read_le32(asset->data + 0xCC);
    if (command_offset > asset->size || asset->size - command_offset < 3)
        return PORTABLE_TRACK_RASTER_INVALID_SECTION;
    commands = asset->data + command_offset;
    command_count = commands[0];
    entry_offset = read_le16(commands + 1);
    if ((size_t)entry_offset > asset->size - command_offset - 3u ||
        (size_t)command_count >
            (asset->size - command_offset - 3u - entry_offset) / 12u)
        return PORTABLE_TRACK_RASTER_INVALID_SECTION;

    width = (size_t)asset->data[4] * 64u;
    if (width == 0 || width > SIZE_MAX / width)
        return PORTABLE_TRACK_RASTER_INVALID_SECTION;
    cell_count = width * width;
    if (capacity < cell_count)
        return PORTABLE_TRACK_RASTER_DESTINATION_TOO_SMALL;

    out->cells = cells;
    out->capacity = capacity;
    out->width = width;
    out->height = width;
    out->command_count = command_count;
    out->entry_offset = entry_offset;
    memset(cells, 0x7f, cell_count);

    for (uint32_t index = 0; index < command_count; ++index)
    {
        const uint8_t *entry = commands + entry_offset + index * 12u;
        const uint8_t value =
            (uint8_t)((index == 0 || index + 1u == command_count)
                          ? index - 0x80u
                          : index);

        if (!write_command(out, entry, value))
            return PORTABLE_TRACK_RASTER_INVALID_COMMAND;
    }
    return PORTABLE_TRACK_RASTER_OK;
}

int portable_track_raster_query16(const PortableTrackRaster *raster,
                                  int32_t x,
                                  int32_t y,
                                  uint8_t *out_value)
{
    const int32_t cell_x = arithmetic_shift4((int64_t)x + (x < 0 ? 15 : 0));
    const int32_t cell_y = arithmetic_shift4((int64_t)y + (y < 0 ? 15 : 0));

    if (raster == NULL || raster->cells == NULL || out_value == NULL)
        return PORTABLE_TRACK_RASTER_INVALID_ARGUMENT;
    if (cell_x < 0 || cell_y < 0 || (size_t)cell_x >= raster->width ||
        (size_t)cell_y >= raster->height)
        return PORTABLE_TRACK_RASTER_OUT_OF_BOUNDS;
    *out_value = raster->cells[(size_t)cell_y * raster->width +
                               (size_t)cell_x];
    return PORTABLE_TRACK_RASTER_OK;
}
