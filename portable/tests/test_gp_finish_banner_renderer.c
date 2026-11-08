#include "gp_finish_banner_renderer.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"check failed line %d: %s\n",\
    __LINE__,#x);return __LINE__;}}while(0)

static int physical_banner_pixels(void)
{
    uint8_t tiles[PORTABLE_GP_FINISH_BANNER_TILE_BYTES]={0};
    uint16_t palette[PORTABLE_GP_FINISH_BANNER_PALETTE_COUNT]={0};palette[17]=31u;palette[18]=31u<<5u;
    const uint8_t cells[PORTABLE_GP_FINISH_BANNER_CELL_BYTES]={
        1,0,0xf0,0x40,0xe0,0xc1,0xa0,0x14,1,0,0xf0,0x40,0xe0,0xc1,0xc0,0x24};
    const PortableGpFinishBannerAssetView asset={tiles,sizeof tiles,palette,
        PORTABLE_GP_FINISH_BANNER_PALETTE_COUNT,cells,sizeof cells,0xa0u,120u,63u};
    for(unsigned y=0;y<32u;++y)for(unsigned x=0;x<64u;++x){
        const unsigned index=(x+2u*y)%3u;
        const size_t offset=((y/8u)*8u+x/8u)*32u+(y&7u)*4u+(x&7u)/2u;
        tiles[offset]|=(uint8_t)(index<<((x&1u)*4u));
    }
    static uint32_t pixels[963u*640u],native[240u*160u];
    for(unsigned n=0;n<2u;++n)for(unsigned scale=1;scale<=4u;scale*=4u){
        const unsigned inverse=n?385u:193u,stride=240u*scale+3u;
        PortableSoftwareFrame frame={pixels,240u,160u,NULL,(uint8_t)scale,stride,0u,NULL};
        for(unsigned i=0;i<stride*160u*scale;++i)pixels[i]=0x123456u;
        CHECK(portable_gp_finish_banner_render(&asset,&frame,0u,(uint16_t)inverse,120,63));
        const unsigned width=(64u*256u*scale+inverse-1u)/inverse,height=(32u*256u*scale+inverse-1u)/inverse;
        const int left=120*(int)scale-(int)width/2,top=63*(int)scale-(int)height/2;
        unsigned differs=0u;
        for(unsigned y=0;y<160u*scale;++y)for(unsigned x=0;x<stride;++x){
            uint32_t expected=0x123456u;
            const int dx=(int)x-left,dy=(int)y-top;
            if(x<240u*scale && dx>=0 && dy>=0 && dx<(int)width && dy<(int)height){
                const unsigned sx=(unsigned)dx*inverse/(256u*scale),sy=(unsigned)dy*inverse/(256u*scale);
                const unsigned index=(sx+2u*sy)%3u;
                if(index)expected=index==1u?0xff0000u:0x00ff00u;
            }
            CHECK(pixels[y*stride+x]==expected);
            if(x<240u*scale){
                if(scale==1u)native[y*240u+x]=pixels[y*stride+x];
                else differs+=pixels[y*stride+x]!=native[(y/scale)*240u+x/scale];
            }
        }
        CHECK(scale==1u || differs>0u);
    }
    return 0;
}

int main(void)
{
    CHECK(physical_banner_pixels()==0);
    uint8_t tiles[PORTABLE_GP_FINISH_BANNER_TILE_BYTES];
    uint16_t palette[PORTABLE_GP_FINISH_BANNER_PALETTE_COUNT]={0};
    const uint8_t cells[PORTABLE_GP_FINISH_BANNER_CELL_BYTES]={
        1,0,0xf0,0x40,0xe0,0xc1,0xa0,0x14,
        1,0,0xf0,0x40,0xe0,0xc1,0xc0,0x24};
    uint32_t pixels[240u*160u];
    PortableSoftwareFrame frame={pixels,240u,160u,NULL,1u,0u,0u,NULL};
    PortableGpFinishBannerAssetView asset={
        tiles,sizeof tiles,palette,PORTABLE_GP_FINISH_BANNER_PALETTE_COUNT,
        cells,sizeof cells,0xa0u,120u,63u};
    memset(tiles,0x11,sizeof tiles);palette[17]=UINT16_C(0x001f);
    palette[33]=UINT16_C(0x03e0);memset(pixels,0,sizeof pixels);
    CHECK(portable_gp_finish_banner_render(&asset,&frame,0u,0x100u,120,63));
    CHECK(pixels[47u*240u+88u]==0xff0000u);
    CHECK(pixels[78u*240u+151u]==0xff0000u);
    memset(pixels,0,sizeof pixels);
    CHECK(portable_gp_finish_banner_render(&asset,&frame,1u,0x200u,120,63));
    CHECK(pixels[55u*240u+104u]==0x00ff00u);
    CHECK(pixels[70u*240u+135u]==0x00ff00u);
    CHECK(pixels[54u*240u+103u]==0u);
    CHECK(!portable_gp_finish_banner_render(&asset,&frame,2u,0x100u,120,63));
    puts("GP finish banner renderer: PASS variants=2 size=64x32 affine=100/200");
    return 0;
}
