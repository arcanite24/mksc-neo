#include "track_asset_select.h"
PortableTrackAssetStatus portable_track_asset_select(
    const uint32_t *course_indices, size_t header_count, uint32_t header_index,
    const uint8_t *container, size_t container_size,
    uint8_t *owned_storage, size_t owned_capacity, PortableTrackAsset *out)
{
    if (!course_indices || !header_count || header_count > 256u ||
        header_index >= header_count ||
        course_indices[header_index] >= PORTABLE_TRACK_DIRECTORY_ENTRIES)
        return PORTABLE_TRACK_ASSET_INVALID_ARGUMENT;
    return portable_track_asset_import(container, container_size, header_index,
        course_indices[header_index], owned_storage, owned_capacity, out);
}
