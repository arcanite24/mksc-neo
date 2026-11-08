#ifndef PORTABLE_FINISH_ASSET_H
#define PORTABLE_FINISH_ASSET_H

#include "asset_pack.h"
#include "finish_camera.h"

typedef struct
{
    PortableFinishRules rules;
    PortableFinishRow rows[8][32];
} PortableFinishAssetStorage;

int portable_finish_asset_open(const PortableAssetSectionView *section,
    PortableFinishAssetStorage *storage);
/* Optional global rules: old packs succeed with NULL. */
int portable_finish_asset_load(const PortableAssetPackView *pack,
    PortableFinishAssetStorage *storage,const PortableFinishRules **out);

#endif
