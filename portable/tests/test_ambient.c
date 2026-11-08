#include "test_support.h"
#include "ambient.h"

#define AMBIENT_CHECK(x) do { if(!(x)){fprintf(stderr,"ambient line %d: %s\n",__LINE__,#x);return 1;} } while(0)

/* Invented colors, durations and image bytes; no recovered content is embedded. */
typedef struct {
    PortableAmbientRules rules;
    PortableAmbientLane lanes[16];
    PortableAmbientFrame frames[64];
    PortableAmbientPatch patches[64];
    uint8_t data[128];
} AmbientFixture;

static void ambient_fixture(AmbientFixture *f,unsigned frames)
{
    memset(f,0,sizeof *f);
    f->rules=(PortableAmbientRules){.lanes=f->lanes,.frames=f->frames,
        .patches=f->patches,.data=f->data,.lane_count=1,
        .frame_count=(uint16_t)frames,.patch_count=(uint16_t)frames,.data_size=sizeof f->data};
    f->lanes[0]=(PortableAmbientLane){.stage=PORTABLE_AMBIENT_POST_DRIVER,
        .frame_count=(uint16_t)frames,.loop_frame=0};
    for(unsigned i=0;i<frames;++i){
        f->frames[i]=(PortableAmbientFrame){.duration=3,.patch_count=1,.first_patch=i};
        f->patches[i]=(PortableAmbientPatch){.target=PORTABLE_AMBIENT_ROAD,.count=1,.data_offset=i*2u};
        f->data[i*2u]=(uint8_t)(i+17u);
    }
}

static int ambient_step(PortableAmbientState *s,const AmbientFixture *f,
    unsigned stage,const PortableAmbientInputs *in)
{ return portable_ambient_tick(s,&f->rules,(uint8_t)stage,in,NULL,0); }

