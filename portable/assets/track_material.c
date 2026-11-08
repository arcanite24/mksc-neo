#include "track_material.h"

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

PortableTrackMaterialStatus portable_track_material_init(
    const PortableTrackAsset *asset,
    PortableTrackMaterialView *out)
{
    uint32_t source_offset;

    if (asset == NULL || asset->data == NULL || out == NULL)
        return PORTABLE_TRACK_MATERIAL_INVALID_ARGUMENT;
    if (asset->size < 0x8cu)
        return PORTABLE_TRACK_MATERIAL_INVALID_HEADER;

    source_offset = read_le32(asset->data + 0x88);
    if (source_offset > asset->size ||
        asset->size - source_offset < PORTABLE_TRACK_MATERIAL_ENTRY_COUNT)
        return PORTABLE_TRACK_MATERIAL_INVALID_HEADER;

    out->flags = asset->data + source_offset;
    out->count = PORTABLE_TRACK_MATERIAL_ENTRY_COUNT;
    out->source_offset = source_offset;
    out->protected_hop_thresholds = NULL;
    out->protected_hop_threshold_count = 0u;
    return PORTABLE_TRACK_MATERIAL_OK;
}

int portable_track_material_query(
    const PortableTrackMaterialView *view,
    uint8_t terrain_index,
    uint8_t *out_flags)
{
    if (view == NULL || view->flags == NULL || out_flags == NULL)
        return 1;
    if ((size_t)terrain_index >= view->count)
        return 1;
    *out_flags = view->flags[terrain_index];
    return 0;
}
