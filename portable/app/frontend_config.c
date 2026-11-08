#include "frontend_config.h"

#include "content_registry.h"
#include "frontend_motion_asset.h"

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0]|((uint32_t)p[1]<<8u)|((uint32_t)p[2]<<16u)|((uint32_t)p[3]<<24u);
}

static size_t manifest_ids(const PortableAssetPackView *pack,uint32_t kind,
                           uint32_t *ids,size_t capacity)
{
    size_t count=0;
    for(uint32_t i=0;i<pack->section_count;++i){
        PortableAssetSectionView section;
        if(portable_asset_pack_section_at(pack,i,&section)!=PORTABLE_ASSET_PACK_OK)
            return 0;
        if(section.kind!=kind)continue;
        if(count==capacity)return 0;
        size_t at=count++;
        while(at>0u&&ids[at-1u]>section.content_id){
            ids[at]=ids[at-1u];--at;
        }
        ids[at]=section.content_id;
    }
    return count;
}

static int id_present(const uint32_t *ids,size_t count,uint32_t wanted)
{
    for(size_t index=0;index<count;++index)if(ids[index]==wanted)return 1;
    return 0;
}

/* Roster drivers fill pages after the stock grid, skipping Random. Each one
 * borrows fallback art from the stock driver of its seat class and menu voices
 * from its row's voice driver (the seat's when zero). */
static size_t roster_pages(const PortableAssetPackView *pack,
    PortableFrontendDriverSlot drivers[PORTABLE_FRONTEND_DRIVER_CAPACITY])
{
    PortableDriverRoster roster;
    uint8_t stock_class[PORTABLE_FRONTEND_DRIVER_SLOTS];
    if(portable_content_registry_driver_roster(pack,&roster)!=PORTABLE_CONTENT_OK)return 0u;
    if(roster.count==0u)return PORTABLE_FRONTEND_DRIVER_SLOTS;
    for(uint8_t slot=0u;slot<PORTABLE_FRONTEND_DRIVER_SLOTS;++slot){
        PortableCharacterContent stock;
        stock_class[slot]=UINT8_MAX;
        if(slot!=PORTABLE_FRONTEND_RANDOM_SLOT&&drivers[slot].available&&
           portable_content_registry_character(pack,drivers[slot].content_id,&stock)==
               PORTABLE_CONTENT_OK)
            stock_class[slot]=(uint8_t)stock.character_class;
    }
    size_t index=PORTABLE_FRONTEND_DRIVER_SLOTS;
    for(size_t row=0u;row<roster.count;++row){
        PortableCharacterContent driver;
        uint8_t seat=0u;
        if(index%PORTABLE_FRONTEND_DRIVER_SLOTS==PORTABLE_FRONTEND_RANDOM_SLOT)
            drivers[index++]=(PortableFrontendDriverSlot){0};
        if(index>=PORTABLE_FRONTEND_DRIVER_CAPACITY||
           portable_content_registry_character(pack,roster.entries[row].character_id,&driver)!=
               PORTABLE_CONTENT_OK)return 0u;
        while(seat<PORTABLE_FRONTEND_DRIVER_SLOTS&&stock_class[seat]!=driver.character_class)++seat;
        if(seat==PORTABLE_FRONTEND_DRIVER_SLOTS)return 0u;
        for(uint8_t slot=0u;slot<PORTABLE_FRONTEND_DRIVER_SLOTS;++slot)
            if(drivers[slot].content_id==driver.id)return 0u;
        drivers[index++]=(PortableFrontendDriverSlot){driver.id,1u,
            roster.entries[row].voice_driver!=0u?roster.entries[row].voice_driver:
                drivers[seat].content_id,seat,roster.entries[row].art_slot,0u,0u};
    }
    while(index%PORTABLE_FRONTEND_DRIVER_SLOTS!=0u)
        drivers[index++]=(PortableFrontendDriverSlot){0};
    return index;
}

