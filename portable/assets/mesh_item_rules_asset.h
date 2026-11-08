#ifndef PORTABLE_MESH_ITEM_RULES_ASSET_H
#define PORTABLE_MESH_ITEM_RULES_ASSET_H
#include "asset_pack.h"
#include "mesh_item_rules.h"
/* Exact bounded payload/identity validation; failure preserves out. */
int portable_mesh_item_rules_asset_open(const PortableAssetSectionView *, PortableMeshItemRules *);
int portable_mesh_item_rules_asset_load(const PortableAssetPackView *, uint32_t,
                                        PortableMeshItemRules *);
#endif
