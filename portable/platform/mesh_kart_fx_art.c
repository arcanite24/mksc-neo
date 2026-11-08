#include "mesh_kart_fx_art.h"
#include "race_particle_asset.h"
#include "race_particle_renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_CAP 12u
#define CELL_LIMIT 256u
#define UNTOUCHED UINT32_C(0x01000000)
typedef struct
{
    PortableRenderVertex vertices[4];
    PortableRenderMaterial material;
    PortableRenderRun run;
    PortableRenderMesh mesh;
    uint8_t *pixels;
    uint16_t duration;
} FxFrame;
struct PortableMeshKartFxArt
{
    FxFrame frames[2][FRAME_CAP];
    uint32_t counts[2];
    uint16_t lifetimes[2];
};
static const uint32_t indices[6] = {0,1,2,0,2,3};
static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1]<<8u);
}
static int signed_bits(unsigned value, unsigned sign)
{
    return value & sign ? (int)value-(int)(sign*2u) : (int)value;
}
static int fail(char *error, size_t capacity, unsigned kind, const char *detail)
{
    if (error && capacity)
        snprintf(error,capacity,"3D kart FX %s: %s",kind ? "flame" : "spark",detail);
    return 0;
}
void portable_mesh_kart_fx_art_close(PortableMeshKartFxArt *owner)
{
    if (!owner) return;
    for (unsigned kind=0;kind<2u;++kind)
        for (unsigned frame=0;frame<FRAME_CAP;++frame) free(owner->frames[kind][frame].pixels);
    free(owner);
}
static int cell_bounds(const PortableRaceParticleAssetView *view, unsigned frame,
                        int *left, int *top, unsigned *width, unsigned *height)
{
    static const unsigned widths[3][4]={{8,16,32,64},{16,32,32,64},{8,8,16,32}};
    static const unsigned heights[3][4]={{8,16,32,64},{8,8,16,32},{16,32,32,64}};
    const PortableRaceParticleFrame *f=&view->frames[frame];
    const uint8_t *cell=view->cells+f->offset;
    const unsigned count=u16(cell);
    if (!count || f->size!=2u+(size_t)count*6u) return 0;
    int lo_x=512,lo_y=256,hi_x=-512,hi_y=-256;
    for (unsigned i=0;i<count;++i)
    {
        const uint8_t *p=cell+2u+i*6u;
        const unsigned a0=u16(p),a1=u16(p+2u),a2=u16(p+4u);
        const unsigned shape=a0>>14u,size=a1>>14u;
        if (shape>2u) return 0;
        const unsigned w=widths[shape][size],h=heights[shape][size];
        const unsigned tile=a2&1023u,bank=a2>>12u;
        if (tile<view->tile_base || bank<view->palette_base ||
            (size_t)(tile-view->tile_base)*32u+(size_t)w*h/2u>view->tile_bytes ||
            (size_t)(bank-view->palette_base)*16u+15u>=view->palette_count) return 0;
        const int x=signed_bits(a1&511u,256u),y=signed_bits(a0&255u,128u);
        if (x<lo_x) lo_x=x;
        if (y<lo_y) lo_y=y;
        if (x+(int)w>hi_x) hi_x=x+(int)w;
        if (y+(int)h>hi_y) hi_y=y+(int)h;
    }
    if (hi_x<=lo_x || hi_y<=lo_y || hi_x-lo_x>(int)CELL_LIMIT || hi_y-lo_y>(int)CELL_LIMIT)
        return 0;
    *left=lo_x;*top=lo_y;*width=(unsigned)(hi_x-lo_x);*height=(unsigned)(hi_y-lo_y);
    return 1;
}
static int bake(FxFrame *out, const PortableRaceParticleAssetView *view, unsigned frame)
{
    int left,top;
    unsigned width,height;
    if (!cell_bounds(view,frame,&left,&top,&width,&height)) return 0;
    const size_t count=(size_t)width*height;
    uint32_t *argb=malloc(count*sizeof *argb);
    out->pixels=malloc(count*4u);
    if (!argb || !out->pixels) {free(argb);return 0;}
    for (size_t i=0;i<count;++i) argb[i]=UNTOUCHED;
    PortableSoftwareFrame canvas;
    PortableRaceParticleAssetView one=*view;
    one.frames=&view->frames[frame];one.frame_count=1;
    if (!portable_software_frame_init(&canvas,argb,width,height) ||
        !portable_race_particle_render(&one,&canvas,-left,-top,2u))
    {free(argb);return 0;}
    for (size_t i=0;i<count;++i)
    {
        const uint32_t color=argb[i]==UNTOUCHED ? 0u : argb[i]|UINT32_C(0xff000000);
        for (unsigned byte=0;byte<4u;++byte) out->pixels[i*4u+byte]=(uint8_t)(color>>(byte*8u));
    }
    free(argb);
    out->duration=view->frames[frame].duration;
    out->material=(PortableRenderMaterial){.argb=UINT32_MAX,.flags=4u|16u,.alpha_cutoff_q16=1,
        .texture={.pixels=out->pixels,.width=(uint16_t)width,.height=(uint16_t)height,
                  .anchor_x=(int16_t)left,.anchor_y=(int16_t)top}};
    const double x0=left*.05,x1=(left+(int)width)*.05;
    const double z0=-(top+(int)height)*.05,z1=-top*.05;
    out->vertices[0]=(PortableRenderVertex){.position={x0,0,z0},.uv={0,1}};
    out->vertices[1]=(PortableRenderVertex){.position={x1,0,z0},.uv={1,1}};
    out->vertices[2]=(PortableRenderVertex){.position={x1,0,z1},.uv={1,0}};
    out->vertices[3]=(PortableRenderVertex){.position={x0,0,z1},.uv={0,0}};
    out->run=(PortableRenderRun){0,6,0};
    out->mesh=(PortableRenderMesh){.vertices=out->vertices,.indices=indices,.materials=&out->material,
        .runs=&out->run,.vertex_count=4,.index_count=6,.material_count=1,.run_count=1};
    return 1;
}
int portable_mesh_kart_fx_art_open(const PortableAssetPackView *pack, PortableMeshKartFxArt **owner,
                                  char *error, size_t capacity)
{
    if (!pack || !owner) return fail(error,capacity,0,"invalid pack/owner");
    PortableMeshKartFxArt *next=calloc(1,sizeof *next);
    PortableRaceParticleStorage *storage=malloc(sizeof *storage);
    if (!next || !storage)
    {free(storage);portable_mesh_kart_fx_art_close(next);return fail(error,capacity,0,"allocation failed");}
    for (unsigned kind=0;kind<2u;++kind)
    {
        PortableAssetSectionView section;
        const PortableAssetPackStatus found=portable_asset_pack_find_section(pack,
            PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS,kind+1u,&section);
        if (found==PORTABLE_ASSET_PACK_NOT_FOUND) continue;
        PortableRaceParticleAssetView view;
        const unsigned frames=kind ? PORTABLE_MUSHROOM_BURST_FRAME_COUNT : PORTABLE_DRIFT_SPARK_FRAME_COUNT;
        const unsigned bytes=kind ? PORTABLE_MUSHROOM_BURST_CELL_BYTES : PORTABLE_DRIFT_SPARK_CELL_BYTES;
        if (found!=PORTABLE_ASSET_PACK_OK ||
            !portable_race_particle_asset_open(&section,storage,frames,bytes,&view))
        {free(storage);portable_mesh_kart_fx_art_close(next);return fail(error,capacity,kind,"FXC1 invalid");}
        next->counts[kind]=(uint32_t)view.frame_count;
        unsigned lifetime=2;
        for (unsigned i=0;i<view.frame_count;++i)
        {
            if (!bake(&next->frames[kind][i],&view,i))
            {free(storage);portable_mesh_kart_fx_art_close(next);return fail(error,capacity,kind,"cell bounds/pixels invalid");}
            lifetime+=(unsigned)view.frames[i].duration+1u;
        }
        next->lifetimes[kind]=(uint16_t)lifetime;
    }
    free(storage);portable_mesh_kart_fx_art_close(*owner);*owner=next;return 1;
}
const PortableRenderMesh *portable_mesh_kart_fx_art_frame(const PortableMeshKartFxArt *owner,
                                                         unsigned kind, uint16_t age)
{
    if (!owner || kind>1u || age<2u) return NULL;
    unsigned first=2;
    for (unsigned i=0;i<owner->counts[kind];++i)
    {
        first+=(unsigned)owner->frames[kind][i].duration+1u;
        if (age<first) return &owner->frames[kind][i].mesh;
    }
    return NULL;
}
uint32_t portable_mesh_kart_fx_art_frame_count(const PortableMeshKartFxArt *owner,unsigned kind)
{
    return owner && kind<2u ? owner->counts[kind] : 0u;
}
uint16_t portable_mesh_kart_fx_art_lifetime(const PortableMeshKartFxArt *owner,unsigned kind)
{
    return owner && kind<2u ? owner->lifetimes[kind] : 0u;
}
