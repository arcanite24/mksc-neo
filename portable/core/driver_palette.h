#ifndef PORTABLE_DRIVER_PALETTE_H
#define PORTABLE_DRIVER_PALETTE_H
#include <stdint.h>
/* C84C priority and one-shot +194 consumption, expressed as palette banks. */
static inline uint8_t portable_driver_palette_select(uint16_t *requests,
    int16_t protection,int16_t flash_timer,uint8_t material,uint8_t track_selector,
    uint32_t clock)
{
    if(protection)return (uint8_t)(1u+((clock>>2u)&3u));
    if(flash_timer>295 && ((clock>>2u)&1u)==0u)return 1u;
    if(*requests&0x0800u){*requests&=UINT16_C(0xf7ff);return 9u;}
    if(*requests&0x6000u){
        const uint8_t bank=(*requests&0x2000u)?5u:6u;
        *requests&=UINT16_C(0x9fff);return bank;
    }
    if(material==0x50u && track_selector==12u)return 8u;
    if(*requests&0x8000u){*requests&=0x7fffu;return 7u;}
    return 0u;
}
/* 805ACFC/F2C38: D328 requests bank5 for 240 ticks, then bank6 for ten. */
#define PORTABLE_DRIVER_BURN_TICKS 250u
static inline void portable_driver_palette_burn(uint16_t *ticks,uint16_t *requests)
{
    if(!*ticks)return;
    *requests|=*ticks>10u?0x2000u:0x4000u;
    --*ticks;
}
/* 5704C does not consult immunity, hop mode or protection. */
static inline void portable_driver_palette_boo_contact(uint16_t type,uint16_t *requests)
{if((type&0xf000u)==0x2000u)*requests|=0x8000u;}
#endif
