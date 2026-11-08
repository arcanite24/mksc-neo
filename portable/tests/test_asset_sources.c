#include "asset_sources.h"
#include "game_library.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"asset sources line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static PortableAssetSectionView section(uint32_t kind,uint32_t id,const uint8_t *bytes,size_t size)
{return (PortableAssetSectionView){.kind=kind,.content_id=id,.version=1,.flags=2,.bytes=bytes,.size=size};}
static void registry(uint8_t *bytes,uint32_t driver,uint32_t scenery)
{
    memset(bytes,0,96);memcpy(bytes,"BND1",4);put32(bytes+4,2);
    put32(bytes+8,PORTABLE_MODEL_DRIVER);put32(bytes+12,1);put32(bytes+24,driver);
    put32(bytes+52,PORTABLE_MODEL_SCENERY);put32(bytes+56,24);put32(bytes+68,scenery);
    for(unsigned i=0;i<3;++i){put32(bytes+28+4*i,65536);put32(bytes+72+4*i,65536);}
}
int main(void)
{
    const uint8_t first[]={1,2,3,4},last[]={4,3,2,1};
    uint8_t old_bindings[96],selected_bindings[96];registry(old_bindings,20,30);registry(selected_bindings,40,50);
    PortableAssetSectionView current[]={section(PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS,1,last,4),
        section(PORTABLE_ASSET_SECTION_ITEM_BOX_GRAPHICS,1,last,4),section(PORTABLE_ASSET_SECTION_ROAD_GRAPHICS,24,last,4),
        section(PORTABLE_ASSET_SECTION_MODEL_BINDINGS,1,old_bindings,96),
        section(PORTABLE_ASSET_SECTION_DRIVER_3D,1,last,4),section(PORTABLE_ASSET_SECTION_KART_3D,1,last,4)};
    PortableAssetSectionView donor[]={section(PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS,1,first,4),
        section(PORTABLE_ASSET_SECTION_ITEM_BOX_GRAPHICS,1,first,4),section(PORTABLE_ASSET_SECTION_ROAD_GRAPHICS,24,first,4),
        section(PORTABLE_ASSET_SECTION_MODEL_BINDINGS,1,selected_bindings,96)};
    PortableAssetPackView fallback={.resolved_sections=current,.section_count=6};
    PortableAssetPackView choice={.resolved_sections=donor,.section_count=4};
    const PortableAssetPackView *preferred[3]={&choice,NULL,NULL};
    PortableAssetSectionView rows[16],found;PortableAssetPackView out;PortableModelBindingOverlay owner={0};char error[256];
    CHECK(portable_asset_sources_compose(&fallback,preferred,0,rows,16,&owner,&out,error,sizeof error));
    CHECK(portable_asset_pack_find_section(&out,PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS,1,&found)==PORTABLE_ASSET_PACK_OK&&found.bytes==first);
    CHECK(portable_asset_pack_find_section(&out,PORTABLE_ASSET_SECTION_ITEM_BOX_GRAPHICS,1,&found)==PORTABLE_ASSET_PACK_OK&&found.bytes==last);
    CHECK(portable_asset_pack_find_section(&out,PORTABLE_ASSET_SECTION_ROAD_GRAPHICS,24,&found)==PORTABLE_ASSET_PACK_OK&&found.bytes==last);
    CHECK(get32(owner.bytes+24)==40&&get32(owner.bytes+68)==30);
    uint8_t driver_digest[32];memcpy(driver_digest,out.deterministic_digest,32);
    preferred[0]=NULL;preferred[1]=&choice;preferred[2]=&choice;
    CHECK(portable_asset_sources_compose(&fallback,preferred,0,rows,16,&owner,&out,error,sizeof error));
    CHECK(get32(owner.bytes+24)==20&&get32(owner.bytes+68)==50);
    CHECK(portable_asset_pack_find_section(&out,PORTABLE_ASSET_SECTION_ITEM_BOX_GRAPHICS,1,&found)==PORTABLE_ASSET_PACK_OK&&found.bytes==first);
    CHECK(memcmp(driver_digest,out.deterministic_digest,32));
    PortableAssetPackView empty={0};preferred[0]=&empty;preferred[1]=NULL;preferred[2]=NULL;
    CHECK(portable_asset_sources_compose(&fallback,preferred,1,rows,16,&owner,&out,error,sizeof error));
    CHECK(get32(owner.bytes+4)==1&&get32(owner.bytes+8)==PORTABLE_MODEL_SCENERY);
    CHECK(portable_asset_pack_find_section(&out,PORTABLE_ASSET_SECTION_DRIVER_3D,1,&found)==PORTABLE_ASSET_PACK_NOT_FOUND);
    CHECK(portable_asset_pack_find_section(&out,PORTABLE_ASSET_SECTION_KART_3D,1,&found)==PORTABLE_ASSET_PACK_NOT_FOUND);
    /* An absent replacement retains source art; truncated registries reject. */
    CHECK(portable_asset_pack_find_section(&out,PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS,1,&found)==PORTABLE_ASSET_PACK_OK&&found.bytes==last);
    preferred[0]=&choice;donor[3].size=20;
    CHECK(!portable_asset_sources_compose(&fallback,preferred,0,rows,16,&owner,&out,error,sizeof error));
    donor[3].size=96;
    CHECK(!portable_asset_sources_compose(&fallback,preferred,0,rows,1,&owner,&out,error,sizeof error));
    portable_model_bindings_overlay_close(&owner);
    const PortableAssetPackView *no_preference[3]={0};
    memset(fallback.deterministic_digest,0x5a,32);
    CHECK(portable_asset_sources_compose(&fallback,no_preference,0,rows,16,&owner,&out,error,sizeof error));
    CHECK(!memcmp(fallback.deterministic_digest,out.deterministic_digest,32));
    PortableAssetSectionView base_row=section(PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS,1,first,4);
    PortableAssetPackView sprite_base={.section_count=1,.resolved_sections=&base_row};
    memset(sprite_base.deterministic_digest,0xa5,32);
    no_preference[0]=&sprite_base;
    CHECK(portable_asset_sources_compose(&sprite_base,no_preference,1,rows,16,&owner,&out,error,sizeof error));
    CHECK(out.section_count==1&&!memcmp(sprite_base.deterministic_digest,out.deterministic_digest,32));
    portable_model_bindings_overlay_close(&owner);
    /* A selected binding must bring its own mesh and materials, even when a
     * later mod supplied resources under the same keys. */
    uint8_t mesh_bytes[88]={0},material_a[16]={0},material_b[16]={0};
    memcpy(mesh_bytes,"MSH1",4);put32(mesh_bytes+4,3);put32(mesh_bytes+8,1);put32(mesh_bytes+12,1);
    put32(mesh_bytes+16,90);put32(mesh_bytes+40,65536);put32(mesh_bytes+64,65536);
    mesh_bytes[82]=1;mesh_bytes[84]=2;
    memcpy(material_a,"MTL1",4);put32(material_a+4,0xffff0000u);
    memcpy(material_b,"MTL1",4);put32(material_b+4,0xff0000ffu);
    PortableAssetSectionView resources[]={section(PORTABLE_ASSET_SECTION_MODEL_MESH,40,mesh_bytes,sizeof mesh_bytes),
        section(PORTABLE_ASSET_SECTION_MODEL_MATERIAL,90,material_b,sizeof material_b)};
    PortableAssetSectionView chosen[]={section(PORTABLE_ASSET_SECTION_MODEL_MESH,40,mesh_bytes,sizeof mesh_bytes),
        section(PORTABLE_ASSET_SECTION_MODEL_MATERIAL,90,material_a,sizeof material_a),
        section(PORTABLE_ASSET_SECTION_MODEL_BINDINGS,1,selected_bindings,sizeof selected_bindings)};
    PortableAssetPackView resource_base={.section_count=2,.resolved_sections=resources};
    PortableAssetPackView resource_choice={.section_count=3,.resolved_sections=chosen};
    no_preference[0]=&resource_choice;
    CHECK(portable_asset_sources_compose(&resource_base,no_preference,0,rows,16,&owner,&out,error,sizeof error));
    PortableModelView model;
    CHECK(portable_model_mesh_open(&out,40,&model)&&model.materials[0].argb==0xffff0000u);
    portable_model_bindings_overlay_close(&owner);
    CHECK(portable_game_library_count()==3&&portable_game_library_at(0)->importer==PORTABLE_GAME_BASE);
    CHECK(portable_game_library_at(1)->importer==PORTABLE_GAME_IMPORT&&!strcmp(portable_game_library_at(1)->id,"mario-kart-ds"));
    CHECK(portable_game_library_at(2)->importer==PORTABLE_GAME_IMPORT&&!strcmp(portable_game_library_at(2)->id,"mario-kart-64")&&!portable_game_library_at(3));
    puts("Asset source priority: PASS");return 0;
}
