#ifndef MKSC_PORTABLE_RECOVERY_FX_H
#define MKSC_PORTABLE_RECOVERY_FX_H
#include <stdint.h>

enum { PORTABLE_RECOVERY_DRIPS=4, PORTABLE_RECOVERY_THAW_TICKS=12,
    PORTABLE_RECOVERY_DRIP_TICKS=9, PORTABLE_RECOVERY_EMITTER_TICKS=257,
    PORTABLE_RECOVERY_FX_THAW_START=1, PORTABLE_RECOVERY_FX_DRIP_START=2 };
typedef struct {
    uint8_t freeze_durations[3];
    int8_t drip_dy[9];
} PortableRecoveryFxRules;
/*dy retains the source post-placement byte; draw subtracts drip_dy[age-1]. */
typedef struct { int8_t dx,dy; uint8_t age; } PortableRecoveryDrip;
typedef struct {
    PortableRecoveryDrip drips[PORTABLE_RECOVERY_DRIPS];
    uint16_t drip_emitter_ticks;
    uint8_t drip_wait,thaw_age,pending_thaw,visible;
} PortableRecoveryFx;
static inline int portable_recovery_fx_rules_valid(const PortableRecoveryFxRules *rules)
{
    if(!rules)return 0;
    for(unsigned i=0u;i<3u;++i)
        if(!rules->freeze_durations[i]||rules->freeze_durations[i]>127u)return 0;
    for(unsigned i=0u;i<9u;++i)if(rules->drip_dy[i]<-16||rules->drip_dy[i]>16)return 0;
    return 1;
}
/* D950 leaves bit8. Displaced emitters receive their final C554 callback;
 * independent thaw/drip children finish their scripts after cancellation. */
static inline void portable_recovery_fx_cancel(uint16_t *word,uint16_t *palette)
{
    *word&=UINT16_C(0x100);*palette&=UINT16_C(0xf7ff);
}
/* D214/AD68/AF3C replace channel72. selected_live gates FX construction,
 * independently of D818, which only gates the children's draw visibility. */
static inline int portable_recovery_fx_request(PortableRecoveryFx *fx,
    const PortableRecoveryFxRules *rules,uint16_t *word,uint8_t mode,int selected_live)
{
    if(!fx||!word||!portable_recovery_fx_rules_valid(rules)||
       (mode!=3u&&mode!=11u&&mode!=12u&&mode!=13u&&mode!=14u))return 0;
    *word=(uint16_t)((*word&(mode==3u?0u:0xfff0u))|mode);
    if(mode==12u)*word=(uint16_t)(mode|((uint16_t)rules->freeze_durations[0]<<9u));
    if(mode==14u&&selected_live){fx->drip_emitter_ticks=PORTABLE_RECOVERY_EMITTER_TICKS;fx->drip_wait=0u;}
    return 1;
}
/* Shared FX callbacks precede D328. Thaw created by D328 waits until the
 * next tick; drips use the native uniform callback-on-birth policy. */
static inline int portable_recovery_fx_tick(PortableRecoveryFx *fx,
    const PortableRecoveryFxRules *rules,uint16_t *word,uint16_t animation_flags,
    uint16_t *palette,uint32_t clock,int visible,int selected_live)
{
    if(!fx||!word||!palette||!portable_recovery_fx_rules_valid(rules))return -1;
    int events=0;fx->visible=(uint8_t)(visible!=0);
    if(fx->pending_thaw){fx->pending_thaw=0u;fx->thaw_age=1u;}
    else if(fx->thaw_age&&++fx->thaw_age>PORTABLE_RECOVERY_THAW_TICKS)fx->thaw_age=0u;
    for(unsigned i=0u;i<PORTABLE_RECOVERY_DRIPS;++i){
        PortableRecoveryDrip *drip=&fx->drips[i];
        if(!drip->age)continue;
        if(drip->age>=PORTABLE_RECOVERY_DRIP_TICKS){drip->age=0u;continue;}
        ++drip->age;
        const uint8_t bits=(uint8_t)((int)drip->dy+rules->drip_dy[drip->age-1u]);
        drip->dy=(int8_t)(bits>=128u?(int)bits-256:(int)bits);
    }
    const uint8_t mode=(uint8_t)(*word&15u);
    if((mode==11u||mode==13u)&&(animation_flags&0x8000u))
        portable_recovery_fx_cancel(word,palette);
    else if(mode==12u){
        const uint8_t phase=(uint8_t)((*word>>4u)&31u);
        if(phase>=3u)return -1;
        uint8_t remaining=(uint8_t)(((*word>>9u)&127u)-1u);
        *palette|=0x0800u;
        if(remaining){*word=(uint16_t)((*word&0x1ffu)|((uint16_t)(remaining&127u)<<9u));}
        else if(phase<2u){
            *word=(uint16_t)((*word&0x10fu)|((uint16_t)(phase+1u)<<4u)|
                ((uint16_t)rules->freeze_durations[phase+1u]<<9u));
        }else{
            portable_recovery_fx_cancel(word,palette);
            if(selected_live){fx->pending_thaw=1u;events|=PORTABLE_RECOVERY_FX_THAW_START;}
        }
    }
    /* C554 performs its due emission BEFORE checking for displaced mode14,
     * even when D818 hides the owner. Neither owner consumes random numbers. */
    if(fx->drip_emitter_ticks){
        --fx->drip_emitter_ticks;
        if(fx->drip_wait)--fx->drip_wait;
        else{
            fx->drip_wait=(uint8_t)((clock&3u)*4u+4u);
            unsigned slot=0u;
            while(slot<PORTABLE_RECOVERY_DRIPS&&fx->drips[slot].age)++slot;
            if(slot<PORTABLE_RECOVERY_DRIPS){
                fx->drips[slot]=(PortableRecoveryDrip){(int8_t)((int)(clock&31u)-16),
                    (int8_t)((int)(clock&7u)*2-4),1u};
                const uint8_t bits=(uint8_t)((int)fx->drips[slot].dy+rules->drip_dy[0]);
                fx->drips[slot].dy=(int8_t)(bits>=128u?(int)bits-256:(int)bits);
                events|=PORTABLE_RECOVERY_FX_DRIP_START;
            }
        }
        if((*word&15u)!=14u)fx->drip_emitter_ticks=0u;
    }
    return events;
}
static inline uint64_t portable_recovery_fx_digest(const PortableRecoveryFx *fx)
{
    uint64_t hash=UINT64_C(14695981039346656037);
    const uint8_t owner[]={(uint8_t)fx->drip_emitter_ticks,(uint8_t)(fx->drip_emitter_ticks>>8u),
        fx->drip_wait,fx->thaw_age,fx->pending_thaw,fx->visible};
    for(unsigned i=0u;i<sizeof owner;++i){hash^=owner[i];hash*=UINT64_C(1099511628211);}
    for(unsigned i=0u;i<PORTABLE_RECOVERY_DRIPS;++i){
        const uint8_t values[]={(uint8_t)fx->drips[i].dx,(uint8_t)fx->drips[i].dy,fx->drips[i].age};
        for(unsigned j=0u;j<sizeof values;++j){hash^=values[j];hash*=UINT64_C(1099511628211);}
    }
    return hash;
}
#endif
