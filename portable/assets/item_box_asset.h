#ifndef PORTABLE_ITEM_BOX_ASSET_H
#define PORTABLE_ITEM_BOX_ASSET_H

#include "asset_pack.h"
#include <stddef.h>
#include <stdint.h>

#define PORTABLE_ITEM_BOX_FRAME_COUNT 8u
#define PORTABLE_ITEM_BOX_FRAME_BYTES 0x200u
#define PORTABLE_ITEM_BOX_TILE_BYTES \
    (PORTABLE_ITEM_BOX_FRAME_COUNT * PORTABLE_ITEM_BOX_FRAME_BYTES)
#define PORTABLE_ITEM_BOX_PALETTE_COUNT 16u

typedef struct
{
    const uint8_t *tiles;
    size_t tile_bytes;
    const uint16_t *palette;
    size_t palette_count;
    uint32_t frame_count;
    uint32_t width;
    uint32_t height;
} PortableItemBoxAssetView;

enum {
    PORTABLE_ITEM_BOX_RESOURCE_BYTES = 3354u,
    PORTABLE_ITEM_BOX_CELL_COUNT = 7u,
    PORTABLE_ITEM_BOX_SEQUENCE_COUNT = 7u,
    PORTABLE_ITEM_BOX_SHARD_GROUP_COUNT = 3u,
    PORTABLE_ITEM_BOX_SHARD_FRAME_BYTES = 128u
};

typedef struct {
    const uint8_t *cells[PORTABLE_ITEM_BOX_CELL_COUNT];
    const uint8_t *sequence;
    const uint8_t *shard_tiles;
    uint32_t cell_ids[PORTABLE_ITEM_BOX_CELL_COUNT];
} PortableItemBoxResourcesView;

/* Read IBP1 from normalized pack bytes. Source addresses are immutable data
 * identifiers, never host pointers or callable addresses. Failure leaves out
 * unchanged; caller retains ownership of the byte storage. */
int portable_item_box_resources_read(const uint8_t *bytes, size_t size,
                                    PortableItemBoxResourcesView *out);
const uint8_t *portable_item_box_resource_cell(
    const PortableItemBoxResourcesView *resources, uint32_t source_id);

/* Decode normalized IBX1 into bounded caller storage. Returns 1 on success;
 * failed decoding preserves out, but scratch buffers may change. */
int portable_item_box_asset_open(const PortableAssetSectionView *,uint8_t *tiles,size_t tile_capacity,
    uint16_t *palette,size_t palette_capacity,PortableItemBoxAssetView *out);

#endif
