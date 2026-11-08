#ifndef PORTABLE_GP_FINISH_BANNER_ASSET_H
#define PORTABLE_GP_FINISH_BANNER_ASSET_H

#include <stddef.h>
#include <stdint.h>

#define PORTABLE_GP_FINISH_BANNER_TILE_BYTES 0x800u
#define PORTABLE_GP_FINISH_BANNER_PALETTE_COUNT 48u
#define PORTABLE_GP_FINISH_BANNER_CELL_BYTES 16u
#define PORTABLE_GP_FINISH_BANNER_VARIANT_COUNT 2u

typedef struct
{
    const uint8_t *tiles;
    size_t tile_bytes;
    const uint16_t *palette;
    size_t palette_count;
    const uint8_t *cells;
    size_t cell_bytes;
    uint16_t tile_base;
    uint16_t origin_x;
    uint16_t origin_y;
} PortableGpFinishBannerAssetView;

#endif
