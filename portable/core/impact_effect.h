#ifndef MKSC_PORTABLE_IMPACT_EFFECT_H
#define MKSC_PORTABLE_IMPACT_EFFECT_H
#include <stdint.h>

enum { PORTABLE_IMPACT_EFFECT_SLOTS=32, PORTABLE_IMPACT_EFFECT_VARIANTS=6 };
typedef struct { uint16_t script_ticks[PORTABLE_IMPACT_EFFECT_VARIANTS]; } PortableImpactEffectRules;
typedef struct {
    int16_t x,y,depth;
    uint16_t inverse,age;
    uint8_t variant,active;
} PortableImpactEffectSlot;
typedef struct { PortableImpactEffectSlot slots[PORTABLE_IMPACT_EFFECT_SLOTS]; } PortableImpactEffects;
typedef struct {
    uint32_t state_flags;
    int32_t speed_q16;
    int16_t component_x,component_y,x,y,depth;
    uint16_t axis;
    uint8_t selected;
} PortableImpactEffectInputs;
static inline int portable_impact_effect_rules_valid(const PortableImpactEffectRules *rules)
{
    if(!rules)return 0;
    for(unsigned i=0u;i<PORTABLE_IMPACT_EFFECT_VARIANTS;++i)
        if(!rules->script_ticks[i]||rules->script_ticks[i]>8192u)return 0;
    return 1;
}
/* 378FC consumes the entry5A request before B550 clears its collision owner.
 * Result1 selects driving-quality event18 (B554); result2 selects event19.
 * Missing optional art suppresses allocation, while still consuming the owner.
 * Projected body XY/+24 are retained from the preceding projection pass. */
static inline int portable_impact_effect_request(PortableImpactEffects *effects,
    const PortableImpactEffectRules *rules,const PortableImpactEffectInputs *input,uint16_t *request)
{
    if(!effects||!input||!request||(rules&&!portable_impact_effect_rules_valid(rules)))return -1;
    if(!input->selected||(input->state_flags&UINT32_C(0x20))||!(*request&0x8000u))return 0;
    const uint8_t reverse=(uint8_t)(input->speed_q16<0);
    uint8_t variant=reverse;
    if(*request&0x4000u){
        uint8_t side=0u;
        switch(input->axis){
        case 0u:side=(uint8_t)(input->component_y>=0);break;
        case 1u:side=(uint8_t)(input->component_y<0);break;
        case 2u:side=(uint8_t)(input->component_x<0);break;
        case 3u:side=(uint8_t)(input->component_x>=0);break;
        default:break;
        }
        side^=reverse;
        variant=(uint8_t)((side?2u:4u)+reverse);
    }
    *request=0u;
    if(rules){
        /* Lowest idle slot, or source allocator's final-slot replacement. */
        unsigned i=0u;
        while(i<PORTABLE_IMPACT_EFFECT_SLOTS-1u&&effects->slots[i].active)++i;
        const uint16_t bits=(uint16_t)((uint16_t)input->depth+1u);
        const int16_t depth=(int16_t)(bits>=0x8000u?(int32_t)bits-65536:(int32_t)bits);
        effects->slots[i]=(PortableImpactEffectSlot){.x=input->x,.y=input->y,.depth=depth,
            .inverse=256u,.variant=variant,.active=1u};
    }
    return variant<2u?1:2;
}
/* B554/B5B8/B61C install only B44C: no body following, RNG or finish gate.
 * Age0 is constructor-only; callback1 selects the first imported cell. */
static inline int portable_impact_effect_tick(PortableImpactEffects *effects,const PortableImpactEffectRules *rules)
{
    if(!effects||!portable_impact_effect_rules_valid(rules))return 0;
    for(unsigned i=0u;i<PORTABLE_IMPACT_EFFECT_SLOTS;++i){
        PortableImpactEffectSlot *slot=&effects->slots[i];
        if(!slot->active)continue;
        if(slot->variant>=PORTABLE_IMPACT_EFFECT_VARIANTS)return 0;
        if(++slot->age>rules->script_ticks[slot->variant])slot->active=0u;
    }
    return 1;
}
static inline uint64_t portable_impact_effect_digest(const PortableImpactEffects *effects)
{
    uint64_t hash=UINT64_C(1469598103934665603);
    if(!effects)return hash;
#define IMPACT_HASH(value) do { hash^=(uint64_t)(value);hash*=UINT64_C(1099511628211); } while(0)
    for(unsigned i=0u;i<PORTABLE_IMPACT_EFFECT_SLOTS;++i){
        const PortableImpactEffectSlot *slot=&effects->slots[i];
        IMPACT_HASH((uint16_t)slot->x);IMPACT_HASH((uint16_t)slot->y);IMPACT_HASH((uint16_t)slot->depth);
        IMPACT_HASH(slot->inverse);IMPACT_HASH(slot->age);IMPACT_HASH(slot->variant);IMPACT_HASH(slot->active);
    }
#undef IMPACT_HASH
    return hash;
}
#endif
