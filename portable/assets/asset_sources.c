#include "asset_sources.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t u32(const uint8_t *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t v)
{for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8u*i));}

static int category(uint32_t kind)
{
    switch(kind){
    case PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS:
    case PORTABLE_ASSET_SECTION_DRIVER_3D:
    case PORTABLE_ASSET_SECTION_KART_3D:
    case PORTABLE_ASSET_SECTION_DRIVER_SPRITES: return PORTABLE_ASSET_SOURCE_DRIVERS;
    case PORTABLE_ASSET_SECTION_HUD_GRAPHICS:
    case PORTABLE_ASSET_SECTION_ITEM_BOX_GRAPHICS:
    case PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS:
    case PORTABLE_ASSET_SECTION_LIGHTNING_EFFECT_GRAPHICS: return PORTABLE_ASSET_SOURCE_ITEMS;
    case PORTABLE_ASSET_SECTION_ROAD_GRAPHICS:
    case PORTABLE_ASSET_SECTION_SKY_GRAPHICS:
    case PORTABLE_ASSET_SECTION_COURSE_SCENERY:
    case PORTABLE_ASSET_SECTION_COURSE_3D:
    case PORTABLE_ASSET_SECTION_COURSE_3D_TEXTURE:
    case PORTABLE_ASSET_SECTION_COURSE_3D_TEXTURE_BUNDLE: return PORTABLE_ASSET_SOURCE_COURSES;
    default:return -1;
    }
}
static int binding_rows(const PortableAssetPackView *pack,PortableAssetSectionView *s)
{
    memset(s,0,sizeof *s);
    if(!pack || !pack->section_count || portable_asset_pack_find_section(pack,PORTABLE_ASSET_SECTION_MODEL_BINDINGS,1,s)==PORTABLE_ASSET_PACK_NOT_FOUND){
        memset(s,0,sizeof *s);return 1;
    }
    return s->bytes && s->size>=8 && (s->version==1||s->version==2) &&
        !memcmp(s->bytes,"BND1",4) && u32(s->bytes+4)<=PORTABLE_MODEL_MAX_BINDINGS &&
        s->size==8u+44u*u32(s->bytes+4);
}
int portable_asset_source_available(const PortableAssetPackView *pack,unsigned group)
{
    if(!pack||group>=PORTABLE_ASSET_SOURCE_COUNT)return 0;
    for(uint32_t i=0;i<pack->section_count;++i){
        PortableAssetSectionView s;
        if(portable_asset_pack_section_at(pack,i,&s)!=PORTABLE_ASSET_PACK_OK)return 0;
        if(category(s.kind)==(int)group)return 1;
    }
    if(group!=PORTABLE_ASSET_SOURCE_ITEMS){
        PortableAssetSectionView s;
        if(!binding_rows(pack,&s))return 1; /* Let composition name invalid registries. */
        const uint32_t kind=group==PORTABLE_ASSET_SOURCE_DRIVERS?PORTABLE_MODEL_DRIVER:PORTABLE_MODEL_SCENERY;
        for(size_t i=8;s.bytes&&i<s.size;i+=44)if(u32(s.bytes+i)==kind)return 1;
    }
    return 0;
}

static int append(PortableAssetSectionView *rows,size_t *count,size_t cap,PortableAssetSectionView s)
{
    for(size_t i=0;i<*count;++i)if(rows[i].kind==s.kind && rows[i].content_id==s.content_id){rows[i]=s;return 1;}
    if(*count==cap)return 0;
    rows[(*count)++]=s;return 1;
}

/* Follow selected descriptors into their resource graph. Missing resources
 * may be supplied by an earlier mod; only resources present in this source
 * gain priority. Resource IDs are shared and must be namespaced by authors. */
