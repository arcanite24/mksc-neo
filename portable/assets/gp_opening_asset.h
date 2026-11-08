#ifndef PORTABLE_GP_OPENING_ASSET_H
#define PORTABLE_GP_OPENING_ASSET_H
#include "asset_pack.h"

enum { PORTABLE_GP_OPENING_TILE_BYTES=0xa00,
       PORTABLE_GP_OPENING_MAP_BYTES=0x800,
       PORTABLE_GP_OPENING_CLASS_BYTES=192,
       PORTABLE_GP_OPENING_CLASS_COUNT=3,
       PORTABLE_GP_OPENING_ASSET_BYTES=5224 };
typedef struct {
    const uint8_t *tiles,*tilemap,*palette,*class_tiles;
    uint8_t class_count;
} PortableGpOpeningAssetView;

/* GOT1 v27: course-owned art, three class glyph banks, no absolute timeline. */
int portable_gp_opening_asset_open(const PortableAssetSectionView *section,
    PortableGpOpeningAssetView *out);
#endif
