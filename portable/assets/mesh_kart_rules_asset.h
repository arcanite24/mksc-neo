#ifndef PORTABLE_MESH_KART_RULES_ASSET_H
#define PORTABLE_MESH_KART_RULES_ASSET_H
#include "asset_pack.h"
#include "mesh_kart_rules.h"
/* Kind-102 KPH2 profiles. Failure leaves out unchanged; the section identity
 * must equal the profile identity. */
int portable_mesh_kart_profile_asset_open(const PortableAssetSectionView *section,
                                          PortableMeshKartProfile *out);
int portable_mesh_kart_profile_asset_load(const PortableAssetPackView *pack, uint32_t profile_id,
                                          PortableMeshKartProfile *out);
/* The profile's rules at 150cc and its base stat levels. */
int portable_mesh_kart_rules_asset_load(const PortableAssetPackView *pack, uint32_t profile_id,
                                        PortableMeshKartRules *out);
#endif