static int resource(const PortableAssetPackView *pack,uint32_t kind,uint32_t id,
    PortableAssetSectionView *rows,size_t *count,size_t capacity)
{
    PortableAssetSectionView s;
    const PortableAssetPackStatus status=portable_asset_pack_find_section(pack,kind,id,&s);
    if(status==PORTABLE_ASSET_PACK_NOT_FOUND)return 1;
    if(status!=PORTABLE_ASSET_PACK_OK||!append(rows,count,capacity,s))return 0;
    if(kind==PORTABLE_ASSET_SECTION_MODEL_MESH){
        if(!s.bytes||s.size<16||memcmp(s.bytes,"MSH1",4))return 0;
        const uint32_t materials=u32(s.bytes+12);
        if(materials>PORTABLE_MODEL_MAX_MATERIALS||s.size<16u+4u*materials)return 0;
        for(uint32_t i=0;i<materials;++i)
            if(!resource(pack,PORTABLE_ASSET_SECTION_MODEL_MATERIAL,u32(s.bytes+16u+4u*i),rows,count,capacity))return 0;
        if(!resource(pack,PORTABLE_ASSET_SECTION_MODEL_RIG,id,rows,count,capacity))return 0;
    }
    return 1;
}

int portable_asset_sources_compose(const PortableAssetPackView *fallback,
    const PortableAssetPackView *const preferred[PORTABLE_ASSET_SOURCE_COUNT],
    int base_drivers,PortableAssetSectionView *sections,size_t capacity,
    PortableModelBindingOverlay *bindings,PortableAssetPackView *out,char *error,size_t error_capacity)
{
    if(!fallback||!preferred||!sections||!bindings||!out)return 0;
    size_t count=0;
    for(uint32_t i=0;i<fallback->section_count;++i){
        PortableAssetSectionView s;
        if(portable_asset_pack_section_at(fallback,i,&s)!=PORTABLE_ASSET_PACK_OK)goto invalid;
        if(base_drivers && (s.kind==PORTABLE_ASSET_SECTION_DRIVER_3D||s.kind==PORTABLE_ASSET_SECTION_KART_3D||
            s.kind==PORTABLE_ASSET_SECTION_DRIVER_SPRITES))continue;
        if(!append(sections,&count,capacity,s))goto invalid;
    }
    for(unsigned group=0;group<PORTABLE_ASSET_SOURCE_COUNT;++group){
        const PortableAssetPackView *pack=preferred[group];
        if(!pack)continue;
        for(uint32_t i=0;i<pack->section_count;++i){
            PortableAssetSectionView s;
            if(portable_asset_pack_section_at(pack,i,&s)!=PORTABLE_ASSET_PACK_OK)goto invalid;
            if(category(s.kind)!=(int)group)continue;
            if(!append(sections,&count,capacity,s))goto invalid;
            if(s.kind==PORTABLE_ASSET_SECTION_KART_3D && s.bytes && s.size>=16 &&
               !resource(pack,PORTABLE_ASSET_SECTION_MODEL_MESH,u32(s.bytes+12),sections,&count,capacity))goto invalid;
            if(s.kind==PORTABLE_ASSET_SECTION_DRIVER_3D && s.bytes && s.size>=32 &&
               !resource(pack,PORTABLE_ASSET_SECTION_MODEL_MATERIAL,u32(s.bytes+28),sections,&count,capacity))goto invalid;
        }
    }
    /* Registries contain both driver and scenery rows: prioritize each key
     * independently, using the raw selected layer rather than inherited rows. */
    if(base_drivers||preferred[0]||preferred[2]){
        PortableAssetSectionView source[3];
        if(!binding_rows(fallback,&source[0])||!binding_rows(preferred[0],&source[1])||
           !binding_rows(preferred[2],&source[2]))goto invalid;
        uint8_t bytes[8u+PORTABLE_MODEL_MAX_BINDINGS*44u];
        memcpy(bytes,"BND1",4);uint32_t rows=0;
        for(unsigned pass=0;pass<3;++pass){
            const PortableAssetSectionView *s=&source[pass];
            if(pass){
                /* Remove inherited keys once, then retain incoming duplicates
                 * for the model catalog's ordinary ambiguity fallback. */
                uint32_t kept=0;
                for(uint32_t old=0;old<rows;++old){
                    uint8_t *row=bytes+8u+old*44u;int replaced=0;
                    const uint32_t wanted=pass==1?PORTABLE_MODEL_DRIVER:PORTABLE_MODEL_SCENERY;
                    for(size_t i=8;s->bytes&&i<s->size;i+=44)
                        if(u32(s->bytes+i)==wanted&&!memcmp(row,s->bytes+i,16u)){replaced=1;break;}
                    if(!replaced){memmove(bytes+8u+kept*44u,row,44u);++kept;}
                }
                rows=kept;
            }
            for(size_t i=8;s->bytes && i<s->size;i+=44){
                const uint8_t *row=s->bytes+i;const uint32_t kind=u32(row);
                if((pass==0 && base_drivers && kind==PORTABLE_MODEL_DRIVER)||
                   (pass==1 && kind!=PORTABLE_MODEL_DRIVER)||(pass==2 && kind!=PORTABLE_MODEL_SCENERY))continue;
                if(rows==PORTABLE_MODEL_MAX_BINDINGS)goto invalid;
                memcpy(bytes+8u+rows*44u,row,44u);++rows;
                const PortableAssetPackView *pack=pass==1?preferred[0]:pass==2?preferred[2]:NULL;
                if(pack&&!resource(pack,PORTABLE_ASSET_SECTION_MODEL_MESH,u32(row+16),sections,&count,capacity))goto invalid;
            }
        }
        put32(bytes+4,rows);
        PortableAssetSectionView registry={.kind=PORTABLE_ASSET_SECTION_MODEL_BINDINGS,.content_id=1,
            .version=2,.flags=PORTABLE_ASSET_SECTION_DETERMINISTIC,.bytes=bytes,.size=8u+rows*44u};
        if(rows||source[0].bytes){
            if(source[0].version==1 && source[0].size==registry.size &&
               !memcmp(source[0].bytes,registry.bytes,registry.size)){
                if(!append(sections,&count,capacity,source[0]))goto invalid;
            }else{
                if(!portable_model_bindings_overlay_prepare(NULL,&registry,bindings,error,error_capacity))return 0;
                bindings->section.flags=PORTABLE_ASSET_SECTION_DETERMINISTIC;
                if(!append(sections,&count,capacity,bindings->section))goto invalid;
            }
        }
    }
    /* Preserve legacy base/profile/identity digests for a genuine no-op. */
    int unchanged=count==fallback->section_count;
    for(size_t i=0;unchanged&&i<count;++i){
        PortableAssetSectionView old;
        const PortableAssetSectionView *s=&sections[i];
        unchanged=portable_asset_pack_find_section(fallback,s->kind,s->content_id,&old)==PORTABLE_ASSET_PACK_OK &&
            s->version==old.version&&s->flags==old.flags&&s->semantic_revision==old.semantic_revision&&
            s->size==old.size&&(!s->size||!memcmp(s->bytes,old.bytes,s->size));
    }
    if(unchanged){*out=*fallback;return 1;}
    /* Compose also recalculates the effective deterministic content digest. */
    PortableAssetPackView view=*fallback;view.bytes=NULL;view.size=0;view.resolved_sections=sections;view.section_count=(uint32_t)count;
    const PortableAssetPackView empty={.resolved_sections=sections,.driving_profile=fallback->driving_profile};
    const PortableAssetPackView *layers[]={&empty,&view};
    /* compose may not alias its input descriptors. */
    PortableAssetSectionView *copy=malloc((count?count:1u)*sizeof *copy);
    if(!copy)goto invalid;
    memcpy(copy,sections,count*sizeof *copy);view.resolved_sections=copy;
    const int ok=portable_asset_pack_compose(layers,2,sections,capacity,out)==PORTABLE_ASSET_PACK_OK;
    free(copy);if(ok)return 1;
invalid:
    if(error&&error_capacity)snprintf(error,error_capacity,"asset sources: invalid registry or section capacity");
    return 0;
}
