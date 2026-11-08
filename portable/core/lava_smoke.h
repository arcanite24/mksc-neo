#ifndef MKSC_PORTABLE_LAVA_SMOKE_H
#define MKSC_PORTABLE_LAVA_SMOKE_H
#include <stdint.h>
/* C354/C2E4 use F3144's load tick plus 180 countdown ticks. C278
 * uses F312C's two six-tick rows, each including its load tick. Neither
 * owner consumes RNG: offsets and wait come directly from the scene clock. */
enum { PORTABLE_LAVA_SMOKE_PUFFS=4, PORTABLE_LAVA_SMOKE_TICKS=181, PORTABLE_LAVA_SMOKE_PUFF_AGE=14 };
typedef struct {
    int8_t dx,dy;
    uint8_t age;
} PortableLavaSmokePuff;
typedef struct {
    PortableLavaSmokePuff puffs[PORTABLE_LAVA_SMOKE_PUFFS];
    uint8_t emitter_ticks,wait,visible;
} PortableLavaSmoke;
static inline void portable_lava_smoke_start(PortableLavaSmoke *smoke)
{
    smoke->emitter_ticks=PORTABLE_LAVA_SMOKE_TICKS;smoke->wait=0u;
}
/* D818's caller supplies race/recovery eligibility, independently of screen
 * clipping. Ineligible attempts still reload the source wait counter. */
static inline int portable_lava_smoke_tick_eligible(PortableLavaSmoke *smoke,
    uint32_t clock,int eligible)
{
    smoke->visible=(uint8_t)(eligible!=0);
    for(unsigned i=0u;i<PORTABLE_LAVA_SMOKE_PUFFS;++i)
        if(smoke->puffs[i].age && ++smoke->puffs[i].age>PORTABLE_LAVA_SMOKE_PUFF_AGE)smoke->puffs[i].age=0u;
    if(!smoke->emitter_ticks)return 0;
    --smoke->emitter_ticks;
    if(smoke->wait){--smoke->wait;return 0;}
    smoke->wait=(uint8_t)((clock&3u)*4u+4u);
    if(!eligible)return 0;
    unsigned slot=0u;
    while(slot<PORTABLE_LAVA_SMOKE_PUFFS && smoke->puffs[slot].age)++slot;
    if(slot==PORTABLE_LAVA_SMOKE_PUFFS)return 0;
    smoke->puffs[slot]=(PortableLavaSmokePuff){.dx=(int8_t)((int)(clock&31u)-16),
        .dy=(int8_t)(-16+(int)(clock&7u)*2),.age=1u};
    return 1;
}
/* Returns one for a new C19C puff; its selected-player audio gate is owned by
 * the session, rather than by this clock-driven effect. */
static inline int portable_lava_smoke_tick(PortableLavaSmoke *smoke,uint32_t clock)
{
    return portable_lava_smoke_tick_eligible(smoke,clock,1);
}
/* Hash typed fields only: snapshot padding never participates in replay. */
static inline uint64_t portable_lava_smoke_digest(const PortableLavaSmoke *smoke)
{
    uint64_t hash=UINT64_C(14695981039346656037);
    const uint8_t owner[]={smoke->emitter_ticks,smoke->wait,smoke->visible};
    for(unsigned i=0u;i<sizeof owner;++i){hash^=owner[i];hash*=UINT64_C(1099511628211);}
    for(unsigned i=0u;i<PORTABLE_LAVA_SMOKE_PUFFS;++i){
        const uint8_t puff[]={(uint8_t)smoke->puffs[i].dx,
            (uint8_t)smoke->puffs[i].dy,smoke->puffs[i].age};
        for(unsigned j=0u;j<sizeof puff;++j){hash^=puff[j];hash*=UINT64_C(1099511628211);}
    }
    return hash;
}
#endif
