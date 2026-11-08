#include "driver_hop.h"
#include "recovery_fx.h"
#include "surface_trail.h"
#include "coin_effect.h"
#include "impact_effect.h"
#include <string.h>

#include <stdint.h>
#include <stdio.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr,"driver hop line %d: %s\n",__LINE__,#condition);return 1; } } while (0)

static int test_recovery_fx(void)
{
    const PortableRecoveryFxRules rules={{127u,127u,127u},{0,1,1,1,2,2,3,3,1}};
    PortableRecoveryFx fx={0};uint16_t word=0x100u,palette=0u;
    word=0xa5a3u;
    CHECK(portable_recovery_fx_request(&fx,&rules,&word,11u,0) && word==0xa5abu);
    CHECK(portable_recovery_fx_request(&fx,&rules,&word,13u,0) && word==0xa5adu);
    CHECK(portable_recovery_fx_request(&fx,&rules,&word,14u,0) && word==0xa5aeu && !fx.drip_emitter_ticks);
    portable_recovery_fx_cancel(&word,&palette);CHECK(word==0x100u);
    CHECK(portable_recovery_fx_rules_valid(&rules));
    CHECK(portable_recovery_fx_request(&fx,&rules,&word,12u,1));
    CHECK(word==0xfe0cu);
    for(unsigned tick=1u;tick<=381u;++tick){
        palette=0u;
        const int event=portable_recovery_fx_tick(&fx,&rules,&word,0u,&palette,tick,1,1);
        CHECK(event==(tick==381u?PORTABLE_RECOVERY_FX_THAW_START:0));
        CHECK((palette&0x800u)==(tick<381u?0x800u:0u));
        if(tick==127u)CHECK(((word>>4u)&31u)==1u && ((word>>9u)&127u)==127u);
        if(tick==254u)CHECK(((word>>4u)&31u)==2u && ((word>>9u)&127u)==127u);
    }
    CHECK(word==0u && !fx.thaw_age && fx.pending_thaw);
    const uint64_t pending_hash=portable_recovery_fx_digest(&fx);
    CHECK(portable_recovery_fx_tick(&fx,&rules,&word,0u,&palette,0u,1,1)==0 &&
        fx.thaw_age==1u && !fx.pending_thaw && pending_hash!=portable_recovery_fx_digest(&fx));
    for(unsigned age=2u;age<=12u;++age){
        CHECK(portable_recovery_fx_tick(&fx,&rules,&word,0u,&palette,0u,0,1)==0 &&
            fx.thaw_age==age && !fx.visible);
    }
    CHECK(portable_recovery_fx_tick(&fx,&rules,&word,0u,&palette,0u,1,1)==0 && !fx.thaw_age);
    for(uint8_t mode=11u;mode<=13u;mode+=2u){
        CHECK(portable_recovery_fx_request(&fx,&rules,&word,mode,1));
        CHECK(portable_recovery_fx_tick(&fx,&rules,&word,0u,&palette,0u,1,1)==0 && (word&15u)==mode);
        CHECK(portable_recovery_fx_tick(&fx,&rules,&word,0x8000u,&palette,0u,1,1)==0 && word==0u);
    }
    CHECK(portable_recovery_fx_request(&fx,&rules,&word,14u,1));
    CHECK(fx.drip_emitter_ticks==257u && !fx.drip_wait);
    CHECK(portable_recovery_fx_tick(&fx,&rules,&word,0u,&palette,0u,0,1)==PORTABLE_RECOVERY_FX_DRIP_START);
    CHECK(!fx.visible && fx.drips[0].age==1u && fx.drips[0].dx==-16 && fx.drips[0].dy==-4);
    fx.drip_emitter_ticks=0u;
    const int8_t post_y[9]={-4,-3,-2,-1,1,3,6,9,10};
    for(unsigned age=2u;age<=9u;++age){
        CHECK(portable_recovery_fx_tick(&fx,&rules,&word,0u,&palette,0u,1,1)==0 &&
            fx.drips[0].age==age && fx.drips[0].dy==post_y[age-1u]);
    }
    CHECK(portable_recovery_fx_tick(&fx,&rules,&word,0u,&palette,0u,1,1)==0 && !fx.drips[0].age);
    CHECK(portable_recovery_fx_request(&fx,&rules,&word,14u,1));
    portable_recovery_fx_cancel(&word,&palette);
    CHECK(portable_recovery_fx_tick(&fx,&rules,&word,0u,&palette,31u,0,1)==PORTABLE_RECOVERY_FX_DRIP_START &&
        !fx.drip_emitter_ticks && fx.drips[0].dx==15 && fx.drips[0].dy==10);
    const uint64_t before=portable_recovery_fx_digest(&fx);
    CHECK(before==portable_recovery_fx_digest(&fx));
    fx.visible^=1u;CHECK(before!=portable_recovery_fx_digest(&fx));
    PortableRecoveryFxRules authored=rules;authored.freeze_durations[0]=1u;
    authored.freeze_durations[1]=2u;authored.freeze_durations[2]=3u;
    CHECK(portable_recovery_fx_request(&fx,&authored,&word,12u,0));
    for(unsigned tick=0u;tick<6u;++tick)
        CHECK(portable_recovery_fx_tick(&fx,&authored,&word,0u,&palette,tick,1,0)==0);
    CHECK(word==0u && !fx.thaw_age && !fx.pending_thaw);
    word=0xffffu;palette=0xffffu;portable_recovery_fx_cancel(&word,&palette);
    CHECK(word==0x100u && palette==0xf7ffu);
    fx=(PortableRecoveryFx){0};word=0u;
    CHECK(portable_recovery_fx_request(&fx,&rules,&word,14u,1));
    unsigned emissions=0u;
    for(uint32_t tick=0u;tick<257u;++tick){
        const int events=portable_recovery_fx_tick(&fx,&rules,&word,0u,&palette,tick,1,1);
        CHECK(events>=0);emissions+=(unsigned)((events&PORTABLE_RECOVERY_FX_DRIP_START)!=0);
        CHECK(fx.drip_emitter_ticks==256u-tick);
    }
    CHECK(emissions==24u && !fx.drip_emitter_ticks);
    CHECK(portable_recovery_fx_tick(&fx,&rules,&word,0u,&palette,257u,1,1)==0);
    authored.freeze_durations[0]=0u;CHECK(!portable_recovery_fx_rules_valid(&authored));
    CHECK(!portable_recovery_fx_request(&fx,&authored,&word,12u,1));
    CHECK(portable_recovery_fx_tick(&fx,NULL,&word,0u,&palette,0u,1,1)==-1);
    return 0;
}

