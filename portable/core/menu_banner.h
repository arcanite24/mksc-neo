#ifndef PORTABLE_MENU_BANNER_H
#define PORTABLE_MENU_BANNER_H

#include <stdint.h>

/* 317C sends the player banner from x272 to x32 with E930 curve10.
 * Its damped sine deliberately overshoots before settling; replacing this
 * with a linear slide removes the small return visible in the original. */
static inline int portable_menu_banner_x(unsigned turn,const int32_t sine[1024])
{
    if(turn==0u)return 272;
    if(turn>=32u||sine==0)return 32;
    const unsigned angle=(turn*turn*131072u/(70u*32u))/64u;
    const int remaining=32-(int)turn;
    int coefficient=(int)((int64_t)sine[(256u-angle)&1023u]*remaining*remaining/1024);
    if(coefficient<0)coefficient=-coefficient;
    const int product=-240*coefficient;
    const int offset=product>=0?product/32768:-((-product+32767)/32768);
    return 32-offset;
}

#endif
