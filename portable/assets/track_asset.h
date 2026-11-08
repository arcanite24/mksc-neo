#ifndef MKSC_PORTABLE_TRACK_ASSET_H
#define MKSC_PORTABLE_TRACK_ASSET_H

#include <stddef.h>
#include <stdint.h>

enum
{
    PORTABLE_TRACK_DIRECTORY_ENTRIES = 49u,
};

typedef enum
{
    PORTABLE_TRACK_ASSET_OK = 0,
    PORTABLE_TRACK_ASSET_INVALID_ARGUMENT,
    PORTABLE_TRACK_ASSET_CONTAINER_TOO_SMALL,
    PORTABLE_TRACK_ASSET_INVALID_DIRECTORY,
    PORTABLE_TRACK_ASSET_DESTINATION_TOO_SMALL,
} PortableTrackAssetStatus;

/*
 * The original course container stores only 32-bit little-endian offsets.
 * The view keeps the copied blob read-only and retains source provenance as
 * data instead of storing any original runtime address.
 */
typedef struct
{
    const uint8_t *data;
    size_t size;
    uint32_t header_index;
    uint32_t course_index;
    uint32_t source_offset;
    uint32_t source_end;
} PortableTrackAsset;

PortableTrackAssetStatus portable_track_asset_import(
    const uint8_t *container,
    size_t container_size,
    uint32_t header_index,
    uint32_t course_index,
    uint8_t *owned_storage,
    size_t owned_capacity,
    PortableTrackAsset *out);

#endif
