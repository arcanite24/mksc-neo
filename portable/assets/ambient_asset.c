#include "ambient_asset.h"
#include "native_image.h"
#include "sky_asset.h"

#include <string.h>

static uint16_t u16(const uint8_t *p)
{return (uint16_t)p[0]|(uint16_t)((uint16_t)p[1]<<8u);}
static uint32_t u32(const uint8_t *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8u)|((uint32_t)p[2]<<16u)|((uint32_t)p[3]<<24u);}

static int decode_patch(const uint8_t *p,uint32_t data_size,PortableAmbientPatch *out)
{
    static const uint16_t bounds[7]={64u,64u,128u,0x4000u,0x1000u,0x1000u,0x1000u};
    const uint8_t target=p[0];
    const uint16_t offset=u16(p+2u),count=u16(p+4u);
    const uint32_t source=u32(p+8u);
    if(target>6u || p[1]!=0u || u16(p+6u)!=0u || count==0u ||
       (uint32_t)offset+count>bounds[target])return 0;
    const uint32_t size=(uint32_t)count*(target<=2u?2u:1u);
    if(source>data_size || size>data_size-source)return 0;
    *out=(PortableAmbientPatch){.target=target,.offset=offset,.count=count,.data_offset=source};
    return 1;
}

int portable_ambient_asset_open(const PortableAssetSectionView *section,
    PortableAmbientAssetStorage *storage)
{
    if(section==NULL || storage==NULL || section->bytes==NULL ||
       section->version!=1u || section->size<32u)return 0;
    const uint8_t *b=section->bytes;
    const uint32_t lanes=u32(b+8u),frames=u32(b+12u),patches=u32(b+16u),initial=u32(b+20u);
    const uint32_t data_size=u32(b+24u),object_seed=u32(b+28u);
    if(u32(b)!=UINT32_C(0x31424d41) || u32(b+4u)!=1u ||
       lanes>PORTABLE_AMBIENT_LANES || frames>PORTABLE_AMBIENT_FRAMES ||
       patches>PORTABLE_AMBIENT_PATCHES || initial>32u || data_size>262144u ||
       object_seed>data_size || data_size-object_seed<256u ||
       section->size!=32u+(size_t)lanes*32u+(size_t)frames*8u+
           ((size_t)patches+initial)*12u+data_size)return 0;
    memset(storage,0,sizeof *storage);
    const uint8_t *cursor=b+32u;
    for(uint32_t index=0u;index<lanes;++index,cursor+=32u){
        PortableAmbientLane *lane=&storage->lanes[index];
        lane->stage=cursor[0];lane->write_policy=cursor[1];
        lane->controller=cursor[2];lane->trigger=cursor[3];
        lane->first_frame=u16(cursor+4u);lane->frame_count=u16(cursor+6u);
        lane->loop_frame=u16(cursor+8u);lane->initial_frame=u16(cursor+10u);
        lane->initial_counter=u32(cursor+12u);
        for (unsigned param = 0u; param < 4u; ++param)
            lane->params[param] = u32(cursor + 16u + param * 4u);
        if(lane->stage>1u || lane->write_policy>1u || lane->controller>5u || lane->trigger>1u ||
           lane->frame_count==0u || (uint32_t)lane->first_frame+lane->frame_count>frames ||
           lane->initial_frame>=lane->frame_count ||
           (lane->loop_frame!=UINT16_MAX && lane->loop_frame>=lane->frame_count))return 0;
    }
    for(uint32_t index=0u;index<frames;++index,cursor+=8u){
        const uint16_t duration=u16(cursor),first=u16(cursor+2u);
        const uint8_t count=cursor[4];
        if(duration==0u || count>3u || (uint32_t)first+count>patches ||
           cursor[5]!=0u || u16(cursor+6u)!=0u)return 0;
        storage->frames[index]=(PortableAmbientFrame){.duration=duration,.first_patch=first,.patch_count=count};
    }
    for(uint32_t index=0u;index<patches;++index,cursor+=12u)
        if(!decode_patch(cursor,data_size,&storage->patches[index]))return 0;
    for(uint32_t index=0u;index<initial;++index,cursor+=12u)
        if(!decode_patch(cursor,data_size,&storage->initial[index]))return 0;
    storage->rules=(PortableAmbientRules){.lanes=storage->lanes,.frames=storage->frames,
        .patches=storage->patches,.init_patches=storage->initial,.data=cursor,
        .lane_count=(uint16_t)lanes,.frame_count=(uint16_t)frames,.patch_count=(uint16_t)patches,
        .init_patch_count=(uint16_t)initial,.data_size=data_size,.object_seed_offset=object_seed};
    for (unsigned color = 0u; color < 128u; ++color)
        storage->seed.object[color] = u16(cursor + object_seed + color * 2u);
    return portable_ambient_rules_valid(&storage->rules);
}