int run_ambient(void)
{
    AmbientFixture f;
    PortableAmbientSeed seed={.road={6},.sky={7},.object={8}};
    PortableAmbientInputs in={0};
    PortableAmbientState state={0};
    ambient_fixture(&f,3);
    f.frames[1].patch_count=0;f.lanes[0].loop_frame=UINT16_MAX;
    AMBIENT_CHECK(portable_ambient_init(&state,&f.rules,&seed)==1);
    AMBIENT_CHECK(state.snapshot.road[0]==6 && !state.snapshot.object_dirty[0]);
    AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1 && state.snapshot.road[0]==17);
    state.snapshot.road[0]=90;
    for(unsigned i=0;i<5;++i)AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1 && state.snapshot.road[0]==90);
    AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1 && state.snapshot.road[0]==19);
    for(unsigned i=0;i<3;++i)AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1);
    AMBIENT_CHECK(!state.lanes[0].active && state.snapshot.road[0]==19);

    /* PRE callbacks advance before copying, unlike entry-only script actors. */
    ambient_fixture(&f,2);f.lanes[0].stage=0;f.lanes[0].params[0]=1;
    f.lanes[0].write_policy=PORTABLE_AMBIENT_EVERY_CALL;
    AMBIENT_CHECK(portable_ambient_init(&state,&f.rules,&seed)==1);
    for(unsigned i=0;i<2;++i)AMBIENT_CHECK(ambient_step(&state,&f,0,&in)==1 && state.snapshot.road[0]==17);
    AMBIENT_CHECK(ambient_step(&state,&f,0,&in)==1 && state.snapshot.road[0]==18);
    PortableAmbientState saved=state;uint32_t digest=portable_ambient_digest(&state);
    in.source_gate=1;
    AMBIENT_CHECK(ambient_step(&state,&f,0,&in)==1 && portable_ambient_digest(&state)==digest);
    in.source_gate=0;in.paused=1;
    AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1 && memcmp(&saved,&state,sizeof state)==0);
    AMBIENT_CHECK(portable_ambient_init(&state,NULL,&seed)==0 && memcmp(&saved,&state,sizeof state)==0);
    in.paused=0;

    /* Imported execution order is preserved. A held script cannot erase a later write. */
    ambient_fixture(&f,2);f.rules.lane_count=2;
    f.lanes[0].frame_count=1;f.lanes[1]=f.lanes[0];f.lanes[1].first_frame=1;
    f.patches[1].target=PORTABLE_AMBIENT_OBJECT;
    AMBIENT_CHECK(portable_ambient_init(&state,&f.rules,&seed)==1);
    in.source_gate=1;
    AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1 && state.snapshot.road[0]==17 &&
        state.snapshot.object[0]==18 && state.snapshot.object_dirty[0] && !state.snapshot.object_dirty[1]);
    PortableAmbientSnapshot frozen=state.snapshot;
    state.snapshot.object[0]=91;
    AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1 && state.snapshot.object[0]==91 && frozen.object[0]==18);
    in.source_gate=0;

    /* The callback's sentinel counter refreshes its initial last frame immediately. */
    ambient_fixture(&f,3);f.lanes[0].params[0]=1;f.lanes[0].initial_frame=2;
    f.lanes[0].initial_counter=999;
    AMBIENT_CHECK(portable_ambient_init(&state,&f.rules,&seed)==1);
    AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1 && state.snapshot.road[0]==17);

    ambient_fixture(&f,64);f.lanes[0].stage=0;f.lanes[0].controller=PORTABLE_AMBIENT_SUNSET;
    AMBIENT_CHECK(portable_ambient_init(&state,&f.rules,&seed)==1);
    in.sky_phase_before=1;in.sky_elapsed_before=5;in.sky_counter_before=2;
    AMBIENT_CHECK(ambient_step(&state,&f,0,&in)==1 && state.snapshot.road[0]==6);
    in.sky_counter_before=3;in.sky_phase_after=2;
    AMBIENT_CHECK(ambient_step(&state,&f,0,&in)==1 && state.snapshot.road[0]==22);
    in.sky_phase_before=4;in.sky_elapsed_before=63;
    AMBIENT_CHECK(ambient_step(&state,&f,0,&in)==1 && state.snapshot.road[0]==80);

    ambient_fixture(&f,18);f.lanes[0].stage=0;f.lanes[0].controller=PORTABLE_AMBIENT_DIRECTION_TILES;
    for(unsigned i=0;i<18;++i){f.patches[i].target=PORTABLE_AMBIENT_SKY_TILES;f.patches[i].offset=12;f.patches[i].count=2;}
    AMBIENT_CHECK(portable_ambient_init(&state,&f.rules,&seed)==1);
    in.selected_speed_q16=40;in.selected_base_speed_q16=90;
    AMBIENT_CHECK(ambient_step(&state,&f,0,&in)==1 && !state.snapshot.image_patch_count);
    AMBIENT_CHECK(ambient_step(&state,&f,0,&in)==1 && state.snapshot.image_patch_count==1 && state.snapshot.image_patch_ids[0]==7);
    frozen=state.snapshot;
    for(unsigned i=0;i<6;++i)AMBIENT_CHECK(ambient_step(&state,&f,0,&in)==1);
    AMBIENT_CHECK(state.profile_lanes[0].controller_phase==2 && state.snapshot.image_patch_count==1 && frozen.image_patch_ids[0]==7);
    in.selected_flags=0x20;
    AMBIENT_CHECK(ambient_step(&state,&f,0,&in)==1 && !state.profile_lanes[0].controller_phase);
    in.selected_flags=0;

    ambient_fixture(&f,41);f.lanes[0].controller=PORTABLE_AMBIENT_FINISH_BACKDROP;
    AMBIENT_CHECK(portable_ambient_init(&state,&f.rules,&seed)==1);
    in.race_phase_7d4=3;
    AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1 && state.snapshot.road[0]==6);
    for(unsigned i=0;i<513;++i)AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1);
    AMBIENT_CHECK(state.lanes[0].controller_phase==2 && state.snapshot.road[0]==49);
    in.leader_route_wrap=2;
    AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1 && state.lanes[0].controller_phase==3);
    state.lanes[0].control_counter=512;
    AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1 && state.lanes[0].controller_phase==4);
    AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1 && state.snapshot.road[0]==49);
    for(unsigned i=0;i<512;++i)AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1);
    AMBIENT_CHECK(state.lanes[0].controller_phase==5 && state.snapshot.road[0]==17);

    ambient_fixture(&f,18);f.lanes[0].controller=PORTABLE_AMBIENT_RAINBOW;
    AMBIENT_CHECK(portable_ambient_init(&state,&f.rules,&seed)==1);
    in.leader_progress=129u<<8u;
    AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1 && state.lanes[0].controller_phase==1);
    for(unsigned i=0;i<60;++i)AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1 && state.snapshot.road[0]==6);
    AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1 && state.snapshot.road[0]==17);
    AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1 && state.snapshot.road[0]==17);
    for(unsigned i=1;i<=16;++i)AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1 && state.snapshot.road[0]==17u+i);
    AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1 && state.snapshot.road[0]==34);

    /* Storm spawns are newest-first next turn, and a full pool recycles its oldest actor. */
    ambient_fixture(&f,5);f.rules.lane_count=5;
    f.lanes[0].controller=PORTABLE_AMBIENT_STORM;f.lanes[0].frame_count=1;
    for(unsigned i=1;i<5;++i){f.lanes[i]=(PortableAmbientLane){.stage=1,.trigger=1,.first_frame=(uint16_t)i,.frame_count=1,.loop_frame=UINT16_MAX};f.lanes[0].params[i-1u]=i;}
    AMBIENT_CHECK(portable_ambient_init(&state,&f.rules,&seed)==1);
    in.clock=0;in.scene_mode_80d=2;
    AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1 && !state.lanes[0].counter);
    in.scene_mode_80d=0;
    for(unsigned i=0;i<126;++i)AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1);
    AMBIENT_CHECK(state.snapshot.road[0]==6 && state.lanes[1].pending && state.lanes[4].pending);
    AMBIENT_CHECK(ambient_step(&state,&f,1,&in)==1 && state.snapshot.road[0]==21);
    for(unsigned i=0;i<20;++i)AMBIENT_CHECK(portable_ambient_spawn(&state,&f.rules,1)==1);
    unsigned active=0;uint32_t oldest=UINT32_MAX;
    for(unsigned i=0;i<16;++i)if(state.lanes[i].active){++active;if(state.lanes[i].creation_order<oldest)oldest=state.lanes[i].creation_order;}
    AMBIENT_CHECK(active==16 && oldest>=state.next_creation_order-16u);
    state.next_creation_order=UINT32_MAX;
    AMBIENT_CHECK(portable_ambient_spawn(&state,&f.rules,1)==1 && state.next_creation_order<32u);
    PortableAmbientRules bad=f.rules;bad.patch_count=769;
    AMBIENT_CHECK(!portable_ambient_rules_valid(&bad));
    digest=portable_ambient_digest(&state);state.snapshot.object_dirty[127]=1;
    AMBIENT_CHECK(portable_ambient_digest(&state)!=digest);
    puts("portable ambient: PASS");
    return 0;
}
