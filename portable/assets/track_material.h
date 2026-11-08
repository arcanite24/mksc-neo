#ifndef MKSC_PORTABLE_TRACK_MATERIAL_H
#define MKSC_PORTABLE_TRACK_MATERIAL_H

#include <stddef.h>
#include <stdint.h>

#include "track_asset.h"

enum
{
    PORTABLE_TRACK_MATERIAL_ENTRY_COUNT = 0x100u,
};

typedef enum
{
    PORTABLE_TRACK_MATERIAL_OK = 0,
    PORTABLE_TRACK_MATERIAL_INVALID_ARGUMENT,
    PORTABLE_TRACK_MATERIAL_INVALID_HEADER,
} PortableTrackMaterialStatus;

typedef struct
{
    const uint8_t *flags;
    size_t count;
    uint32_t source_offset;
    const uint16_t *protected_hop_thresholds;
    size_t protected_hop_threshold_count;
} PortableTrackMaterialView;

/* Load sub_8032990's trackData + mapOffset lookup table. */
PortableTrackMaterialStatus portable_track_material_init(
    const PortableTrackAsset *asset,
    PortableTrackMaterialView *out);

int portable_track_material_query(
    const PortableTrackMaterialView *view,
    uint8_t terrain_index,
    uint8_t *out_flags);

#endif
