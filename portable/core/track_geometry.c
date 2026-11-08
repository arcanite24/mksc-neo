#include "track_geometry.h"

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

PortableTrackGeometryStatus portable_track_geometry_init(
    const PortableTrackAsset *asset,
    uint16_t nr_checkpoints,
    PortableTrackGeometryView *out)
{
    size_t track_base_offset;
    const uint8_t *track_base;
    size_t remaining;

    if (asset == NULL || out == NULL || asset->data == NULL)
        return PORTABLE_TRACK_GEOMETRY_INVALID_ARGUMENT;
    if (asset->size < 0xD0)
        return PORTABLE_TRACK_GEOMETRY_INVALID_HEADER;

    track_base_offset = read_le32(asset->data + 0xCC);
    if (track_base_offset > asset->size ||
        asset->size - track_base_offset < 5)
        return PORTABLE_TRACK_GEOMETRY_INVALID_HEADER;

    track_base = asset->data + track_base_offset;
    remaining = asset->size - track_base_offset;
    {
        const uint16_t table_offset = read_le16(track_base + 3);

        if ((size_t)table_offset > remaining - 5)
            return PORTABLE_TRACK_GEOMETRY_INVALID_HEADER;

        out->track_data = asset->data;
        out->track_size = asset->size;
        out->nr_checkpoints = nr_checkpoints;
        out->track_base_offset = track_base_offset;
        out->checkpoint_table_offset = table_offset;
        out->grid_width = asset->data[4];
    }
    return PORTABLE_TRACK_GEOMETRY_OK;
}

PortableTrackGeometryStatus portable_track_geometry_read_checkpoint(
    const PortableTrackGeometryView *view,
    uint32_t index,
    uint8_t row,
    PortableTrackCheckpoint *out)
{
    size_t record_index;
    size_t record_offset;
    const uint8_t *record;

    if (view == NULL || out == NULL || view->track_data == NULL)
        return PORTABLE_TRACK_GEOMETRY_INVALID_ARGUMENT;
    record_index =
        (size_t)view->nr_checkpoints * row + (index & UINT32_C(0x7F));
    record_offset = view->track_base_offset + view->checkpoint_table_offset +
                    (record_index << 3);
    if (record_offset > view->track_size || view->track_size - record_offset < 5)
        return PORTABLE_TRACK_GEOMETRY_OUT_OF_BOUNDS;

    record = view->track_data + record_offset;
    out->x = read_le16(record);
    out->y = read_le16(record + 2);
    out->ai_mode = record[4];
    return PORTABLE_TRACK_GEOMETRY_OK;
}
