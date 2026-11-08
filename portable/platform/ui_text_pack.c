#include "ui_text.h"
#include <string.h>
static uint32_t le32(const uint8_t *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
int portable_ui_text_load(const PortableAssetPackView *pack,PortableUiTextBank *bank)
{
    if(bank==NULL)return 0;
    if(pack==NULL){*bank=(PortableUiTextBank){0};return 1;}
    PortableUiTextBank next={0};
    for(uint32_t i=0u;i<pack->section_count;++i){
        PortableAssetSectionView s;
        if(portable_asset_pack_section_at(pack,i,&s)!=PORTABLE_ASSET_PACK_OK)return 0;
        if(s.kind!=PORTABLE_ASSET_SECTION_UI_TEXT)continue;
        if(s.version!=1u || s.size<10u || s.size>67u ||
           memcmp(s.bytes,"UIT1",4u)!=0 || le32(s.bytes+4u)!=s.size-9u ||
           s.bytes[s.size-1u]!=0u || memchr(s.bytes+8u,0,s.size-9u)!=NULL ||
           !portable_ui_text_valid(s.content_id,(const char *)s.bytes+8u))return 0;
        next.values[s.content_id-1u]=(const char *)s.bytes+8u;
    }
    *bank=next;return 1;
}
