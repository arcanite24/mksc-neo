#include "modern_drift.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if(!(condition)){ \
    fprintf(stderr,"modern drift line %d: %s\n",__LINE__,#condition);return 1; \
} } while(0)

static const PortableModernDriftRules rules={
    .profile_id=37u,.revision=2u,.charge_ticks={2u,5u,8u},
    .boost_ticks={3u,5u,7u},.cap_ratio_256={272u,320u,400u},
    .minimum_speed_ratio_256=128u,.acceleration_q16={0x19000,0x27000,0x41000}
};

static PortableModernDriftInput driving(void)
{
    return (PortableModernDriftInput){.held_drift=1u,.grounded=1u,.eligible=1u,
        .speed_q16=5*65536,.target_q16=10*65536};
}

static int equal_state(const PortableModernDriftState *a,const PortableModernDriftState *b)
{
    return a->charge_ticks==b->charge_ticks && a->boost_ticks==b->boost_ticks &&
        a->charge_tier==b->charge_tier && a->boost_tier==b->boost_tier &&
        a->previous_held==b->previous_held && a->last_applied_tier==b->last_applied_tier;
}

static int thresholds_and_duration(void)
{
    const unsigned held_ticks[]={1u,2u,4u,5u,7u,8u,21u};
    const uint8_t expected_tier[]={0u,1u,1u,2u,2u,3u,3u};
    const int32_t expected_cap[]={696320,819200,1024000};
    for(unsigned test=0u;test<sizeof held_ticks/sizeof held_ticks[0];++test){
        PortableModernDriftState state={0};PortableModernDriftResult result;
        PortableModernDriftInput input=driving();
        for(unsigned tick=1u;tick<=held_ticks[test];++tick){
            CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==1);
            CHECK(result.tier_changed==(tick==2u || tick==5u || tick==8u));
            CHECK(!result.released && !result.cancelled && !result.applied_tier);
        }
        CHECK(state.charge_tier==expected_tier[test]);
        CHECK(state.charge_ticks==(held_ticks[test]<8u?held_ticks[test]:8u));
        input.held_drift=0u;
        CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==1);
        CHECK(state.charge_ticks==0u && state.charge_tier==0u && !state.previous_held);
        const unsigned tier=expected_tier[test];
        if(!tier){CHECK(!result.released && result.cancelled && !state.boost_ticks);continue;}
        CHECK(result.released && !result.cancelled && result.applied_tier==tier);
        CHECK(state.boost_ticks==rules.boost_ticks[tier-1u]-1u && state.last_applied_tier==tier);
        CHECK(result.acceleration_q16==rules.acceleration_q16[tier-1u] && result.speed_cap_q16==expected_cap[tier-1u]);
        for(unsigned tick=1u;tick<rules.boost_ticks[tier-1u];++tick){
            CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==1);
            CHECK(!result.released && result.applied_tier==tier && state.last_applied_tier==tier);
        }
        CHECK(state.boost_ticks==0u && state.boost_tier==0u);
        CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==1);
        CHECK(!result.applied_tier && !result.acceleration_q16 && !result.speed_cap_q16 && !state.last_applied_tier);
    }
    return 0;
}

