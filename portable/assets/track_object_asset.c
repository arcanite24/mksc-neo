#include "track_object_asset.h"

#include <string.h>

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static PortableTrackObjectAssetStatus check_record(
    const PortableTrackObjectStream *stream,
    uint32_t index,
    const uint8_t **record)
{
    size_t offset;

    if (stream == NULL || record == NULL)
        return PORTABLE_TRACK_OBJECT_ASSET_INVALID_ARGUMENT;
    if (index >= stream->record_count)
        return PORTABLE_TRACK_OBJECT_ASSET_INDEX_OUT_OF_BOUNDS;
    offset = (size_t)stream->stream_offset +
             (size_t)index * PORTABLE_TRACK_STREAM_RECORD_SIZE;
    if (offset > stream->size ||
        stream->size - offset < PORTABLE_TRACK_STREAM_RECORD_SIZE)
        return PORTABLE_TRACK_OBJECT_ASSET_TRUNCATED_STREAM;
    *record = stream->data + offset;
    return PORTABLE_TRACK_OBJECT_ASSET_OK;
}

PortableTrackObjectAssetStatus portable_track_object_stream_open(
    const PortableTrackAsset *asset,
    uint32_t field_offset,
    PortableTrackObjectStream *out)
{
    uint32_t stream_offset;
    size_t cursor;

    if (asset == NULL || out == NULL || asset->data == NULL)
        return PORTABLE_TRACK_OBJECT_ASSET_INVALID_ARGUMENT;
    if (field_offset > asset->size || asset->size - field_offset < 4u)
        return PORTABLE_TRACK_OBJECT_ASSET_INVALID_FIELD;

    stream_offset = read_le32(asset->data + field_offset);
    memset(out, 0, sizeof(*out));
    out->data = asset->data;
    out->size = asset->size;
    out->field_offset = field_offset;
    out->stream_offset = stream_offset;
    out->terminator_offset = stream_offset;

    if (stream_offset == 0)
        return PORTABLE_TRACK_OBJECT_ASSET_OK;
    if ((size_t)stream_offset > asset->size ||
        asset->size - (size_t)stream_offset <
            PORTABLE_TRACK_STREAM_RECORD_SIZE)
        return PORTABLE_TRACK_OBJECT_ASSET_INVALID_OFFSET;

    cursor = stream_offset;
    while (cursor + PORTABLE_TRACK_STREAM_RECORD_SIZE <= asset->size)
    {
        if (asset->data[cursor] == 0)
        {
            out->terminator_offset = (uint32_t)cursor;
            out->record_count =
                (uint32_t)((cursor - (size_t)stream_offset) /
                           PORTABLE_TRACK_STREAM_RECORD_SIZE);
            return PORTABLE_TRACK_OBJECT_ASSET_OK;
        }
        cursor += PORTABLE_TRACK_STREAM_RECORD_SIZE;
    }
    return PORTABLE_TRACK_OBJECT_ASSET_TRUNCATED_STREAM;
}

PortableTrackObjectAssetStatus portable_track_object_stream_read(
    const PortableTrackObjectStream *stream,
    uint32_t index,
    uint8_t record[PORTABLE_TRACK_STREAM_RECORD_SIZE])
{
    const uint8_t *source;
    PortableTrackObjectAssetStatus status =
        check_record(stream, index, &source);

    if (status != PORTABLE_TRACK_OBJECT_ASSET_OK || record == NULL)
        return status != PORTABLE_TRACK_OBJECT_ASSET_OK
                   ? status
                   : PORTABLE_TRACK_OBJECT_ASSET_INVALID_ARGUMENT;
    memcpy(record, source, PORTABLE_TRACK_STREAM_RECORD_SIZE);
    return PORTABLE_TRACK_OBJECT_ASSET_OK;
}

