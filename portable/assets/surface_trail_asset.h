#ifndef PORTABLE_SURFACE_TRAIL_ASSET_H
#define PORTABLE_SURFACE_TRAIL_ASSET_H
#include "asset_pack.h"
#include "surface_trail.h"
int portable_surface_trail_asset_open(const PortableAssetSectionView *section,PortableSurfaceTrailRules *out);
int portable_surface_trail_asset_load(const PortableAssetPackView *pack,
    PortableSurfaceTrailRules *storage,const PortableSurfaceTrailRules **out);
#endif
