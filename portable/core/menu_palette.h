#ifndef PORTABLE_MENU_PALETTE_H
#define PORTABLE_MENU_PALETTE_H
#include <stdint.h>
typedef struct {
    const uint16_t *first,*second,*factors;
} PortableMenuPaletteProfile;
static inline int portable_menu_palette_colors(const PortableMenuPaletteProfile *profile,
    uint32_t clock,uint16_t colors[16])
{
    if(!profile||!colors||!profile->first||!profile->second||!profile->factors)return 0;
    const uint16_t factor=profile->factors[clock&63u];
    for(unsigned i=0u;i<16u;++i){
        uint16_t color=0u;
        for(unsigned shift=0u;shift<=10u;shift+=5u){
            const int32_t first=(profile->first[i]>>shift)&31u;
            const int32_t second=(profile->second[i]>>shift)&31u;
            const int32_t product=(second-first)*(int32_t)factor;
            const int32_t delta=product>=0?product/32768:
                -(int32_t)(((uint32_t)(-product)+32767u)/32768u);
            color=(uint16_t)(color|((uint16_t)(first+delta)&31u)<<shift);
        }
        colors[i]=color;
    }
    return 1;
}
#endif