static int eligibility_and_air(void)
{
    PortableModernDriftState state={0};PortableModernDriftResult result;
    PortableModernDriftInput input=driving();
    CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==1);
    input.grounded=0u;
    for(unsigned tick=0u;tick<12u;++tick){
        CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==1);
        CHECK(state.charge_ticks==1u && !state.charge_tier && !result.tier_changed);
    }
    input.grounded=1u;
    CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==1);
    CHECK(state.charge_ticks==2u && result.tier_changed);
    input.grounded=0u;input.held_drift=0u;
    CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==1);
    CHECK(result.cancelled && !result.released && !state.charge_ticks && !state.boost_ticks);
    input=driving();
    for(unsigned tick=0u;tick<5u;++tick)CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==1);
    input.held_drift=0u;
    CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==1 && result.applied_tier==2u);
    input.grounded=0u;input.speed_q16=0;
    CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==1);
    CHECK(result.applied_tier==2u && state.boost_ticks==3u); /* Independent boost remains active in air. */
    input.eligible=0u; /* Brake/hazard eligibility is supplied by the shared driver owner. */
    CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==1);
    CHECK(result.cancelled && !state.boost_ticks && !state.last_applied_tier && !result.applied_tier);
    input=driving();input.speed_q16=5*65536-1;
    CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==1 && !state.charge_ticks);
    input.speed_q16=5*65536;
    CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==1 && state.charge_ticks==1u);
    input.speed_q16=-1;
    CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==1 && result.cancelled && !state.charge_ticks);
    input=driving();
    for(unsigned tick=0u;tick<8u;++tick)CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==1);
    input.cancel=1u;input.held_drift=0u;
    CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==1);
    CHECK(result.cancelled && !result.released && !state.charge_ticks && !state.previous_held && !state.boost_ticks);
    return 0;
}

static int bounds_copy_and_bypass(void)
{
    PortableModernDriftState state={0},copy={0},other={0};
    PortableModernDriftInput input=driving();PortableModernDriftResult result={0},copied={0};
    unsigned char input_before[sizeof input];memcpy(input_before,&input,sizeof input);
    for(unsigned i=0u;i<5u;++i)CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==1);
    CHECK(memcmp(input_before,&input,sizeof input)==0);
    copy=state;
    input.held_drift=0u;
    CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==1);
    CHECK(portable_modern_drift_tick(&copy,&rules,&input,&copied)==1);
    CHECK(equal_state(&state,&copy) && result.applied_tier==copied.applied_tier &&
        result.speed_cap_q16==copied.speed_cap_q16 && result.acceleration_q16==copied.acceleration_q16);
    input=driving();
    CHECK(portable_modern_drift_tick(&other,&rules,&input,&result)==1 && other.charge_ticks==1u);
    CHECK(state.charge_ticks==0u && state.boost_ticks==4u); /* Local owners never share charge/timers. */
    PortableModernDriftRules long_rules=rules;long_rules.charge_ticks[2]=8192u;
    state=(PortableModernDriftState){0};
    for(unsigned i=0u;i<20000u;++i)CHECK(portable_modern_drift_tick(&state,&long_rules,&input,&result)==1);
    CHECK(state.charge_ticks==8192u && state.charge_tier==3u);
    state=(PortableModernDriftState){.boost_ticks=1u,.boost_tier=3u};
    input.held_drift=0u;input.target_q16=INT32_MAX;input.speed_q16=INT32_MAX;
    CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==1 && result.speed_cap_q16==INT32_MAX);
    CHECK(result.applied_tier==3u && state.last_applied_tier==3u && !state.boost_tier);
    state=(PortableModernDriftState){.boost_ticks=1u,.boost_tier=3u};input.target_q16=INT32_MIN;
    CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==1 && result.speed_cap_q16==0);

    memset(&state,0xa5,sizeof state);memset(&result,0x5a,sizeof result);
    unsigned char before_state[sizeof state],before_result[sizeof result];
    memcpy(before_state,&state,sizeof state);memcpy(before_result,&result,sizeof result);
    CHECK(portable_modern_drift_tick(&state,NULL,NULL,&result)==0);
    CHECK(memcmp(before_state,&state,sizeof state)==0 && memcmp(before_result,&result,sizeof result)==0);
    state=(PortableModernDriftState){0};input=driving();
    PortableModernDriftRules invalid=rules;
    for(unsigned test=0u;test<14u;++test){
        invalid=rules;
        switch(test){
        case 0u:invalid.profile_id=0u;break;
        case 1u:invalid.revision=0u;break;
        case 2u:invalid.charge_ticks[0]=0u;break;
        case 3u:invalid.charge_ticks[1]=invalid.charge_ticks[0];break;
        case 4u:invalid.charge_ticks[2]=8193u;break;
        case 5u:invalid.boost_ticks[1]=0u;break;
        case 6u:invalid.boost_ticks[2]=8193u;break;
        case 7u:invalid.cap_ratio_256[0]=255u;break;
        case 8u:invalid.cap_ratio_256[2]=1025u;break;
        case 9u:invalid.acceleration_q16[0]=0;break;
        case 10u:invalid.acceleration_q16[2]=128*65536+1;break;
        case 11u:invalid.minimum_speed_ratio_256=257u;break;
        case 12u:invalid.minimum_speed_ratio_256=0u;break;
        default:invalid.acceleration_q16[1]=-1;break;
        }
        memcpy(before_state,&state,sizeof state);memcpy(before_result,&result,sizeof result);
        CHECK(!portable_modern_drift_rules_valid(&invalid));
        CHECK(portable_modern_drift_tick(&state,&invalid,&input,&result)==-1);
        CHECK(memcmp(before_state,&state,sizeof state)==0 && memcmp(before_result,&result,sizeof result)==0);
    }
    state.boost_tier=4u;
    CHECK(portable_modern_drift_tick(&state,&rules,&input,&result)==-1);
    return 0;
}

