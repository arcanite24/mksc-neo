#include "content_registry.h"

#include <string.h>

#define COURSE_MAGIC UINT32_C(0x31535243)
#define CHARACTER_MAGIC UINT32_C(0x31524843)
#define ROSTER_MAGIC UINT32_C(0x31565244) /* DRV1 */

static uint32_t u32(const uint8_t *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}

static int referenced(const PortableAssetPackView *pack,uint32_t kind,uint32_t id)
{PortableAssetSectionView s;return id!=0&&portable_asset_pack_find_section(pack,kind,id,&s)==PORTABLE_ASSET_PACK_OK;}

static PortableContentStatus find_manifest(const PortableAssetPackView *pack,
    uint32_t kind,uint32_t requested,PortableAssetSectionView *out)
{
    PortableAssetSectionView candidate={0};
    for (uint32_t i = 0; i < pack->section_count; ++i)
    {
        PortableAssetSectionView s;
        if(portable_asset_pack_section_at(pack,i,&s)!=PORTABLE_ASSET_PACK_OK)return PORTABLE_CONTENT_BAD_MANIFEST;
        if(s.kind!=kind)continue;
        if(requested!=0&&s.content_id==requested){*out=s;return PORTABLE_CONTENT_OK;}
        if(requested==0&&(candidate.bytes==NULL||s.content_id<candidate.content_id))candidate=s;
    }
    if(requested==0&&candidate.bytes!=NULL){*out=candidate;return PORTABLE_CONTENT_OK;}
    return PORTABLE_CONTENT_NOT_FOUND;
}

PortableContentStatus portable_content_registry_select(
    const PortableAssetPackView *pack,uint32_t course_id,uint32_t character_id,
    PortableContentSelection *out)
{
    PortableAssetSectionView c,h;PortableContentSelection n={0};
    PortableContentStatus s;
    if(pack==NULL||out==NULL)return PORTABLE_CONTENT_INVALID_ARGUMENT;
    if((s=find_manifest(pack,PORTABLE_ASSET_SECTION_COURSE_MANIFEST,course_id,&c))!=PORTABLE_CONTENT_OK)return s;
    if((s=find_manifest(pack,PORTABLE_ASSET_SECTION_CHARACTER_MANIFEST,character_id,&h))!=PORTABLE_CONTENT_OK)return s;
    if(c.version!=1||c.size!=68||u32(c.bytes)!=COURSE_MAGIC||u32(c.bytes+4)!=c.content_id||
       h.version!=1||h.size!=24||u32(h.bytes)!=CHARACTER_MAGIC||u32(h.bytes+4)!=h.content_id)
        return PORTABLE_CONTENT_BAD_MANIFEST;
    n.course=(PortableCourseContent){u32(c.bytes+4),u32(c.bytes+8),u32(c.bytes+12),u32(c.bytes+16),
        u32(c.bytes+20),u32(c.bytes+24),u32(c.bytes+28),u32(c.bytes+32),u32(c.bytes+36),u32(c.bytes+40),u32(c.bytes+44),
        u32(c.bytes+48),u32(c.bytes+52),u32(c.bytes+56),u32(c.bytes+60),u32(c.bytes+64)};
    n.character = (PortableCharacterContent){u32(h.bytes + 4), u32(h.bytes + 8), u32(h.bytes + 12),
                                             u32(h.bytes + 16), u32(h.bytes + 20)};
    if(n.character.character_class>=PORTABLE_SOURCE_DRIVER_CLASS_COUNT)
        return PORTABLE_CONTENT_INCOMPATIBLE;
    if(!referenced(pack,PORTABLE_ASSET_SECTION_RACE_RULES,n.course.rules)||
       !referenced(pack,PORTABLE_ASSET_SECTION_ROUTE,n.course.route)||
       !referenced(pack,PORTABLE_ASSET_SECTION_WORLD_MAP,n.course.world)||
       !referenced(pack,PORTABLE_ASSET_SECTION_MATERIAL_FLAGS,n.course.material)||
       !referenced(pack,PORTABLE_ASSET_SECTION_ROAD_GRAPHICS,n.course.road)||
       !referenced(pack,PORTABLE_ASSET_SECTION_SKY_GRAPHICS,n.course.sky)||
       !referenced(pack,PORTABLE_ASSET_SECTION_MINIMAP_GRAPHICS,n.course.minimap)||
       !referenced(pack,PORTABLE_ASSET_SECTION_HUD_GRAPHICS,n.course.hud)||
       !referenced(pack,PORTABLE_ASSET_SECTION_COLLISION_TABLES,n.course.collision)||
       !referenced(pack,PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS,n.character.graphics)||
       !referenced(pack,PORTABLE_ASSET_SECTION_MOTION_TABLES,n.character.motion))
        return PORTABLE_CONTENT_MISSING_REFERENCE;
    *out=n;return PORTABLE_CONTENT_OK;
}

