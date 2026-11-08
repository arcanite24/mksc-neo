#include "character_view_asset.h"

static uint32_t u32(const uint8_t *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8u)|((uint32_t)p[2]<<16u)|((uint32_t)p[3]<<24u);}

int portable_character_view_asset_open(const PortableAssetSectionView *section,
    PortableCharacterViewRules *rules)
{
    if(section==NULL || rules==NULL || section->bytes==NULL || section->version!=1u ||
       section->size!=38u || u32(section->bytes)!=UINT32_C(0x31525644) ||
       u32(section->bytes+4u)!=15u)return 0;
    for(unsigned index=0u;index<15u;++index){
        const uint8_t response=section->bytes[8u+index],speed=section->bytes[23u+index];
        if(response>18u || speed>18u)return 0;
        rules->response_limit[index]=response;rules->speed_limit[index]=speed;
    }
    return 1;
}

int portable_character_view_asset_load(const PortableAssetPackView *pack,
    PortableCharacterViewRules *storage,const PortableCharacterViewRules **out)
{
    if(pack==NULL || storage==NULL || out==NULL)return 0;
    *out=NULL;
    PortableAssetSectionView section;
    const PortableAssetPackStatus status=portable_asset_pack_find_section(pack,
        PORTABLE_ASSET_SECTION_DRIVER_VIEW_RULES,1u,&section);
    if(status==PORTABLE_ASSET_PACK_NOT_FOUND)return 1;
    if(status!=PORTABLE_ASSET_PACK_OK || !portable_character_view_asset_open(&section,storage))return 0;
    *out=storage;
    return 1;
}