int portable_frontend_add_3d_drivers(const PortableDriver3D *rows,size_t count,
    PortableFrontendDriverSlot drivers[PORTABLE_FRONTEND_DRIVER_CAPACITY],PortableFrontendConfig *config)
{
    size_t index=config->driver_count;
    PortableFrontendDriverSlot added[PORTABLE_FRONTEND_DRIVER_CAPACITY];
    size_t added_count=0u;
    for(size_t row=0u;row<count;++row){
        if(!(rows[row].flags&PORTABLE_DRIVER_3D_SELECTABLE))continue;
        int listed=0;
        uint8_t seat=0u;
        for(size_t i=0u;i<config->driver_count;++i)listed|=drivers[i].content_id==rows[row].id;
        while(seat<PORTABLE_FRONTEND_DRIVER_SLOTS&&drivers[seat].content_id!=rows[row].donor)++seat;
        if(listed||seat==PORTABLE_FRONTEND_DRIVER_SLOTS)continue;
        added[added_count++]=(PortableFrontendDriverSlot){rows[row].id,1u,rows[row].donor,seat,0u,1u,0u};
    }
    for(size_t i=0u;i<added_count;++i){
        if(index%PORTABLE_FRONTEND_DRIVER_SLOTS==PORTABLE_FRONTEND_RANDOM_SLOT&&index<PORTABLE_FRONTEND_DRIVER_CAPACITY)
            drivers[index++]=(PortableFrontendDriverSlot){0};
        if(index>=PORTABLE_FRONTEND_DRIVER_CAPACITY){
            for(size_t j=config->driver_count;j<PORTABLE_FRONTEND_DRIVER_CAPACITY;++j)
                drivers[j]=(PortableFrontendDriverSlot){0};
            return 0;
        }
        drivers[index++]=added[i];
    }
    while(index%PORTABLE_FRONTEND_DRIVER_SLOTS!=0u)drivers[index++]=(PortableFrontendDriverSlot){0};
    config->driver_count=index;
    return 1;
}

int portable_frontend_add_platform_drivers(const PortableDriver3D *rows,size_t count,uint8_t platform,
    PortableFrontendDriverSlot drivers[PORTABLE_FRONTEND_DRIVER_CAPACITY],PortableFrontendConfig *config)
{
    size_t index=config->driver_count;
    const PortableFrontendDriverSlot empty={.platform=platform};
    if(platform==0u||index%PORTABLE_FRONTEND_DRIVER_SLOTS!=0u)return 0;
    for(size_t row=0u;row<count;++row){
        uint8_t stock=0u,seat=0u;
        while(stock<PORTABLE_FRONTEND_DRIVER_SLOTS&&drivers[stock].content_id!=rows[row].id)++stock;
        while(seat<PORTABLE_FRONTEND_DRIVER_SLOTS&&drivers[seat].content_id!=rows[row].donor)++seat;
        if(stock<PORTABLE_FRONTEND_DRIVER_SLOTS)seat=stock;
        else if(!(rows[row].flags&PORTABLE_DRIVER_3D_SELECTABLE)||seat==PORTABLE_FRONTEND_DRIVER_SLOTS)continue;
        if(index%PORTABLE_FRONTEND_DRIVER_SLOTS==PORTABLE_FRONTEND_RANDOM_SLOT&&index<PORTABLE_FRONTEND_DRIVER_CAPACITY)
            drivers[index++]=empty;
        if(index>=PORTABLE_FRONTEND_DRIVER_CAPACITY){
            for(size_t j=config->driver_count;j<PORTABLE_FRONTEND_DRIVER_CAPACITY;++j)
                drivers[j]=(PortableFrontendDriverSlot){0};
            return 0;
        }
        drivers[index++]=(PortableFrontendDriverSlot){rows[row].id,1u,
            stock<PORTABLE_FRONTEND_DRIVER_SLOTS?rows[row].id:rows[row].donor,seat,0u,1u,platform};
    }
    if(index==config->driver_count)return 1; /* No drivers: no pages. */
    while(index%PORTABLE_FRONTEND_DRIVER_SLOTS!=0u)drivers[index++]=empty;
    config->driver_count=index;
    return 1;
}