static int test_surface_trail(void)
{
    PortableSurfaceTrailRules rules={0};
    for(unsigned i=0u;i<256u;++i)rules.material_kind[i]=1u;
    for(unsigned i=0u;i<5u;++i){rules.frame_cutoffs[i][0]=(uint8_t)(i<3u?9u:5u);
        rules.frame_cutoffs[i][1]=(uint8_t)(i<3u?19u:11u);}
    rules.material_kind[0x54]=2u;rules.material_kind[0x5a]=3u;
    rules.material_kind[0x5c]=4u;rules.material_kind[0x58]=5u;
    rules.offroad_material_min=0x50u;rules.suppressed_moving_material=0x50u;rules.spray_material=0x5cu;
    int32_t sine[1024]={0};sine[256]=32768;sine[768]=-32768;
    PortableSurfaceTrail state={0};
    PortableSurfaceTrailInputs in={.camera_yaw=0x3e00u,.body_x=120,.body_y=100,
        .material=0x40u,.animation_flags=0xcu,.speed_q16=65536};
    CHECK(portable_surface_trail_rules_valid(&rules));
    for(unsigned tick=1u;tick<=4u;++tick)
        CHECK(portable_surface_trail_tick(&state,&rules,&in,sine,1024u)==(tick==4u));
    CHECK(state.slots[0].age==2u && state.slots[0].kind==1u && state.slots[0].direction==4u &&
        state.slots[0].x_q7==124*128 && state.slots[0].y_q7==108*128 && state.side==1u);
    for(unsigned tick=0u;tick<4u;++tick)CHECK(portable_surface_trail_tick(&state,&rules,&in,sine,1024u)==(tick==3u));
    CHECK(state.slots[0].age==6u && state.slots[0].x_q7==140*128 &&
        state.slots[1].age==2u && state.slots[1].direction==65532u && !state.side);
    /* Lost full-pool emissions still alternate; expiration occurs afterwards. */
    for(unsigned i=0u;i<6u;++i)state.slots[i].age=29u;
    state.eligible_counter=3u;
    CHECK(!portable_surface_trail_tick(&state,&rules,&in,sine,1024u) && state.side==1u);
    for(unsigned i=0u;i<6u;++i)CHECK(!state.slots[i].age);
    for(unsigned tick=0u;tick<4u;++tick)CHECK(portable_surface_trail_tick(&state,&rules,&in,sine,1024u)==(tick==3u));
    CHECK(!state.side && state.slots[0].age==2u);
    const uint8_t materials[4]={0x54u,0x5au,0x5cu,0x58u};
    for(unsigned i=0u;i<4u;++i){
        state=(PortableSurfaceTrail){.eligible_counter=3u};in.material=materials[i];
        CHECK(portable_surface_trail_tick(&state,&rules,&in,sine,1024u)==1 && state.slots[0].kind==i+2u);
    }
    in.material=0x50u;state=(PortableSurfaceTrail){.eligible_counter=3u};
    CHECK(!portable_surface_trail_tick(&state,&rules,&in,sine,1024u) && state.eligible_counter==3u);
    in.material=0x40u;
    const uint8_t blocked[]={5u,6u,7u,8u,13u,15u};
    for(unsigned i=0u;i<sizeof blocked;++i){in.response_status=blocked[i];
        CHECK(!portable_surface_trail_tick(&state,&rules,&in,sine,1024u) && state.eligible_counter==3u);}
    in.mushroom_state=3u;
    CHECK(portable_surface_trail_tick(&state,&rules,&in,sine,1024u)==1);
    in.response_status=0u;in.mushroom_state=0u;in.animation_flags=0u;in.speed_q16=0;
    in.material=0x5cu;in.parity=1u;
    CHECK(!portable_surface_trail_tick(&state,&rules,&in,sine,1024u) && state.extra_spray && state.visible);
    in.height_q16=1;
    const uint8_t age=state.slots[0].age;
    CHECK(!portable_surface_trail_tick(&state,&rules,&in,sine,1024u) && !state.extra_spray &&
        state.slots[0].age==age+1u && state.visible);
    in.flags=0x20u;
    const PortableSurfaceTrail before=state;
    CHECK(!portable_surface_trail_tick(&state,&rules,&in,sine,1024u) && !state.visible &&
        !memcmp(state.slots,before.slots,sizeof state.slots) && state.side==before.side &&
        state.eligible_counter==before.eligible_counter);
    CHECK(portable_surface_trail_digest(&state)!=portable_surface_trail_digest(&before));
    /* Source signed divisions truncate toward zero before halfword wrapping. */
    in=(PortableSurfaceTrailInputs){.camera_yaw=0x3e00u,.height_q16=1};
    state=(PortableSurfaceTrail){0};state.slots[0]=(PortableSurfaceTrailSlot){32767,-32760,0u,1u,1u};
    sine[256]=-127;sine[0]=-32768;
    CHECK(!portable_surface_trail_tick(&state,&rules,&in,sine,1024u) &&
        state.slots[0].x_q7==32766 && state.slots[0].y_q7==32734);
    /* Original callbacks469/470/489. Sparse synthetic sine samples retain
     * the observed quantized transport without embedding the ROM table. */
    memset(sine,0,sizeof sine);sine[1008]=-3200;sine[1016]=-1600;
    sine[256]=sine[264]=32768;sine[752]=-32768;sine[528]=-3200;
    state=(PortableSurfaceTrail){0};
    state.slots[0]=(PortableSurfaceTrailSlot){13568,19200,34u,1u,3u};
    in=(PortableSurfaceTrailInputs){.source_angle_7a=512u,.height_q16=1};
    CHECK(!portable_surface_trail_tick(&state,&rules,&in,sine,1024u) &&
        state.slots[0].age==2u && state.slots[0].x_q7==13543 && state.slots[0].y_q7==19242);
    in.source_angle_7a=1024u;
    CHECK(!portable_surface_trail_tick(&state,&rules,&in,sine,1024u) &&
        state.slots[0].age==3u && state.slots[0].x_q7==13493 && state.slots[0].y_q7==19284);
    state.slots[0]=(PortableSurfaceTrailSlot){11263,19940,34u,21u,3u};
    in.camera_yaw=45969u;in.source_angle_7a=12621u;
    CHECK(!portable_surface_trail_tick(&state,&rules,&in,sine,1024u) &&
        state.slots[0].age==22u && state.slots[0].x_q7==11188 && state.slots[0].y_q7==19898);
    for(unsigned kind=1u;kind<=5u;++kind){
        PortableSurfaceTrailSlot slot={.kind=(uint8_t)kind,.age=rules.frame_cutoffs[kind-1u][0]};
        CHECK(portable_surface_trail_frame(&rules,&slot)==0u);++slot.age;
        CHECK(portable_surface_trail_frame(&rules,&slot)==1u);slot.age=rules.frame_cutoffs[kind-1u][1]+1u;
        CHECK(portable_surface_trail_frame(&rules,&slot)==2u);
    }
    rules.material_kind[0]=0u;CHECK(!portable_surface_trail_rules_valid(&rules));
    CHECK(portable_surface_trail_tick(&state,&rules,&in,sine,1024u)==-1);
    return 0;
}

