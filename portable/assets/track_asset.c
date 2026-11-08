#include "track_asset.h"

#include <string.h>

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

PortableTrackAssetStatus portable_track_asset_import(
    const uint8_t *container,
    size_t container_size,
    uint32_t header_index,
    uint32_t course_index,
    uint8_t *owned_storage,
    size_t owned_capacity,
    PortableTrackAsset *out)
{
    const size_t directory_size =
        (size_t)PORTABLE_TRACK_DIRECTORY_ENTRIES * sizeof(uint32_t);
    size_t entry;
    uint32_t source_offset;
    uint32_t source_end;
    size_t blob_size;

    if (container == NULL || owned_storage == NULL || out == NULL)
        return PORTABLE_TRACK_ASSET_INVALID_ARGUMENT;
    if (course_index >= PORTABLE_TRACK_DIRECTORY_ENTRIES)
        return PORTABLE_TRACK_ASSET_INVALID_ARGUMENT;
    if (container_size < directory_size)
        return PORTABLE_TRACK_ASSET_CONTAINER_TOO_SMALL;
    if (container_size > UINT32_MAX)
        return PORTABLE_TRACK_ASSET_INVALID_DIRECTORY;

    /* Validate the complete directory before exposing one selected slice. */
    for (entry = 0; entry < PORTABLE_TRACK_DIRECTORY_ENTRIES; ++entry)
    {
        const uint32_t offset = read_le32(container + entry * sizeof(uint32_t));

        if ((size_t)offset < directory_size || (size_t)offset > container_size)
            return PORTABLE_TRACK_ASSET_INVALID_DIRECTORY;
        if (entry != 0 &&
            offset < read_le32(container + (entry - 1u) * sizeof(uint32_t)))
            return PORTABLE_TRACK_ASSET_INVALID_DIRECTORY;
    }

    source_offset = read_le32(container + course_index * sizeof(uint32_t));
    if (course_index + 1u < PORTABLE_TRACK_DIRECTORY_ENTRIES)
        source_end = read_le32(container + (course_index + 1u) * sizeof(uint32_t));
    else
        source_end = (uint32_t)container_size;

    if (source_offset >= source_end || (size_t)source_end > container_size)
        return PORTABLE_TRACK_ASSET_INVALID_DIRECTORY;

    blob_size = (size_t)source_end - (size_t)source_offset;
    if (blob_size > owned_capacity)
        return PORTABLE_TRACK_ASSET_DESTINATION_TOO_SMALL;

    memcpy(owned_storage, container + source_offset, blob_size);
    out->data = owned_storage;
    out->size = blob_size;
    out->header_index = header_index;
    out->course_index = course_index;
    out->source_offset = source_offset;
    out->source_end = source_end;
    return PORTABLE_TRACK_ASSET_OK;
}