PortableTrackObjectAssetStatus portable_track_raw_object_read(
    const PortableTrackObjectStream *stream,
    uint32_t index,
    PortableTrackRawObject *out)
{
    const uint8_t *record;
    PortableTrackObjectAssetStatus status = check_record(stream, index, &record);

    if (status != PORTABLE_TRACK_OBJECT_ASSET_OK || out == NULL)
        return status != PORTABLE_TRACK_OBJECT_ASSET_OK
                   ? status
                   : PORTABLE_TRACK_OBJECT_ASSET_INVALID_ARGUMENT;
    out->stream_offset = stream->stream_offset +
                         index * PORTABLE_TRACK_STREAM_RECORD_SIZE;
    out->raw_type = record[0];
    out->x = record[1];
    out->y = record[2];
    out->parameter = record[3];
    /* loadtrack_loadObjects adds (0x80 << 0xb) after the Q19 shift. */
    out->source_x_q19 = ((uint32_t)record[1] << 19) + UINT32_C(0x40000);
    out->source_y_q19 = ((uint32_t)record[2] << 19) + UINT32_C(0x40000);
    return PORTABLE_TRACK_OBJECT_ASSET_OK;
}

PortableTrackObjectAssetStatus portable_track_coin_read(
    const PortableTrackObjectStream *stream,
    uint32_t index,
    PortableTrackCoinPlacement *out)
{
    const uint8_t *record;
    PortableTrackObjectAssetStatus status = check_record(stream, index, &record);

    if (status != PORTABLE_TRACK_OBJECT_ASSET_OK || out == NULL)
        return status != PORTABLE_TRACK_OBJECT_ASSET_OK
                   ? status
                   : PORTABLE_TRACK_OBJECT_ASSET_INVALID_ARGUMENT;
    out->stream_offset = stream->stream_offset +
                         index * PORTABLE_TRACK_STREAM_RECORD_SIZE;
    out->marker = record[0];
    out->x = record[1];
    out->y = record[2];
    out->parameter = record[3];
    out->world_x = (int32_t)record[1] << 3;
    out->world_y = (int32_t)record[2] << 3;
    out->source_flags = PORTABLE_TRACK_SOURCE_COIN_FLAGS;
    return PORTABLE_TRACK_OBJECT_ASSET_OK;
}

PortableTrackObjectAssetStatus portable_track_boundary_read(
    const PortableTrackObjectStream *stream,
    uint32_t index,
    PortableTrackBoundaryCommand *out)
{
    const uint8_t *record;
    PortableTrackObjectAssetStatus status = check_record(stream, index, &record);

    if (status != PORTABLE_TRACK_OBJECT_ASSET_OK || out == NULL)
        return status != PORTABLE_TRACK_OBJECT_ASSET_OK
                   ? status
                   : PORTABLE_TRACK_OBJECT_ASSET_INVALID_ARGUMENT;
    out->stream_offset = stream->stream_offset +
                         index * PORTABLE_TRACK_STREAM_RECORD_SIZE;
    out->opcode = record[0];
    out->x = record[1];
    out->y = record[2];
    out->parameter = record[3];
    return PORTABLE_TRACK_OBJECT_ASSET_OK;
}

PortableTrackObjectAssetStatus portable_track_boundary_width(
    const PortableTrackObjectStream *stream,
    uint32_t *out_width)
{
    uint32_t index;

    if (stream == NULL || out_width == NULL)
        return PORTABLE_TRACK_OBJECT_ASSET_INVALID_ARGUMENT;
    *out_width = 0;
    for (index = 0; index < stream->record_count; ++index)
    {
        PortableTrackBoundaryCommand command;

        if (portable_track_boundary_read(stream, index, &command) !=
            PORTABLE_TRACK_OBJECT_ASSET_OK)
            return PORTABLE_TRACK_OBJECT_ASSET_TRUNCATED_STREAM;
        if (command.opcode == 0x8b)
        {
            *out_width = ((uint32_t)command.y << 3) + 8u;
            break;
        }
    }
    return PORTABLE_TRACK_OBJECT_ASSET_OK;
}
