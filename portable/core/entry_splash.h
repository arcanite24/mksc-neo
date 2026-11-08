#ifndef MKSC_PORTABLE_ENTRY_SPLASH_H
#define MKSC_PORTABLE_ENTRY_SPLASH_H
#include <stdint.h>
/* 805BDC4/F3014: local water/lava entry stays at its birth projection.
 * 5B44C gives each duration8 cell nine ticks: ages2..28 are visible.
 * Scale is an inverse affine coefficient; 128 doubles the source cell.
 * At local distance256 the body projection moves four pixels per unit. */
typedef struct {
    int16_t x,y;
    uint16_t scale_q8;
    uint8_t age;
} PortableEntrySplash;
static inline void portable_entry_splash_start(PortableEntrySplash *s,int32_t height_q16)
{
    *s=(PortableEntrySplash){.x=120,.y=(int16_t)(140-(height_q16/16384)),
        .scale_q8=128u,.age=1u};
}
static inline void portable_entry_splash_tick(PortableEntrySplash *s)
{
    if(s->age && ++s->age>=29u)s->age=0u;
}
#endif
