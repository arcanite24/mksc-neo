#ifndef PORTABLE_IMPACT_EFFECT_ASSET_H
#define PORTABLE_IMPACT_EFFECT_ASSET_H
#include "asset_pack.h"
#include "impact_effect.h"
int portable_impact_effect_asset_open(const PortableAssetSectionView *section,PortableImpactEffectRules *out);
int portable_impact_effect_asset_load(const PortableAssetPackView *pack,
    PortableImpactEffectRules *storage,const PortableImpactEffectRules **out);
#endif
