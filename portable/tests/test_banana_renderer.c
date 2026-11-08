#include "banana_renderer.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>


static int test_tile_boundaries(void)
{
    uint8_t tiles[0x800] = {0};
    uint16_t palette[48] = {0};
    uint32_t pixels[40u * 40u];
    PortableSoftwareFrame frame = {pixels,40u,40u,NULL,1u,0u,0u,NULL};
    PortableRaceParticleAssetView asset = {.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=48u,.tile_base=0x100u,.palette_base=3u};
    palette[16u+1u]=0x001fu; palette[16u+2u]=0x03e0u; palette[16u+3u]=0x7c00u;
    for(unsigned y=0u;y<16u;++y)for(unsigned x=0u;x<16u;++x){
        const unsigned tile=56u+(y/8u)*2u+x/8u;
        const unsigned color=(x%8u==0u && y%8u==0u)?0u:1u+(tile+y)%3u;
        tiles[tile*32u+(y&7u)*4u+(x&7u)/2u]|=(uint8_t)(color<<((x&1u)*4u));
    }
    for(unsigned i=0u;i<1600u;++i)pixels[i]=0x123456u;
    if(!portable_banana_render(&asset,&frame,20,20,0u))return 0;
    for(unsigned y=0u;y<16u;++y)for(unsigned x=0u;x<16u;++x){
        const unsigned tile=56u+(y/8u)*2u+x/8u;
        const unsigned color=(x%8u==0u && y%8u==0u)?0u:1u+(tile+y)%3u;
        const uint32_t expected=color==0u?0x123456u:color==1u?0xff0000u:color==2u?0x00ff00u:0x0000ffu;
        if(pixels[(7u+y)*40u+12u+x]!=expected)return 0;
    }
    return 1;
}


static int test_projected_bounds(void)
{
    uint8_t tiles[0x800]={0};
    uint16_t palette[48]={0};
    uint32_t pixels[160u*120u];
    PortableSoftwareFrame frame={pixels,160u,120u,NULL,1u,0u,0u,NULL};
    PortableRaceParticleAssetView asset={.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=48u,.tile_base=0x100u,.palette_base=3u};
    static const struct {uint16_t depth;int left,top,width;} cases[]={
        {256u,70,64,21},{512u,74,72,11},{1024u,77,77,5}};
    memset(tiles+56u*32u,0x11,128u);palette[16u+1u]=0x001fu;
    for(unsigned n=0u;n<sizeof cases/sizeof cases[0];++n){
        memset(pixels,0,sizeof pixels);
        if(!portable_banana_render_projected(&asset,&frame,80,80,cases[n].depth))return 0;
        for(unsigned y=0u;y<120u;++y)for(unsigned x=0u;x<160u;++x){
            const int inside=(int)x>=cases[n].left && (int)x<cases[n].left+cases[n].width &&
                (int)y>=cases[n].top && (int)y<cases[n].top+cases[n].width;
            if(pixels[y*160u+x]!=(inside?0xff0000u:0u))return 0;
        }
    }
    return !portable_banana_render_projected(&asset,&frame,80,80,0u);
}

int main(void)
{
 if(!test_tile_boundaries() || !test_projected_bounds())return 1;

    uint8_t tiles[0x800];
    uint16_t palette[32];
    uint32_t pixels[40u * 40u];
    PortableSoftwareFrame frame = {pixels,40u,40u,NULL,1u,0u,0u,NULL};
    PortableRaceParticleAssetView asset = {
        .tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=32u,
        .tile_base=0x100u,.palette_base=3u,
    };
    memset(tiles,0,sizeof tiles);
    memset(palette,0,sizeof palette);
    memset(pixels,0,sizeof pixels);
    palette[17]=0x03e0u;
    tiles[(0x138u-0x100u)*32u]=0x01u;
    if(!portable_banana_render(&asset,&frame,20,20,0u) ||
       pixels[7u*40u+12u]!=0x00ff00u || pixels[7u*40u+27u]!=0u){
        fputs("banana renderer test failed\n",stderr);
        return 1;
    }
    puts("banana renderer tests passed");
    return 0;
}