static int test_coin_effect(void)
{
    const PortableCoinEffectRules rules={
        {0,-2,2,-2,2,-2,2,-2,2},
        {8*65536,8*65536,8*65536,5*65536,5*65536,10*65536,10*65536,7*65536,7*65536},
        {256u,153u,102u},{27u,66u,21u,5u}};
    PortableCoinEffects effects={0};PortableCoinEffectAudioEvents audio;
    PortableCoinEffectRequest request={.cause=PORTABLE_COIN_EFFECT_CAUSE_CONTACT,.loss=1u,.remaining=4u,
        .x=120,.y=130,.inverse=256};
    CHECK(portable_coin_effect_rules_valid(&rules));
    CHECK(portable_coin_effect_request(&effects,&rules,&request));
    CHECK(effects.slots[0].kind==PORTABLE_COIN_EFFECT_BOUNCE && effects.slots[0].variant==3u &&
        effects.slots[0].depth==257 && !effects.slots[0].age);
    CHECK(portable_coin_effect_tick(&effects,&rules,512,&audio) && !audio.count &&
        effects.slots[0].x==118 && effects.slots[0].y==125 && effects.slots[0].age==1u &&
        effects.slots[0].speed_q16==294912 && effects.slots[0].inverse==512u);
    request.cause=PORTABLE_COIN_EFFECT_CAUSE_HAUNT;request.clock=0u;request.remaining=3u;
    CHECK(portable_coin_effect_request(&effects,&rules,&request) && effects.slots[1].variant==4u);
    request.cause=PORTABLE_COIN_EFFECT_CAUSE_CONTACT;request.clock=1u;request.remaining=2u;
    CHECK(portable_coin_effect_request(&effects,&rules,&request) && effects.slots[2].variant==4u);
    effects=(PortableCoinEffects){0};request.cause=PORTABLE_COIN_EFFECT_CAUSE_RETRIEVAL;
    request.loss=1u;request.clock=0u;
    CHECK(portable_coin_effect_request(&effects,&rules,&request));
    CHECK(effects.slots[0].kind==PORTABLE_COIN_EFFECT_RISE_EMITTER && effects.slots[0].limit==1u);
    CHECK(portable_coin_effect_tick(&effects,&rules,1024,&audio) && audio.count==1u &&
        audio.events[0].song_id==101u && effects.slots[1].kind==PORTABLE_COIN_EFFECT_RISE &&
        effects.slots[1].age==1u && !effects.slots[1].visible && effects.slots[1].inverse==256u);
    for(unsigned tick=2u;tick<=21u;++tick){
        CHECK(portable_coin_effect_tick(&effects,&rules,1024,&audio) && !audio.count);
        CHECK(effects.slots[1].age==tick && effects.slots[1].visible==(tick>=4u));
        if(tick==3u)CHECK(effects.slots[1].y==129);
        if(tick==4u)CHECK(effects.slots[1].y==128);
        if(tick==6u)CHECK(!effects.slots[0].kind);
    }
    CHECK(portable_coin_effect_tick(&effects,&rules,1024,&audio) && !effects.slots[1].kind);
    effects=(PortableCoinEffects){0};request.cause=PORTABLE_COIN_EFFECT_CAUSE_HIT;
    request.loss=4u;request.clock=7u;
    CHECK(portable_coin_effect_request(&effects,&rules,&request) && effects.slots[0].x==117);
    for(unsigned tick=1u;tick<=25u;++tick){
        CHECK(portable_coin_effect_tick(&effects,&rules,256,&audio));
        unsigned coins=0u;for(unsigned slot=0u;slot<32u;++slot)coins+=effects.slots[slot].kind==PORTABLE_COIN_EFFECT_BOUNCE;
        CHECK(coins==(tick<9u?0u:tick<13u?1u:tick<17u?2u:tick<21u?3u:4u));
        if(tick==9u)CHECK(effects.slots[1].variant==1u && effects.slots[1].age==1u &&
            effects.slots[1].x==115 && effects.slots[1].y==122);
    }
    CHECK(!effects.slots[0].kind);
    effects=(PortableCoinEffects){0};
    portable_coin_effect_bounce(&effects,&rules,32767,136,256,4u,3u);
    effects.slots[0].speed_q16=0;
    CHECK(portable_coin_effect_tick(&effects,&rules,256,&audio) && !audio.count && effects.slots[0].x==-32767);
    CHECK(portable_coin_effect_tick(&effects,&rules,256,&audio) && audio.count==1u &&
        audio.events[0].song_id==102u && audio.events[0].volume==256u &&
        effects.slots[0].y_q16==136*65536+32768);
    CHECK(portable_coin_effect_tick(&effects,&rules,256,&audio) && audio.count==1u && audio.events[0].volume==153u);
    CHECK(portable_coin_effect_tick(&effects,&rules,256,&audio) && audio.count==1u && audio.events[0].volume==102u);
    CHECK(portable_coin_effect_tick(&effects,&rules,256,&audio) && !audio.count && effects.slots[0].bounces==4u);
    effects.slots[0].y_q16=INT32_MIN;effects.slots[0].speed_q16=1;
    CHECK(portable_coin_effect_tick(&effects,&rules,256,&audio) && effects.slots[0].y_q16==INT32_MAX);
    const PortableCoinEffects before=effects;const uint64_t digest=portable_coin_effect_digest(&effects);
    CHECK(digest==portable_coin_effect_digest(&effects) && !memcmp(&before,&effects,sizeof effects));
    effects.slots[0].inverse=257u;CHECK(digest!=portable_coin_effect_digest(&effects));
    effects=(PortableCoinEffects){0};request.cause=PORTABLE_COIN_EFFECT_CAUSE_CRUSH;request.clock=7u;request.loss=8u;
    CHECK(portable_coin_effect_request(&effects,&rules,&request) && effects.slots[0].x==120 && effects.slots[0].limit==8u);
    request.loss=0u;const PortableCoinEffects no_loss=effects;
    CHECK(portable_coin_effect_request(&effects,&rules,&request) && !memcmp(&effects,&no_loss,sizeof effects));
    CHECK(!portable_coin_effect_tick(&effects,NULL,256,&audio));
    PortableCoinEffectRules authored=rules;authored.horizontal_step[0]=65;
    CHECK(!portable_coin_effect_rules_valid(&authored));
    return 0;
}

