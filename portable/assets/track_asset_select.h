#ifndef PORTABLE_TRACK_ASSET_SELECT_H
#define PORTABLE_TRACK_ASSET_SELECT_H
#include "track_asset.h"
/* Imported first-word course IDs, indexed by original header ID. Unsupported
 * headers use UINT32_MAX. No original pointers or addresses are stored here.
 * Imported immutable inputs must be disjoint from owned output storage. */
PortableTrackAssetStatus portable_track_asset_select(
    const uint32_t *course_indices, size_t header_count, uint32_t header_index,
    const uint8_t *container, size_t container_size,
    uint8_t *owned_storage, size_t owned_capacity, PortableTrackAsset *out);
#endif
