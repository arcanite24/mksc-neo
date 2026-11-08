#include "gp_finish_banner_renderer.h"

static uint16_t le16(const uint8_t *p)
{return (uint16_t)p[0]|(uint16_t)((uint16_t)p[1]<<8u);}

static uint32_t rgb(uint16_t color)
{
    return ((uint32_t)(color&31u)*255u/31u<<16)|
           ((uint32_t)((color>>5)&31u)*255u/31u<<8)|
           (uint32_t)((color>>10)&31u)*255u/31u;
}

int portable_gp_finish_banner_render(
    const PortableGpFinishBannerAssetView *asset,
    PortableSoftwareFrame *frame,uint8_t variant,uint16_t affine_scale,
    int32_t origin_x,int32_t origin_y)
{
    const uint8_t *cell;
    uint16_t attr0,attr1,attr2;
    uint32_t width,height,palette_base,source_tile,dw,dh;
    if(asset==NULL||frame==NULL||frame->pixels==NULL||asset->tiles==NULL||
       asset->palette==NULL||asset->cells==NULL||
       asset->tile_bytes!=PORTABLE_GP_FINISH_BANNER_TILE_BYTES||
       asset->palette_count!=PORTABLE_GP_FINISH_BANNER_PALETTE_COUNT||
       asset->cell_bytes!=PORTABLE_GP_FINISH_BANNER_CELL_BYTES||
       asset->tile_base!=0xa0u||variant>=PORTABLE_GP_FINISH_BANNER_VARIANT_COUNT||
       affine_scale==0u)return 0;
    cell=asset->cells+(size_t)variant*8u;
    if(le16(cell)!=1u)return 0;
    attr0=le16(cell+2u);attr1=le16(cell+4u);attr2=le16(cell+6u);
    width=((attr0>>14u)&3u)==1u&&((attr1>>14u)&3u)==3u?64u:0u;
    height=width!=0u?32u:0u;
    source_tile=attr2&0x03ffu;
    palette_base=(uint32_t)(attr2>>12u)*16u;
    if(width==0u||source_tile<asset->tile_base||
       source_tile-asset->tile_base+32u>asset->tile_bytes/32u||
       palette_base+15u>=asset->palette_count)return 0;
    const uint32_t output_scale=(uint32_t)portable_software_frame_scale(frame);
    dw=(width*UINT32_C(0x100)*output_scale+affine_scale-1u)/affine_scale;
    dh=(height*UINT32_C(0x100)*output_scale+affine_scale-1u)/affine_scale;
    const int64_t left=(int64_t)origin_x*output_scale-dw/2u;
    const int64_t top=(int64_t)origin_y*output_scale-dh/2u;
    const int64_t available_x=(int64_t)frame->width*output_scale-left;
    const int64_t available_y=(int64_t)frame->height*output_scale-top;
    if(available_x<=0 || available_y<=0 || left+(int64_t)dw<=0 || top+(int64_t)dh<=0)return 1;
    const uint32_t first_x=left<0?(uint32_t)(-left):0u,first_y=top<0?(uint32_t)(-top):0u;
    const uint32_t last_x=(uint64_t)available_x<dw?(uint32_t)available_x:dw;
    const uint32_t last_y=(uint64_t)available_y<dh?(uint32_t)available_y:dh;
    for(uint32_t y=first_y;y<last_y;++y)for(uint32_t x=first_x;x<last_x;++x){
        const uint32_t sx=x*affine_scale/(256u*output_scale);
        const uint32_t sy=y*affine_scale/(256u*output_scale);
        const size_t tile=(size_t)(source_tile-asset->tile_base)+
            (size_t)(sy/8u)*8u+sx/8u;
        const size_t offset=tile*32u+(sy&7u)*4u+(sx&7u)/2u;
        const uint8_t packed=asset->tiles[offset];
        const uint8_t color=(sx&1u)!=0u?packed>>4u:packed&15u;
        const int32_t dx=(int32_t)(left+x);
        const int32_t dy=(int32_t)(top+y);
        if(color!=0u&&dx>=0&&dy>=0&&dx<(int32_t)(frame->width*output_scale)&&
           dy<(int32_t)(frame->height*output_scale))
            portable_software_frame_store_physical(frame,(size_t)dx,(size_t)dy, rgb(asset->palette[palette_base+color]));
    }
    return 1;
}
