#ifndef PORTABLE_DRIVER_PROJECTION_ASSET_H
#define PORTABLE_DRIVER_PROJECTION_ASSET_H

#include <stdint.h>
#include "asset_pack.h"

#define PORTABLE_DRIVER_PROJECTION_LODS 3u

typedef struct
{
    uint16_t max_distance;
    uint16_t size;
    int16_t offset_x;
    int16_t offset_y;
    uint8_t affine_double_size;
} PortableDriverProjectionRow;

typedef struct
{
    PortableDriverProjectionRow rows[PORTABLE_DRIVER_PROJECTION_LODS];
} PortableDriverProjectionRules;

/* DPR1 source cell geometry, shared by recovered 2D and authored 3D art owners. */
int portable_driver_projection_asset_open(const PortableAssetSectionView *,
                                          PortableDriverProjectionRules *);

#endif