int build_frontend_config(const PortableAssetPackView *pack,
    PortableFrontendDriverSlot drivers[PORTABLE_FRONTEND_DRIVER_CAPACITY],
    PortableFrontendCourseSlot courses[PORTABLE_FRONTEND_COURSE_CAPACITY],PortableFrontendConfig *config)
{
    static const uint32_t source_driver_ids[PORTABLE_FRONTEND_DRIVER_SLOTS]=
        {1u,2u,3u,4u,0u,5u,6u,7u,8u};
    uint32_t character_ids[64],course_ids[PORTABLE_FRONTEND_MANIFEST_CAPACITY];
    const size_t character_count=manifest_ids(pack,
        PORTABLE_ASSET_SECTION_CHARACTER_MANIFEST,character_ids,64u);
    const size_t course_count=manifest_ids(pack,
        PORTABLE_ASSET_SECTION_COURSE_MANIFEST,course_ids,PORTABLE_FRONTEND_MANIFEST_CAPACITY);
    size_t mapped_courses=0u;
    if(character_count==0u||course_count==0u)return 0;
    for(size_t index=0u;index<PORTABLE_FRONTEND_DRIVER_SLOTS;++index)
        drivers[index]=(PortableFrontendDriverSlot){.content_id=source_driver_ids[index],
            .available=(uint8_t)(source_driver_ids[index]==0u||
                      id_present(character_ids,character_count,source_driver_ids[index]))};
    const size_t driver_count=roster_pages(pack,drivers);
    if(driver_count==0u)return 0;
    for(uint32_t set=0u;set<PORTABLE_TRACK_SET_COUNT;++set){
        const PortableTrackSet track_set=(PortableTrackSet)set;
        const uint32_t cups=portable_track_set_cup_count(track_set);
        PortableAssetSectionView catalog;
        const PortableAssetPackStatus found=portable_asset_pack_find_section(pack,
            PORTABLE_ASSET_SECTION_COURSE_CATALOG,set+1u,&catalog);
        if(found==PORTABLE_ASSET_PACK_NOT_FOUND){
            if(set!=0u)continue;
            /* Small legacy/custom packs default to ascending manifest order. */
            if(course_count>PORTABLE_FRONTEND_CUPS*PORTABLE_FRONTEND_TRACKS_PER_CUP)return 0;
            for(size_t index=0u;index<course_count;++index)
                courses[mapped_courses++]=(PortableFrontendCourseSlot){course_ids[index],
                    (uint8_t)(index/PORTABLE_FRONTEND_TRACKS_PER_CUP),
                    (uint8_t)(index%PORTABLE_FRONTEND_TRACKS_PER_CUP),1u};
            break;
        }
        if(found!=PORTABLE_ASSET_PACK_OK||
           (catalog.version!=1u&&catalog.version!=2u)||catalog.size<8u)return 0;
        const uint32_t stride=catalog.version==2u?12u:8u;
        const uint8_t *bytes=catalog.bytes;
        if(read_u32(bytes)!=UINT32_C(0x31544143))return 0;
        const uint32_t count=read_u32(bytes+4u);
        if(count>cups*PORTABLE_FRONTEND_TRACKS_PER_CUP||catalog.size!=8u+count*stride)
            return 0;
        for(uint32_t index=0u;index<count;++index){
            const uint8_t *row=bytes+8u+index*stride;
            const uint32_t id=read_u32(row);
            const uint8_t cup=row[4],track=row[5];
            const uint8_t mapped_cup=(uint8_t)(cup+portable_track_set_first_cup(track_set));
            if(catalog.version==2u&&read_u32(row+8u)>=portable_track_set_header_limit(track_set))return 0;
            /* Time Trial ghosts store course IDs in one byte. */
            if(track_set==PORTABLE_TRACK_SET_CUSTOM&&id>UINT8_MAX)return 0;
            if(cup>=cups||track>=PORTABLE_FRONTEND_TRACKS_PER_CUP||
               !id_present(course_ids,course_count,id))return 0;
            for(size_t previous=0u;previous<mapped_courses;++previous)
                if(courses[previous].content_id==id||
                   (courses[previous].cup==mapped_cup&&courses[previous].track==track))return 0;
            courses[mapped_courses++]=(PortableFrontendCourseSlot){id,mapped_cup,track,1u};
        }
    }
    if(mapped_courses==0u)return 0;
    *config=(PortableFrontendConfig){drivers,driver_count,courses,mapped_courses};
    return 1;
}


int portable_frontend_course_song(const PortableAssetPackView *pack,
                                  uint32_t course_id,uint16_t *song)
{
    if(song==NULL)return 0;
    for(uint32_t catalog_id=1u;catalog_id<=PORTABLE_TRACK_SET_COUNT;++catalog_id){
        PortableAssetSectionView catalog;
        const PortableAssetPackStatus status=portable_asset_pack_find_section(pack,
            PORTABLE_ASSET_SECTION_COURSE_CATALOG,catalog_id,&catalog);
        if(status==PORTABLE_ASSET_PACK_NOT_FOUND)continue;
        if(status!=PORTABLE_ASSET_PACK_OK||
           (catalog.version!=1u&&catalog.version!=2u)||catalog.size<8u||
           read_u32(catalog.bytes)!=UINT32_C(0x31544143))return 0;
        const uint32_t stride=catalog.version==2u?12u:8u;
        const uint32_t count=read_u32(catalog.bytes+4u);
        if(count>PORTABLE_FRONTEND_COURSE_CAPACITY||catalog.size!=8u+count*stride)return 0;
        for(uint32_t index=0u;index<count;++index){
            const uint8_t *row=catalog.bytes+8u+index*stride;
            if(read_u32(row)==course_id){
                *song=(uint16_t)((uint16_t)row[6]|((uint16_t)row[7]<<8u));
                return *song!=0u;
            }
        }
    }
    return 0;
}

