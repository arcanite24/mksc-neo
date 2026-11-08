#include "finish_asset.h"

#include <string.h>

static uint16_t u16(const uint8_t *p)
{return (uint16_t)p[0]|(uint16_t)((uint16_t)p[1]<<8u);}
static uint32_t u32(const uint8_t *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8u)|((uint32_t)p[2]<<16u)|((uint32_t)p[3]<<24u);}

int portable_finish_asset_open(const PortableAssetSectionView *section,
    PortableFinishAssetStorage *storage)
{
    if(section==NULL || storage==NULL || section->bytes==NULL || section->version!=1u ||
       section->size<20u || section->size>1076u)return 0;
    const uint8_t *b=section->bytes,*cursor=b+20u,*end=b+section->size;
    if(u32(b)!=UINT32_C(0x314e4946) || u32(b+4u)!=8u || b[16]>7u ||
       b[17]!=0u || b[18]!=0u || b[19]!=0u)return 0;
    const uint16_t step=u16(b+8u),orbit_end=u16(b+10u),front=u16(b+12u),final=u16(b+14u);
    if(step==0u || step>32768u || orbit_end==0u || orbit_end>32768u ||
       front==0u || front>32767u || final==0u || final>32767u)return 0;
    memset(storage,0,sizeof *storage);
    storage->rules.camera=(PortableFinishCameraRules){.orbit_step=step,.orbit_end=orbit_end,
        .front_hold_ticks=front,.final_hold_ticks=final,.eligible_rank_max=b[16]};
    for(unsigned driver=0u;driver<8u;++driver){
        if((size_t)(end-cursor)<4u)return 0;
        const uint32_t count=u32(cursor);cursor+=4u;
        if(count==0u || count>32u || (size_t)(end-cursor)<count*4u)return 0;
        storage->rules.schedules[driver]=(PortableFinishSchedule){.count=(uint8_t)count,.rows=storage->rows[driver]};
        for(unsigned row=0u;row<count;++row,cursor+=4u){
            if(cursor[0]>33u || cursor[1]>127u ||
               (cursor[1]==0u && cursor[2]!=255u) ||
               (cursor[2]!=255u && cursor[2]>=count) || cursor[3]!=0u)return 0;
            storage->rows[driver][row]=(PortableFinishRow){.frame=cursor[0],.duration=cursor[1],.next=cursor[2]};
        }
    }
    return cursor==end && portable_finish_rules_valid(&storage->rules);
}

int portable_finish_asset_load(const PortableAssetPackView *pack,
    PortableFinishAssetStorage *storage,const PortableFinishRules **out)
{
    if(pack==NULL || storage==NULL || out==NULL)return 0;
    *out=NULL;
    PortableAssetSectionView section;
    const PortableAssetPackStatus status=portable_asset_pack_find_section(pack,
        PORTABLE_ASSET_SECTION_FINISH_CAMERA_RULES,1u,&section);
    if(status==PORTABLE_ASSET_PACK_NOT_FOUND)return 1;
    if(status!=PORTABLE_ASSET_PACK_OK || !portable_finish_asset_open(&section,storage))return 0;
    *out=&storage->rules;
    return 1;
}
