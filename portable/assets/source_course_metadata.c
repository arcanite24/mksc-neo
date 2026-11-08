#include "source_course_metadata.h"

#include "track_object_asset.h"

#include <limits.h>
#include <string.h>

#define ROM_BASE UINT32_C(0x08000000)
#define TRACK_DEF_TABLE_OFFSET UINT32_C(0x000e7fec)
#define TRACK_DEF_COUNT 56u

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

PortableSourceCourseStatus portable_source_course_selection(
    const uint8_t *rom,
    size_t rom_size,
    uint32_t header_index,
    uint32_t *out_course_id,
    uint32_t *out_background_id)
{
    uint32_t pointer;
    uint32_t definition;

    if (rom == NULL || out_course_id == NULL || out_background_id == NULL)
        return PORTABLE_SOURCE_COURSE_INVALID_ARGUMENT;
    if (header_index >= TRACK_DEF_COUNT ||
        TRACK_DEF_TABLE_OFFSET > rom_size ||
        rom_size - TRACK_DEF_TABLE_OFFSET < TRACK_DEF_COUNT * 4u)
        return PORTABLE_SOURCE_COURSE_BAD_TRACK_TABLE;

    pointer = read_le32(rom + TRACK_DEF_TABLE_OFFSET + header_index * 4u);
    if (pointer < ROM_BASE)
        return PORTABLE_SOURCE_COURSE_BAD_TRACK_DEFINITION;
    definition = pointer - ROM_BASE;
    if (definition > rom_size || rom_size - definition < 8u)
        return PORTABLE_SOURCE_COURSE_BAD_TRACK_DEFINITION;

    *out_course_id = read_le32(rom + definition);
    *out_background_id = read_le32(rom + definition + 4u);
    return PORTABLE_SOURCE_COURSE_OK;
}

PortableSourceCourseStatus portable_source_course_metadata_build(
    const uint8_t *rom,
    size_t rom_size,
    uint32_t header_index,
    const PortableTrackAsset *asset,
    PortableSourceCourseMetadata *out)
{
    uint32_t track_base;
    uint32_t start_count = 0;
    uint32_t start_x;
    uint32_t start_y;
    uint32_t finish_y;
    uint16_t checkpoint_table;
    PortableTrackObjectStream boundary;
    PortableTrackBoundaryCommand start = {0};
    PortableSourceCourseMetadata next = {0};
    PortableSourceCourseStatus selection;

    if (rom == NULL || asset == NULL || asset->data == NULL || out == NULL)
        return PORTABLE_SOURCE_COURSE_INVALID_ARGUMENT;

    next.header_index = header_index;
    selection = portable_source_course_selection(
        rom, rom_size, header_index, &next.course_id, &next.background_id);
    if (selection != PORTABLE_SOURCE_COURSE_OK)
        return selection;
    if (next.course_id != asset->course_index ||
        asset->header_index != header_index)
        return PORTABLE_SOURCE_COURSE_BAD_TRACK_DEFINITION;
    if (asset->size < 0xd0u)
        return PORTABLE_SOURCE_COURSE_BAD_TRACK_BLOCK;

    track_base = read_le32(asset->data + 0xccu);
    if (track_base > asset->size || asset->size - track_base < 5u)
        return PORTABLE_SOURCE_COURSE_BAD_TRACK_BLOCK;
    next.checkpoint_count = asset->data[track_base];
    checkpoint_table = read_le16(asset->data + track_base + 3u);
    if (next.checkpoint_count < 2u ||
        (size_t)track_base + checkpoint_table > asset->size ||
        asset->size - ((size_t)track_base + checkpoint_table) <
            (size_t)next.checkpoint_count * 8u)
        return PORTABLE_SOURCE_COURSE_BAD_TRACK_BLOCK;

    if (portable_track_object_stream_open(
            asset, PORTABLE_TRACK_STREAM_BOUNDARY, &boundary) !=
        PORTABLE_TRACK_OBJECT_ASSET_OK)
        return PORTABLE_SOURCE_COURSE_BAD_BOUNDARY_STREAM;
    for (uint32_t index = 0; index < boundary.record_count; ++index)
    {
        PortableTrackBoundaryCommand command;

        if (portable_track_boundary_read(&boundary, index, &command) !=
            PORTABLE_TRACK_OBJECT_ASSET_OK)
            return PORTABLE_SOURCE_COURSE_BAD_BOUNDARY_STREAM;
        if (command.opcode == 0x8bu)
        {
            start = command;
            ++start_count;
        }
    }
    if (start_count != 1u)
        return PORTABLE_SOURCE_COURSE_BAD_START_FINISH;

    start_x = (uint32_t)start.x * 8u + 40u;
    start_y = (uint32_t)start.y * 8u + 28u;
    finish_y = (uint32_t)start.y * 8u + 8u;
    if (start_x > (uint32_t)INT32_MAX / 65536u ||
        start_y > (uint32_t)INT32_MAX / 65536u ||
        finish_y > (uint32_t)INT32_MAX / 65536u)
        return PORTABLE_SOURCE_COURSE_BAD_START_FINISH;

    next.start_route_index = (uint16_t)(next.checkpoint_count - 1u);
    next.start_x_q16 = (int32_t)(start_x * 65536u);
    next.start_y_q16 = (int32_t)(start_y * 65536u);
    next.finish_y_q16 = (int32_t)(finish_y * 65536u);
    *out = next;
    return PORTABLE_SOURCE_COURSE_OK;
}

const char *portable_source_course_status_name(
    PortableSourceCourseStatus status)
{
    static const char *const names[] = {
        "ok", "invalid_argument", "bad_track_table",
        "bad_track_definition", "bad_track_block", "bad_boundary_stream",
        "bad_start_finish",
    };

    return (unsigned)status < sizeof names / sizeof names[0]
               ? names[status]
               : "invalid_status";
}
