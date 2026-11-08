#ifndef PORTABLE_MKDS_ITEM_ART_H
#define PORTABLE_MKDS_ITEM_ART_H

#include "nds_fs.h"

#define MKDS_ITEM_MESH_ID(slot) (0x4D490000u | (slot))
#define MKDS_ITEM_MATERIAL_ID(slot, material) (0x4D4A0000u | (slot) << 4 | (material))

/* Converts the DS item, item box, shard, explosion and ink models once and
 * appends one kind-98 BND1 row (PORTABLE_MODEL_ITEM) per model and item
 * rules profile to bindings; returns the row count. */
uint32_t mkds_item_art(RiCtx *ctx, const NdsFs *rom, const uint32_t *profiles, size_t profile_count,
                       RiBuf *bindings);

#endif
