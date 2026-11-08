#include "mesh_kart_fx_art.h"
#include "race_particle_asset.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) {fprintf(stderr,"mesh kart FX line %d: %s\n",__LINE__,#c);return 1;} } while (0)
#define SPARK_BYTES (32u+5u*8u+8192u+160u+160u)
#define FLAME_BYTES (32u+12u*8u+8192u+160u+528u)
typedef struct
{
    uint8_t spark[SPARK_BYTES],flame[FLAME_BYTES];
    PortableAssetSectionView sections[2];
    PortableAssetPackView pack;
} Fixture;
static void w16(uint8_t *p,uint16_t value)
{p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8u);}
static void w32(uint8_t *p,uint32_t value)
{for(unsigned i=0;i<4u;++i)p[i]=(uint8_t)(value>>(i*8u));}
static void effect(uint8_t *bytes,unsigned frames,unsigned cells)
{
    const uint32_t header[8]={UINT32_C(0x31435846),8192,80,frames,256,3,cells,0};
    for(unsigned i=0;i<8u;++i)w32(bytes+i*4u,header[i]);
    uint8_t *tiles=bytes+32u+frames*8u;
    uint8_t *palette=tiles+8192u,*definitions=palette+160u;
    w16(palette+2u,31);w16(palette+4u,UINT16_C(0x03e0));
    /* Nonzero index3 contains opaque black, distinct from transparent index0. */
    for(unsigned i=0;i<frames;++i)
    {
        w32(bytes+32u+i*8u,i*8u);w16(bytes+36u+i*8u,8);w16(bytes+38u+i*8u,1);
        memset(tiles+i*32u,i&1u?0x22:0x11,32u);
        w16(definitions+i*8u,1);w16(definitions+i*8u+2u,248);
        w16(definitions+i*8u+4u,508);w16(definitions+i*8u+6u,(uint16_t)(UINT16_C(0x3100)+i));
    }
    tiles[0]=0x10;tiles[1]=0x33;
}
static void fixture(Fixture *f)
{
    memset(f,0,sizeof *f);effect(f->spark,5,160);effect(f->flame,12,528);
    f->sections[0]=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS,
        .content_id=1,.version=1,.bytes=f->spark,.size=sizeof f->spark};
    f->sections[1]=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS,
        .content_id=2,.version=1,.bytes=f->flame,.size=sizeof f->flame};
    f->pack=(PortableAssetPackView){.resolved_sections=f->sections,.section_count=2};
}
static int decoded_owned_frames(void)
{
    Fixture f;fixture(&f);PortableMeshKartFxArt *owner=NULL;
    CHECK(portable_mesh_kart_fx_art_open(&f.pack,&owner,NULL,0));
    CHECK(portable_mesh_kart_fx_art_frame_count(owner,0)==5);
    CHECK(portable_mesh_kart_fx_art_frame_count(owner,1)==12);
    CHECK(portable_mesh_kart_fx_art_lifetime(owner,0)==12);
    CHECK(portable_mesh_kart_fx_art_lifetime(owner,1)==26);
    const PortableRenderMesh *first=portable_mesh_kart_fx_art_frame(owner,0,2);
    const PortableRenderMesh *second=portable_mesh_kart_fx_art_frame(owner,0,4);
    CHECK(first && second && first!=second);
    CHECK(first==portable_mesh_kart_fx_art_frame(owner,0,3));
    CHECK(!portable_mesh_kart_fx_art_frame(owner,0,0) && !portable_mesh_kart_fx_art_frame(owner,0,1));
    CHECK(!portable_mesh_kart_fx_art_frame(owner,0,12) && !portable_mesh_kart_fx_art_frame(owner,1,26));
    CHECK(!portable_mesh_kart_fx_art_frame(owner,2,2) && !portable_mesh_kart_fx_art_frame(owner,0,UINT16_MAX));
    CHECK(first->material_count==1 && first->vertex_count==4 && first->index_count==6);
    const PortableNativeImage *image=&first->materials[0].texture;
    CHECK(image->width==8 && image->height==8 && image->anchor_x==-4 && image->anchor_y==-8);
    CHECK(portable_native_image_pixel(image,0,0)==0);
    CHECK(portable_native_image_pixel(image,1,0)==UINT32_C(0xffff0000));
    CHECK(portable_native_image_pixel(image,2,0)==UINT32_C(0xff000000));
    CHECK(portable_native_image_pixel(&second->materials[0].texture,0,0)==UINT32_C(0xff00ff00));
    CHECK(fabs(first->vertices[0].position[0]+.2)<1e-12);
    CHECK(fabs(first->vertices[0].position[2])<1e-12);
    CHECK(fabs(first->vertices[2].position[2]-.4)<1e-12);
    memset(f.spark,0,sizeof f.spark);memset(f.flame,0,sizeof f.flame);
    CHECK(first==portable_mesh_kart_fx_art_frame(owner,0,2));
    CHECK(portable_native_image_pixel(image,1,0)==UINT32_C(0xffff0000));
    portable_mesh_kart_fx_art_close(owner);return 0;
}
static int rollback_and_missing(void)
{
    Fixture f;fixture(&f);PortableMeshKartFxArt *owner=NULL;char error[128];
    CHECK(portable_mesh_kart_fx_art_open(&f.pack,&owner,error,sizeof error));
    PortableMeshKartFxArt *before=owner;
    const PortableRenderMesh *mesh=portable_mesh_kart_fx_art_frame(owner,0,2);
    w16(f.flame+32u+12u*8u+8192u+160u+6u,UINT16_C(0x3000));
    CHECK(!portable_mesh_kart_fx_art_open(&f.pack,&owner,error,sizeof error));
    CHECK(owner==before && strstr(error,"flame") && mesh==portable_mesh_kart_fx_art_frame(owner,0,2));
    fixture(&f);w32(f.spark+32u,160);
    CHECK(!portable_mesh_kart_fx_art_open(&f.pack,&owner,error,sizeof error) && owner==before);
    fixture(&f);f.sections[0].size=UINT64_C(1)<<32;
    CHECK(!portable_mesh_kart_fx_art_open(&f.pack,&owner,error,sizeof error) && owner==before);
    fixture(&f);f.pack.section_count=0;
    CHECK(portable_mesh_kart_fx_art_open(&f.pack,&owner,error,sizeof error));
    CHECK(!portable_mesh_kart_fx_art_frame_count(owner,0) && !portable_mesh_kart_fx_art_frame(owner,1,2));
    CHECK(!portable_mesh_kart_fx_art_lifetime(owner,2));
    CHECK(!portable_mesh_kart_fx_art_open(NULL,&owner,error,sizeof error));
    CHECK(!portable_mesh_kart_fx_art_open(&f.pack,NULL,error,sizeof error));
    CHECK(!portable_mesh_kart_fx_art_frame(NULL,0,2));
    CHECK(!portable_mesh_kart_fx_art_frame_count(NULL,0));
    portable_mesh_kart_fx_art_close(owner);portable_mesh_kart_fx_art_close(NULL);return 0;
}
int main(void)
{
    CHECK(!decoded_owned_frames() && !rollback_and_missing());
    puts("Mesh kart FX art: PASS source cells, source age spans, immutable meshes, anchors, rollback");
    return 0;
}
