#include "gp_coin_hud.h"
#include <stddef.h>
#include <string.h>

int portable_gp_coin_hud_warning(uint32_t clock,uint16_t coins,
    uint32_t flags,uint8_t phase)
{
    if((phase&15u)!=3u||coins>1u||(flags&UINT32_C(0x20))!=0u)return 0;
    return (clock&(coins==0u?31u:63u))==0u;
}

static void pending(PortableGpCoinHud *s,uint32_t clock,uint16_t coins,
    uint32_t flags,const uint8_t colors[18])
{
    const unsigned tens=((uint32_t)coins*UINT32_C(0xcccd)>>19u)&15u;
    s->pending_cells[0]=UINT16_C(0xc020);
    s->pending_cells[1]=(uint16_t)(UINT16_C(0xb010)+tens);
    s->pending_cells[2]=(uint16_t)(UINT16_C(0xb010)+((coins-tens*10u)&15u));
    const unsigned offset=coins>1u||(flags&UINT32_C(0x20))?12u:
        (clock&8u)?0u:6u;
    for(unsigned i=0u;i<3u;++i)s->pending_colors[i]=
        (uint16_t)colors[offset+2u*i]|(uint16_t)colors[offset+2u*i+1u]<<8u;
}

int portable_gp_coin_hud_init(PortableGpCoinHud *s,uint32_t clock,
    uint16_t coins,const uint8_t colors[18])
{
    if(s==NULL||colors==NULL||coins>99u)return 0;
    PortableGpCoinHud next={.clock=clock,.initialized=1u};
    pending(&next,clock,coins,0u,colors);
    memcpy(next.displayed_cells,next.pending_cells,sizeof next.pending_cells);
    memcpy(next.displayed_colors,next.pending_colors,sizeof next.pending_colors);
    *s=next;return 1;
}

int portable_gp_coin_hud_step(PortableGpCoinHud *s,uint32_t clock,
    uint16_t coins,uint32_t flags,uint8_t phase,const uint8_t colors[18])
{
    if(s==NULL||!s->initialized||colors==NULL||coins>99u||clock!=s->clock+1u)return 0;
    PortableGpCoinHud next=*s;
    memcpy(next.displayed_cells,next.pending_cells,sizeof next.pending_cells);
    memcpy(next.displayed_colors,next.pending_colors,sizeof next.pending_colors);
    pending(&next,clock,coins,flags,colors);
    next.warning=(uint8_t)portable_gp_coin_hud_warning(clock,coins,flags,phase);
    next.clock=clock;*s=next;return 1;
}
