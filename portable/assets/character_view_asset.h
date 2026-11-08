#ifndef PORTABLE_CHARACTER_VIEW_ASSET_H
#define PORTABLE_CHARACTER_VIEW_ASSET_H

#include "asset_pack.h"
#include "character_view.h"

int portable_character_view_asset_open(const PortableAssetSectionView *section,
    PortableCharacterViewRules *rules);
/* Missing optional rules succeed with a NULL output for old packs. */
int portable_character_view_asset_load(const PortableAssetPackView *pack,
    PortableCharacterViewRules *storage,const PortableCharacterViewRules **out);

#endif
