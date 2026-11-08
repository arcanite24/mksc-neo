#include "item_box_renderer.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>


static int test_projected_bounds(void)
{
    uint8_t tiles[PORTABLE_ITEM_BOX_TILE_BYTES];
    uint16_t palette[16]={0u,0x001fu};
    uint32_t pixels[240u*160u];
    PortableSoftwareFrame frame={pixels,240u,160u,NULL,1u,0u,0u,NULL};
    PortableItemBoxAssetView asset={tiles,sizeof tiles,palette,16u,8u,32u,32u};
    static const struct {uint16_t depth;int left,top,width;} cases[]={
        {256u,98,59,45},{512u,108,80,23},{1024u,114,91,11}};
    memset(tiles,0x11,sizeof tiles);
    for(unsigned n=0u;n<sizeof cases/sizeof cases[0];++n){
        memset(pixels,0,sizeof pixels);
        if(!portable_item_box_render_projected(&asset,&frame,120,100,0u,cases[n].depth))return 0;
        unsigned count=0u;
        for(unsigned y=0u;y<160u;++y)for(unsigned x=0u;x<240u;++x){
            const int inside=(int)x>=cases[n].left && (int)x<cases[n].left+cases[n].width &&
                (int)y>=cases[n].top && (int)y<cases[n].top+cases[n].width;
            if(pixels[y*240u+x]!=(inside?0xff0000u:0u))return 0;
            count+=(unsigned)inside;
        }
        if(count!=(unsigned)(cases[n].width*cases[n].width))return 0;
    }
    return !portable_item_box_render_projected(&asset,&frame,120,100,0u,0u);
}

static int test_tile_boundaries(void)
{
    uint8_t tiles[PORTABLE_ITEM_BOX_TILE_BYTES]={0};
    uint16_t palette[16]={0u,0x001fu,0x03e0u,0x7c00u};
    uint32_t pixels[64u*64u];
    PortableSoftwareFrame frame={pixels,64u,64u,NULL,1u,0u,0u,NULL};
    PortableItemBoxAssetView asset={tiles,sizeof tiles,palette,16u,8u,32u,32u};
    for(unsigned y=0u;y<32u;++y)for(unsigned x=0u;x<32u;++x){
        const unsigned tile=(y/8u)*4u+x/8u;
        const unsigned color=(x%8u==0u && y%8u==0u)?0u:1u+(tile+y)%3u;
        tiles[tile*32u+(y&7u)*4u+(x&7u)/2u]|=(uint8_t)(color<<((x&1u)*4u));
    }
    for(unsigned i=0u;i<64u*64u;++i)pixels[i]=0x123456u;
    if(!portable_item_box_render(&asset,&frame,32,32,0u))return 0;
    for(unsigned y=0u;y<32u;++y)for(unsigned x=0u;x<32u;++x){
        const unsigned tile=(y/8u)*4u+x/8u;
        const unsigned color=(x%8u==0u && y%8u==0u)?0u:1u+(tile+y)%3u;
        const uint32_t expected=color==0u?0x123456u:color==1u?0xff0000u:color==2u?0x00ff00u:0x0000ffu;
        if(pixels[(2u+y)*64u+16u+x]!=expected)return 0;
    }
    return 1;
}

static int test_physical_perspective(void)
{
    uint8_t tiles[PORTABLE_ITEM_BOX_TILE_BYTES]={0};
    uint16_t palette[16]={0u,31u,31u<<5u};
    uint32_t pixels[260u*256u];
    const PortableItemBoxAssetView asset={tiles,sizeof tiles,palette,16u,8u,32u,32u};
    for(unsigned y=0;y<32u;++y)for(unsigned x=0;x<32u;++x){
        const unsigned index=(x+2u*y)%3u;
        const size_t offset=((y/8u)*4u+x/8u)*32u+(y&7u)*4u+(x&7u)/2u;
        tiles[offset]|=(uint8_t)(index<<((x&1u)*4u));
    }
    const uint16_t depths[]={257u,513u,1025u};
    for(unsigned scale=1u;scale<=4u;scale*=2u)for(unsigned n=0;n<3u;++n){
        const size_t stride=64u*scale+3u;
        PortableSoftwareFrame frame={pixels,64u,64u,NULL,(uint8_t)scale,stride,0u,NULL};
        for(size_t i=0;i<stride*64u*scale;++i)pixels[i]=0x123456u;
        if(!portable_item_box_render_projected(&asset,&frame,32,48,0u,depths[n]))return 0;
        const int inverse=depths[n]*182/256,canvas=inverse<256?64:32;
        const int left=32*(int)scale+256*(int)scale/inverse-(int)scale-canvas*(int)scale/2;
        const int top=48*(int)scale-14*256*(int)scale/inverse-canvas*(int)scale/2;
        unsigned non_block=0u;
        for(unsigned y=0;y<64u*scale;++y)for(unsigned x=0;x<stride;++x){
            uint32_t expected=0x123456u;
            const int dx=(int)x-left,dy=(int)y-top;
            if(x<64u*scale && dx>=0 && dy>=0 && dx<canvas*(int)scale && dy<canvas*(int)scale){
                const int sx=16+(int)portable_software_frame_floor_div((int64_t)(dx-canvas*(int)scale/2)*inverse,256*(int)scale);
                const int sy=16+(int)portable_software_frame_floor_div((int64_t)(dy-canvas*(int)scale/2)*inverse,256*(int)scale);
                if(sx>=0 && sx<32 && sy>=0 && sy<32){
                    const unsigned index=(unsigned)(sx+2*sy)%3u;
                    if(index)expected=index==1u?0xff0000u:0x00ff00u;
                }
            }
            if(pixels[y*stride+x]!=expected)return 0;
            if(scale>1u && x<64u*scale && pixels[y*stride+x]!=pixels[(y/scale*scale)*stride+x/scale*scale])++non_block;
        }
        if(scale>1u && !non_block)return 0;
    }
    return 1;
}

int main(void)
{
    if(!test_projected_bounds() || !test_tile_boundaries() || !test_physical_perspective())return 1;

    uint8_t tiles[PORTABLE_ITEM_BOX_TILE_BYTES];
    uint16_t palette[PORTABLE_ITEM_BOX_PALETTE_COUNT];
    uint32_t pixels[64u * 64u];
    PortableSoftwareFrame frame = {pixels,64u,64u,NULL,1u,0u,0u,NULL};
    PortableItemBoxAssetView asset = {
        tiles,sizeof tiles,palette,PORTABLE_ITEM_BOX_PALETTE_COUNT,
        PORTABLE_ITEM_BOX_FRAME_COUNT,32u,32u};

    memset(tiles,0,sizeof tiles);
    memset(palette,0,sizeof palette);
    palette[1]=0x001fu;
    tiles[0]=0x01u;
    tiles[PORTABLE_ITEM_BOX_FRAME_BYTES]=0x10u;
    memset(pixels,0,sizeof pixels);
    if(!portable_item_box_render(&asset,&frame,32,32,0u) ||
       pixels[2u*64u+16u]!=0xff0000u || pixels[2u*64u+17u]!=0u)
        goto fail;
    memset(pixels,0,sizeof pixels);
    if(!portable_item_box_render(&asset,&frame,32,32,4u) ||
       pixels[2u*64u+16u]!=0u || pixels[2u*64u+17u]!=0xff0000u)
        goto fail;
    asset.frame_count=7u;
    if(portable_item_box_render(&asset,&frame,32,32,0u))goto fail;
    puts("item box renderer tests passed");
    return 0;
fail:
    fputs("item box renderer test failed\n",stderr);
    return 1;
}