static PortableContentStatus character_manifest(const PortableAssetPackView *pack,
    const PortableAssetSectionView *section,PortableCharacterContent *out)
{
    if(section->version!=1||section->size!=24||u32(section->bytes)!=CHARACTER_MAGIC||
       u32(section->bytes+4)!=section->content_id)
        return PORTABLE_CONTENT_BAD_MANIFEST;
    *out=(PortableCharacterContent){u32(section->bytes+4),u32(section->bytes+8),
        u32(section->bytes+12),u32(section->bytes+16),u32(section->bytes+20)};
    if(out->character_class>=PORTABLE_SOURCE_DRIVER_CLASS_COUNT||
       !referenced(pack,PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS,out->graphics)||
       !referenced(pack,PORTABLE_ASSET_SECTION_MOTION_TABLES,out->motion))
        return PORTABLE_CONTENT_INCOMPATIBLE;
    return PORTABLE_CONTENT_OK;
}

PortableContentStatus portable_content_registry_character(
    const PortableAssetPackView *pack,uint32_t character_id,
    PortableCharacterContent *out)
{
    PortableAssetSectionView section;
    PortableCharacterContent found;
    PortableContentStatus status;
    if(pack==NULL||out==NULL||character_id==0u)return PORTABLE_CONTENT_INVALID_ARGUMENT;
    if(portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_CHARACTER_MANIFEST,
           character_id,&section)!=PORTABLE_ASSET_PACK_OK)
        return PORTABLE_CONTENT_NOT_FOUND;
    if((status=character_manifest(pack,&section,&found))!=PORTABLE_CONTENT_OK)return status;
    *out=found;return PORTABLE_CONTENT_OK;
}

PortableContentStatus portable_content_registry_driver_roster(
    const PortableAssetPackView *pack,PortableDriverRoster *out)
{
    PortableDriverRoster roster;
    uint32_t previous=0u;
    int started=0;
    if(pack==NULL||out==NULL)return PORTABLE_CONTENT_INVALID_ARGUMENT;
    roster.count=0u;
    /* Sections compose by key, so walk content IDs in ascending order. */
    for(;;){
        PortableAssetSectionView next={0};
        for (uint32_t i = 0; i < pack->section_count; ++i)
        {
            PortableAssetSectionView s;
            if(portable_asset_pack_section_at(pack,i,&s)!=PORTABLE_ASSET_PACK_OK)return PORTABLE_CONTENT_BAD_MANIFEST;
            if(s.kind==PORTABLE_ASSET_SECTION_DRIVER_ROSTER&&(!started||s.content_id>previous)&&
               (next.bytes==NULL||s.content_id<next.content_id))next=s;
        }
        if(next.bytes==NULL)break;
        previous=next.content_id;started=1;
        if(next.version!=1u||next.size<8u||u32(next.bytes)!=ROSTER_MAGIC)return PORTABLE_CONTENT_BAD_MANIFEST;
        const uint32_t count=u32(next.bytes+4);
        if(count==0u||count>PORTABLE_DRIVER_ROSTER_CAPACITY||next.size!=8u+(size_t)count*8u)
            return PORTABLE_CONTENT_BAD_MANIFEST;
        for(uint32_t row=0u;row<count;++row){
            const uint8_t *p=next.bytes+8u+row*8u;
            PortableCharacterContent character;
            PortableContentStatus status;
            const uint32_t id=u32(p);
            if(p[4]>=9u||p[4]==4u||p[5]>8u||p[6]!=0u||p[7]!=0u||
               id==0u||id>PORTABLE_DRIVER_ROSTER_MAX_ID)return PORTABLE_CONTENT_BAD_MANIFEST;
            for(size_t seen=0u;seen<roster.count;++seen)
                if(roster.entries[seen].character_id==id)return PORTABLE_CONTENT_INCOMPATIBLE;
            if(roster.count==PORTABLE_DRIVER_ROSTER_CAPACITY)return PORTABLE_CONTENT_INCOMPATIBLE;
            status=portable_content_registry_character(pack,id,&character);
            if(status!=PORTABLE_CONTENT_OK)
                return status==PORTABLE_CONTENT_NOT_FOUND?PORTABLE_CONTENT_MISSING_REFERENCE:status;
            if(p[5]!=0u&&portable_content_registry_character(pack,p[5],&character)!=PORTABLE_CONTENT_OK)
                return PORTABLE_CONTENT_MISSING_REFERENCE;
            roster.entries[roster.count++]=(PortableDriverRosterEntry){id,p[4],p[5]};
        }
    }
    /* Voices come from stock drivers, never from another roster row. */
    for(size_t row=0u;row<roster.count;++row)
        for(size_t other=0u;other<roster.count;++other)
            if(roster.entries[row].voice_driver==roster.entries[other].character_id)
                return PORTABLE_CONTENT_INCOMPATIBLE;
    *out=roster;return PORTABLE_CONTENT_OK;
}