static int test_impact_effect(void)
{
    const PortableImpactEffectRules rules={{14u,14u,14u,14u,13u,13u}};
    PortableImpactEffects effects={0};
    PortableImpactEffectInputs input={.selected=1u,.x=120,.y=140,.depth=256,
        .component_x=-1,.component_y=-1};
    CHECK(portable_impact_effect_rules_valid(&rules));
    PortableImpactEffectRules invalid=rules;invalid.script_ticks[5]=0u;
    CHECK(!portable_impact_effect_rules_valid(&invalid));
    invalid.script_ticks[5]=8193u;CHECK(!portable_impact_effect_rules_valid(&invalid));
    uint16_t word=0xc007u;
    input.selected=0u;
    CHECK(!portable_impact_effect_request(&effects,&rules,&input,&word)&&word==0xc007u);
    input.selected=1u;input.state_flags=0x20u;
    CHECK(!portable_impact_effect_request(&effects,&rules,&input,&word)&&word==0xc007u);
    input.state_flags=0u;word=0x4007u;
    CHECK(!portable_impact_effect_request(&effects,&rules,&input,&word)&&word==0x4007u);
    /* Ordinary Peach TT wall callback:5A=C007, axis0, component50<0,
     * speed>=0 selects B61C first donor, with cached120/140/depth256. */
    word=0xc007u;
    CHECK(portable_impact_effect_request(&effects,&rules,&input,&word)==2 && !word);
    CHECK(effects.slots[0].variant==4u && effects.slots[0].x==120 && effects.slots[0].y==140 &&
        effects.slots[0].depth==257 && effects.slots[0].inverse==256u && !effects.slots[0].age);
    /* 0/3 complement sign, 1/2 use sign directly; zero is significant. */
    for(uint16_t axis=0u;axis<=4u;++axis)
        for(unsigned negative=0u;negative<2u;++negative)
            for(unsigned reverse=0u;reverse<2u;++reverse){
                effects=(PortableImpactEffects){0};word=0xffffu;
                input.axis=axis;input.component_x=input.component_y=negative?-1:0;
                input.speed_q16=reverse?-1:0;
                const unsigned sign=axis==0u||axis==3u?!negative:axis<4u?negative:0u;
                const unsigned variant=((sign^reverse)?2u:4u)+reverse;
                CHECK(portable_impact_effect_request(&effects,&rules,&input,&word)==2 && !word &&
                    effects.slots[0].variant==variant);
            }
    for(uint8_t variant=0u;variant<6u;++variant){
        effects=(PortableImpactEffects){0};word=variant<2u?0x8007u:0xc007u;
        input.axis=1u;input.speed_q16=(variant&1u)?-1:0;
        input.component_y=((variant>=2u&&variant<4u)^((variant&1u)!=0u))?-1:0;
        CHECK(portable_impact_effect_request(&effects,&rules,&input,&word)==(variant<2u?1:2) &&
            effects.slots[0].variant==variant && !word);
        const PortableImpactEffectSlot anchor=effects.slots[0];
        for(uint16_t age=1u;age<=rules.script_ticks[variant];++age){
            CHECK(portable_impact_effect_tick(&effects,&rules) && effects.slots[0].active &&
                effects.slots[0].age==age && effects.slots[0].x==anchor.x &&
                effects.slots[0].y==anchor.y && effects.slots[0].depth==anchor.depth &&
                effects.slots[0].inverse==256u);
        }
        const PortableImpactEffects held=effects;
        const uint64_t digest=portable_impact_effect_digest(&effects);
        CHECK(digest==portable_impact_effect_digest(&held));
        CHECK(portable_impact_effect_tick(&effects,&rules) && !effects.slots[0].active &&
            digest!=portable_impact_effect_digest(&effects) && held.slots[0].active);
    }
    effects=(PortableImpactEffects){0};input.depth=INT16_MAX;
    for(unsigned i=0u;i<33u;++i){
        word=0x8000u;input.x=(int16_t)i;
        CHECK(portable_impact_effect_request(&effects,&rules,&input,&word)==1);
    }
    CHECK(effects.slots[0].x==0 && effects.slots[30].x==30 && effects.slots[31].x==32 &&
        effects.slots[31].depth==INT16_MIN);
    effects.slots[3].active=0u;word=0x8000u;input.x=77;
    CHECK(portable_impact_effect_request(&effects,&rules,&input,&word)==1 && effects.slots[3].x==77);
    const PortableImpactEffects prior=effects;word=0xffffu;
    CHECK(portable_impact_effect_request(&effects,NULL,&input,&word)==2 && !word &&
        !memcmp(&prior,&effects,sizeof effects));
    word=0xc007u;
    CHECK(portable_impact_effect_request(&effects,&invalid,&input,&word)==-1 && word==0xc007u);
    PortableImpactEffectRules authored={{1u,2u,3u,4u,5u,6u}};
    effects=(PortableImpactEffects){0};word=0x8000u;input.speed_q16=0;
    CHECK(portable_impact_effect_request(&effects,&authored,&input,&word)==1 &&
        portable_impact_effect_tick(&effects,&authored) && effects.slots[0].active &&
        portable_impact_effect_tick(&effects,&authored) && !effects.slots[0].active);
    return 0;
}

