#include "coin_effect_asset.h"
static uint16_t u16(const uint8_t *p)
{return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8u));}
static uint32_t u32(const uint8_t *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8u)|((uint32_t)p[2]<<16u)|((uint32_t)p[3]<<24u);}
static int32_t i32(const uint8_t *p)
{const uint32_t bits=u32(p);return (int32_t)(bits>=0x80000000u?(int64_t)bits-INT64_C(4294967296):(int64_t)bits);}
int portable_coin_effect_asset_open(const PortableAssetSectionView *section,PortableCoinEffectRules *out)
{
    if(!section || !out || !section->bytes || section->version!=1u || section->size!=104u ||
       u32(section->bytes)!=UINT32_C(0x31584643) || u32(section->bytes+4u)!=9u ||
       u32(section->bytes+8u)!=3u || u32(section->bytes+12u)!=4u || u16(section->bytes+102u))return 0;
    PortableCoinEffectRules rules={0};
    for(unsigned i=0u;i<9u;++i){
        rules.horizontal_step[i]=i32(section->bytes+16u+i*4u);
        rules.upward_speed_q16[i]=i32(section->bytes+52u+i*4u);
    }
    for (unsigned i = 0u; i < 3u; ++i)
        rules.bounce_volumes[i] = u16(section->bytes + 88u + i * 2u);
    for (unsigned i = 0u; i < 4u; ++i)
        rules.script_ticks[i] = u16(section->bytes + 94u + i * 2u);
    if(!portable_coin_effect_rules_valid(&rules))return 0;
    *out=rules;return 1;
}
int portable_coin_effect_asset_load(const PortableAssetPackView *pack,
    PortableCoinEffectRules *storage,const PortableCoinEffectRules **out)
{
    if(!pack || !storage || !out)return 0;
    *out=NULL;PortableAssetSectionView section;
    const PortableAssetPackStatus status=portable_asset_pack_find_section(pack,
        PORTABLE_ASSET_SECTION_COIN_EFFECT_RULES,1u,&section);
    if(status==PORTABLE_ASSET_PACK_NOT_FOUND)return 1;
    if(status!=PORTABLE_ASSET_PACK_OK || !portable_coin_effect_asset_open(&section,storage))return 0;
    *out=storage;return 1;
}
