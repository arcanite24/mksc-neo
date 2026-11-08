#ifndef PORTABLE_COIN_LOSS_ASSET_H
#define PORTABLE_COIN_LOSS_ASSET_H

#include <stddef.h>
#include <stdint.h>

enum { PORTABLE_COIN_LOSS_RESOURCE_BYTES = 386u };

/* Immutable B988/B9C4 script, cells and resident OBJ art. Serialized source
 * IDs identify data only; they are never native pointers or callbacks. */
typedef struct {
    const uint8_t *child_script, *parent_script;
    const uint8_t *cells, *tiles, *palette;
} PortableCoinLossResourcesView;

int portable_coin_loss_resources_read(const uint8_t *bytes, size_t size,
                                     PortableCoinLossResourcesView *out);
const uint8_t *portable_coin_loss_resource_cell(
    const PortableCoinLossResourcesView *resources, uint32_t source_id);

#endif
