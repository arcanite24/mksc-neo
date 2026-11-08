#ifndef PORTABLE_RECOVERY_FX_ASSET_H
#define PORTABLE_RECOVERY_FX_ASSET_H
#include "asset_pack.h"
#include "recovery_fx.h"
int portable_recovery_fx_asset_open(const PortableAssetSectionView *section,PortableRecoveryFxRules *out);
int portable_recovery_fx_asset_load(const PortableAssetPackView *pack,
    PortableRecoveryFxRules *storage,const PortableRecoveryFxRules **out);
/* The existing surface theme is optional in older content. */
int portable_recovery_fx_ice_gate(const PortableAssetPackView *pack,uint32_t course,uint8_t *out);
#endif