static int seed_palette(const PortableAssetPackView *pack,uint32_t kind,uint32_t id,
    uint16_t *out,uint32_t header_size)
{
    PortableAssetSectionView section;
    if(portable_asset_pack_find_section(pack,kind,id,&section)!=PORTABLE_ASSET_PACK_OK ||
       section.size<header_size)return 0;
    const uint32_t tiles=u32(section.bytes+4u),colors=u32(section.bytes+8u);
    if(colors>64u || tiles>section.size-header_size ||
       colors*2u>section.size-header_size-tiles)return 0;
    for (uint32_t color = 0u; color < colors; ++color)
        out[color] = u16(section.bytes + header_size + tiles + color * 2u);
    return 1;
}

static int seed_scenery(const PortableAssetPackView *pack,uint32_t id,PortableAmbientSeed *seed)
{
    PortableAssetSectionView section;
    const PortableAssetPackStatus status=portable_asset_pack_find_section(pack,
        PORTABLE_ASSET_SECTION_COURSE_SCENERY,id,&section);
    if(status==PORTABLE_ASSET_PACK_NOT_FOUND)return 1;
    if(status!=PORTABLE_ASSET_PACK_OK || section.size<16u)return 0;
    if(section.version<2u || section.version>4u)return 1;
    const uint32_t count=u32(section.bytes+4u),art=u32(section.bytes+8u);
    const size_t stride=section.version==4u?20u:section.version==3u?16u:12u;
    const size_t offset=16u+(size_t)count*stride;
    if(offset>section.size)return 0;
    if(art==0u)return 1;
    if(section.size-offset<8u)return 0;
    const uint32_t image_size=u32(section.bytes+offset+4u);
    if(image_size>section.size-offset-8u)return 0;
    PortableNativeImage image;
    if(!portable_native_image_open(section.bytes+offset+8u,image_size,&image))return 0;
    if(image.palette!=NULL){
        if((uint32_t)image.palette_base+image.palette_count>128u)return 0;
        for(unsigned color=0u;color<image.palette_count;++color)
            seed->object[image.palette_base+color]=u16(image.palette+color*2u);
    }
    return 1;
}

/* Global AMB1 bounds also cover extended mod art. A composed course must own
 * every byte a lane can change, including dormant and startup patches. */
static int image_patches_fit(const PortableAssetPackView *pack,uint32_t sky_id,
    const PortableAmbientRules *rules)
{
    PortableAssetSectionView sky;
    if(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_SKY_GRAPHICS,
        sky_id,&sky)!=PORTABLE_ASSET_PACK_OK)return 0;
    PortableSkyMotionRules motion;
    const PortableSkyMotionRules *motion_view=NULL;
    if(portable_sky_asset_open_motion(&sky,&motion,&motion_view)!=PORTABLE_SKY_ASSET_OK)return 0;
    const uint32_t sizes[4]={u32(sky.bytes+4u),u32(sky.bytes+12u),
        u32(sky.bytes+16u),u32(sky.bytes+20u)};
    for(unsigned initial=0u;initial<2u;++initial){
        const PortableAmbientPatch *patches=initial?rules->init_patches:rules->patches;
        const unsigned count=initial?rules->init_patch_count:rules->patch_count;
        for(unsigned index=0u;index<count;++index){
            const PortableAmbientPatch *patch=&patches[index];
            if(patch->target>=PORTABLE_AMBIENT_SKY_TILES &&
               (uint32_t)patch->offset+patch->count>sizes[patch->target-PORTABLE_AMBIENT_SKY_TILES])return 0;
        }
    }
    return 1;
}

int portable_ambient_asset_load(const PortableAssetPackView *pack,
    const PortableContentSelection *selection,PortableAmbientAssetStorage *storage,
    const PortableAmbientRules **out_rules,const PortableAmbientSeed **out_seed)
{
    if(pack==NULL || selection==NULL || storage==NULL || out_rules==NULL || out_seed==NULL)return 0;
    *out_rules=NULL;*out_seed=NULL;
    PortableAssetSectionView section;
    const PortableAssetPackStatus status=portable_asset_pack_find_section(pack,
        PORTABLE_ASSET_SECTION_AMBIENT_RULES,selection->course.id,&section);
    if(status==PORTABLE_ASSET_PACK_NOT_FOUND)return 1;
    if(status!=PORTABLE_ASSET_PACK_OK || !portable_ambient_asset_open(&section,storage) ||
       !seed_palette(pack,PORTABLE_ASSET_SECTION_ROAD_GRAPHICS,selection->course.road,storage->seed.road,24u) ||
       !seed_palette(pack,PORTABLE_ASSET_SECTION_SKY_GRAPHICS,selection->course.sky,storage->seed.sky,64u) ||
       !seed_scenery(pack,selection->course.id,&storage->seed) ||
       !image_patches_fit(pack,selection->course.sky,&storage->rules))return 0;
    *out_rules=&storage->rules;*out_seed=&storage->seed;
    return 1;
}