/* Imported source header keys are explicit data, independent of manifest IDs.
 * Version 1 remains usable by menus and Time Trial, but needs reimport for GP. */
uint8_t portable_frontend_cup_tracks(const PortableAssetPackView *pack,uint8_t cup,
                                     PortableGpCupDefinition *definition)
{
    PortableFrontendDriverSlot drivers[PORTABLE_FRONTEND_DRIVER_CAPACITY];
    PortableFrontendCourseSlot courses[PORTABLE_FRONTEND_COURSE_CAPACITY];
    PortableFrontendConfig config;
    PortableAssetSectionView catalog;
    const PortableTrackSet set=portable_track_set_of_cup(cup);
    PortableGpCupDefinition next={0};
    uint8_t seen=0u;
    if(definition==NULL||set==PORTABLE_TRACK_SET_ARENA||set==PORTABLE_TRACK_SET_COUNT)return 0;
    next.cup_index=(uint8_t)(cup-portable_track_set_first_cup(set));
    next.track_set=(uint8_t)set;
    if(!build_frontend_config(pack,drivers,courses,&config)||
       portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_COURSE_CATALOG,
           1u+next.track_set,&catalog)!=PORTABLE_ASSET_PACK_OK||catalog.version!=2u)return 0;
    for(size_t index=0u;index<read_u32(catalog.bytes+4u);++index){
        const uint8_t *row=catalog.bytes+8u+index*12u;
        if(row[4]!=next.cup_index)continue;
        const uint8_t track=row[5],header=(uint8_t)read_u32(row+8u);
        for(unsigned previous=0u;previous<4u;++previous)
            if((seen&(1u<<previous))!=0u&&next.source_headers[previous]==header)return 0;
        next.course_ids[track]=read_u32(row);
        next.source_headers[track]=header;
        seen=(uint8_t)(seen|(1u<<track));
    }
    if(seen!=0u)*definition=next;
    return seen;
}

int portable_frontend_gp_cup(const PortableAssetPackView *pack,uint8_t cup,
                             PortableGpCupDefinition *definition)
{
    PortableGpCupDefinition next;
    if(portable_frontend_cup_tracks(pack,cup,&next)!=15u)return 0;
    *definition=next;
    return 1;
}

int portable_frontend_configure_motion(const PortableAssetPackView *pack,PortableFrontendFlow *flow)
{
    PortableFrontendMotion motion;PortableMenuHeadingRules rules={0};
    if(!flow||!portable_frontend_motion_load(pack,&motion))return 0;
    if(motion.bytes){
        int32_t samples[8];for(unsigned i=0;i<8u;++i)samples[i]=portable_menu_sine(&motion,(i+1u)*32u);
        if(!portable_frontend_flow_set_title_cursor_sine(flow,samples))return 0;
    }else if(!portable_frontend_flow_set_title_cursor_sine(flow,NULL))return 0;
    if(!portable_frontend_motion_headings(&motion,&rules))return portable_frontend_flow_set_heading_rules(flow,NULL,NULL);
    int32_t sine[1024];for(unsigned i=0;i<1024u;++i)sine[i]=portable_menu_sine(&motion,i);
    if(!portable_frontend_flow_set_heading_rules(flow,&rules,sine))return 0;
    for(unsigned group=0;group<4u;++group){
        PortableAssetSectionView strip;
        if(portable_asset_pack_find_section(pack,15u,rules.groups[group].strip_image_id,&strip)!=PORTABLE_ASSET_PACK_OK)return 0;
        uint32_t digest=UINT32_C(2166136261);
        for(size_t i=0;i<strip.size;++i)digest=(digest^strip.bytes[i])*UINT32_C(16777619);
        if(digest!=rules.groups[group].strip_digest)flow->heading_static_mask|=(uint8_t)(1u<<group);
    }
    return 1;
}