static int roster_driver(const PortableDriverRoster *roster,uint32_t id)
{
    for (size_t i = 0u; i < roster->count; ++i)
        if (roster->entries[i].character_id == id)
            return 1;
    return 0;
}

PortableContentStatus portable_content_registry_find_character_class(
    const PortableAssetPackView *pack,uint32_t character_class,
    PortableCharacterContent *out)
{
    PortableCharacterContent found={0};
    PortableDriverRoster roster;
    PortableContentStatus status;
    int matches=0;
    if(pack==NULL||out==NULL)return PORTABLE_CONTENT_INVALID_ARGUMENT;
    if(character_class>=PORTABLE_SOURCE_DRIVER_CLASS_COUNT)
        return PORTABLE_CONTENT_INCOMPATIBLE;
    if((status=portable_content_registry_driver_roster(pack,&roster))!=PORTABLE_CONTENT_OK)
        return status;
    for(uint32_t i=0;i<pack->section_count;++i){
        PortableAssetSectionView section;
        PortableCharacterContent candidate;
        if(portable_asset_pack_section_at(pack,i,&section)!=PORTABLE_ASSET_PACK_OK)
            return PORTABLE_CONTENT_BAD_MANIFEST;
        if(section.kind!=PORTABLE_ASSET_SECTION_CHARACTER_MANIFEST)continue;
        if((status=character_manifest(pack,&section,&candidate))!=PORTABLE_CONTENT_OK)
            return status;
        if(candidate.character_class==character_class&&!roster_driver(&roster,candidate.id)){
            found=candidate;++matches;}
    }
    if(matches==0)return PORTABLE_CONTENT_NOT_FOUND;
    if(matches!=1)return PORTABLE_CONTENT_INCOMPATIBLE;
    *out=found;return PORTABLE_CONTENT_OK;
}

PortableContentStatus portable_content_registry_saved_driver(
    const PortableAssetPackView *pack,uint32_t character_class,
    uint32_t roster_driver_id,PortableCharacterContent *out)
{
    PortableDriverRoster roster;
    PortableCharacterContent driver;
    PortableContentStatus status;
    if(roster_driver_id==0u)
        return portable_content_registry_find_character_class(pack,character_class,out);
    if(pack==NULL||out==NULL)return PORTABLE_CONTENT_INVALID_ARGUMENT;
    if((status=portable_content_registry_driver_roster(pack,&roster))!=PORTABLE_CONTENT_OK)
        return status;
    if(!roster_driver(&roster,roster_driver_id))return PORTABLE_CONTENT_NOT_FOUND;
    if((status=portable_content_registry_character(pack,roster_driver_id,&driver))!=
       PORTABLE_CONTENT_OK)return status;
    if(driver.character_class!=character_class)return PORTABLE_CONTENT_INCOMPATIBLE;
    *out=driver;return PORTABLE_CONTENT_OK;
}

PortableContentStatus portable_content_registry_voice_driver(
    const PortableAssetPackView *pack,uint32_t character_id,
    PortableCharacterContent *out)
{
    PortableDriverRoster roster;
    PortableCharacterContent driver;
    PortableContentStatus status;
    if(pack==NULL||out==NULL)return PORTABLE_CONTENT_INVALID_ARGUMENT;
    if((status=portable_content_registry_driver_roster(pack,&roster))!=PORTABLE_CONTENT_OK||
       (status=portable_content_registry_character(pack,character_id,&driver))!=PORTABLE_CONTENT_OK)
        return status;
    for(size_t row=0u;row<roster.count;++row)
        if(roster.entries[row].character_id==character_id&&roster.entries[row].voice_driver!=0u)
            return portable_content_registry_character(pack,roster.entries[row].voice_driver,out);
    return portable_content_registry_find_character_class(pack,driver.character_class,out);
}

const char *portable_content_status_name(PortableContentStatus s)
{
    static const char *const n[] = {"ok",           "invalid_argument",  "not_found",
                                    "bad_manifest", "missing_reference", "incompatible"};
    return (unsigned)s < sizeof n / sizeof n[0] ? n[s] : "invalid_status";
}

uint32_t portable_content_import_version(const PortableAssetPackView *pack)
{
    PortableAssetSectionView section;
    if(pack==NULL||portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_IMPORT_STAMP,1u,&section)!=
           PORTABLE_ASSET_PACK_OK||section.version!=1u||section.size!=8u)return 0u;
    const uint8_t *b=section.bytes;
    const uint32_t magic=(uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;
    if(magic!=PORTABLE_IMPORT_STAMP_MAGIC)return 0u;
    return (uint32_t)b[4]|(uint32_t)b[5]<<8|(uint32_t)b[6]<<16|(uint32_t)b[7]<<24;
}
