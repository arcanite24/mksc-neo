#include "boo_attack_renderer.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Boo render failed:%d\n",__LINE__);return 1;}}while(0)
int main(void)
{
    uint8_t tiles[0x2000]={0};uint16_t palette[80]={0};uint32_t pixels[40*40]={0};
    PortableSoftwareFrame frame={pixels,40u,40u,NULL,1u,0u,0u,NULL};
    const PortableRaceParticleAssetView asset={.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=80u,.tile_base=0x100u,.palette_base=3u};
    tiles[(0x190u-0x100u)*32u]=1u;palette[65]=31u;
    CHECK(portable_boo_attack_render(&asset,&frame,20,20,0u,0u));
    CHECK(pixels[4*40+4]==0xff0000u && pixels[4*40+35]==0u);
    memset(pixels,0,sizeof pixels);
    CHECK(portable_boo_attack_render(&asset,&frame,20,20,1u,1u));
    CHECK(pixels[35*40+35]==0xff0000u && pixels[4*40+4]==0u);
    CHECK(tiles[(0x190u-0x100u)*32u]==1u && palette[65]==31u);
    PortableRaceParticleAssetView bad=asset;bad.palette_count=64u;
    CHECK(!portable_boo_attack_render(&bad,&frame,20,20,0u,0u));
    puts("Boo item renderer: PASS");return 0;
}
