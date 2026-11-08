#ifndef PORTABLE_AMBIENT_ASSET_H
#define PORTABLE_AMBIENT_ASSET_H

#include "asset_pack.h"
#include "content_registry.h"
#include "ambient.h"

typedef struct
{
    PortableAmbientRules rules;
    PortableAmbientLane lanes[PORTABLE_AMBIENT_LANES];
    PortableAmbientFrame frames[PORTABLE_AMBIENT_FRAMES];
    PortableAmbientPatch patches[PORTABLE_AMBIENT_PATCHES];
    PortableAmbientPatch initial[32];
    PortableAmbientSeed seed;
} PortableAmbientAssetStorage;

/* Missing optional AMB1 returns success with NULL views, preserving old packs. */
int portable_ambient_asset_open(const PortableAssetSectionView *section,
    PortableAmbientAssetStorage *storage);
int portable_ambient_asset_load(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,PortableAmbientAssetStorage *storage,
    const PortableAmbientRules **out_rules,const PortableAmbientSeed **out_seed);

#endif
