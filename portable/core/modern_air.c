#include "modern_air.h"

#include <limits.h>
#include <stddef.h>

int portable_modern_air_rules_enabled(const PortableModernAirRules *rules)
{
    return rules!=NULL && rules->launch_mask!=0u;
}

int portable_modern_air_rules_valid(const PortableModernAirRules *rules)
{
    if(rules==NULL)return 0;
    if(!rules->minimum_air_ticks && !rules->trick_window_ticks && !rules->boost_ticks &&
       !rules->cap_ratio_256 && !rules->minimum_speed_ratio_256 &&
       !rules->acceleration_q16 && !rules->launch_mask)return 1;
    return rules->minimum_air_ticks>=1u && rules->minimum_air_ticks<=8192u &&
        rules->trick_window_ticks>=rules->minimum_air_ticks && rules->trick_window_ticks<=8192u &&
        rules->boost_ticks>=1u && rules->boost_ticks<=8192u &&
        rules->cap_ratio_256>=256u && rules->cap_ratio_256<=1024u &&
        rules->minimum_speed_ratio_256>=1u && rules->minimum_speed_ratio_256<=256u &&
        rules->acceleration_q16>0 && rules->acceleration_q16<=128*65536 &&
        rules->launch_mask>=1u && rules->launch_mask<=3u;
}

static void clear_air(PortableModernAirState *state)
{
    state->air_ticks=0u;state->armed=0u;state->tricked=0u;
}

int portable_modern_air_tick(PortableModernAirState *state,const PortableModernAirRules *rules,
    const PortableModernAirInput *input,PortableModernAirResult *result)
{
    if(rules==NULL)return 0;
    if(!portable_modern_air_rules_valid(rules))return -1;
    if(!portable_modern_air_rules_enabled(rules))return 0;
    if(state==NULL || input==NULL || result==NULL || input->hop_pressed>1u || input->hop_held>1u ||
       input->airborne>1u || input->launch_kind>2u || input->landed>1u || input->eligible>1u ||
       input->cancel>1u || state->armed>1u || state->tricked>1u || state->previous_hop>1u ||
       state->last_applied>1u || state->boost_ticks>rules->boost_ticks ||
       (state->tricked && !state->armed) || (state->armed && !state->air_ticks))return -1;
    PortableModernAirState next=*state;
    PortableModernAirResult effect={0};
    next.last_applied=0u;
    if(input->cancel || !input->eligible){
        effect.cancelled=(uint8_t)(next.armed || next.boost_ticks || next.previous_hop);
        next=(PortableModernAirState){0};
    }else{
        const int enough_speed=input->target_q16>0 &&
            (int64_t)input->speed_q16*256 >= (int64_t)input->target_q16*rules->minimum_speed_ratio_256;
        if(input->launch_kind){
            clear_air(&next);
            if(input->airborne && enough_speed && (rules->launch_mask&input->launch_kind)){
                next.armed=1u;next.air_ticks=1u;
            }
        }else if(input->airborne && next.armed){
            if(next.air_ticks<UINT16_MAX)++next.air_ticks;
            if(input->hop_pressed && !next.previous_hop && !next.tricked && enough_speed &&
               next.air_ticks>=rules->minimum_air_ticks && next.air_ticks<=rules->trick_window_ticks){
                next.tricked=1u;effect.trick_started=1u;
            }
        }else if(input->landed){
            if(next.armed && next.tricked && enough_speed){
                next.boost_ticks=rules->boost_ticks;effect.released=1u;
            }
            clear_air(&next);
        }else if(!input->airborne)clear_air(&next);
        next.previous_hop=input->hop_held;
        if(next.boost_ticks){
            int64_t cap=(int64_t)input->target_q16*rules->cap_ratio_256/256;
            if(cap<0)cap=0;
            if(cap>INT32_MAX)cap=INT32_MAX;
            effect.applied=1u;effect.acceleration_q16=rules->acceleration_q16;
            effect.speed_cap_q16=(int32_t)cap;next.last_applied=1u;--next.boost_ticks;
        }
    }
    *state=next;*result=effect;return 1;
}
