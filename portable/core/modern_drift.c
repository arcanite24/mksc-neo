#include "modern_drift.h"

#include <limits.h>
#include <stddef.h>

int portable_modern_drift_rules_valid(const PortableModernDriftRules *rules)
{
    if(rules==NULL || rules->profile_id==0u || rules->revision==0u ||
       rules->minimum_speed_ratio_256==0u || rules->minimum_speed_ratio_256>256u)return 0;
    for(unsigned i=0u;i<3u;++i){
        if(rules->charge_ticks[i]==0u || rules->charge_ticks[i]>8192u ||
           (i!=0u && rules->charge_ticks[i]<=rules->charge_ticks[i-1u]) ||
           rules->boost_ticks[i]==0u || rules->boost_ticks[i]>8192u ||
           rules->cap_ratio_256[i]<256u || rules->cap_ratio_256[i]>1024u ||
           rules->acceleration_q16[i]<=0 || rules->acceleration_q16[i]>128*65536)return 0;
    }
    return portable_modern_air_rules_valid(&rules->air);
}

static uint8_t charge_tier(const PortableModernDriftRules *rules,uint16_t ticks)
{
    uint8_t tier=0u;
    for(unsigned i=0u;i<3u;++i)if(ticks>=rules->charge_ticks[i])tier=(uint8_t)(i+1u);
    return tier;
}

static int state_valid(const PortableModernDriftState *state,const PortableModernDriftRules *rules)
{
    if(state->charge_ticks>rules->charge_ticks[2] || state->previous_held>1u ||
       state->last_applied_tier>3u || state->charge_tier!=charge_tier(rules,state->charge_ticks) ||
       (state->charge_ticks!=0u && !state->previous_held) || state->boost_tier>3u)return 0;
    if(state->boost_tier==0u)return state->boost_ticks==0u;
    return state->boost_ticks!=0u && state->boost_ticks<=rules->boost_ticks[state->boost_tier-1u];
}

static void discard_charge(PortableModernDriftState *state,PortableModernDriftResult *result)
{
    if(state->charge_ticks!=0u)result->cancelled=1u;
    state->charge_ticks=0u;
    state->charge_tier=0u;
}

int portable_modern_drift_tick(PortableModernDriftState *state,
    const PortableModernDriftRules *rules,const PortableModernDriftInput *input,
    PortableModernDriftResult *result)
{
    if(rules==NULL)return 0;
    if(state==NULL || input==NULL || result==NULL || !portable_modern_drift_rules_valid(rules) ||
       input->held_drift>1u || input->grounded>1u || input->eligible>1u || input->cancel>1u ||
       !state_valid(state,rules))return -1;
    PortableModernDriftState next=*state;
    PortableModernDriftResult effect={0};
    next.last_applied_tier=0u;
    if(input->cancel || !input->eligible){
        effect.cancelled=(uint8_t)(next.charge_ticks!=0u || next.boost_ticks!=0u || next.previous_held!=0u);
        next=(PortableModernDriftState){0};
    }else{
        const int enough_speed=input->target_q16>0 &&
            (int64_t)input->speed_q16*256 >= (int64_t)input->target_q16*rules->minimum_speed_ratio_256;
        if(!enough_speed)discard_charge(&next,&effect);
        else if(input->held_drift){
            if(input->grounded){
                if(next.charge_ticks<rules->charge_ticks[2])++next.charge_ticks;
                const uint8_t tier=charge_tier(rules,next.charge_ticks);
                effect.tier_changed=(uint8_t)(tier>next.charge_tier);
                next.charge_tier=tier;
            }
        }else if(next.previous_held && next.charge_ticks!=0u){
            if(input->grounded && next.charge_tier!=0u){
                const unsigned index=next.charge_tier-1u;
                next.boost_tier=next.charge_tier;
                next.boost_ticks=rules->boost_ticks[index];
                effect.released=1u;
                next.charge_ticks=0u;
                next.charge_tier=0u;
            }else discard_charge(&next,&effect);
        }
        next.previous_held=input->held_drift;
        if(next.boost_ticks!=0u){
            const unsigned index=next.boost_tier-1u;
            int64_t cap=(int64_t)input->target_q16*rules->cap_ratio_256[index]/256;
            if(cap<0)cap=0;
            if(cap>INT32_MAX)cap=INT32_MAX;
            effect.applied_tier=next.boost_tier;
            effect.acceleration_q16=rules->acceleration_q16[index];
            effect.speed_cap_q16=(int32_t)cap;
            next.last_applied_tier=effect.applied_tier;
            if(--next.boost_ticks==0u)next.boost_tier=0u;
        }
    }
    *state=next;
    *result=effect;
    return 1;
}
