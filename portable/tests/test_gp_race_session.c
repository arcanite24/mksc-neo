#include "gp_roster_fixture.h"
#include "driver_item_fixture.h"
#include "test_support.h"
#include "gp_race_session.h"
#include "gp_clock_fixture.h"
#include "spatial_audio_pitch.h"
#include "gp_race_presentation.h"
#include "gp_race_results.h"
#include "race_audio_timeline.h"
#include "course_contacts.h"
#include "script_host.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <pthread.h>
#endif

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr,"GP race session failed line %d: %s\n",__LINE__,#condition); \
    exit(1); \
} } while (0)



static PortableSurfaceTrailRules surface_trail_fixture(void)
{
    PortableSurfaceTrailRules rules={0};memset(rules.material_kind,1,sizeof rules.material_kind);
    rules.material_kind[0x54]=2u;
    for(unsigned i=0u;i<5u;++i){rules.frame_cutoffs[i][0]=9u;rules.frame_cutoffs[i][1]=19u;}
    rules.offroad_material_min=0x50u;rules.suppressed_moving_material=0x50u;rules.spray_material=0x5cu;
    return rules;
}

static int test_spatial_pitch(void)
{
    int16_t table[PORTABLE_SPATIAL_PITCH_COUNT],pitch;
    for(unsigned i=0u;i<PORTABLE_SPATIAL_PITCH_COUNT;++i)table[i]=(int16_t)((int)i-544);
    PortableSpatialAudioCamera camera={.x_q16=64*65536,.heading=0x4000u,.racing=1u};
    PortableSpatialAudioActor actor={.type=0x2001u,.heading=0x4000u};
    /* Every signed speed-bin pair independently identifies its table cell. */
    for(int observer=-16;observer<=16;++observer)for(int source=-16;source<=16;++source){
        camera.speed_q16=observer*0x800000;actor.speed=source*0x800000;
        CHECK(portable_spatial_audio_pitch(&camera,&actor,table,1089u,&pitch));
        CHECK(pitch==observer*33+source);
    }
    camera.speed_q16=2*0x800000;actor.type=0x8403u;actor.speed=255*16;
    CHECK(portable_spatial_audio_pitch(&camera,&actor,table,1089u,&pitch) && pitch==66);
    camera.heading=0xc001u;
    CHECK(portable_spatial_audio_pitch(&camera,&actor,table,1089u,&pitch) && pitch==-66);
    camera.heading=0xc000u; /* abs(-32768) narrows back to negative. */
    CHECK(portable_spatial_audio_pitch(&camera,&actor,table,1089u,&pitch) && pitch==66);
    camera.fix_heading_wrap=1u; /* Optional fix: dead astern is behind. */
    CHECK(portable_spatial_audio_pitch(&camera,&actor,table,1089u,&pitch) && pitch==-66);
    camera.heading=0xc001u;
    CHECK(portable_spatial_audio_pitch(&camera,&actor,table,1089u,&pitch) && pitch==-66);
    camera.heading=0u;
    CHECK(portable_spatial_audio_pitch(&camera,&actor,table,1089u,&pitch) && pitch==66);
    camera.fix_heading_wrap=0u;
    camera.heading=0u; /* inclusive quarter-turn boundary. */
    CHECK(portable_spatial_audio_pitch(&camera,&actor,table,1089u,&pitch) && pitch==66);
    camera.heading=0xffffu;
    CHECK(portable_spatial_audio_pitch(&camera,&actor,table,1089u,&pitch) && pitch==-66);
    camera.finished=1u;
    CHECK(portable_spatial_audio_pitch(&camera,&actor,table,1089u,&pitch) && pitch==0);
    camera.finished=0u;camera.racing=0u;
    CHECK(portable_spatial_audio_pitch(&camera,&actor,table,1089u,&pitch) && pitch==0);
    camera.racing=1u;camera.heading=0x4000u;camera.speed_q16=INT32_MAX;
    CHECK(portable_spatial_audio_pitch(&camera,&actor,table,1089u,&pitch) && pitch==16*33);
    actor.type=0x8001u;
    CHECK(portable_spatial_audio_pitch(&camera,&actor,table,1089u,&pitch) && pitch==0);
    CHECK(portable_spatial_audio_pitch(&camera,&actor,NULL,0u,&pitch) && pitch==0);
    CHECK(!portable_spatial_audio_pitch(&camera,&actor,table,1088u,&pitch));
    return 0;
}

static void test_passive_course_props(void)
{
    int32_t sine[1024]={0};sine[256]=32768;sine[768]=-32768;
    const PortableDriverPilotDependencies deps={.sine_table=sine,.sine_count=1024u};
    for(unsigned dynamic=0;dynamic<3u;++dynamic)for(unsigned local=0;local<2u;++local)for(unsigned scenario=0;scenario<11u;++scenario){
        const int mecha=dynamic==1u,kamek=dynamic==2u;
        PortableCourseProps props={.count=1u};
        CHECK(portable_course_prop_init(&props.actors[0],100*65536,100*65536,
            scenario==10u?0xc0000u:kamek?0xa00000u:mecha?0xa10000u:(scenario&1u)?0x90000u:0x80000u,256u,0u));
        props.actors[0].checkpoint=2u;props.actors[0].placement_order=14u;
        PortableDriverPilotState driver={0};
        driver.pose.position_x=100*65536;driver.pose.position_y=100*65536;
        driver.contact_anchor_x_ee=99;driver.contact_anchor_y_f0=100;
        driver.recovery_contact_34=3u;driver.source_route_index_90=2u;
        driver.invulnerability_timer_6a=(scenario==2u || scenario==4u || scenario==5u || scenario==6u)?1u:0u;
        driver.contact_guard_66=(scenario==3u || scenario==4u)?1u:0u;
        driver.hop.mode=scenario==5u?2u:0u;
        driver.normal_collision_timer_38=scenario==1u?1u:scenario==6u?2u:0u;
        if(scenario==7u)driver.pose.position_z=5*65536;
        if(scenario==8u)props.actors[0].flags=0u;
        if(scenario==9u)props.actors[0].flags=1u;
        PortableLavaBubbleCourse lava={0};PortableThwompCourse thwomps={0};
        PortableCrabCourse crabs={0};PortableSeagullCourse seagulls={0};
        PortableCannonballCourse cannonballs={0};PortablePiranhaCourse piranhas={0};
        PortableFallingStarCourse falling_stars={0};PortableThundercloudCourse thunderclouds={0};PortableMouseCourse mice={0};PortableSnowmanCourse snowmen={0};PortablePenguinCourse penguins={0};PortableBooCourse boos={0};
        PortablePiranhaSounds piranha_sounds={0};PortableCourseImpactSounds impacts={0};
        PortableCourseContacts world={.driver=&driver,.dependencies=&deps,.course_props=&props,
            .lava_bubbles=&lava,.thwomps=&thwomps,.crabs=&crabs,.seagulls=&seagulls,
            .cannonballs=&cannonballs,.piranhas=&piranhas,.snowmen=&snowmen,.mice=&mice,.thunderclouds=&thunderclouds,.falling_stars=&falling_stars,
            .penguins=&penguins,.boos=&boos,.piranha_sounds=&piranha_sounds,.impact_sounds=&impacts,
            .mode_name="GP/TT",.course_id=26u,.checkpoint_count=8u,.is_player=(uint8_t)local};
        CHECK(portable_course_contacts_collide(&world));
        CHECK(world.events.contacts==(scenario<6u?1u:0u));
        CHECK(!world.events.strong_hits && !world.events.flattened_hits && !world.events.coin_loss && !impacts.count);
        CHECK(driver.forward_acceleration.state_flags==(mecha && scenario==0u?0x1000u:0u));
        if(scenario==0u || scenario==1u){
            CHECK(driver.normal_collision_timer_38==8u && driver.vector_effect.contact_word_5a==0x8007u);
            CHECK(driver.steering.state_flags&0x1000u);
            CHECK(driver.contact_audio.song_id==(local && scenario==0u && !kamek?169u:0u));
            PortableDriverObjectContactResult expected;
            CHECK((scenario==1u?portable_driver_object_contact_escape(100,100,100,100,sine,1024u,&expected):
                portable_driver_object_contact_resultant(99,100,100,100,0,0,0u,sine,1024u,&expected))==0);
            if(!dynamic || scenario==1u)CHECK(driver.motion.component_x==expected.resultant_x && driver.motion.component_y==expected.resultant_y);
        }else{
            CHECK(driver.normal_collision_timer_38==(scenario==6u?2u:0u));
            CHECK(driver.contact_audio.song_id==(local && scenario==2u?160u:0u));
        }
        CHECK(props.actors[0].state==((scenario==2u || (!dynamic && scenario==4u) || scenario==5u)?(kamek?5u:1u):0u));
        if(props.actors[0].state && !kamek)CHECK(props.actors[0].timer==32 && props.actors[0].affine_y_q8==-256);
        if(scenario==2u && !dynamic){
            /* A protected first driver changes live state, but the original
             * passive node remains available to later drivers in this pass. */
            driver.normal_collision_timer_38=0u;
            PortableDriverPilotState second=driver;
            second.invulnerability_timer_6a=0u;second.contact_audio=(PortableDriverContactAudio){0};
            PortableCourseContacts worlds[2]={world,world};
            worlds[1].driver=&second;worlds[1].is_player=0u;
            CHECK(portable_course_contacts_collide_many(worlds,2u));
            CHECK(worlds[0].events.contacts==1u && worlds[1].events.contacts==1u &&
                second.normal_collision_timer_38==8u && props.actors[0].timer==32);
            CHECK(portable_course_props_advance(&props,sine,1024u));
            driver.normal_collision_timer_38=second.normal_collision_timer_38=0u;
            CHECK(portable_course_contacts_collide_many(worlds,2u));
            CHECK(!worlds[0].events.contacts && !worlds[1].events.contacts);
        }
        if(scenario==0u){
            if(mecha){
                PortableDriverPilotState weak=driver;
                driver.normal_collision_timer_38=0u;driver.steering.state_flags|=0x2000u;
                driver.forward_acceleration.state_flags=0u;
                CHECK(portable_course_contacts_collide(&world));
                CHECK(world.events.strong_hits==1u && world.events.coin_loss==4u && impacts.count==1u);
                CHECK(driver.strong_spin_rate_b6==0x2000u && driver.forward_acceleration.state_flags==0x200u);
                /* E490's squeal162 is local and unmuted only. */
                CHECK(piranha_sounds.count==(local?1u:0u) && (!local || piranha_sounds.songs[0]==162u));
                driver=weak;impacts.count=0u;piranha_sounds.count=0u;
            }
            /* Global prepend order includes props and the other families. */
            snowmen.count=1u;
            CHECK(portable_snowman_init(&snowmen.actors[0],100*65536,100*65536,2u,0u));
            snowmen.actors[0].placement_order=13u;
            driver.normal_collision_timer_38=0u;driver.contact_audio=(PortableDriverContactAudio){0};
            CHECK(portable_course_contacts_collide(&world));
            CHECK(world.events.contacts==1u && driver.contact_audio.song_id==(local && !kamek?169u:0u));
            snowmen.actors[0].placement_order=15u;
            driver.normal_collision_timer_38=0u;driver.contact_audio=(PortableDriverContactAudio){0};
            CHECK(portable_course_contacts_collide(&world));
            CHECK(world.events.contacts==1u && driver.contact_audio.song_id==(local?373u:0u));
        }
    }
}

/* Fixed input over the shared race stack; the digest covers roster, player
 * pose, held item and the game's own RNG after every tick. */
static uint64_t scripted_race_digest(const PortableRecoveredRaceView *race,
    PortableScriptHost *scripts,int press_items)
{
    PortableGpRaceSession *session=calloc(1u,sizeof *session);
    PortableRaceTickDisposition disposition;
    uint64_t digest=UINT64_C(14695981039346656037);
    CHECK(session!=NULL);
    CHECK(test_gp_init(session,race,7u)==PORTABLE_GP_RACE_SESSION_OK);
    CHECK(portable_script_host_begin_race(scripts,session,PORTABLE_SCRIPT_MODE_QUICK_RUN,24u,0u));
    CHECK((scripts==NULL)==(session->script_hooks==NULL));
    for(uint32_t tick=0u;tick<240u;++tick){
        const uint16_t buttons=(uint16_t)(PORTABLE_INPUT_ACCELERATE|
            (press_items&&(tick%16u)==8u?PORTABLE_INPUT_ITEM:0u));
        CHECK(portable_gp_race_session_tick(session,buttons,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        const uint64_t words[]={portable_gp_race_roster_identity(&session->roster),session->source_rng_state,
            (uint32_t)session->player.pose.position_x,(uint32_t)session->player.pose.position_y,
            session->roster.racers[0].item_inventory.held_item_17a};
        for(size_t i=0u;i<sizeof words/sizeof words[0];++i)digest=(digest^words[i])*UINT64_C(1099511628211);
    }
    portable_script_host_end_race(scripts);
    free(session);
    return digest;
}

typedef struct
{
    PortableGpRaceSession *session;
    unsigned ticks, offers[3];
    uint16_t items[3];
} LocalScriptProbe;

static void local_script_after_tick(void *context)
{
    LocalScriptProbe *probe=context;
    /* Shared-world completion restores the first camera/player before Lua. */
    CHECK(probe->session->local_index==0u);
    CHECK(probe->session->roster.script_hooks==probe->session->script_hooks);
    ++probe->ticks;
}

static uint16_t local_script_item_use(void *context,size_t racer,uint16_t item)
{
    LocalScriptProbe *probe=context;
    CHECK(racer<3u && probe->session->local_index==racer);
    CHECK(probe->session->roster.racers[racer].is_player);
    CHECK(item==probe->items[racer]);
    ++probe->offers[racer];
    return 0u; /* Cancellation must retain this local's inventory. */
}

/* Three VS locals share one race stack: each pilot follows its own input,
 * each view lists the others, and the race ends when all but one finish. */
static void test_surface_trail_session(const PortableRecoveredRaceView *base)
{
    const PortableSurfaceTrailRules rules=surface_trail_fixture();
    PortableRecoveredRaceView race=*base;race.surface_trail_rules=&rules;
    int32_t trail_sine[1024];
    for(unsigned i=0u;i<1024u;++i)trail_sine[i]=(int32_t)lround(sin((double)i*6.283185307179586/1024.0)*32768.0);
    race.dependencies.sine_table=trail_sine;race.dependencies.sine_count=1024u;
    static PortableGpRaceSession session;
    CHECK(test_gp_init(&session,&race,7u)==0);
    session.player.forward_acceleration.state_flags=0x20u;
    session.player.steering.response_flags=0x20u;
    const uint16_t inputs[PORTABLE_GP_LAUNCH_PRELUDE_TICKS]={0};
    CHECK(portable_gp_race_session_handoff_launch_inputs(&session,inputs,PORTABLE_GP_LAUNCH_PRELUDE_TICKS)==0);
    CHECK(!session.surface_trails.eligible_counter && session.surface_trails.side==1u);
    for(unsigned i=0u;i<5u;++i)CHECK(session.surface_trails.slots[i].age==18u-4u*i &&
        session.surface_trails.slots[i].kind==1u);
    CHECK(!session.surface_trails.slots[5].age);
    PortableGpRaceSnapshot snapshot;
    CHECK(portable_gp_race_session_snapshot(&session,&snapshot)==0 &&
        !memcmp(&snapshot.surface_trails,&session.surface_trails,sizeof session.surface_trails));
    const PortableSurfaceTrail held=snapshot.surface_trails;
    session.player.forward_acceleration.state_flags=0u;
    session.player.pose.position_z=2*65536;session.player.hop.mode=1u;
    session.player.hop.height_q16=2*65536;
    PortableRaceTickDisposition disposition;
    CHECK(portable_gp_race_session_tick(&session,0u,&disposition)==0 &&
        session.surface_trails.slots[0].age==held.slots[0].age+1u && !session.surface_trails.eligible_counter);
    CHECK(!memcmp(&snapshot.surface_trails,&held,sizeof held)); /* immutable held/pause view */
    session.player.steering.state_flags|=0x20u;
    const PortableSurfaceTrail before=session.surface_trails;
    CHECK(portable_gp_race_session_tick(&session,0u,&disposition)==0 && !session.surface_trails.visible);
    CHECK(!memcmp(session.surface_trails.slots,before.slots,sizeof before.slots));
    CHECK(test_gp_init(&session,&race,7u)==0);
    for(unsigned i=0u;i<6u;++i)CHECK(!session.surface_trails.slots[i].age);
    CHECK(!session.surface_trails.side && !session.surface_trails.eligible_counter);
}

static void test_modern_air_item_drag(const PortableRecoveredRaceView *base)
{
    const PortableModernDriftRules modern={.profile_id=74u,.revision=2u,
        .charge_ticks={2u,3u,4u},.boost_ticks={3u,4u,5u},
        .cap_ratio_256={256u,272u,288u},.minimum_speed_ratio_256=1u,
        .acceleration_q16={65536,2*65536,3*65536},
        .air={.minimum_air_ticks=2u,.trick_window_ticks=10u,.boost_ticks=8u,
            .cap_ratio_256=300u,.minimum_speed_ratio_256=1u,
            .acceleration_q16=3*65536,.launch_mask=3u}};
    PortableRecoveredRaceView race=*base;
    int32_t sine[1024];
    for(unsigned i=0u;i<1024u;++i)
        sine[i]=(int32_t)lround(sin((double)i*6.283185307179586/1024.0)*32768.0);
    race.dependencies.sine_table=sine;race.dependencies.sine_count=1024u;
    static PortableGpRaceSession session;
    const uint16_t items[]={PORTABLE_GP_GREEN_SHELL_ITEM,PORTABLE_GP_RED_SHELL_ITEM,PORTABLE_GP_BANANA_ITEM};
    int32_t original_positions[3][4][3];
    for(unsigned profile=0u;profile<2u;++profile)for(unsigned family=0u;family<3u;++family){
        race.dependencies.modern_drift_rules=profile?&modern:NULL;
        CHECK(test_gp_init(&session,&race,7u)==0);
        PortableRaceTickDisposition disposition;
        CHECK(portable_gp_race_session_tick(&session,0u,&disposition)==0);
        CHECK(portable_gp_race_session_tick(&session,0u,&disposition)==0);
        for(unsigned i=1u;i<8u;++i){
            session.roster.racers[i].position_x_q16=(1400+(int32_t)i*20)*65536;
            session.roster.racers[i].position_y_q16=(1400+(int32_t)i*20)*65536;
        }
        CHECK(portable_gp_race_session_grant_player_item(&session,items[family])==0);
        PortableGpRacer *owner=&session.roster.racers[0];
        for(unsigned tick=0u;tick<4u;++tick){
            CHECK(portable_gp_race_session_tick(&session,PORTABLE_INPUT_ITEM|PORTABLE_INPUT_ACCELERATE,&disposition)==0);
            CHECK(session.roster.item_uses==1u&&owner->item_inventory.held_item_17a==0u);
            CHECK((owner->control&0x40u)!=0u&&owner->shell_item_18c==items[family]);
            int32_t x,y,z;
            if(family==0u){
                CHECK(owner->green_shell.phase==PORTABLE_GP_GREEN_SHELL_ACTIVE);
                x=owner->green_shell.position_x_q16;y=owner->green_shell.position_y_q16;z=owner->green_shell.position_z_q16;
                CHECK(portable_gp_green_actor_owner(&session.roster,0u)==0u);
            }else if(family==1u){
                CHECK(owner->red_shell.phase==PORTABLE_GP_RED_SHELL_ACTIVE);
                x=owner->red_shell.position_x_q16;y=owner->red_shell.position_y_q16;z=owner->red_shell.position_z_q16;
                CHECK(portable_gp_red_actor_owner(&session.roster,0u)==0u);
            }else{
                CHECK(owner->banana.phase==PORTABLE_GP_BANANA_HELD);
                x=owner->banana.position_x_q16;y=owner->banana.position_y_q16;z=owner->banana.position_z_q16;
                CHECK(portable_gp_banana_actor_owner(&session.roster,0u)==0u);
            }
            const int64_t dx=(int64_t)x-owner->position_x_q16,dy=(int64_t)y-owner->position_y_q16;
            CHECK(dx*dx+dy*dy>=INT64_C(49)*65536*65536&&dx*dx+dy*dy<=INT64_C(81)*65536*65536);
            CHECK(z==owner->world_height_q16);
            if(!profile){original_positions[family][tick][0]=x;original_positions[family][tick][1]=y;original_positions[family][tick][2]=z;}
            else CHECK(x==original_positions[family][tick][0]&&y==original_positions[family][tick][1]&&z==original_positions[family][tick][2]);
        }
        PortableGpRaceSnapshot held;CHECK(portable_gp_race_session_snapshot(&session,&held)==0);
        CHECK(held.modern_air_enabled==(profile!=0u));
        CHECK(portable_gp_race_session_tick(&session,PORTABLE_INPUT_ACCELERATE|PORTABLE_INPUT_DOWN,&disposition)==0);
        CHECK(!(owner->control&0x40u)&&owner->shell_item_18c==0u&&session.roster.item_uses==1u);
        if(family==0u)CHECK(owner->green_shell.phase==PORTABLE_GP_GREEN_SHELL_RELEASED);
        else if(family==1u)CHECK(owner->red_shell.phase==PORTABLE_GP_RED_SHELL_RELEASED_BACKWARD);
        else CHECK(owner->banana.phase==PORTABLE_GP_BANANA_DROPPED);
        CHECK(held.modern_air_enabled==(profile!=0u));
    }
    /* Each local's Item level reaches its own attached actor before the one
     * shared world pass. Releasing P1 must not release P2's dragged item. */
    race.dependencies.modern_drift_rules=&modern;
    PortableGpGridRow grid[8];
    for(unsigned i=0u;i<8u;++i)grid[i]=(PortableGpGridRow){
        race.rules.start_x_q16+(int32_t)i*32*65536,
        race.rules.start_y_q16,race.rules.start_heading};
    race.gp_grid=grid;race.gp_grid_count=8u;
    const PortableRecoveredRaceView *races[2]={&race,&race};
    const uint8_t drivers[2]={7u,7u};
    CHECK(test_gp_init_locals(&session,races,drivers,2u,0u,(PortableQuickRunOptions){0})==0);
    PortableRaceTickDisposition disposition;
    const uint16_t neutral[2]={0u,0u};
    CHECK(portable_gp_race_session_tick_locals(&session,neutral,&disposition)==0);
    CHECK(portable_gp_race_session_tick_locals(&session,neutral,&disposition)==0);
    CHECK(portable_gp_race_session_grant_player_item(&session,PORTABLE_GP_GREEN_SHELL_ITEM)==0);
    CHECK(portable_gp_race_session_select_local(&session,1u)==0);
    CHECK(portable_gp_race_session_grant_player_item(&session,PORTABLE_GP_BANANA_ITEM)==0);
    CHECK(portable_gp_race_session_select_local(&session,0u)==0);
    uint16_t buttons[2]={PORTABLE_INPUT_ITEM|PORTABLE_INPUT_ACCELERATE,
                         PORTABLE_INPUT_ITEM|PORTABLE_INPUT_ACCELERATE};
    CHECK(portable_gp_race_session_tick_locals(&session,buttons,&disposition)==0);
    CHECK(session.roster.racers[0].green_shell.phase==PORTABLE_GP_GREEN_SHELL_ACTIVE&&
        session.roster.racers[1].banana.phase==PORTABLE_GP_BANANA_HELD&&session.roster.item_uses==2u);
    CHECK(portable_gp_green_actor_owner(&session.roster,0u)==0u&&
        portable_gp_banana_actor_owner(&session.roster,1u)==1u);
    const uint16_t banana_age=session.roster.racers[1].banana.attachment_ticks_4c;
    buttons[0]=PORTABLE_INPUT_ACCELERATE;
    CHECK(portable_gp_race_session_tick_locals(&session,buttons,&disposition)==0);
    CHECK(session.roster.racers[0].green_shell.phase==PORTABLE_GP_GREEN_SHELL_RELEASED&&
        session.roster.racers[1].banana.phase==PORTABLE_GP_BANANA_HELD&&
        session.roster.racers[1].banana.attachment_ticks_4c==(uint16_t)(banana_age+1u)&&
        session.roster.racers[1].shell_item_18c==PORTABLE_GP_BANANA_ITEM);
    buttons[1]=PORTABLE_INPUT_ACCELERATE;
    CHECK(portable_gp_race_session_tick_locals(&session,buttons,&disposition)==0);
    CHECK(session.roster.racers[1].banana.phase==PORTABLE_GP_BANANA_DROPPED&&
        session.roster.racers[1].shell_item_18c==0u&&session.roster.item_uses==2u);
}

static void test_modern_local_owners(const PortableRecoveredRaceView *base)
{
    const PortableModernDriftRules modern={.profile_id=73u,.revision=1u,
        .charge_ticks={2u,3u,4u},.boost_ticks={3u,4u,5u},
        .cap_ratio_256={256u,272u,288u},.minimum_speed_ratio_256=1u,
        .acceleration_q16={65536,2*65536,3*65536}};
    PortableRecoveredRaceView race=*base;
    race.dependencies.modern_drift_rules=&modern;
    int32_t sine[1024];
    for(unsigned i=0u;i<1024u;++i)
        sine[i]=(int32_t)lround(sin((double)i*6.283185307179586/1024.0)*32768.0);
    race.dependencies.sine_table=sine;race.dependencies.sine_count=1024u;
    PortableGpGridRow grid[8];
    for(unsigned i=0u;i<8u;++i)grid[i]=(PortableGpGridRow){
        race.rules.start_x_q16+(int32_t)i*32*65536,
        race.rules.start_y_q16,race.rules.start_heading};
    race.gp_grid=grid;race.gp_grid_count=8u;
    const PortableRecoveredRaceView *races[2]={&race,&race};
    const uint8_t drivers[2]={7u,7u};
    static PortableGpRaceSession session,copy;
    CHECK(test_gp_init_locals(&session,races,drivers,2u,0u,(PortableQuickRunOptions){0})==0);
    for(uint8_t local=0u;local<2u;++local){
        CHECK(portable_gp_race_session_select_local(&session,local)==0);
        session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
        session.runtime.session.countdown_remaining=0u;
        session.player.motion.speed_q16=400*65536;
        session.player.item_lock_ac=0u;
        /* Start at a completed charge on P1 and an already active P2 boost.
         * Their release and countdown still run through both real pilot passes. */
        session.player.modern_drift=local?
            (PortableModernDriftState){.boost_ticks=4u,.boost_tier=2u}:
            (PortableModernDriftState){.charge_ticks=2u,.charge_tier=1u,.previous_held=1u};
    }
    CHECK(portable_gp_race_session_select_local(&session,0u)==0);
    session.roster.race_phase_7d4=3u;
    memcpy(&copy,&session,sizeof copy);
    /* Every local runtime targets the active player fields, even while stored
     * in locals[]. A session byte copy must rebind those self-references before
     * either ticking it or selecting one of its parked local owners. */
    copy.runtime.recovered_driver=&copy.player;
    copy.runtime.recovered_dependencies=&copy.player_dependencies;
    for(uint8_t local=1u;local<copy.local_count;++local){
        copy.locals[local-1u].runtime.recovered_driver=&copy.player;
        copy.locals[local-1u].runtime.recovered_dependencies=&copy.player_dependencies;
    }
    const uint16_t buttons[2]={PORTABLE_INPUT_ACCELERATE,PORTABLE_INPUT_ACCELERATE};
    PortableRaceTickDisposition disposition,copy_disposition;
    PortableGpRaceSnapshot held;
    CHECK(portable_gp_race_session_snapshot(&session,&held)==0);
    CHECK(held.modern_drift_enabled && held.modern_drift.charge_ticks==2u);
    for(unsigned tick=0u;tick<3u;++tick){
        CHECK(portable_gp_race_session_tick_locals(&session,buttons,&disposition)==0);
        CHECK(portable_gp_race_session_tick_locals(&copy,buttons,&copy_disposition)==0);
        CHECK(disposition==copy_disposition && session.source_frame_clock==copy.source_frame_clock);
        for(uint8_t local=0u;local<2u;++local){
            CHECK(portable_gp_race_session_select_local(&session,local)==0);
            CHECK(portable_gp_race_session_select_local(&copy,local)==0);
            PortableGpRaceSnapshot snapshot,repeated;
            CHECK(portable_gp_race_session_snapshot(&session,&snapshot)==0);
            CHECK(portable_gp_race_session_snapshot(&copy,&repeated)==0);
            CHECK(snapshot.modern_drift_enabled && repeated.modern_drift_enabled);
            const PortableModernDriftState *a=&snapshot.modern_drift,*b=&repeated.modern_drift;
            CHECK(a->charge_ticks==0u && a->charge_tier==0u && a->previous_held==0u);
            CHECK(a->boost_ticks==(local?3u-tick:2u-tick));
            CHECK(a->last_applied_tier==(local?2u:1u));
            CHECK(a->boost_tier==(local?2u:tick==2u?0u:1u));
            CHECK(a->charge_ticks==b->charge_ticks && a->charge_tier==b->charge_tier &&
                a->boost_ticks==b->boost_ticks && a->boost_tier==b->boost_tier &&
                a->previous_held==b->previous_held && a->last_applied_tier==b->last_applied_tier);
            CHECK(session.player.motion.speed_q16==copy.player.motion.speed_q16 &&
                session.player.pose.position_x==copy.player.pose.position_x &&
                session.player.pose.position_y==copy.player.pose.position_y);
        }
        CHECK(portable_gp_race_session_select_local(&session,0u)==0);
        CHECK(portable_gp_race_session_select_local(&copy,0u)==0);
    }
    CHECK(held.modern_drift.charge_ticks==2u && held.modern_drift.boost_ticks==0u);
    CHECK(test_gp_init_locals(&session,races,drivers,2u,0u,(PortableQuickRunOptions){0})==0);
    for(uint8_t local=0u;local<2u;++local){
        CHECK(portable_gp_race_session_select_local(&session,local)==0);
        PortableGpRaceSnapshot snapshot;
        CHECK(portable_gp_race_session_snapshot(&session,&snapshot)==0 && snapshot.modern_drift_enabled);
        CHECK(!snapshot.modern_drift.charge_ticks && !snapshot.modern_drift.boost_ticks &&
            !snapshot.modern_drift.charge_tier && !snapshot.modern_drift.boost_tier &&
            !snapshot.modern_drift.previous_held && !snapshot.modern_drift.last_applied_tier);
    }
}

static void test_finish_lakitu_owner(const PortableRecoveredRaceView *base)
{
    uint8_t bob[128]={0};int32_t sine[1024];
    for(unsigned i=0u;i<1024u;++i)sine[i]=(int32_t)lround(sin((double)i*6.283185307179586/1024.0)*32768.0);
    const PortableGpFinishLakituRules rules={.row_count=2u,.entrance_ticks=22u,
        .bob_ticks=8u,.exit_ticks=3u,.frames={{21u,13u},{28u,20u}}};
    PortableRecoveredRaceView race=*base;
    race.recovery_bob_offsets=bob;race.finish_lakitu_rules=&rules;
    race.dependencies.sine_table=sine;race.dependencies.sine_count=1024u;
    PortableGpGridRow grid[8];
    for(unsigned i=0u;i<8u;++i)grid[i]=(PortableGpGridRow){
        race.rules.start_x_q16+(int32_t)i*32*65536,
        race.rules.start_y_q16,race.rules.start_heading};
    race.gp_grid=grid;race.gp_grid_count=8u;
    static PortableGpRaceSession session;
    const PortableRecoveredRaceView *races[2]={&race,&race};const uint8_t drivers[2]={7u,7u};
    CHECK(test_gp_init_locals(&session,races,drivers,2u,0u,(PortableQuickRunOptions){0})==0);
    for(uint8_t local=0u;local<2u;++local){
        CHECK(portable_gp_race_session_select_local(&session,local)==0);
        session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
        session.runtime.session.countdown_remaining=0u;
        session.roster.racers[local].lap_marker_a4|=UINT16_C(0x4000);
    }
    CHECK(portable_gp_race_session_select_local(&session,0u)==0);
    session.roster.race_phase_7d4=3u;
    const uint16_t buttons[2]={0u,0u};PortableRaceTickDisposition disposition;
    CHECK(portable_gp_race_session_tick_locals(&session,buttons,&disposition)==0);
    for(uint8_t local=0u;local<2u;++local){
        CHECK(portable_gp_race_session_select_local(&session,local)==0);
        CHECK(session.hazard_lakitu.finish_turn==1u && session.hazard_lakitu.phase==PORTABLE_GP_LAKITU_FINISH &&
            !(session.roster.racers[local].lap_marker_a4&0x4000u));
        PortableGpRaceSnapshot snapshot;CHECK(portable_gp_race_session_snapshot(&session,&snapshot)==0);
        CHECK(snapshot.lakitu_visible && snapshot.lakitu_state==0xd000u && snapshot.lakitu_art_bank==21u &&
            snapshot.lakitu_scale_q8==512u);
        PortableRacePresentationState presentation;
        CHECK(portable_gp_race_presentation_build(&session,&snapshot,&presentation));
        CHECK(session.hazard_lakitu.finish_turn==1u); /* Queries do not tick the owner. */
        session.player.hop.recovery_requests|=UINT16_C(0x400);
    }
    CHECK(portable_gp_race_session_select_local(&session,0u)==0);
    /* GP pause is a host-owned hold: repeated snapshot queries cannot advance it. */
    const PortableGpRecoveryLakituState frozen=session.hazard_lakitu;
    PortableGpRaceSnapshot held;
    CHECK(portable_gp_race_session_snapshot(&session,&held)==0 &&
        !memcmp(&frozen,&session.hazard_lakitu,sizeof frozen));
    CHECK(portable_gp_race_session_tick_locals(&session,buttons,&disposition)==0);
    for(uint8_t local=0u;local<2u;++local){
        CHECK(portable_gp_race_session_select_local(&session,local)==0);
        CHECK(session.hazard_lakitu.finish_turn==2u && (session.player.hop.recovery_requests&0x400u));
    }
}

static void test_owned_hud_session(const PortableRecoveredRaceView *base)
{
    uint8_t stable[520]={0},transition[416]={0},pages[8]={0,1,2,3,4,5,6,7};
    uint8_t colors[64]={0},flash[16]={0},separator[8]={1u,0u,2u,0u,3u,0u,4u,0u};
    uint8_t coin_colors[18]={1u,0u,2u,0u,3u,0u,4u,0u,5u,0u,6u,0u,7u,0u,8u,0u,9u,0u};
    for(unsigned rank=0u;rank<5u;++rank)stable[rank*104u]=(uint8_t)(10u+rank);
    for(unsigned rank=0u;rank<4u;++rank)transition[rank*104u]=(uint8_t)(20u+rank);
    const PortableGpRaceHudAssetView rules={.kind_pages=pages,.rank_maps=stable,.rank_colors=colors,
        .rank_transition_maps=transition,.rank_flash_colors=flash,.rank_separator_colors=separator,
        .coin_hud_colors=coin_colors};
    PortableRecoveredRaceView race=*base;
    /* This fixture advances the real camera/driver twice; its source camera
     * contract uses Q15 sine, unlike the legacy base fixture's Q16 table. */
    int32_t sine[1024];
    for(unsigned i=0u;i<1024u;++i)sine[i]=(int32_t)lround(sin((double)i*6.283185307179586/1024.0)*32768.0);
    race.dependencies.sine_table=sine;race.dependencies.sine_count=1024u;
    static PortableGpRaceSession session;
    CHECK(test_gp_init(&session,&race,7u)==0);
    const uint16_t launch[PORTABLE_GP_LAUNCH_PRELUDE_TICKS]={0};
    CHECK(portable_gp_race_session_handoff_launch_inputs(&session,launch,PORTABLE_GP_LAUNCH_PRELUDE_TICKS)==0);
    session.roster.racers[0].coin_count_178=2u;
    uint8_t clock_steps[PORTABLE_HUD_CLOCK_STEP_COUNT];
    memcpy(clock_steps,test_gp_clock_hud()->clock_steps,sizeof clock_steps);
    clock_steps[0]=7u;
    const PortableHudAssetView clock_hud={.clock_steps=clock_steps,.clock_step_count=sizeof clock_steps};
    CHECK(portable_gp_race_session_configure_hud(&session,&rules,NULL)==PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT);
    CHECK(portable_gp_race_session_configure_hud(&session,&rules,&clock_hud)==0);
    CHECK(session.hud_clock_steps==clock_steps&&session.hud_clock_step_count==sizeof clock_steps);
    CHECK(session.rank_hud.initialized&&session.coin_hud.initialized);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;session.runtime.session.countdown_remaining=0u;
    session.roster.race_phase_7d4=3u;
    session.roster.racers[0].coin_count_178=0u;
    session.roster.racers[0].rank_flags_ba=6u;
    CHECK(portable_gp_source_clock_enable(&session.source_result_clock));
    const uint16_t old_digit=session.coin_hud.displayed_cells[2];
    PortableRaceTickDisposition disposition;
    CHECK(portable_gp_race_session_tick(&session,0u,&disposition)==0);
    CHECK(session.source_result_clock.centiseconds==7u&&session.source_result_clock.phase==1u&&clock_steps[0]==7u);
    CHECK(session.coin_hud.displayed_cells[2]==old_digit&&session.coin_hud.pending_cells[2]==0xb010u);
    CHECK(session.rank_hud.pending_rank==3u&&session.rank_hud.badge_delay==5&&
        session.rank_hud.pending_cells[0]==23u&&session.rank_hud.displayed_cells[0]!=23u);
    PortableGpRaceSnapshot snapshot;
    CHECK(portable_gp_race_session_snapshot(&session,&snapshot)==0);
    const PortableGpRankHud rank_before=snapshot.rank_hud;
    const PortableGpCoinHud coins_before=snapshot.coin_hud;
    session.rank_hud.pending_cells[0]=999u;session.coin_hud.displayed_cells[2]=999u;
    PortableRacePresentationState presentation;
    CHECK(portable_gp_race_presentation_build(&session,&snapshot,&presentation));
    CHECK(memcmp(&rank_before,&presentation.gp_rank_hud,sizeof rank_before)==0&&
        memcmp(&coins_before,&presentation.gp_coin_hud,sizeof coins_before)==0);
    session.rank_hud=rank_before;session.coin_hud=coins_before;
    /* This legacy GP route fixture only supplies the selected pilot's AI
     * tables. Test the following accepted HUD upload through the valid
     * two-local race below, which has no unrelated CPU route owner. */
    PortableGpRaceHudAssetView legacy=rules;
    legacy.rank_transition_maps=NULL;
    CHECK(portable_gp_race_session_configure_hud(&session,&legacy,test_gp_clock_hud())==0&&
        !session.rank_hud.initialized&&!session.coin_hud.initialized);
    /* Each split-screen human has an independent pending/displayed owner. */
    PortableGpGridRow grid[8];
    for(unsigned i=0u;i<8u;++i)grid[i]=(PortableGpGridRow){race.rules.start_x_q16+(int32_t)i*32*65536,
        race.rules.start_y_q16,race.rules.start_heading};
    race.gp_grid=grid;race.gp_grid_count=8u;
    const PortableRecoveredRaceView *races[2]={&race,&race};const uint8_t drivers[2]={7u,7u};
    CHECK(test_gp_init_locals(&session,races,drivers,2u,0u,(PortableQuickRunOptions){0})==0);
    session.roster.racers[0].coin_count_178=2u;session.roster.racers[1].coin_count_178=7u;
    CHECK(portable_gp_race_session_configure_hud(&session,&rules,test_gp_clock_hud())==0&&session.local_index==0u);
    session.roster.racers[0].coin_count_178=0u;
    for(uint8_t local=0u;local<2u;++local){
        CHECK(portable_gp_race_session_select_local(&session,local)==0);
        session.runtime.session.max_progress_raw=(uint16_t)(local?0x8200u:0x8100u);
    }
    CHECK(portable_gp_race_session_select_local(&session,0u)==0);
    const uint16_t buttons[2]={0u,0u};
    CHECK(portable_gp_race_session_tick_locals(&session,buttons,&disposition)==0);
    for(uint8_t local=0u;local<2u;++local){
        CHECK(portable_gp_race_session_select_local(&session,local)==0);
        CHECK(portable_gp_race_session_snapshot(&session,&snapshot)==0);
        CHECK(snapshot.coin_hud.displayed_cells[2]==(local?0xb017u:0xb012u)&&
            snapshot.coin_hud.clock==session.source_frame_clock);
        CHECK(snapshot.lap_hud.displayed_lap==1u&&snapshot.lap_hud.pending_lap==(local?3u:2u));
    }
    CHECK(portable_gp_race_session_select_local(&session,0u)==0);
    CHECK(session.coin_hud.pending_cells[2]==0xb010u);
    CHECK(portable_gp_race_session_tick_locals(&session,buttons,&disposition)==0);
    for(uint8_t local=0u;local<2u;++local){
        CHECK(portable_gp_race_session_select_local(&session,local)==0);
        CHECK(portable_gp_race_session_snapshot(&session,&snapshot)==0);
        CHECK(snapshot.coin_hud.displayed_cells[2]==(local?0xb017u:0xb010u)&&
            snapshot.coin_hud.clock==session.source_frame_clock);
        CHECK(snapshot.lap_hud.displayed_lap==(local?3u:2u));
    }
    CHECK(portable_gp_race_session_select_local(&session,0u)==0);
}

static void test_impact_session(const PortableRecoveredRaceView *base)
{
    const PortableImpactEffectRules rules={{14u,14u,14u,14u,13u,13u}};
    PortableRecoveredRaceView race=*base;
    race.impact_effect_rules=&rules;race.dependencies.impact_effect_rules=&rules;
    int32_t sine[1024];
    for(unsigned i=0u;i<1024u;++i)sine[i]=(int32_t)lround(sin((double)i*6.283185307179586/1024.0)*32768.0);
    race.dependencies.sine_table=sine;race.dependencies.sine_count=1024u;
    static PortableGpRaceSession session;
    CHECK(test_gp_init(&session,&race,7u)==0 && session.player.impact_selected &&
        session.player_dependencies.impact_effect_rules==&rules);
    const uint16_t launch[PORTABLE_GP_LAUNCH_PRELUDE_TICKS]={0};
    CHECK(portable_gp_race_session_handoff_launch_inputs(&session,launch,PORTABLE_GP_LAUNCH_PRELUDE_TICKS)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;session.roster.race_phase_7d4=3u;
    PortableRaceTickDisposition disposition;
    for(unsigned family=0u;family<2u;++family){
        session.player.impact_effects=(PortableImpactEffects){0};
        session.player.vector_effect.contact_word_5a=family?0xc007u:0x8007u;
        session.roster.racers[0].kart_contact_state_5a=family?0xc007u:0x8007u;
        session.player.vector_effect_source_direction=0u;
        session.player.motion.component_x=session.player.motion.component_y=0;
        session.player.pose.component_x=session.player.pose.component_y=0;
        session.player.motion.speed_q16=0;
        session.player.normal_collision_source_from_native_state=1u;
        session.player.normal_collision_source_use_b550_table=1u;
        session.player.normal_collision_timer_38=8u;
        const uint8_t count18=session.driving_quality.event_18,count19=session.driving_quality.event_19;
        CHECK(portable_gp_race_session_tick(&session,0u,&disposition)==0);
        CHECK(session.player.impact_event==(family?2u:1u) &&
            !session.player.vector_effect.contact_word_5a && !session.roster.racers[0].kart_contact_state_5a &&
            session.player.normal_collision_timer_38==7u && session.player.impact_effects.slots[0].active &&
            session.player.impact_effects.slots[0].age==1u &&
            session.driving_quality.event_18==count18+(family?0u:1u) &&
            session.driving_quality.event_19==count19+(family?1u:0u));
        PortableGpRaceSnapshot snapshot;
        CHECK(portable_gp_race_session_snapshot(&session,&snapshot)==0);
        const PortableImpactEffects held=snapshot.impact_effects;
        CHECK(!memcmp(&held,&session.player.impact_effects,sizeof held));
        CHECK(portable_gp_race_session_tick(&session,0u,&disposition)==0 &&
            !session.player.impact_event && session.player.normal_collision_timer_38==6u &&
            session.player.impact_effects.slots[0].age==2u &&
            session.driving_quality.event_18==count18+(family?0u:1u) &&
            session.driving_quality.event_19==count19+(family?1u:0u) &&
            !memcmp(&held,&snapshot.impact_effects,sizeof held));
    }
}

static void test_general_kart_contacts(const PortableRecoveredRaceView *base)
{
    int32_t sine[1024];
    for(unsigned i=0u;i<1024u;++i)sine[i]=(int32_t)lround(sin((double)i*6.283185307179586/1024.0)*32768.0);
    const PortableImpactEffectRules effects={{14u,14u,14u,14u,13u,13u}};
    PortableRecoveredRaceView race=*base;race.rules.compatibility.course_id=28u;
    const int32_t push_entries[8]={-4,-8,-16,-24,-36,-56,-64,-85};
    const PortableSourceNormalResponseTableView push_table={push_entries,8u};
    race.dependencies.normal_response_table=&push_table;
    race.dependencies.sine_table=sine;race.dependencies.sine_count=1024u;
    race.dependencies.impact_effect_rules=&effects;race.impact_effect_rules=&effects;
    static PortableGpRaceSession session;
    PortableRaceTickDisposition disposition;
    for(uint8_t driver=0u;driver<2u;++driver){
        race.rules.compatibility.character_class=driver;
        race.initial_driver.auto_acceleration.character_class=driver;
        CHECK(test_gp_init(&session,&race,driver)==0);
        session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;session.runtime.session.countdown_remaining=0u;
        session.roster.race_phase_7d4=3u;session.runtime.session.elapsed_ticks=10u;
        /* Isolate the pair from the other six grid actors while retaining
         * the ordinary eight-racer GP stack and its shared collision pass. */
        for(unsigned i=2u;i<8u;++i){
            session.roster.racers[i].position_x_q16=session.player.pose.position_x+(int32_t)(200u+i*16u)*65536;
            session.roster.racers[i].position_y_q16=session.player.pose.position_y;
        }
        PortableGpRacer *other=&session.roster.racers[1];
        other->position_x_q16=session.player.pose.position_x+4*65536;
        other->position_y_q16=session.player.pose.position_y;
        other->contact_center_x=(int16_t)(other->position_x_q16/65536);
        other->contact_center_y=(int16_t)(other->position_y_q16/65536);
        other->route_index=session.roster.racers[0].route_index;
        other->component_x=-64;other->component_y=0;other->world_height_q16=0;
        other->banana_hit_timer_38=0u;other->contact_status_60=0u;other->contact_status_66=0;
        other->lightning_state_78=0u;other->hit_cue_flags_58|=0x40u;
        session.player.motion.component_x=session.player.pose.component_x=128;
        session.player.motion.component_y=session.player.pose.component_y=0;
        session.roster.racers[0].coin_count_178=5u;session.rival_coin_count_handoff=5u;
        const PortableGpRaceSessionStatus contact_status=portable_gp_race_session_tick(&session,0u,&disposition);
        if(contact_status!=PORTABLE_GP_RACE_SESSION_OK)
            fprintf(stderr,"general contact driver=%u status=%u stage=%u pilot=%d timer=%u events=%u flags=%x\n",
                driver,contact_status,session.failed_roster_stage,session.runtime.last_driver_status,
                session.player.normal_collision_timer_38,session.roster.contact_events,session.player.steering.state_flags);
        CHECK(contact_status==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(session.roster.racers[0].kart_contact_events==1u && session.player.normal_collision_timer_38==7u &&
            other->banana_hit_timer_38==7u && session.roster.racers[0].coin_count_178==4u &&
            session.driving_quality.event_1a==1u && session.player.impact_effects.slots[0].active &&
            session.player.impact_effects.slots[0].age==1u);
        CHECK(portable_gp_race_session_tick(&session,0u,&disposition)==0 &&
            session.roster.racers[0].kart_contact_events==1u && session.player.normal_collision_timer_38==6u);
    }
    PortableGpGridRow grid[8];
    for(unsigned i=0u;i<8u;++i)grid[i]=(PortableGpGridRow){
        race.rules.start_x_q16+(int32_t)(i&1u)*32*65536,
        race.rules.start_y_q16+(int32_t)i*12*65536,race.rules.start_heading};
    race.gp_grid=grid;race.gp_grid_count=8u;
    PortableRecoveredRaceView local_races[2]={race,race};
    for(uint8_t k=0u;k<2u;++k){
        local_races[k].rules.compatibility.character_class=k;
        local_races[k].initial_driver.auto_acceleration.character_class=k;
    }
    const PortableRecoveredRaceView *races[2]={&local_races[0],&local_races[1]};const uint8_t drivers[2]={0u,1u};
    CHECK(test_gp_init_locals(&session,races,drivers,2u,0u,(PortableQuickRunOptions){0})==0);
    const int32_t x=session.player.pose.position_x,y=session.player.pose.position_y;
    for(uint8_t k=0u;k<2u;++k){
        CHECK(portable_gp_race_session_select_local(&session,k)==0);
        session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;session.runtime.session.countdown_remaining=0u;
        session.player.pose.position_x=x+(int32_t)k*4*65536;session.player.pose.position_y=y;
        session.player.motion.component_x=session.player.pose.component_x=k?-64:128;
        session.player.motion.component_y=session.player.pose.component_y=0;
        session.roster.racers[k].contact_center_x=(int16_t)(session.player.pose.position_x/65536);
        session.roster.racers[k].contact_center_y=(int16_t)(y/65536);
        session.roster.racers[k].route_index=session.roster.racers[0].route_index;
        session.roster.racers[k].coin_count_178=5u;
    }
    CHECK(portable_gp_race_session_select_local(&session,0u)==0);session.roster.race_phase_7d4=3u;
    const uint16_t buttons[2]={0u,0u};
    CHECK(portable_gp_race_session_tick_locals(&session,buttons,&disposition)==0 && session.local_index==0u &&
        session.roster.racers[0].kart_contact_events==1u);
    for(uint8_t k=0u;k<2u;++k){
        CHECK(portable_gp_race_session_select_local(&session,k)==0 &&
            session.player.normal_collision_timer_38==7u && session.roster.racers[k].coin_count_178==4u && session.roster.racers[k].kart_contact_events==1u &&
            session.player.impact_effects.slots[0].age==1u);
    }
    CHECK(portable_gp_race_session_select_local(&session,0u)==0);
    CHECK(portable_gp_race_session_tick_locals(&session,buttons,&disposition)==0 && session.roster.racers[0].kart_contact_events==1u);
}

/* The actual GP/VS turn adopts the size response into the selected pilot,
 * rather than leaving a roster-only strong-hit request. */
static void test_size_contact_sessions(const PortableRecoveredRaceView *base)
{
    int32_t sine[1024];
    for (unsigned i = 0u; i < 1024u; ++i)
        sine[i] = (int32_t)lround(sin((double)i * 6.283185307179586 / 1024.0) * 32768.0);
    const int32_t entries[8] = {-4, -8, -16, -24, -36, -56, -64, -85};
    const PortableSourceNormalResponseTableView response = {entries, 8u};
    const PortableImpactEffectRules effects = {{14u, 14u, 14u, 14u, 13u, 13u}};
    PortableRecoveredRaceView race = *base;
    race.rules.compatibility.course_id = 28u;
    race.dependencies.normal_response_table = &response;
    race.dependencies.sine_table = sine;
    race.dependencies.sine_count = 1024u;
    race.dependencies.impact_effect_rules = &effects;
    race.impact_effect_rules = &effects;
    PortableGpGridRow grid[8];
    for (unsigned i = 0u; i < 8u; ++i)
        grid[i] = (PortableGpGridRow){race.rules.start_x_q16 + (int32_t)(i & 1u) * 32 * 65536,
                                      race.rules.start_y_q16 + (int32_t)i * 12 * 65536,
                                      race.rules.start_heading};
    race.gp_grid = grid;
    race.gp_grid_count = 8u;
    const PortableRecoveredRaceView *races[2] = {&race, &race};
    const uint8_t drivers[2] = {7u, 7u};
    const uint16_t neutral[2] = {0u, 0u};
    static PortableGpRaceSession session;
    for (unsigned vs = 0u; vs < 2u; ++vs)
        for (unsigned small = 0u; small < 2u; ++small)
        {
            if (vs)
                CHECK(test_gp_init_locals(&session, races, drivers, 2u, 0u,
                                          (PortableQuickRunOptions){0}) == 0);
            else
                CHECK(test_gp_init(&session, &race, 7u) == 0);
            const int32_t x = session.player.pose.position_x, y = session.player.pose.position_y;
            const unsigned locals = vs ? 2u : 1u;
            for (uint8_t k = 0u; k < locals; ++k)
            {
                CHECK(portable_gp_race_session_select_local(&session, k) == 0);
                session.runtime.session.phase = PORTABLE_RACE_PHASE_RACING;
                session.runtime.session.countdown_remaining = 0u;
                session.player.pose.position_x = x + (int32_t)k * 4 * 65536;
                session.player.pose.position_y = y;
                session.player.motion.component_x = session.player.pose.component_x = 0;
                session.player.motion.component_y = session.player.pose.component_y = 0;
                session.player.item_cue_flags_58 = (uint16_t)(0x40u | (k == small ? 0u : 0x10u));
                session.roster.racers[k].hit_cue_flags_58 = session.player.item_cue_flags_58;
            }
            CHECK(portable_gp_race_session_select_local(&session, 0u) == 0);
            session.roster.race_phase_7d4 = 3u;
            for (unsigned i = 0u; i < portable_gp_race_roster_count(&session.roster); ++i)
            {
                PortableGpRacer *r = &session.roster.racers[i];
                r->position_x_q16 = x + (int32_t)(i < 2u ? i * 4u : 200u + i * 16u) * 65536;
                r->position_y_q16 = y;
                r->contact_center_x = (int16_t)(r->position_x_q16 / 65536);
                r->contact_center_y = (int16_t)(y / 65536);
                r->route_index = session.roster.racers[0].route_index;
                /* CA78 view reactions share contact_events with physical
                 * impacts. Keep an existing presentation reaction active so
                 * this fixture counts only the isolated physical pair. */
                r->contact_mode_6e = 8u;
                r->coin_count_178 = 9u;
                if (i < 2u)
                {
                    r->hit_cue_flags_58 = (uint16_t)(0x40u | (i == small ? 0u : 0x10u));
                    r->lightning_timer_68 = i == small ? 100u : 0u;
                    r->banana_hit_timer_38 = 0u;
                    r->contact_status_60 = 0u;
                    r->contact_status_66 = 0;
                    r->lightning_state_78 = 0u;
                    r->world_height_q16 = 0;
                    r->component_x = r->component_y = 0;
                }
            }
            session.rival_coin_count_handoff = 9u;
            PortableRaceTickDisposition disposition;
            CHECK((vs ? portable_gp_race_session_tick_locals(&session, neutral, &disposition)
                      : portable_gp_race_session_tick(&session, 0u, &disposition)) == 0);
            CHECK(session.roster.contact_events == 1u &&
                  session.roster.racers[0].kart_contact_events == 1u &&
                  session.roster.racers[1].kart_contact_events == 1u);
            /* Local adoption decrements the fresh8 immediately; the CPU
             * strong response is queued after its movement pass. */
            CHECK(session.roster.racers[small].banana_hit_timer_38 ==
                      (vs || small == 0u ? 7u : 8u) &&
                  !session.roster.racers[1u - small].banana_hit_timer_38);
            /* GP mirrors the local count to rivals on contact; VS keeps
             * each local's count independent. The direct roster test above
             * separately verifies that only the victim requests coin loss. */
            CHECK(session.roster.racers[small].coin_count_178 == (vs || small == 0u ? 5u : 9u) &&
                  session.roster.racers[1u - small].coin_count_178 ==
                      (vs || small != 0u ? 9u : 5u));
            CHECK(session.roster.pending_voice_count == 1u &&
                  session.roster.pending_voices[0].racer == small &&
                  session.roster.pending_voices[0].ticks == 15u);
            if (vs || small == 0u)
            {
                CHECK(portable_gp_race_session_select_local(&session, (uint8_t)small) == 0);
                CHECK(session.player.normal_collision_timer_38 == 7u &&
                      session.player.strong_spin_rate_b6 != 0u && !session.player.flattened_64);
                CHECK(session.driving_quality.event_1a == (vs ? 0u : 1u));
            }
            CHECK(portable_gp_race_session_select_local(&session, 0u) == 0);
            CHECK((vs ? portable_gp_race_session_tick_locals(&session, neutral, &disposition)
                      : portable_gp_race_session_tick(&session, 0u, &disposition)) == 0);
            CHECK(session.roster.contact_events == 1u &&
                  session.roster.racers[small].coin_count_178 == (vs || small == 0u ? 5u : 9u) &&
                  session.roster.pending_voice_count == 1u);
        }
}

/* Accepted GP/VS turns must see the C0 cell before the same-turn AC loss.
 * All positions and duplicated placements remain in the ordinary owned map. */
static void test_coin_haunt_order(const PortableRecoveredRaceView *base)
{
    static uint8_t cells[256u*256u];
    uint8_t flags[256];
    CHECK(base->dependencies.track_material->count==sizeof flags);
    memcpy(flags,base->dependencies.track_material->flags,sizeof flags);
    flags[0]=0x40u;flags[0xc0]=0xc0u;flags[0xc1]=0x40u;
    const PortableTerrainView terrain={cells,256u,256u,256u};
    PortableTrackMaterialView material=*base->dependencies.track_material;material.flags=flags;
    const PortableCoinEffectRules effects={.horizontal_step={0,-2,2,-2,2,-2,2,-2,2},
        .upward_speed_q16={0x80000,0x80000,0x80000,0x50000,0x50000,0x80000,0x80000,0x50000,0x50000},
        .bounce_volumes={256u,153u,102u},.script_ticks={27u,66u,21u,5u}};
    PortableRecoveredRaceView race=*base;
    /* Body projection, like the existing coin FX fixture, consumes Q15 sine. */
    int32_t sine[1024];
    for (unsigned i=0u;i<1024u;++i)
        sine[i]=(int32_t)lround(sin((double)i*6.283185307179586/1024.0)*32768.0);
    race.dependencies.sine_table=sine;race.dependencies.sine_count=1024u;
    PortableGpGridRow grid[8];PortableRecoveredCoin placements[6];
    for (unsigned k=0u;k<8u;++k)
        grid[k]=(PortableGpGridRow){(872+(int32_t)k*32)*65536,612*65536,0u};
    for (unsigned k=0u;k<3u;++k)
        placements[k*2u]=placements[k*2u+1u]=(PortableRecoveredCoin){
            .position_x_q16=grid[k].x_q16,.position_y_q16=grid[k].y_q16};
    race.gp_grid=grid;race.gp_grid_count=8u;
    race.coins=placements;race.course_objects=NULL;race.course_object_count=0u;
    race.coin_effect_rules=&effects;
    race.dependencies.terrain=&terrain;race.dependencies.track_material=&material;
    race.dependencies.owned_terrain_cells=cells;race.dependencies.owned_terrain_capacity=sizeof cells;
    const PortableRecoveredRaceView *races[3]={&race,&race,&race};
    const uint8_t classes[3]={7u,7u,7u};
    static const struct
    {
        uint16_t coins,lock,buttons,star,remaining,lock_after;
        uint8_t picked;
    } cases[]={
        {99u,64u,0u,0u,98u,63u,0u},
        {98u,64u,0u,0u,98u,63u,1u},
        {99u,63u,0u,0u,99u,62u,0u},
        {99u,65u,0u,0u,99u,64u,0u},
        {99u,0u,0u,0u,99u,0u,0u},
        {99u,64u,0u,20u,99u,0u,0u},
        {99u,64u,PORTABLE_INPUT_ACCELERATE,0u,99u,53u,0u},
        {98u,0u,0u,0u,99u,0u,1u}
    };
    static PortableGpRaceSession session;
    for (unsigned locals=1u;locals<=3u;++locals)
    for (unsigned row=0u;row<sizeof cases/sizeof cases[0];++row)
    {
        memset(cells,0,sizeof cells);race.coin_count=locals*2u;
        const PortableGpRaceSessionStatus init=locals==1u?test_gp_init(&session,&race,7u):
            test_gp_init_locals(&session,races,classes,(uint8_t)locals,0u,(PortableQuickRunOptions){0});
        CHECK(init==0);
        for (unsigned k=locals;k<portable_gp_race_roster_count(&session.roster);++k)
        {
            session.roster.racers[k].position_x_q16=(1400+(int32_t)k*20)*65536;
            session.roster.racers[k].position_y_q16=(1400+(int32_t)k*20)*65536;
        }
        for (uint8_t k=0u;k<locals;++k)
        {
            CHECK(portable_gp_race_session_select_local(&session,k)==0);
            session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
            session.runtime.session.countdown_remaining=0u;
            session.runtime.session.elapsed_ticks=1u;
            session.player.pose.position_x=grid[k].x_q16;
            session.player.pose.position_y=grid[k].y_q16;
            session.player.pose.position_z=0;session.player.hop.mode=0u;
            session.player.motion.speed_q16=session.player.motion.acceleration_q16=0;
            session.player.motion.component_x=session.player.motion.component_y=0;
            session.player.item_lock_ac=cases[row].lock;
            session.previous_semantic_buttons=0u;
            session.roster.racers[k].item_lock_ac=cases[row].lock;
            session.roster.racers[k].coin_count_178=cases[row].coins;
            session.roster.racers[k].lightning_status_6a=(int16_t)cases[row].star;
        }
        CHECK(portable_gp_race_session_select_local(&session,0u)==0);
        const uint16_t buttons[3]={cases[row].buttons,cases[row].buttons,cases[row].buttons};
        PortableRaceTickDisposition disposition;
        const PortableGpRaceSessionStatus status=locals==1u?
            portable_gp_race_session_tick(&session,buttons[0],&disposition):
            portable_gp_race_session_tick_locals(&session,buttons,&disposition);
        if (status)fprintf(stderr,"coin/haunt accepted tick locals=%u case=%u status=%u\n",locals,row,(unsigned)status);
        CHECK(status==0 && session.local_index==0u &&
              session.roster.coin_events==locals*cases[row].picked);
        unsigned pickups=0u;
        for (unsigned audio=0u;audio<session.audio_event_count;++audio)
            pickups+=session.audio_events[audio].song_id==101u;
        CHECK(pickups==locals*cases[row].picked);
        for (uint8_t k=0u;k<locals;++k)
        {
            CHECK(portable_gp_race_session_select_local(&session,k)==0);
            const size_t cell=612u/8u*256u+(size_t)(872u+k*32u)/8u;
            CHECK(session.roster.racers[k].coin_count_178==cases[row].remaining &&
                  session.roster.racers[k].item_lock_ac==cases[row].lock_after &&
                  session.player.input.speed_parameter==99u &&
                  cells[cell]==(cases[row].picked?0xc1u:0xc0u) &&
                  session.coins[k*2u].active==!cases[row].picked &&
                  session.coins[k*2u+1u].active==!cases[row].picked);
            if (row<2u)
                CHECK(session.coin_effects.slots[0].cause==PORTABLE_COIN_EFFECT_CAUSE_HAUNT &&
                      session.coin_effects.slots[0].variant==3u);
            else CHECK(!session.coin_effects.slots[0].kind);
        }
        CHECK(portable_gp_race_session_select_local(&session,0u)==0);
        if (row==0u)
        {
            /* The retained coin is genuinely available on the next turn;
             * the old drain-first order consumed it on the previous turn. */
            const uint16_t neutral[3]={0u,0u,0u};
            CHECK((locals==1u?portable_gp_race_session_tick(&session,0u,&disposition):
                  portable_gp_race_session_tick_locals(&session,neutral,&disposition))==0 &&
                  session.roster.coin_events==locals);
            for (unsigned k=0u;k<locals;++k)
                CHECK(session.roster.racers[k].coin_count_178==99u &&
                      !session.coins[k*2u].active && !session.coins[k*2u+1u].active);
        }
        CHECK(session.local_index==0u);
    }
}

static void test_local_players(const PortableRecoveredRaceView *base)
{
    test_coin_haunt_order(base);
    static PortableGpRaceSession session;
    PortableRecoveredRaceView race=*base;
    PortableGpGridRow grid[8];
    for(unsigned row=0u;row<8u;++row)
        grid[row]=(PortableGpGridRow){(INT32_C(872)+(int32_t)(row&1u)*32)<<16u,
            (INT32_C(612)+(int32_t)row*12)<<16u,0u};
    race.gp_grid=grid;race.gp_grid_count=8u;
    const PortableRecoveredRaceView *const races[3]={&race,&race,&race};
    const uint8_t classes[3]={7u,7u,7u};
    PortableRaceTickDisposition disposition;
    CHECK(test_gp_init_locals(&session,races,classes,1u,0u,
        (PortableQuickRunOptions){0})==PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT);
    CHECK(test_gp_init_locals(&session,races,classes,3u,0u,
        (PortableQuickRunOptions){0})==PORTABLE_GP_RACE_SESSION_OK);
    CHECK(session.local_count==3u && session.local_index==0u &&
          portable_gp_race_roster_count(&session.roster)==3u &&
          session.roster.race_mode_flags_7da==0x0400u);
    for(unsigned k=0u;k<3u;++k)CHECK(session.roster.racers[k].item_inventory.held_item_17a==0u);
    {
        /* VS mushroom start (save +0A): every local starts holding 0x1008. */
        static PortableGpRaceSession mushroom;
        CHECK(test_gp_init_locals(&mushroom,races,classes,3u,0u,
            (PortableQuickRunOptions){0u,0u,0u,2u})==PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT);
        CHECK(test_gp_init_locals(&mushroom,races,classes,3u,0u,
            (PortableQuickRunOptions){0u,0u,0u,1u})==PORTABLE_GP_RACE_SESSION_OK);
        for(unsigned k=0u;k<3u;++k)
            CHECK(mushroom.roster.racers[k].item_inventory.held_item_17a==0x1008u);
        CHECK(mushroom.local_index==0u && mushroom.roster.item_hud.current==0x1008u);
    }
    {
        static PortableGpRaceSession scripted;
        CHECK(test_gp_init_locals(&scripted,races,classes,3u,0u,
            (PortableQuickRunOptions){0})==PORTABLE_GP_RACE_SESSION_OK);
        LocalScriptProbe probe={.session=&scripted,.items={0x1007u,0x1008u,0x1009u}};
        const PortableRaceScriptHooks hooks={&probe,local_script_after_tick,local_script_item_use};
        scripted.script_hooks=&hooks;
        const uint16_t released[3]={0u,0u,0u};
        const uint16_t pressed[3]={PORTABLE_INPUT_ITEM,PORTABLE_INPUT_ITEM,PORTABLE_INPUT_ITEM};
        for(unsigned tick=0u;tick<120u;++tick)
            CHECK(portable_gp_race_session_tick_locals(&scripted,released,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(probe.ticks==120u); /* One callback, rather than three per tick. */
        for(uint8_t k=0u;k<3u;++k){
            CHECK(portable_gp_race_session_select_local(&scripted,k)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(portable_gp_race_session_grant_player_item(&scripted,probe.items[k])==PORTABLE_GP_RACE_SESSION_OK);
        }
        CHECK(portable_gp_race_session_select_local(&scripted,0u)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(portable_gp_race_session_tick_locals(&scripted,pressed,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(probe.ticks==121u);
        for(unsigned k=0u;k<3u;++k)
            CHECK(probe.offers[k]==1u && scripted.roster.racers[k].item_inventory.held_item_17a==probe.items[k]);
        CHECK(portable_gp_race_session_tick_locals(&scripted,pressed,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        for(unsigned k=0u;k<3u;++k)CHECK(probe.offers[k]==1u);
        CHECK(portable_gp_race_session_tick_locals(&scripted,released,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(portable_gp_race_session_tick_locals(&scripted,pressed,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(probe.ticks==124u);
        for(unsigned k=0u;k<3u;++k)CHECK(probe.offers[k]==2u);
        scripted.script_hooks=NULL;
        scripted.roster.script_hooks=NULL;
    }
    {
        /* The shared FX pass follows every local/world pass once. A retained
         * request keeps its producer clock even when drained on a later tick. */
        static PortableGpRaceSession coins;
        int32_t coin_sine[1024];
        for(unsigned i=0u;i<1024u;++i)
            coin_sine[i]=(int32_t)lround(sin((double)i*6.283185307179586/1024.0)*32768.0);
        const PortableCoinEffectRules rules={.horizontal_step={0,-2,2,-2,2,-2,2,-2,2},
            .upward_speed_q16={0x80000,0x80000,0x80000,0x50000,0x50000,0x80000,0x80000,0x50000,0x50000},
            .bounce_volumes={256u,153u,102u},.script_ticks={27u,66u,21u,5u}};
        CHECK(test_gp_init_locals(&coins,races,classes,3u,0u,
            (PortableQuickRunOptions){0})==0);
        coins.coin_effect_rules=&rules;
        const PortableImpactEffectRules impact_rules={{14u,14u,14u,14u,13u,13u}};
        for(uint8_t k=0u;k<3u;++k){
            CHECK(portable_gp_race_session_select_local(&coins,k)==0);
            coins.player_dependencies.sine_table=coin_sine;
            coins.player_dependencies.sine_count=1024u;
            coins.player_dependencies.impact_effect_rules=&impact_rules;
            coins.player.impact_effects.slots[0]=(PortableImpactEffectSlot){.active=1u,.variant=k,.age=4u};
            coins.roster.racers[k].coin_loss_event_count=1u;
            coins.roster.racers[k].coin_loss_events[0].cause=
                k==1u?PORTABLE_COIN_EFFECT_CAUSE_CONTACT:PORTABLE_COIN_EFFECT_CAUSE_HIT;
            coins.roster.racers[k].coin_loss_events[0].loss=1u;
            coins.roster.racers[k].coin_loss_events[0].remaining=1u;
            coins.roster.racers[k].coin_loss_events[0].clock=3u;
        }
        CHECK(portable_gp_race_session_select_local(&coins,0u)==0);
        coins.source_frame_clock=9u; /* drain clock10 differs in both low bits */
        const uint16_t released[3]={0u,0u,0u};
        for(unsigned tick=1u;tick<=2u;++tick){
            CHECK(portable_gp_race_session_tick_locals(&coins,released,&disposition)==0 && coins.local_index==0u);
            for(uint8_t k=0u;k<3u;++k){
                CHECK(portable_gp_race_session_select_local(&coins,k)==0);
                const PortableCoinEffectSlot *effect=&coins.coin_effects.slots[0];
                CHECK(!coins.roster.racers[k].coin_loss_event_count &&
                    coins.player.impact_effects.slots[0].age==4u+tick &&
                    coins.player.impact_effects.slots[0].variant==k);
                if(k==1u)CHECK(effect->kind==PORTABLE_COIN_EFFECT_BOUNCE && effect->variant==4u && effect->age==tick);
                else CHECK(effect->kind==PORTABLE_COIN_EFFECT_DROP_EMITTER && effect->counter==tick &&
                    effect->x==coins.coin_birth_x+1); /* clock3: +4-3 */
                CHECK(!coins.coin_effects.slots[1].kind);
            }
            CHECK(portable_gp_race_session_select_local(&coins,0u)==0);
        }
    }
    {
        /* D4E4 acts on the selected local owner, never on both coin counts.
         * Use the same valid decoded class7 views as the other VS fixtures. */
        static PortableGpRaceSession coins;
        CHECK(test_gp_init_locals(&coins,races,classes,2u,0u,(PortableQuickRunOptions){0})==0);
        const int32_t scenes[]={INT32_MIN+1,INT32_MIN,0};
        const uint8_t enabled[]={1u,0u,1u};
        for (unsigned row=0u;row<3u;++row)
        {
            coins.source_scene_row_c88=scenes[row];
            coins.roster.racers[0].coin_count_178=9u;
            coins.roster.racers[1].coin_count_178=7u;
            for (uint8_t local=0u;local<2u;++local)
            {
                CHECK(portable_gp_race_session_select_local(&coins,local)==0);
                coins.player.steering.state_flags=UINT32_C(0x8000);
                const uint16_t other=coins.roster.racers[1u-local].coin_count_178;
                const uint16_t own=coins.roster.racers[local].coin_count_178;
                CHECK(portable_gp_race_session_apply_coin_loss(&coins,PORTABLE_COIN_EFFECT_CAUSE_RETRIEVAL)==0 &&
                      coins.recovery_coin_loss==(enabled[row]?2u:0u) &&
                      coins.roster.racers[local].coin_count_178==own-(enabled[row]?2u:0u) &&
                      coins.roster.racers[1u-local].coin_count_178==other);
            }
            CHECK(portable_gp_race_session_select_local(&coins,0u)==0 && coins.local_index==0u &&
                  coins.roster.racers[0].coin_count_178==(enabled[row]?7u:9u) &&
                  coins.roster.racers[1].coin_count_178==(enabled[row]?5u:7u));
        }
    }
    {
        /* The real progress callback retires a finisher before the outer VS
         * pass. Reproduce that boundary with completed, distinct lap clocks;
         * a flags20 guard must not suppress either recorded result. */
        static PortableGpRaceSession finished;
        const uint32_t finish_cs[2]={2676u,3104u};
        CHECK(test_gp_init_locals(&finished,races,classes,3u,0u,
            (PortableQuickRunOptions){0})==0);
        for(uint8_t local=0u;local<2u;++local){
            CHECK(portable_gp_race_session_select_local(&finished,local)==0);
            finished.runtime.session.phase=PORTABLE_RACE_PHASE_FINISHED;
            finished.runtime.session.laps_completed=finished.runtime.session.lap_count;
            portable_driver_pilot_retire_selected(&finished.player);
            finished.roster.racers[local].source_flags=finished.player.steering.state_flags;
            PortableGpSourceClock *clock=&finished.source_result_clock;
            clock->enabled=1u;clock->laps_recorded=clock->lap_count;
            clock->finish_centiseconds=finish_cs[local];
            clock->centiseconds=finish_cs[local]+57u;
        }
        CHECK(portable_gp_race_session_select_local(&finished,0u)==0);
        const uint16_t neutral[3]={0u,0u,0u};
        for(unsigned tick=0u;tick<3u;++tick){
            CHECK(portable_gp_race_session_tick_locals(&finished,neutral,&disposition)==0);
            CHECK(finished.vs_finish_centiseconds[0]==finish_cs[0] &&
                finished.vs_finish_centiseconds[1]==finish_cs[1] &&
                finished.vs_finish_centiseconds[2]==0u && finished.vs_retired[2]);
        }
        CHECK(portable_gp_race_session_locals_finished(&finished));
        PortableVsResults results;
        CHECK(portable_vs_results_build(&finished,&results)==PORTABLE_GP_RACE_RESULTS_OK);
        CHECK(results.count==3u);
        for(unsigned row=0u;row<3u;++row){
            const unsigned local=results.rows[row].racer_index;
            CHECK(local<3u);
            CHECK(results.rows[row].retired==(local==2u));
            CHECK(results.rows[row].centiseconds==(local<2u?finish_cs[local]:0u));
        }
        /* Selection exchanges whole local clock owners without replacing
         * another racer's frozen result with the current HUD timer. */
        CHECK(portable_gp_race_session_select_local(&finished,1u)==0);
        CHECK(finished.source_result_clock.centiseconds>finish_cs[1]+57u &&
            finished.source_result_clock.finish_centiseconds==finish_cs[1]);
        CHECK(portable_gp_race_session_select_local(&finished,0u)==0 &&
            finished.source_result_clock.finish_centiseconds==finish_cs[0]);
    }
    for(unsigned k=0u;k<3u;++k)
        CHECK(session.roster.racers[k].is_player &&
              session.roster.racers[k].position_x_q16==grid[k].x_q16 &&
              session.roster.racers[k].position_y_q16==grid[k].y_q16);
    CHECK(portable_gp_race_session_tick(&session,0u,&disposition)==
          PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT);
    const uint16_t buttons[3]={0u,PORTABLE_INPUT_ACCELERATE,0u};
    for(unsigned tick=0u;tick<120u;++tick)
        CHECK(portable_gp_race_session_tick_locals(&session,buttons,&disposition)==
              PORTABLE_GP_RACE_SESSION_OK && session.local_index==0u);
    CHECK(session.roster.racers[1].position_y_q16!=grid[1].y_q16 &&
          session.roster.racers[0].position_y_q16==grid[0].y_q16 &&
          session.roster.racers[2].position_y_q16==grid[2].y_q16);
    PortableGpRaceSnapshot view;
    session.coin_effects.slots[0]=(PortableCoinEffectSlot){.kind=PORTABLE_COIN_EFFECT_BOUNCE,.age=7u,.x=91};
    session.coin_birth_x=92;session.coin_birth_y=93;session.coin_birth_inverse=256;
    CHECK(portable_gp_race_session_select_local(&session,1u)==0 && !session.coin_effects.slots[0].kind);
    session.coin_effects.slots[2]=(PortableCoinEffectSlot){.kind=PORTABLE_COIN_EFFECT_RISE,.age=11u,.y=81};
    session.coin_birth_x=82;
    CHECK(portable_gp_race_session_select_local(&session,0u)==0 && session.coin_effects.slots[0].age==7u &&
        session.coin_effects.slots[0].x==91 && session.coin_birth_x==92 && !session.coin_effects.slots[2].kind);
    CHECK(portable_gp_race_session_select_local(&session,1u)==0 && session.coin_effects.slots[2].age==11u &&
        session.coin_birth_x==82 && !session.coin_effects.slots[0].kind);
    session.coin_effects=(PortableCoinEffects){0};
    CHECK(portable_gp_race_session_select_local(&session,0u)==0);
    session.coin_effects=(PortableCoinEffects){0};
    session.surface_trails.slots[0].age=7u;session.surface_trails.side=1u;
    CHECK(portable_gp_race_session_select_local(&session,1u)==0 && !session.surface_trails.slots[0].age);
    session.surface_trails.slots[2].age=11u;session.surface_trails.eligible_counter=3u;
    CHECK(portable_gp_race_session_select_local(&session,0u)==0 && session.surface_trails.slots[0].age==7u &&
        session.surface_trails.side==1u && !session.surface_trails.slots[2].age);
    CHECK(portable_gp_race_session_select_local(&session,1u)==0 && session.surface_trails.slots[2].age==11u &&
        session.surface_trails.eligible_counter==3u && !session.surface_trails.slots[0].age);
    session.surface_trails=(PortableSurfaceTrail){0};
    CHECK(portable_gp_race_session_select_local(&session,0u)==0);
    session.surface_trails=(PortableSurfaceTrail){0};
    session.recovery_fx.thaw_age=3u;session.recovery_fx.pending_thaw=1u;
    session.recovery_fx_events=PORTABLE_RECOVERY_FX_THAW_START;
    CHECK(portable_gp_race_session_select_local(&session,1u)==0 && !session.recovery_fx.thaw_age &&
        !session.recovery_fx.pending_thaw && !session.recovery_fx_events);
    session.recovery_fx.drips[0].age=2u;session.recovery_fx_events=PORTABLE_RECOVERY_FX_DRIP_START;
    CHECK(portable_gp_race_session_select_local(&session,0u)==0 && session.recovery_fx.thaw_age==3u &&
        session.recovery_fx.pending_thaw==1u && !session.recovery_fx.drips[0].age && session.recovery_fx_events==PORTABLE_RECOVERY_FX_THAW_START);
    session.recovery_fx=(PortableRecoveryFx){0};session.recovery_fx_events=0u;
    CHECK(portable_gp_race_session_select_local(&session,1u)==0 && session.recovery_fx.drips[0].age==2u &&
        !session.recovery_fx.thaw_age && session.recovery_fx_events==PORTABLE_RECOVERY_FX_DRIP_START);
    session.recovery_fx=(PortableRecoveryFx){0};session.recovery_fx_events=0u;
    CHECK(portable_gp_race_session_select_local(&session,0u)==0);
    CHECK(portable_gp_race_session_select_local(&session,3u)==PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT);
    CHECK(portable_gp_race_session_select_local(&session,1u)==PORTABLE_GP_RACE_SESSION_OK);
    CHECK(portable_gp_race_session_snapshot(&session,&view)==PORTABLE_GP_RACE_SESSION_OK);
    CHECK(view.player_y_q16==session.roster.racers[1].position_y_q16 &&
          view.opponents[0].position_y_q16==grid[0].y_q16 && view.opponents[0].visible &&
          view.opponents[1].position_y_q16==grid[2].y_q16 && view.opponents[1].visible &&
          !view.opponents[2].visible);
    session.player.steering.state_flags|=UINT32_C(0x20);
    CHECK(portable_gp_race_session_select_local(&session,0u)==PORTABLE_GP_RACE_SESSION_OK);
    CHECK(portable_gp_race_session_tick_locals(&session,buttons,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
    CHECK((session.roster.race_phase_7d4&0x0fu)==0u && !session.vs_retired[2]);
    session.player.steering.state_flags|=UINT32_C(0x20);
    CHECK(portable_gp_race_session_tick_locals(&session,buttons,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
    CHECK((session.roster.race_phase_7d4&0x0fu)==3u && session.vs_retired[2] &&
          !session.vs_retired[0] && !session.vs_retired[1] &&
          !portable_gp_race_session_locals_finished(&session));
    for(unsigned tick=0u;tick<2u;++tick)
        CHECK(portable_gp_race_session_tick_locals(&session,buttons,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
    CHECK(portable_gp_race_session_locals_finished(&session));
}

static void test_script_hooks(const PortableRecoveredRaceView *race)
{
    /* Observing scripts, including race.random draws, leave the race identical. */
    const char observe[]=
        "local n = 0\n"
        "function on_tick(race) n = n + race.random(6) end\n"
        "function on_item_use(kart, item) n = n + 1 end\n"
        "function on_race_start(race) race.message('go', 30) end\n";
    const PortableScriptSource observer={"observer",observe,sizeof observe-1u};
    PortableScriptHost *host=portable_script_host_create(&observer,1u);
    CHECK(host!=NULL);
    CHECK(scripted_race_digest(race,host,1)==scripted_race_digest(race,NULL,1));
    portable_script_host_free(host);
    /* Effects are reproducible: the same mods and input give the same digest. */
    const char effect[]=
        "local prizes = { 'mushroom', 'star', 'banana' }\n"
        "function on_race_start(race) race.give_item(1, prizes[race.random(3)]) end\n";
    const PortableScriptSource effects={"effects",effect,sizeof effect-1u};
    host=portable_script_host_create(&effects,1u);
    CHECK(host!=NULL);
    const uint64_t first=scripted_race_digest(race,host,0);
    CHECK(first==scripted_race_digest(race,host,0));
    CHECK(first!=scripted_race_digest(race,NULL,0));
    portable_script_host_free(host);
}

static void test_art_motion_owner(const PortableRecoveredRaceView *base)
{
    static PortableGpRaceSession session;
    PortableRecoveredRaceView race=*base;
    PortableDriverArtRules rules={0};
    for(unsigned i=0u;i<16u;++i){
        rules.speed_spans[i]=(uint16_t)(4u+i%3u);
        rules.refresh_masks[i]=7u;
    }
    race.dependencies.art_motion_rules=&rules;
    const PortableSkyMotionRules sky_rules={.controller=PORTABLE_SKY_MOTION_CLOCK_MID,
        .yaw_shift={6u,6u,7u},.layer_mask=7u,.x_bias={0,3,-9},
        .y_bias={40,41,42},.split_y=-1,.split_layer=0u,.layer_source={0u,1u,2u}};
    race.sky_motion_rules=&sky_rules;
    const uint8_t ambient_data[]={31u,0u,0u,124u};
    const PortableAmbientPatch ambient_patches[]={
        {.target=PORTABLE_AMBIENT_OBJECT,.offset=19u,.count=1u,.data_offset=0u},
        {.target=PORTABLE_AMBIENT_OBJECT,.offset=19u,.count=1u,.data_offset=2u}};
    const PortableAmbientFrame ambient_frames[]={
        {.duration=1u,.patch_count=1u,.first_patch=0u},
        {.duration=1u,.patch_count=1u,.first_patch=1u}};
    const PortableAmbientLane ambient_lane={.stage=PORTABLE_AMBIENT_POST_DRIVER,
        .write_policy=PORTABLE_AMBIENT_ON_ENTRY,.controller=PORTABLE_AMBIENT_TIMER,
        .frame_count=2u,.loop_frame=0u};
    const PortableAmbientRules ambient_rules={.lanes=&ambient_lane,.frames=ambient_frames,
        .patches=ambient_patches,.data=ambient_data,.lane_count=1u,.frame_count=2u,
        .patch_count=2u,.data_size=sizeof ambient_data};
    PortableAmbientSeed ambient_seed={0};ambient_seed.sky[12]=0x1234u;ambient_seed.object[19]=100u;
    race.ambient_rules=&ambient_rules;race.ambient_seed=&ambient_seed;
    const PortableWeatherAssetView weather={.rules={.enabled=1u,.count=12u}};
    race.weather_profile=&weather;
    PortableCourseProp props[3];
    for(unsigned i=0u;i<3u;++i)
        CHECK(portable_course_prop_init(&props[i],(800+(int)i*8)*65536,800*65536,0xa30000u,256u,0u));
    race.course_props=props;race.course_prop_count=3u;
    CHECK(test_gp_init(&session,&race,7u)==PORTABLE_GP_RACE_SESSION_OK);
    CHECK(session.snow_weather.enabled && session.main_visual_rng_state==UINT32_C(2802067423) &&
        session.course_props.actors[0].pier_boo.phase==0u &&
        session.course_props.actors[1].pier_boo.phase==0x5000u &&
        session.course_props.actors[2].pier_boo.phase==0x2400u);
    uint32_t weather_rng=UINT32_C(2802067423);

    CHECK(session.ambient.snapshot.initialized && !session.ambient.post_serial &&
        session.ambient.snapshot.sky[12]==0x1234u && session.ambient.snapshot.object[19]==100u);
    CHECK(session.runtime.session.phase==PORTABLE_RACE_PHASE_COUNTDOWN &&
        session.player_dependencies.art_motion_rules==&rules);
    PortableRaceTickDisposition disposition;
    for(unsigned tick=0u;tick<2u;++tick){
        const PortableDriverArtMotion prior=session.player.art_motion;
        CHECK(portable_gp_race_session_tick(&session,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        for(unsigned draw=0u;draw<4u;++draw)(void)portable_gp_item_random(&weather_rng);
        CHECK(session.main_visual_rng_state==weather_rng &&
            session.snow_weather.particles[tick].active==1u);
        CHECK(session.ambient.post_serial==tick+1u && session.ambient.snapshot.object_dirty[19] &&
            session.ambient.snapshot.object[19]==(tick?0x7c00u:31u) &&
            session.ambient.snapshot.sky[12]==0x1234u);
        CHECK(session.player.art_motion.initialized &&
            session.player.art_motion.wheel_phase==0u &&
            session.player.art_motion.bob_phase==1u &&
            session.player.art_motion.countdown==1u-tick);
        CHECK(session.player.art_motion.cached_bob_offset==
            portable_driver_art_motion_bob_offset(&prior,session.player.motion.speed_q16,
                session.player.steering.state_flags,session.player.forward_acceleration.state_flags,
                session.player.source_material_85,session.source_frame_clock));
        for(unsigned i=1u;i<8u;++i)
            CHECK(session.roster.racers[i].art_motion_rules==&rules &&
                session.roster.racers[i].art_motion.initialized &&
                session.roster.racers[i].art_motion.wheel_phase==0u &&
                session.roster.racers[i].art_motion.bob_phase==1u &&
                session.roster.racers[i].art_motion.countdown==1u-tick);
    }
    CHECK(session.runtime.session.phase==PORTABLE_RACE_PHASE_RACING);
    /* Each source character gets one refresh in eight racing turns, even
     * when that character occupies a different grid seat. Wheel timers
     * advance once on every turn, independently of that cell cadence. */
    unsigned refreshes[8]={0};
    CHECK(session.roster.racers[1].driver_class!=1u);
    for(unsigned turn=0u;turn<8u;++turn){
        session.player.art_motion.countdown=1u;
        for(unsigned i=1u;i<8u;++i){
            session.roster.racers[i].art_motion.countdown=1u;
            session.roster.racers[i].art_motion.cached_bob_offset=7;
        }
        CHECK(portable_gp_race_session_tick(&session,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(session.player.art_motion.countdown==0u &&
            session.player.art_motion.wheel_phase==0u &&
            session.player.art_motion.bob_phase==1u);
        for(unsigned i=1u;i<8u;++i){
            const PortableGpRacer *racer=&session.roster.racers[i];
            CHECK(racer->art_motion.countdown==0u &&
                racer->art_motion.wheel_phase==0u && racer->art_motion.bob_phase==1u);
            const int due=(session.source_frame_clock&7u)==(racer->driver_class&7u);
            if(due){
                ++refreshes[i];
                CHECK(racer->art_motion.cached_bob_offset==
                    portable_driver_art_motion_bob_offset(&racer->art_motion,
                        racer->speed_q16,racer->source_flags,
                        racer->mushroom_response_flags_b4,racer->source_material_85,
                        session.source_frame_clock));
            }else CHECK(racer->art_motion.cached_bob_offset==7);
        }
    }
    for(unsigned i=1u;i<8u;++i)CHECK(refreshes[i]==1u);
    PortableGpRaceSnapshot snapshot;
    PortableRacePresentationState presentation;
    CHECK(portable_gp_race_session_snapshot(&session,&snapshot)==PORTABLE_GP_RACE_SESSION_OK);
    CHECK(snapshot.sky_motion.initialized && session.sky_motion_rules==&sky_rules);
    const PortableAmbientSnapshot published_ambient=snapshot.ambient;
    const PortableSnowWeatherSnapshot published_weather=snapshot.snow_weather;
    CHECK(snapshot.snow_weather_digest==portable_snow_weather_digest(&session.snow_weather) &&
        snapshot.main_visual_rng_state==session.main_visual_rng_state);
    CHECK(snapshot.ambient_digest==portable_ambient_digest(&session.ambient));
    session.ambient.snapshot.object[19]=987u;
    const PortableSkyMotionSnapshot published_sky=snapshot.sky_motion;
    session.sky_motion.snapshot.scroll_x[0]=1234;
    const PortableDriverArtMotion published=snapshot.player_art_motion;
    const PortableDriverArtMotion published_cpu=snapshot.opponents[0].art_motion;
    session.player.art_motion.cached_bob_offset=6;
    session.roster.racers[1].art_motion.cached_bob_offset=5;
    CHECK(portable_gp_race_presentation_build(&session,&snapshot,&presentation));
    CHECK(memcmp(&published_ambient,&presentation.ambient,sizeof published_ambient)==0 &&
        presentation.ambient_rules==&ambient_rules && session.ambient.snapshot.object[19]==987u &&
        memcmp(&published_weather,&presentation.snow_weather,sizeof published_weather)==0 &&
        memcmp(&published_sky,&presentation.sky_motion,sizeof published_sky)==0 &&
        session.sky_motion.snapshot.scroll_x[0]==1234 &&
        memcmp(&published,&presentation.art_motion,sizeof published)==0 &&
        memcmp(&published_cpu,&presentation.opponents[0].art_motion,sizeof published_cpu)==0 &&
        session.player.art_motion.cached_bob_offset==6 &&
        session.roster.racers[1].art_motion.cached_bob_offset==5);
    /* The finish HUD freezes the recorded final lap, rather than showing the
     * still-advancing result clock while waiting for the lap board. */
    PortableGpRaceSnapshot finished=snapshot;
    finished.results_ready=finished.source_result_times_ready=1u;
    finished.lap_count=3u;finished.source_clock_centiseconds=9001u;
    finished.source_result_times_centiseconds[2]=2819u;
    CHECK(portable_gp_race_presentation_build(&session,&finished,&presentation) &&
        presentation.result_clock_mode==2u && presentation.result_lap_centiseconds==2819u);
    /* Three local pilots share the world turn without multiplying their
     * independent wheel advances or dropping state on local selection. */
    PortableGpGridRow grid[8];
    for(unsigned i=0u;i<8u;++i)
        grid[i]=(PortableGpGridRow){(872+(int32_t)(i&1u)*32)*65536,
            (612+(int32_t)i*12)*65536,0u};
    race.gp_grid=grid;race.gp_grid_count=8u;
    const PortableRecoveredRaceView *const races[3]={&race,&race,&race};
    const uint8_t classes[3]={7u,7u,7u};
    const uint16_t buttons[3]={0u,0u,0u};
    CHECK(test_gp_init_locals(&session,races,classes,3u,0u,
        (PortableQuickRunOptions){0})==PORTABLE_GP_RACE_SESSION_OK);
    CHECK(session.main_visual_rng_state==UINT32_C(2802067423));
    weather_rng=UINT32_C(2802067423);
    for(unsigned tick=0u;tick<2u;++tick){
        CHECK(portable_gp_race_session_tick_locals(&session,buttons,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        for(unsigned draw=0u;draw<4u;++draw)(void)portable_gp_item_random(&weather_rng);
        CHECK(session.main_visual_rng_state==weather_rng);
        CHECK(session.local_index==0u);
        for(uint8_t local=0u;local<3u;++local){
            CHECK(portable_gp_race_session_select_local(&session,local)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(session.ambient.post_serial==tick+1u &&
                session.ambient.snapshot.object[19]==(tick?0x7c00u:31u));
            CHECK(session.sky_motion.snapshot.initialized &&
                session.sky_motion.snapshot.scroll_x[1]==3+(int)(session.source_frame_clock>>4));
            CHECK(session.player.art_motion.initialized &&
                session.player.art_motion.wheel_phase==0u &&
                session.player.art_motion.bob_phase==1u &&
                session.player.art_motion.countdown==1u-tick);
        }
        CHECK(portable_gp_race_session_select_local(&session,0u)==PORTABLE_GP_RACE_SESSION_OK);
    }
}

static void test_character_view_owner(const PortableRecoveredRaceView *base)
{
    PortableRecoveredRaceView race=*base;
    int32_t sine[1024];
    for(unsigned i=0u;i<1024u;++i)
        sine[i]=(int32_t)lround(sin((double)i*6.283185307179586/1024.0)*32768.0);
    race.dependencies.sine_table=sine;
    PortableGpRaceSession session;
    PortableGpRaceSnapshot snapshot;
    PortableRacePresentationState presentation;
    PortableRaceTickDisposition disposition;
    CHECK(test_gp_init(&session,&race,7u)==PORTABLE_GP_RACE_SESSION_OK);
    session.roster.racers[0].contact_mode_6e=0x18u;
    session.roster.racers[0].contact_phase_6f=8u;
    session.roster.racers[0].contact_frame_6c=25u;
    session.roster.racers[0].contact_flags_6d=1u;
    session.roster.racers[0].contact_height_24=256u;
    CHECK(portable_gp_race_session_tick(&session,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
    CHECK(session.roster.racers[0].contact_phase_6f==6u);
    CHECK(session.roster.racers[0].contact_height_24!=UINT16_MAX);
    CHECK(portable_gp_race_session_snapshot(&session,&snapshot)==PORTABLE_GP_RACE_SESSION_OK);
    CHECK(snapshot.player_art_pose.frame_override==25u && snapshot.player_art_pose.mirror==1u);
    CHECK(snapshot.camera_yaw==portable_character_camera_yaw(&session.character_camera));
    /* CPU and other-local art uses source56 from the immutable snapshot;
     * changing live roster state cannot change a published frame. */
    snapshot.opponents[0].source_angle=(uint16_t)(snapshot.camera_yaw-37u*512u);
    snapshot.opponents[0].steering_response=0;
    snapshot.opponents[0].contact_mode=0;
    session.roster.racers[1].source_heading=999u;
    session.roster.racers[0].contact_frame_6c=27u;
    CHECK(portable_gp_race_presentation_build(&session,&snapshot,&presentation));
    CHECK(presentation.art_pose_valid && presentation.art_pose.frame_override==25u &&
        presentation.opponents[0].art_pose.angle==37u &&
        presentation.opponents[0].art_pose.mirror==0u);
    const uint8_t phase=session.roster.racers[0].contact_phase_6f;
    const PortableCharacterCameraState camera=session.character_camera;
    CHECK(portable_gp_race_presentation_build(&session,&snapshot,&presentation));
    CHECK(session.roster.racers[0].contact_phase_6f==phase &&
        memcmp(&camera,&session.character_camera,sizeof camera)==0);
}

static int run_tests(void)
{
    test_passive_course_props();
    CHECK(test_spatial_pitch()==0);
    PortableRacePoint route[47] = {0};
    uint8_t route_modes[47] = {0};
    uint8_t raster_cells[128u * 128u];
    uint8_t terrain_cells[256u * 256u] = {0};
    uint8_t material_flags[256] = {0x40};
    int16_t auto_acceleration[64] = {0};
    int16_t brake[8] = {0};
    int16_t forward_response[8] = {0};
    int16_t forward_local[16];
    int16_t steering_delta[10] = {0};
    int16_t steering_response[16] = {0};
    int32_t sine[1024];
    uint16_t track_angles[0x4000]={0};
    PortableTrackRaster raster = {
        raster_cells, sizeof raster_cells, 128u, 128u, 0u, 0u
    };
    PortableTerrainView terrain = {
        terrain_cells, 256u, 256u, 256u
    };
    PortableTrackMaterialView material = {
        .flags = material_flags,
        .count = 256u,
    };
    PortableCharacterViewRules fixture_view={0};
    for(unsigned i=0u;i<15u;++i){
        fixture_view.response_limit[i]=(uint8_t)(3u+i%4u);
        fixture_view.speed_limit[i]=(uint8_t)(4u+i%3u);
    }
    PortableGpRouletteRules fixture_roulette=test_roulette_rules();
    PortableGpPlayerItemRules fixture_player_items={0};
    PortableGpCpuItemRules fixture_cpu_items[3]={0};
    for(unsigned row=0u;row<11u;++row){
        fixture_player_items.table[row][0]=(PortableGpItemWeight){0x1007u,1u};
        fixture_player_items.table[row][1]=(PortableGpItemWeight){0u,255u};
    }
    for(unsigned engine=0u;engine<3u;++engine)for(unsigned variant=0u;variant<3u;++variant)
        for(unsigned driver=0u;driver<8u;++driver)for(unsigned rank=0u;rank<8u;++rank)
            fixture_cpu_items[engine].item[variant][driver][rank]=0x1000u;
    PortableRecoveredRaceView race = {.view_rules=&fixture_view,
        .hazard_sound_rules=test_hazard_rules(),.roulette_rules=&fixture_roulette,
        .gp_player_items=&fixture_player_items,.gp_cpu_items=fixture_cpu_items,
        .gp_cpu_item_engine_count=3u};
    PortableGpRaceSession session;
    PortableGpRaceSession repeat;
    PortableGpRaceSession item_session;
    PortableGpRaceSession water_session;
    PortableGpRaceSnapshot snapshot;
    PortableGpRaceSnapshot repeated_snapshot;
    PortableRacePresentationState presentation_state;
    PortableRecoveredCourseObject item_box;
    PortableRaceTickDisposition disposition;
    int32_t initial_player_y;
    int32_t initial_opponent_y;

    for (size_t index = 0u; index < 47u; ++index)
        route[index] = (PortableRacePoint){880, 640};
    route[44] = (PortableRacePoint){908, 794};
    route[45] = (PortableRacePoint){880, 640};
    route[46] = (PortableRacePoint){888, 552};
    memset(raster_cells, 45, sizeof raster_cells);
    raster_cells[(size_t)(788 / 16) * 128u + (size_t)(908 / 16)] = 44u;
    for (size_t index = 0u; index < 16u; ++index)
        forward_local[index] = 64;
    for (size_t index = 0u; index < 1024u; ++index)
        sine[index] = (int32_t)lround(
            sin((double)index * 6.283185307179586 / 1024.0) * 65536.0);

    race.rules = (PortableRaceRulesView){
        .route = route,
        .route_count = 47u,
        .ai_modes = route_modes,
        .ai_mode_count = 47u,
        .start_x_q16 = INT32_C(888) << 16u,
        .start_y_q16 = INT32_C(612) << 16u,
        .start_heading = 0u,
        .start_route_index = 44u,
        .start_checkpoint_raw = 44u,
        .progress_kind = PORTABLE_RACE_PROGRESS_SOURCE_RASTER_V1,
        .finish_kind = PORTABLE_FINISH_SOURCE_Y_CROSSING,
        .finish_y_q16 = INT32_C(0x03100000),
        .lap_count = 3u,
        .countdown_ticks = 2u,
        .rescue_penalty_ticks = 180u,
        .compatibility = {
            .course_id = 24u,
            .character_class = 7u,
            .handling_profile_id = PORTABLE_HANDLING_RECOVERED_V1,
        },
    };
    race.dependencies = (PortableDriverPilotDependencies){
        .response_rules=portable_test_response_profile(),
        .auto_acceleration_table = auto_acceleration,
        .auto_acceleration_count = 64u,
        .brake_table = brake,
        .brake_table_count = 8u,
        .forward_response_curve = forward_response,
        .forward_response_count = 8u,
        .forward_local_table = forward_local,
        .forward_local_count = 16u,
        .steering_delta_table = steering_delta,
        .steering_delta_count = 10u,
        .steering_response_data = steering_response,
        .steering_response_data_count = 16u,
        .sine_table = sine,
        .sine_count = 1024u,
        .raster = &raster,
        .track_material = &material,
        .terrain = &terrain,
        .source_track_angle_lut = track_angles,
        .source_track_angle_lut_count = 0x4000u,
    };
    PortableGpEngineProfile fixture_engines[3]={0};
    int16_t fixture_target_response[8];
    for(unsigned i=0u;i<8u;++i)fixture_target_response[i]=(int16_t)(-3+(int)i*3);
    for(unsigned engine=0u;engine<3u;++engine){
        fixture_engines[engine].cpu=*test_cpu_tuning();
        fixture_engines[engine].player_base_speed_q16=(int32_t)(600u+engine*80u)*65536;
        for(unsigned i=0u;i<16u;++i)fixture_engines[engine].player_forward_limits[i]=(int16_t)(64u+24u*i);
    }
    race.gp_engines=fixture_engines;race.gp_engine_count=3u;
    race.dependencies.forward_target_response_table=fixture_target_response;
    race.dependencies.forward_target_response_count=8u;
    test_roster_rules_attach(&race.rules);
    race.dependencies.source_auto_deceleration_table=race.rules.source_auto_deceleration_table;
    race.dependencies.source_auto_deceleration_count=race.rules.source_auto_deceleration_count;
    test_item_dependencies(&race.dependencies);
    race.rules.item_strength_threshold=8u;
    race.gp_forward_local_table = forward_local;
    race.gp_forward_local_count = 16u;
    race.initial_driver.pose.position_x = race.rules.start_x_q16;
    race.initial_driver.pose.position_y = race.rules.start_y_q16;
    race.initial_driver.auto_acceleration.character_class = 7u;
    race.initial_driver.input.speed_parameter = 5u;

    {
        PortableGpRaceSessionStatus init_status =
            test_gp_init(&session, &race, 7u);
        if (init_status != PORTABLE_GP_RACE_SESSION_OK)
            fprintf(stderr, "initial GP session status=%u\n",
                    (unsigned)init_status);
        CHECK(init_status == PORTABLE_GP_RACE_SESSION_OK);
    }
    test_script_hooks(&race);
    test_local_players(&race);
    test_surface_trail_session(&race);
    test_impact_session(&race);
    test_owned_hud_session(&race);
    test_finish_lakitu_owner(&race);
    test_modern_local_owners(&race);
    test_modern_air_item_drag(&race);
    test_general_kart_contacts(&race);
    test_size_contact_sessions(&race);
    test_art_motion_owner(&race);
    test_character_view_owner(&race);
    {
        PortableGpScoringRules scoring={.finish_points={9u,6u,3u,1u,0u,0u,0u,0u}};
        CHECK(session.retry_credits==3u);
        PortableMushroomRules mushrooms={3u,2u,4u,7u,1000u*65536u};
        race.dependencies.mushroom_rules=&mushrooms;
        race.gp_scoring=&scoring;
        for(uint8_t credits=0u;credits<=3u;++credits){
            scoring.initial_retry_credits=credits;
            CHECK(test_gp_init(&repeat,&race,7u)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(repeat.retry_credits==credits);
            CHECK(repeat.player_dependencies.mushroom_rules==&mushrooms);
            for(unsigned racer=0u;racer<8u;++racer)CHECK(repeat.roster.racers[racer].mushroom_rules==&mushrooms);
        }
        scoring.initial_retry_credits=4u;
        CHECK(test_gp_init(&repeat,&race,7u)==PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT);
        race.gp_scoring=NULL;
        race.dependencies.mushroom_rules=test_mushroom_rules();
    }
    {
        PortableCrab initial_crab;
        CHECK(portable_crab_init(&initial_crab,400*65536,500*65536,2u,1u));
        PortableLavaBubble initial_bubble;
        PortableThwomp initial_thwomp;
        PortableCourseProp initial_prop;
        CHECK(portable_course_prop_init(&initial_prop,12*65536,20*65536,0x860000u,224u,0u));
        race.course_props=&initial_prop;race.course_prop_count=1u;
        CHECK(portable_lava_bubble_init(&initial_bubble,600*65536,500*65536,2u,1u));
        CHECK(portable_thwomp_init(&initial_thwomp,700*65536,500*65536,2u,1u));
        race.lava_bubbles=&initial_bubble;race.lava_bubble_count=1u;
        race.thwomps=&initial_thwomp;race.thwomp_count=1u;
        PortableSeagull initial_seagull;
        CHECK(portable_seagull_init(&initial_seagull,800*65536,500*65536,2u,0u));
        initial_seagull.state=16;
        race.seagulls=&initial_seagull;race.seagull_count=1u;
        PortablePiranha initial_piranha;
        CHECK(portable_piranha_init(&initial_piranha,900*65536,500*65536,2u,0u));
        initial_piranha.state=16;
        race.piranhas=&initial_piranha;race.piranha_count=1u;
        PortableFallingStar initial_star;
        CHECK(portable_falling_star_init(&initial_star,920*65536,700*65536,2u,0u));
        race.falling_stars=&initial_star;race.falling_star_count=1u;
        PortableThundercloud initial_cloud;
        CHECK(portable_thundercloud_init(&initial_cloud,920*65536,600*65536,2u,0u));
        race.thunderclouds=&initial_cloud;race.thundercloud_count=1u;
        PortableMouse initial_mouse;
        CHECK(portable_mouse_init(&initial_mouse,920*65536,500*65536,2u,0u));
        race.mice=&initial_mouse;race.mouse_count=1u;
        PortableSnowman initial_snowman;
        CHECK(portable_snowman_init(&initial_snowman,950*65536,500*65536,2u,0u));
        initial_snowman.state=32;
        race.snowmen=&initial_snowman;race.snowman_count=1u;
        PortablePenguin initial_penguin;
        CHECK(portable_penguin_init(&initial_penguin,980*65536,500*65536,2u,1u));
        initial_penguin.timer=6;
        race.penguins=&initial_penguin;race.penguin_count=1u;
        PortableBoo initial_boo;
        CHECK(portable_boo_init(&initial_boo,980*65536,500*65536,2u,1u));
        race.boos=&initial_boo;race.boo_count=1u;
        PortableCannonball initial_cannonball;
        CHECK(portable_cannonball_init(&initial_cannonball,800*65536,500*65536,2u,0u));
        race.cannonballs=&initial_cannonball;race.cannonball_count=1u;
        race.crabs=&initial_crab;race.crab_count=1u;
        CHECK(test_gp_init(&repeat,&race,7u)==PORTABLE_GP_RACE_SESSION_OK);
        race.course_props=NULL;race.course_prop_count=0u;
        repeat.course_props.actors[0].contact_phase=1u;
        portable_course_prop_damage(&repeat.course_props.actors[0]);
        race.crabs=NULL;race.crab_count=0u;
        race.seagulls=NULL;race.seagull_count=0u;
        race.cannonballs=NULL;race.cannonball_count=0u;
        race.piranhas=NULL;race.piranha_count=0u;
        race.mice=NULL;race.mouse_count=0u;
        race.thunderclouds=NULL;race.thundercloud_count=0u;
        race.falling_stars=NULL;race.falling_star_count=0u;
        race.snowmen=NULL;race.snowman_count=0u;
        race.penguins=NULL;race.penguin_count=0u;
        race.boos=NULL;race.boo_count=0u;
        race.lava_bubbles=NULL;race.lava_bubble_count=0u;
        race.thwomps=NULL;race.thwomp_count=0u;
        CHECK(portable_gp_race_session_tick(&repeat,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(repeat.crabs.count==1u && repeat.crabs.actors[0].timer==1 &&
              repeat.crabs.actors[0].y_q16==500*65536+0x8000 && initial_crab.timer==0);
        PortableGpRaceSnapshot actor_snapshot;
        PortableRacePresentationState actor_presentation;
        CHECK(portable_gp_race_session_snapshot(&repeat,&actor_snapshot)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(portable_gp_race_presentation_build(&repeat,&actor_snapshot,&actor_presentation));
        CHECK(actor_snapshot.course_props.count==1u && actor_snapshot.course_props.actors[0].timer==31);
        CHECK(actor_presentation.course_props.actors[0].affine_y_q8==-256 &&
              actor_presentation.course_props.actors[0].timer==31 &&
              actor_presentation.course_props.actors[0].contact_phase==1u && !initial_prop.state);
#define PUBLISHED(family) \
        CHECK(actor_presentation.family.count==1u && \
            memcmp(&actor_presentation.family.actors[0],&repeat.family.actors[0], \
                sizeof repeat.family.actors[0])==0)
        PUBLISHED(lava_bubbles);PUBLISHED(thwomps);PUBLISHED(crabs);
        PUBLISHED(seagulls);PUBLISHED(piranhas);PUBLISHED(snowmen);PUBLISHED(mice);PUBLISHED(thunderclouds);PUBLISHED(falling_stars);
        PUBLISHED(penguins);PUBLISHED(boos);PUBLISHED(cannonballs);
#undef PUBLISHED
        CHECK(actor_snapshot.thunderclouds.count==1u && actor_snapshot.thunderclouds.actors[0].timer==31);
        ++repeat.thunderclouds.actors[0].timer;
        CHECK(portable_gp_race_presentation_build(&repeat,&actor_snapshot,&actor_presentation));
        CHECK(actor_presentation.thunderclouds.actors[0].timer==31);
        --repeat.thunderclouds.actors[0].timer;
        CHECK(actor_snapshot.mice.count==1u && actor_snapshot.mice.actors[0].timer==1);
        ++repeat.mice.actors[0].timer;
        CHECK(portable_gp_race_presentation_build(&repeat,&actor_snapshot,&actor_presentation));
        CHECK(actor_presentation.mice.actors[0].timer==1 && actor_snapshot.mice.actors[0].timer==1);
        --repeat.mice.actors[0].timer;
        /* Published state survives later simulation changes. */
        ++repeat.crabs.actors[0].timer;
        CHECK(actor_presentation.crabs.actors[0].timer==1);
        --repeat.crabs.actors[0].timer;
        CHECK(repeat.lava_bubbles.actors[0].timer==99 && initial_bubble.timer==100);
        CHECK(repeat.thwomps.actors[0].timer==32 && initial_thwomp.timer==33);
        CHECK(repeat.seagulls.actors[0].state==17 && repeat.seagulls.actors[0].timer==1 &&
              initial_seagull.state==16 && initial_seagull.timer==0);
        CHECK(repeat.piranhas.actors[0].state==17 && repeat.piranhas.actors[0].timer==3 &&
              repeat.piranhas.actors[0].flags==1u && initial_piranha.state==16);
        CHECK(repeat.snowmen.actors[0].state==33 && repeat.snowmen.actors[0].timer==0 &&
              repeat.snowmen.actors[0].flags==3u && initial_snowman.state==32);
        CHECK(repeat.penguins.actors[0].timer==7 && repeat.penguins.actors[0].art==1u &&
              repeat.penguins.actors[0].flags==3u && initial_penguin.timer==6);
        CHECK(repeat.boos.actors[0].phase==1024u && repeat.boos.actors[0].state==17u &&
              repeat.boos.actors[0].height_q16==0x80000 && initial_boo.phase==0u);
        CHECK(repeat.cannonballs.actors[0].state==3 && repeat.cannonballs.actors[0].height_q16==0x27f800 &&
              repeat.cannonballs.actors[0].affine_x_q8==1000u && initial_cannonball.state==1);
        CHECK(portable_gp_race_session_snapshot(&repeat,&repeated_snapshot)==
              PORTABLE_GP_RACE_SESSION_OK);
        CHECK(repeated_snapshot.cannonball_sounds.serial==1u &&
              repeated_snapshot.cannonball_sounds.count==1u &&
              repeated_snapshot.cannonball_sounds.events[0].song_id==382u);
        repeat.thwomps.actors[0].state=5u;
        repeat.thwomps.actors[0].timer=1;
        repeat.thwomps.actors[0].x_q16=repeat.player.pose.position_x+100*65536;
        repeat.thwomps.actors[0].y_q16=repeat.player.pose.position_y;
        CHECK(portable_gp_race_session_tick(&repeat,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(repeat.thwomp_sounds.count==1u && repeat.thwomp_sounds.volumes[0]==247u);
        CHECK(portable_gp_race_session_snapshot(&repeat,&repeated_snapshot)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(repeated_snapshot.thwomp_sounds.count==1u && repeated_snapshot.thwomp_sounds.volumes[0]==247u);
        CHECK(portable_gp_race_session_tick(&repeat,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(repeat.thwomp_sounds.count==0u);
        repeat.crabs.actors[0].timer=380;
        CHECK(portable_gp_race_session_tick(&repeat,0u,&disposition)!=PORTABLE_GP_RACE_SESSION_OK);
    }
    CHECK(session.runtime.recovered_driver == &session.player);
    CHECK(session.player_dependencies.forward_limit_thresholds != NULL &&
          session.player_dependencies.forward_limit_threshold_count == 16u &&
          session.player_dependencies.forward_limit_thresholds[9] == fixture_engines[0].player_forward_limits[9] &&
          session.player_dependencies.forward_limit_thresholds[13] == fixture_engines[0].player_forward_limits[13]);
    CHECK(session.player.preserve_source_base_speed == 1u &&
          session.player.input.base_speed_q16 == fixture_engines[0].player_base_speed_q16 &&
          session.player_dependencies.forward_target_response_count == 8u &&
          session.player_dependencies.forward_target_response_table[0] == fixture_target_response[0]);
    CHECK(session.player.pose.position_x == INT32_C(0x038c0000) &&
          session.player.pose.position_y == INT32_C(0x03140000));
    CHECK(session.roster.racers[0].driver_class == 7u &&
          session.roster.racers[1].driver_class == test_roster_rules()->rival_classes[7][6] &&
          session.roster.racers[7].driver_class == test_roster_rules()->rival_classes[7][0]);
    CHECK(session.roster.race_mode_flags_7da == UINT16_C(0x0100));
    for(size_t racer=0u;racer<PORTABLE_GP_RACER_COUNT;++racer)
        CHECK(session.roster.racers[racer].coin_count_178==5u);
    CHECK(session.roster.racers[0].route_index == 44u &&
          session.roster.racers[0].route_wrap_91 == 127u);
    {
        PortableGpGridRow source_rows[8];
        PortableRecoveredRaceView next_race = race;
        PortableGpRaceSession *next_session = malloc(sizeof *next_session);
        const uint8_t rank_by_racer[8] = {0u,7u,6u,5u,4u,3u,2u,1u};
        const uint8_t invalid_ranks[8] = {0u,0u,6u,5u,4u,3u,2u,1u};
        CHECK(next_session != NULL);
        for (size_t rank = 0u; rank < 8u; ++rank)
            source_rows[rank] = (PortableGpGridRow){
                (int32_t)((rank & 1u ? 908u : 868u) << 16u),
                (int32_t)((620u + (uint32_t)rank * 24u) << 16u), 0u};
        next_race.gp_grid = source_rows;
        next_race.gp_grid_count = 8u;
        CHECK(test_gp_init_grid(
                  next_session, &next_race, 7u, rank_by_racer) ==
              PORTABLE_GP_RACE_SESSION_OK);
        CHECK(next_session->player.pose.position_x == source_rows[0].x_q16 &&
              next_session->player.pose.position_y == source_rows[0].y_q16 &&
              next_session->roster.racers[0].grid_slot == 0u &&
              next_session->roster.standings[0] == 0u);
        CHECK(next_session->rival_coin_count_handoff==2u);
        for(size_t racer=0u;racer<PORTABLE_GP_RACER_COUNT;++racer)
            CHECK(next_session->roster.racers[racer].coin_count_178==2u);
        for (size_t index = 1u; index < 8u; ++index)
            CHECK(next_session->roster.racers[index].position_x_q16 ==
                      source_rows[rank_by_racer[index]].x_q16 &&
                  next_session->roster.racers[index].position_y_q16 ==
                      source_rows[rank_by_racer[index]].y_q16 &&
                  next_session->roster.standings[rank_by_racer[index]] ==
                      index);
        CHECK(test_gp_init_grid(
                  next_session, &next_race, 7u, invalid_ranks) ==
              PORTABLE_GP_RACE_SESSION_ROSTER_ERROR);
        next_race.gp_grid_count = 7u;
        CHECK(test_gp_init_grid(
                  next_session, &next_race, 7u, rank_by_racer) ==
              PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT);
        free(next_session);
    }
    {
        PortableGpRaceSession launch_handoff;
        CHECK(test_gp_init(&launch_handoff,&race,7u) ==
              PORTABLE_GP_RACE_SESSION_OK);
        CHECK(portable_gp_race_session_handoff_launch_rng(NULL,103u) ==
              PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT);
        CHECK(portable_gp_race_session_handoff_launch_rng(
                  &launch_handoff,103u) == PORTABLE_GP_RACE_SESSION_OK &&
              launch_handoff.source_rng_state == UINT32_C(0x00797a9b) &&
              launch_handoff.runtime.session.elapsed_ticks == 0u &&
              launch_handoff.roster.racers[0].route_index == 44u);
    }
    {
        PortableGpRaceSession prelude;
        PortableRecoveredRaceView launch_race = race;
        const PortableSurfaceTrailRules trail_rules=surface_trail_fixture();
        launch_race.surface_trail_rules=&trail_rules;
        int32_t trail_sine[1024];
        for(unsigned i=0u;i<1024u;++i)trail_sine[i]=(int32_t)lround(sin((double)i*6.283185307179586/1024.0)*32768.0);
        launch_race.dependencies.sine_table=trail_sine;launch_race.dependencies.sine_count=1024u;
        uint16_t inputs[PORTABLE_GP_LAUNCH_PRELUDE_TICKS];
        launch_race.rules.countdown_ticks = 180u;
        for(size_t index=0u;index<PORTABLE_GP_LAUNCH_PRELUDE_TICKS;++index)
            inputs[index]=PORTABLE_INPUT_ACCELERATE;
        CHECK(test_gp_init(&prelude,&launch_race,7u)==
              PORTABLE_GP_RACE_SESSION_OK);
        const uint32_t clock=prelude.source_frame_clock;
        CHECK(portable_gp_race_session_handoff_launch_inputs(&prelude,
                  inputs,19u)==PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT);
        CHECK(portable_gp_race_session_handoff_launch_inputs(&prelude,
                  NULL,20u)==PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT);
        CHECK(portable_gp_race_session_handoff_launch_inputs(&prelude,
                  inputs,20u)==PORTABLE_GP_RACE_SESSION_OK &&
              prelude.launch_revs_92==640u && prelude.launch_rev_mode_98==0u &&
              prelude.player.steering.response_flags==0u &&
              prelude.surface_trails.eligible_counter==0u && prelude.surface_trails.side==0u &&
              prelude.source_frame_clock==clock && prelude.source_rng_state==0u &&
              prelude.runtime.session.countdown_remaining==180u &&
              prelude.runtime.session.elapsed_ticks==0u);
        CHECK(portable_gp_race_session_handoff_launch_inputs(&prelude,
                  inputs,20u)==PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR);
        for(size_t tick=0u;tick<27u;++tick){
            CHECK(portable_gp_race_session_tick(&prelude,
                      PORTABLE_INPUT_ACCELERATE,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            if(tick==25u)CHECK(prelude.player.steering.response_flags==0u&&
                              prelude.surface_trails.eligible_counter==0u);
        }
        CHECK(prelude.player.steering.response_flags==1u &&
              prelude.surface_trails.eligible_counter==1u && prelude.surface_trails.side==0u);

    }
    {
        /* 40DD8/A9AC start boost: A held for the last 20 lights ticks lands
         * in the rev window; none, or held through every light, does not. */
        PortableRecoveredRaceView launch_race = race;
        launch_race.rules.countdown_ticks = 180u;
        static PortableGpRaceSession start;
        static const unsigned holds[3] = {20u, 0u, 180u};
        for (unsigned variant = 0u; variant < 3u; ++variant) {
            CHECK(test_gp_init(&start, &launch_race, 7u) == PORTABLE_GP_RACE_SESSION_OK);
            unsigned guard = 0u;
            while (start.runtime.session.phase == PORTABLE_RACE_PHASE_COUNTDOWN && ++guard < 400u)
                CHECK(portable_gp_race_session_tick(&start,
                          start.runtime.session.countdown_remaining <= holds[variant]
                              ? PORTABLE_INPUT_ACCELERATE : 0u,
                          &disposition) == PORTABLE_GP_RACE_SESSION_OK);
            CHECK(guard < 400u);
            for (unsigned tick = 0u; tick < 2u; ++tick)
                CHECK(portable_gp_race_session_tick(&start, PORTABLE_INPUT_ACCELERATE,
                                                    &disposition) == PORTABLE_GP_RACE_SESSION_OK);
            const int boosted = variant == 0u;
            CHECK(start.player.start_boosts == (boosted ? 1u : 0u));
            CHECK((start.player.steering.driver_mode == 8u) == boosted);
            CHECK((start.player.boost_particle_age != 0u) == boosted);
            CHECK((start.roster.racers[start.local_index].item_response_flags_b2 & 1u) == 0u);
            CHECK(((start.player.steering.response_flags & 1u) != 0u) == (variant == 2u));
        }
    }
    {
        PortableRecoveredRaceView launch_race = race;
        PortableGpRaceSession early;
        PortableGpRaceSession late;
        PortableGpRaceSession ready_clock;
        launch_race.rules.countdown_ticks = 180u;
        CHECK(test_gp_init(&early, &launch_race, 7u) ==
              PORTABLE_GP_RACE_SESSION_OK);
        CHECK(test_gp_init(&late, &launch_race, 7u) ==
              PORTABLE_GP_RACE_SESSION_OK);
        CHECK(test_gp_init(&ready_clock, &launch_race, 7u) ==
              PORTABLE_GP_RACE_SESSION_OK);
        CHECK(portable_gp_race_session_handoff_clock_boundary(
                  &ready_clock,
                  PORTABLE_GP_CLOCK_ENABLE_BEFORE_FINAL_COUNTDOWN) ==
              PORTABLE_GP_RACE_SESSION_OK &&
              ready_clock.source_frame_clock == 0u &&
              early.source_frame_clock == 6u);
        for (size_t tick = 0u; tick < 180u; ++tick)
        {
            CHECK(portable_gp_race_session_tick(
                      &early, PORTABLE_INPUT_ACCELERATE, &disposition) ==
                  PORTABLE_GP_RACE_SESSION_OK);
            CHECK(portable_gp_race_session_tick(
                      &late, tick >= 178u ? PORTABLE_INPUT_ACCELERATE :
                             PORTABLE_INPUT_NONE, &disposition) ==
                  PORTABLE_GP_RACE_SESSION_OK);
            CHECK(portable_gp_race_session_tick(
                      &ready_clock, PORTABLE_INPUT_NONE, &disposition) ==
                  PORTABLE_GP_RACE_SESSION_OK);
            if (tick == 178u)
                CHECK(ready_clock.source_result_clock.enabled == 1u &&
                      ready_clock.source_result_clock.centiseconds == 0u);
            if (tick == 179u)
                CHECK(ready_clock.source_result_clock.centiseconds == 1u &&
                      ready_clock.source_result_clock.phase == 1u &&
                      (ready_clock.source_frame_clock & 7u) == 4u &&
                      (early.source_frame_clock & 7u) == 2u);
            if (tick == 0u)
                CHECK(early.source_rng_state == UINT32_C(0x00003039) &&
                      late.source_rng_state == early.source_rng_state);
            if (tick == 1u)
                CHECK(early.source_rng_state == UINT32_C(0xd3dc167e) &&
                      late.source_rng_state == early.source_rng_state);
            if (tick == 45u)
                CHECK(early.launch_revs_92 == 0x05c0u &&
                      (early.player.steering.response_flags & 1u) == 0u);
            if (tick == 46u)
                CHECK(early.launch_revs_92 == 0x05e0u &&
                      (early.player.steering.response_flags & 1u) == 1u);
        }
        CHECK(early.launch_revs_92 == 0x0e00u &&
              early.player.steering.response_flags == 1u &&
              late.launch_revs_92 == 0x0040u &&
              late.player.steering.response_flags == 0u);
        CHECK(portable_gp_race_session_tick(
                  &early, PORTABLE_INPUT_ACCELERATE, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK);
        CHECK(portable_gp_race_session_tick(
                  &late, PORTABLE_INPUT_ACCELERATE, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK);
        CHECK(portable_gp_race_session_tick(
                  &ready_clock, PORTABLE_INPUT_ACCELERATE, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK);
        CHECK(ready_clock.source_result_clock.centiseconds == 3u &&
              ready_clock.source_result_clock.phase == 2u);
        CHECK(ready_clock.roster.racers[1].speed_q16 !=
              early.roster.racers[1].speed_q16);
        CHECK(early.player.motion.acceleration_q16 == 0x0000c000 &&
              early.player.steering.response_flags == 0x0021u &&
              late.player.steering.response_flags == 0u);
    }
    initial_player_y = session.player.pose.position_y;
    initial_opponent_y = session.roster.racers[1].position_y_q16;

    CHECK(portable_gp_race_session_tick(
              &session, PORTABLE_INPUT_ACCELERATE, &disposition) ==
          PORTABLE_GP_RACE_SESSION_OK &&
          disposition == PORTABLE_RACE_TICK_COUNTDOWN);
    CHECK(session.player.pose.position_y == initial_player_y &&
          session.roster.racers[1].position_y_q16 == initial_opponent_y &&
          session.roster.authoritative_ticks == 0u);
    CHECK(portable_gp_race_session_tick(
              &session, PORTABLE_INPUT_ACCELERATE, &disposition) ==
          PORTABLE_GP_RACE_SESSION_OK &&
          disposition == PORTABLE_RACE_TICK_COUNTDOWN);
    CHECK(session.runtime.session.phase == PORTABLE_RACE_PHASE_RACING &&
          session.roster.authoritative_ticks == 0u &&
          session.source_result_clock.centiseconds == 0u &&
          session.source_result_clock.enabled == 0u);
    CHECK(session.item_art_clock.calls==2u);

    /* Ordinary GP ticks use the same source course-contact families as
     * TT. Each fixture isolates one entry-pose contact before actor motion. */
    for(unsigned family=0u;family<14u;++family){
        PortableGpRaceSession hit=session;
        hit.runtime.recovered_driver=&hit.player;
        hit.runtime.recovered_dependencies=&hit.player_dependencies;
        hit.roster.racers[0].coin_count_178=12u;
        for(unsigned i=1u;i<8u;++i){
            hit.roster.racers[i].position_x_q16=(1400+(int32_t)i*20)*65536;
            hit.roster.racers[i].position_y_q16=1400*65536;
        }
        const int32_t x=hit.player.pose.position_x,y=hit.player.pose.position_y;
        const uint8_t cp=hit.roster.racers[0].route_index;
        if(family==0u){
            hit.lava_bubbles.count=1u;
            CHECK(portable_lava_bubble_init(&hit.lava_bubbles.actors[0],x,y,cp,1u));
            hit.lava_bubbles.actors[0].flags=3u;
        }else if(family==1u){
            hit.thwomps.count=1u;
            CHECK(portable_thwomp_init(&hit.thwomps.actors[0],x,y,cp,0u));
            hit.thwomps.actors[0].state=5u;hit.thwomps.actors[0].timer=2;
            hit.thwomps.actors[0].height_q16=4*65536;
            hit.thwomps.actors[0].velocity_z_q16=-4*65536;
        }else if(family==2u){
            hit.crabs.count=1u;
            CHECK(portable_crab_init(&hit.crabs.actors[0],x,y,cp,0u));
        }else if(family==3u){
            hit.seagulls.count=1u;
            CHECK(portable_seagull_init(&hit.seagulls.actors[0],x,y,cp,0u));
        }else if(family==4u){
            hit.cannonballs.count=1u;
            CHECK(portable_cannonball_init(&hit.cannonballs.actors[0],x,y,cp,0u));
            hit.cannonballs.actors[0].state=3;hit.cannonballs.actors[0].timer=100;
            hit.cannonballs.actors[0].flags=3u;
        }else if(family==5u){
            hit.piranhas.count=1u;
            CHECK(portable_piranha_init(&hit.piranhas.actors[0],x,y,cp,0u));
        }else if(family==6u){
            hit.snowmen.count=1u;
            CHECK(portable_snowman_init(&hit.snowmen.actors[0],x,y,cp,0u));
        }else if(family==7u){
            hit.penguins.count=1u;
            CHECK(portable_penguin_init(&hit.penguins.actors[0],x,y,cp,0u));
        }else if(family==8u){
            hit.boos.count=1u;
            CHECK(portable_boo_init(&hit.boos.actors[0],x,y,cp,0u));
            hit.boos.actors[0].height_q16=0;
        }else if(family==13u){
            hit.falling_stars.count=1u;
            CHECK(portable_falling_star_init(&hit.falling_stars.actors[0],x,y,cp,0u));
            hit.falling_stars.actors[0].state=5u;hit.falling_stars.actors[0].timer=1;
            hit.falling_stars.actors[0].flags=3u;
        }else if(family==12u){
            hit.thunderclouds.count=1u;
            CHECK(portable_thundercloud_init(&hit.thunderclouds.actors[0],x,y,cp,0u));
            hit.thunderclouds.actors[0].flags=3u;
        }else if(family==11u){
            hit.mice.count=1u;
            CHECK(portable_mouse_init(&hit.mice.actors[0],x,y,cp,0u));
        }else{
            hit.course_props.count=1u;
            CHECK(portable_course_prop_init(&hit.course_props.actors[0],x,y,
                family==9u?0x80000u:0x90000u,256u,0u));
            hit.course_props.actors[0].checkpoint=cp;
        }
        {
            PortableGpRaceSession cpu=hit;
            cpu.runtime.recovered_driver=&cpu.player;
            cpu.runtime.recovered_dependencies=&cpu.player_dependencies;
            cpu.source_clock_enable_boundary=PORTABLE_GP_CLOCK_ENABLE_BEFORE_FINAL_COUNTDOWN;
            cpu.player.pose.position_x=x+200*65536;
            cpu.player.pose.position_y=y+200*65536;
            PortableGpRacer *victim=&cpu.roster.racers[1];
            victim->position_x_q16=x;victim->position_y_q16=y;
            victim->contact_center_x=(int16_t)(x/65536);
            victim->contact_center_y=(int16_t)(y/65536);
            victim->route_index=cp;victim->source_flags=UINT32_C(0x2000);
            victim->coin_count_178=17u;
            victim->hit_cue_flags_58=(family&1u)?0x10u:0u;
            for(unsigned i=0u;i<8u;++i){
                cpu.roster.standings[i]=(uint8_t)i;
                cpu.roster.racers[i].rank_flags_ba=(uint16_t)(i*2u);
            }
            if(family==0u){
                PortableGpRaceSession absent=cpu;
                absent.runtime.recovered_driver=&absent.player;
                absent.runtime.recovered_dependencies=&absent.player_dependencies;
                absent.roster.racers[1].recovery_contact_34=0u;
                CHECK(portable_gp_race_session_tick(&absent,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
                CHECK(absent.cpu_course_contact_events==0u && absent.roster.racers[1].banana_hit_timer_38==0u && absent.impact_sounds.count==0u);
                PortableGpRaceSession ahead=cpu;
                ahead.runtime.recovered_driver=&ahead.player;
                ahead.runtime.recovered_dependencies=&ahead.player_dependencies;
                ahead.roster.standings[0]=1u;ahead.roster.standings[1]=0u;
                ahead.roster.racers[1].rank_flags_ba=0u;ahead.roster.racers[0].rank_flags_ba=2u;
                CHECK(portable_gp_race_session_tick(&ahead,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
                CHECK(ahead.roster.racers[1].hit_response_b6==UINT16_C(0x1fc0));
            }
            if(family==2u){
                PortableGpRaceSession protected=cpu;
                protected.runtime.recovered_driver=&protected.player;
                protected.runtime.recovered_dependencies=&protected.player_dependencies;
                protected.roster.racers[1].lightning_status_6a=10;
                CHECK(portable_gp_race_session_tick(&protected,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
                CHECK(protected.cpu_course_contact_events==1u &&
                    protected.roster.racers[1].banana_hit_timer_38==0u &&
                    protected.crabs.actors[0].timer==31 && protected.crabs.actors[0].flip_y &&
                    protected.player.contact_audio.serial==0u && protected.impact_sounds.count==0u);
                /* The passive node remains in this collision pass after a
                 * protected first driver changes its live actor flags. */
                PortableGpRaceSession pair=cpu;
                pair.runtime.recovered_driver=&pair.player;
                pair.runtime.recovered_dependencies=&pair.player_dependencies;
                pair.roster.racers[1].lightning_status_6a=10;
                pair.roster.racers[2].position_x_q16=x;
                pair.roster.racers[2].position_y_q16=y;
                pair.roster.racers[2].route_index=cp;
                CHECK(portable_gp_race_session_tick(&pair,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
                CHECK(pair.impact_sounds.count==1u);
                CHECK(pair.cpu_course_contact_events==2u &&
                    (pair.roster.racers[2].mushroom_response_flags_b4&UINT16_C(0x200))!=0u &&
                    pair.crabs.actors[0].flip_y);
                PortableGpRaceSession airborne=cpu;
                airborne.runtime.recovered_driver=&airborne.player;
                airborne.runtime.recovered_dependencies=&airborne.player_dependencies;
                airborne.roster.racers[1].world_height_q16=100*65536;
                airborne.roster.racers[1].lightning_state_78=1u;
                CHECK(portable_gp_race_session_tick(&airborne,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
                CHECK(airborne.cpu_course_contact_events==0u && airborne.roster.racers[1].banana_hit_timer_38==0u && airborne.impact_sounds.count==0u);
            }
            if(family==0u){
                PortableGpRaceSession distant=cpu;
                distant.runtime.recovered_driver=&distant.player;
                distant.runtime.recovered_dependencies=&distant.player_dependencies;
                distant.player.pose.position_x=x+600*65536;
                CHECK(portable_gp_race_session_tick(&distant,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
                CHECK(distant.impact_sounds.count==1u && distant.impact_sounds.events[0].volume==64u);
                PortableGpRaceSession finished=cpu;
                finished.runtime.recovered_driver=&finished.player;
                finished.runtime.recovered_dependencies=&finished.player_dependencies;
                finished.player.steering.state_flags|=0x20u;
                CHECK(portable_gp_race_session_tick(&finished,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
                CHECK(finished.impact_sounds.count==0u);
            }
            if(family==9u || family==10u){
                PortableGpRaceSession protected=cpu;
                protected.runtime.recovered_driver=&protected.player;
                protected.runtime.recovered_dependencies=&protected.player_dependencies;
                protected.roster.racers[1].lightning_status_6a=10;
                CHECK(portable_gp_race_session_tick(&protected,0u,&disposition)==0);
                CHECK(protected.cpu_course_contact_events==1u && protected.course_contact_events==0u &&
                    protected.course_props.actors[0].timer==31 && protected.course_props.actors[0].flags==1u &&
                    protected.course_props.actors[0].affine_y_q8==-256 &&
                    protected.roster.racers[1].banana_hit_timer_38==0u && !protected.player.contact_audio.serial);
            }
            if(family==11u){
                PortableGpRaceSession protected=cpu;
                protected.runtime.recovered_driver=&protected.player;
                protected.runtime.recovered_dependencies=&protected.player_dependencies;
                protected.roster.racers[1].lightning_status_6a=10;
                CHECK(portable_gp_race_session_tick(&protected,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
                CHECK(protected.mice.actors[0].flags==1u && protected.mice.actors[0].timer==31 &&
                    protected.mice.actors[0].affine_y_q8==-256 &&
                    !protected.roster.racers[1].banana_hit_timer_38 && !protected.impact_sounds.count);
            }
            if(family==12u){
                PortableGpRaceSession protected=cpu;
                protected.runtime.recovered_driver=&protected.player;
                protected.runtime.recovered_dependencies=&protected.player_dependencies;
                protected.roster.racers[1].lightning_status_6a=10;
                CHECK(portable_gp_race_session_tick(&protected,0u,&disposition)==0);
                CHECK(protected.thunderclouds.actors[0].flags==3u &&
                    !protected.roster.racers[1].lightning_timer_68 && !protected.impact_sounds.count);
                /* E490 is unconditional for clouds, even without bit2000. */
                victim->source_flags&=~UINT32_C(0x2000);
            }
            if(family==13u){
                PortableGpRaceSession protected=cpu;
                protected.runtime.recovered_driver=&protected.player;
                protected.runtime.recovered_dependencies=&protected.player_dependencies;
                protected.roster.racers[1].lightning_status_6a=10;
                CHECK(portable_gp_race_session_tick(&protected,0u,&disposition)==0);
                CHECK(protected.falling_stars.actors[0].flags==3u &&
                    !protected.roster.racers[1].banana_hit_timer_38 && !protected.impact_sounds.count);
                victim->source_flags&=~UINT32_C(0x2000);
            }
            const PortableGpRaceSessionStatus cpu_status=portable_gp_race_session_tick(&cpu,0u,&disposition);
            CHECK(cpu_status==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(cpu.course_contact_events==0u && cpu.cpu_course_contact_events==1u);
            CHECK(cpu.player.contact_audio.serial==0u && cpu.piranha_sounds.count==0u);
            CHECK(cpu.impact_sounds.count==((family<=2u || family==4u || family==7u || family==11u || family==12u || family==13u)?1u:0u));
            if(cpu.impact_sounds.count){
                const PortableCourseImpactSound *voice=&cpu.impact_sounds.events[0];
                CHECK(voice->character==victim->driver_class && voice->rival==1u &&
                    voice->variant==((family&1u)?0u:1u) && voice->volume==178u);
            }
            if(family==0u || family==2u || family==4u || family==7u || family==11u || family==12u || family==13u)
                CHECK((victim->mushroom_response_flags_b4&UINT16_C(0x200))!=0u &&
                    victim->hit_response_b6==UINT16_C(0x0fc0));
            else if(family==1u)CHECK(victim->flattened_64 && victim->banana_hit_timer_38==0x6fffu);
            else if(family==5u || family==6u || family>=9u)CHECK(victim->banana_hit_timer_38==7u &&
                victim->mushroom_mode_84==0u);
            else CHECK(victim->banana_hit_timer_38==0u);
            if(family==12u)CHECK(victim->lightning_timer_68==359u &&
                !(victim->hit_cue_flags_58&0x10u));
            PortableGpRaceSnapshot cpu_snapshot;
            CHECK(portable_gp_race_session_snapshot(&cpu,&cpu_snapshot)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(cpu_snapshot.opponents[0].flattened==(family==1u) &&
                cpu_snapshot.opponents[0].palette_bank==(family==8u?7u:family==12u?1u:family==0u?5u:0u));
            CHECK(cpu_snapshot.impact_sounds.count==cpu.impact_sounds.count);
            if(family==8u){
                const PortableGpRaceSession frozen=cpu;
                PortableGpRaceSnapshot again;
                CHECK(portable_gp_race_session_snapshot(&cpu,&again)==PORTABLE_GP_RACE_SESSION_OK &&
                    memcmp(&frozen,&cpu,sizeof cpu)==0 && again.opponents[0].palette_bank==7u);
                cpu.boos.count=0u;
                CHECK(portable_gp_race_session_tick(&cpu,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
                CHECK(portable_gp_race_session_snapshot(&cpu,&again)==PORTABLE_GP_RACE_SESSION_OK &&
                    again.opponents[0].palette_bank==0u);
            }
            if(family==5u)CHECK(!cpu.piranhas.actors[0].struck_player);
            if(family==1u){
                cpu.thwomps.count=0u;
                for(unsigned tick=1u;tick<128u;++tick){
                    CHECK(portable_gp_race_session_tick(&cpu,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
                    CHECK(victim->flattened_64==(tick<127u));
                }
                CHECK(victim->banana_hit_timer_38==0u && (victim->source_flags&UINT32_C(0x4000))!=0u);
                CHECK(portable_gp_race_session_tick(&cpu,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
                CHECK((victim->source_flags&UINT32_C(0x4000))==0u);
            }
        }
        if(family==11u){
            /* An earlier item writer's shrink survives an ordinary actor hit. */
            PortableGpRaceSession shrunk=hit;
            shrunk.runtime.recovered_driver=&shrunk.player;
            shrunk.runtime.recovered_dependencies=&shrunk.player_dependencies;
            shrunk.roster.racers[0].lightning_timer_68=100u;
            shrunk.player.lightning_timer_68=0;
            CHECK(portable_gp_race_session_tick(&shrunk,0u,&disposition)==0);
            CHECK(shrunk.roster.racers[0].lightning_timer_68==99u && shrunk.player.lightning_timer_68==99);
        }
        if(family==0u || family==2u || family==7u || family==11u){
            /* FF80 dispatches E490 for any of these families with +48 bit2000. */
            PortableGpRaceSession strong=hit;
            strong.runtime.recovered_driver=&strong.player;
            strong.runtime.recovered_dependencies=&strong.player_dependencies;
            strong.player.steering.state_flags|=UINT32_C(0x2000);
            CHECK(portable_gp_race_session_tick(&strong,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(strong.impact_sounds.count==1u && !strong.impact_sounds.events[0].rival &&
                strong.impact_sounds.events[0].volume==256u);
            CHECK(strong.course_contact_events==1u && strong.roster.racers[0].coin_count_178==8u &&
                (strong.player.forward_acceleration.state_flags&UINT16_C(0x200))!=0u);
        }
        CHECK(portable_gp_race_session_tick(&hit,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(hit.course_contact_events==1u);
        CHECK(hit.impact_sounds.count==((family==1u || family==4u || family==12u || family==13u)?1u:0u));
        const uint16_t songs[]={377u,169u,373u,385u,170u,0u,373u,373u,380u,169u,169u,373u,374u,373u};
        if(family!=5u)CHECK(hit.player.contact_audio.song_id==songs[family]);
        else CHECK(hit.piranha_sounds.count>=1u && hit.piranha_sounds.songs[0]==373u);
        if(family==3u || family==8u)CHECK(hit.player.normal_collision_timer_38==0u);
        else if(family==1u)CHECK(hit.player.flattened_64 && hit.player.normal_collision_timer_38==0x6fffu);
        else CHECK(hit.player.normal_collision_timer_38==7u);
        CHECK(hit.roster.racers[0].coin_count_178==(family==1u?4u:(family==4u || family==12u || family==13u)?8u:12u));
        if(family==12u){
            CHECK(hit.player.lightning_timer_68==359u && hit.roster.racers[0].lightning_timer_68==359u);
            PortableGpRaceSession protected=session;
            protected.runtime.recovered_driver=&protected.player;
            protected.runtime.recovered_dependencies=&protected.player_dependencies;
            protected.thunderclouds=hit.thunderclouds;
            protected.roster.racers[0].lightning_status_6a=10;
            protected.player.invulnerability_timer_6a=10;
            CHECK(portable_gp_race_session_tick(&protected,0u,&disposition)==0);
            CHECK(!protected.player.lightning_timer_68 && protected.player.contact_audio.song_id==160u &&
                protected.thunderclouds.actors[0].flags==3u && !protected.impact_sounds.count);
        }
        PortableGpRaceSnapshot hazard_snapshot;
        CHECK(portable_gp_race_session_snapshot(&hit,&hazard_snapshot)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(hazard_snapshot.contact_audio.song_id==hit.player.contact_audio.song_id &&
            hazard_snapshot.piranha_sounds.count==hit.piranha_sounds.count);
        CHECK(hazard_snapshot.player_flattened==(family==1u) &&
            hazard_snapshot.player_palette_bank==(family==8u?7u:family==12u?1u:family==0u?5u:0u));
    }

    repeat = session;
    repeat.runtime.recovered_driver = &repeat.player;
    repeat.runtime.recovered_dependencies = &repeat.player_dependencies;
    for (size_t tick = 0u; tick < 12u; ++tick)
    {
        CHECK(portable_gp_race_session_tick(
                  &session, PORTABLE_INPUT_ACCELERATE, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
              disposition == PORTABLE_RACE_TICK_ADVANCE_MOTION);
        CHECK(portable_gp_race_session_tick(
                  &repeat, PORTABLE_INPUT_ACCELERATE, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
              disposition == PORTABLE_RACE_TICK_ADVANCE_MOTION);
        CHECK(session.item_art_clock.calls==3u+(uint32_t)tick&&
              repeat.item_art_clock.calls==session.item_art_clock.calls);
        if (tick == 0u)
            CHECK(session.source_result_clock.enabled == 1u &&
                  session.source_result_clock.centiseconds == 0u);
        if (tick == 1u)
            CHECK(session.source_result_clock.phase == 1u &&
                  session.source_result_clock.centiseconds == 1u);
    }
    CHECK(session.source_result_clock.phase == 11u &&
          session.source_result_clock.centiseconds == 18u &&
          repeat.source_result_clock.centiseconds == 18u);
    CHECK(portable_gp_race_session_snapshot(&session, &snapshot) ==
          PORTABLE_GP_RACE_SESSION_OK &&
          snapshot.source_clock_centiseconds == 18u &&
          snapshot.source_clock_phase == 11u &&
          snapshot.source_result_times_ready == 0u);
    CHECK(session.player.pose.position_y != initial_player_y);
    CHECK(session.roster.racers[1].position_y_q16 != initial_opponent_y);
    /* Source GP grid frame 451 installs the rivals' control without motion;
     * their first movement is the following racing frame. */
    CHECK(session.roster.authoritative_ticks == 11u);
    CHECK(session.roster.contact_events == session.driving_quality.event_1a);
    CHECK(session.roster.racers[0].position_x_q16 ==
              session.player.pose.position_x &&
          session.roster.racers[0].position_y_q16 ==
              session.player.pose.position_y &&
          session.roster.racers[0].speed_q16 ==
              session.player.motion.speed_q16);
    CHECK(memcmp(&session.player, &repeat.player,
                 sizeof session.player) == 0);
    CHECK(memcmp(&session.roster, &repeat.roster,
                 sizeof session.roster) == 0);

    /* B98C keeps the last route on both outside-route raster sentinels. */
    for(unsigned high=0u;high<2u;++high){
        PortableGpRaceSession boundary=session;
        boundary.runtime.recovered_driver=&boundary.player;
        boundary.runtime.recovered_dependencies=&boundary.player_dependencies;
        const uint8_t prior_route=boundary.roster.racers[0].route_index;
        uint8_t saved[sizeof raster_cells];
        memcpy(saved,raster_cells,sizeof saved);
        const unsigned x=(unsigned)(boundary.player.pose.position_x/(16*65536));
        const unsigned y=(unsigned)(boundary.player.pose.position_y/(16*65536));
        boundary.player.pose.position_x=(int32_t)(x*16u+8u)*65536;
        boundary.player.pose.position_y=(int32_t)(y*16u+8u)*65536;
        raster_cells[y*128u+x]=(uint8_t)(high?0xff:0x7f);
        boundary.player.source_speed_variant_19b=2u;
        boundary.roster.racers[0].acceleration_mode_19b=2u;
        CHECK(portable_gp_race_session_tick(&boundary,PORTABLE_INPUT_ACCELERATE,
            &disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(boundary.player.pose.track_cell==(high?0xff:0x7f));
        CHECK(boundary.roster.racers[0].route_index==prior_route&&
            boundary.player.source_speed_variant_19b==0u&&
            boundary.roster.racers[0].acceleration_mode_19b==0u&&
            (boundary.player.steering.state_flags&2u)!=0u);
        memcpy(raster_cells,saved,sizeof saved);
    }

    {
        /* BA52 rejects the new cell at entry height4+epsilon. GP publication
         * must retain core90/19B after the hop lowers it below four, so next
         * turn's rescue/standings cannot consume the rejected raw checkpoint. */
        PortableGpRaceSession airborne=session;
        airborne.runtime.recovered_driver=&airborne.player;
        airborne.runtime.recovered_dependencies=&airborne.player_dependencies;
        uint8_t saved[sizeof raster_cells];memcpy(saved,raster_cells,sizeof saved);
        const uint8_t saved_mode=route_modes[45];
        memset(raster_cells,45,sizeof raster_cells);route_modes[45]=0x82u;
        airborne.runtime.session.checkpoint_raw=44u;
        airborne.player.source_route_index_90=44u;
        airborne.roster.racers[0].route_index=44u;
        airborne.player.pose.position_x=888*65536;
        airborne.player.pose.position_y=612*65536;
        airborne.player.pose.position_z=4*65536+1;
        airborne.player.hop.height_q16=airborne.player.pose.position_z;
        airborne.player.hop.velocity_q8=-1;
        airborne.player.hop.mode=1u;
        airborne.player.motion.speed_q16=0;
        airborne.player.motion.acceleration_q16=0;
        airborne.player.motion.component_x=0;
        airborne.player.motion.component_y=0;
        CHECK(portable_gp_race_session_tick(&airborne,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(airborne.player.source_progress.height_q16==4*65536+1 &&
            airborne.player.pose.position_z<=4*65536 &&
            airborne.runtime.session.checkpoint_raw==44u &&
            airborne.player.source_route_index_90==44u &&
            airborne.roster.racers[0].route_index==44u &&
            airborne.roster.racers[0].acceleration_mode_19b==0x82u &&
            (airborne.roster.racers[0].source_flags&2u)!=0u);
        CHECK(portable_gp_race_session_tick(&airborne,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(airborne.runtime.session.checkpoint_raw==45u &&
            airborne.player.source_route_index_90==45u &&
            airborne.roster.racers[0].route_index==45u &&
            (airborne.roster.racers[0].source_flags&2u)==0u);
        route_modes[45]=saved_mode;memcpy(raster_cells,saved,sizeof saved);
    }

    /* The shared coin ledger feeds the source input target over the borrowed
     * engine base; four coins contribute two Q16 speed units. */
    {
        PortableGpRaceSession coin_sync = session;
        coin_sync.runtime.recovered_driver = &coin_sync.player;
        coin_sync.runtime.recovered_dependencies =
            &coin_sync.player_dependencies;
        coin_sync.roster.racers[0].coin_count_178 = 4u;
        CHECK(portable_gp_race_session_tick(
                  &coin_sync, PORTABLE_INPUT_ACCELERATE,
                  &disposition) == PORTABLE_GP_RACE_SESSION_OK);
        CHECK(coin_sync.player.input.speed_parameter == 4u &&
              coin_sync.player.input.speed_target_q16 ==
                  fixture_engines[0].player_base_speed_q16+INT32_C(2)*65536);
    }

    CHECK(portable_gp_race_session_snapshot(&session, &snapshot) ==
          PORTABLE_GP_RACE_SESSION_OK);
    CHECK(portable_gp_race_session_snapshot(&repeat, &repeated_snapshot) ==
          PORTABLE_GP_RACE_SESSION_OK);
    CHECK(memcmp(&snapshot, &repeated_snapshot, sizeof snapshot) == 0);
    CHECK(snapshot.phase == PORTABLE_RACE_PHASE_RACING &&
          snapshot.lap_count == 3u && snapshot.total_ticks == 12u &&
          snapshot.player_x_q16 == session.player.pose.position_x &&
          snapshot.player_route_index ==
              session.roster.racers[0].route_index);
    CHECK(portable_gp_race_presentation_build(
              &session, &snapshot, &presentation_state));
    CHECK(presentation_state.gp_rank==snapshot.player_rank &&
          presentation_state.gp_coins==session.roster.racers[0].coin_count_178 &&
          presentation_state.source_clock_override &&
          presentation_state.source_clock_centiseconds==snapshot.source_clock_centiseconds);
    for(size_t rank=0u;rank<4u;++rank)
        CHECK(presentation_state.gp_top_four_classes[rank]==
              session.roster.racers[session.roster.standings[rank]].driver_class);
    CHECK(presentation_state.position_x_q16 == snapshot.player_x_q16 &&
          presentation_state.position_y_q16 == snapshot.player_y_q16 &&
          presentation_state.camera_yaw == snapshot.player_heading &&
          presentation_state.total_ticks == snapshot.total_ticks &&
          presentation_state.opponent_count == PORTABLE_GP_OPPONENT_COUNT &&
          presentation_state.opponents[0].position_x_q16 ==
              snapshot.opponents[0].position_x_q16 &&
          presentation_state.opponents[0].visible == 1u);
    for (size_t index = 0u; index < PORTABLE_GP_OPPONENT_COUNT; ++index)
        CHECK(snapshot.opponents[index].visible &&
              snapshot.opponents[index].driver_class ==
                  session.roster.racers[index + 1u].driver_class);

    /* The production session owns roulette, source edge generation, use, and
     * the active item lifecycle.  Holding C must not synthesize another
     * +0x80 press on the following authoritative frame. */
    CHECK(test_gp_init(&item_session, &race, 7u) ==
          PORTABLE_GP_RACE_SESSION_OK);
    CHECK(portable_gp_race_session_tick(
              &item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          disposition == PORTABLE_RACE_TICK_COUNTDOWN);
    CHECK(portable_gp_race_session_tick(
              &item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          disposition == PORTABLE_RACE_TICK_COUNTDOWN);
    for (size_t index = 1u; index < PORTABLE_GP_RACER_COUNT; ++index)
    {
        item_session.roster.racers[index].position_x_q16 =
            (INT32_C(1400) + (int32_t)index * 20) << 16u;
        item_session.roster.racers[index].position_y_q16 =
            (INT32_C(1400) + (int32_t)index * 20) << 16u;
    }
    CHECK(portable_gp_race_session_begin_player_item_box(
              &item_session, 0u) == PORTABLE_GP_RACE_SESSION_OK &&
          item_session.roster.racers[0].item_inventory.held_item_17a ==
              PORTABLE_GP_ITEM_ROULETTE);
    for (size_t tick = 0u;
         item_session.roster.racers[0].item_inventory.active && tick < 32u;
         ++tick)
        CHECK(portable_gp_race_session_tick_player_item(
                  &item_session, 1u, UINT16_C(0x100c)) ==
              PORTABLE_GP_RACE_SESSION_OK);
    CHECK(item_session.roster.racers[0].item_inventory.held_item_17a ==
              UINT16_C(0x100c) &&
          !item_session.roster.racers[0].item_inventory.active);
    CHECK(portable_gp_race_session_tick(
              &item_session, PORTABLE_INPUT_ITEM, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          disposition == PORTABLE_RACE_TICK_ADVANCE_MOTION &&
          item_session.roster.racers[0].item_inventory.held_item_17a == 0u &&
          item_session.roster.lightning_events == 1u &&
          item_session.driving_quality.event_16 == 1u &&
          item_session.roster.racers[1].lightning_timer_68 ==
              test_item_status_rules()->lightning_ticks);
    CHECK(item_session.audio_event_count==2u && item_session.audio_events[0].song_id==104u &&
          item_session.audio_events[0].player==7u && item_session.audio_events[1].song_id==126u &&
          item_session.audio_events[1].player==10u && !item_session.roster.lightning_audio_pending);
    CHECK(item_session.roster.item_hud.current==0u && item_session.roster.item_hud.displayed_item==0u);
    CHECK(portable_gp_race_session_snapshot(&item_session, &snapshot) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          portable_gp_race_presentation_build(
              &item_session, &snapshot, &presentation_state) &&
          presentation_state.character_affine_scale == UINT16_C(0x0100) &&
          presentation_state.opponents[0].character_affine_scale ==
              UINT16_C(0x0180));
    {
        PortableGpRaceSession flashing=item_session;
        flashing.runtime.recovered_driver=&flashing.player;
        for (unsigned tick=0u;tick<68u;++tick) {
            flashing.source_frame_clock=tick;
            /* C84C's fixed >295 flash gate is independent of item duration overrides. */
            flashing.roster.racers[1].lightning_timer_68=(uint16_t)(359u-tick);
            CHECK(portable_gp_race_session_snapshot(&flashing,&snapshot)==PORTABLE_GP_RACE_SESSION_OK &&
                  portable_gp_race_presentation_build(&flashing,&snapshot,&presentation_state));
            CHECK(presentation_state.palette_bank==0u &&
                  presentation_state.opponents[0].palette_bank==
                      (tick<64u && (tick&4u)==0u?1u:0u));
        }
        flashing.roster.racers[1].lightning_status_6a=1;
        flashing.source_frame_clock=12u;
        CHECK(portable_gp_race_session_snapshot(&flashing,&snapshot)==PORTABLE_GP_RACE_SESSION_OK &&
              portable_gp_race_presentation_build(&flashing,&snapshot,&presentation_state) &&
              presentation_state.opponents[0].palette_bank==4u);
    }
    CHECK(portable_gp_race_session_tick(
              &item_session, PORTABLE_INPUT_ITEM, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          item_session.roster.lightning_events == 1u &&
          item_session.driving_quality.event_16 == 1u);

    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
          PORTABLE_GP_RACE_SESSION_OK);
    {
        PortableGpRaceSession restored=item_session;
        restored.runtime.recovered_driver=&restored.player;
        for (unsigned tick=0u;tick<test_item_status_rules()->lightning_ticks;++tick)
            CHECK(portable_gp_race_session_tick(&restored,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        for (unsigned i=1u;i<8u;++i) {
            const PortableGpRacer *cpu=&restored.roster.racers[i];
            CHECK(cpu->lightning_timer_68==0u && (cpu->hit_cue_flags_58&UINT16_C(0x10))!=0u &&
                  (cpu->mushroom_response_flags_b4&UINT16_C(0x0200))==0u);
        }
        CHECK(portable_gp_race_session_snapshot(&restored,&snapshot)==PORTABLE_GP_RACE_SESSION_OK &&
              portable_gp_race_presentation_build(&restored,&snapshot,&presentation_state) &&
              presentation_state.opponents[0].character_affine_scale==UINT16_C(0x100));
    }
    item_session.roster.racers[0].item_inventory.held_item_17a =
        PORTABLE_GP_GREEN_SHELL_ITEM;
    item_session.roster.racers[0].item_inventory.settled_item =
        PORTABLE_GP_GREEN_SHELL_ITEM;
    CHECK(portable_gp_race_session_tick(
              &item_session, PORTABLE_INPUT_ITEM, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          item_session.roster.racers[0].green_shell.phase ==
              PORTABLE_GP_GREEN_SHELL_ACTIVE &&
          item_session.roster.item_uses == 2u);
    CHECK(portable_gp_race_session_snapshot(&item_session, &snapshot) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          portable_gp_race_presentation_build(
              &item_session, &snapshot, &presentation_state) &&
          presentation_state.green_shells[0].visible &&
          presentation_state.green_shells[0].x_q16 ==
              item_session.roster.racers[0].green_shell.position_x_q16);
    CHECK(portable_gp_race_session_tick(
              &item_session, PORTABLE_INPUT_ITEM, &disposition) ==
          PORTABLE_GP_RACE_SESSION_OK);
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          item_session.roster.racers[0].green_shell.phase ==
              PORTABLE_GP_GREEN_SHELL_RELEASED);

    /*1001 uses the normal input path: staggered orbits survive held input,
     * then each new press fires one newest actor, always forward. */
    for(unsigned profile=0u;profile<2u;++profile){
        const uint32_t saved_course=race.rules.compatibility.course_id;
        const PortableGpCpuItemRules *saved_rules=race.gp_cpu_items;
        const size_t saved_count=race.gp_cpu_item_engine_count;
        PortableGpCpuItemRules orbit_rules[3]={0};
        for(unsigned engine=0u;engine<3u;++engine){
            orbit_rules[engine].shell_orbit_delay_valid=1u;
            orbit_rules[engine].shell_orbit_delay[0][1]=17u;
            orbit_rules[engine].shell_orbit_delay[0][2]=34u;
        }
        race.rules.compatibility.course_id=profile?25u:24u;
        race.gp_cpu_items=orbit_rules;race.gp_cpu_item_engine_count=3u;
        PortableGpRaceSession triple;
        CHECK(test_gp_init(&triple,&race,7u)==0);
        CHECK(portable_gp_race_session_tick(&triple,0u,&disposition)==0 &&
            portable_gp_race_session_tick(&triple,0u,&disposition)==0);
        for(unsigned i=1u;i<8u;++i){
            triple.roster.racers[i].position_x_q16=(1400+(int32_t)i*20)*65536;
            triple.roster.racers[i].position_y_q16=(1400+(int32_t)i*20)*65536;
        }
        PortableGpRacer *local=&triple.roster.racers[0];
        local->item_inventory.held_item_17a=PORTABLE_GP_TRIPLE_GREEN_SHELL_ITEM;
        const unsigned triple_status=portable_gp_race_session_tick(&triple,PORTABLE_INPUT_ITEM,&disposition);
        CHECK(triple_status==0 &&
            local->item_use_state_17c==3u && triple.roster.triple_green_uses==1u && !local->shell_item_18c &&
            !local->item_inventory.held_item_17a && portable_gp_item_pool_count(&triple.roster)==3u);
        CHECK(triple.roster.green_pool[0].shell.phase==PORTABLE_GP_GREEN_SHELL_ORBITING &&
            triple.roster.green_pool[1].shell.release_timer_74==16u &&
            local->green_shell.release_timer_74==33u);
        CHECK(triple.audio_event_count==1u && triple.audio_events[0].song_id==122u);
        for(unsigned tick=0u;tick<34u;++tick)
            CHECK(portable_gp_race_session_tick(&triple,PORTABLE_INPUT_ITEM,&disposition)==0);
        CHECK(local->green_shell.phase==PORTABLE_GP_GREEN_SHELL_ORBITING && local->item_use_state_17c==3u);
        CHECK(portable_gp_race_session_snapshot(&triple,&snapshot)==0 &&
            portable_gp_race_presentation_build(&triple,&snapshot,&presentation_state));
        CHECK(presentation_state.green_shells[0].visible && presentation_state.green_shells[8].visible &&
            presentation_state.green_shells[9].visible &&
            (presentation_state.green_shells[0].x_q16!=presentation_state.green_shells[8].x_q16 ||
             presentation_state.green_shells[0].y_q16!=presentation_state.green_shells[8].y_q16));
        {
            PortableGpRaceSession hit=triple;
            hit.runtime.recovered_driver=&hit.player;hit.runtime.recovered_dependencies=&hit.player_dependencies;
            PortableGpGreenShell *orbit=&hit.roster.racers[0].green_shell;
            orbit->animation_tick_4c=0u;
            hit.roster.racers[1].position_x_q16=hit.player.pose.position_x;
            hit.roster.racers[1].position_y_q16=hit.player.pose.position_y-14*sine[256];
            const unsigned orbit_hit_status=portable_gp_race_session_tick(&hit,PORTABLE_INPUT_ITEM,&disposition);
            CHECK(orbit_hit_status==0 &&
                orbit->phase==PORTABLE_GP_GREEN_SHELL_DESTROYING &&
                hit.roster.racers[0].item_use_state_17c==2u &&
                hit.roster.racers[1].banana_hit_timer_38==7u && orbit->retire_held_slot);
        }
        CHECK(portable_gp_race_session_tick(&triple,0u,&disposition)==0);
        CHECK(portable_gp_race_session_tick(&triple,PORTABLE_INPUT_ITEM|PORTABLE_INPUT_DOWN,&disposition)==0 &&
            local->green_shell.phase==PORTABLE_GP_GREEN_SHELL_RELEASED && local->item_use_state_17c==2u &&
            local->green_shell.angle_26==(uint16_t)local->release_angle_offset);
        CHECK(triple.audio_event_count==1u && triple.audio_events[0].song_id==123u);
        CHECK(portable_gp_race_session_tick(&triple,0u,&disposition)==0 &&
            portable_gp_race_session_tick(&triple,PORTABLE_INPUT_ITEM,&disposition)==0 &&
            local->item_use_state_17c==2u); /* flight lock rejects this press */
        for(unsigned shot=0u;shot<2u;++shot){
            for(unsigned tick=0u;tick<17u;++tick)
                CHECK(portable_gp_race_session_tick(&triple,0u,&disposition)==0);
            CHECK(portable_gp_race_session_tick(&triple,PORTABLE_INPUT_ITEM,&disposition)==0 &&
                local->item_use_state_17c==1u-shot);
        }
        CHECK(!local->shell_item_18c && triple.roster.green_pool[0].shell.phase!=PORTABLE_GP_GREEN_SHELL_ORBITING &&
            triple.roster.green_pool[1].shell.phase!=PORTABLE_GP_GREEN_SHELL_ORBITING);
        race.gp_cpu_items=saved_rules;race.gp_cpu_item_engine_count=saved_count;
        race.rules.compatibility.course_id=saved_course;
    }

    /*1003 uses the normal input path: staggered orbits survive held input,
     * then each new press fires one newest actor, always forward. */
    for(unsigned profile=0u;profile<2u;++profile){
        const uint32_t saved_course=race.rules.compatibility.course_id;
        const PortableGpCpuItemRules *saved_rules=race.gp_cpu_items;
        const size_t saved_count=race.gp_cpu_item_engine_count;
        PortableGpCpuItemRules orbit_rules[3]={0};
        for(unsigned engine=0u;engine<3u;++engine){
            orbit_rules[engine].shell_orbit_delay_valid=1u;
            orbit_rules[engine].shell_orbit_delay[1][1]=17u;
            orbit_rules[engine].shell_orbit_delay[1][2]=34u;
        }
        race.rules.compatibility.course_id=profile?25u:24u;
        race.gp_cpu_items=orbit_rules;race.gp_cpu_item_engine_count=3u;
        PortableGpRaceSession triple;
        CHECK(test_gp_init(&triple,&race,7u)==0);
        CHECK(portable_gp_race_session_tick(&triple,0u,&disposition)==0 &&
            portable_gp_race_session_tick(&triple,0u,&disposition)==0);
        for(unsigned i=1u;i<8u;++i){
            triple.roster.racers[i].position_x_q16=(1400+(int32_t)i*20)*65536;
            triple.roster.racers[i].position_y_q16=(1400+(int32_t)i*20)*65536;
        }
        PortableGpRacer *local=&triple.roster.racers[0];
        local->item_inventory.held_item_17a=PORTABLE_GP_TRIPLE_RED_SHELL_ITEM;
        const unsigned triple_status=portable_gp_race_session_tick(&triple,PORTABLE_INPUT_ITEM,&disposition);
        CHECK(triple_status==0 &&
            local->item_use_state_17c==3u && triple.roster.triple_red_uses==1u && !local->shell_item_18c &&
            !local->item_inventory.held_item_17a && portable_gp_item_pool_count(&triple.roster)==3u);
        CHECK(triple.roster.red_pool[0].shell.phase==PORTABLE_GP_RED_SHELL_ORBITING &&
            triple.roster.red_pool[1].shell.release_timer_74==16u &&
            local->red_shell.release_timer_74==33u);
        CHECK(triple.audio_event_count==1u && triple.audio_events[0].song_id==122u);
        for(unsigned tick=0u;tick<34u;++tick)
            CHECK(portable_gp_race_session_tick(&triple,PORTABLE_INPUT_ITEM,&disposition)==0);
        CHECK(local->red_shell.phase==PORTABLE_GP_RED_SHELL_ORBITING && local->item_use_state_17c==3u);
        CHECK(portable_gp_race_session_snapshot(&triple,&snapshot)==0 &&
            portable_gp_race_presentation_build(&triple,&snapshot,&presentation_state));
        CHECK(presentation_state.red_shells[0].visible && presentation_state.red_shells[8].visible &&
            presentation_state.red_shells[9].visible &&
            (presentation_state.red_shells[0].x_q16!=presentation_state.red_shells[8].x_q16 ||
             presentation_state.red_shells[0].y_q16!=presentation_state.red_shells[8].y_q16));
        {
            PortableGpRaceSession hit=triple;
            hit.runtime.recovered_driver=&hit.player;hit.runtime.recovered_dependencies=&hit.player_dependencies;
            PortableGpRedShell *orbit=&hit.roster.racers[0].red_shell;
            orbit->animation_tick_4c=0u;
            hit.roster.racers[1].position_x_q16=hit.player.pose.position_x;
            hit.roster.racers[1].position_y_q16=hit.player.pose.position_y-14*sine[256];
            const unsigned orbit_hit_status=portable_gp_race_session_tick(&hit,PORTABLE_INPUT_ITEM,&disposition);
            CHECK(orbit_hit_status==0 &&
                orbit->phase==PORTABLE_GP_RED_SHELL_DESTROYING_ROUTE &&
                hit.roster.racers[0].item_use_state_17c==2u &&
                hit.roster.racers[1].banana_hit_timer_38==7u && orbit->retire_held_slot);
        }
        CHECK(portable_gp_race_session_tick(&triple,0u,&disposition)==0);
        CHECK(portable_gp_race_session_tick(&triple,PORTABLE_INPUT_ITEM|PORTABLE_INPUT_DOWN,&disposition)==0 &&
            (local->red_shell.phase==PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_TARGET ||
             local->red_shell.phase==PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET) && local->item_use_state_17c==2u &&
            local->red_shell.angle_26==(uint16_t)local->release_angle_offset);
        CHECK(triple.audio_event_count==1u && triple.audio_events[0].song_id==123u);
        CHECK(portable_gp_race_session_tick(&triple,0u,&disposition)==0 &&
            portable_gp_race_session_tick(&triple,PORTABLE_INPUT_ITEM,&disposition)==0 &&
            local->item_use_state_17c==2u); /* flight lock rejects this press */
        for(unsigned shot=0u;shot<2u;++shot){
            for(unsigned tick=0u;tick<17u;++tick)
                CHECK(portable_gp_race_session_tick(&triple,0u,&disposition)==0);
            CHECK(portable_gp_race_session_tick(&triple,PORTABLE_INPUT_ITEM,&disposition)==0 &&
                local->item_use_state_17c==1u-shot);
        }
        CHECK(!local->shell_item_18c && triple.roster.red_pool[0].shell.phase!=PORTABLE_GP_RED_SHELL_ORBITING &&
            triple.roster.red_pool[1].shell.phase!=PORTABLE_GP_RED_SHELL_ORBITING);
        race.gp_cpu_items=saved_rules;race.gp_cpu_item_engine_count=saved_count;
        race.rules.compatibility.course_id=saved_course;
    }

    /* A backward red/Spiny remains a real trap after another red-family
     * item is equipped, with its own timers and immutable presentation. */
    for(unsigned spiny=0u;spiny<2u;++spiny){
        PortableGpRaceSession repeated;
        CHECK(test_gp_init(&repeated,&race,7u)==0 &&
            portable_gp_race_session_tick(&repeated,0u,&disposition)==0 &&
            portable_gp_race_session_tick(&repeated,0u,&disposition)==0);
        for(unsigned i=1u;i<8u;++i){
            repeated.roster.racers[i].position_x_q16=(1400+(int32_t)i*20)*65536;
            repeated.roster.racers[i].position_y_q16=(1400+(int32_t)i*20)*65536;
        }
        PortableGpRacer *local=&repeated.roster.racers[0];
        local->item_inventory.held_item_17a=spiny?PORTABLE_GP_SPINY_SHELL_ITEM:PORTABLE_GP_RED_SHELL_ITEM;
        CHECK(portable_gp_race_session_tick(&repeated,PORTABLE_INPUT_ITEM,&disposition)==0 &&
            portable_gp_race_session_tick(&repeated,PORTABLE_INPUT_DOWN,&disposition)==0);
        for(unsigned tick=0u;tick<16u;++tick)CHECK(portable_gp_race_session_tick(&repeated,0u,&disposition)==0);
        CHECK(local->red_shell.phase==PORTABLE_GP_RED_SHELL_RELEASED_BACKWARD && !(local->source_flags&0x10000u));
        const uint16_t age=local->red_shell.animation_tick_4c;
        local->item_inventory.held_item_17a=spiny?PORTABLE_GP_RED_SHELL_ITEM:PORTABLE_GP_SPINY_SHELL_ITEM;
        CHECK(portable_gp_race_session_tick(&repeated,PORTABLE_INPUT_ITEM,&disposition)==0 &&
            local->red_shell.phase==PORTABLE_GP_RED_SHELL_ACTIVE &&
            repeated.roster.red_pool[0].shell.animation_tick_4c==age+1u &&
            repeated.roster.red_pool[0].shell.kind_10==(spiny?PORTABLE_GP_SPINY_SHELL_KIND:PORTABLE_GP_RED_SHELL_KIND));
        CHECK(portable_gp_race_session_snapshot(&repeated,&snapshot)==0 &&
            portable_gp_race_presentation_build(&repeated,&snapshot,&presentation_state) &&
            presentation_state.red_shells[0].visible && presentation_state.red_shells[8].visible &&
            presentation_state.red_shells[8].kind==(spiny?PORTABLE_GP_SPINY_SHELL_KIND:PORTABLE_GP_RED_SHELL_KIND));
        /* Force the retained trap's imported arming boundary; the new held
         * shell must keep its counter and cannot replace its callback. */
        repeated.roster.red_pool[0].shell.animation_tick_4c=(uint16_t)(test_red_shell_rules()->trap_arm_ticks-1u);
        CHECK(portable_gp_race_session_tick(&repeated,PORTABLE_INPUT_ITEM,&disposition)==0 &&
            repeated.roster.red_pool[0].shell.phase==PORTABLE_GP_RED_SHELL_TRAP_ARMED &&
            local->red_shell.phase==PORTABLE_GP_RED_SHELL_ACTIVE && local->item_use_state_17c==1u);
        if(spiny){
            int16_t pitch_table[PORTABLE_SPATIAL_PITCH_COUNT];
            for(unsigned i=0u;i<PORTABLE_SPATIAL_PITCH_COUNT;++i)pitch_table[i]=-777;
            repeated.player_dependencies.spatial_pitch_table=pitch_table;
            repeated.player_dependencies.spatial_pitch_count=PORTABLE_SPATIAL_PITCH_COUNT;
            PortableGpRedShell *retained=&repeated.roster.red_pool[0].shell;
            retained->flight_audio_pending=1u;
            retained->flight_audio_x_q16=repeated.player.pose.position_x;
            retained->flight_audio_y_q16=repeated.player.pose.position_y;
            CHECK(portable_gp_race_session_tick(&repeated,PORTABLE_INPUT_ITEM,&disposition)==0);
            unsigned cues=0u;
            for(unsigned i=0u;i<repeated.audio_event_count;++i)
                if(repeated.audio_events[i].song_id==128u){
                    CHECK(repeated.audio_events[i].player==4u && repeated.audio_events[i].pitch==-777);
                    ++cues;
                }
            CHECK(cues==1u && !retained->flight_audio_pending);
        }
    }

    /* Two ordinary shell uses keep the first actor moving and visible while
     * the new shell is held, on both course contact profiles. */
    for(unsigned profile=0u;profile<2u;++profile){
        const uint32_t course_before=race.rules.compatibility.course_id;
        race.rules.compatibility.course_id=profile?25u:24u;
        PortableGpRaceSession repeated;
        CHECK(test_gp_init(&repeated,&race,7u)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(portable_gp_race_session_tick(&repeated,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
              portable_gp_race_session_tick(&repeated,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        for(unsigned i=1u;i<8u;++i){
            repeated.roster.racers[i].position_x_q16=(1400+(int32_t)i*20)*65536;
            repeated.roster.racers[i].position_y_q16=(1400+(int32_t)i*20)*65536;
        }
        PortableGpRacer *local=&repeated.roster.racers[0];
        local->item_inventory.held_item_17a=0x1000u;
        CHECK(portable_gp_race_session_tick(&repeated,PORTABLE_INPUT_ITEM,&disposition)==0);
        CHECK(portable_gp_race_session_tick(&repeated,0u,&disposition)==0);
        for(unsigned tick=0u;tick<16u;++tick)CHECK(portable_gp_race_session_tick(&repeated,0u,&disposition)==0);
        CHECK(local->green_shell.occupied && local->green_shell.phase==PORTABLE_GP_GREEN_SHELL_RELEASED &&
              !(local->source_flags&0x10000u));
        const uint16_t age=local->green_shell.animation_tick_4c;
        local->item_inventory.held_item_17a=0x1000u;
        CHECK(portable_gp_race_session_tick(&repeated,PORTABLE_INPUT_ITEM,&disposition)==0);
        CHECK(local->green_shell.phase==PORTABLE_GP_GREEN_SHELL_ACTIVE &&
              repeated.roster.green_pool[0].shell.animation_tick_4c==age+1u &&
              portable_gp_item_pool_count(&repeated.roster)==2u);
        CHECK(portable_gp_race_session_snapshot(&repeated,&snapshot)==0 &&
              portable_gp_race_presentation_build(&repeated,&snapshot,&presentation_state) &&
              presentation_state.green_shells[0].visible && presentation_state.green_shells[8].visible);
        PortableGpGreenShell *old=&repeated.roster.green_pool[0].shell;
        old->position_x_q16=repeated.player.pose.position_x;
        old->position_y_q16=repeated.player.pose.position_y;old->lifetime_38=0u;
        old->velocity_x_q16=0;old->velocity_y_q16=0;
        CHECK(portable_gp_race_session_tick(&repeated,PORTABLE_INPUT_ITEM,&disposition)==0);
        CHECK(old->phase==PORTABLE_GP_GREEN_SHELL_DESTROYING && repeated.roster.green_shell_hits==1u &&
              repeated.player.normal_collision_timer_38>0u);
        CHECK(portable_gp_race_session_tick(&repeated,PORTABLE_INPUT_ITEM,&disposition)==0 && old->position_z_q16>0);
        race.rules.compatibility.course_id=course_before;
    }

    /* Ordinary held input reaches446FC for either shell. A rival entering
     * the attached actor damages once, retires17C/18C, and permits another
     * item while the consumed shell finishes its destruction animation. */
    for(unsigned red=0u;red<2u;++red){
        PortableGpRaceSession held;
        CHECK(test_gp_init(&held,&race,7u)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(portable_gp_race_session_tick(&held,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
              portable_gp_race_session_tick(&held,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        for(unsigned i=1u;i<8u;++i){
            held.roster.racers[i].position_x_q16=(1400+(int32_t)i*20)*65536;
            held.roster.racers[i].position_y_q16=(1400+(int32_t)i*20)*65536;
        }
        PortableGpRacer *local=&held.roster.racers[0];
        local->item_inventory.held_item_17a=red?PORTABLE_GP_RED_SHELL_ITEM:PORTABLE_GP_GREEN_SHELL_ITEM;
        CHECK(portable_gp_race_session_tick(&held,PORTABLE_INPUT_ITEM,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
              local->item_use_state_17c==1u && local->shell_item_18c!=0u);
        held.roster.racers[1].position_x_q16=red?local->red_shell.position_x_q16:local->green_shell.position_x_q16;
        held.roster.racers[1].position_y_q16=red?local->red_shell.position_y_q16:local->green_shell.position_y_q16;
        const PortableGpRaceSessionStatus held_status=portable_gp_race_session_tick(&held,PORTABLE_INPUT_ITEM,&disposition);
        CHECK(held_status==PORTABLE_GP_RACE_SESSION_OK &&
              (red?held.roster.red_shell_hits:held.roster.green_shell_hits)==1u &&
              local->item_use_state_17c==0u && local->shell_item_18c==0u &&
              held.roster.racers[1].banana_hit_timer_38==7u &&
              held.audio_event_count==1u && held.audio_events[0].song_id==132u);
        CHECK(portable_gp_race_session_tick(&held,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
              (red?held.roster.red_shell_hits:held.roster.green_shell_hits)==1u);
        local->item_inventory.held_item_17a=0x100au;
        CHECK(portable_gp_race_session_tick(&held,PORTABLE_INPUT_ITEM,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
              local->lightning_status_6a!=0 && held.roster.item_uses==2u);
    }

    /* Ordinary item input creates an attached banana, its rival contact
     * clears18C without a shell counter, and another item can be used. */
    {
        PortableGpRaceSession held;
        CHECK(test_gp_init(&held,&race,7u)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(portable_gp_race_session_tick(&held,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
              portable_gp_race_session_tick(&held,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        for(unsigned i=1u;i<8u;++i){
            held.roster.racers[i].position_x_q16=(1400+(int32_t)i*20)*65536;
            held.roster.racers[i].position_y_q16=(1400+(int32_t)i*20)*65536;
        }
        PortableGpRacer *local=&held.roster.racers[0];
        local->item_inventory.held_item_17a=PORTABLE_GP_BANANA_ITEM;
        CHECK(portable_gp_race_session_tick(&held,PORTABLE_INPUT_ITEM,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
              local->banana.phase==PORTABLE_GP_BANANA_HELD && local->shell_item_18c==0x1005u && local->item_use_state_17c==0u);
        held.roster.racers[1].position_x_q16=local->banana.position_x_q16;
        held.roster.racers[1].position_y_q16=local->banana.position_y_q16;
        CHECK(portable_gp_race_session_tick(&held,PORTABLE_INPUT_ITEM,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
              local->banana.phase==PORTABLE_GP_BANANA_DESTROYING && local->shell_item_18c==0u &&
              (held.roster.racers[1].mushroom_response_flags_b4&0x200u));
        CHECK(held.audio_event_count==1u && held.audio_events[0].player==4u);
        local->item_inventory.held_item_17a=0x100au;
        CHECK(portable_gp_race_session_tick(&held,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
              portable_gp_race_session_tick(&held,PORTABLE_INPUT_ITEM,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
              local->lightning_status_6a!=0 && held.roster.item_uses==2u);
    }

    /* Up/item is ordinary input: release constructs the forward flight,
     * exports immutable height and lands through the course terrain owner. */
    for(unsigned profile=0u;profile<2u;++profile){
        const uint32_t course_before=race.rules.compatibility.course_id;
        race.rules.compatibility.course_id=profile?25u:24u;
        PortableGpRaceSession thrown;
        CHECK(test_gp_init(&thrown,&race,7u)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(portable_gp_race_session_tick(&thrown,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
              portable_gp_race_session_tick(&thrown,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        for(unsigned i=1u;i<8u;++i){
            thrown.roster.racers[i].position_x_q16=(1400+(int32_t)i*20)*65536;
            thrown.roster.racers[i].position_y_q16=(1400+(int32_t)i*20)*65536;
        }
        PortableGpRacer *local=&thrown.roster.racers[0];local->item_inventory.held_item_17a=0x1005u;
        CHECK(portable_gp_race_session_tick(&thrown,PORTABLE_INPUT_ITEM,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
              local->banana.phase==PORTABLE_GP_BANANA_HELD);
        CHECK(portable_gp_race_session_tick(&thrown,PORTABLE_INPUT_UP,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
              local->banana.phase==PORTABLE_GP_BANANA_THROWN && local->banana.collision_guard_38==test_banana_rules()->collision_grace_ticks &&
              local->banana.angle_26==(uint16_t)thrown.player.steering.steering_offset &&
              thrown.audio_event_count==1u && thrown.audio_events[0].song_id==123u);
        CHECK(portable_gp_race_session_snapshot(&thrown,&snapshot)==PORTABLE_GP_RACE_SESSION_OK &&
              portable_gp_race_presentation_build(&thrown,&snapshot,&presentation_state) &&
              presentation_state.bananas[0].visible && presentation_state.bananas[0].z_q16==65536);
        unsigned flight_ticks=1u;
        while(INT64_C(65536)+(int64_t)flight_ticks*test_banana_rules()->throw_velocity_q16-
            (int64_t)flight_ticks*(flight_ticks-1u)*test_banana_rules()->throw_gravity_q16/2>0)
            ++flight_ticks;
        for(unsigned tick=1u;tick<=flight_ticks;++tick){
            CHECK(portable_gp_race_session_tick(&thrown,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(((local->source_flags&0x10000u)!=0u)==(tick<test_banana_rules()->release_lock_ticks));
        }
        CHECK(local->banana.phase==PORTABLE_GP_BANANA_DROPPED && local->banana.position_z_q16==0 &&
              local->banana.collision_guard_38==0u && !(local->source_flags&0x10000u) &&
              thrown.audio_event_count==1u && thrown.audio_events[0].song_id==122u);
        /* Ordinary Item after the flight lock retains the first live banana.
         * The retained actor keeps its own ballistic clock and landing. */
        local->item_inventory.held_item_17a=PORTABLE_GP_BANANA_ITEM;
        CHECK(portable_gp_race_session_tick(&thrown,PORTABLE_INPUT_ITEM,&disposition)==0 &&
            local->banana.phase==PORTABLE_GP_BANANA_HELD && thrown.roster.banana_pool[0].owner==0u &&
            thrown.roster.banana_pool[0].banana.phase==PORTABLE_GP_BANANA_DROPPED);
        CHECK(portable_gp_race_session_snapshot(&thrown,&snapshot)==0 &&
            portable_gp_race_presentation_build(&thrown,&snapshot,&presentation_state) &&
            presentation_state.bananas[0].visible && presentation_state.bananas[8].visible);
        PortableGpBanana *retained=&thrown.roster.banana_pool[0].banana;
        retained->phase=PORTABLE_GP_BANANA_THROWN;retained->position_z_q16=1;
        retained->vertical_velocity_q16=-1;retained->collision_guard_38=2u;
        CHECK(portable_gp_race_session_tick(&thrown,PORTABLE_INPUT_ITEM,&disposition)==0 &&
            retained->phase==PORTABLE_GP_BANANA_DROPPED && retained->collision_guard_38==1u &&
            local->banana.phase==PORTABLE_GP_BANANA_HELD);
        retained->position_x_q16=1500*65536;retained->position_y_q16=1500*65536;
        thrown.roster.racers[1].position_x_q16=1500*65536;
        thrown.roster.racers[1].position_y_q16=1500*65536;
        const unsigned retained_hit_status=portable_gp_race_session_tick(&thrown,PORTABLE_INPUT_ITEM,&disposition);
        CHECK(retained_hit_status==0 &&
            retained->phase==PORTABLE_GP_BANANA_DESTROYING && thrown.roster.banana_hits==1u &&
            thrown.roster.racers[1].banana_hit_timer_38==2u && local->banana.phase==PORTABLE_GP_BANANA_HELD);
        const int32_t retained_height=retained->position_z_q16;
        CHECK(portable_gp_race_session_tick(&thrown,PORTABLE_INPUT_ITEM,&disposition)==0 &&
            retained->position_z_q16>retained_height && local->banana.phase==PORTABLE_GP_BANANA_HELD);
        race.rules.compatibility.course_id=course_before;
    }

    /* CPU red actors attach, hold and drop without local input, appear in
     * snapshots, and hit the authoritative player on both collision profiles. */
    for(unsigned profile=0u;profile<4u;++profile){
        const uint32_t previous_course=race.rules.compatibility.course_id;
        race.rules.compatibility.course_id=(profile&1u)?25u:24u;
        PortableGpRaceSession cpu;
        CHECK(test_gp_init(&cpu,&race,7u)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(portable_gp_race_session_tick(&cpu,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
              portable_gp_race_session_tick(&cpu,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        for(unsigned i=1u;i<8u;++i){
            cpu.roster.racers[i].position_x_q16=(1400+(int32_t)i*20)*65536;
            cpu.roster.racers[i].position_y_q16=(1400+(int32_t)i*20)*65536;
        }
        PortableGpRacer *owner=&cpu.roster.racers[1];
        owner->item_inventory.held_item_17a=profile>=2u?0x1004u:0x1002u;owner->item_decision_190=1u;
        CHECK(portable_gp_race_roster_use_red_shell(&cpu.roster,1u,sine,1024u)==PORTABLE_GP_ROSTER_OK);
        CHECK(portable_gp_race_session_tick(&cpu,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(owner->red_shell.phase==PORTABLE_GP_RED_SHELL_ACTIVE && owner->shell_item_18c==(profile>=2u?0x1004u:0x1002u));
        CHECK(portable_gp_race_session_tick(&cpu,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(owner->red_shell.animation_tick_4c==1u);
        CHECK(portable_gp_race_session_snapshot(&cpu,&snapshot)==PORTABLE_GP_RACE_SESSION_OK &&
              portable_gp_race_presentation_build(&cpu,&snapshot,&presentation_state) &&
              presentation_state.red_shells[1].visible &&
              presentation_state.red_shells[1].kind==(profile>=2u?0x8403u:0x8402u));
        owner->item_decision_190=2u;
        CHECK(portable_gp_race_session_tick(&cpu,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(owner->red_shell.phase==PORTABLE_GP_RED_SHELL_RELEASED_BACKWARD && owner->shell_item_18c==0u);
        CHECK(cpu.audio_event_count==1u && cpu.audio_events[0].song_id==134u && cpu.audio_events[0].volume>=64u);
        owner->red_shell.phase=PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET;
        owner->red_shell.position_x_q16=cpu.player.pose.position_x;
        owner->red_shell.position_y_q16=cpu.player.pose.position_y;
        owner->red_shell.position_z_q16=0;owner->red_shell.lifetime_38=0u;
        owner->red_shell.collision_active=1u;owner->red_shell.target_valid=0u;
        if(profile>=2u){
            const unsigned leader=cpu.roster.standings[0];
            const unsigned player_rank=cpu.roster.racers[0].rank_flags_ba>>1u;
            cpu.roster.racers[leader].rank_flags_ba=(uint16_t)(player_rank*2u);
            cpu.roster.racers[0].rank_flags_ba=0u;
            cpu.roster.standings[player_rank]=(uint8_t)leader;cpu.roster.standings[0]=0u;
            owner->red_shell.phase=PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_TARGET;
            owner->red_shell.target_valid=1u;owner->red_shell.target_racer_index=0u;
        }
        const PortableGpRaceSessionStatus hit_status=portable_gp_race_session_tick(&cpu,0u,&disposition);
        if(hit_status)fprintf(stderr,"Spiny fixture profile=%u status=%u stage=%u\n",profile,hit_status,cpu.failed_roster_stage);
        CHECK(hit_status==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(owner->red_shell.phase==PORTABLE_GP_RED_SHELL_DESTROYING_ROUTE && cpu.roster.red_shell_hits==1u &&
              cpu.player.normal_collision_timer_38>0u);
        CHECK(portable_gp_race_session_tick(&cpu,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
              owner->red_shell.position_z_q16>0);
        race.rules.compatibility.course_id=previous_course;
    }
    /* Ordinary local item input constructs, holds and releases Spiny art. */
    {
        PortableGpRaceSession local;
        CHECK(test_gp_init(&local,&race,7u)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(portable_gp_race_session_tick(&local,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
              portable_gp_race_session_tick(&local,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        for(unsigned i=1u;i<8u;++i){
            local.roster.racers[i].position_x_q16=(1400+(int32_t)i*20)*65536;
            local.roster.racers[i].position_y_q16=(1400+(int32_t)i*20)*65536;
        }
        PortableGpRacer *owner=&local.roster.racers[0];
        owner->item_inventory.held_item_17a=PORTABLE_GP_SPINY_SHELL_ITEM;
        CHECK(portable_gp_race_session_tick(&local,PORTABLE_INPUT_ITEM,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(owner->red_shell.kind_10==PORTABLE_GP_SPINY_SHELL_KIND && owner->shell_item_18c==0x1004u);
        CHECK(portable_gp_race_session_tick(&local,PORTABLE_INPUT_ITEM,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(owner->red_shell.phase==PORTABLE_GP_RED_SHELL_ACTIVE);
        CHECK(portable_gp_race_session_snapshot(&local,&snapshot)==PORTABLE_GP_RACE_SESSION_OK &&
              portable_gp_race_presentation_build(&local,&snapshot,&presentation_state) &&
              presentation_state.red_shells[0].kind==0x8403u && presentation_state.red_shells[0].visible);
        CHECK(portable_gp_race_session_tick(&local,PORTABLE_INPUT_DOWN,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(owner->red_shell.phase==PORTABLE_GP_RED_SHELL_RELEASED_BACKWARD && owner->shell_item_18c==0u);
    }
    /* CPU-owned actors use the same motion, immutable presentation and
     * local-victim handoff on the shared and late-overlap course profiles. */
    for(unsigned profile=0u;profile<2u;++profile){
        const uint32_t course_before=race.rules.compatibility.course_id;
        race.rules.compatibility.course_id=profile?25u:24u;
        PortableGpRaceSession cpu;
        CHECK(test_gp_init(&cpu,&race,7u)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(portable_gp_race_session_tick(&cpu,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
              portable_gp_race_session_tick(&cpu,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        for(unsigned i=1u;i<8u;++i){
            cpu.roster.racers[i].position_x_q16=(1400+(int32_t)i*20)*65536;
            cpu.roster.racers[i].position_y_q16=(1400+(int32_t)i*20)*65536;
        }
        PortableGpRacer *owner=&cpu.roster.racers[1];
        owner->item_inventory.held_item_17a=0x1005u;owner->item_decision_190=1u;
        CHECK(portable_gp_race_roster_use_banana(&cpu.roster,1u,sine,1024u)==PORTABLE_GP_ROSTER_OK);
        const uint16_t age=owner->banana.attachment_ticks_4c;
        CHECK(portable_gp_race_session_tick(&cpu,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(owner->banana.phase==PORTABLE_GP_BANANA_HELD && owner->banana.attachment_ticks_4c==age+1u);
        CHECK(portable_gp_race_session_snapshot(&cpu,&snapshot)==PORTABLE_GP_RACE_SESSION_OK &&
              portable_gp_race_presentation_build(&cpu,&snapshot,&presentation_state) &&
              presentation_state.bananas[1].visible &&
              presentation_state.bananas[1].x_q16==owner->banana.position_x_q16);
        owner->banana.phase=PORTABLE_GP_BANANA_DROPPED;owner->banana.collision_active=1u;
        owner->banana.position_x_q16=cpu.player.pose.position_x;
        owner->banana.position_y_q16=cpu.player.pose.position_y;
        owner->banana.position_z_q16=0;owner->shell_item_18c=0u;
        CHECK(portable_gp_race_session_tick(&cpu,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(owner->banana.phase==PORTABLE_GP_BANANA_DESTROYING && cpu.roster.banana_hits==1u &&
              cpu.roster.racers[0].banana_hit_timer_38==3u && cpu.player.normal_collision_timer_38==3u);
        const int32_t height=owner->banana.position_z_q16;
        CHECK(portable_gp_race_session_tick(&cpu,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
              owner->banana.position_z_q16>height);
        race.rules.compatibility.course_id=course_before;
    }

    /* Returning to one's dropped banana queues the speed-dependent local
     * reaction, retains coins and enters the existing A9AC ordinary spin. */
    {
        PortableGpRaceSession hit;
        CHECK(test_gp_init(&hit,&race,7u)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(portable_gp_race_session_tick(&hit,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
              portable_gp_race_session_tick(&hit,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        for(unsigned i=1u;i<8u;++i){
            hit.roster.racers[i].position_x_q16=(1400+(int32_t)i*20)*65536;
            hit.roster.racers[i].position_y_q16=(1400+(int32_t)i*20)*65536;
        }
        hit.roster.racers[0].banana=(PortableGpBanana){.rules=test_banana_rules(),.occupied=1u,.flags_34=3u,
            .kind_10=0x8001u,.phase=PORTABLE_GP_BANANA_DROPPED,.collision_active=1u,
            .position_x_q16=hit.player.pose.position_x,.position_y_q16=hit.player.pose.position_y};
        const uint16_t coins=hit.roster.racers[0].coin_count_178;
        CHECK(portable_gp_race_session_tick(&hit,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
              hit.player.normal_collision_timer_38==3u && hit.audio_event_count==1u &&
              hit.audio_events[0].song_id==135u && hit.audio_events[0].pitch==-512 &&
              hit.roster.racers[0].coin_count_178==coins);
        const PortableGpRaceSessionStatus banana_next=portable_gp_race_session_tick(&hit,0u,&disposition);
        CHECK(banana_next==PORTABLE_GP_RACE_SESSION_OK &&
              (hit.player.steering.response_status==5u || hit.player.steering.response_status==6u) &&
              hit.player.normal_collision_timer_38==2u);
        for(unsigned tick=0u;tick<150u;++tick)
            CHECK(portable_gp_race_session_tick(&hit,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(hit.player.normal_collision_timer_38==0u && hit.player.steering.response_status==0u &&
              !hit.roster.racers[0].banana.occupied && hit.roster.racers[0].coin_count_178==coins);
    }

    /* Down remains an independent source direction bit even though the macOS
     * keyboard also treats that arrow as brake.  Releasing C+Down therefore
     * selects the already-qualified backward red-shell initializer. */
    CHECK(test_gp_init(&item_session, &race, 7u) ==
          PORTABLE_GP_RACE_SESSION_OK);
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK);
    for (size_t index = 1u; index < PORTABLE_GP_RACER_COUNT; ++index)
    {
        item_session.roster.racers[index].position_x_q16 =
            (INT32_C(1400) + (int32_t)index * 20) << 16u;
        item_session.roster.racers[index].position_y_q16 =
            (INT32_C(1400) + (int32_t)index * 20) << 16u;
    }
    item_session.roster.racers[0].item_inventory.held_item_17a =
        PORTABLE_GP_RED_SHELL_ITEM;
    item_session.roster.racers[0].item_inventory.settled_item =
        PORTABLE_GP_RED_SHELL_ITEM;
    CHECK(portable_gp_race_session_tick(
              &item_session, PORTABLE_INPUT_ITEM, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          item_session.roster.racers[0].red_shell.phase ==
              PORTABLE_GP_RED_SHELL_ACTIVE);
    CHECK(portable_gp_race_session_tick(
              &item_session, PORTABLE_INPUT_ITEM, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK);
    CHECK(portable_gp_race_session_tick(
              &item_session, PORTABLE_INPUT_DOWN, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          (item_session.roster.racers[0].control & UINT16_C(0x0400)) != 0u &&
          item_session.roster.racers[0].red_shell.phase ==
              PORTABLE_GP_RED_SHELL_RELEASED_BACKWARD &&
          item_session.roster.racers[0].red_shell.angle_26 ==
              UINT16_C(0x8000));

    CHECK(item_session.audio_event_count!=0u &&
          item_session.audio_events[item_session.audio_event_count-1u].song_id==134u);
    const int32_t trap_x=item_session.roster.racers[0].red_shell.position_x_q16;
    const int32_t trap_y=item_session.roster.racers[0].red_shell.position_y_q16;
    for(unsigned tick=1u;tick<=test_red_shell_rules()->trap_arm_ticks;++tick){
        for(unsigned i=1u;i<8u;++i){
            item_session.roster.racers[i].position_x_q16=(1400+(int32_t)i*20)*65536;
            item_session.roster.racers[i].position_y_q16=(1400+(int32_t)i*20)*65536;
        }
        CHECK(portable_gp_race_session_tick(&item_session,0u,&disposition)==0);
        CHECK(item_session.roster.racers[0].red_shell.position_x_q16==trap_x &&
              item_session.roster.racers[0].red_shell.position_y_q16==trap_y);
    }
    CHECK(item_session.roster.racers[0].red_shell.phase==PORTABLE_GP_RED_SHELL_TRAP_ARMED);
    CHECK(portable_gp_race_session_tick(&item_session,0u,&disposition)==0);
    CHECK(item_session.roster.racers[0].red_shell.phase==PORTABLE_GP_RED_SHELL_TRAP_ARMED);
    item_session.player.pose.position_x=trap_x+8*65536;
    item_session.player.pose.position_y=trap_y-8*65536;
    CHECK(portable_gp_race_session_tick(&item_session,0u,&disposition)==0);
    CHECK(item_session.roster.racers[0].red_shell.phase==PORTABLE_GP_RED_SHELL_TRAP_LOCKING &&
          item_session.roster.racers[0].red_shell.target_racer_index==0u &&
          item_session.audio_event_count!=0u);
    for(unsigned tick=0u;tick<test_red_shell_rules()->trap_lock_ticks;++tick)
        CHECK(portable_gp_race_session_tick(&item_session,0u,&disposition)==0);
    CHECK(item_session.roster.racers[0].red_shell.phase==PORTABLE_GP_RED_SHELL_TRAP_CHASING);
    item_session.roster.racers[0].coin_count_178=9u;
    CHECK(portable_gp_race_session_tick(&item_session,0u,&disposition)==0);
    CHECK(item_session.roster.red_shell_hits==1u &&
          item_session.roster.racers[0].red_shell.phase==PORTABLE_GP_RED_SHELL_DESTROYING_ROUTE &&
          item_session.roster.racers[0].coin_count_178==5u &&
          item_session.player.normal_collision_timer_38==8u &&
          (item_session.player.steering.state_flags&0x1000u)!=0u &&
          (item_session.player.steering.response_flags&0x200u)!=0u &&
          (item_session.player.motion.component_x!=0||item_session.player.motion.component_y!=0));
    CHECK(item_session.roster.racers[0].red_shell.position_x_q16!=trap_x ||
          item_session.roster.racers[0].red_shell.position_y_q16!=trap_y);
    CHECK(portable_gp_race_session_snapshot(&item_session,&snapshot)==0 &&
          portable_gp_race_presentation_build(&item_session,&snapshot,&presentation_state) &&
          presentation_state.red_shells[0].visible);
    CHECK(portable_gp_race_session_tick(&item_session,0u,&disposition)==0);
    CHECK(item_session.player.normal_collision_timer_38==7u &&
          item_session.player.steering.driver_mode==10u);
    for(unsigned tick=0u;tick<150u;++tick){
        for(unsigned i=1u;i<8u;++i){
            item_session.roster.racers[i].position_x_q16=(1400+(int32_t)i*20)*65536;
            item_session.roster.racers[i].position_y_q16=(1400+(int32_t)i*20)*65536;
        }
        CHECK(portable_gp_race_session_tick(&item_session,PORTABLE_INPUT_ACCELERATE,&disposition)==0);
    }
    CHECK(item_session.player.normal_collision_timer_38==0u &&
          (item_session.player.steering.response_flags&0x200u)==0u &&
          item_session.player.steering.driver_mode!=10u);

    /* Green shells use the same recovered collision-manager AABB and racer
     * response, while retaining their own sub_80445CC actor owner. */
    for(unsigned green_index=0u;green_index<3u;green_index+=2u){
    CHECK(test_gp_init(&item_session, &race, 7u) ==
          PORTABLE_GP_RACE_SESSION_OK);
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK);
    for (size_t index = 1u; index < PORTABLE_GP_RACER_COUNT; ++index)
    {
        item_session.roster.racers[index].position_x_q16 =
            (INT32_C(2000) + (int32_t)index * 20) << 16u;
        item_session.roster.racers[index].position_y_q16 =
            (INT32_C(2000) + (int32_t)index * 20) << 16u;
    }
    item_session.roster.racers[1].position_x_q16 = INT32_C(1500) << 16u;
    item_session.roster.racers[1].position_y_q16 = INT32_C(1500) << 16u;
    item_session.roster.racers[green_index].source_flags |= UINT32_C(0x00010000);
    item_session.roster.racers[green_index].green_shell = (PortableGpGreenShell){.rules=test_green_shell_rules(),.orbit_rules=test_shell_orbit_rules(),
        .position_x_q16 = INT32_C(1500) << 16u,
        .position_y_q16 = (INT32_C(1500) << 16u) + INT32_C(0x8000),
        .kind_10 = PORTABLE_GP_GREEN_SHELL_KIND,
        .phase = PORTABLE_GP_GREEN_SHELL_RELEASED,
        .occupied = 1u,
        .lifetime_38 = 1u,
        .state_71 = 48u,
    };
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          item_session.roster.green_shell_hits == 1u &&
          item_session.roster.racers[green_index].green_shell.phase ==
              PORTABLE_GP_GREEN_SHELL_DESTROYING &&
          (item_session.roster.racers[1].mushroom_response_flags_b4 &
              UINT16_C(0x0200)) != 0u &&
          item_session.audio_event_count == 1u &&
          item_session.audio_events[0].song_id == 132u);
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          item_session.audio_event_count == 0u &&
          item_session.roster.racers[1].hit_state_7e == 13u &&
          item_session.roster.racers[green_index].green_shell.position_z_q16 > 0);
    CHECK(portable_gp_race_presentation_build(&item_session,&snapshot,&presentation_state) &&
          presentation_state.green_shells[green_index].visible &&
          presentation_state.green_shells[green_index].z_q16>0);
    }

    /* The production item actor now reaches the source collision manager's
     * (sizeA+sizeB)<<14 AABB, invokes the exact red-shell kart callback, and
     * queues the source victim132 sound;445CC plays124 for other items. */
    CHECK(test_gp_init(&item_session, &race, 7u) ==
          PORTABLE_GP_RACE_SESSION_OK);
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK);
    for (size_t index = 1u; index < PORTABLE_GP_RACER_COUNT; ++index)
    {
        item_session.roster.racers[index].position_x_q16 =
            (INT32_C(2000) + (int32_t)index * 20) << 16u;
        item_session.roster.racers[index].position_y_q16 =
            (INT32_C(2000) + (int32_t)index * 20) << 16u;
    }
    item_session.roster.racers[1].position_x_q16 = INT32_C(1500) << 16u;
    item_session.roster.racers[1].position_y_q16 = INT32_C(1500) << 16u;
    item_session.roster.racers[0].source_flags |= UINT32_C(0x00010000);
    item_session.roster.racers[0].red_shell = (PortableGpRedShell){.rules=test_red_shell_rules(),.orbit_rules=test_shell_orbit_rules(),
        .position_x_q16 = INT32_C(1500) << 16u,
        .position_y_q16 = (INT32_C(1500) << 16u) + INT32_C(0x8000),
        .kind_10 = PORTABLE_GP_RED_SHELL_KIND,
        .angle_26 = 0u,
        .phase = PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET,
        .occupied = 1u,
        .lifetime_38 = 1u,
        .collision_active = 1u,
    };
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          item_session.roster.red_shell_hits == 1u &&
          item_session.roster.racers[0].red_shell.phase ==
              PORTABLE_GP_RED_SHELL_DESTROYING_ROUTE &&
          (item_session.roster.racers[1].mushroom_response_flags_b4 &
              UINT16_C(0x0200)) != 0u &&
          item_session.audio_event_count == 1u &&
          item_session.audio_events[0].song_id == 132u);
    CHECK(portable_gp_race_session_snapshot(&item_session, &snapshot) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          snapshot.audio_event_count == 1u &&
          snapshot.audio_events[0].player == 4u &&
          snapshot.audio_events[0].volume == UINT16_C(0x0100));
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          item_session.audio_event_count == 0u &&
          item_session.roster.racers[1].hit_state_7e == 13u &&
          item_session.roster.racers[0].red_shell.position_z_q16 > 0);

    /* A dropped banana is passive in the same source collision manager. The
     * retained rival +0x2000 flag selects the source special response and its
     * driver-class cue; the ordinary player-victim route remains in the same
     * roster callback for future opponent-owned actors. */
    CHECK(test_gp_init(&item_session, &race, 7u) ==
          PORTABLE_GP_RACE_SESSION_OK);
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK);
    for (size_t index = 1u; index < PORTABLE_GP_RACER_COUNT; ++index)
    {
        item_session.roster.racers[index].position_x_q16 =
            (INT32_C(2000) + (int32_t)index * 20) << 16u;
        item_session.roster.racers[index].position_y_q16 =
            (INT32_C(2000) + (int32_t)index * 20) << 16u;
    }
    item_session.roster.racers[1].position_x_q16 = INT32_C(1500) << 16u;
    item_session.roster.racers[1].position_y_q16 = INT32_C(1500) << 16u;
    item_session.roster.racers[0].source_flags |= UINT32_C(0x00010000);
    item_session.roster.racers[0].banana = (PortableGpBanana){.rules=test_banana_rules(),
        .position_x_q16 = INT32_C(1500) << 16u,
        .position_y_q16 = INT32_C(1500) << 16u,
        .kind_10 = PORTABLE_GP_BANANA_KIND,
        .flags_34 = 3u,
        .phase = PORTABLE_GP_BANANA_DROPPED,
        .occupied = 1u,
        .collision_active = 1u,
    };
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          item_session.roster.racers[0].banana.phase ==
              PORTABLE_GP_BANANA_DESTROYING &&
          (item_session.roster.racers[1].mushroom_response_flags_b4 &
              UINT16_C(0x0200)) != 0u &&
          item_session.audio_event_count == 1u &&
          item_session.audio_events[0].player == 4u);
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          item_session.roster.racers[1].hit_state_7e == 13u &&
          item_session.roster.racers[0].banana.position_z_q16 > 0);

    /* Coins are ground-entry map actions, not radius pickups. A full
     * inventory leaves the tile intact; landing waits one more driver turn. */
    {
        const PortableRecoveredCoin pickups[2] = {
            {.position_x_q16=INT32_C(908)*65536,.position_y_q16=INT32_C(788)*65536},
            {.position_x_q16=INT32_C(908)*65536,.position_y_q16=INT32_C(788)*65536}};
        const size_t cell=(788u/8u)*256u+908u/8u;
        race.coins=pickups; race.coin_count=2u;
        race.dependencies.owned_terrain_cells=terrain_cells;
        race.dependencies.owned_terrain_capacity=sizeof terrain_cells;
        material_flags[0xc0]=0xc0u; material_flags[0xc1]=0x40u;
        for (unsigned gate=0u; gate<3u; ++gate) {
            CHECK(test_gp_init(&item_session,&race,7u)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(terrain_cells[cell]==0xc0u && item_session.coins[0].active);
            CHECK(portable_gp_race_session_tick(&item_session,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(portable_gp_race_session_tick(&item_session,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(item_session.coins[0].active);
            for (unsigned i=1u;i<8u;++i) {
                item_session.roster.racers[i].position_x_q16=(1400+(int32_t)i*20)*65536;
                item_session.roster.racers[i].position_y_q16=(1400+(int32_t)i*20)*65536;
            }
            if (gate==0u) item_session.roster.racers[0].coin_count_178=99u;
            if (gate==1u) {
                item_session.player.hop.mode=1u;
                item_session.player.hop.height_q16=item_session.player.pose.position_z=65536;
                item_session.player.hop.velocity_q8=-256;
            }
            if (gate==2u) item_session.player.steering.state_flags|=UINT32_C(0x2000);
            CHECK(portable_gp_race_session_tick(&item_session,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(item_session.coins[0].active && terrain_cells[cell]==0xc0u &&
                  item_session.roster.coin_events==0u && item_session.audio_event_count==0u);
            const int32_t old_speed_target=item_session.player.input.speed_target_q16;
            if (gate==0u) item_session.roster.racers[0].coin_count_178=98u;
            if (gate==1u) CHECK(item_session.player.hop.mode==0u);
            if (gate==2u) item_session.player.steering.state_flags&=~UINT32_C(0x2000);
            CHECK(portable_gp_race_session_tick(&item_session,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(!item_session.coins[0].active && !item_session.coins[1].active &&
                  terrain_cells[cell]==0xc1u &&
                  item_session.roster.coin_events==1u &&
                  item_session.roster.racers[0].coin_count_178==(gate==0u?99u:6u) &&
                  item_session.audio_event_count==1u && item_session.audio_events[0].song_id==101u &&
                  item_session.audio_events[0].player==7u);
            CHECK(item_session.coin_pickups[0].age==1u &&
                item_session.coin_pickups[0].upward_speed_q16==0x80000 &&
                item_session.coin_pickups[0].x==124-(int)(item_session.source_frame_clock&7u));
            PortableCoinPickupEffect effect=item_session.coin_pickups[0];
            const int32_t birth_y=effect.y_q16;
            for(unsigned age=2u;age<=28u;++age){
                portable_coin_pickup_effect_tick(&effect);
                const int32_t n=(int32_t)age-1;
                CHECK(effect.age==age && effect.y_q16==birth_y-n*0x80000+n*(n-1)*0x4000);
                CHECK(effect.upward_speed_q16==0x80000-n*0x8000);
            }
            portable_coin_pickup_effect_tick(&effect);
            CHECK(effect.age==0u);
            if (gate!=0u) CHECK(item_session.player.input.speed_target_q16==old_speed_target+2*65536);
            CHECK(portable_gp_race_session_tick(&item_session,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(item_session.roster.coin_events==1u && item_session.audio_event_count==0u);
        }
        /* Retry re-installs placements in the same mutable course buffer. */
        CHECK(test_gp_init(&item_session,&race,7u)==PORTABLE_GP_RACE_SESSION_OK &&
              terrain_cells[cell]==0xc0u && item_session.coins[0].active);
        race.coins=NULL; race.coin_count=0u;
        race.dependencies.owned_terrain_cells=NULL;
        race.dependencies.owned_terrain_capacity=0u;
        terrain_cells[cell]=0u;
        material_flags[0xc0]=0u; material_flags[0xc1]=0u;
    }

    /* Single/triple boosts use the actual pilot's mode/CC, including refresh
     * and expiry. Holding the item button must not consume successors. */
    CHECK(test_gp_init(&item_session, &race, 7u) ==
          PORTABLE_GP_RACE_SESSION_OK);
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK);
    for (size_t index = 1u; index < PORTABLE_GP_RACER_COUNT; ++index)
    {
        item_session.roster.racers[index].position_x_q16 =
            (INT32_C(1400) + (int32_t)index * 20) << 16u;
        item_session.roster.racers[index].position_y_q16 =
            (INT32_C(1400) + (int32_t)index * 20) << 16u;
    }
    item_session.player_dependencies.mushroom_voice_songs[0] = 202u;
    item_session.player_dependencies.mushroom_voice_songs[1] = 282u;
    item_session.roster.racers[0].item_inventory.held_item_17a =
        UINT16_C(0x1007);
    item_session.roster.racers[0].item_inventory.settled_item =
        UINT16_C(0x1007);
    CHECK(portable_gp_race_session_tick(
              &item_session, PORTABLE_INPUT_ITEM, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          item_session.player.steering.driver_mode == 0u &&
          item_session.player.steering.exit_timer == 0u &&
          item_session.roster.racers[0].item_response_flags_b2 == UINT16_C(0x8000) &&
          item_session.roster.racers[0].item_inventory.held_item_17a == 0u);
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          item_session.roster.racers[0].mushroom_mode_84 == 8u &&
          item_session.player.motion.acceleration_q16 ==
              item_session.roster.racers[0].acceleration_q16 &&
          (uint32_t)item_session.player.motion.acceleration_q16 ==
              (uint32_t)test_mushroom_rules()->acceleration_units*65536u &&
          item_session.player.steering.exit_timer == test_mushroom_rules()->boost_ticks-1u &&
          item_session.player.boost_voice.song_id == 202u);
    for (unsigned boost_tick=0u;boost_tick+1u<test_mushroom_rules()->boost_ticks;++boost_tick)
        CHECK(portable_gp_race_session_tick(&item_session,0u,&disposition) ==
              PORTABLE_GP_RACE_SESSION_OK);
    CHECK(item_session.player.steering.driver_mode == 0u &&
          item_session.player.steering.exit_timer == 0u &&
          item_session.roster.racers[0].mushroom_timer_cc == 0u &&
          (item_session.player.steering.response_flags & UINT16_C(0xc0)) == 0u);
    /* The item dispatcher rejects airborne presses and does not synthesize
     * an edge on landing. The constructor needs a fresh ground press. */
    CHECK(portable_gp_race_session_tick(&item_session, PORTABLE_INPUT_HOP,
              &disposition) == PORTABLE_GP_RACE_SESSION_OK);
    CHECK(portable_gp_race_session_tick(&item_session, PORTABLE_INPUT_HOP,
              &disposition) == PORTABLE_GP_RACE_SESSION_OK);
    CHECK(item_session.player.hop.mode == 1u);
    item_session.roster.racers[0].item_inventory.held_item_17a = UINT16_C(0x1007);
    for (unsigned tick = 0u; tick < 25u; ++tick)
    {
        CHECK(portable_gp_race_session_tick(&item_session, PORTABLE_INPUT_ITEM,
                  &disposition) == PORTABLE_GP_RACE_SESSION_OK);
        CHECK(item_session.roster.racers[0].item_inventory.held_item_17a == UINT16_C(0x1007) &&
              item_session.roster.racers[0].item_response_flags_b2 == 0u);
    }
    CHECK(item_session.player.hop.mode == 0u);
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
          PORTABLE_GP_RACE_SESSION_OK);
    CHECK(portable_gp_race_session_tick(&item_session, PORTABLE_INPUT_ITEM,
              &disposition) == PORTABLE_GP_RACE_SESSION_OK);
    CHECK(item_session.roster.racers[0].item_inventory.held_item_17a == 0u &&
          item_session.roster.racers[0].item_response_flags_b2 == UINT16_C(0x8000));
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
          PORTABLE_GP_RACE_SESSION_OK);
    CHECK(item_session.player.steering.exit_timer == test_mushroom_rules()->boost_ticks-1u &&
          item_session.player.contact_audio.song_id == 125u);
    item_session.roster.racers[0].item_inventory.held_item_17a = UINT16_C(0x1008);
    const uint16_t successors[3] = {UINT16_C(0x100f),UINT16_C(0x1007),0u};
    for (unsigned use=0u;use<3u;++use) {
        CHECK(portable_gp_race_session_tick(&item_session,PORTABLE_INPUT_ITEM,&disposition) ==
              PORTABLE_GP_RACE_SESSION_OK);
        CHECK(item_session.roster.racers[0].item_response_flags_b2 == UINT16_C(0x8000) &&
              item_session.roster.racers[0].item_inventory.held_item_17a == successors[use]);
        CHECK(portable_gp_race_session_tick(&item_session,PORTABLE_INPUT_ITEM,&disposition) ==
              PORTABLE_GP_RACE_SESSION_OK);
        CHECK(item_session.player.steering.exit_timer == test_mushroom_rules()->boost_ticks-1u &&
              item_session.roster.racers[0].item_inventory.held_item_17a == successors[use]);
        CHECK(portable_gp_race_session_tick(&item_session,0u,&disposition) == PORTABLE_GP_RACE_SESSION_OK);
    }
    item_session.roster.racers[0].item_inventory.held_item_17a = UINT16_C(0x1009);
    CHECK(portable_gp_race_session_tick(&item_session,PORTABLE_INPUT_ITEM,&disposition) ==
          PORTABLE_GP_RACE_SESSION_OK);
    CHECK(item_session.roster.racers[0].item_response_flags_b2 == UINT16_C(0x8000) &&
          item_session.roster.racers[0].item_inventory.held_item_17a == UINT16_C(0x100e) &&
          item_session.roster.racers[0].golden_mushroom_ticks == test_mushroom_rules()->golden_actor_ticks-1u);
    for (unsigned golden_tick=0u;golden_tick+1u<test_mushroom_rules()->golden_actor_ticks;++golden_tick)
        CHECK(portable_gp_race_session_tick(&item_session,
            (golden_tick&1u)?PORTABLE_INPUT_ITEM:0u,&disposition) == PORTABLE_GP_RACE_SESSION_OK);
    CHECK(item_session.roster.racers[0].golden_mushroom_ticks == 0u &&
          item_session.roster.racers[0].item_inventory.held_item_17a == 0u);

    /* The pack's typed +0x94 item-box placement owns the production contact:
     * no test-only begin call supplies the roulette or selected item. */
    item_box = (PortableRecoveredCourseObject){
        .position_x_q16 = INT32_C(0x038c0000),
        .position_y_q16 = INT32_C(0x03140000),
        .kind = PORTABLE_GP_ITEM_BOX_KIND,
        .source_parameter = 9u,
        .collision_half_extent = 0x1eu,
    };
    race.course_objects = &item_box;
    race.course_object_count = 1u;
    CHECK(test_gp_init(&item_session, &race, 7u) ==
          PORTABLE_GP_RACE_SESSION_OK);
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK);
    for (size_t index = 1u; index < PORTABLE_GP_RACER_COUNT; ++index)
    {
        item_session.roster.racers[index].position_x_q16 =
            (INT32_C(1400) + (int32_t)index * 20) << 16u;
        item_session.roster.racers[index].position_y_q16 =
            (INT32_C(1400) + (int32_t)index * 20) << 16u;
    }
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          item_session.item_box_contacts == 1u &&
          !item_session.item_boxes[0].active &&
          item_session.audio_event_count==2u && item_session.audio_events[0].song_id==121u &&
          item_session.audio_events[0].player==6u && item_session.audio_events[1].song_id==103u &&
          item_session.audio_events[1].player==7u &&
          item_session.item_boxes[0].cooldown_ticks ==
              PORTABLE_GP_ITEM_BOX_RESPAWN_TICKS &&
          item_session.roster.racers[0].item_inventory.held_item_17a ==
              PORTABLE_GP_ITEM_ROULETTE);
    CHECK(portable_gp_race_session_snapshot(&item_session, &snapshot) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          portable_gp_race_presentation_build(
              &item_session, &snapshot, &presentation_state) &&
          presentation_state.item_box_count == 1u &&
          presentation_state.item_boxes[0].position_x_q16 ==
              item_box.position_x_q16 &&
          presentation_state.item_boxes[0].position_y_q16 ==
              item_box.position_y_q16 &&
          !presentation_state.item_boxes[0].active);
    /* Keep this item timeline away from the advancing rival pack; kart
     * contact is covered by its source-paired race route. */
    item_session.player.pose.position_x = INT32_C(1000) << 16u;
    item_session.player.pose.position_y = INT32_C(1000) << 16u;
    for (size_t tick = 1u; tick < PORTABLE_GP_ITEM_BOX_RESPAWN_TICKS;
         ++tick)
    {
        CHECK(portable_gp_race_session_tick(
                  &item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
              !item_session.item_boxes[0].active);
    }
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          item_session.item_boxes[0].active);
    for (size_t tick = 0u;
         item_session.roster.racers[0].item_inventory.active && tick < 180u;
         ++tick)
        CHECK(portable_gp_race_session_tick(
                  &item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK);
    CHECK(!item_session.roster.racers[0].item_inventory.active &&
          item_session.roster.racers[0].item_inventory.held_item_17a != 0u &&
          item_session.roster.racers[0].item_inventory.held_item_17a ==
              item_session.roster.racers[0].item_inventory.settled_item &&
          item_session.source_rng_state != 0u &&
          item_session.item_boxes[0].active &&
          item_session.item_box_contacts == 1u &&
          item_session.driving_quality.event_24 == 0u);
    CHECK(portable_gp_race_session_snapshot(&item_session, &snapshot) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          portable_gp_race_presentation_build(
              &item_session, &snapshot, &presentation_state) &&
          presentation_state.item_boxes[0].active);
    /* Battle uses content odds by remaining balloons, independent of GP rank.
     * Until the Battle shell lands, exercise its mode branch in the shared stack. */
    {
        const PortableBattleItemRules battle={.table={
            {{0x1007u,10u},{0u,255u}},
            {{0x1002u,10u},{0u,255u}},
            {{0x1000u,10u},{0u,255u}}},.box_hidden_ticks=120u,.elimination_ticks=10u};
        PortableGpRaceSession arena;
        race.battle_items=&battle;
        for (uint8_t balloons=0u; balloons<4u; ++balloons) {
            CHECK(test_gp_init(&arena,&race,7u)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(arena.battle_item_rules==&battle);
            CHECK(portable_gp_race_session_tick(&arena,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(portable_gp_race_session_tick(&arena,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            arena.roster.race_mode_flags_7da=0x800u;
            arena.roster.racers[0].balloons_remaining=balloons;
            for (size_t index=1u; index<8u; ++index) {
                arena.roster.racers[index].position_x_q16=(1400+(int32_t)index*20)*65536;
                arena.roster.racers[index].position_y_q16=(1400+(int32_t)index*20)*65536;
            }
            CHECK(portable_gp_race_session_tick(&arena,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(arena.item_box_contacts==1u && arena.item_boxes[0].cooldown_ticks==168u);
            arena.player.pose.position_x=1000*65536;
            arena.player.pose.position_y=1000*65536;
            arena.roster.racers[0].item_inventory.timeline_index_78=27;
            arena.roster.racers[0].item_inventory.delay_74=1;
            uint32_t expected_rng=arena.source_rng_state;
            (void)portable_gp_item_random(&expected_rng); /* frame loop */
            (void)portable_gp_item_random(&expected_rng); /* weighted settlement */
            CHECK(portable_gp_race_session_tick(&arena,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(arena.source_rng_state==expected_rng &&
                arena.roster.racers[0].item_inventory.held_item_17a==
                    (balloons==0u?0x1007u:balloons==1u?0x1002u:0x1000u));
            for (unsigned tick=1u; tick<168u; ++tick) {
                CHECK(!arena.item_boxes[0].active);
                CHECK(portable_gp_race_session_tick(&arena,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            }
            CHECK(arena.item_boxes[0].active);
        }
        /* CPUs use those same odds only when their delayed award settles. */
        arena.item_box_count=0u;
        arena.roster.cpu_item_cooldown_c62=60000u;
        CHECK(portable_gp_race_roster_begin_item_box(&arena.roster,1u,0x8800u,0u)==PORTABLE_GP_ROSTER_OK);
        arena.roster.racers[1].balloons_remaining=1u;
        arena.roster.racers[1].item_inventory.roulette_index_70=1;
        uint32_t expected_rng=arena.source_rng_state;
        (void)portable_gp_item_random(&expected_rng);
        CHECK(portable_gp_race_session_tick(&arena,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(arena.source_rng_state==expected_rng && arena.roster.racers[1].item_inventory.active);
        (void)portable_gp_item_random(&expected_rng);
        (void)portable_gp_item_random(&expected_rng);
        CHECK(portable_gp_race_session_tick(&arena,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(arena.source_rng_state==expected_rng &&
            arena.roster.racers[1].item_inventory.held_item_17a==0x1002u);
        race.battle_items=NULL;
    }
    /* CPU use is wired into the ordinary session update without input.
     * The scheduler queues B2 after motion; the next ground tick boosts. */
    {
        PortableGpRaceSession boost=item_session;
        boost.runtime.recovered_driver=&boost.player;
        boost.source_frame_clock=255u;
        PortableGpRacer *cpu=&boost.roster.racers[1];
        cpu->item_inventory=(PortableGpItemInventory){.held_item_17a=UINT16_C(0x1007)};
        cpu->source_flags=UINT32_C(0x2000);
        cpu->lightning_state_78=0u;cpu->world_height_q16=0;
        cpu->vertical_velocity_q8=0;cpu->mushroom_mode_84=0u;
        cpu->mushroom_response_flags_b4=0u;cpu->banana_hit_timer_38=0u;
        CHECK(portable_gp_race_session_tick(&boost,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(boost.roster.cpu_mushroom_uses==1u && cpu->item_inventory.held_item_17a==0u &&
              cpu->item_response_flags_b2==UINT16_C(0x8000) &&
              boost.roster.cpu_item_cooldown_c62==510u);
        CHECK(portable_gp_race_session_tick(&boost,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(cpu->mushroom_mode_84==8u && cpu->mushroom_timer_cc==test_mushroom_rules()->boost_ticks-1u &&
              cpu->item_response_flags_b2==0u && cpu->acceleration_q16>=(int32_t)test_mushroom_rules()->acceleration_units*65536 &&
              boost.roster.cpu_item_cooldown_c62==509u);
        CHECK(portable_gp_race_session_tick(&boost,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(cpu->mushroom_timer_cc==test_mushroom_rules()->boost_ticks-2u && (cpu->hit_cue_flags_58&UINT16_C(0x20))!=0u);
    }
    /* CPU star activates at its source slot, can hit the player on ground
     * or during a hop, and expires through the ordinary driver update. */
    for (unsigned airborne=0u;airborne<2u;++airborne) {
        PortableGpRaceSession hit=item_session;
        hit.runtime.recovered_driver=&hit.player;
        hit.runtime.recovered_dependencies=&hit.player_dependencies;
        hit.player.source_terrain22_enabled=1u;
        hit.source_frame_clock=255u;
        PortableGpRacer *cpu=&hit.roster.racers[1];
        cpu->item_inventory=(PortableGpItemInventory){.held_item_17a=UINT16_C(0x100a)};
        cpu->source_flags=UINT32_C(0x2000);
        cpu->lightning_state_78=0u;cpu->world_height_q16=0;
        cpu->vertical_velocity_q8=0;cpu->mushroom_mode_84=0u;
        cpu->mushroom_response_flags_b4=0u;cpu->banana_hit_timer_38=0u;
        CHECK(portable_gp_race_session_tick(&hit,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(hit.roster.cpu_star_uses==1u && cpu->lightning_status_6a==test_item_status_rules()->star_cpu_ticks &&
              cpu->item_inventory.held_item_17a==0u && hit.roster.cpu_item_cooldown_c62==510u);
        CHECK(portable_gp_race_session_snapshot(&hit,&snapshot)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(snapshot.opponents[0].star_active && !snapshot.player_star_active);
        for (unsigned i=1u;i<8u;++i) {
            hit.roster.racers[i].position_x_q16=hit.player.pose.position_x+(20+(int)i*10)*65536;
            hit.roster.racers[i].position_y_q16=hit.player.pose.position_y;
        }
        cpu->position_x_q16=hit.player.pose.position_x;
        cpu->position_y_q16=hit.player.pose.position_y;
        PortableGpRacer *local=&hit.roster.racers[0];
        local->coin_count_178=6u;local->banana_hit_timer_38=0u;
        hit.player.normal_collision_timer_38=0u;
        if (airborne) {
            hit.player.hop.mode=1u;hit.player.steering.driver_mode=1u;
            hit.player.pose.position_z=INT32_C(0x10000);
            hit.player.hop.velocity_q8=-50;
            hit.player.steering.state_flags|=UINT32_C(0x800);
            local->source_flags|=UINT32_C(0x800);
            local->lightning_state_78=1u;local->world_height_q16=hit.player.pose.position_z;
        }
        const unsigned quality=hit.driving_quality.event_1a;
        CHECK(portable_gp_race_session_tick(&hit,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(cpu->lightning_status_6a==test_item_status_rules()->star_cpu_ticks-1 && local->coin_count_178==2u &&
              hit.player.normal_collision_timer_38==7u && hit.driving_quality.event_1a==quality+1u &&
              local->lightning_timer_68==0u && hit.audio_event_count==2u &&
              hit.audio_events[0].song_id==162u && hit.audio_events[0].player==5u &&
              hit.audio_events[1].song_id==167u && hit.audio_events[1].player==6u);
        if (airborne) {
            CHECK(hit.player.strong_spin_rate_b6==UINT16_C(0x1000) &&
                  hit.player.hop.mode==1u && (hit.player.steering.response_flags&UINT16_C(0x200))!=0u);
            for (unsigned t=0u;t<20u && hit.player.hop.mode!=0u;++t) {
                cpu->position_x_q16=hit.player.pose.position_x+20*65536;
                CHECK(portable_gp_race_session_tick(&hit,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            }
            CHECK(hit.player.hop.mode==0u);
            CHECK(portable_gp_race_session_tick(&hit,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        }
        CHECK(hit.player.steering.driver_mode==10u && hit.player.strong_spin_rate_b6==UINT16_C(0x1000));
        unsigned ouches=0u;
        for (unsigned t=0u;t<test_item_status_rules()->star_cpu_ticks;++t) {
            cpu->position_x_q16=hit.player.pose.position_x+20*65536;
            CHECK(portable_gp_race_session_tick(&hit,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            for (size_t i=0u;i<hit.audio_event_count;++i)
                if (hit.audio_events[i].player==2u &&
                    (hit.audio_events[i].song_id==test_hazard_rules()->impact_voices[portable_gp_racer_voice_class(&hit.roster.racers[hit.local_index])][0][0] ||
                     hit.audio_events[i].song_id==test_hazard_rules()->impact_voices[portable_gp_racer_voice_class(&hit.roster.racers[hit.local_index])][0][1]))
                    ++ouches;
        }
        /* EC934: the local victim says one local-column ouch 16 ticks later. */
        CHECK(ouches==1u && hit.roster.pending_voice_count==0u);
        CHECK(cpu->lightning_status_6a==0 && (cpu->hit_cue_flags_58&UINT16_C(0x8000))==0u &&
              hit.player.steering.driver_mode==0u && hit.player.strong_spin_rate_b6<=255u);
    }
    /* The CPU scheduler consumes lightning without a local item button;
     * the same player pilot must spin, shrink, and recover under normal ticks. */
    for(unsigned preset=0u;preset<2u;++preset) {
        const PortableItemStatusRules custom={90u,60u,120u};
        const unsigned duration=preset?120u:test_item_status_rules()->lightning_ticks;
        PortableGpRaceSession hit=item_session;
        hit.roster.item_status_rules=preset?&custom:test_item_status_rules();
        hit.roster.item_hud=(PortableGpItemHud){0};
        hit.runtime.recovered_driver=&hit.player;
        hit.source_frame_clock=255u;
        PortableGpRacer *cpu=&hit.roster.racers[1];
        cpu->item_inventory=(PortableGpItemInventory){.held_item_17a=UINT16_C(0x1001)};
        cpu->source_flags=UINT32_C(0x2000);
        cpu->lightning_state_78=0u;cpu->world_height_q16=0;
        cpu->vertical_velocity_q8=0;cpu->mushroom_mode_84=0u;
        cpu->mushroom_response_flags_b4=0u;cpu->banana_hit_timer_38=0u;
        CHECK(portable_gp_race_session_tick(&hit,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(hit.roster.lightning_duration_c82==duration);
        CHECK(portable_gp_race_session_snapshot(&hit,&snapshot)==0 &&
              snapshot.lightning_scene_active && !snapshot.lightning_scene_white);

        PortableRaceMusic music={0};PortableRaceMusicEvent music_events[6];
        CHECK(portable_gp_race_session_snapshot(&hit,&snapshot)==0 && snapshot.lightning_music_active);
        PortableRaceMusicView music_view={snapshot.player_star_uses,snapshot.lightning_uses,25u,
            snapshot.player_star_active,snapshot.lightning_music_active,snapshot.music_finished,snapshot.music_final_lap,snapshot.player_boo_active};
        CHECK(portable_race_music_step(&music,&music_view,music_events)==1u && music_events[0].song_id==11u);
        CHECK(hit.roster.lightning_effect_events==1u && hit.roster.racers[0].lightning_timer_68==duration &&
              hit.player.strong_spin_rate_b6==0x1000u && (hit.player.steering.response_flags&0x300u)==0x300u &&
              cpu->item_inventory.held_item_17a==0u && hit.roster.cpu_item_cooldown_c62==510u &&
              hit.audio_event_count==1u && hit.audio_events[0].song_id==126u &&
              hit.roster.racers[2].lightning_timer_68==duration-1u && hit.driving_quality.count_0a==1u);
        CHECK(portable_gp_race_session_tick(&hit,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(hit.player.steering.response_status==13u && hit.player.steering.driver_mode==10u &&
              hit.player.strong_spin_rate_b6==0xfc0u && hit.roster.racers[0].lightning_timer_68==duration-1u);
        CHECK(portable_gp_race_session_snapshot(&hit,&snapshot)==PORTABLE_GP_RACE_SESSION_OK &&
              portable_gp_race_presentation_build(&hit,&snapshot,&presentation_state) &&
              presentation_state.character_affine_scale==0x180u &&
              presentation_state.character_view_angle!=0u);
        unsigned grow_audio=0u;
        for (unsigned tick=0u;tick<duration-1u;++tick) {
            CHECK(portable_gp_race_session_tick(&hit,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            const unsigned elapsed=tick+2u;
            CHECK(portable_gp_race_session_snapshot(&hit,&snapshot)==0 &&
                  portable_gp_race_presentation_build(&hit,&snapshot,&presentation_state));
            CHECK(snapshot.lightning_scene_active==(elapsed<48u) &&
                  snapshot.lightning_scene_white==(elapsed<48u && (elapsed&4u)!=0u) &&
                  presentation_state.lightning_scene_active==snapshot.lightning_scene_active &&
                  presentation_state.lightning_scene_white==snapshot.lightning_scene_white);
            if(elapsed>=48u) CHECK(hit.roster.lightning_state_80d==0u);
            for (unsigned audio=0u;audio<hit.audio_event_count;++audio)
                grow_audio+=hit.audio_events[audio].song_id==133u;
        }
        CHECK(hit.roster.lightning_duration_c82==0u && portable_gp_race_session_snapshot(&hit,&snapshot)==0 &&
            !snapshot.lightning_music_active);
        music_view.lightning_active=snapshot.lightning_music_active;
        CHECK(portable_race_music_step(&music,&music_view,music_events)==1u && music_events[0].song_id==25u);
        CHECK(grow_audio==1u && hit.roster.racers[0].lightning_timer_68==0u &&
              hit.player.steering.response_status==0u && (hit.player.steering.response_flags&0x200u)==0u);
    }
    /* sub_8043AB4 breaks an overlapped player box even when +0x17a already
     * holds an item, and sub_8043514 records quality event +0x24 instead of
     * starting a second roulette. */
    {
        const uint16_t selected_item =
            item_session.roster.racers[0].item_inventory.held_item_17a;
        item_session.player.pose.position_x = item_box.position_x_q16;
        item_session.player.pose.position_y = item_box.position_y_q16;
        CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          item_session.item_box_contacts == 2u &&
          !item_session.item_boxes[0].active &&
          item_session.driving_quality.event_24 == 1u &&
          item_session.roster.racers[0].item_inventory.held_item_17a ==
              selected_item);
    }

    /* Boo runs through ordinary item input, the shared driver and immutable
     * presentation; the stolen inventory is immediately usable. */
    {
        PortableGpRaceSession boo=item_session;
        boo.runtime.recovered_driver=&boo.player;
        boo.previous_semantic_buttons=0u;
        boo.roster.racers[0].item_inventory=(PortableGpItemInventory){.held_item_17a=0x100bu};
        for(unsigned i=1u;i<8u;++i)
            boo.roster.racers[i].item_inventory=(PortableGpItemInventory){.held_item_17a=0x1007u};
        CHECK(portable_gp_race_session_tick(&boo,PORTABLE_INPUT_ITEM,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        unsigned boo_audio=0u;
        for(unsigned i=0u;i<boo.audio_event_count;++i)
            boo_audio+=(boo.audio_events[i].song_id==129u && boo.audio_events[i].player==6u);
        CHECK(boo_audio==1u);
        CHECK(boo.roster.boo_uses==1u && boo.roster.boo_thefts==1u &&
            boo.roster.racers[0].contact_status_66==test_boo_rules()->invisibility_ticks && boo.player.contact_guard_66==test_boo_rules()->invisibility_ticks &&
            boo.roster.racers[0].item_inventory.held_item_17a==0x1007u);
        PortableGpRaceSnapshot boo_snapshot;
        PortableRacePresentationState presentation;
        CHECK(portable_gp_race_session_snapshot(&boo,&boo_snapshot)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(portable_gp_race_presentation_build(&boo,&boo_snapshot,&presentation));
        CHECK(presentation.gp_item==0x1007u &&
            memcmp(presentation.boo_attacks,boo.roster.boo_attacks,sizeof presentation.boo_attacks)==0);
        {
            /* 49D78 compares the camera owner, not the player/CPU class.
             * Self uses accepted-clock bit0; other karts use signed timer
             * edges with the inclusive33..343 fully invisible interval. */
            static const struct
            {
                int16_t timer;
                uint8_t self_even, self_odd, other_visible;
            } cases[] = {
                {0,1u,1u,1u}, {1,1u,0u,0u}, {2,1u,0u,1u},
                {32,1u,0u,1u}, {33,1u,0u,0u}, {34,1u,0u,0u},
                {342,1u,0u,0u}, {343,1u,0u,0u},
                {344,1u,0u,1u}, {345,1u,0u,0u}, {360,1u,0u,1u},
                {-1,1u,0u,0u}, {-2,1u,0u,1u}, {INT16_MIN,1u,0u,1u}
            };
            static PortableGpRaceSession visible, before_visible;
            static PortableGpRaceSnapshot view, before_view;
            static PortableRacePresentationState first, second;
            visible=boo;
            visible.runtime.recovered_driver=&visible.player;
            visible.runtime.recovered_dependencies=&visible.player_dependencies;
            CHECK(visible.local_index==0u && visible.roster.racers[0].is_player &&
                  !visible.roster.racers[1].is_player);
            for (size_t row=0u;row<sizeof cases/sizeof cases[0];++row)
            {
                visible.roster.racers[0].contact_status_66=cases[row].timer;
                visible.roster.racers[1].contact_status_66=cases[row].timer;
                for (unsigned clock=0u;clock<4u;++clock)
                {
                    visible.source_frame_clock=clock;
                    before_visible=visible;
                    CHECK(portable_gp_race_session_snapshot(&visible,&view)==0 &&
                          !view.driver_hidden && view.opponents[0].visible &&
                          !view.opponents[0].driver_hidden);
                    before_view=view;
                    CHECK(portable_gp_race_presentation_build(&visible,&view,&first) &&
                          portable_gp_race_presentation_build(&visible,&view,&second));
                    const unsigned self_visible=(clock&1u)?cases[row].self_odd:cases[row].self_even;
                    CHECK(first.suppress_player==!self_visible &&
                          first.opponents[0].visible==cases[row].other_visible &&
                          memcmp(&first,&second,sizeof first)==0 &&
                          memcmp(&visible,&before_visible,sizeof visible)==0 &&
                          memcmp(&view,&before_view,sizeof view)==0);
                    /* Visibility never resurrects a recovery-hidden kart or
                     * a culled opponent, even on a nominal visible edge. */
                    view.driver_hidden=1u;
                    view.opponents[0].driver_hidden=1u;
                    CHECK(portable_gp_race_presentation_build(&visible,&view,&first) &&
                          first.suppress_player && !first.opponents[0].visible);
                    view.opponents[0].driver_hidden=0u;
                    view.opponents[0].visible=0u;
                    CHECK(portable_gp_race_presentation_build(&visible,&view,&first) &&
                          !first.opponents[0].visible);
                }
            }
            /* Both local racers are players, yet each is an "other" in the
             * opposite pane. Swapping the selected view advances no clock. */
            PortableRecoveredRaceView local_race=race;
            PortableGpGridRow grid[8];
            for (unsigned i=0u;i<8u;++i)
                grid[i]=(PortableGpGridRow){race.rules.start_x_q16+(int32_t)i*32*65536,
                    race.rules.start_y_q16,race.rules.start_heading};
            local_race.gp_grid=grid;local_race.gp_grid_count=8u;
            const PortableRecoveredRaceView *races[2]={&local_race,&local_race};
            /* Both views share this class7 fixture's decoded handling.
             * Distinct local owners do not require different driver classes. */
            const uint8_t drivers[2]={7u,7u};
            const PortableGpRaceSessionStatus local_status=test_gp_init_locals(
                &visible,races,drivers,2u,0u,(PortableQuickRunOptions){0});
            if (local_status!=PORTABLE_GP_RACE_SESSION_OK)
                fprintf(stderr,"Boo visibility local fixture init status=%u course=%u class=%u\n",
                        (unsigned)local_status,local_race.rules.compatibility.course_id,
                        local_race.rules.compatibility.character_class);
            CHECK(local_status==PORTABLE_GP_RACE_SESSION_OK &&
                  visible.roster.racers[0].is_player && visible.roster.racers[1].is_player);
            for (size_t row=0u;row<sizeof cases/sizeof cases[0];++row)
            {
                visible.roster.racers[0].contact_status_66=cases[row].timer;
                visible.roster.racers[1].contact_status_66=cases[row].timer;
                for (unsigned clock=0u;clock<4u;++clock)
                {
                    visible.source_frame_clock=clock;
                    for (uint8_t local=0u;local<2u;++local)
                    {
                        CHECK(portable_gp_race_session_select_local(&visible,local)==0 &&
                              portable_gp_race_session_other_racer(&visible,0u)==1u-local);
                        before_visible=visible;
                        CHECK(portable_gp_race_session_snapshot(&visible,&view)==0 &&
                              !view.driver_hidden && view.opponents[0].visible &&
                              !view.opponents[0].driver_hidden);
                        before_view=view;
                        CHECK(portable_gp_race_presentation_build(&visible,&view,&first) &&
                              portable_gp_race_presentation_build(&visible,&view,&second));
                        const unsigned self_visible=(clock&1u)?cases[row].self_odd:cases[row].self_even;
                        CHECK(first.suppress_player==!self_visible &&
                              first.opponents[0].visible==cases[row].other_visible &&
                              memcmp(&first,&second,sizeof first)==0 &&
                              memcmp(&visible,&before_visible,sizeof visible)==0 &&
                              memcmp(&view,&before_view,sizeof view)==0);
                    }
                    CHECK(portable_gp_race_session_select_local(&visible,0u)==0 &&
                          visible.local_index==0u && visible.source_frame_clock==clock &&
                          visible.roster.racers[0].contact_status_66==cases[row].timer &&
                          visible.roster.racers[1].contact_status_66==cases[row].timer);
                }
            }
        }
        {
            PortableGpRaceSession blink=boo;
            blink.runtime.recovered_driver=&blink.player;
            unsigned pulses=0u;
            for(unsigned t=0u;t<9u;++t){
                CHECK(portable_gp_race_session_tick(&blink,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
                for(unsigned i=0u;i<blink.audio_event_count;++i)
                    if(blink.audio_events[i].song_id==130u){
                        CHECK(t==8u && blink.audio_events[i].player==7u);++pulses;
                    }
            }
            CHECK(pulses==1u);
            CHECK(portable_gp_race_session_snapshot(&blink,&boo_snapshot)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(portable_gp_race_presentation_build(&blink,&boo_snapshot,&presentation) && presentation.gp_item==0x100bu);
        }
        CHECK(portable_gp_race_session_tick(&boo,PORTABLE_INPUT_ITEM,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
            boo.roster.boo_uses==1u && boo.roster.racers[0].contact_status_66==(int16_t)(test_boo_rules()->invisibility_ticks-1u));
        CHECK(portable_gp_race_session_tick(&boo,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(portable_gp_race_session_tick(&boo,PORTABLE_INPUT_ITEM,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
            boo.roster.player_mushroom_uses==1u);
        /* 7B30 removes10 on new throttle; C4C0 then decrements AC every turn. */
        boo.roster.racers[0].item_lock_ac=20u;
        boo.previous_semantic_buttons=0u;
        CHECK(portable_gp_race_session_tick(&boo,PORTABLE_INPUT_ACCELERATE,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
            boo.roster.racers[0].item_lock_ac==9u && boo.player.item_lock_ac==9u);
        CHECK(portable_gp_race_session_tick(&boo,PORTABLE_INPUT_ACCELERATE,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
            boo.roster.racers[0].item_lock_ac==8u);
        CHECK(portable_gp_race_session_tick(&boo,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(portable_gp_race_session_tick(&boo,PORTABLE_INPUT_ACCELERATE,&disposition)==PORTABLE_GP_RACE_SESSION_OK &&
            boo.roster.racers[0].item_lock_ac==0u && boo.player.item_lock_ac==0u);
        /* AC64 loses one before decrement; a fresh A removes10 first,
         * avoiding that boundary. Protection clears the owner without loss. */
        for(unsigned mode=0u;mode<3u;++mode){
            PortableGpRaceSession haunt=boo;haunt.runtime.recovered_driver=&haunt.player;
            haunt.roster.racers[0].coin_count_178=5u;haunt.roster.racers[0].item_lock_ac=64u;
            haunt.previous_semantic_buttons=0u;
            if(mode==2u)haunt.roster.racers[0].lightning_status_6a=20;
            CHECK(portable_gp_race_session_tick(&haunt,mode==1u?PORTABLE_INPUT_ACCELERATE:0u,&disposition)==0);
            CHECK(haunt.roster.racers[0].item_lock_ac==(mode==0u?63u:mode==1u?53u:0u));
            CHECK(haunt.roster.racers[0].coin_count_178==(mode==0u?4u:5u));
        }
        /* A local leader consumes 4886C's actor edges as music operations. */
        memset(boo.roster.boo_attacks,0,sizeof boo.roster.boo_attacks);
        boo.roster.racers[0].lightning_status_6a=0;
        boo.roster.boo_attacks[0]=(PortableBooAttack){.occupied=1u,.phase=1u,
            .timer=1u,.radius=63u,.target=0u};
        CHECK(portable_gp_race_session_tick(&boo,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        unsigned requests=0u,releases=0u;
        for(unsigned i=0u;i<boo.audio_event_count;++i)
            requests+=boo.audio_events[i].operation==PORTABLE_GP_AUDIO_MUSIC_REQUEST&&boo.audio_events[i].music_effect==2u;
        CHECK(requests==1u&&boo.roster.boo_attacks[0].phase==2u);
        boo.roster.racers[0].item_lock_ac=0u;boo.player.item_lock_ac=0u;
        CHECK(portable_gp_race_session_tick(&boo,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        for(unsigned i=0u;i<boo.audio_event_count;++i)
            releases+=boo.audio_events[i].operation==PORTABLE_GP_AUDIO_MUSIC_RELEASE&&boo.audio_events[i].music_effect==2u;
        CHECK(releases==1u&&boo.roster.boo_attacks[0].phase==4u);
        CHECK(portable_gp_race_session_snapshot(&boo,&boo_snapshot)==PORTABLE_GP_RACE_SESSION_OK&&
            boo_snapshot.player_boo_active);
    }
    /* Ordinary item input activates the recovered star through the session;
     * its independent protection clock reaches the pilot and presentation. */
    {
        PortableGpRaceSession star=item_session;
        star.runtime.recovered_driver=&star.player;
        star.roster.racers[0].item_inventory=(PortableGpItemInventory){.held_item_17a=UINT16_C(0x100a)};
        star.previous_semantic_buttons=0u;
        CHECK(portable_gp_race_session_tick(&star,PORTABLE_INPUT_ITEM,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(star.roster.racers[0].lightning_status_6a==test_item_status_rules()->star_player_ticks &&
              star.roster.racers[0].item_inventory.held_item_17a==0u);
        /* Two protected karts retain their clocks and take the local push
         * adapter, including D4E4's one-coin loss and the eight-tick hold. */
        {
            PortableGpRaceSession mutual=star;
            mutual.runtime.recovered_driver=&mutual.player;
            mutual.player.source_terrain22_enabled=1u;
            const int32_t push_entries[8]={-4,-8,-16,-24,-36,-56,-64,-85};
            const PortableSourceNormalResponseTableView push_table={push_entries,8u};
            mutual.player_dependencies.normal_response_table=&push_table;
            mutual.runtime.recovered_dependencies=&mutual.player_dependencies;
            PortableGpRacer *local=&mutual.roster.racers[0];
            PortableGpRacer *rival=&mutual.roster.racers[2];
            for (unsigned i=1u;i<8u;++i) {
                mutual.roster.racers[i].position_x_q16=mutual.player.pose.position_x+(20+(int)i*10)*65536;
                mutual.roster.racers[i].position_y_q16=mutual.player.pose.position_y;
            }
            rival->position_x_q16=mutual.player.pose.position_x;
            rival->position_y_q16=mutual.player.pose.position_y;
            rival->hit_cue_flags_58=local->hit_cue_flags_58;
            rival->lightning_status_6a=100;
            rival->source_flags=UINT32_C(0x2000);
            rival->lightning_state_78=0u; rival->world_height_q16=0;
            rival->vertical_velocity_q8=0; rival->banana_hit_timer_38=0u;
            rival->contact_status_60=0u; rival->contact_status_66=0;
            rival->mushroom_response_flags_b4=0u; rival->mushroom_mode_84=0u;
            local->banana_hit_timer_38=0u; local->coin_count_178=5u;
            const uint32_t contacts=mutual.roster.contact_events;
            CHECK(portable_gp_race_session_tick(&mutual,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(mutual.roster.contact_events==contacts+1u && local->coin_count_178==4u &&
                  mutual.player.invulnerability_timer_6a==test_item_status_rules()->star_player_ticks-1u && rival->lightning_status_6a==99);
            {
                unsigned bumps=0u;
                for (size_t i=0u;i<mutual.audio_event_count;++i)
                    if (mutual.audio_events[i].song_id==167u) {
                        CHECK(mutual.audio_events[i].player==6u &&
                              mutual.audio_events[i].volume>=192u && mutual.audio_events[i].volume<=255u &&
                              mutual.audio_events[i].pitch>=-512 && mutual.audio_events[i].pitch<512);
                        ++bumps;
                    }
                CHECK(bumps==1u && !mutual.roster.player_bump_pending);
            }
            CHECK(mutual.player.normal_collision_timer_38==7u && rival->banana_hit_timer_38==7u &&
                  rival->hit_response_b6==0u && mutual.player.strong_spin_rate_b6==0u);
            for (unsigned i=0u;i<7u;++i) {
                rival->position_x_q16=mutual.player.pose.position_x+20*65536;
                CHECK(portable_gp_race_session_tick(&mutual,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            }
            CHECK(mutual.player.normal_collision_timer_38==0u && rival->banana_hit_timer_38==0u &&
                  mutual.player.invulnerability_timer_6a==test_item_status_rules()->star_player_ticks-8u);
        }
        PortableGpRacer *airborne=&star.roster.racers[2];
        airborne->position_x_q16=star.player.pose.position_x;
        airborne->position_y_q16=star.player.pose.position_y;
        airborne->source_flags=UINT32_C(0x2800);
        airborne->world_height_q16=INT32_C(0x10000);
        airborne->vertical_velocity_q8=-50;
        airborne->lightning_state_78=1u;
        airborne->mushroom_mode_84=1u;
        airborne->banana_hit_timer_38=0u;
        airborne->mushroom_response_flags_b4=UINT16_C(0x8000);
        CHECK(portable_gp_race_session_tick(&star,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(star.player.invulnerability_timer_6a==test_item_status_rules()->star_player_ticks-1u);
        CHECK(airborne->banana_hit_timer_38==7u && airborne->world_height_q16==INT32_C(0xb400) &&
              airborne->lightning_state_78==1u && (airborne->source_flags&UINT32_C(0x1000))!=0u);
        CHECK(portable_gp_race_session_snapshot(&star,&snapshot)==PORTABLE_GP_RACE_SESSION_OK &&
              snapshot.player_star_active && snapshot.player_palette_bank>=1u && snapshot.player_palette_bank<=4u);
        CHECK(portable_gp_race_presentation_build(&star,&snapshot,&presentation_state) &&
              presentation_state.palette_bank==snapshot.player_palette_bank);
        /* The ordinary star contact also enters lava retrieval, rather
         * than keeping the CPU in the pending-spin shortcut over the hazard. */
        {
            PortableGpRaceSession lava=star;
            lava.runtime.recovered_driver=&lava.player;
            PortableGpRacer *victim=&lava.roster.racers[2];
            victim->position_x_q16=lava.player.pose.position_x+5*65536;
            victim->position_y_q16=lava.player.pose.position_y+5*65536;
            victim->source_flags=UINT32_C(0x2000);
            victim->world_height_q16=0;victim->vertical_velocity_q8=0;
            victim->lightning_state_78=0u;victim->mushroom_mode_84=0u;
            victim->banana_hit_timer_38=0u;victim->mushroom_response_flags_b4=0u;
            victim->speed_q16=0;victim->acceleration_q16=0;
            const size_t player_cell=(size_t)(lava.player.pose.position_y/(8*65536))*256u+
                (size_t)(lava.player.pose.position_x/(8*65536));
            for (int y=-1;y<=1;++y) for (int x=-1;x<=1;++x)
                if (x!=0 || y!=0) terrain_cells[(ptrdiff_t)player_cell+y*256+x]=1u;
            material_flags[1]=UINT8_C(0x24);
            CHECK(portable_gp_race_session_tick(&lava,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(victim->lightning_state_78==3u && victim->recovery_material==UINT8_C(0x24) &&
                  (victim->mushroom_response_flags_b4&UINT16_C(0x0200))==0u);
            for (int y=-1;y<=1;++y) for (int x=-1;x<=1;++x)
                if (x!=0 || y!=0) terrain_cells[(ptrdiff_t)player_cell+y*256+x]=0u;
            material_flags[1]=0u;
            for (unsigned tick=0u;tick<300u && victim->lightning_state_78!=0u;++tick)
                CHECK(portable_gp_race_session_tick(&lava,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(victim->lightning_state_78==0u && victim->world_height_q16==0);
        }
        star.roster.racers[0].lightning_status_6a=1;
        CHECK(portable_gp_race_session_tick(&star,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(star.player.invulnerability_timer_6a==0u);
        CHECK(portable_gp_race_session_snapshot(&star,&snapshot)==PORTABLE_GP_RACE_SESSION_OK &&
              !snapshot.player_star_active && snapshot.player_palette_bank==0u);
    }

    /* A rival uses the same source 0x8800 actor, then the qualified Peach
     * 50cc 30-tick AI table settles its inventory without host injection. */
    item_box.position_x_q16 = INT32_C(1500) << 16u;
    item_box.position_y_q16 = INT32_C(1500) << 16u;
    CHECK(test_gp_init(&item_session, &race, 7u) ==
          PORTABLE_GP_RACE_SESSION_OK);
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK);
    for (size_t index = 1u; index < PORTABLE_GP_RACER_COUNT; ++index)
    {
        item_session.roster.racers[index].position_x_q16 =
            (INT32_C(2000) + (int32_t)index * 20) << 16u;
        item_session.roster.racers[index].position_y_q16 =
            (INT32_C(2000) + (int32_t)index * 20) << 16u;
    }
    item_session.roster.racers[1].position_x_q16 = item_box.position_x_q16;
    item_session.roster.racers[1].position_y_q16 = item_box.position_y_q16;
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          item_session.item_box_contacts == 1u &&
          item_session.opponent_item_box_contacts == 1u &&
          item_session.roster.racers[1].item_inventory.active &&
          item_session.roster.racers[1].item_inventory.held_item_17a ==
              PORTABLE_GP_ITEM_ROULETTE &&
          item_session.roster.racers[1].item_inventory.roulette_index_70 ==
              PORTABLE_GP_ITEM_OPPONENT_DELAY);
    for (size_t tick = 0u;
         item_session.roster.racers[1].item_inventory.active && tick < 32u;
         ++tick)
        CHECK(portable_gp_race_session_tick(
                  &item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK);
    CHECK(!item_session.roster.racers[1].item_inventory.active &&
          item_session.roster.racers[1].item_inventory.held_item_17a ==
              fixture_cpu_items[0].item[0][item_session.roster.racers[1].driver_class&7u]
                  [(item_session.roster.racers[1].rank_flags_ba>>1u)&7u] &&
          item_session.roster.item_settlements == 1u);

    /* A non-Peach box settles from imported content rather than the fallback. */
    {
        PortableGpCpuItemRules cpu_items[3]={0};
        for (unsigned variant=0u;variant<3u;++variant)
            for (unsigned driver=0u;driver<8u;++driver)
                for (unsigned rank=0u;rank<8u;++rank)
                    cpu_items[0].item[variant][driver][rank]=
                        variant==2u?UINT16_C(0x1002):UINT16_C(0x1007);
        race.gp_cpu_items=cpu_items;
        race.gp_cpu_item_engine_count=3u;
        race.rules.compatibility.course_id=26u;
        CHECK(test_gp_init(&item_session,&race,7u)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(item_session.cpu_item_rules==&cpu_items[0]);
        for (unsigned tick=0u;tick<2u;++tick)
            CHECK(portable_gp_race_session_tick(&item_session,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        for (unsigned owner=1u;owner<8u;++owner) {
            item_session.roster.racers[owner].position_x_q16=(1800+(int32_t)owner*20)*65536;
            item_session.roster.racers[owner].position_y_q16=(1800+(int32_t)owner*20)*65536;
        }
        item_session.roster.racers[1].position_x_q16=item_box.position_x_q16;
        item_session.roster.racers[1].position_y_q16=item_box.position_y_q16;
        CHECK(portable_gp_race_session_tick(&item_session,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(item_session.opponent_item_box_contacts==1u);
        for (unsigned tick=0u;tick<32u && item_session.roster.racers[1].item_inventory.active;++tick)
            CHECK(portable_gp_race_session_tick(&item_session,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(item_session.roster.racers[1].item_inventory.held_item_17a==UINT16_C(0x1007));
        CHECK(item_session.roster.item_settlements==1u);
        race.gp_cpu_items=fixture_cpu_items;
        race.gp_cpu_item_engine_count=3u;
        race.rules.compatibility.course_id=24u;
    }

    /* The retained natural source rival-7 call chose 0x1000 from class 3,
     * rank 0. The same production callback boundary must reach that value. */
    CHECK(test_gp_init(&item_session, &race, 7u) ==
          PORTABLE_GP_RACE_SESSION_OK);
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK);
    for (size_t index = 1u; index < PORTABLE_GP_RACER_COUNT; ++index)
    {
        item_session.roster.racers[index].position_x_q16 =
            (INT32_C(1800) + (int32_t)index * 20) << 16u;
        item_session.roster.racers[index].position_y_q16 =
            (INT32_C(1800) + (int32_t)index * 20) << 16u;
    }
    item_session.roster.racers[7].position_x_q16 = item_box.position_x_q16;
    item_session.roster.racers[7].position_y_q16 = item_box.position_y_q16;
    item_session.roster.racers[7].driver_class = 3u;
    item_session.roster.racers[7].rank_flags_ba = 0u;
    CHECK(portable_gp_race_session_tick(&item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          item_session.roster.racers[7].item_inventory.roulette_index_70 ==
              PORTABLE_GP_ITEM_OPPONENT_DELAY);
    for (size_t tick = 0u;
         item_session.roster.racers[7].item_inventory.active && tick < 32u;
         ++tick)
        CHECK(portable_gp_race_session_tick(
                  &item_session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK);
    CHECK(item_session.roster.racers[7].item_inventory.held_item_17a ==
              UINT16_C(0x1000) &&
          item_session.roster.item_settlements == 1u);
    race.course_objects = NULL;
    race.course_object_count = 0u;

    /* Production GP uses the same qualified source driver-rescue clock while
     * its seven opponents continue to advance. */
    memset(terrain_cells, 7, sizeof terrain_cells);
    material_flags[7] = UINT8_C(0x22);
    race.rules.compatibility.course_id = 25u;
    {
        PortableGpRaceSession recovery;
        CHECK(test_gp_init(&recovery,&race,7u)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(recovery.player.recovery_contact_34==3u&&recovery.source_scene_row_c88==0);
        recovery.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
        recovery.runtime.session.countdown_remaining=0u;
        recovery.player.source_terrain22_enabled=1u;
        recovery.player.source_terrain22_active=1u;
        recovery.player.hop.mode=7u;
        recovery.player.steering.state_flags |= UINT32_C(0x800);
        recovery.player.pose.position_z=0x8000;
        recovery.player.hop.height_q16=0x8000;
        recovery.player.recovery_target_angle_a2=(uint16_t)recovery.player.steering.steering_offset;
        recovery.player.steering.driver_mode=3u;
        recovery.roster.racers[0].hit_state_flags_74=0x2000u;
        recovery.driving_quality.event_17=255u;
        for(unsigned i=0u;i<8u;++i)recovery.roster.racers[i].coin_count_178=2u;
        recovery.rival_coin_count_handoff=2u;
        CHECK(portable_gp_race_session_tick(&recovery,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(recovery.player.hop.recovery_touchdown==1u&&recovery.player.hop.mode==0u&&
            recovery.player.recovery_flags_74==0x2001u&&
            recovery.roster.racers[0].hit_state_flags_74==0x2001u&&
            recovery.driving_quality.event_17==0u &&
            (recovery.roster.racers[0].source_flags & UINT32_C(0x800)) != 0u);
        CHECK(portable_gp_race_session_apply_coin_loss(&recovery,2u)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(recovery.recovery_coin_loss==2u&&recovery.roster.racers[0].coin_count_178==0u&&
            recovery.roster.racers[1].coin_count_178==2u&&recovery.rival_coin_count_handoff==0u);
        /* Leave water before the following normal pass, as in the source.
         * Its grounded flag must propagate even after active22 becomes false. */
        material_flags[7] = UINT8_C(0x46);
        CHECK(portable_gp_race_session_tick(&recovery,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(!recovery.player.source_terrain22_active &&
            (recovery.player.steering.state_flags & UINT32_C(0x800)) == 0u &&
            recovery.roster.racers[0].source_flags == recovery.player.steering.state_flags);
        material_flags[7] = UINT8_C(0x22);
        CHECK(!recovery.player.hop.recovery_touchdown&&recovery.driving_quality.event_17==0u&&
            recovery.recovery_coin_loss==0u&&recovery.roster.racers[1].coin_count_178==0u);
        recovery.roster.racers[0].coin_count_178=1u;
        CHECK(portable_gp_race_session_apply_coin_loss(&recovery,2u)==PORTABLE_GP_RACE_SESSION_OK&&
              recovery.recovery_coin_loss==1u&&recovery.roster.racers[0].coin_count_178==0u);
        recovery.roster.racers[0].coin_count_178=2u;
        recovery.player.steering.state_flags|=0x20u;
        CHECK(portable_gp_race_session_apply_coin_loss(&recovery,2u)==PORTABLE_GP_RACE_SESSION_OK&&
              recovery.recovery_coin_loss==0u&&recovery.roster.racers[0].coin_count_178==2u);
        recovery.player.steering.state_flags&=~UINT32_C(0x20);
        recovery.source_scene_row_c88=-1;
        CHECK(portable_gp_race_session_apply_coin_loss(&recovery,2u)==PORTABLE_GP_RACE_SESSION_OK&&
              recovery.recovery_coin_loss==2u&&recovery.roster.racers[0].coin_count_178==0u);
        recovery.roster.racers[0].coin_count_178=2u;
        recovery.source_scene_row_c88=0;
        recovery.roster.race_mode_flags_7da=0x110u;
        CHECK(portable_gp_race_session_apply_coin_loss(&recovery,2u)==PORTABLE_GP_RACE_SESSION_OK&&
              recovery.roster.racers[0].coin_count_178==0u);
        {
            /* D4E4 cause1 preserves four-coin identity through imported FX. */
            const PortableCoinEffectRules coin_rules={.horizontal_step={0,-2,2,-2,2,-2,2,-2,2},
                .upward_speed_q16={0x80000,0x80000,0x80000,0x50000,0x50000,0x80000,0x80000,0x50000,0x50000},
                .bounce_volumes={256u,153u,102u},.script_ticks={27u,66u,21u,5u}};
            PortableGpRaceSession coins=recovery;
            coins.runtime.recovered_driver=&coins.player;
            coins.runtime.recovered_dependencies=&coins.player_dependencies;
            int32_t coin_sine[1024];
            for(unsigned i=0u;i<1024u;++i)coin_sine[i]=(int32_t)lround(sin((double)i*6.283185307179586/1024.0)*32768.0);
            coins.player_dependencies.sine_table=coin_sine;coins.player_dependencies.sine_count=1024u;
            coins.coin_effect_rules=&coin_rules;coins.coin_birth_x=120;coins.coin_birth_y=140;coins.coin_birth_inverse=256;
            {
                /* D4E4's signed negative scene overrides ordinary mode
                 * policy only through bit0 or the low word's high byte. */
                static const struct { int32_t scene; uint8_t enabled; } overrides[]={
                    {INT32_MIN,0u},{INT32_MIN+1,1u},{INT32_MIN+256,1u},
                    {INT32_MIN+32768,1u},{INT32_MIN+2,0u},
                    {INT32_MIN+65536,0u},{-65536,0u},{-1,1u}
                };
                const uint16_t modes[]={0x100u,0x110u,0x400u,0x800u,0x1000u};
                const uint32_t saved_flags=coins.player.steering.state_flags;
                const PortableCoinEffects empty={0};
                for (unsigned mode=0u;mode<sizeof modes/sizeof modes[0];++mode)
                for (unsigned row=0u;row<sizeof overrides/sizeof overrides[0];++row)
                {
                    coins.player.steering.state_flags=UINT32_C(0x8000);
                    coins.source_scene_row_c88=overrides[row].scene;
                    coins.roster.race_mode_flags_7da=modes[mode];
                    coins.roster.racers[0].coin_count_178=9u;
                    coins.roster.racers[1].coin_count_178=17u;
                    coins.recovery_coin_loss=77u;coins.rival_coin_count_handoff=77u;
                    coins.coin_effects=empty;
                    CHECK(portable_gp_race_session_apply_coin_loss(&coins,
                          PORTABLE_COIN_EFFECT_CAUSE_RETRIEVAL)==PORTABLE_GP_RACE_SESSION_OK);
                    CHECK(coins.recovery_coin_loss==(overrides[row].enabled?2u:0u) &&
                          coins.roster.racers[0].coin_count_178==(overrides[row].enabled?7u:9u) &&
                          coins.roster.racers[1].coin_count_178==17u &&
                          coins.rival_coin_count_handoff==(overrides[row].enabled?7u:77u));
                    if (overrides[row].enabled)
                        CHECK(coins.coin_effects.slots[0].kind==PORTABLE_COIN_EFFECT_RISE_EMITTER &&
                              coins.coin_effects.slots[0].limit==2u);
                    else CHECK(memcmp(&coins.coin_effects,&empty,sizeof empty)==0);
                }
                /* Nonnegative values, even with those same low bits set,
                 * keep the original GP/Quick/VS/Battle policy controls. */
                const int32_t normal_scenes[]={0,1,256};
                for (unsigned mode=0u;mode<sizeof modes/sizeof modes[0];++mode)
                for (unsigned row=0u;row<sizeof normal_scenes/sizeof normal_scenes[0];++row)
                {
                    coins.source_scene_row_c88=normal_scenes[row];
                    coins.roster.race_mode_flags_7da=modes[mode];
                    coins.roster.racers[0].coin_count_178=9u;
                    coins.recovery_coin_loss=77u;coins.coin_effects=empty;
                    const PortableGpRaceSessionStatus status=portable_gp_race_session_apply_coin_loss(
                        &coins,PORTABLE_COIN_EFFECT_CAUSE_RETRIEVAL);
                    CHECK(status==(modes[mode]==0x1000u?PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT:
                                                                      PORTABLE_GP_RACE_SESSION_OK));
                    CHECK(coins.roster.racers[0].coin_count_178==(mode<3u?7u:9u) &&
                          coins.recovery_coin_loss==(mode<3u?2u:mode==3u?0u:77u));
                    if (mode>=3u)CHECK(memcmp(&coins.coin_effects,&empty,sizeof empty)==0);
                }
                const uint32_t flags[]={UINT32_C(0x8000),UINT32_C(0x8020),0u,UINT32_C(0x20)};
                for (unsigned off=0u;off<2u;++off)
                for (unsigned flag=0u;flag<sizeof flags/sizeof flags[0];++flag)
                {
                    coins.source_scene_row_c88=-1;coins.roster.race_mode_flags_7da=0x1000u;
                    coins.player.steering.state_flags=flags[flag];
                    coins.quick_run_options.coins_off=(uint8_t)off;
                    coins.roster.racers[0].coin_count_178=9u;
                    coins.recovery_coin_loss=77u;coins.coin_effects=empty;
                    CHECK(portable_gp_race_session_apply_coin_loss(&coins,
                          PORTABLE_COIN_EFFECT_CAUSE_RETRIEVAL)==0 &&
                          coins.recovery_coin_loss==(flag==0u?2u:0u) &&
                          coins.roster.racers[0].coin_count_178==(flag==0u?7u:9u));
                    if (flag!=0u)CHECK(memcmp(&coins.coin_effects,&empty,sizeof empty)==0);
                }
                /* The save-policy toggle remains meaningful only on the
                 * ordinary nonnegative scene path; negative clear stays off. */
                for (unsigned row=0u;row<2u;++row)
                {
                    coins.quick_run_options.coins_off=1u;
                    coins.source_scene_row_c88=row?0:INT32_MIN;
                    coins.roster.race_mode_flags_7da=0x100u;
                    coins.player.steering.state_flags=UINT32_C(0x8000);
                    coins.roster.racers[0].coin_count_178=9u;coins.coin_effects=empty;
                    CHECK(portable_gp_race_session_apply_coin_loss(&coins,PORTABLE_COIN_EFFECT_CAUSE_RETRIEVAL)==0 &&
                          !coins.recovery_coin_loss && coins.roster.racers[0].coin_count_178==9u &&
                          memcmp(&coins.coin_effects,&empty,sizeof empty)==0);
                }
                coins.source_scene_row_c88=-1;coins.roster.race_mode_flags_7da=0x1000u;
                coins.quick_run_options.coins_off=0u;
                coins.player.steering.state_flags=UINT32_C(0x8000);
                const uint8_t amounts[]={0u,4u,2u,1u,8u,1u};
                for (unsigned cause=0u;cause<6u;++cause)
                {
                    coins.roster.racers[0].coin_count_178=9u;coins.coin_effects=empty;
                    CHECK(portable_gp_race_session_apply_coin_loss(&coins,(PortableCoinEffectCause)cause)==0 &&
                          coins.recovery_coin_loss==amounts[cause] &&
                          coins.roster.racers[0].coin_count_178==9u-amounts[cause]);
                    if (!cause)CHECK(memcmp(&coins.coin_effects,&empty,sizeof empty)==0);
                    else CHECK(coins.coin_effects.slots[0].cause==cause);
                }
                for (unsigned count=0u;count<=3u;count+=3u)
                {
                    coins.roster.racers[0].coin_count_178=(uint16_t)count;coins.coin_effects=empty;
                    CHECK(portable_gp_race_session_apply_coin_loss(&coins,PORTABLE_COIN_EFFECT_CAUSE_CRUSH)==0 &&
                          coins.recovery_coin_loss==count && !coins.roster.racers[0].coin_count_178);
                    if (!count)CHECK(memcmp(&coins.coin_effects,&empty,sizeof empty)==0);
                    else CHECK(coins.coin_effects.slots[0].limit==3u);
                }
                const PortableCoinEffects before_invalid=coins.coin_effects;
                coins.recovery_coin_loss=77u;
                CHECK(portable_gp_race_session_apply_coin_loss(&coins,(PortableCoinEffectCause)6)==
                      PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT && coins.recovery_coin_loss==77u &&
                      memcmp(&coins.coin_effects,&before_invalid,sizeof before_invalid)==0);
                coins.player.steering.state_flags=saved_flags;
                coins.source_scene_row_c88=0;coins.roster.race_mode_flags_7da=0x110u;
                coins.coin_effects=empty;
            }
            const uint8_t losses[5]={4u,2u,1u,8u,1u};
            for(unsigned cause=1u;cause<=5u;++cause){
                coins.coin_effects=(PortableCoinEffects){0};coins.source_frame_clock=4u;
                coins.roster.racers[0].coin_count_178=9u;
                CHECK(portable_gp_race_session_apply_coin_loss(&coins,(PortableCoinEffectCause)cause)==0 &&
                    coins.recovery_coin_loss==losses[cause-1u] && coins.roster.racers[0].coin_count_178==9u-losses[cause-1u]);
                const PortableCoinEffectSlot *effect=&coins.coin_effects.slots[0];
                CHECK(effect->cause==cause && effect->kind==
                    (cause==1u||cause==4u?PORTABLE_COIN_EFFECT_DROP_EMITTER:
                     cause==2u?PORTABLE_COIN_EFFECT_RISE_EMITTER:PORTABLE_COIN_EFFECT_BOUNCE));
                if(cause==3u||cause==5u)CHECK(effect->variant==3u);
                else CHECK(effect->limit==losses[cause-1u]);
            }
            coins.coin_effects=(PortableCoinEffects){0};
            /* Roster producers already subtracted4 from9. The FX pass drains
             * the retained cause exactly once without subtracting again. */
            coins.roster.racers[0].coin_count_178=5u;
            coins.roster.racers[0].coin_loss_event_count=1u;
            coins.roster.racers[0].coin_loss_events[0].cause=PORTABLE_COIN_EFFECT_CAUSE_HIT;
            coins.roster.racers[0].coin_loss_events[0].loss=4u;
            coins.roster.racers[0].coin_loss_events[0].remaining=5u;
            coins.roster.racers[0].coin_loss_events[0].clock=3u;
            CHECK(portable_gp_race_session_tick_dropped_coins(&coins) &&
                !coins.roster.racers[0].coin_loss_event_count && coins.roster.racers[0].coin_count_178==5u &&
                coins.coin_effects.slots[0].kind==PORTABLE_COIN_EFFECT_DROP_EMITTER && coins.coin_effects.slots[0].counter==1u &&
                coins.coin_effects.slots[0].x==121);
            CHECK(portable_gp_race_session_tick_dropped_coins(&coins) &&
                coins.coin_effects.slots[0].counter==2u && !coins.coin_effects.slots[1].kind &&
                coins.roster.racers[0].coin_count_178==5u);
            const PortableCoinEffects immutable=coins.coin_effects;
            PortableGpRaceSnapshot coin_snapshot;
            CHECK(portable_gp_race_session_snapshot(&coins,&coin_snapshot)==0 &&
                !memcmp(&immutable,&coins.coin_effects,sizeof immutable) &&
                !memcmp(&immutable,&coin_snapshot.coin_effects,sizeof immutable));
            coins.coin_effects=(PortableCoinEffects){0};coins.audio_event_count=0u;
            coins.roster.racers[0].coin_count_178=4u;
            CHECK(portable_gp_race_session_apply_coin_loss(&coins,PORTABLE_COIN_EFFECT_CAUSE_HIT)==PORTABLE_GP_RACE_SESSION_OK&&
                  coins.coin_effects.slots[0].kind==PORTABLE_COIN_EFFECT_DROP_EMITTER);
            unsigned bounces=0u,loud=0u;
            for(unsigned tick=0u;tick<100u;++tick){
                coins.audio_event_count=0u;
                CHECK(portable_gp_race_session_tick_dropped_coins(&coins));
                for(uint8_t i=0u;i<coins.audio_event_count;++i)
                    if(coins.audio_events[i].song_id==102u){
                        ++bounces;loud+=coins.audio_events[i].volume==256u;
                        CHECK(coins.audio_events[i].player==7u&&coins.audio_events[i].unless_busy);
                    }
            }
            CHECK(bounces>=4u&&loud==4u);
            for(unsigned i=0u;i<PORTABLE_COIN_EFFECT_SLOTS;++i)CHECK(!coins.coin_effects.slots[i].kind);
            coins.roster.racers[0].coin_count_178=1u;
            CHECK(portable_gp_race_session_apply_coin_loss(&coins,PORTABLE_COIN_EFFECT_CAUSE_RETRIEVAL)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(coins.coin_effects.slots[0].kind==PORTABLE_COIN_EFFECT_RISE_EMITTER&&coins.coin_effects.slots[0].limit==1u);
        }
        recovery.roster.racers[0].coin_count_178=2u;
        recovery.roster.race_mode_flags_7da=0x400u;
        CHECK(portable_gp_race_session_apply_coin_loss(&recovery,2u)==PORTABLE_GP_RACE_SESSION_OK&&
              recovery.recovery_coin_loss==2u);
        recovery.recovery_coin_loss=0u;recovery.roster.racers[0].coin_count_178=2u;
        recovery.roster.race_mode_flags_7da=0x1000u;
        CHECK(portable_gp_race_session_apply_coin_loss(&recovery,2u)==PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT&&
              recovery.roster.racers[0].coin_count_178==2u);
        recovery.roster.race_mode_flags_7da=0x800u;
        CHECK(portable_gp_race_session_apply_coin_loss(&recovery,2u)==PORTABLE_GP_RACE_SESSION_OK&&
              recovery.recovery_coin_loss==0u&&recovery.roster.racers[0].coin_count_178==2u);
        CHECK(portable_gp_race_session_apply_coin_loss(&recovery,(PortableCoinEffectCause)6)==PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT);
        /* DA80 owns the event at pre-motion entry, independent of rendering.
         * The low-coin cue starts on song-table player10 at clock multiples32. */
        recovery.source_coin_hud_enabled=1u;
        recovery.roster.race_mode_flags_7da=0x100u;
        recovery.roster.race_phase_7d4=3u;
        recovery.roster.racers[0].coin_count_178=0u;
        recovery.source_frame_clock=31u;
        CHECK(portable_gp_race_session_tick(&recovery,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(recovery.audio_event_count>0u&&recovery.audio_events[0].song_id==194u&&
              recovery.audio_events[0].player==10u&&recovery.audio_events[0].volume==256u&&
              recovery.audio_events[0].pitch==0);
        CHECK(portable_gp_race_session_tick(&recovery,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        for(unsigned i=0;i<recovery.audio_event_count;++i)
            CHECK(recovery.audio_events[i].song_id!=194u);
    }
    CHECK(test_gp_init(&water_session, &race, 7u) ==
          PORTABLE_GP_RACE_SESSION_OK);
    water_session.runtime.session.phase = PORTABLE_RACE_PHASE_RACING;
    water_session.runtime.session.countdown_remaining = 0u;
    initial_opponent_y = water_session.roster.racers[1].position_y_q16;
    CHECK(portable_gp_race_session_tick(
              &water_session, PORTABLE_INPUT_NONE, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          water_session.water_recovery.phase ==
              PORTABLE_WATER_RECOVERY_SINK &&
          water_session.water_recovery.elapsed_ticks == 1u &&
          water_session.runtime.session.elapsed_ticks == 1u &&
          water_session.runtime.session.rescue_count == 1u);
    CHECK(portable_gp_race_session_snapshot(&water_session, &snapshot) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          portable_gp_race_presentation_build(
              &water_session, &snapshot, &presentation_state) &&
          snapshot.recovery_phase == PORTABLE_WATER_RECOVERY_SINK &&
          snapshot.recovery_elapsed_ticks == 1u &&
          presentation_state.recovery_phase ==
              PORTABLE_WATER_RECOVERY_SINK);
    for (unsigned tick = 1u;
         tick < PORTABLE_WATER_RECOVERY_TOTAL_TICKS; ++tick)
        CHECK(portable_gp_race_session_tick(
                  &water_session, PORTABLE_INPUT_ACCELERATE,
                  &disposition) == PORTABLE_GP_RACE_SESSION_OK &&
              disposition == PORTABLE_RACE_TICK_ADVANCE_MOTION);
    CHECK(water_session.water_recovery.phase ==
              PORTABLE_WATER_RECOVERY_NONE &&
          water_session.item_art_clock.calls==PORTABLE_WATER_RECOVERY_TOTAL_TICKS&&
          water_session.water_recovery.elapsed_ticks ==
              PORTABLE_WATER_RECOVERY_TOTAL_TICKS &&
          water_session.runtime.session.elapsed_ticks ==
              PORTABLE_WATER_RECOVERY_TOTAL_TICKS &&
          water_session.player.pose.position_x ==
              water_session.runtime.session.rescue_x_q16 &&
          water_session.player.pose.position_y ==
              water_session.runtime.session.rescue_y_q16 &&
          water_session.roster.racers[1].position_y_q16 !=
              initial_opponent_y);

    /* Complete-pack GP owns water/void/lava/bounds recovery without SDL.
     * Enter each material, consume requests on the following turn, then
     * leave it during carry and resume driving, with one rescue counted. */
    const PortableRecoveryFxRules recovery_fx_rules={{127u,127u,127u},{0,1,1,1,2,2,3,3,1}};
    for(unsigned hazard=0u;hazard<6u;++hazard)
    {
        const uint8_t materials[6]={0x22u,0x20u,0x24u,0x26u,0x22u,0x22u};
        const uint8_t initial_modes[6]={4u,2u,3u,5u,4u,4u};
        material_flags[7]=materials[hazard];
        race.recovery_fx_rules=(hazard==0u || hazard>=4u)?&recovery_fx_rules:NULL;
        race.icy_recovery=(uint8_t)(hazard==5u);
        uint8_t bob_offsets[128] = {0};
        race.recovery_bob_offsets = bob_offsets;
        race.dependencies.source_track_route=route;
        race.dependencies.source_track_route_count=47u;
        /* Added water tracks retain native sink/carry/touchdown ownership;
         * a legacy course25-only fallback cannot satisfy these assertions. */
        race.rules.compatibility.course_id = hazard==4u?128u:29u;
        PortableGpRaceSession native;
        CHECK(test_gp_init(&native,&race,7u)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(native.player.source_hazard_enabled && native.player.source_terrain22_enabled);
        native.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
        native.runtime.session.countdown_remaining=0u;
        const int16_t splash_y=140;
        /* BDC4 uses the projected body at birth, including elevated entries;
         * grounded terrain dispatch itself rejects an airborne kart. */
        PortableEntrySplash elevated={0};
        portable_entry_splash_start(&elevated,8*65536);
        CHECK(elevated.y==108 && elevated.scale_q8==128u);
        CHECK(portable_gp_race_session_tick(&native,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(native.player.hop.mode==initial_modes[hazard]);
        if(race.recovery_fx_rules)CHECK((native.hazard_effect_72&15u)==(race.icy_recovery?11u:13u));
        const int splash=hazard==0u || hazard==2u || hazard>=4u;
        CHECK(native.entry_splash.age==(splash?1u:0u));
        if(splash)CHECK(native.entry_splash.x==120 && native.entry_splash.y==splash_y &&
            native.entry_splash.scale_q8==128u);
        if(hazard==2u)CHECK(native.player.burn_ticks_72==249u &&
            native.roster.racers[0].burn_ticks_72==249u && native.player.palette_bank==5u &&
            native.lava_smoke.emitter_ticks==PORTABLE_LAVA_SMOKE_TICKS-1u);
        /* Fresh-grid racing tick one intentionally defers the CPU pass;
         * assert their terrain entry after their first ordinary world turn. */
        CHECK(native.runtime.session.rescue_count==1u &&
              native.water_recovery.phase==PORTABLE_WATER_RECOVERY_NONE);
        unsigned saw_actor=0u, saw_carry=0u, saw_touchdown=0u;
        unsigned local_sink_edges=0u,cpu_sink14=0u,cpu_sink15=0u,cpu_retrieval=0u,held_read_checked=0u;
        unsigned freeze_calls=0u,thaw_ticks=0u,drip_starts=0u,promoted=0u;
        for(unsigned tick=0u;tick<1200u;++tick)
        {
            const unsigned before_fx=native.hazard_effect_72&15u;
            const uint8_t previous_local_mode=native.player.hop.mode;
            const uint16_t previous_local_sink=native.player.fall_frames_76;
            uint8_t previous_cpu_mode[7];uint16_t previous_cpu_sink[7];
            for(unsigned slot=0u;slot<7u;++slot){
                const size_t other=portable_gp_race_session_other_racer(&native,slot);
                previous_cpu_mode[slot]=native.roster.racers[other].lightning_state_78;
                previous_cpu_sink[slot]=native.roster.racers[other].fall_frames_76;
            }
            const PortableGpRaceSessionStatus status=portable_gp_race_session_tick(&native,0u,&disposition);
            if(status!=PORTABLE_GP_RACE_SESSION_OK)
                fprintf(stderr,"native recovery tick=%u status=%u driver=%u mode=%u requests=%x actor=%u\n",
                    tick,status,native.runtime.last_driver_status,native.player.hop.mode,
                    native.player.hop.recovery_requests,native.hazard_lakitu.phase);
            CHECK(status==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(portable_gp_race_session_snapshot(&native,&snapshot)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(portable_gp_race_presentation_build(&native,&snapshot,&presentation_state));
            if(race.recovery_fx_rules){
                const unsigned mode=native.hazard_effect_72&15u;
                CHECK(!memcmp(&snapshot.recovery_fx,&native.recovery_fx,sizeof native.recovery_fx) &&
                    snapshot.recovery_fx_events==native.recovery_fx_events &&
                    snapshot.engine_sound.ice_thaw_started==!!(native.recovery_fx_events&PORTABLE_RECOVERY_FX_THAW_START));
                if(mode==(race.icy_recovery?12u:14u))promoted=1u;
                if(before_fx==12u || mode==12u){
                    ++freeze_calls;
                    if(mode==12u && !native.roster.racers[native.local_index].lightning_status_6a)
                        CHECK(snapshot.player_palette_bank==9u);
                }
                if(snapshot.recovery_fx_events&PORTABLE_RECOVERY_FX_THAW_START)
                    CHECK(snapshot.recovery_fx.pending_thaw && !snapshot.recovery_fx.thaw_age);
                if(snapshot.recovery_fx.thaw_age){++thaw_ticks;CHECK(snapshot.recovery_fx.thaw_age<=12u);}
                drip_starts+=!!(snapshot.recovery_fx_events&PORTABLE_RECOVERY_FX_DRIP_START);
                for(unsigned i=0u;i<PORTABLE_RECOVERY_DRIPS;++i)CHECK(snapshot.recovery_fx.drips[i].age<=9u);
            }
            if(hazard==1u){
                CHECK(snapshot.fall_frames_76==native.player.fall_frames_76 &&
                    native.roster.racers[native.local_index].fall_frames_76==native.player.fall_frames_76);
                if(native.player.hop.mode==2u && snapshot.fall_frames_76==14u){
                    local_sink_edges|=1u;
                    CHECK(!snapshot.driver_hidden && snapshot.player_height_q16==-14*32768);
                }
                if(snapshot.fall_frames_76==15u){
                    local_sink_edges|=2u;
                    CHECK(snapshot.driver_hidden && presentation_state.suppress_player);
                    if(native.player.hop.mode==2u)CHECK(snapshot.player_height_q16==-15*32768);
                    if(!held_read_checked){
                        /* GP host pause holds this immutable presentation; no
                         * sink counter may advance just by reading it again. */
                        PortableGpRaceSession *held=malloc(sizeof *held);CHECK(held!=NULL);
                        memcpy(held,&native,sizeof native);
                        PortableGpRaceSnapshot held_snapshot;
                        PortableRacePresentationState held_presentation;
                        CHECK(portable_gp_race_session_snapshot(&native,&held_snapshot)==0 &&
                            portable_gp_race_presentation_build(&native,&held_snapshot,&held_presentation));
                        CHECK(!memcmp(held,&native,sizeof native) && held_snapshot.driver_hidden &&
                            held_snapshot.fall_frames_76==15u && held_presentation.suppress_player);
                        free(held);held_read_checked=1u;
                    }
                }
                if(previous_local_mode==2u && native.player.hop.mode==6u){
                    local_sink_edges|=4u;CHECK(snapshot.fall_frames_76==15u && snapshot.driver_hidden);
                }
                if(previous_local_mode==6u && previous_local_sink==15u){
                    local_sink_edges|=8u;CHECK(!snapshot.fall_frames_76 && !snapshot.driver_hidden);
                }
                for(unsigned slot=0u;slot<7u;++slot){
                    const size_t other=portable_gp_race_session_other_racer(&native,slot);
                    const PortableGpRacer *cpu=&native.roster.racers[other];
                    const PortableGpOpponentSnapshot *opponent=&snapshot.opponents[slot];
                    CHECK(opponent->fall_frames_76==cpu->fall_frames_76);
                    if(cpu->lightning_state_78==2u && cpu->fall_frames_76==14u){
                        cpu_sink14|=1u<<slot;
                        CHECK(!opponent->driver_hidden && opponent->height_q16==-14*32768);
                    }
                    if(cpu->fall_frames_76==15u){
                        cpu_sink15|=1u<<slot;
                        CHECK(opponent->driver_hidden && !presentation_state.opponents[slot].visible);
                    }
                    if(previous_cpu_mode[slot]==2u && cpu->lightning_state_78==6u){
                        cpu_retrieval|=1u<<slot;CHECK(opponent->fall_frames_76==15u && opponent->driver_hidden);
                    }
                    if(previous_cpu_mode[slot]==6u && previous_cpu_sink[slot]==15u)
                        CHECK(!opponent->fall_frames_76 && !opponent->driver_hidden);
                }
            }
            if(hazard==2u && tick<2u)
                for(size_t rival=1u;rival<PORTABLE_GP_RACER_COUNT;++rival)
                    CHECK(native.roster.racers[rival].recovery_material==0x24u &&
                        native.roster.racers[rival].burn_ticks_72==249u-tick &&
                        native.roster.racers[rival].palette_bank==5u &&
                        snapshot.opponents[rival-1u].palette_bank==5u);
            if(splash && tick<28u){
                const unsigned age=tick<27u?tick+2u:0u;
                CHECK(snapshot.entry_splash.age==age && presentation_state.entry_splash.age==age &&
                    snapshot.entry_splash.x==120 && snapshot.entry_splash.y==splash_y);
            }
            saw_actor |= snapshot.lakitu_visible;
            if(native.player.hop.mode==6u)
            {
                saw_carry=1u;
                memset(terrain_cells,0,sizeof terrain_cells);
            }
            saw_touchdown |= native.player.hop.recovery_touchdown;
            unsigned fx_children=native.recovery_fx.thaw_age | native.recovery_fx.pending_thaw;
            for(unsigned i=0u;i<PORTABLE_RECOVERY_DRIPS;++i)fx_children|=native.recovery_fx.drips[i].age;
            if(saw_touchdown && !native.player.source_terrain22_active && !native.player.source_hazard_kind &&
               (!race.recovery_fx_rules || (promoted && (native.hazard_effect_72&15u)!=12u &&
                !native.recovery_fx.drip_emitter_ticks && !fx_children)))break;
        }
        CHECK(saw_actor && saw_carry && saw_touchdown);
        if(race.recovery_fx_rules){
            CHECK(promoted);
            if(race.icy_recovery)CHECK(freeze_calls==381u && thaw_ticks==12u && !drip_starts);
            else CHECK(!freeze_calls && !thaw_ticks && drip_starts>0u);
        }
        if(hazard==1u)CHECK(local_sink_edges==15u && cpu_sink14==127u && cpu_sink15==127u &&
            cpu_retrieval==127u && held_read_checked);
        CHECK(native.runtime.session.rescue_count==1u && native.player.hop.mode==0u);
        if(hazard==4u)
        {
            /* A custom track also uses 9904's shore escape and cancels the
             * pending Lakitu, instead of completing the fallback rescue. */
            memset(terrain_cells,7,sizeof terrain_cells);
            CHECK(test_gp_init(&native,&race,7u)==PORTABLE_GP_RACE_SESSION_OK);
            native.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
            native.runtime.session.countdown_remaining=0u;
            for(unsigned tick=0u;tick<86u;++tick)
                CHECK(portable_gp_race_session_tick(&native,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(native.player.hop.mode==4u && native.hazard_lakitu.phase==1u &&
                  native.water_recovery.phase==PORTABLE_WATER_RECOVERY_NONE);
            material_flags[7]=UINT8_C(0x40);
            CHECK(portable_gp_race_session_tick(&native,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(native.player.hop.mode==1u && native.player.hop.recovery_requests==0x200u &&
                  native.hazard_effect_72==0u);
            CHECK(portable_gp_race_session_tick(&native,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(native.player.hop.recovery_requests==0u && native.hazard_lakitu.phase==5u &&
                  !native.player.source_terrain22_active && native.runtime.session.rescue_count==1u);
        }
        race.recovery_fx_rules=NULL;race.icy_recovery=0u;
        race.recovery_bob_offsets=NULL;
        race.dependencies.source_track_route=NULL;
        race.dependencies.source_track_route_count=0u;
        race.rules.compatibility.course_id=25u;
        memset(terrain_cells,7,sizeof terrain_cells);
        material_flags[7]=0x22u;
    }

    /* A production player/Bowser response enters source sub_803E490 and
     * therefore records score event +0x1a exactly once. */
    session.roster.racers[2].position_x_q16 =
        session.player.pose.position_x;
    session.roster.racers[2].position_y_q16 =
        session.player.pose.position_y;
    session.player.motion.speed_q16 = 0;
    session.player.motion.acceleration_q16 = 0;
    session.player.motion.component_x = 0;
    session.player.motion.component_y = 0;
    session.roster.racers[2].banana_hit_timer_38 = 0u;
    session.roster.racers[0].banana_hit_timer_38 = 0u;
    {
        PortableGpRaceSessionStatus contact_status =
            portable_gp_race_session_tick(&session, 0u, &disposition);
        /* This reduced fixture omits the response table: the shared contact
         * is accepted, but local response adoption fails before score commit. */
        CHECK(contact_status == PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR &&
              session.roster.racers[0].kart_contact_events == 1u &&
              session.driving_quality.event_1a == 0u);
    }

    session.runtime.session.phase = PORTABLE_RACE_PHASE_FINISHED;
    session.runtime.session.laps_completed = 3u;
    session.runtime.session.elapsed_ticks = 6000u;
    session.runtime.session.lap_ticks[0] = 1900u;
    session.runtime.session.lap_ticks[1] = 2000u;
    session.runtime.session.lap_ticks[2] = 2100u;
    /* This finish begins in a real impact response and needs its C0 table. */
    const int32_t finish_response_entries[8] = {-4,-8,-16,-24,-36,-56,-64,-85};
    const PortableSourceNormalResponseTableView finish_response = {
        finish_response_entries, 8u,
    };
    session.player_dependencies.normal_response_table = &finish_response;
    session.player.steering.response_status = UINT16_C(0x000b);
    session.player.steering.base_angle = INT16_C(0x05fe);
    session.player.steering.driver_mode = 11u;
    session.player.auto_acceleration.driver_mode = 11u;
    session.player.steering.state_flags = UINT32_C(0x800c);
    const uint16_t finish_retained_control=session.player.input.control_word;
    const uint32_t finish_art_before=session.item_art_clock.calls;
    CHECK(portable_gp_race_session_tick(&session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          disposition == PORTABLE_RACE_TICK_FINISHED &&
          session.player.input.control_word == finish_retained_control &&
          (session.roster.race_phase_7d4 & UINT8_C(0x0f)) == 4u &&
          session.roster.race_mode_flags_7da == UINT16_C(0x0100) &&
          (session.roster.racers[0].source_flags & UINT32_C(0x20)) != 0u);
    CHECK(session.item_art_clock.calls==finish_art_before+1u);
    /* This minimal fixture has no autonomous tables: retirement preserves84
     * and96. Full imported profiles settle mode11 through BD24 itself. */
    CHECK(session.player.steering.response_status == 0u);
    CHECK(session.player.steering.base_angle == 0);
    CHECK(session.player.steering.driver_mode == 11u);
    CHECK(session.player.auto_acceleration.driver_mode == 11u);
    CHECK(session.player.steering.state_flags == UINT32_C(0xa034));
    CHECK(session.roster.racers[0].source_flags == UINT32_C(0xa034));
    CHECK((session.roster.racers[0].lap_marker_a4 & UINT16_C(0x4000)) != 0u);
    CHECK(portable_gp_race_session_tick(&session, 0u, &disposition) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          session.player.input.control_word == finish_retained_control &&
          (session.roster.race_phase_7d4 & UINT8_C(0x0f)) == 5u);
    CHECK(session.item_art_clock.calls==finish_art_before+2u);
    CHECK(portable_gp_race_session_snapshot(&session, &snapshot) ==
              PORTABLE_GP_RACE_SESSION_OK &&
          snapshot.results_ready && snapshot.finish_transition_phase == 5u &&
          snapshot.total_ticks == 6000u && snapshot.lap_ticks[0] == 1900u &&
          snapshot.lap_ticks[1] == 2000u && snapshot.lap_ticks[2] == 2100u);

    /* Full synthetic source tables exercise the autonomous owner rather than
     * the old application-only heading correction on an incomplete fixture. */
    uint8_t finish_targets[0x1a0]={0};
    static const uint16_t finish_angles[16384]={0};
    const int16_t finish_deceleration[8]={-2,-4,-8,-12,-16,-20,-24,-28};
    session.player_dependencies.source_track_route=route;
    session.player_dependencies.source_track_route_count=47u;
    session.player_dependencies.source_track_angle_lut=finish_angles;
    session.player_dependencies.source_track_angle_lut_count=16384u;
    session.player_dependencies.source_angle_target_table=finish_targets;
    session.player_dependencies.source_angle_target_table_count=sizeof finish_targets;
    session.player_dependencies.source_auto_deceleration_table=finish_deceleration;
    session.player_dependencies.source_auto_deceleration_count=8u;
    session.player.steering.driver_mode=session.player.auto_acceleration.driver_mode=0u;
    session.player.normal_collision_pending=0u;
    material_flags[7]=0x40u;
    /* BC00 steps toward large signed heading gaps after the player finishes. */
    for(unsigned negative=0u;negative<2u;++negative)
    {
        PortableGpRaceSession steering=session;
        steering.runtime.recovered_driver=&steering.player;
        steering.runtime.recovered_dependencies=&steering.player_dependencies;
        steering.player.motion.angle=0;
        steering.player.motion.component_x=0;
        steering.player.motion.component_y=0;
        steering.player.steering.angle=0;
        steering.player.steering.steering_offset=0;
        steering.player.steering.source_angle_56=negative?(-INT16_C(8192)):0x2000;
        CHECK(portable_gp_race_session_tick(&steering,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK((uint16_t)steering.player.motion.angle==(negative?0xfa00u:0x0600u));
    }

    /* Finished autopilot keeps its accepted checkpoint on both sentinels
     * and finite-map misses, instead of aborting while results are visible. */
    for(unsigned outside=0u;outside<3u;++outside)
    {
        uint8_t angle_targets[0x1a0]={0};
        uint8_t saved_cells[sizeof raster_cells];
        memcpy(saved_cells,raster_cells,sizeof raster_cells);
        memset(raster_cells,outside==0u?127:255,sizeof raster_cells);
        PortableGpRaceSession autopilot=session;
        autopilot.runtime.recovered_driver=&autopilot.player;
        autopilot.runtime.recovered_dependencies=&autopilot.player_dependencies;
        autopilot.player_dependencies.source_track_route=route;
        autopilot.player_dependencies.source_track_route_count=47u;
        autopilot.player_dependencies.source_angle_target_table=angle_targets;
        autopilot.player_dependencies.source_angle_target_table_count=sizeof angle_targets;
        autopilot.player.motion.angle=0;
        autopilot.player.motion.speed_q16=0;
        autopilot.player.motion.acceleration_q16=0;
        autopilot.player.motion.component_x=0;
        autopilot.player.motion.component_y=0;
        autopilot.player.steering.source_angle_56=0;
        autopilot.player.steering.angle=0;
        autopilot.player.steering.steering_offset=0;
        autopilot.player.pose.position_x=(outside==2u?-16:888)*65536;
        autopilot.player.pose.position_y=612*65536;
        autopilot.roster.racers[0].route_index=45u;
        CHECK(portable_gp_race_session_tick(&autopilot,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(autopilot.roster.racers[0].route_index==45u);
        memcpy(raster_cells,saved_cells,sizeof raster_cells);
    }

    CHECK(test_gp_init(NULL, &race, 7u) ==
          PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT);
    CHECK(test_gp_init(&session, &race, 6u) ==
          PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT);
    race.rules.compatibility.course_id = 25u;
    race.rules.start_x_q16 = INT32_C(1752) << 16u;
    race.rules.start_y_q16 = INT32_C(1220) << 16u;
    route[45] = (PortableRacePoint){1772, 1396};
    CHECK(test_gp_init(&session, &race, 7u) ==
          PORTABLE_GP_RACE_SESSION_OK);
    CHECK(session.player.pose.position_x == (INT32_C(1772) << 16u) &&
          session.player.pose.position_y == (INT32_C(1396) << 16u) &&
          session.roster.course_id == 25u &&
          session.roster.racers[7].position_x_q16 ==
              (INT32_C(1732) << 16u) &&
          session.roster.racers[7].position_y_q16 ==
              (INT32_C(1228) << 16u));
    {
        PortableGpRaceSnapshot height_snapshot;
        PortableRacePresentationState height_presentation;
        session.roster.racers[1].world_height_q16 = INT32_C(0x28000);
        session.roster.racers[1].contact_height_24 = 900u;
        CHECK(portable_gp_race_session_snapshot(&session, &height_snapshot) ==
            PORTABLE_GP_RACE_SESSION_OK);
        CHECK(height_snapshot.opponents[0].height_q16 == INT32_C(0x28000));
        CHECK(portable_gp_race_presentation_build(&session, &height_snapshot,
            &height_presentation));
        CHECK(height_presentation.opponents[0].height_q16 == INT32_C(0x28000));
    }
    /* Engine selection precedes initialization, including the restart state.
     * Legacy packs reject unavailable classes rather than running 50cc. */
    PortableRecoveredRaceView legacy_race=race;
    legacy_race.gp_engines=NULL;
    legacy_race.gp_engine_count=0u;
    CHECK(test_gp_init_engine_grid(
        &session,&legacy_race,7u,NULL,1u)==PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT);
    PortableGpEngineProfile engines[3]={0};
    PortableEngineSoundRules engine_sounds={0};
    for(unsigned i=0u;i<3u;++i)
        engine_sounds.profiles[i]=(PortableEngineRevProfile){(int16_t)(700+i*100),
            (int16_t)(7+i),-100,-40,-30};
    const PortableEngineSoundRules *saved_engine_sounds=race.engine_sound_rules;
    race.engine_sound_rules=&engine_sounds;
    int16_t boosted_local[16]={0};
    race.gp_engines=engines;
    race.gp_engine_count=3u;
    race.dependencies.forward_local_table=boosted_local;
    for (uint8_t engine=0u;engine<3u;++engine)
    {
        for (unsigned driver=0u;driver<8u;++driver)
            engines[engine].cpu.acceleration[driver][0]=(uint8_t)(engine+3u);
        engines[engine].player_base_speed_q16=(900+engine*100)*65536;
        engines[engine].player_forward_limits[10]=(int16_t)(800+engine*100);
        CHECK(test_gp_init_engine_grid(
            &session,&race,7u,NULL,engine)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(session.engine_class==engine);
        CHECK(session.engine_rev_profile.limit==700+engine*100 &&
              session.engine_rev_profile.acceleration==7+engine);
        CHECK(session.roster.racers[1].acceleration_q16==(int32_t)(engine+3u)*4096);
        CHECK(session.runtime.recovered_initial_state.input.base_speed_q16==
              engines[engine].player_base_speed_q16);
        CHECK(session.rules.cpu_tuning==&engines[engine].cpu);
        CHECK(session.player.input.base_speed_q16==engines[engine].player_base_speed_q16);
        CHECK(session.player_dependencies.forward_limit_thresholds[10]==800+engine*100);
        CHECK(session.player_dependencies.forward_local_table==
            (engine==2u?boosted_local:race.gp_forward_local_table));
        CHECK(session.player_dependencies.source_angle_target_table==engines[engine].angle_targets);
    }
    race.engine_sound_rules=saved_engine_sounds;
    CHECK(test_gp_init_engine_grid(
        &session,&race,7u,NULL,3u)==PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT);
    /* All eight combinations select the initial clock/finish marker and
     * actual gameplay placements, without mutating the pack view. */
    {
        const PortableRecoveredCoin coin = {.position_x_q16=908*65536,
            .position_y_q16=788*65536};
        race.dependencies.owned_terrain_cells=terrain_cells;
        race.dependencies.owned_terrain_capacity=sizeof terrain_cells;
        race.coins=&coin;race.coin_count=1u;
        race.course_objects=&item_box;race.course_object_count=1u;
        for(uint8_t mask=0u;mask<8u;++mask) {
            memset(terrain_cells,0,sizeof terrain_cells);
            const PortableQuickRunOptions options={mask&1u,(mask>>1u)&1u,(mask>>2u)&1u,0u};
            CHECK(test_gp_init_quick(&session,&race,7u,2u,options)==
                PORTABLE_GP_RACE_SESSION_OK);
            CHECK(session.rules.lap_count==(options.five_laps?5u:3u) &&
                session.runtime.session.lap_count==session.rules.lap_count &&
                session.roster.race_mode_flags_7da==0x110u);
            CHECK(session.item_box_count==(options.items_off?0u:1u) &&
                session.coin_count==(options.coins_off?0u:1u));
            CHECK(terrain_cells[(788u/8u)*256u+908u/8u]==(options.coins_off?0u:0xc0u));
            for(unsigned racer=0u;racer<8u;++racer)
                CHECK(session.roster.racers[racer].coin_count_178==(options.coins_off?0u:5u));
            /* D4E4 consults the option even when a caller has supplied coins. */
            session.roster.racers[0].coin_count_178=5u;
            session.player.steering.state_flags=0x8000u;
            CHECK(portable_gp_race_session_apply_coin_loss(&session,2u)==
                PORTABLE_GP_RACE_SESSION_OK);
            CHECK(session.recovery_coin_loss==(options.coins_off?0u:2u) &&
                session.roster.racers[0].coin_count_178==(options.coins_off?5u:3u));
        }
        CHECK(race.coin_count==1u && race.course_object_count==1u && race.rules.lap_count==3u);
        CHECK(test_gp_init_quick(&session,&race,7u,2u,
            (PortableQuickRunOptions){2u,0u,0u,0u})==PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT);
    }
    /* Four directional starts share the production GP runtime with zero laps.
     * Unused storage must never move, collect items or keep Battle alive. */
    {
        PortableRecoveredRaceView arena_race=race;
        const PortableGpGridRow grid[4]={
            {600*65536,600*65536,0u},{800*65536,600*65536,0x8000u},
            {600*65536,800*65536,0x4000u},{800*65536,800*65536,0xc000u}};
        const PortableBattleItemRules battle={.table={
            {{0x1007u,10u},{0u,255u}},{{0x1002u,10u},{0u,255u}},
            {{0x1000u,10u},{0u,255u}}},.box_hidden_ticks=120u,.elimination_ticks=10u};
        arena_race.rules.progress_kind=PORTABLE_RACE_PROGRESS_ARENA_RASTER_V1;
        arena_race.rules.finish_kind=PORTABLE_FINISH_NONE;
        arena_race.rules.lap_count=0u;
        arena_race.rules.finish_y_q16=0;
        arena_race.rules.compatibility.course_id=96u;
        arena_race.gp_grid=grid;arena_race.gp_grid_count=4u;
        arena_race.battle_items=&battle;
        arena_race.course_objects=NULL;arena_race.course_object_count=0u;
        PortableGpRaceSession arena;
        CHECK(test_gp_init(&arena,&arena_race,7u)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(arena.rules.progress_kind==PORTABLE_RACE_PROGRESS_ARENA_RASTER_V1 &&
            arena.runtime.session.lap_count==0u && arena.source_result_clock.lap_count==0u &&
            arena.roster.race_mode_flags_7da==0x800u && arena.roster.race_phase_7d4==3u &&
            portable_gp_race_roster_count(&arena.roster)==4u && arena.coin_count==0u);
        for (unsigned index=0u;index<8u;++index) {
            const PortableGpRacer *r=&arena.roster.racers[index];
            CHECK(r->rank_flags_ba==index*2u && arena.roster.standings[index]==index);
            CHECK(r->coin_count_178==0u && r->balloons_remaining==(index<4u?3u:0u));
            if (index<4u) CHECK(r->position_x_q16==grid[index].x_q16 &&
                r->position_y_q16==grid[index].y_q16 && r->heading==grid[index].heading);
            else CHECK(r->source_flags==0x20u && !r->decision_valid);
        }
        const PortableGpRacer unused=arena.roster.racers[4];
        for (unsigned tick=0u;tick<18u;++tick)
            CHECK(portable_gp_race_session_tick(&arena,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(memcmp(&unused,&arena.roster.racers[4],sizeof unused)==0 &&
            arena.runtime.session.phase==PORTABLE_RACE_PHASE_RACING &&
            arena.runtime.session.laps_completed==0u && arena.source_result_clock.laps_recorded==0u);
        CHECK(portable_gp_race_session_snapshot(&arena,&snapshot)==PORTABLE_GP_RACE_SESSION_OK);
        for (unsigned rival=0u;rival<7u;++rival) CHECK(snapshot.opponents[rival].visible==(rival<3u));
        CHECK(portable_gp_race_presentation_build(&arena,&snapshot,&presentation_state));
        CHECK(presentation_state.show_battle && presentation_state.lap_count==0u &&
            presentation_state.opponent_count==3u && presentation_state.balloons_remaining==3u);
        /* Released projectiles enter the real contact manager, then C444
         * consumes damage in the victim's driver pass, including B550 hold. */
        for (unsigned victim=0u;victim<2u;++victim)
        for (unsigned impact=0u;impact<3u;++impact) {
            PortableGpRaceSession combat;
            CHECK(test_gp_init(&combat,&arena_race,7u)==0);
            for (unsigned tick=0u;tick<18u;++tick)
                CHECK(portable_gp_race_session_tick(&combat,0u,&disposition)==0);
            PortableGpRacer *target=&combat.roster.racers[victim];
            combat.roster.racers[2].green_shell=(PortableGpGreenShell){.rules=test_green_shell_rules(),.orbit_rules=test_shell_orbit_rules(),
                .position_x_q16=target->position_x_q16,
                .position_y_q16=target->position_y_q16,
                .kind_10=PORTABLE_GP_GREEN_SHELL_KIND,
                .phase=PORTABLE_GP_GREEN_SHELL_RELEASED,.occupied=1u,
                .lifetime_38=1u,.state_71=48u};
            CHECK(portable_gp_race_session_tick(&combat,0u,&disposition)==0);
            CHECK(combat.roster.green_shell_hits==1u);
            /* The contact manager follows player motion and precedes CPU
             * motion: a CPU consumes this hit now, the player next tick. */
            if (victim==0u) {
                CHECK(target->balloons_remaining==3u && (target->hit_state_flags_74&1u)!=0u);
                CHECK(portable_gp_race_session_tick(&combat,0u,&disposition)==0);
            }
            CHECK(target->balloons_remaining==2u && target->balloon_event==PORTABLE_BATTLE_BALLOON_POP &&
                (target->hit_state_flags_74&0x8000u)!=0u && target->banana_hit_timer_38!=0u);
            /* Another pending writer cannot pop on the latch-clearing turn. */
            target->hit_state_flags_74|=1u;
            CHECK(portable_gp_race_session_tick(&combat,0u,&disposition)==0);
            CHECK(target->balloons_remaining==2u && target->balloon_event==PORTABLE_BATTLE_BALLOON_NONE &&
                (target->hit_state_flags_74&0x8001u)==1u);
            CHECK(portable_gp_race_session_tick(&combat,0u,&disposition)==0);
            CHECK(target->balloons_remaining==1u && target->balloon_event==PORTABLE_BATTLE_BALLOON_POP);
            /* Scene4 suppression uses its own field, not the race phase. */
            target->hit_state_flags_74=1u;combat.roster.battle_scene_state=4u;
            CHECK(portable_gp_race_session_tick(&combat,0u,&disposition)==0);
            CHECK(target->balloons_remaining==1u && (target->hit_state_flags_74&1u)==0u &&
                target->balloon_event==PORTABLE_BATTLE_BALLOON_NONE && combat.roster.race_phase_7d4==3u);
            combat.roster.battle_scene_state=3u;
            target->hit_state_flags_74=1u;
            CHECK(portable_gp_race_session_tick(&combat,0u,&disposition)==0);
            CHECK(target->balloons_remaining==0u && target->balloon_event==PORTABLE_BATTLE_BALLOON_ELIMINATED);
            CHECK(portable_gp_race_session_snapshot(&combat,&snapshot)==0);
            CHECK((victim==0u?snapshot.player_balloons_remaining:
                snapshot.opponents[0].balloons_remaining)==0u);
            CHECK(portable_gp_race_presentation_build(&combat,&snapshot,&presentation_state));
            CHECK(presentation_state.show_battle && presentation_state.opponent_count==3u);
            /* C444 enters B020; the late D328 pass consumes tick one now.
             * Conversion retains the kart class and cannot restart on the
             * persistent death event while the finish scene is frozen. */
            const uint8_t handling_class=target->driver_class;
            CHECK(target->elimination.remaining_ticks==9u && !target->elimination.revenge_bomb);
            for (unsigned tick=0u;tick<8u;++tick) {
                CHECK(portable_gp_race_session_tick(&combat,0u,&disposition)==0);
                CHECK(target->elimination.remaining_ticks==8u-tick && !target->elimination.revenge_bomb);
            }
            const uint64_t before_role=portable_gp_race_roster_identity(&combat.roster);
            CHECK(portable_gp_race_session_tick(&combat,0u,&disposition)==0);
            CHECK(target->elimination.remaining_ticks==0u && target->elimination.revenge_bomb &&
                  target->driver_class==handling_class && target->balloons_remaining==0u);
            CHECK(portable_gp_race_roster_identity(&combat.roster)!=before_role);
            CHECK(portable_gp_race_session_snapshot(&combat,&snapshot)==0);
            CHECK((victim==0u?snapshot.player_revenge_bomb:
                snapshot.opponents[0].revenge_bomb)==1u);
            CHECK((victim==0u?snapshot.player_bomb_walk_phase:
                snapshot.opponents[0].bomb_walk_phase)==(target->contact_flags_6d>>1u));
            CHECK(((snapshot.engine_sound.character_flags&8u)!=0u)==(victim==0u));
            if (victim==0u)
                CHECK(snapshot.engine_sound.bomb_walk_audio==combat.player_bomb_walk_audio);
            CHECK(portable_gp_race_presentation_build(&combat,&snapshot,&presentation_state));
            CHECK((victim==0u?presentation_state.revenge_bomb:
                presentation_state.opponents[0].revenge_bomb)==1u);
            CHECK(portable_gp_race_session_tick(&combat,0u,&disposition)==0);
            CHECK(target->elimination.remaining_ticks==0u && target->elimination.revenge_bomb);
            /* F8C8 detonates a bomb before normal item response. Keep the
             * existing shell case, plus dropped and genuinely held bananas.
             * A94C then holds60 turns before returning to this racer's GRID. */
            const int16_t start_x=target->arena_start_x,start_y=target->arena_start_y;
            const uint16_t start_heading=target->arena_start_heading;
            PortableGpRacer *owner=&combat.roster.racers[2];
            if (impact==0u)
                owner->green_shell=(PortableGpGreenShell){.rules=test_green_shell_rules(),.orbit_rules=test_shell_orbit_rules(),
                    .position_x_q16=target->position_x_q16,.position_y_q16=target->position_y_q16,
                    .kind_10=PORTABLE_GP_GREEN_SHELL_KIND,.phase=PORTABLE_GP_GREEN_SHELL_RELEASED,
                    .occupied=1u,.lifetime_38=1u,.state_71=48u};
            else if (impact==1u)
                owner->banana=(PortableGpBanana){.rules=test_banana_rules(),
                    .position_x_q16=target->position_x_q16,.position_y_q16=target->position_y_q16,
                    .kind_10=PORTABLE_GP_BANANA_KIND,.flags_34=3u,
                    .phase=PORTABLE_GP_BANANA_DROPPED,.occupied=1u,.collision_active=1u};
            else
            {
                owner->item_inventory=(PortableGpItemInventory){.held_item_17a=PORTABLE_GP_BANANA_ITEM};
                owner->item_decision_190=1u;
                CHECK(portable_gp_race_roster_use_banana(&combat.roster,2u,
                    combat.player_dependencies.sine_table,combat.player_dependencies.sine_count)==0);
                CHECK(owner->banana.phase==PORTABLE_GP_BANANA_HELD &&
                      owner->shell_item_18c==PORTABLE_GP_BANANA_ITEM);
                /* Move the stationary fixture owner by the actual source
                 * attachment offset, so its next held update crosses the
                 * unchanged victim pose without overlapping the owner kart. */
                owner->position_x_q16+=target->position_x_q16-owner->banana.position_x_q16;
                owner->position_y_q16+=target->position_y_q16-owner->banana.position_y_q16;
                owner->speed_q16=owner->acceleration_q16=0;
                owner->component_x=owner->component_y=0;
            }
            const uint32_t banana_hits=combat.roster.banana_hits;
            const uint16_t coins=target->coin_count_178;
            const uint16_t prior_response=target->mushroom_response_flags_b4;
            const int detonation_status=portable_gp_race_session_tick(&combat,0u,&disposition);
            if(detonation_status) fprintf(stderr,"detonate victim=%u impact=%u status=%d stage=%u pilot_mode=%u hazard=%u cpu_mode=%u recovery=%u flags=%x\n",victim,impact,detonation_status,combat.failed_roster_stage,combat.player.hop.mode,combat.player.source_hazard_kind,target->lightning_state_78,target->recovery_material,target->mushroom_response_flags_b4);
            CHECK(detonation_status==0);
            CHECK(target->lightning_state_78==9u && target->mushroom_mode_84==2u &&
                  target->balloons_remaining==0u && target->elimination.revenge_bomb &&
                  target->recovery_target_x_9e==start_x && target->recovery_target_y_a0==start_y &&
                  target->recovery_target_heading_a2==start_heading);
            if (impact!=0u)
            {
                CHECK(owner->banana.phase==PORTABLE_GP_BANANA_DESTROYING &&
                      !owner->banana.collision_active && !owner->shell_item_18c &&
                      !(owner->source_flags&UINT32_C(0x10000)) &&
                      combat.roster.banana_hits==banana_hits && target->coin_count_178==coins &&
                      target->balloon_event==PORTABLE_BATTLE_BALLOON_NONE &&
                      target->mushroom_response_flags_b4==prior_response && !target->hit_spin_82 &&
                      !target->banana_reaction_audio_pending);
                for (unsigned audio=0u;audio<combat.audio_event_count;++audio)
                    CHECK(combat.audio_events[audio].song_id!=135u);
            }
            /* Local collision is after C4C0; CPU collision precedes its turn. */
            const unsigned hold_remaining=victim==0u?60u:59u;
            for (unsigned tick=0u;tick<hold_remaining;++tick) {
                const int respawn_status=portable_gp_race_session_tick(&combat,PORTABLE_INPUT_ACCELERATE,&disposition);
                if(respawn_status) fprintf(stderr,"respawn victim=%u tick=%u status=%d stage=%u pilot_mode=%u hazard=%u timer=%u driver=%u cpu_mode=%u recovery=%u\n",victim,tick,respawn_status,combat.failed_roster_stage,combat.player.hop.mode,combat.player.source_hazard_kind,combat.player.drift_charge.charge_counter,combat.player.steering.driver_mode,target->lightning_state_78,target->recovery_material);
                CHECK(respawn_status==0);
                CHECK((victim==0u?combat.player.hop.mode:target->lightning_state_78)==9u);
                CHECK((victim==0u?combat.player.motion.speed_q16:target->speed_q16)==0);
            }
            if (impact!=0u)
                CHECK(!owner->banana.occupied && owner->banana.phase==PORTABLE_GP_BANANA_DESTROYED &&
                      combat.roster.banana_hits==banana_hits && target->balloons_remaining==0u);
            CHECK(portable_gp_race_session_tick(&combat,0u,&disposition)==0);
            CHECK((victim==0u?combat.player.hop.mode:target->lightning_state_78)==6u &&
                  (victim==0u?combat.player.pose.position_z:target->world_height_q16)==48*65536);
            unsigned recovery_ticks=0u;
            while ((victim==0u?combat.player.hop.mode:target->lightning_state_78)!=0u && recovery_ticks<1000u) {
                CHECK(portable_gp_race_session_tick(&combat,0u,&disposition)==0);
                ++recovery_ticks;
            }
            CHECK(recovery_ticks>96u && recovery_ticks<1000u && target->elimination.revenge_bomb &&
                  target->position_x_q16/65536==start_x && target->position_y_q16/65536==start_y &&
                  target->heading==start_heading && target->world_height_q16==0 && target->balloons_remaining==0u);
            combat.roster.racers[2].green_shell=(PortableGpGreenShell){.rules=test_green_shell_rules(),.orbit_rules=test_shell_orbit_rules(),
                .position_x_q16=target->position_x_q16,.position_y_q16=target->position_y_q16,
                .kind_10=PORTABLE_GP_GREEN_SHELL_KIND,.phase=PORTABLE_GP_GREEN_SHELL_RELEASED,
                .occupied=1u,.lifetime_38=1u,.state_71=48u};
            CHECK(portable_gp_race_session_tick(&combat,0u,&disposition)==0);
            CHECK(target->lightning_state_78==9u && target->elimination.revenge_bomb &&
                  target->balloons_remaining==0u);
        }
        /* Ordinary session overlap dispatches the revenge-bomb branch before
         * normal kart/Star responses, for either local side and CPU pairs. */
        for (unsigned pair=0u;pair<3u;++pair) for (unsigned variant=0u;variant<3u;++variant) {
            const unsigned bomb_index=pair==0u?0u:pair==1u?1u:2u;
            const unsigned victim_index=pair==0u?1u:pair==1u?0u:3u;
            PortableGpRaceSession contact;
            CHECK(test_gp_init(&contact,&arena_race,7u)==0);
            for (unsigned tick=0u;tick<18u;++tick)
                CHECK(portable_gp_race_session_tick(&contact,0u,&disposition)==0);
            PortableGpRacer *bomb=&contact.roster.racers[bomb_index];
            PortableGpRacer *victim=&contact.roster.racers[victim_index];
            bomb->elimination.revenge_bomb=1u;bomb->balloons_remaining=0u;
            if (variant==2u) { victim->elimination.revenge_bomb=1u;victim->balloons_remaining=0u; }
            const int32_t center=(pair==2u?700:600)*65536;
            bomb->position_x_q16=center;bomb->position_y_q16=center;
            victim->position_x_q16=center+4*65536;victim->position_y_q16=center;
            bomb->component_x=bomb->component_y=victim->component_x=victim->component_y=0;
            bomb->speed_q16=bomb->acceleration_q16=victim->speed_q16=victim->acceleration_q16=0;
            if (variant==1u) {
                victim->hit_cue_flags_58|=0x8000u;
                victim->lightning_timer_68=60u;victim->lightning_status_6a=12;
            }
            if (pair<2u) {
                const PortableGpRacer *local=&contact.roster.racers[0];
                contact.player.pose.position_x=local->position_x_q16;
                contact.player.pose.position_y=local->position_y_q16;
                contact.player.pose.component_x=contact.player.pose.component_y=0;
                contact.player.motion.component_x=contact.player.motion.component_y=0;
                contact.player.motion.speed_q16=contact.player.motion.acceleration_q16=0;
            }
            CHECK(portable_gp_race_session_tick(&contact,0u,&disposition)==0);
            CHECK(bomb->lightning_state_78==9u && bomb->elimination.revenge_bomb &&
                  bomb->balloons_remaining==0u && contact.roster.contact_events==1u);
            unsigned explosion_sounds=0u;
            for (unsigned i=0u;i<contact.audio_event_count;++i) {
                const PortableGpRaceAudioEvent *event=&contact.audio_events[i];
                if (event->song_id!=390u) continue;
                CHECK(event->player==7u && event->volume>0u && event->volume<=256u && event->pitch==0);
                ++explosion_sounds;
            }
            CHECK(explosion_sounds==(variant==2u?2u:1u));
            if (variant==2u) CHECK(victim->lightning_state_78==9u && victim->elimination.revenge_bomb);
            else {
                if(victim->balloons_remaining!=(variant==1u?3u:2u))
                    fprintf(stderr,"bomb contact pair=%u variant=%u health=%u flags74=%x event=%u cue=%x\n",pair,variant,victim->balloons_remaining,victim->hit_state_flags_74,victim->balloon_event,victim->hit_cue_flags_58);
                CHECK(victim->balloons_remaining==(variant==1u?3u:2u));
            }
        }
        arena.roster.racers[0].balloons_remaining=1u;
        arena.roster.racers[1].balloons_remaining=2u;
        arena.roster.racers[2].balloons_remaining=3u;
        arena.roster.racers[3].balloons_remaining=0u;
        CHECK(portable_gp_race_roster_update_standings(&arena.roster,0u)==PORTABLE_GP_ROSTER_OK);
        CHECK(arena.roster.standings[0]==2u && arena.roster.standings[1]==1u &&
            arena.roster.standings[2]==0u && arena.roster.standings[3]==3u);
        CHECK(portable_gp_race_roster_begin_item_box(&arena.roster,4u,0x8800u,0u)==PORTABLE_GP_ROSTER_INVALID_ARGUMENT);
        /* Arena raster cells retain recovery checkpoints without B7B8 laps. */
        PortableRaceRulesView checkpoint_rules=arena.rules;
        checkpoint_rules.finish_y_q16=700*65536;
        PortableGpRacer checkpoint_racer=arena.roster.racers[1];
        checkpoint_racer.contact_center_y=600;
        checkpoint_racer.position_y_q16=800*65536;
        const uint8_t prior_wrap=checkpoint_racer.route_wrap_91;
        uint8_t saved_raster[sizeof raster_cells];
        memcpy(saved_raster,raster_cells,sizeof saved_raster);
        memset(raster_cells,0xadu,sizeof raster_cells);
        CHECK(portable_gp_race_roster_track_progress(&checkpoint_racer,&checkpoint_rules,&raster,0u)==PORTABLE_GP_ROSTER_OK);
        CHECK(checkpoint_racer.route_index==45u && checkpoint_racer.route_wrap_91==prior_wrap && !checkpoint_racer.lap_marker_a4);
        memcpy(raster_cells,saved_raster,sizeof saved_raster);
        arena.roster.racers[0].hit_state_flags_74=1u;
        arena.roster.racers[1].balloons_remaining=1u;
        arena.roster.racers[1].hit_state_flags_74=1u;
        arena.roster.racers[4].balloons_remaining=3u; /* Unused capacity cannot keep Battle alive. */
        CHECK(portable_gp_race_session_tick(&arena,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(arena.roster.racers[0].balloon_event==PORTABLE_BATTLE_BALLOON_ELIMINATED &&
            arena.roster.racers[1].balloon_event==PORTABLE_BATTLE_BALLOON_ELIMINATED);
        CHECK(arena.runtime.session.phase==PORTABLE_RACE_PHASE_FINISHED &&
            arena.roster.race_phase_7d4==4u && arena.roster.finish_countdown==180u &&
            arena.player.motion.speed_q16==0 && arena.roster.finish_player_rank==2u);
        const int32_t finish_x=arena.player.pose.position_x;
        const uint32_t finish_clock=arena.source_result_clock.centiseconds;
        for (unsigned tick=0u;tick<180u;++tick) {
            CHECK(portable_gp_race_session_tick(&arena,PORTABLE_INPUT_ACCELERATE,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
            CHECK(arena.roster.race_phase_7d4==4u);
        }
        CHECK(portable_gp_race_session_tick(&arena,0u,&disposition)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(portable_gp_race_session_snapshot(&arena,&snapshot)==PORTABLE_GP_RACE_SESSION_OK);
        CHECK(snapshot.results_ready && !snapshot.source_result_times_ready && snapshot.lap_count==0u &&
            arena.roster.race_phase_7d4==5u && arena.player.pose.position_x==finish_x &&
            arena.source_result_clock.centiseconds==finish_clock);
        arena_race.battle_items=NULL;
        CHECK(test_gp_init(&arena,&arena_race,7u)==PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT);
        arena_race.battle_items=&battle;arena_race.gp_grid_count=8u;
        CHECK(test_gp_init(&arena,&arena_race,7u)==PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT);
        arena_race.gp_grid_count=4u;
        const uint8_t invalid_order[8]={0u,1u,4u,3u,4u,5u,6u,7u};
        CHECK(test_gp_init_grid(&arena,&arena_race,7u,invalid_order)==PORTABLE_GP_RACE_SESSION_ROSTER_ERROR);
        CHECK(test_gp_init_quick(&arena,&arena_race,7u,0u,
            (PortableQuickRunOptions){0})==PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT);
    }
    puts("portable GP race session: PASS");
    return 0;
}

/* main() holds dozens of full race fixtures; give it a 64 MiB stack. */
#ifdef _WIN32
int main(void) { return run_tests(); }
#else
static void *run_tests_thread(void *result)
{
    *(int *)result=run_tests();
    return NULL;
}

int main(void)
{
    pthread_attr_t attr;
    pthread_t thread;
    int result=1;
    if(pthread_attr_init(&attr)!=0 || pthread_attr_setstacksize(&attr,(size_t)64u<<20u)!=0 ||
       pthread_create(&thread,&attr,run_tests_thread,&result)!=0 || pthread_join(thread,NULL)!=0)
    {
        fprintf(stderr,"cannot start the test thread\n");
        return 1;
    }
    pthread_attr_destroy(&attr);
    return result;
}
#endif