static int air_tricks_and_bounds(void)
{
    const PortableModernAirRules air={.minimum_air_ticks=3u,.trick_window_ticks=5u,
        .boost_ticks=3u,.cap_ratio_256=320u,.minimum_speed_ratio_256=128u,
        .acceleration_q16=2*65536,.launch_mask=3u};
    PortableModernAirState state={0};PortableModernAirResult effect={0};
    PortableModernAirInput input={.hop_pressed=1u,.hop_held=1u,.airborne=1u,
        .launch_kind=2u,.eligible=1u,.speed_q16=5*65536,.target_q16=10*65536};
    CHECK(portable_modern_air_tick(&state,&air,&input,&effect)==1 && state.armed &&
        state.air_ticks==1u && !state.tricked && !effect.released);
    input.launch_kind=0u;input.hop_pressed=input.hop_held=0u;
    CHECK(portable_modern_air_tick(&state,&air,&input,&effect)==1 && state.air_ticks==2u);
    input.hop_pressed=input.hop_held=1u;
    CHECK(portable_modern_air_tick(&state,&air,&input,&effect)==1 && state.air_ticks==3u && effect.trick_started);
    PortableModernAirState copy=state;
    CHECK(portable_modern_air_tick(&state,&air,&input,&effect)==1 && !effect.trick_started);
    input.airborne=0u;input.landed=1u;input.hop_pressed=input.hop_held=0u;
    CHECK(portable_modern_air_tick(&state,&air,&input,&effect)==1 && effect.released && effect.applied &&
        effect.acceleration_q16==2*65536 && effect.speed_cap_q16==819200 && state.boost_ticks==2u &&
        !state.armed && !state.tricked && !state.air_ticks);
    CHECK(copy.armed && copy.tricked && copy.air_ticks==3u && !copy.boost_ticks);
    input.landed=0u;input.airborne=1u;
    for(unsigned tick=0u;tick<2u;++tick)
        CHECK(portable_modern_air_tick(&state,&air,&input,&effect)==1 && effect.applied &&
            !effect.released && state.boost_ticks==1u-tick && state.last_applied);
    CHECK(portable_modern_air_tick(&state,&air,&input,&effect)==1 && !effect.applied && !state.last_applied);
    /* Inclusive last window tick; too-early, too-late and no second press all
     * land without a reward. Ramp-only rules do not arm an ordinary hop. */
    for(unsigned age=2u;age<=6u;++age){
        state=(PortableModernAirState){0};input.launch_kind=1u;input.hop_pressed=1u;input.hop_held=1u;
        CHECK(portable_modern_air_tick(&state,&air,&input,&effect)==1);
        input.launch_kind=0u;input.hop_pressed=input.hop_held=0u;
        for(unsigned tick=2u;tick<age;++tick)CHECK(portable_modern_air_tick(&state,&air,&input,&effect)==1);
        input.hop_pressed=input.hop_held=1u;
        CHECK(portable_modern_air_tick(&state,&air,&input,&effect)==1 && effect.trick_started==(age>=3u&&age<=5u));
        input.airborne=0u;input.landed=1u;
        CHECK(portable_modern_air_tick(&state,&air,&input,&effect)==1 && effect.released==(age>=3u&&age<=5u));
        input.landed=0u;input.airborne=1u;
    }
    PortableModernAirRules ramp=air;ramp.launch_mask=1u;state=(PortableModernAirState){0};input.launch_kind=2u;
    CHECK(portable_modern_air_tick(&state,&ramp,&input,&effect)==1 && !state.armed);
    input.launch_kind=1u;input.speed_q16=5*65536-1;
    CHECK(portable_modern_air_tick(&state,&air,&input,&effect)==1 && !state.armed);
    input.speed_q16=5*65536;input.launch_kind=0u;
    for(unsigned gate=0u;gate<2u;++gate){
        state=copy;state.boost_ticks=2u;input.cancel=(uint8_t)(gate==0u);input.eligible=(uint8_t)(gate!=1u);
        CHECK(portable_modern_air_tick(&state,&air,&input,&effect)==1 && effect.cancelled &&
            !state.armed && !state.tricked && !state.boost_ticks && !state.last_applied);
    }
    input.cancel=0u;input.eligible=1u;state=copy;state.air_ticks=UINT16_MAX;state.tricked=0u;
    CHECK(portable_modern_air_tick(&state,&air,&input,&effect)==1 && state.air_ticks==UINT16_MAX && !state.tricked);
    state=(PortableModernAirState){.boost_ticks=1u};input.target_q16=INT32_MAX;
    CHECK(portable_modern_air_tick(&state,&air,&input,&effect)==1 && effect.speed_cap_q16==INT32_MAX);
    PortableModernAirRules disabled={0};
    uint8_t prior[sizeof state],prior_effect[sizeof effect];
    memcpy(prior,&state,sizeof state);memcpy(prior_effect,&effect,sizeof effect);
    CHECK(portable_modern_air_tick(&state,&disabled,&input,&effect)==0 &&
        !memcmp(prior,&state,sizeof state) && !memcmp(prior_effect,&effect,sizeof effect));
    for(unsigned bad=0u;bad<8u;++bad){
        PortableModernAirRules invalid=air;
        switch(bad){
        case 0u:invalid.minimum_air_ticks=0u;break;
        case 1u:invalid.trick_window_ticks=2u;break;
        case 2u:invalid.trick_window_ticks=8193u;break;
        case 3u:invalid.boost_ticks=0u;break;
        case 4u:invalid.cap_ratio_256=255u;break;
        case 5u:invalid.minimum_speed_ratio_256=257u;break;
        case 6u:invalid.acceleration_q16=128*65536+1;break;
        default:invalid.launch_mask=4u;break;
        }
        CHECK(!portable_modern_air_rules_valid(&invalid));
        CHECK(portable_modern_air_tick(&state,&invalid,&input,&effect)==-1 &&
            !memcmp(prior,&state,sizeof state) && !memcmp(prior_effect,&effect,sizeof effect));
        PortableModernDriftRules profile=rules;profile.air=invalid;
        CHECK(!portable_modern_drift_rules_valid(&profile));
    }
    return 0;
}

int main(void)
{
    CHECK(portable_modern_drift_rules_valid(&rules));
    CHECK(thresholds_and_duration()==0);
    CHECK(air_tricks_and_bounds()==0);
    CHECK(eligibility_and_air()==0);
    CHECK(bounds_copy_and_bypass()==0);
    puts("portable modern drift: PASS");
    return 0;
}
