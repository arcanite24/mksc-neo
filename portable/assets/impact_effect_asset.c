#include "impact_effect_asset.h"
static uint16_t u16(const uint8_t *p)
{return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8u));}
static uint32_t u32(const uint8_t *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8u)|((uint32_t)p[2]<<16u)|((uint32_t)p[3]<<24u);}
int portable_impact_effect_asset_open(const PortableAssetSectionView *section,PortableImpactEffectRules *out)
{
    if(!section || !out || !section->bytes || section->version!=1u || section->size!=20u ||
       u32(section->bytes)!=UINT32_C(0x31584649) || u32(section->bytes+4u)!=6u)return 0;
    PortableImpactEffectRules rules={0};
    for (unsigned i = 0u; i < 6u; ++i)
        rules.script_ticks[i] = u16(section->bytes + 8u + i * 2u);
    if(!portable_impact_effect_rules_valid(&rules))return 0;
    *out=rules;return 1;
}
int portable_impact_effect_asset_load(const PortableAssetPackView *pack,
    PortableImpactEffectRules *storage,const PortableImpactEffectRules **out)
{
    if(!pack || !storage || !out)return 0;
    *out=NULL;PortableAssetSectionView section;
    const PortableAssetPackStatus status=portable_asset_pack_find_section(pack,
        PORTABLE_ASSET_SECTION_IMPACT_EFFECT_RULES,1u,&section);
    if(status==PORTABLE_ASSET_PACK_NOT_FOUND)return 1;
    if(status!=PORTABLE_ASSET_PACK_OK || !portable_impact_effect_asset_open(&section,storage))return 0;
    *out=storage;return 1;
}