int main(void)
{
    {
        /* ASR keeps a negative fractional height below zero. Truncating it
         * before adding the new velocity would incorrectly prolong the hop. */
        PortableDriverHopState edge = {.mode = 1u, .height_q16 = -1, .velocity_q8 = 27};
        CHECK(portable_driver_hop_tick(&edge, 0u) == PORTABLE_DRIVER_HOP_OK);
        CHECK(edge.landed && edge.height_q16 == 0 && edge.velocity_q8 == 0);
        edge =
            (PortableDriverHopState){.mode = 1u, .height_q16 = INT32_MIN, .velocity_q8 = INT16_MIN};
        CHECK(portable_driver_hop_tick(&edge, 0u) == PORTABLE_DRIVER_HOP_OK);
        CHECK(edge.landed && edge.height_q16 == 0);
        /* Repacking the positive Q8 sum retains the source's low 32 bits. */
        edge =
            (PortableDriverHopState){.mode = 1u, .height_q16 = INT32_MAX, .velocity_q8 = INT16_MAX};
        CHECK(portable_driver_hop_tick(&edge, 0u) == PORTABLE_DRIVER_HOP_OK);
        CHECK(!edge.landed && edge.mode == 1u && edge.velocity_q8 == 32741 &&
              edge.height_q16 == -INT32_C(2139102208));
    }
    CHECK(test_recovery_fx() == 0);
    CHECK(test_surface_trail() == 0);
    CHECK(test_coin_effect() == 0);
    CHECK(test_impact_effect()==0);
    static const int32_t heights[] = {
        INT32_C(0x00000000), INT32_C(0x0000c600),
        INT32_C(0x00017200), INT32_C(0x00020400),
        INT32_C(0x00027c00), INT32_C(0x0002da00),
        INT32_C(0x00031e00), INT32_C(0x00034800),
        INT32_C(0x00035800), INT32_C(0x00034e00),
        INT32_C(0x00032a00), INT32_C(0x0002ec00),
        INT32_C(0x00029400), INT32_C(0x00022200),
        INT32_C(0x00019600), INT32_C(0x0000f000),
        INT32_C(0x00003000), INT32_C(0x00000000),
    };
    {
        /* 8040A44/8040C3C: a Piranha Plant pipe on material 0x2A. */
        const int32_t kart_x=INT32_C(100)<<16, kart_y=INT32_C(200)<<16;
        const PortableDriverCaptureActor far={(INT32_C(140)<<16),kart_y,2u};
        const PortableDriverCaptureActor hidden_plant={(INT32_C(124)<<16)+0x4000,
            (INT32_C(180)<<16),0u};
        const PortableDriverCaptureActor plant={(INT32_C(124)<<16)+0x4000,
            (INT32_C(180)<<16),2u};
        PortableDriverCaptureState capture={.x_q16=kart_x,.y_q16=kart_y,
            .speed_q16=INT32_C(0x2000000),.spin_82=0x100u};
        /* No actor in range, or a first in-range actor that is not
         * collidable, leaves 0x2A plain ground. */
        CHECK(portable_driver_capture_begin(&capture,0u,0,0,&far,1u)==PORTABLE_DRIVER_HOP_OK &&
              !capture.captured && capture.mode==0u);
        const PortableDriverCaptureActor shadowed[2]={hidden_plant,plant};
        CHECK(portable_driver_capture_begin(&capture,0u,0,0,shadowed,2u)==PORTABLE_DRIVER_HOP_OK &&
              !capture.captured);
        /* +0x58 bit 0x20, protection and contact guard all refuse. */
        CHECK(portable_driver_capture_begin(&capture,0x20u,0,0,&plant,1u)==PORTABLE_DRIVER_HOP_OK && !capture.captured);
        CHECK(portable_driver_capture_begin(&capture,0u,1,0,&plant,1u)==PORTABLE_DRIVER_HOP_OK && !capture.captured);
        CHECK(portable_driver_capture_begin(&capture,0u,0,1,&plant,1u)==PORTABLE_DRIVER_HOP_OK && !capture.captured);
        CHECK(portable_driver_capture_begin(&capture,0u,0,0,&plant,1u)==PORTABLE_DRIVER_HOP_OK);
        CHECK(capture.captured && capture.mode==8u && capture.timer_9c==92u &&
              capture.timer_62==1u && capture.state_flags==0x4800u &&
              capture.component_x==((24*65536+0x4000)/8192) &&
              capture.component_y==(-20*65536)/8192 && capture.animation_b4==0u);
        PortableDriverCaptureState cpu=capture;
        cpu.mode=0u;cpu.state_flags=0x2000u;
        CHECK(portable_driver_capture_begin(&cpu,0u,0,0,&plant,1u)==PORTABLE_DRIVER_HOP_OK &&
              cpu.animation_b4==0x300u && cpu.spin_rate_b6==0x1000u);
        CHECK(!portable_driver_hidden(capture.mode,capture.height_q16,0u,capture.timer_9c));
        unsigned turns=0u;
        while(capture.mode==8u && turns<200u){
            const uint16_t timer=capture.timer_9c;
            capture.timer_62=0u;
            CHECK(portable_driver_capture_tick(&capture)==PORTABLE_DRIVER_HOP_OK);
            ++turns;
            CHECK(capture.timer_62==1u);
            if(timer>59u)
                CHECK((capture.animation_b4&8u) && capture.spin_82==(uint16_t)(0x100u+turns*0xc00u) &&
                      capture.component_x!=0 && capture.speed_q16==INT32_C(0x2000000));
            else if(capture.mode==8u)
                CHECK(!(capture.animation_b4&8u) && capture.component_x==0 &&
                      capture.component_y==0 && capture.speed_q16==0 &&
                      portable_driver_hidden(capture.mode,capture.height_q16,0u,capture.timer_9c));
        }
        CHECK(turns==92u && capture.launched && capture.mode==1u && capture.timer_9c==0u &&
              capture.velocity_q8==0x2f0 && capture.height_q16==0x100 &&
              capture.animation_b4==0x8000u && capture.driver_mode_84==1u &&
              capture.response_status_7e==12u && capture.spin_82==0u &&
              capture.speed_q16==INT32_C(0x1f00000) && capture.acceleration_q16==0 &&
              capture.component_x==0 && capture.component_y==0);
        CHECK(portable_driver_capture_tick(&capture)==PORTABLE_DRIVER_HOP_UNSUPPORTED_MODE);
        /* The flight flips backward until a revolution boundary on descent. */
        uint8_t status=12u;uint16_t flags=0x8000u,spin=0u;
        CHECK(portable_driver_launch_flip_tick(&status,&flags,&spin,0x100)==PORTABLE_DRIVER_HOP_OK &&
              status==12u && spin==0xf800u);
        for(unsigned i=0u;i<31u;++i)
            CHECK(portable_driver_launch_flip_tick(&status,&flags,&spin,-1)==PORTABLE_DRIVER_HOP_OK &&
                  status==12u);
        CHECK(spin==0u && portable_driver_launch_flip_tick(&status,&flags,&spin,-1)==
              PORTABLE_DRIVER_HOP_OK && status==14u && spin==0u);
        status=12u;flags=0u;
        CHECK(portable_driver_launch_flip_tick(&status,&flags,&spin,1)==PORTABLE_DRIVER_HOP_OK &&
              status==14u);
    }
    {
        PortableDriverHopState bounds={.height_q16=INT32_C(0x30000)};
        int32_t speed=INT32_C(0x800000);
        CHECK(portable_driver_bounds_tick(&bounds,0u,&speed)==PORTABLE_DRIVER_HOP_OK);
        CHECK(bounds.mode==5u && bounds.height_q16==INT32_C(0x30000) && speed==0 &&
              bounds.recovery_requests==0x20u && bounds.retrieval_started);
        CHECK(portable_driver_bounds_tick(&bounds,5u,&speed)==PORTABLE_DRIVER_HOP_OK && bounds.mode==5u);
        bounds.height_q16=INT32_C(0x70000);
        CHECK(portable_driver_bounds_tick(&bounds,5u,&speed)==PORTABLE_DRIVER_HOP_OK && bounds.mode==6u);
        bounds.state_flags|=UINT32_C(0x2000);
        bounds.height_q16=0;
        CHECK(portable_driver_bounds_tick(&bounds,5u,&speed)==PORTABLE_DRIVER_HOP_OK &&
              bounds.mode==6u && bounds.height_q16==INT32_C(0x70000));
    }
    {
        int32_t sine[1024] = {0};
        sine[56]=16384; sine[200]=24576;
        sine[52]=8192; sine[204]=32768;
        for (unsigned material=0u; material<=0x10u; material+=0x10u)
        {
            PortableDriverHopState ramp={0};
            int32_t speed=INT32_C(0x1001234);
            CHECK(portable_driver_surface_jump(&ramp,0u,(uint8_t)material,&speed,
                0,sine,1024u)==PORTABLE_DRIVER_HOP_OK);
            CHECK(ramp.mode==1u && ramp.velocity_q8==368 &&
                ramp.height_q16==INT32_C(0x28000) && speed==INT32_C(0x2280000) &&
                ramp.state_flags==0x800u && ramp.animation_flags==0x8000u);
            /* Airborne and the landing turn cannot relaunch on the pad. */
            CHECK(portable_driver_surface_jump(&ramp,1u,(uint8_t)material,&speed,
                0,sine,1024u)==PORTABLE_DRIVER_HOP_OK && speed==INT32_C(0x2280000));
            unsigned ticks=0;
            while (ramp.mode==1u && ticks++<100u)
                CHECK(portable_driver_hop_tick(&ramp,0u)==PORTABLE_DRIVER_HOP_OK);
            CHECK(ramp.landed && ticks<100u && ramp.height_q16==0);
        }
        PortableDriverHopState ramp={0};
        int32_t speed=INT32_C(0x5001234);
        CHECK(portable_driver_surface_jump(&ramp,0u,0x10u,&speed,INT32_C(0x3a00000),
            sine,1024u)==PORTABLE_DRIVER_HOP_OK);
        CHECK(speed==INT32_C(0x2b80000) && ramp.velocity_q8==464);
        ramp=(PortableDriverHopState){.state_flags=0x20u};speed=0;
        CHECK(portable_driver_surface_jump(&ramp,0u,0u,&speed,0,sine,1024u)==PORTABLE_DRIVER_HOP_OK);
        CHECK(speed==INT32_C(0x4200000) && ramp.velocity_q8==704);
        ramp=(PortableDriverHopState){0};speed=-INT32_C(0x1001234);
        CHECK(portable_driver_surface_jump(&ramp,0u,0u,&speed,0,sine,1024u)==PORTABLE_DRIVER_HOP_OK);
        CHECK(speed==-INT32_C(0x2280000) && ramp.velocity_q8==368);
        ramp=(PortableDriverHopState){0};speed=INT32_C(0x17fffff);
        CHECK(portable_driver_surface_jump(&ramp,0u,2u,&speed,0,sine,1024u)==PORTABLE_DRIVER_HOP_OK && ramp.mode==0u);
        ++speed;
        CHECK(portable_driver_surface_jump(&ramp,0u,0x12u,&speed,0,sine,1024u)==PORTABLE_DRIVER_HOP_OK);
        CHECK(ramp.mode==1u && ramp.velocity_q8==96 && speed==INT32_C(0x1800000) && ramp.height_q16==0);
        ramp=(PortableDriverHopState){0};speed=INT32_C(0x1000000);
        CHECK(portable_driver_surface_jump(&ramp,0u,14u,&speed,0,NULL,0u)==PORTABLE_DRIVER_HOP_OK);
        CHECK(ramp.mode==1u && ramp.velocity_q8==128 && ramp.height_q16==256);
        ramp=(PortableDriverHopState){0};speed=INT32_C(0xffffff);
        CHECK(portable_driver_surface_jump(&ramp,0u,14u,&speed,0,NULL,0u)==PORTABLE_DRIVER_HOP_OK && ramp.mode==0u);
        ramp.state_flags=0x2000u;speed=-1;
        CHECK(portable_driver_surface_jump(&ramp,0u,12u,&speed,0,NULL,0u)==PORTABLE_DRIVER_HOP_OK);
        CHECK(ramp.mode==1u && ramp.velocity_q8==256 && speed==INT32_C(0x1000000));
        ramp=(PortableDriverHopState){0};speed=0;
        CHECK(portable_driver_surface_jump(&ramp,0u,0x40u,&speed,0,NULL,0u)==PORTABLE_DRIVER_HOP_OK && ramp.mode==0u);
        CHECK(portable_driver_surface_jump(&ramp,0u,0u,&speed,0,NULL,0u)==PORTABLE_DRIVER_HOP_INVALID_ARGUMENT);
    }
    PortableDriverHopState state = {
        .state_flags = UINT32_C(0x00008000),
    };

    CHECK(portable_driver_hop_tick(&state, UINT16_C(0x8014)) ==
          PORTABLE_DRIVER_HOP_OK);
    CHECK(state.mode == 1u && state.height_q16 == heights[0] &&
          (uint16_t)state.velocity_q8 == UINT16_C(0x00e0) &&
          state.state_flags == UINT32_C(0x00008800) &&
          state.animation_flags == UINT16_C(0x8000));
    for (size_t tick = 1u; tick < sizeof heights / sizeof heights[0]; ++tick)
    {
        CHECK(portable_driver_hop_tick(&state, UINT16_C(0x8010)) ==
              PORTABLE_DRIVER_HOP_OK);
        CHECK(state.height_q16 == heights[tick]);
    }
    CHECK(state.mode == 0u && state.velocity_q8 == 0 &&
          state.state_flags == UINT32_C(0x00008800) &&
          state.animation_flags == 0u);
    CHECK(portable_driver_hop_tick(&state, UINT16_C(0x8010)) ==
          PORTABLE_DRIVER_HOP_OK);
    CHECK(state.state_flags == UINT32_C(0x00008000));
    CHECK(portable_driver_hop_tick(NULL, 0u) ==
          PORTABLE_DRIVER_HOP_INVALID_ARGUMENT);
    state.mode = 2u;
    CHECK(portable_driver_hop_tick(&state, 0u) ==
          PORTABLE_DRIVER_HOP_UNSUPPORTED_MODE);

    {
        int32_t sine[1024] = {0};
        int32_t speed = INT32_C(0x02781234);
        uint16_t timer = 0;
        PortableDriverHopState terrain = {.state_flags=UINT32_C(0x8000)};
        sine[32]=8192;
        sine[224]=24576;
        CHECK(portable_driver_terrain22_tick(&terrain,0u,0x22u,&speed,&timer,sine,1024u)==
              PORTABLE_DRIVER_HOP_OK);
        CHECK(speed==INT32_C(0x01621234) && terrain.velocity_q8==118 &&
              terrain.mode==1u && terrain.height_q16==0 &&
              terrain.animation_flags==UINT16_C(0x8010) && timer==0u);
        while (terrain.mode==1u)
        {
            CHECK(portable_driver_hop_tick(&terrain,UINT16_C(0x8000))==
                  PORTABLE_DRIVER_HOP_OK);
            CHECK(portable_driver_terrain22_tick(&terrain,1u,0x22u,&speed,&timer,sine,1024u)==
                  PORTABLE_DRIVER_HOP_OK);
        }
        CHECK(terrain.landed && terrain.mode==0u && timer==0u);
        CHECK(!terrain.recovery_started);
        CHECK(portable_driver_terrain22_tick(&terrain,0u,0x22u,&speed,&timer,sine,1024u)==
              PORTABLE_DRIVER_HOP_OK);
        CHECK(terrain.mode==4u && speed==0 && timer==144u);
        CHECK(terrain.recovery_started);
        for (unsigned turn=0u;turn<61u;++turn)
            CHECK(portable_driver_terrain22_tick(&terrain,4u,0x22u,&speed,&timer,sine,1024u)==
                  PORTABLE_DRIVER_HOP_OK);
        CHECK(timer==83u && terrain.state_flags==UINT32_C(0x8800));
        CHECK(!terrain.recovery_started);
        timer=61u;
        CHECK(portable_driver_terrain22_tick(&terrain,4u,0x22u,&speed,&timer,sine,1024u)==
              PORTABLE_DRIVER_HOP_OK && timer==60u);
        CHECK(terrain.state_flags==UINT32_C(0x8900) &&
              terrain.recovery_requests==UINT16_C(0x400));
        /* Acknowledging the pulse cannot recreate it on the next turn. */
        terrain.recovery_requests=0u;
        CHECK(portable_driver_terrain22_tick(&terrain,4u,0x22u,&speed,&timer,sine,1024u)==
              PORTABLE_DRIVER_HOP_OK && timer==59u);
        CHECK(terrain.recovery_requests==0u && !terrain.retrieval_started &&
              terrain.lakitu_ticks==51u && !terrain.lakitu_arrived);
        for (unsigned turn=1u;turn<=51u;++turn) {
            CHECK(portable_driver_terrain22_tick(&terrain,4u,0x22u,&speed,&timer,sine,1024u)==
                  PORTABLE_DRIVER_HOP_OK);
            CHECK(terrain.lakitu_arrived==(turn==51u));
        }
        CHECK(timer==8u && terrain.mode==4u);
        timer=1u;
        speed=INT32_C(0x00781234);
        CHECK(portable_driver_terrain22_tick(&terrain,4u,0x22u,&speed,&timer,sine,1024u)==
              PORTABLE_DRIVER_HOP_OK && timer==0u);
        CHECK(terrain.mode==4u && speed==INT32_C(0x00781234));
        CHECK(portable_driver_terrain22_tick(&terrain,4u,0x22u,&speed,&timer,sine,1024u)==
              PORTABLE_DRIVER_HOP_OK && timer==0u);
        CHECK(terrain.mode==3u && speed==0 && terrain.retrieval_started &&
              terrain.recovery_requests==UINT16_C(0x10) &&
              terrain.state_flags==UINT32_C(0x8800) && terrain.height_q16==0);
        terrain.recovery_requests=0u;
        terrain.height_q16=INT32_C(0x00018000);
        CHECK(portable_driver_terrain22_tick(&terrain,3u,0x22u,&speed,&timer,sine,1024u)==
              PORTABLE_DRIVER_HOP_OK && !terrain.retrieval_started);
        CHECK(terrain.height_q16==INT32_C(0x00018000) &&
              terrain.recovery_requests==0u);
        terrain.height_q16=INT32_C(0x00070000);
        CHECK(portable_driver_terrain22_tick(&terrain,3u,0x22u,&speed,&timer,sine,1024u)==
              PORTABLE_DRIVER_HOP_OK && terrain.mode==6u && speed==0);
        terrain.mode=3u;
        terrain.height_q16=INT32_C(0x0006ffff);
        CHECK(portable_driver_terrain22_tick(&terrain,3u,0x22u,&speed,&timer,sine,1024u)==
              PORTABLE_DRIVER_HOP_OK && terrain.mode==3u);
        terrain.state_flags|=UINT32_C(0x2000);
        CHECK(portable_driver_terrain22_tick(&terrain,3u,0x22u,&speed,&timer,sine,1024u)==
              PORTABLE_DRIVER_HOP_OK && terrain.height_q16==INT32_C(0x70000));
        CHECK(portable_driver_terrain22_tick(&terrain,0u,0x22u,&speed,&timer,sine,32u)==
              PORTABLE_DRIVER_HOP_INVALID_ARGUMENT);
    }

    {
        int32_t x=INT32_C(0x06028000), y=INT32_C(0x00048000);
        uint16_t heading=0;
        /* X snaps first; Y remains fixed until the following turn. */
        CHECK(portable_driver_recovery_relocate(&x,&y,&heading,1536,200,
              UINT16_C(0xc000))==PORTABLE_DRIVER_HOP_OK);
        CHECK(x==INT32_C(0x06000000) && y==INT32_C(0x00048000) &&
              heading==UINT16_C(0xfec0));
        CHECK(portable_driver_recovery_relocate(&x,&y,&heading,1536,200,
              UINT16_C(0xc000))==PORTABLE_DRIVER_HOP_OK);
        CHECK(y==INT32_C(0x00078000) && heading==UINT16_C(0xfd80));
        heading=UINT16_C(0xc100);
        CHECK(portable_driver_recovery_relocate(&x,&y,&heading,1536,200,
              UINT16_C(0xc000))==PORTABLE_DRIVER_HOP_OK && heading==UINT16_C(0xc000));
        heading=UINT16_C(0xbfff);
        CHECK(portable_driver_recovery_relocate(&x,&y,&heading,1536,200,
              UINT16_C(0xc000))==PORTABLE_DRIVER_HOP_OK && heading==UINT16_C(0xc000));
        x=-INT32_C(0x00018000);
        CHECK(portable_driver_recovery_relocate(&x,&y,&heading,1536,200,
              UINT16_C(0xc000))==PORTABLE_DRIVER_HOP_OK && x==INT32_C(0x00018000));
        x=INT32_C(0x06000000);y=INT32_C(0x00c60000);
        CHECK(portable_driver_recovery_relocate(&x,&y,&heading,1536,200,
              UINT16_C(0xc000))==PORTABLE_DRIVER_HOP_UNSUPPORTED_MODE);
        CHECK(portable_driver_recovery_relocate(NULL,&y,&heading,1536,200,0)==
              PORTABLE_DRIVER_HOP_INVALID_ARGUMENT);
    }

    {
        PortableDriverHopState drop = {.mode=6u, .height_q16=INT32_C(0x70000),
            .recovery_requests=UINT16_C(0x1000), .state_flags=UINT32_C(0x8800)};
        int32_t x=INT32_C(1536)*65536, y=INT32_C(197)*65536+65535;
        uint16_t yaw=UINT16_C(0xbfff), flags=UINT16_C(0xa002), contact=4u;
        CHECK(portable_driver_recovery_tick(&drop,&x,&y,&yaw,&flags,&contact,
            1536,200,UINT16_C(0xc000))==PORTABLE_DRIVER_HOP_OK);
        CHECK(!drop.recovery_checkpoint_sample);
        CHECK(drop.mode==7u && drop.recovery_requests==UINT16_C(0x1040) &&
            flags==UINT16_C(0x8002) && contact==7u && y==INT32_C(200)*65536);
        /* A904's heading snap reports change; it cannot descend that turn. */
        yaw=UINT16_C(0xc1ff);
        CHECK(portable_driver_recovery_tick(&drop,&x,&y,&yaw,&flags,&contact,
            1536,200,UINT16_C(0xc000))==PORTABLE_DRIVER_HOP_OK);
        CHECK(yaw==UINT16_C(0xc000) && drop.height_q16==INT32_C(0x70000));
        for(unsigned i=0;i<13u;++i){
            CHECK(portable_driver_recovery_tick(&drop,&x,&y,&yaw,&flags,&contact,
                1536,200,UINT16_C(0xc000))==PORTABLE_DRIVER_HOP_OK);
            CHECK(drop.mode==7u && !drop.recovery_touchdown);
        }
        CHECK(drop.height_q16==INT32_C(0x8000));
        CHECK(portable_driver_recovery_tick(&drop,&x,&y,&yaw,&flags,&contact,
            1536,200,UINT16_C(0xc000))==PORTABLE_DRIVER_HOP_OK);
        CHECK(drop.height_q16==0 && drop.mode==0u && drop.recovery_touchdown &&
            flags==UINT16_C(0x8003) && drop.state_flags==UINT32_C(0x8800));
        CHECK(portable_driver_hop_tick(&drop,0u)==PORTABLE_DRIVER_HOP_OK);
        CHECK(!drop.recovery_touchdown && drop.state_flags==UINT32_C(0x8000));
        /* X's truncating equality and the signed Y guard are independent. */
        drop.mode=6u; x=INT32_C(1536)*65536+65535; y=-1;
        CHECK(portable_driver_recovery_tick(&drop,&x,&y,&yaw,&flags,&contact,
            1536,0,UINT16_C(0xc000))==PORTABLE_DRIVER_HOP_OK);
        CHECK(drop.mode==6u && y==INT32_C(0x2ffff) && drop.recovery_checkpoint_sample);
        x=INT32_C(1520)*65536;
        CHECK(portable_driver_recovery_tick(&drop,&x,&y,&yaw,&flags,&contact,
            1536,0,UINT16_C(0xc000))==PORTABLE_DRIVER_HOP_OK);
        CHECK(x==INT32_C(1523)*65536 && !drop.recovery_checkpoint_sample);
        CHECK(portable_driver_recovery_tick(NULL,&x,&y,&yaw,&flags,&contact,
            1536,0,0u)==PORTABLE_DRIVER_HOP_INVALID_ARGUMENT);
    }

    {
        {
        int32_t sine[1024]={0};
        sine[248]=32768;sine[8]=4096;
        PortableDriverHopState water={.mode=4u,.state_flags=0x900u};
        int32_t speed=INT32_C(0x780000);uint16_t timer=120u;
        CHECK(portable_driver_terrain22_tick(&water,4u,0x40u,&speed,&timer,sine,1024u)==PORTABLE_DRIVER_HOP_OK);
        CHECK(timer==119u && water.mode==1u && water.velocity_q8==120 &&
              speed==INT32_C(0xf0000) && water.recovery_requests==0x200u &&
              !(water.state_flags&0x100u) && (water.animation_flags&0x8010u)==0x8010u);
        water=(PortableDriverHopState){.mode=4u};timer=120u;
        CHECK(portable_driver_terrain22_tick(&water,4u,0x24u,&speed,&timer,sine,1024u)==PORTABLE_DRIVER_HOP_OK);
        CHECK(water.mode==3u && water.retrieval_started && water.height_q16==INT32_C(0x300000));
        water=(PortableDriverHopState){.mode=4u};timer=120u;
        CHECK(portable_driver_terrain22_tick(&water,4u,0x80u,&speed,&timer,sine,1024u)==PORTABLE_DRIVER_HOP_OK && water.mode==4u);
    }
    PortableDriverDriftChargeState drift = {0};
        uint16_t flags = UINT16_C(0x0024);

        /* First host R frame: the prior stored input has no R, so only the
         * current-input boundary advances. On the next frame the early stage
         * sees the stored edge before it starts the hop, and both boundaries
         * advance. */
        CHECK(portable_driver_drift_charge_pre_tick(
                  &drift, UINT16_C(0x8200), 0u) ==
              PORTABLE_DRIVER_HOP_OK);
        CHECK(portable_driver_drift_charge_post_tick(
                  &drift, UINT16_C(0x8214), INT32_C(0x038e8000),
                  INT32_C(0x04400000), &flags) ==
              PORTABLE_DRIVER_HOP_OK);
        CHECK(drift.charge_counter == 1u && flags == UINT16_C(0x0024));
        CHECK(portable_driver_drift_charge_pre_tick(
                  &drift, UINT16_C(0x8214), 0u) ==
              PORTABLE_DRIVER_HOP_OK);
        CHECK(portable_driver_drift_charge_post_tick(
                  &drift, UINT16_C(0x8210), INT32_C(0x038f4000),
                  INT32_C(0x04400000), &flags) ==
              PORTABLE_DRIVER_HOP_OK);
        CHECK(drift.charge_counter == 3u);
        for (unsigned frame = 0; frame < 16u; ++frame)
        {
            CHECK(portable_driver_drift_charge_pre_tick(
                      &drift, UINT16_C(0x8210), 1u) ==
                  PORTABLE_DRIVER_HOP_OK);
            CHECK(portable_driver_drift_charge_post_tick(
                      &drift, UINT16_C(0x8210), INT32_C(0x03900000),
                      INT32_C(0x04400000), &flags) ==
                  PORTABLE_DRIVER_HOP_OK);
        }
        CHECK(drift.charge_counter == 19u);
        drift.charge_counter = UINT16_C(0x0076);
        CHECK(portable_driver_drift_charge_pre_tick(
                  &drift, UINT16_C(0x8210), 0u) ==
              PORTABLE_DRIVER_HOP_OK);
        CHECK(portable_driver_drift_charge_post_tick(
                  &drift, UINT16_C(0x8210), INT32_C(0x03c18000),
                  INT32_C(0x04400000), &flags) ==
              PORTABLE_DRIVER_HOP_OK);
        CHECK(drift.charge_counter == UINT16_C(0x0078) &&
              flags == UINT16_C(0x0064));

        /* The prior boundary still observes held R on the release frame;
         * the current boundary owns the reset and preserves the charged bit. */
        CHECK(portable_driver_drift_charge_pre_tick(
                  &drift, UINT16_C(0x8210), 0u) ==
              PORTABLE_DRIVER_HOP_OK);
        CHECK(drift.charge_counter == UINT16_C(0x0079));
        CHECK(portable_driver_drift_charge_post_tick(
                  &drift, UINT16_C(0x8200), INT32_C(0x03cb4000),
                  INT32_C(0x04400000), &flags) ==
              PORTABLE_DRIVER_HOP_OK);
        CHECK(drift.charge_counter == 0u && flags == UINT16_C(0x0064));
        CHECK(portable_driver_drift_charge_pre_tick(NULL, 0u, 0u) ==
              PORTABLE_DRIVER_HOP_INVALID_ARGUMENT);
        CHECK(portable_driver_drift_charge_post_tick(
                  &drift, 0u, 0, 0, NULL) ==
              PORTABLE_DRIVER_HOP_INVALID_ARGUMENT);
    }

    {
        uint16_t sink=0u,position=0u;int32_t height=256;
        for(uint16_t step=1u;step<=15u;++step){
            portable_driver_sink_prefix(2u,&sink,&height,&position);
            CHECK(sink==step && height==-(int32_t)step*32768 && position==(uint16_t)(step*2048u));
            CHECK(portable_driver_hidden(2u,height,sink,60u-step)==(step==15u));
        }
        portable_driver_sink_prefix(2u,&sink,&height,&position);
        CHECK(sink==15u && height==-491520 && position==30720u);
        /*9B38 mode2->6 raises the owner but retains76 for this tick. */
        height=0x300000;
        CHECK(portable_driver_hidden(6u,height,sink,0u));
        portable_driver_sink_prefix(6u,&sink,&height,&position);
        CHECK(!sink && height==0x300000 && position==30720u &&
            !portable_driver_hidden(6u,height,sink,0u));
        sink=16u;portable_driver_sink_prefix(2u,&sink,&height,&position);
        CHECK(sink==16u && height==-524288 && position==32768u &&
            !portable_driver_hidden(2u,height,sink,0u));
        sink=UINT16_MAX;portable_driver_sink_prefix(2u,&sink,&height,&position);
        CHECK(sink==UINT16_MAX && height==-2147450880 && position==63488u);
        portable_driver_sink_prefix(0u,&sink,&height,&position);
        CHECK(!sink && height==-2147450880 && position==63488u);
        CHECK(portable_driver_hidden(3u,0,0u,61u) && portable_driver_hidden(4u,0,0u,61u));
        CHECK(!portable_driver_hidden(3u,-1,0u,61u) && !portable_driver_hidden(4u,1,0u,61u));
        CHECK(portable_driver_hidden(8u,1,0u,60u) && !portable_driver_hidden(8u,1,0u,61u));
        CHECK(portable_driver_hidden(0u,1,15u,61u));
    }

    puts("portable driver hop: PASS");
    return 0;
}
