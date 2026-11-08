#include "shell_cells_fixture.h"
#include "red_shell_renderer.h"
#include <stdio.h>
#include <string.h>

static int test_tile_boundaries(void)
{
    uint8_t tiles[0x800] = {0};
    uint16_t palette[48] = {0};
    uint32_t pixels[40u * 40u];
    PortableSoftwareFrame frame = {pixels,40u,40u,NULL,1u,0u,0u,NULL};
    PortableRaceParticleAssetView asset = {.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=48u,.cells=shell_test_cells(),.cell_bytes=304u,
        .tile_base=0x100u,.palette_base=3u};
    palette[16u+1u]=0x001fu; palette[16u+2u]=0x03e0u; palette[16u+3u]=0x7c00u;
    for(unsigned y=0u;y<16u;++y)for(unsigned x=0u;x<16u;++x){
        const unsigned tile=0u+(y/8u)*2u+x/8u;
        const unsigned color=(x%8u==0u && y%8u==0u)?0u:1u+(tile+y)%3u;
        tiles[tile*32u+(y&7u)*4u+(x&7u)/2u]|=(uint8_t)(color<<((x&1u)*4u));
    }
    for(unsigned i=0u;i<1600u;++i)pixels[i]=0x123456u;
    if(!portable_red_shell_render(&asset,&frame,20,20,0u,0u))return 0;
    for(unsigned y=0u;y<16u;++y)for(unsigned x=0u;x<16u;++x){
        const unsigned tile=0u+(y/8u)*2u+x/8u;
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
        .palette=palette,.palette_count=48u,.cells=shell_test_cells(),.cell_bytes=304u,
        .tile_base=0x100u,.palette_base=3u};
    static const struct {uint16_t depth;int left,top,width;} cases[]={
        {256u,70,64,21},{512u,74,72,11},{1024u,77,77,5}};
    memset(tiles+0u*32u,0x11,128u);palette[16u+1u]=0x001fu;
    memset(tiles+16u*32u,0x11,128u);palette[33u]=0x001fu;
    for(unsigned spiny=0u;spiny<2u;++spiny)
    for(unsigned n=0u;n<sizeof cases/sizeof cases[0];++n){
        memset(pixels,0,sizeof pixels);
        if(!(spiny?portable_spiny_shell_render_projected(&asset,&frame,80,80,1u,cases[n].depth):
            portable_red_shell_render_projected(&asset,&frame,80,80,0u,cases[n].depth)))return 0;
        const int top=cases[n].top-(spiny?(n==0u?2:1):0);
        for(unsigned y=0u;y<120u;++y)for(unsigned x=0u;x<160u;++x){
            const int inside=(int)x>=cases[n].left && (int)x<cases[n].left+cases[n].width &&
                (int)y>=top && (int)y<top+cases[n].width;
            if(pixels[y*160u+x]!=(inside?0xff0000u:0u))return 0;
        }
    }
    return !portable_red_shell_render_projected(&asset,&frame,80,80,0u,0u);
}

int main(void){
 if(!test_tile_boundaries() || !test_projected_bounds())return 1;

 uint8_t tiles[0x300];uint16_t palette[48];uint32_t pixels[1600];
 PortableSoftwareFrame frame={pixels,40u,40u,NULL,1u,0u,0u,NULL};
 PortableRaceParticleAssetView asset={
  .tiles=tiles,.tile_bytes=sizeof tiles,.palette=palette,.palette_count=48u,
  .cells=shell_test_cells(),.cell_bytes=304u,
        .tile_base=0x100u,.palette_base=3u};
 memset(tiles,0,sizeof tiles);memset(palette,0,sizeof palette);memset(pixels,0,sizeof pixels);
 palette[17]=0x001fu;tiles[0]=1u;
 if(!portable_red_shell_render(&asset,&frame,20,20,0u,0u)||
    pixels[7u*40u+12u]!=0xff0000u||
    portable_red_shell_render(&asset,&frame,20,20,6u,0u)){
  fputs("red shell renderer test failed\n",stderr);return 1;}
 /* Spiny uses tile10C, palette5 and a one-pixel vertical offset on
  * frames1..4. The mirrored last frame flips the same source pixel. */
 palette[33]=0x03e0u;tiles[12u*32u]=1u;memset(pixels,0,sizeof pixels);
 if(!portable_spiny_shell_render(&asset,&frame,20,20,0u,0u)||
    pixels[7u*40u+12u]!=0x00ff00u ||
    !portable_spiny_shell_render(&asset,&frame,20,20,5u,0u)||
    pixels[7u*40u+27u]!=0x00ff00u){fputs("Spiny art test failed\n",stderr);return 1;}
 tiles[16u*32u]=1u;memset(pixels,0,sizeof pixels);
 if(!portable_spiny_shell_render(&asset,&frame,20,20,1u,0u)||
    pixels[6u*40u+12u]!=0x00ff00u || portable_spiny_shell_render(&asset,&frame,20,20,6u,0u))return 1;
 puts("red/Spiny shell renderer tests passed");return 0;
}
