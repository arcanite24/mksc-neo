#include "shell_cells_fixture.h"
#include "green_shell_renderer.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int test_frames_and_mirror(void)
{
    uint8_t tiles[0x180];
    uint16_t palette[16];
    uint32_t pixels[40u * 40u];
    PortableSoftwareFrame frame = {pixels,40u,40u,NULL,1u,0u,0u,NULL};
    PortableRaceParticleAssetView asset = {
        .tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=16u,
        .cells=shell_test_cells(),.cell_bytes=304u,
        .tile_base=0x100u,.palette_base=3u,
    };
    memset(tiles,0,sizeof tiles);
    memset(palette,0,sizeof palette);
    palette[1]=0x001fu;
    /* A single opaque left-edge pixel in frame 0. */
    tiles[0]=0x01u;
    memset(pixels,0,sizeof pixels);
    if(!portable_green_shell_render(&asset,&frame,20,20,0u,0u)||
       pixels[7u*40u+12u]!=0xff0000u||pixels[7u*40u+27u]!=0u)
        return 0;
    memset(pixels,0,sizeof pixels);
    if(!portable_green_shell_render(&asset,&frame,20,20,5u,0x100u)||
       pixels[7u*40u+12u]!=0u||pixels[7u*40u+27u]!=0xff0000u)
        return 0;
    return !portable_green_shell_render(&asset,&frame,20,20,6u,0x100u);
}


static int test_tile_boundaries(void)
{
    uint8_t tiles[0x800] = {0};
    uint16_t palette[48] = {0};
    uint32_t pixels[40u * 40u];
    PortableSoftwareFrame frame = {pixels,40u,40u,NULL,1u,0u,0u,NULL};
    PortableRaceParticleAssetView asset = {.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=48u,.cells=shell_test_cells(),.cell_bytes=304u,
        .tile_base=0x100u,.palette_base=3u};
    palette[0u+1u]=0x001fu; palette[0u+2u]=0x03e0u; palette[0u+3u]=0x7c00u;
    for(unsigned y=0u;y<16u;++y)for(unsigned x=0u;x<16u;++x){
        const unsigned tile=0u+(y/8u)*2u+x/8u;
        const unsigned color=(x%8u==0u && y%8u==0u)?0u:1u+(tile+y)%3u;
        tiles[tile*32u+(y&7u)*4u+(x&7u)/2u]|=(uint8_t)(color<<((x&1u)*4u));
    }
    for(unsigned i=0u;i<1600u;++i)pixels[i]=0x123456u;
    if(!portable_green_shell_render(&asset,&frame,20,20,0u,0u))return 0;
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
    memset(tiles+0u*32u,0x11,128u);palette[0u+1u]=0x001fu;
    for(unsigned n=0u;n<sizeof cases/sizeof cases[0];++n){
        memset(pixels,0,sizeof pixels);
        if(!portable_green_shell_render_projected(&asset,&frame,80,80,0u,cases[n].depth))return 0;
        for(unsigned y=0u;y<120u;++y)for(unsigned x=0u;x<160u;++x){
            const int inside=(int)x>=cases[n].left && (int)x<cases[n].left+cases[n].width &&
                (int)y>=cases[n].top && (int)y<cases[n].top+cases[n].width;
            if(pixels[y*160u+x]!=(inside?0xff0000u:0u))return 0;
        }
    }
    return !portable_green_shell_render_projected(&asset,&frame,80,80,0u,0u);
}

static int test_physical_perspective(void)
{
    uint8_t tiles[0x800]={0};
    uint16_t palette[48]={0};palette[1]=31u;palette[2]=31u<<5u;
    uint32_t pixels[260u*256u];
    const PortableRaceParticleAssetView asset={.tiles=tiles,.tile_bytes=sizeof tiles,
        .palette=palette,.palette_count=48u,.cells=shell_test_cells(),.cell_bytes=304u,
        .tile_base=0x100u,.palette_base=3u};
    for(unsigned y=0;y<16u;++y)for(unsigned x=0;x<16u;++x){
        const unsigned index=(x+y)%3u;
        const size_t offset=((y/8u)*2u+x/8u)*32u+(y&7u)*4u+(x&7u)/2u;
        tiles[offset]|=(uint8_t)(index<<((x&1u)*4u));
    }
    const uint16_t inverses[]={193u,385u,769u};
    for(unsigned scale=1u;scale<=4u;scale*=2u)for(unsigned n=0;n<3u;++n){
        const size_t stride=64u*scale+3u;
        PortableSoftwareFrame frame={pixels,64u,64u,NULL,(uint8_t)scale,stride,0u,NULL};
        for(size_t i=0;i<stride*64u*scale;++i)pixels[i]=0x123456u;
        if(!portable_green_shell_render(&asset,&frame,32,40,0u,inverses[n]))return 0;
        const int inverse=inverses[n],canvas=inverse<256?32:16;
        const int left=32*(int)scale+256*(int)scale/inverse-(int)scale-canvas*(int)scale/2;
        const int top=40*(int)scale-5*256*(int)scale/inverse-canvas*(int)scale/2;
        unsigned non_block=0u;
        for(unsigned y=0;y<64u*scale;++y)for(unsigned x=0;x<stride;++x){
            uint32_t expected=0x123456u;
            const int dx=(int)x-left,dy=(int)y-top;
            if(x<64u*scale && dx>=0 && dy>=0 && dx<canvas*(int)scale && dy<canvas*(int)scale){
                const int sx=8+(int)portable_software_frame_floor_div((int64_t)(dx-canvas*(int)scale/2)*inverse,256*(int)scale);
                const int sy=8+(int)portable_software_frame_floor_div((int64_t)(dy-canvas*(int)scale/2)*inverse,256*(int)scale);
                if(sx>=0 && sx<16 && sy>=0 && sy<16){
                    const unsigned index=(unsigned)(sx+sy)%3u;
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
 if(!test_tile_boundaries() || !test_projected_bounds() || !test_physical_perspective())return 1;

    if(!test_frames_and_mirror()){
        fputs("green shell renderer test failed\n",stderr);
        return 1;
    }
    puts("green shell renderer tests passed");
    return 0;
}
