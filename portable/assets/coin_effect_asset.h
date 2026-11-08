#ifndef PORTABLE_COIN_EFFECT_ASSET_H
#define PORTABLE_COIN_EFFECT_ASSET_H
#include "asset_pack.h"
#include "coin_effect.h"
int portable_coin_effect_asset_open(const PortableAssetSectionView *section,PortableCoinEffectRules *out);
int portable_coin_effect_asset_load(const PortableAssetPackView *pack,
    PortableCoinEffectRules *storage,const PortableCoinEffectRules **out);
#endif
