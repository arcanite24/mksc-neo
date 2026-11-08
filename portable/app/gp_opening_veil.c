#include "gp_opening_veil.h"

#include <stddef.h>

static int tile_row_transparent(unsigned tile, unsigned row)
{
    if(tile==0u)return 1;
    if(tile==0x171u)return row==0u||row==7u;
    if(tile==0x172u)return row<=1u||row>=6u;
    if(tile==0x173u)return row<=2u||row>=5u;
    return 0;
}

int portable_gp_opening_veil_row_hidden(uint8_t phase,unsigned row)
{
    if(row>=160u||phase>10u)return 0;
    if(phase==10u)return row<24u||row>=144u;
    unsigned tiles[20];for(unsigned i=0u;i<20u;++i)tiles[i]=0x170u;
    for(unsigned step=0u;step<=phase;++step){
        if(step<=7u){tiles[10u-step]=0x171u;tiles[10u+step]=0x171u;}
        if(step>=1u&&step<=8u){tiles[11u-step]=0x172u;tiles[9u+step]=0x172u;}
        if(step>=2u&&step<=9u){tiles[12u-step]=0x173u;tiles[8u+step]=0x173u;}
        if(step>2u){tiles[13u-step]=0u;tiles[7u+step]=0u;}
    }
    return !tile_row_transparent(tiles[row/8u],row&7u);
}

int portable_gp_opening_veil_apply(uint32_t frame,
                                   uint32_t pixels[240u * 160u])
{
    unsigned tiles[20];
    if(pixels==NULL||frame>30u)return 0;
    for(size_t row=0u;row<20u;++row)tiles[row]=0x170u;
    for(unsigned phase=0u;phase<=frame/3u;++phase){
        /* Source BG0 map row writes are ordered. Each later tile replaces
         * the earlier row, exposing more of its two outer scanlines. */
        if(phase<=7u){
            tiles[10u-phase]=0x171u;
            tiles[10u+phase]=0x171u;
        }
        if(phase>=1u&&phase<=8u){
            tiles[11u-phase]=0x172u;
            tiles[9u+phase]=0x172u;
        }
        if(phase>=2u&&phase<=9u){
            tiles[12u-phase]=0x173u;
            tiles[8u+phase]=0x173u;
        }
        if(phase>2u){
            tiles[13u-phase]=0u;
            tiles[7u+phase]=0u;
        }
    }
    /* At phase ten sub_8052864 clears the visible BG0 map before the
     * course-title panel scrolls in from the adjacent screen block. */
    if(frame==30u){
        /* The cleared map exposes the world only inside WIN0V=0x1890. */
        for(size_t y=0u;y<160u;++y){
            if(y>=24u&&y<144u)continue;
            for(size_t x=0u;x<240u;++x)
                pixels[y*240u+x]=UINT32_C(0xff000000);
        }
        return 1;
    }
    for(size_t y=0u;y<160u;++y){
        if(tile_row_transparent(tiles[y/8u],(unsigned)(y&7u)))continue;
        for(size_t x=0u;x<240u;++x)
            pixels[y*240u+x]=UINT32_C(0xff000000);
    }
    return 1;
}
