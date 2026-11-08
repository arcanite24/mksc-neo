#ifndef PORTABLE_ITEM_BOX_RENDERER_H
#define PORTABLE_ITEM_BOX_RENDERER_H

#include <stdint.h>

#include "item_box_asset.h"
#include "software_frame.h"

int portable_item_box_render(const PortableItemBoxAssetView *asset,
                             PortableSoftwareFrame *frame,
                             int32_t origin_x, int32_t origin_y,
                             uint32_t animation_tick);

/* Source 43860/49B0C inverse scale: projected depth multiplied by 182/256. */
int portable_item_box_render_projected(const PortableItemBoxAssetView *asset,
    PortableSoftwareFrame *frame, int32_t x, int32_t y, uint32_t tick,
    uint16_t depth);

/* Source4389C colors for a separately owned callback phase. Caller derives
 * the displayed phase from the art clock, independently of the scene clock. */
int portable_item_box_animated_palette(const PortableItemBoxAssetView *asset,
    uint32_t phase,uint16_t palette[16]);

#endif
