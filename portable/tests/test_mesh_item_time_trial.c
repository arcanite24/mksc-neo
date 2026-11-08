#include "mesh_time_trial.h"
#include "mesh_course_fixture.h"
#include "mesh_item_fixture.h"
#include "mesh_item_rules_asset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define Q 65536
_Static_assert(PORTABLE_MESH_TIME_TRIAL_STATE_WORDS == 44, "legacy TT wire stays 44 words");
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"item TT line %d: %s\n",__LINE__,#x); return 1; } } while (0)
typedef struct
{
    PortableTestMeshCourse floor;
    PortableMeshItemRules rules;
    PortableMeshTimeTrial trial;
    PortableMeshItems world;
    PortableMeshItemsConfig config;
    PortableMeshKartEvents events;
    PortableMeshItemEvents item_events;
    uint8_t vertices[96], triangles[128], item[32];
} Fixture;
static int setup(Fixture *f,int stacked)
{
    memset(f,0,sizeof *f);test_mesh_course_init(&f->floor);
    uint8_t payload[PORTABLE_MESH_ITEM_RULES_BYTES];test_mir_payload(payload);
    const PortableAssetSectionView section={.kind=103,.content_id=41,.version=1,
        .bytes=payload,.size=sizeof payload};
    if(!portable_mesh_item_rules_asset_open(&section,&f->rules))return 0;
    f->rules.mushroom_boost_ticks=4;
    f->rules.mushroom_acceleration_q16=24*Q;
    f->rules.mushroom_cap_ratio_q16=2*Q;
    if(stacked)
    {
        memcpy(f->vertices,f->floor.vertices,48);
        memcpy(f->vertices+48,f->floor.vertices,48);
        for(unsigned i=0;i<4;++i)test_mesh_word(f->vertices+48+i*12+8,4*Q);
        memcpy(f->triangles,f->floor.triangles,64);
        memcpy(f->triangles+64,f->floor.triangles,64);
        for(unsigned i=0;i<2;++i)for(unsigned j=0;j<3;++j)
            test_mesh_word(f->triangles+64+i*32+j*4,(i==0?(j==0?0:j==1?1:2):(j==0?0:j==1?2:3))+4);
        f->floor.course.tables[2].bytes=f->vertices;f->floor.course.tables[2].count=8;
        f->floor.course.tables[3].bytes=f->triangles;f->floor.course.tables[3].count=4;
    }
    /* An authored box directly on the start path proves TT excludes boxes. */
    test_mesh_word(f->item+4,4*Q);test_mesh_word(f->item+8,3*Q/4);
    test_mesh_word(f->item+12,Q);test_mesh_word(f->item+20,90);
    f->floor.course.tables[f->floor.course.table_count++]=(PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('I','T','E','M'),1,32,f->item};
    f->config=(PortableMeshItemsConfig){&f->floor.course,&f->rules,&f->floor.rules,2,1,1,0,0, NULL};
    char error[256];
    if(portable_mesh_time_trial_init(&f->trial,&f->floor.course,&f->floor.rules,0,error,sizeof error)!=PORTABLE_MESH_OK)
    {fprintf(stderr,"TT init: %s\n",error);return 0;}
    return portable_mesh_items_init(&f->world,&f->config,123,error,sizeof error)==PORTABLE_MESH_ITEMS_OK;
}
static int tick(Fixture *f,PortableMeshKartInput input,int held)
{
    char error[256];const PortableMeshItemControls control={.item_pressed=(uint8_t)held};
    const PortableMeshStatus result=portable_mesh_time_trial_tick_items(&f->trial,&f->world,
        &f->config,input,control,&f->events,&f->item_events,error,sizeof error);
    if(result!=PORTABLE_MESH_OK)fprintf(stderr,"TT items: %s\n",error);
    return result==PORTABLE_MESH_OK;
}
static int countdown(Fixture *f)
{
    for(unsigned i=0;i<180;++i)if(!tick(f,(PortableMeshKartInput){0,0},1))return 0;
    return f->trial.state.phase==PORTABLE_MESH_TT_RACING;
}
static int legacy_wire_and_finish(void)
{
    Fixture *f=calloc(1,sizeof *f);PortableMeshItems *idle=malloc(sizeof *idle);
    CHECK(f && idle && setup(f,0));memcpy(idle,&f->world,sizeof *idle);
    CHECK(f->world.inventory.racers[0].held_item==0x1008);
    PortableMeshTimeTrial legacy=f->trial;
    uint8_t a[44*4],b[44*4];char error[256];PortableMeshKartEvents old_events;
    unsigned elapsed=0;
    for(;elapsed<1000 && f->trial.state.phase!=PORTABLE_MESH_TT_FINISHED;++elapsed)
    {
        const PortableMeshKartInput input={0,elapsed<180?0:PORTABLE_MESH_ACCELERATE};
        CHECK(portable_mesh_time_trial_tick(&legacy,input,&old_events,error,sizeof error)==PORTABLE_MESH_OK);
        CHECK(tick(f,input,0));
        CHECK(portable_mesh_time_trial_state_write(&legacy.state,a,sizeof a));
        CHECK(portable_mesh_time_trial_state_write(&f->trial.state,b,sizeof b));
        CHECK(!memcmp(a,b,sizeof a) && !memcmp(&old_events,&f->events,sizeof old_events));
        CHECK(portable_mesh_time_trial_checksum(&legacy.state)==portable_mesh_time_trial_checksum(&f->trial.state));
        CHECK(!f->item_events.count && !f->world.boxes.cooldowns[0] && f->world.inventory.seed==123);
        if(elapsed<180)CHECK(!memcmp(idle,&f->world,sizeof *idle));
    }
    CHECK(f->trial.state.phase==PORTABLE_MESH_TT_FINISHED && f->trial.state.finish_ticks>0);
    CHECK(f->world.inventory.racers[0].held_item==0x1008);
    memcpy(idle,&f->world,sizeof *idle);
    CHECK(tick(f,(PortableMeshKartInput){0,0},1));
    CHECK(!f->item_events.count && !memcmp(idle,&f->world,sizeof *idle));
    free(f);free(idle);return 0;
}
static int mushrooms_rescue_and_pause(void)
{
    for(unsigned stacked=0;stacked<2;++stacked)
    {
        Fixture *f=calloc(1,sizeof *f);PortableMeshItems *paused=malloc(sizeof *paused);
        CHECK(f && paused && setup(f,(int)stacked) && countdown(f));
        CHECK(f->world.tick==0 && !f->world.previous_item[0]);
        CHECK(tick(f,(PortableMeshKartInput){0,0},0)); /* Establish real floor support. */
        CHECK(f->trial.state.kart.grounded && f->trial.state.kart.position_q16[2]==3*Q/4);
        const uint16_t successors[3]={0x100f,0x1007,0};
        for(unsigned i=0;i<3;++i)
        {
            CHECK(tick(f,(PortableMeshKartInput){0,0},1));
            CHECK(f->world.inventory.racers[0].held_item==successors[i]);
            CHECK(f->world.status.racers[0].mushroom_ticks==f->rules.mushroom_boost_ticks);
            CHECK(f->item_events.count==2 && f->item_events.values[0].kind==PORTABLE_MESH_EVENT_USE &&
                  f->item_events.values[1].kind==PORTABLE_MESH_EVENT_BOOST);
            CHECK(f->trial.state.kart.speed_q16>0 && f->trial.state.kart.position_q16[2]==3*Q/4);
            CHECK(tick(f,(PortableMeshKartInput){0,0},0));
        }
        memcpy(paused,&f->world,sizeof *paused);
        const uint64_t sum=portable_mesh_time_trial_checksum(&f->trial.state);
        for(unsigned view=0;view<8;++view)
        {
            uint16_t hud;
            CHECK(portable_mesh_items_hud(&f->world,0,&hud)==PORTABLE_MESH_ITEMS_OK && !hud);
            CHECK(portable_mesh_time_trial_checksum(&f->trial.state)==sum);
        }
        CHECK(!memcmp(paused,&f->world,sizeof *paused)); /* Pause admits no tick calls. */
        CHECK(setup(f,(int)stacked) && countdown(f));
        CHECK(tick(f,(PortableMeshKartInput){0,0},0));
        CHECK(tick(f,(PortableMeshKartInput){0,PORTABLE_MESH_RESCUE},1));
        CHECK(f->world.inventory.racers[0].held_item==0x1008 && !f->world.status.racers[0].pending_mushroom);
        CHECK(!f->world.status.racers[0].mushroom_ticks && !f->item_events.count);
        CHECK(f->trial.state.kart.respawn_ticks==f->floor.rules.respawn_ticks);
        CHECK(!f->world.boxes.cooldowns[0]);
        free(f);free(paused);
    }
    return 0;
}
static int rollback(void)
{
    Fixture *f=calloc(1,sizeof *f),*saved=malloc(sizeof *saved);
    CHECK(f && saved && setup(f,0) && countdown(f));
    CHECK(tick(f,(PortableMeshKartInput){0,0},0));
    /* Legal owner metadata, but malformed late projectile support. Preparation
       and kart movement precede the rejected projectile motion in resolve. */
    f->world.serial=1;
    f->world.actors[0]=(PortableMeshItemActor){.serial=1,.item=0x1005,
        .phase=PORTABLE_MESH_ACTOR_FLYING,.owner=0,.target=UINT8_MAX,.lifetime=100,.grace=10};
    f->world.actors[0].motion.mode=PORTABLE_MESH_ITEM_BANANA;
    f->world.actors[0].motion.position_q16[0]=10*Q;
    f->world.actors[0].motion.position_q16[2]=Q;
    memset(&f->events,0xa5,sizeof f->events);memset(&f->item_events,0xa5,sizeof f->item_events);
    memcpy(saved,f,sizeof *saved);
    char error[256];
    CHECK(portable_mesh_time_trial_tick_items(&f->trial,&f->world,&f->config,
        (PortableMeshKartInput){0,PORTABLE_MESH_ACCELERATE},(PortableMeshItemControls){.item_pressed=1},
        &f->events,&f->item_events,error,sizeof error)!=PORTABLE_MESH_OK);
    CHECK(!memcmp(&f->trial,&saved->trial,sizeof f->trial) && !memcmp(&f->world,&saved->world,sizeof f->world));
    CHECK(!memcmp(&f->events,&saved->events,sizeof f->events) && !memcmp(&f->item_events,&saved->item_events,sizeof f->item_events));
    /* A mismatched borrowed course/config is rejected before any staging. */
    f->config.count=2;memcpy(saved,f,sizeof *saved);
    CHECK(portable_mesh_time_trial_tick_items(&f->trial,&f->world,&f->config,
        (PortableMeshKartInput){0,0},(PortableMeshItemControls){0},&f->events,&f->item_events,
        error,sizeof error)==PORTABLE_MESH_INVALID);
    CHECK(!memcmp(&f->trial,&saved->trial,sizeof f->trial) && !memcmp(&f->world,&saved->world,sizeof f->world));
    CHECK(!memcmp(&f->events,&saved->events,sizeof f->events) && !memcmp(&f->item_events,&saved->item_events,sizeof f->item_events));
    free(f);free(saved);return 0;
}
static int capture_tick(Fixture *f,PortableMeshCourseActors *actors,
    const PortableMeshCourseActorsConfig *config,PortableMeshKartInput input,int pressed,
    PortableMeshCourseActorEvents *actor_events)
{
    char error[256];
    const PortableMeshStatus result=portable_mesh_time_trial_tick_actors(&f->trial,&f->world,
        &f->config,actors,config,input,(PortableMeshItemControls){.item_pressed=(uint8_t)pressed},
        &f->events,&f->item_events,actor_events,error,sizeof error);
    if(result!=PORTABLE_MESH_OK)fprintf(stderr,"TT capture items: %s\n",error);
    return result==PORTABLE_MESH_OK;
}
static int coin_only_tt_defaults(void)
{
    Fixture *f=calloc(1,sizeof *f),*saved=malloc(sizeof *saved);
    PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
    PortableMeshCourseActors *actors=malloc(sizeof *actors),*old=malloc(sizeof *old);
    CHECK(f && saved && config && actors && old && setup(f,0) && countdown(f));
    config->course_id=f->floor.course.id;config->profile_id=5;config->revision=1;
    config->coins=(PortableMeshCourseCoinsConfig){.course_id=f->floor.course.id,.enabled=1,
        .profile_id=5,.revision=1,.modes=13,.cap=99,.initial=5,.placement_count=1,
        .loss={4,2,1,8,1}};
    for(unsigned i=0;i<100;++i)config->coins.ratios_q16[i]=Q;
    config->coins.placements[0]=(PortableMeshCourseCoin){.id=1,.model_id=1,
        .position_q16={0,4*Q,0},.half_q16={Q,Q,Q/2}};
    CHECK(!portable_mesh_course_actors_config_count(config) &&
          portable_mesh_course_actors_init(config,actors,NULL,0));
    PortableMeshCourseActorEvents events={0};
    CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,0},0,&events));
    CHECK(actors->coins.count[0]==5 && actors->coins.available==1 && !events.coins.picked[0]);
    CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,PORTABLE_MESH_RESCUE},0,&events));
    CHECK(f->trial.state.kart.respawn_ticks==f->floor.rules.respawn_ticks &&
          actors->coins.count[0]==5 && !events.coins.lost[0] && actors->coins.available==1);
    *saved=*f;*old=*actors;
    const PortableMeshCourseActorEvents old_events=events;
    config->coins.ratios_q16[0]=0;
    CHECK(!capture_tick(f,actors,config,(PortableMeshKartInput){0,0},0,&events));
    CHECK(!memcmp(&f->trial,&saved->trial,sizeof f->trial) &&
          !memcmp(&f->world,&saved->world,sizeof f->world) &&
          !memcmp(&f->events,&saved->events,sizeof f->events) &&
          !memcmp(&f->item_events,&saved->item_events,sizeof f->item_events) &&
          !memcmp(actors,old,sizeof *actors) && !memcmp(&events,&old_events,sizeof events));
    free(old);free(actors);free(config);free(saved);free(f);return 0;
}
static int pier_boo_owner(void)
{
    Fixture *f=calloc(1,sizeof *f);PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
    PortableMeshCourseActors *actors=malloc(sizeof *actors);CHECK(f && config && actors);
    for(unsigned scenario=0;scenario<7;++scenario)
    {
        CHECK(setup(f,0) && countdown(f));f->rules.star_player_ticks=500;
        memset(config,0,sizeof *config);config->course_id=f->floor.course.id;
        config->profile_id=config->revision=1;config->scale_q16=6554;config->pier_boo_count=1;
        config->actor_visual_seed=123;config->pier_boo_rules=(PortableBooRules){1,360,64,1024,1024,3*Q,Q/4};
        CHECK(portable_course_prop_init(&config->initial_pier_boos[0],100*Q,100*Q,0xa30000,256,0));
        config->pier_boo_anchors_q16[0][1]=4*Q;config->pier_boo_anchors_q16[0][2]=scenario==4?4*Q:0;
        for(unsigned n=0;n<3;++n)config->pier_boo_frame_ids[0][n]=230+n;
        CHECK(portable_mesh_course_actors_init(config,actors,NULL,0));
        if(scenario==3)f->world.inventory.racers[0]=(PortableMeshItemRacer){.held_item=0x100a,.settled_item=0x100a,.preview_item=0x100a};
        if(scenario==6)f->trial.state.phase=PORTABLE_MESH_TT_FINISHED;
        const uint64_t before=portable_mesh_course_actors_checksum(actors);
        PortableMeshCourseActorEvents events={0};
        CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,scenario==5?PORTABLE_MESH_RESCUE:0},scenario==3,&events));
        CHECK(!events.hit_mask && !f->world.status.racers[0].spin_ticks && !f->world.status.racers[0].boo_invisible_ticks);
        if(scenario>=4){CHECK(!events.pier_boo_contacts);if(scenario==6)CHECK(before==portable_mesh_course_actors_checksum(actors));continue;}
        if(scenario==3){CHECK(events.pier_boo_protected_mask==1 && actors->pier_boos[0].state==5 &&
                              !actors->source_haunts[0].occupied && !events.source_haunt_music_start_mask);continue;}
        CHECK(events.pier_boo_touch_mask==1 && !(actors->pier_boos[0].flags&1) &&
              actors->source_haunts[0].occupied && actors->source_haunts[0].target==0 &&
              actors->source_haunts[0].phase==1 && actors->source_haunts[0].timer==63 &&
              !events.source_haunt_music_start_mask && !f->world.status.racers[0].boo_lock_ticks);
        if(scenario==1)f->world.inventory.racers[0]=(PortableMeshItemRacer){.held_item=0x100a,.settled_item=0x100a,.preview_item=0x100a};
        for(unsigned n=1;n<64;++n)
        {
            CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,0},scenario==1 && n==1,&events));
            if(n<63)CHECK(!events.source_haunt_assigned_mask && !events.source_haunt_music_start_mask);
        }
        if(scenario==1){CHECK(actors->source_haunts[0].phase==6 && !events.source_haunt_music_start_mask &&
                              !events.source_haunt_music_release_mask && !f->world.status.racers[0].boo_lock_ticks);continue;}
        CHECK(actors->source_haunts[0].phase==2 && events.source_haunt_assigned_mask==1 &&
              events.source_haunt_music_start_mask==1 && f->world.status.racers[0].boo_lock_ticks==360);
        if(scenario==2)
        {
            f->world.inventory.racers[0]=(PortableMeshItemRacer){.held_item=0x100a,.settled_item=0x100a,.preview_item=0x100a};
            CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,0},1,&events));
            CHECK(actors->source_haunts[0].phase==6 && events.source_haunt_music_release_mask==1 && !f->world.status.racers[0].boo_lock_ticks);
            continue;
        }
        const uint64_t paused=portable_mesh_course_actors_checksum(actors);
        PortableMeshCourseActorView view;CHECK(portable_mesh_course_actors_view(config,actors,0,&view));
        CHECK(paused==portable_mesh_course_actors_checksum(actors));
        for(unsigned remaining=359;;--remaining)
        {
            CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,0},0,&events));
            CHECK(f->world.status.racers[0].boo_lock_ticks==remaining && !events.hit_mask);
            if(!remaining)break;
        }
        CHECK(actors->source_haunts[0].phase==4 && events.source_haunt_music_release_mask==1);
        for(unsigned n=0;n<64;++n)CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,0},0,&events));
        CHECK(!actors->source_haunts[0].occupied && !f->world.status.racers[0].boo_lock_ticks);
    }
    free(actors);free(config);free(f);return 0;
}
static int kamek_owner(void)
{
    Fixture *f=calloc(1,sizeof *f);
    PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
    PortableMeshCourseActors *actors=malloc(sizeof *actors);
    CHECK(f && config && actors);
    for(unsigned scenario=0;scenario<5;++scenario)
    {
        CHECK(setup(f,0) && countdown(f));
        memset(config,0,sizeof *config);config->course_id=f->floor.course.id;
        config->profile_id=config->revision=1;config->scale_q16=6554;config->kamek_count=1;
        CHECK(portable_course_prop_init(&config->initial_kameks[0],100*Q,100*Q,0xa00000,224,0));
        config->kamek_anchors_q16[0][1]=4*Q;
        config->kamek_anchors_q16[0][2]=scenario==2?4*Q:-Q/2;
        config->kamek_frame_ids[0][0]=220;config->kamek_frame_ids[0][1]=221;
        CHECK(portable_mesh_course_actors_init(config,actors,NULL,0));
        if(scenario==4)f->trial.state.phase=PORTABLE_MESH_TT_FINISHED;
        const uint64_t before=portable_mesh_course_actors_checksum(actors);
        PortableMeshCourseActorEvents events={0};
        CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,scenario==3?PORTABLE_MESH_RESCUE:0},0,&events));
        CHECK(!events.hit_mask && !events.bump_mask && !f->world.status.racers[0].spin_ticks &&
              !actors->lava_burn_ticks[0] && !f->trial.state.kart.velocity_q16[0] &&
              !f->trial.state.kart.velocity_q16[1]);
        if(scenario>=2){CHECK(!events.kamek_contacts);
            if(scenario==4)CHECK(portable_mesh_course_actors_checksum(actors)==before);
            continue;}
        CHECK(events.kamek_contacts==1 && events.kamek_touch_mask==1 && actors->actor_grace[0]==8);
        if(scenario==0)
        {
            CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,0},0,&events));
            CHECK(!events.kamek_contacts && actors->actor_grace[0]==7);
            continue;
        }
        /* Fresh accepted Star bypasses prior ordinary grace, without creating
         * a new hit/grace or disturbing the frozen source XY patrol. */
        f->world.inventory.racers[0]=(PortableMeshItemRacer){
            .held_item=0x100a,.settled_item=0x100a,.preview_item=0x100a};
        CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,0},1,&events));
        CHECK(events.kamek_protected_mask==1 && !events.kamek_touch_mask && !events.hit_mask &&
              actors->actor_grace[0]==7 && actors->kameks[0].state==5);
        PortableCourseProps source={.count=1};source.actors[0]=actors->kameks[0];
        const int32_t patrol_x=source.actors[0].x_q16;
        const uint64_t paused=portable_mesh_course_actors_checksum(actors);
        PortableMeshCourseActorView view;CHECK(portable_mesh_course_actors_view(config,actors,0,&view));
        CHECK(portable_mesh_course_actors_checksum(actors)==paused);
        unsigned turns=0;
        while((actors->kameks[0].flags&1) && turns++<64)
        {
            CHECK(portable_course_props_advance(&source,config->sine,1024));
            CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,0},0,&events));
            CHECK(!memcmp(&source.actors[0],&actors->kameks[0],sizeof source.actors[0]) &&
                  actors->kameks[0].x_q16==patrol_x && !events.hit_mask);
            if(turns==1)CHECK(actors->kameks[0].state==6 && actors->kameks[0].affine_y_q8==-256 &&
                             actors->kameks[0].kamek.velocity_z_q16==3*Q-Q/4);
        }
        CHECK(turns>1 && turns<64 && actors->kameks[0].state==8 && actors->kameks[0].height_q16<0);
    }
    free(actors);free(config);free(f);return 0;
}
static int mechakoopa_owner(void)
{
    Fixture *f=calloc(1,sizeof *f);
    PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
    PortableMeshCourseActors *actors=malloc(sizeof *actors);
    CHECK(f && config && actors);
    for(unsigned scenario=0;scenario<5;++scenario)
    {
        CHECK(setup(f,0) && countdown(f));
        memset(config,0,sizeof *config);config->course_id=f->floor.course.id;
        config->profile_id=config->revision=1;config->scale_q16=6554;config->mechakoopa_count=1;
        CHECK(portable_course_prop_init(&config->initial_mechakoopas[0],100*Q,100*Q,0xa10000,224,0));
        config->mechakoopa_anchors_q16[0][1]=4*Q;
        config->mechakoopa_anchors_q16[0][2]=scenario==2?4*Q:0;
        for(unsigned n=0;n<4;++n)config->mechakoopa_frame_ids[0][n]=210+n;
        CHECK(portable_mesh_course_actors_init(config,actors,NULL,0));
        if(scenario==1)f->world.inventory.racers[0]=(PortableMeshItemRacer){
            .held_item=0x100a,.settled_item=0x100a,.preview_item=0x100a};
        if(scenario==4)f->trial.state.phase=PORTABLE_MESH_TT_FINISHED;
        const uint64_t before=portable_mesh_course_actors_checksum(actors);
        PortableMeshCourseActorEvents events={0};
        CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,scenario==3?PORTABLE_MESH_RESCUE:0},scenario==1,&events));
        CHECK(!actors->lava_burn_ticks[0] && !f->world.status.racers[0].lightning_ticks);
        if(scenario>=2){CHECK(!events.mechakoopa_contacts);
            if(scenario==4)CHECK(portable_mesh_course_actors_checksum(actors)==before);
            continue;}
        CHECK(events.mechakoopa_contacts==1 && !events.mechakoopa_strong_mask &&
              !actors->strong_hit[0].pending && !actors->strong_hit[0].active);
        if(scenario==0)
        {
            CHECK(events.mechakoopa_hit_mask==1 && events.hit_mask==1 &&
                  f->world.status.racers[0].spin_ticks>0 && actors->actor_grace[0]==8 &&
                  !f->trial.state.kart.velocity_q16[0] && !f->trial.state.kart.velocity_q16[1]);
            CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,0},0,&events));
            CHECK(!events.mechakoopa_contacts && actors->actor_grace[0]==7);
        }
        else
        {
            CHECK(events.mechakoopa_protected_mask==1 && !events.hit_mask &&
                  !actors->actor_grace[0] && actors->mechakoopas[0].state==1 && actors->mechakoopas[0].timer==32);
            const uint64_t paused=portable_mesh_course_actors_checksum(actors);
            PortableMeshCourseActorView view;CHECK(portable_mesh_course_actors_view(config,actors,0,&view));
            CHECK(portable_mesh_course_actors_checksum(actors)==paused);
            for(unsigned n=0;n<33;++n)CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,0},0,&events));
            CHECK(!(actors->mechakoopas[0].flags&1) && actors->mechakoopas[0].timer==-1);
        }
    }
    free(actors);free(config);free(f);return 0;
}
static int common_prop_owner(void)
{
    Fixture *f=calloc(1,sizeof *f);
    PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
    PortableMeshCourseActors *actors=malloc(sizeof *actors);
    CHECK(f && config && actors);
    for(unsigned scenario=0;scenario<5;++scenario)
    {
        CHECK(setup(f,0) && countdown(f));
        memset(config,0,sizeof *config);config->course_id=f->floor.course.id;
        config->profile_id=config->revision=1;config->scale_q16=6554;config->common_prop_count=1;
        CHECK(portable_course_prop_init(&config->initial_common_props[0],100*Q,100*Q,0x860000,256,0));
        config->common_prop_anchors_q16[0][0]=Q/4;
        config->common_prop_anchors_q16[0][1]=4*Q;
        config->common_prop_anchors_q16[0][2]=scenario==2?4*Q:0;
        config->common_prop_frame_ids[0][0]=200;config->common_prop_frame_ids[0][1]=201;
        CHECK(portable_mesh_course_actors_init(config,actors,NULL,0));
        if(scenario==1)f->world.inventory.racers[0]=(PortableMeshItemRacer){
            .held_item=0x100a,.settled_item=0x100a,.preview_item=0x100a};
        PortableMeshCourseActorEvents events={0};
        if(scenario==4)f->trial.state.phase=PORTABLE_MESH_TT_FINISHED; /* Finished-owner fixture. */
        const uint64_t before=portable_mesh_course_actors_checksum(actors);
        CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,scenario==3?PORTABLE_MESH_RESCUE:0},scenario==1,&events));
        CHECK(!events.hit_mask && !f->world.status.racers[0].spin_ticks && !actors->lava_burn_ticks[0]);
        if(scenario>=2){CHECK(!events.common_prop_contacts);
            if(scenario==4)CHECK(portable_mesh_course_actors_checksum(actors)==before);
            continue;}
        CHECK(events.common_prop_contacts==1);
        if(scenario==0)
        {
            CHECK(events.common_prop_hit_mask==1 && events.common_prop_umbrella_open_mask==1 &&
                  !events.common_prop_protected_mask && actors->common_props[0].contact_phase==1 &&
                  actors->actor_grace[0]==8 && !actors->common_props[0].state);
            CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,0},0,&events));
            CHECK(!events.common_prop_contacts && actors->actor_grace[0]==7);
        }
        else
        {
            CHECK(events.common_prop_protected_mask==1 && !events.common_prop_hit_mask &&
                  !events.common_prop_umbrella_open_mask && !actors->actor_grace[0] &&
                  actors->common_props[0].state==1 && actors->common_props[0].timer==32);
            const uint64_t paused=portable_mesh_course_actors_checksum(actors);
            PortableMeshCourseActorView view;
            CHECK(portable_mesh_course_actors_view(config,actors,0,&view));
            CHECK(portable_mesh_course_actors_checksum(actors)==paused);
            for(unsigned n=0;n<33;++n)
                CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,0},0,&events));
            CHECK(!(actors->common_props[0].flags&1) && actors->common_props[0].timer==-1);
        }
    }
    free(actors);free(config);free(f);return 0;
}
static int thundercloud_owner(void)
{
    Fixture *f=calloc(1,sizeof *f);
    PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
    PortableMeshCourseActors *actors=malloc(sizeof *actors);
    CHECK(f && config && actors);
    for(unsigned protected=0;protected<2;++protected)
    {
        CHECK(setup(f,0) && countdown(f));f->rules.star_player_ticks=500;
        memset(config,0,sizeof *config);config->course_id=f->floor.course.id;
        config->profile_id=config->revision=1;config->scale_q16=6554;config->thundercloud_count=1;
        CHECK(portable_thundercloud_init(&config->initial_thunderclouds[0],100*Q,100*Q,0,0));
        config->thundercloud_anchors_q16[0][1]=4*Q;
        for(unsigned i=0;i<72;++i)config->thundercloud_frame_ids[0][i]=80+i;
        CHECK(portable_mesh_course_actors_init(config,actors,NULL,0));
        if(protected)f->world.inventory.racers[0]=(PortableMeshItemRacer){
            .held_item=0x100a,.settled_item=0x100a,.preview_item=0x100a};
        PortableMeshCourseActorEvents events={0};unsigned hit=0;
        for(unsigned turn=0;turn<300 && !hit;++turn)
        {
            CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,0},protected && turn==0,&events));
            hit=protected ? events.thundercloud_protected_mask&1 : events.thundercloud_hit_mask&1;
        }
        CHECK(hit && events.thundercloud_contacts && !f->world.status.racers[0].spin_ticks &&
              !f->trial.state.kart.velocity_q16[0] && !f->trial.state.kart.velocity_q16[1]);
        if(protected)
        {
            CHECK(!f->world.status.racers[0].lightning_ticks && !actors->strong_hit[0].pending &&
                  !actors->strong_hit[0].active && actors->thunderclouds[0].flags==3);
            continue;
        }
        CHECK(f->world.status.racers[0].lightning_ticks==360 && actors->strong_hit[0].pending);
        CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,0},0,&events));
        CHECK(f->world.status.racers[0].lightning_ticks==359 && actors->strong_hit[0].active &&
              !actors->strong_hit[0].pending && actors->strong_hit[0].angle);
        f->trial.state.kart.position_q16[0]=10*Q; /* Supported, beyond this cloud's next strike. */
        unsigned angles=0;uint16_t previous=actors->strong_hit[0].angle;
        for(unsigned remaining=358;;--remaining)
        {
            CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,0},0,&events));
            CHECK(f->world.status.racers[0].lightning_ticks==remaining && !f->trial.state.kart.respawn_ticks);
            angles+=actors->strong_hit[0].angle!=previous;previous=actors->strong_hit[0].angle;
            if(!remaining)break;
        }
        CHECK(angles>2 && !actors->strong_hit[0].active && !actors->strong_hit[0].pending &&
              !actors->strong_hit[0].angle && actors->strong_hit[0].rate==0xc0);
    }
    free(actors);free(config);free(f);return 0;
}
static int falling_star_owner(void)
{
    Fixture *f=calloc(1,sizeof *f);
    PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
    PortableMeshCourseActors *actors=malloc(sizeof *actors);
    CHECK(f && config && actors);
    for(unsigned protected=0;protected<2;++protected)
    {
        CHECK(setup(f,0) && countdown(f));
        f->rules.star_player_ticks=200;
        memset(config,0,sizeof *config);config->course_id=f->floor.course.id;
        config->profile_id=config->revision=1;config->scale_q16=6554;
        config->falling_star_count=1;
        CHECK(portable_falling_star_init(&config->initial_falling_stars[0],100*Q,100*Q,0,0));
        config->falling_star_anchors_q16[0][1]=4*Q;
        for(unsigned i=0;i<5;++i)config->falling_star_frame_ids[0][i]=70+i;
        CHECK(portable_mesh_course_actors_init(config,actors,NULL,0));
        if(protected)f->world.inventory.racers[0]=(PortableMeshItemRacer){
            .held_item=0x100a,.settled_item=0x100a,.preview_item=0x100a};
        unsigned hit=0;
        PortableMeshCourseActorEvents events={0};
        for(unsigned turn=0;turn<140 && !hit;++turn)
        {
            CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,0},protected && turn==0,&events));
            hit=protected ? events.falling_star_protected_mask&1 : events.falling_star_hit_mask&1;
        }
        CHECK(hit && events.falling_star_contacts && !f->world.status.racers[0].spin_ticks);
        if(protected)
            CHECK((events.protected_mask&1) && !events.hit_mask && !actors->strong_hit[0].pending &&
                  !actors->strong_hit[0].active && (actors->falling_stars[0].flags&2));
        else
        {
            CHECK((events.hit_mask&1) && (actors->strong_hit[0].pending || actors->strong_hit[0].active));
            /* Model the next supported contact pose on this real flat fixture,
             * so pending-air admission cannot conceal a missing wrapper clock. */
            f->trial.state.kart.position_q16[2]=f->floor.rules.kart_radius_q16+f->floor.rules.ride_height_q16;
            f->trial.state.kart.velocity_q16[2]=0;f->trial.state.kart.grounded=1;
            const uint16_t admitted_rate=actors->strong_hit[0].rate;
            CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,0},0,&events));
            CHECK(!actors->strong_hit[0].pending && actors->strong_hit[0].active &&
                  actors->strong_hit[0].rate<admitted_rate && actors->strong_hit[0].angle);
            /* A supported ordinary kart pose away from the source star keeps
             * this assertion about one admitted controller, not repeated hits. */
            f->trial.state.kart.position_q16[0]=10*Q;
            uint16_t last_angle=actors->strong_hit[0].angle;
            unsigned advanced=0;
            for(unsigned turn=0;turn<256 && actors->strong_hit[0].active;++turn)
            {
                CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,0},0,&events));
                advanced+=actors->strong_hit[0].angle!=last_angle;
                last_angle=actors->strong_hit[0].angle;
                CHECK(!f->trial.state.kart.respawn_ticks);
            }
            CHECK(advanced>2 && !actors->strong_hit[0].pending && !actors->strong_hit[0].active &&
                  !actors->strong_hit[0].angle && actors->strong_hit[0].rate==0xc0);
        }
    }
    free(actors);free(config);free(f);return 0;
}
static int teepee_attachment_owner(void)
{
    Fixture *f=calloc(1,sizeof *f);
    PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
    PortableMeshCourseActors *actors=malloc(sizeof *actors);
    CHECK(f && config && actors);
    for(unsigned star=0;star<2;++star)
    {
        CHECK(setup(f,0) && countdown(f));
        memset(config,0,sizeof *config);
        config->course_id=f->floor.course.id;config->profile_id=config->revision=1;
        config->scale_q16=6554;config->teepee_count=1;
        CHECK(portable_course_prop_init(&config->initial_teepees[0],100*Q,100*Q,
                                        0x030000u,256,0));
        config->teepee_anchors_q16[0][1]=4*Q;
        for(unsigned frame=0;frame<11;++frame)config->teepee_frame_ids[0][frame]=50+frame;
        CHECK(portable_mesh_course_actors_init(config,actors,NULL,0));
        if(star)f->world.inventory.racers[0]=(PortableMeshItemRacer){
            .held_item=0x100a,.settled_item=0x100a,.preview_item=0x100a};
        PortableMeshCourseActorEvents events={0};
        CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,0},star,&events));
        CHECK(!events.hit_mask && !f->world.status.racers[0].spin_ticks);
        if(star)
        {
            CHECK(f->world.status.racers[0].star_ticks && !f->world.status.racers[0].boo_lock_ticks &&
                  actors->teepees[0].teepee.rider_state==2);
            continue;
        }
        CHECK(actors->teepees[0].teepee.rider_state==1 &&
              f->world.status.racers[0].boo_lock_ticks==360);
        CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,PORTABLE_MESH_ACCELERATE},0,&events));
        CHECK(f->world.status.racers[0].boo_lock_ticks==349 && !events.hit_mask);
        CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,PORTABLE_MESH_ACCELERATE},0,&events));
        CHECK(f->world.status.racers[0].boo_lock_ticks==348); /* HeldA is not another edge. */
        CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,0},0,&events));
        CHECK(f->world.status.racers[0].boo_lock_ticks==347);
        CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,PORTABLE_MESH_ACCELERATE},0,&events));
        CHECK(f->world.status.racers[0].boo_lock_ticks==336);
    }
    free(actors);free(config);free(f);return 0;
}
static int fresh_star_and_capture_hold(void)
{
    Fixture *f=calloc(1,sizeof *f);
    PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
    PortableMeshCourseActors *actors=malloc(sizeof *actors);
    CHECK(f && config && actors);
    for(unsigned star=0;star<2;++star)
    {
        CHECK(setup(f,0) && countdown(f));
        f->rules.mushroom_golden_ticks=60;
        CHECK(tick(f,(PortableMeshKartInput){0,0},0)); /* Genuine floor support. */
        CHECK(f->trial.state.kart.grounded && f->trial.state.kart.contact_surface==0);
        memset(config,0,sizeof *config);
        config->course_id=f->floor.course.id;config->profile_id=config->revision=1;
        config->scale_q16=6554;config->piranha_count=1;
        CHECK(portable_piranha_init(&config->initial_piranhas[0],100*Q,100*Q,0,0));
        config->piranha_anchors_q16[0][1]=4*Q;
        for(unsigned i=0;i<5;++i)config->piranha_frame_ids[0][i]=i+1;
        config->capture_surface_count=1;config->capture_surfaces[0]=0;
        config->capture_forward_speed_q16=12*Q;config->capture_up_speed_q16=3*Q;
        CHECK(portable_mesh_course_actors_init(config,actors,NULL,0));
        if(star)
            f->world.inventory.racers[0].held_item=0x100a; /* Fixture-only TT override. */
        else
            CHECK(portable_mesh_item_status_activate(&f->world.status,&f->rules,0,
                PORTABLE_MESH_STATUS_GOLDEN_START)==PORTABLE_MESH_ITEM_STATUS_OK);
        PortableMeshCourseActorEvents actor_events;
        CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,0},(int)star,&actor_events));
        if(star)
        {
            CHECK(f->world.status.racers[0].star_ticks>0 && !actors->capture[0].remaining &&
                !actor_events.capture_mask && f->world.inventory.racers[0].held_item==0);
            CHECK(f->item_events.count && f->item_events.values[0].kind==PORTABLE_MESH_EVENT_USE);
            continue;
        }
        CHECK(actors->capture[0].remaining==92 && actor_events.capture_mask==1);
        const uint16_t held=f->world.inventory.racers[0].held_item;
        const uint32_t seed=f->world.inventory.seed;
        for(unsigned turn=0;turn<8;++turn)
        {
            const uint16_t golden=f->world.status.racers[0].golden_ticks;
            const uint32_t race_ticks=f->trial.state.race_ticks;
            CHECK(golden>1);
            CHECK(capture_tick(f,actors,config,
                (PortableMeshKartInput){32767,PORTABLE_MESH_ACCELERATE|PORTABLE_MESH_HOP},1,&actor_events));
            CHECK(actors->capture[0].remaining==91-turn &&
                f->world.status.racers[0].golden_ticks==golden-1 &&
                f->trial.state.race_ticks==race_ticks+1);
            CHECK(!f->item_events.count && f->world.inventory.racers[0].held_item==held &&
                f->world.inventory.seed==seed && !f->world.boxes.cooldowns[0] &&
                !f->world.status.racers[0].pending_mushroom);
        }
        CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,PORTABLE_MESH_RESCUE},1,&actor_events));
        CHECK(!actors->capture[0].remaining &&
            f->trial.state.kart.respawn_ticks==f->floor.rules.respawn_ticks &&
            !actor_events.capture_mask && !f->item_events.count);
    }
    free(f);free(config);free(actors);return 0;
}
static int burn_rescue_and_pause(int rocks)
{
    Fixture *f = calloc(1, sizeof *f);
    PortableMeshCourseActorsConfig *config = calloc(1, sizeof *config);
    PortableMeshCourseActors *actors = malloc(sizeof *actors);
    CHECK(f && config && actors && setup(f, 0) && countdown(f));
    CHECK(tick(f, (PortableMeshKartInput){0, 0}, 0));
    config->course_id = f->floor.course.id;
    config->profile_id = config->revision = 1;
    config->scale_q16 = 6554;
    if (rocks)
    {
        config->rock_count = 1;
        CHECK(portable_course_prop_init(&config->initial_rocks[0],100*Q,100*Q,0xa20000,256,0));
        config->rock_anchors_q16[0][0] = 100*Q;
        for (unsigned i=0;i<5;++i) config->rock_frame_ids[0][i]=10u+i;
    }
    else
    {
        config->lava_count = 1;
        CHECK(portable_lava_bubble_init(&config->initial_lava[0], 100 * Q, 100 * Q, 0, 0));
        config->lava_anchors_q16[0][0] = 100 * Q; /* Away from the ordinary driving lane. */
        config->lava_frame_ids[0][0] = 1;
        config->lava_frame_ids[0][1] = 2;
    }
    config->boo_count = 1;
    CHECK(portable_boo_init(&config->initial_boos[0], 100 * Q, 100 * Q, 0, 0));
    config->initial_boos[0].placement_order = 1;
    config->boo_anchors_q16[0][0] = 100 * Q;
    config->boo_frame_ids[0][0] = 3;
    config->boo_frame_ids[0][1] = 4;
    CHECK(portable_mesh_course_actors_init(config, actors, NULL, 0));
    actors->lava_burn_ticks[0] = 250;
    actors->boo_pending_mask = actors->boo_flash_mask = 1;
    PortableMeshCourseActorEvents actor_events;
    CHECK(capture_tick(f, actors, config, (PortableMeshKartInput){0, 0}, 0, &actor_events));
    CHECK(actors->lava_burn_ticks[0] == 249);
    const uint64_t paused = portable_mesh_course_actors_checksum(actors);
    for (unsigned draw = 0; draw < 8; ++draw)
    {
        PortableMeshCourseActorView view;
        CHECK(portable_mesh_course_actors_view(config, actors, 0, &view));
        CHECK(portable_mesh_course_actors_checksum(actors) == paused);
    }
    CHECK(capture_tick(f, actors, config, (PortableMeshKartInput){0, PORTABLE_MESH_RESCUE},
                       0, &actor_events));
    CHECK(!actors->lava_burn_ticks[0] && f->trial.state.kart.respawn_ticks == f->floor.rules.respawn_ticks);
    CHECK(!actors->boo_pending_mask && !actors->boo_flash_mask);
    CHECK(!actor_events.lava_burn_mask && !f->item_events.count);
    free(actors); free(config); free(f);
    return 0;
}
static int crush_owner_release_and_rescue(void)
{
    Fixture *f = calloc(1, sizeof *f);
    PortableMeshCourseActorsConfig *config = calloc(1, sizeof *config);
    PortableMeshCourseActors *actors = malloc(sizeof *actors);
    CHECK(f && config && actors && setup(f, 0) && countdown(f));
    config->course_id = f->floor.course.id;
    config->profile_id = config->revision = 1;
    config->scale_q16 = 6554;
    config->thwomp_count = 1;
    CHECK(portable_thwomp_init(&config->initial_thwomps[0], 0, 0, 0, 0));
    config->thwomp_anchors_q16[0][0] = 100 * Q;
    config->thwomp_frame_ids[0][0] = 1;
    for (unsigned i = 0; i < 5; ++i)
    {
        config->crush_rules.counts[i] = 1;
        config->crush_rules.rows[i][0] = (PortableDriverAffineRow){256, 256, 1};
    }
    config->crush_rules.rows[2][0] = (PortableDriverAffineRow){224, 384, 110};
    config->crush_rules.rows[3][0] = (PortableDriverAffineRow){224, 384, 18};
    config->crush_rules.counts[4] = 2;
    config->crush_rules.rows[4][0] = (PortableDriverAffineRow){224, 384, 112};
    config->crush_rules.rows[4][1] = (PortableDriverAffineRow){256, 256, 1};
    CHECK(portable_mesh_course_actors_init(config, actors, NULL, 0));
    CHECK(portable_mesh_course_crush_admit(&config->crush_rules, &actors->crush[0],
                                          &f->trial.state.kart, NULL, 0));
    const int32_t x = f->trial.state.kart.position_q16[0];
    const int32_t y = f->trial.state.kart.position_q16[1];
    const uint16_t item = f->world.inventory.racers[0].held_item;
    PortableMeshCourseActorEvents events;
    for (unsigned turn = 1; turn <= 239; ++turn)
    {
        const PortableMeshKartInput input = {0, turn <= 129 ? PORTABLE_MESH_ACCELERATE : 0};
        CHECK(capture_tick(f, actors, config, input, turn <= 128, &events));
        CHECK(events.thwomp_recover_mask == (turn == 239 ? 1 : 0));
        if (turn <= 128)
        {
            CHECK(f->trial.state.kart.position_q16[0] == x &&
                  f->trial.state.kart.position_q16[1] == y);
            CHECK(!f->trial.state.kart.speed_q16 && !f->item_events.count);
            CHECK(f->world.inventory.racers[0].held_item == item);
            CHECK(actors->crush[0].contact_timer == (turn < 128 ? 0x7000 - turn : 0));
        }
        if (turn == 128)
            CHECK(actors->crush[0].affine.mode == 7 && actors->crush[0].affine.remaining == 111);
        if (turn == 129) CHECK(f->trial.state.kart.speed_q16 > 0);
        const uint64_t paused = portable_mesh_course_actors_checksum(actors);
        PortableMeshCourseActorView view;
        CHECK(portable_mesh_course_actors_view(config, actors, 0, &view));
        CHECK(portable_mesh_course_actors_checksum(actors) == paused);
    }
    CHECK(portable_mesh_course_crush_admit(&config->crush_rules, &actors->crush[0],
                                          &f->trial.state.kart, NULL, 0));
    CHECK(capture_tick(f, actors, config, (PortableMeshKartInput){0, PORTABLE_MESH_RESCUE},
                       1, &events));
    CHECK(!actors->crush[0].contact_timer && !actors->crush[0].affine.mode &&
          !events.thwomp_recover_mask && !f->item_events.count);
    CHECK(f->trial.state.kart.respawn_ticks == f->floor.rules.respawn_ticks);
    free(actors); free(config); free(f);
    return 0;
}
static int strong_owner_release_and_rescue(void)
{
    Fixture *f = calloc(1, sizeof *f);
    PortableMeshCourseActorsConfig *config = calloc(1, sizeof *config);
    PortableMeshCourseActors *actors = malloc(sizeof *actors);
    CHECK(f && config && actors && setup(f,0) && countdown(f));
    CHECK(tick(f,(PortableMeshKartInput){0,0},0) && f->trial.state.kart.grounded);
    config->course_id = f->floor.course.id;
    config->profile_id = config->revision = 1;
    config->scale_q16 = 6554; config->cannonball_count = 1;
    CHECK(portable_cannonball_init(&config->initial_cannonballs[0],0,0,0,0));
    config->cannonball_anchors_q16[0][0] = 100 * Q;
    for (unsigned i=0;i<7;++i) config->cannonball_frame_ids[0][i]=i+1;
    CHECK(portable_mesh_course_actors_init(config,actors,NULL,0));
    CHECK(portable_mesh_course_strong_hit_admit(&actors->strong_hit[0],0x1000,0));
    const int32_t x=f->trial.state.kart.position_q16[0],y=f->trial.state.kart.position_q16[1];
    const uint16_t item=f->world.inventory.racers[0].held_item;
    PortableMeshCourseActorEvents events;
    for (unsigned tick=1;tick<=65;++tick)
    {
        CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,PORTABLE_MESH_ACCELERATE},tick<=64,&events));
        CHECK(!f->world.status.racers[0].spin_ticks);
        if (tick<=64)
            CHECK(f->trial.state.kart.position_q16[0]==x && f->trial.state.kart.position_q16[1]==y &&
                  !f->item_events.count && f->world.inventory.racers[0].held_item==item);
        else CHECK(f->trial.state.kart.speed_q16>0);
    }
    CHECK(!actors->strong_hit[0].active && !actors->strong_hit[0].pending);
    CHECK(portable_mesh_course_strong_hit_admit(&actors->strong_hit[0],0x2000,1));
    CHECK(capture_tick(f,actors,config,(PortableMeshKartInput){0,PORTABLE_MESH_RESCUE},1,&events));
    CHECK(!actors->strong_hit[0].active && !actors->strong_hit[0].pending &&
          !actors->strong_hit[0].rate && !actors->strong_hit[0].angle &&
          f->trial.state.kart.respawn_ticks==f->floor.rules.respawn_ticks && !f->item_events.count);
    free(actors);free(config);free(f);
    return 0;
}
int main(void)
{
    return pier_boo_owner() || kamek_owner() || mechakoopa_owner() || common_prop_owner() || thundercloud_owner() || falling_star_owner() || teepee_attachment_owner() || coin_only_tt_defaults() || strong_owner_release_and_rescue() || crush_owner_release_and_rescue() || burn_rescue_and_pause(0) || burn_rescue_and_pause(1) || legacy_wire_and_finish() || mushrooms_rescue_and_pause() || rollback() || fresh_star_and_capture_hold();
}
