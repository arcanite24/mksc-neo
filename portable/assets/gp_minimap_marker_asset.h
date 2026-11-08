#ifndef MKSC_PORTABLE_GP_MINIMAP_MARKER_ASSET_H
#define MKSC_PORTABLE_GP_MINIMAP_MARKER_ASSET_H

#include <stdint.h>

#define PORTABLE_GP_MINIMAP_MARKER_TILE_BYTES 64u
#define PORTABLE_GP_MINIMAP_MARKER_PALETTE_COUNT 32u

typedef struct
{
    const uint8_t *tiles;
    const uint16_t *palette;
    const uint16_t *rival_palette;
} PortableGpMinimapMarkerAssetView;

#endif
