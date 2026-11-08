#include "mesh_course_fixture.h"
#include "mesh_time_trial_pilot.h"
#include "mesh_cpu.h"
#include "mesh_kart_rules_asset.h"
#include "mesh_item_fixture.h"
#include "mesh_item_rules_asset.h"
#include "fixed_3d.h"
#include <stdlib.h>
#include <stdio.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
typedef struct
{
    PortableTestMeshCourse floor;
    uint8_t header[32], nodes[128], edges[16], property[32];
    PortableMeshCourse geometry;
    PortableMeshTimeTrialSession session;
    PortableMeshTimeTrialPilotState pilot;
    unsigned geometry_mode;
} FinishFixture;
static FinishFixture finish_fixture;
static int finish_test_triangle(const void *context, uint32_t index, PortableMeshTriangle *out)
{
    const FinishFixture *f=context;
    if ((f->geometry_mode==2 || f->geometry_mode==10) && index==2)
    {
        const int32_t y=(f->geometry_mode==10 ? 24:8)*65536;
        *out=(PortableMeshTriangle){.vertices_q16={{-65536,y,0},
            {65536,y,0},{0,y,3*65536}},.respawn=UINT32_MAX};
        return 1;
    }
    if (f->geometry_mode==6 && index==2) return 0;
    if (f->geometry_mode==7)
    {
        const int32_t low=index<2u ? 24*65536:-60*65536;
        const int32_t high=index<2u ? 60*65536:22*65536;
        const int32_t vertices[4][3]={{-60*65536,low,0},{60*65536,low,0},
            {60*65536,high,0},{-60*65536,high,0}};
        const unsigned ids[2][3]={{0,1,2},{0,2,3}};
        memset(out,0,sizeof *out);out->respawn=UINT32_MAX;
        for(unsigned i=0;i<3;++i) memcpy(out->vertices_q16[i],vertices[ids[index%2u][i]],12);
        return 1;
    }
    PortableCourse3DCollision row;
    if (!portable_course_3d_collision(&f->floor.course,index%2u,&row)) return 0;
    for (unsigned i=0;i<3;++i)
        if (!portable_course_3d_collision_vertex(&f->floor.course,row.vertices[i],out->vertices_q16[i])) return 0;
    if(f->geometry_mode==8)
        for(unsigned i=0;i<3;++i)
            out->vertices_q16[i][0]=out->vertices_q16[i][0]<0 ? -65536/4:65536/4;
    out->surface=row.surface;out->respawn=row.respawn;
    return 1;
}
static int finish_test_surface(const void *context, uint32_t index, PortableMeshSurface *out)
{
    const FinishFixture *f=context;
    PortableCourse3DSurface row;
    if (!portable_course_3d_surface(&f->floor.course,index,&row)) return 0;
    *out=(PortableMeshSurface){.type=f->geometry_mode==1?6u:f->geometry_mode==9?5u:row.type,
        .friction_q16=row.friction_q16,.speed_ratio_q16=row.speed_ratio_q16,
        .gravity_ratio_q16=row.gravity_ratio_q16};
    return 1;
}
static int finish_test_next(const void *context,const int32_t low[3],const int32_t high[3],
    uint32_t first,uint32_t *out)
{
    const FinishFixture *f=context;
    (void)low;(void)high;
    if (first>=f->geometry.triangle_count) return 0;
    *out=first;return 1;
}
static int finish_setup(void)
{
    FinishFixture *f=&finish_fixture;
    memset(f,0,sizeof *f);
    test_mesh_course_init(&f->floor);
    test_mesh_word(f->floor.meta+12u,6);
    const int32_t xy[4][2]={{-60,-60},{60,-60},{60,60},{-60,60}};
    for (unsigned i=0;i<4;++i)
        for (unsigned axis=0;axis<2;++axis)
            test_mesh_word(f->floor.vertices+12u*i+4u*axis,(uint32_t)(xy[i][axis]*65536));
    const int32_t route[4][2]={{0,36},{0,-16},{16,-16},{16,36}};
    test_mesh_word(f->header+8u,4);
    test_mesh_word(f->header+16u,1);
    test_mesh_word(f->header+24u,1);
    test_mesh_word(f->property+4u,PORTABLE_COURSE_3D_PROPERTY_BOOLEAN);
    test_mesh_word(f->property+8u,1);
    for(unsigned i=0;i<4;++i)
    {
        for(unsigned axis=0;axis<2;++axis)
            test_mesh_word(f->nodes+i*32u+axis*4u,(uint32_t)(route[i][axis]*65536));
        test_mesh_word(f->nodes+i*32u+12u,65536);
        test_mesh_word(f->nodes+i*32u+16u,10*65536);
        test_mesh_word(f->nodes+i*32u+20u,i);
        test_mesh_word(f->nodes+i*32u+24u,1);
        test_mesh_word(f->edges+i*4u,(i+1u)%4u);
    }
    static const uint8_t strings[]="mksc:route-pilot-continuous";
#define FINISH_TABLE(tag,n,stride,bytes) f->floor.course.tables[f->floor.course.table_count++]=(PortableCourse3DTable){     PORTABLE_COURSE_3D_TAG(tag[0],tag[1],tag[2],tag[3]),n,stride,bytes}
    FINISH_TABLE("RTHD",1,32,f->header);
    FINISH_TABLE("RTND",4,32,f->nodes);
    FINISH_TABLE("RTED",4,4,f->edges);
    FINISH_TABLE("STRS",sizeof strings,1,strings);
    FINISH_TABLE("PROP",1,32,f->property);
#undef FINISH_TABLE
    CHECK(portable_mesh_time_trial_init(&f->session,&f->floor.course,&f->floor.rules,0,NULL,0)==PORTABLE_MESH_OK);
    f->session.mode=4;
    f->session.state.phase=PORTABLE_MESH_RACER_RACING;
    f->session.state.countdown_ticks=0;
    f->session.state.crossing_armed=1;
    f->session.state.kart.position_q16[1]=28*65536;
    f->session.state.kart.position_q16[2]=f->floor.rules.kart_radius_q16+f->floor.rules.ride_height_q16;
    f->session.state.kart.grounded=1;
    f->session.state.kart.contact_surface=f->session.state.kart.contact_triangle=0;
    memcpy(f->session.state.previous_position_q16,f->session.state.kart.position_q16,12);
    f->geometry=(PortableMeshCourse){f,2,10*65536,finish_test_triangle,finish_test_surface,finish_test_next,NULL};
    f->session.geometry=&f->geometry;
    f->pilot.active=1;f->pilot.mode=4;
    CHECK(portable_mesh_cpu_init(&f->pilot.cpu,&f->session,0,NULL,0)==PORTABLE_MESH_OK);
    return 0;
}
static int defensive_green_hold(void)
{
    PortableTestMeshCourse floor;
    test_mesh_course_init(&floor);
    test_mesh_word(floor.meta + 12u, 8);
    floor.rules.kart_radius_q16 = floor.rules.ride_height_q16 = 65536 / 8;
    uint8_t payload[PORTABLE_MESH_ITEM_RULES_BYTES];
    test_mir_payload(payload);
    const PortableAssetSectionView section = {
        .kind=103,.content_id=41,.version=1,.bytes=payload,.size=sizeof payload};
    PortableMeshItemRules rules;
    CHECK(portable_mesh_item_rules_asset_open(&section,&rules));
    rules.actors_hold_distance_q16=65536;
    rules.actors_hold_height_q16=0;
    rules.actors_radius_q16=65536/4;
    rules.actors_lifetime_ticks=120;
    rules.actors_owner_grace_ticks=30;
    rules.shell_speed_q16=12*65536;
    rules.shell_launch_bonus_q16=0;
    rules.shell_acceleration_q16=65536;
    rules.cpu_use_distance_q16=8*65536;
    const PortableMeshItemsConfig config={.course=&floor.course,.rules=&rules,
        .kart_rules=&floor.rules,.mode=8,.count=2,.human_mask=3};
    for(unsigned defended=0;defended<2;++defended)
    {
        static PortableMeshItems world, before;
        PortableMeshRacerState racers[2];
        memset(racers,0,sizeof racers);
        const int32_t forward[3]={0,-1073741824,0};
        for(unsigned i=0;i<2;++i)
        {
            const int32_t position[3]={0,(int32_t)i*2*65536,65536/4};
            CHECK(portable_mesh_kart_init(&racers[i].kart,&floor.rules,position,forward)==PORTABLE_MESH_OK);
            racers[i].phase=PORTABLE_MESH_RACER_RACING;
            racers[i].kart.grounded=1;
        }
        CHECK(portable_mesh_items_init(&world,&config,55,NULL,0)==PORTABLE_MESH_ITEMS_OK);
        for(unsigned i=0;i<2;++i)
            world.inventory.racers[i]=(PortableMeshItemRacer){
                .held_item=0x1000,.settled_item=0x1000,.preview_item=0x1000};
        unsigned retained=0,impacts=0,hit=0;
        for(unsigned tick=0;tick<100;++tick)
        {
            const uint8_t pulse=(uint8_t)(tick<30);
            before=world;
            const PortableMeshRacerState racer_before=racers[0];
            const uint8_t held=portable_mesh_time_trial_pilot_defensive_hold(&world,&racers[0],&rules,0,
                (PortableMeshKartInput){0,0},pulse);
            CHECK(held==portable_mesh_time_trial_pilot_defensive_hold(&world,&racers[0],&rules,0,
                (PortableMeshKartInput){0,0},pulse));
            CHECK(!memcmp(&before,&world,sizeof world) &&
                !memcmp(&racer_before,&racers[0],sizeof racer_before));
            if(defended && tick==20)
                CHECK(!portable_mesh_time_trial_pilot_defensive_hold(&world,&racers[0],&rules,0,
                    (PortableMeshKartInput){0,0},0)); /* Held defense alone does not delay release. */
            if(defended && tick==31)
            {
                CHECK(held && !pulse && !world.inventory.racers[0].held_item);
                retained=1;
                unsigned hostile=PORTABLE_MESH_ITEMS_ACTORS;
                for(unsigned a=0;a<PORTABLE_MESH_ITEMS_ACTORS;++a)
                    if(world.actors[a].phase==PORTABLE_MESH_ACTOR_FLYING && world.actors[a].owner==1)
                        hostile=a;
                CHECK(hostile<PORTABLE_MESH_ITEMS_ACTORS);
                static PortableMeshItems probe;
                probe=world;probe.actors[hostile].phase=PORTABLE_MESH_ACTOR_FREE;
                CHECK(!portable_mesh_time_trial_pilot_defensive_hold(&probe,&racers[0],&rules,0,
                    (PortableMeshKartInput){0,0},0));
                probe=world;
                probe.actors[hostile].motion.position_q16[2]+=rules.cpu_use_distance_q16+65536;
                CHECK(!portable_mesh_time_trial_pilot_defensive_hold(&probe,&racers[0],&rules,0,
                    (PortableMeshKartInput){0,0},0)); /* FullXYZ range, not a planar threat. */
                probe=world;
                for(unsigned axis=0;axis<3;++axis)
                    probe.actors[hostile].motion.velocity_q16[axis]=
                        -probe.actors[hostile].motion.velocity_q16[axis];
                CHECK(!portable_mesh_time_trial_pilot_defensive_hold(&probe,&racers[0],&rules,0,
                    (PortableMeshKartInput){0,0},0)); /* Green flying away. */
                probe.actors[hostile].item=0x1002;probe.actors[hostile].target=0;
                CHECK(portable_mesh_time_trial_pilot_defensive_hold(&probe,&racers[0],&rules,0,
                    (PortableMeshKartInput){0,0},0)); /* Explicit red target may turn back. */
                probe=world;probe.actors[hostile].owner=0;
                CHECK(!portable_mesh_time_trial_pilot_defensive_hold(&probe,&racers[0],&rules,0,
                    (PortableMeshKartInput){0,0},0)); /* Own flying shell is not hostile. */
                CHECK(!portable_mesh_time_trial_pilot_defensive_hold(&world,&racers[0],&rules,0,
                    (PortableMeshKartInput){0,PORTABLE_MESH_RESCUE},1));
                PortableMeshRacerState inactive=racers[0];
                inactive.respawn_grace=1;
                CHECK(!portable_mesh_time_trial_pilot_defensive_hold(&world,&inactive,&rules,0,
                    (PortableMeshKartInput){0,0},1));
                inactive=racers[0];inactive.phase=PORTABLE_MESH_RACER_FINISHED;
                CHECK(!portable_mesh_time_trial_pilot_defensive_hold(&world,&inactive,&rules,0,
                    (PortableMeshKartInput){0,0},1));
                /* A real fresh-recovery world turn retires the attachment,
                 * without mutating the still-live comparison owner. */
                static PortableMeshItems recovering;
                recovering=world;
                PortableMeshRacerState recovering_racers[2];
                memcpy(recovering_racers,racers,sizeof racers);
                const PortableMeshItemControls recovering_controls[2]={{.recovering=1},{0}};
                const uint8_t recovering_order[2]={0,1};
                PortableMeshKartEffects recovering_effects[2];
                PortableMeshItemEvents recovering_events;
                CHECK(portable_mesh_items_prepare_tick(&recovering,&config,recovering_racers,
                    recovering_controls,recovering_order,recovering_effects,&recovering_events,
                    NULL,0)==PORTABLE_MESH_ITEMS_OK);
                CHECK(portable_mesh_items_resolve_after_move(&recovering,&config,recovering_racers,
                    &recovering_events,NULL,0)==PORTABLE_MESH_ITEMS_OK);
                for(unsigned a=0;a<PORTABLE_MESH_ITEMS_ACTORS;++a)
                    CHECK(!recovering.actors[a].phase || recovering.actors[a].owner!=0);
                CHECK(!portable_mesh_time_trial_pilot_defensive_hold(&recovering,&racers[0],&rules,0,
                    (PortableMeshKartInput){0,0},0));
            }
            const PortableMeshItemControls controls[2]={
                {.item_pressed=defended ? held : pulse},
                {.item_pressed=(uint8_t)(tick==25),.aim_forward=1}};
            const uint8_t standings[2]={0,1};
            PortableMeshKartEffects effects[2];
            PortableMeshItemEvents events;
            CHECK(portable_mesh_items_prepare_tick(&world,&config,racers,controls,standings,
                effects,&events,NULL,0)==PORTABLE_MESH_ITEMS_OK);
            CHECK(portable_mesh_items_resolve_after_move(&world,&config,racers,&events,
                NULL,0)==PORTABLE_MESH_ITEMS_OK);
            for(unsigned i=0;i<events.count;++i)
            {
                impacts+=events.values[i].kind==PORTABLE_MESH_EVENT_IMPACT;
                hit+=events.values[i].kind==PORTABLE_MESH_EVENT_HIT && events.values[i].target==0;
            }
        }
        CHECK(world.inventory.seed==55);
        if(defended)
        {
            CHECK(retained && impacts && !hit && !world.status.racers[0].spin_ticks);
            CHECK(!portable_mesh_time_trial_pilot_defensive_hold(&world,&racers[0],&rules,0,
                (PortableMeshKartInput){0,0},0)); /* Destroyed defense never re-presses. */
        }
        else CHECK(hit); /* Same ordinary release schedule leaves the kart exposed. */
    }
    CHECK(!portable_mesh_time_trial_pilot_defensive_hold(NULL,NULL,NULL,0,
        (PortableMeshKartInput){0,0},1));
    return 0;
}

static int midrace_item_window(void)
{
    uint8_t payload[PORTABLE_MESH_ITEM_RULES_BYTES];test_mir_payload(payload);
    const PortableAssetSectionView section={.kind=103,.content_id=41,.version=1,.bytes=payload,.size=sizeof payload};
    PortableMeshItemRules rules;
    CHECK(portable_mesh_item_rules_asset_open(&section,&rules));
    rules.mushroom_boost_ticks=12;rules.mushroom_cap_ratio_q16=81920;
    rules.mushroom_acceleration_q16=16*65536;
    rules.mushroom_golden_ticks=120;
    PortableMeshItemRacer inventory={.held_item=0x1008};
    PortableMeshItemRacerStatus status={0};
    const PortableMeshKartInput driving={0,PORTABLE_MESH_ACCELERATE};
    CHECK(!finish_setup());
    FinishFixture *f=&finish_fixture;
    test_mesh_word(f->floor.meta+16u,3); /* A real midrace admission, not the final-lap exception. */
    f->session.state.kart.speed_q16=8*65536;
    f->session.state.kart.velocity_q16[1]=-8*65536;
    const PortableMeshTimeTrialSession saved=f->session;
    const PortableMeshTimeTrialPilotState pilot=f->pilot;
    const PortableMeshItemRules rules_before=rules;
    CHECK(portable_mesh_time_trial_pilot_item_press_rules(&f->session,&f->pilot,&inventory,
        &status,&rules,driving,0));
    CHECK(!memcmp(&saved,&f->session,sizeof saved) && !memcmp(&pilot,&f->pilot,sizeof pilot) &&
        !memcmp(&rules_before,&rules,sizeof rules));
    /* Stock-sized authored parameters:24m/s base,1.5cap,80ticks and12m/s².
     * At8m/s the actual acceleration window fits this40m straight; pricing all
     * eighty turns at the36m/s top speed would incorrectly refuse it. */
    f->floor.rules.forward_speed_q16=24*65536;
    f->floor.rules.brake_deceleration_q16=1236095; /* the stock profile's 18.86m/s² brake */
    rules.mushroom_boost_ticks=80;rules.mushroom_cap_ratio_q16=98304;
    rules.mushroom_acceleration_q16=12*65536;
    CHECK(portable_mesh_time_trial_pilot_item_press_rules(&f->session,&f->pilot,&inventory,
        &status,&rules,driving,0));
    f->session.state.kart.speed_q16=32*65536;f->session.state.kart.velocity_q16[1]=-32*65536;
    CHECK(!portable_mesh_time_trial_pilot_item_press_rules(&f->session,&f->pilot,&inventory,
        &status,&rules,driving,0)); /* Already-fast approach has no braking/runout room. */
    rules.mushroom_cap_ratio_q16=81920;rules.mushroom_acceleration_q16=16*65536;
    for(unsigned failure=0;failure<14;++failure)
    {
        CHECK(!finish_setup());test_mesh_word(f->floor.meta+16u,3);
        f->session.state.kart.speed_q16=8*65536;f->session.state.kart.velocity_q16[1]=-8*65536;
        PortableMeshKartInput input=driving;
        rules.mushroom_boost_ticks=12;
        status=(PortableMeshItemRacerStatus){0};
        switch(failure)
        {
        case 0:test_mesh_word(f->nodes+32u,16*65536);break; /* Immediate hard bend. */
        case 1:f->geometry_mode=10;f->geometry.triangle_count=3;break; /* Wall in runout. */
        case 2:f->geometry_mode=7;f->geometry.triangle_count=4;break; /* True2m support gap. */
        case 3:f->geometry_mode=9;break; /* Automatic launch is never ordinary support. */
        case 4:f->session.state.kart.velocity_q16[0]=65536;break; /* Current lateral slip. */
        case 5:input.steering=4096;break;
        case 6:f->session.state.kart.steer_filtered_q16=8192;break;
        case 7:f->session.state.kart.grounded=0;break;
        case 8:status.pending_mushroom=1;break;
        case 9:status.spin_ticks=1;break;
        case 10:f->geometry_mode=8;break; /* Center supported, body-offset probes unsupported. */
        case 11:input.buttons|=PORTABLE_MESH_BRAKE;break;
        case 12:f->session.state.kart.drift_direction=1;break;
        default:rules.mushroom_boost_ticks=8192;break; /* Required runout exceeds40m. */
        }
        CHECK(!portable_mesh_time_trial_pilot_item_press_rules(&f->session,&f->pilot,&inventory,
            &status,&rules,input,1));
    }
    CHECK(!finish_setup());test_mesh_word(f->floor.meta+16u,3);
    rules.mushroom_boost_ticks=12;
    f->session.state.kart.speed_q16=8*65536;f->session.state.kart.velocity_q16[1]=-8*65536;
    inventory.held_item=0x100e;status=(PortableMeshItemRacerStatus){.golden_ticks=50};
    CHECK(portable_mesh_time_trial_pilot_item_press_rules(&f->session,&f->pilot,&inventory,
        &status,&rules,driving,0));
    status.mushroom_ticks=1;
    CHECK(!portable_mesh_time_trial_pilot_item_press_rules(&f->session,&f->pilot,&inventory,
        &status,&rules,driving,1));
    status=(PortableMeshItemRacerStatus){0};
    CHECK(!portable_mesh_time_trial_pilot_item_press_rules(&f->session,&f->pilot,&inventory,
        &status,&rules,driving,1)); /* Expired Golden cannot request another burst. */
    status.golden_ticks=50;inventory.previous_item_pressed=1;
    CHECK(!portable_mesh_time_trial_pilot_item_press_rules(&f->session,&f->pilot,&inventory,
        &status,&rules,driving,1));
    inventory.previous_item_pressed=0;inventory.held_item=0x1009;
    CHECK(!portable_mesh_time_trial_pilot_item_press_rules(&f->session,&f->pilot,&inventory,
        &status,&rules,driving,1)); /* No second Golden owner while already active. */
    status=(PortableMeshItemRacerStatus){0};inventory.held_item=0x100a;
    rules.star_player_ticks=300;rules.star_cap_ratio_q16=81920;
    rules.star_acceleration_q16=512*65536;
    CHECK(!portable_mesh_time_trial_pilot_item_press_rules(&f->session,&f->pilot,&inventory,
        &status,&rules,driving,0)); /* Star duration, not the shorter mushroom duration. */
    rules.star_player_ticks=12;
    CHECK(portable_mesh_time_trial_pilot_item_press_rules(&f->session,&f->pilot,&inventory,
        &status,&rules,driving,0));
    rules.star_cap_ratio_q16=262144;
    CHECK(!portable_mesh_time_trial_pilot_item_press_rules(&f->session,&f->pilot,&inventory,
        &status,&rules,driving,0)); /* Star cap also controls the braking/runout estimate. */
    rules.star_cap_ratio_q16=81920;
    inventory.held_item=0x1000;
    CHECK(portable_mesh_time_trial_pilot_item_press_rules(&f->session,&f->pilot,&inventory,
        &status,&rules,driving,1)==1);
    CHECK(!portable_mesh_time_trial_pilot_item_press_rules(&f->session,&f->pilot,&inventory,
        &status,&rules,driving,0));
    /* Genuine triple inventory activates once on a midlap straight. Full
     * physics moves forward while stock survives the complete active burst. */
    static PortableMeshItems world;
    const PortableMeshItemsConfig config={.course=&f->floor.course,.rules=&rules,
        .kart_rules=&f->floor.rules,.mode=4,.count=1,.human_mask=1,.geometry=&f->geometry};
    CHECK(portable_mesh_items_init(&world,&config,55,NULL,0)==PORTABLE_MESH_ITEMS_OK);
    world.inventory.racers[0]=(PortableMeshItemRacer){.held_item=0x1008,.settled_item=0x1008,.preview_item=0x1008};
    unsigned uses=0,boost_ticks=0,ready_again=0;
    const int32_t initial_y=f->session.state.kart.position_q16[1];
    for(unsigned tick=0;tick<64;++tick)
    {
        const uint8_t press=portable_mesh_time_trial_pilot_item_press_rules(&f->session,&f->pilot,
            &world.inventory.racers[0],&world.status.racers[0],&rules,driving,0);
        if(uses && press) {ready_again=1;break;}
        const PortableMeshItemControls control={.item_pressed=press};
        const uint8_t standings[1]={0};PortableMeshKartEffects effects;PortableMeshItemEvents events;
        CHECK(portable_mesh_items_prepare_tick(&world,&config,&f->session.state,&control,standings,
            &effects,&events,NULL,0)==PORTABLE_MESH_ITEMS_OK);
        boost_ticks+=effects.boost_active;
        PortableMeshRacerTurn turn;
        CHECK(portable_mesh_racer_move_effects(&f->session,driving,&effects,&turn,NULL,0)==PORTABLE_MESH_OK);
        CHECK(portable_mesh_items_resolve_after_move(&world,&config,&f->session.state,&events,NULL,0)==PORTABLE_MESH_ITEMS_OK);
        CHECK(portable_mesh_racer_publish(&f->session,&turn,NULL,0)==PORTABLE_MESH_OK);
        for(unsigned i=0;i<events.count;++i) uses+=events.values[i].kind==PORTABLE_MESH_EVENT_USE;
        CHECK(world.inventory.racers[0].held_item==0x100f);
    }
    CHECK(uses==1 && boost_ticks==(unsigned)rules.mushroom_boost_ticks && ready_again &&
        world.inventory.seed==55 && f->session.state.lap==0 &&
        f->session.state.phase==PORTABLE_MESH_RACER_RACING &&
        f->session.state.kart.position_q16[1]<initial_y);
    CHECK(portable_mesh_items_init(&world,&config,55,NULL,0)==PORTABLE_MESH_ITEMS_OK);
    world.inventory.racers[0]=(PortableMeshItemRacer){.held_item=0x1009,.settled_item=0x1009,.preview_item=0x1009};
    const PortableMeshItemControls golden={.item_pressed=portable_mesh_time_trial_pilot_item_press_rules(
        &f->session,&f->pilot,&world.inventory.racers[0],&world.status.racers[0],&rules,driving,0)};
    CHECK(golden.item_pressed);
    const uint8_t standings[1]={0};PortableMeshKartEffects effects;PortableMeshItemEvents events;
    CHECK(portable_mesh_items_prepare_tick(&world,&config,&f->session.state,&golden,standings,
        &effects,&events,NULL,0)==PORTABLE_MESH_ITEMS_OK);
    CHECK(portable_mesh_items_resolve_after_move(&world,&config,&f->session.state,&events,NULL,0)==PORTABLE_MESH_ITEMS_OK);
    CHECK(world.inventory.racers[0].held_item==0x100e && effects.boost_active &&
        world.status.racers[0].golden_ticks && world.status.racers[0].mushroom_ticks);
    CHECK(!portable_mesh_time_trial_pilot_item_press_rules(&f->session,&f->pilot,
        &world.inventory.racers[0],&world.status.racers[0],&rules,driving,1));
    return 0;
}

static int final_finish_window(void)
{
    CHECK(!finish_setup());
    FinishFixture *f=&finish_fixture;
    PortableMeshFinishWindow window;
    unsigned char before[sizeof f->session],pilot_before[sizeof f->pilot];
    memcpy(before,&f->session,sizeof before);memcpy(pilot_before,&f->pilot,sizeof pilot_before);
    CHECK(portable_mesh_time_trial_pilot_finish_window(&f->session,&f->pilot,&window));
    /* The shared race crossing fraction truncates toQ30; its reported
     * intersection therefore lies at most oneQ16LSB before this plane. */
    CHECK(window.distance_q16>=32*65536-1 && window.distance_q16<=32*65536 && window.gate_count==2 &&
        window.finish_position_q16[0]==0 && window.finish_position_q16[1]>=-4*65536 &&
        window.finish_position_q16[1]<=-4*65536+1);
    CHECK(!memcmp(before,&f->session,sizeof before) && !memcmp(pilot_before,&f->pilot,sizeof pilot_before));
    for(unsigned failure=0;failure<16;++failure)
    {
        CHECK(!finish_setup());
        switch(failure)
        {
        case 0:test_mesh_word(f->floor.meta+16u,3);break; /* Earlier lap. */
        case 1:f->session.state.crossing_armed=0;break;
        case 2:f->session.state.kart.forward_q30[1]=1073741824;break;
        case 3:f->session.state.kart.position_q16[0]=5*65536-1;break; /* Body margin. */
        case 4:test_mesh_word(f->floor.edges+4u,1);break; /* Required successor never reached. */
        case 5:test_mesh_word(f->nodes+32u,16*65536);break; /* Hard selected-route bend. */
        case 6:f->geometry_mode=1;break; /* Recovery support. */
        case 7:f->geometry_mode=2;f->geometry.triangle_count=3;break; /* Intervening wall. */
        case 8:f->geometry.triangle_count=1;break; /* Uncovered floor interval. */
        case 9:f->geometry_mode=5;f->geometry.triangle_count=513;break; /* Bounded candidates. */
        case 10:f->geometry_mode=6;f->geometry.triangle_count=3;break; /* Late invalid callback. */
        case 11:f->session.geometry=NULL;break;
        case 12:f->session.state.kart.position_q16[1]=38*65536;break; /* Finish outside40m. */
        case 13:f->session.state.respawn_grace=1;break;
        case 14:test_mesh_word(f->floor.vertices,INT32_MAX);break; /* Reject before cross products. */
        default:f->geometry.next_triangle=NULL;break;
        }
        memset(&window,0xa5,sizeof window);
        unsigned char saved[sizeof window];memcpy(saved,&window,sizeof saved);
        CHECK(!portable_mesh_time_trial_pilot_finish_window(&f->session,&f->pilot,&window));
        CHECK(!memcmp(saved,&window,sizeof saved));
    }
    CHECK(!finish_setup());
    PortableCourse3DCheckpoint gate;
    CHECK(portable_course_3d_checkpoint(&f->floor.course,0,&gate));
    const int32_t crossing_start[3]={0,65536,0},crossing_end[3]={0,-8*65536,0};
    int64_t time=123;
    CHECK(portable_mesh_racer_gate_crossing(&gate,crossing_start,crossing_end,&time) && time>0);
    gate.normal_q30[0]=INT32_MAX;
    time=123;
    CHECK(!portable_mesh_racer_gate_crossing(&gate,crossing_start,crossing_end,&time) && time==123);
    CHECK(!finish_setup());
    /* Forced boosts ignore pulse phase only inside the genuine finite window;
     * all other items (including an active Golden repeat) keep baseline pulses. */
    PortableMeshItemRacer inventory={0};
    PortableMeshItemRacerStatus item_status={0};
    const PortableMeshKartInput accelerate={0,PORTABLE_MESH_ACCELERATE};
    const uint16_t forced_items[]={0x1007,0x1008,0x1009,0x100f,0x100a};
    for(unsigned i=0;i<sizeof forced_items/sizeof forced_items[0];++i)
    {
        inventory.held_item=forced_items[i];
        CHECK(portable_mesh_time_trial_pilot_item_press(&f->session,&f->pilot,&inventory,
            &item_status,accelerate,0)==1);
        f->session.state.crossing_armed=0;
        CHECK(!portable_mesh_time_trial_pilot_item_press(&f->session,&f->pilot,&inventory,
            &item_status,accelerate,1));
        f->session.state.crossing_armed=1;
    }
    for(unsigned refusal=0;refusal<9;++refusal)
    {
        inventory=(PortableMeshItemRacer){.held_item=0x1008};
        item_status=(PortableMeshItemRacerStatus){0};
        PortableMeshKartInput driving=accelerate;
        switch(refusal)
        {
        case 0:inventory.previous_item_pressed=1;break;
        case 1:item_status.pending_mushroom=1;break;
        case 2:item_status.mushroom_ticks=1;break;
        case 3:item_status.star_ticks=1;break;
        case 4:driving.buttons|=PORTABLE_MESH_BRAKE;break;
        case 5:driving.buttons|=PORTABLE_MESH_RESCUE;break;
        case 6:driving.buttons=0;break;
        case 7:f->session.state.kart.grounded=0;break;
        default:f->session.state.kart.panel_ticks=1;break;
        }
        CHECK(!portable_mesh_time_trial_pilot_item_press(&f->session,&f->pilot,&inventory,
            &item_status,driving,1));
        CHECK(inventory.held_item==0x1008); /* Decision never spends stock. */
        f->session.state.kart.grounded=1;
        f->session.state.kart.panel_ticks=0;
    }
    const uint16_t periodic_items[]={0,0x1000,0x1002,0x100b,0x100e};
    for(unsigned i=0;i<sizeof periodic_items/sizeof periodic_items[0];++i)
    {
        inventory.held_item=periodic_items[i];
        CHECK(!portable_mesh_time_trial_pilot_item_press(&f->session,&f->pilot,&inventory,
            &item_status,accelerate,0));
        CHECK(portable_mesh_time_trial_pilot_item_press(&f->session,&f->pilot,&inventory,
            &item_status,accelerate,1)==1);
    }
    inventory.held_item=0x100a;
    f->pilot.active=0;
    CHECK(portable_mesh_time_trial_pilot_item_press(&f->session,&f->pilot,&inventory,
        &item_status,accelerate,1)==1);
    CHECK(!portable_mesh_time_trial_pilot_item_press(&f->session,&f->pilot,&inventory,
        &item_status,accelerate,0));
    CHECK(!portable_mesh_time_trial_pilot_item_press(NULL,&f->pilot,&inventory,
        &item_status,accelerate,1));
    CHECK(!portable_mesh_time_trial_pilot_item_press(&f->session,NULL,&inventory,
        &item_status,accelerate,1));
    CHECK(!finish_setup());
    /* Accepted ordinary Star activation and real fixed turns cross BOTH gates;
     * finishing absorbs motion while its independent status still has time. */
    uint8_t payload[PORTABLE_MESH_ITEM_RULES_BYTES];test_mir_payload(payload);
    const PortableAssetSectionView section={.kind=103,.content_id=41,.version=1,.bytes=payload,.size=sizeof payload};
    PortableMeshItemRules rules;
    CHECK(portable_mesh_item_rules_asset_open(&section,&rules));
    rules.star_player_ticks=300;rules.star_cap_ratio_q16=98304;rules.star_acceleration_q16=16*65536;
    static PortableMeshItems items;
    static PortableMeshItemEvents events;
    const PortableMeshItemsConfig config={.course=&f->floor.course,.rules=&rules,
        .kart_rules=&f->floor.rules,.mode=4,.count=1,.human_mask=1,.geometry=&f->geometry};
    CHECK(portable_mesh_items_init(&items,&config,55,NULL,0)==PORTABLE_MESH_ITEMS_OK);
    items.inventory.racers[0]=(PortableMeshItemRacer){.held_item=0x100a,.settled_item=0x100a,.preview_item=0x100a};
    f->session.state.kart.speed_q16=8*65536;f->session.state.kart.velocity_q16[1]=-8*65536;
    int passed=0;
    for(unsigned tick=0;tick<250 && f->session.state.phase!=PORTABLE_MESH_RACER_FINISHED;++tick)
    {
        /* No periodic pulse: the helper supplies the fresh ordinary edge. */
        const PortableMeshItemControls controls={.item_pressed=
            portable_mesh_time_trial_pilot_item_press(&f->session,&f->pilot,
                &items.inventory.racers[0],&items.status.racers[0],accelerate,0)};
        if(tick==0) CHECK(controls.item_pressed);
        else CHECK(!controls.item_pressed);
        const uint8_t standings[1]={0};PortableMeshKartEffects effects;
        CHECK(portable_mesh_items_prepare_tick(&items,&config,&f->session.state,&controls,standings,
            &effects,&events,NULL,0)==PORTABLE_MESH_ITEMS_OK);
        PortableMeshRacerTurn turn;
        CHECK(portable_mesh_racer_move_effects(&f->session,(PortableMeshKartInput){0,PORTABLE_MESH_ACCELERATE},
            &effects,&turn,NULL,0)==PORTABLE_MESH_OK);
        CHECK(portable_mesh_items_resolve_after_move(&items,&config,&f->session.state,&events,NULL,0)==PORTABLE_MESH_ITEMS_OK);
        CHECK(portable_mesh_racer_publish(&f->session,&turn,NULL,0)==PORTABLE_MESH_OK);
        passed|=f->session.state.checkpoint==1;
    }
    CHECK(passed && f->session.state.phase==PORTABLE_MESH_RACER_FINISHED &&
        f->session.state.lap==1 && items.status.racers[0].star_ticks>0 &&
        !items.inventory.racers[0].held_item && items.inventory.seed==55);
    int32_t position[3];memcpy(position,f->session.state.kart.position_q16,sizeof position);
    PortableMeshRacerTurn turn;
    CHECK(portable_mesh_racer_move(&f->session,(PortableMeshKartInput){0,PORTABLE_MESH_ACCELERATE},&turn,NULL,0)==PORTABLE_MESH_OK);
    CHECK(!memcmp(position,f->session.state.kart.position_q16,sizeof position));
    return 0;
}

/* A broad safe road has a short automatic panel before a left bend. The
 * controller can pass a point-like node outside its arrival disk even while
 * crossing the genuine checkpoint. Only authored arrival radii differ below. */
static int panel_bend_arrival(void)
{
    for (unsigned wider=0;wider<2;++wider)
    {
        PortableTestMeshCourse f;
        test_mesh_course_init(&f);
        f.rules.handling_turn_rate_q16=4096; /* Slow, valid authored steering. */
        uint8_t vertices[72]={0},triangles[128]={0},surfaces[96]={0};
        const int32_t xyz[6][3]={{-20*65536,-20*65536,0},{20*65536,-20*65536,0},
            {-20*65536,65536,0},{20*65536,65536,0},
            {-20*65536,20*65536,0},{20*65536,20*65536,0}};
        const uint32_t indices[4][3]={{0,1,3},{0,3,2},{2,3,5},{2,5,4}};
        for(unsigned v=0;v<6;++v)
            for(unsigned axis=0;axis<3;++axis)
                test_mesh_word(vertices+v*12u+axis*4u,(uint32_t)xyz[v][axis]);
        memcpy(surfaces,f.surface,48);
        memcpy(surfaces+48,f.surface,48);
        test_mesh_word(surfaces+48,4); /* Upper lead-in is an actual boost panel. */
        for(unsigned t=0;t<4;++t)
        {
            for(unsigned axis=0;axis<3;++axis)
                test_mesh_word(triangles+t*32u+axis*4u,indices[t][axis]);
            test_mesh_word(triangles+t*32u+12u,t>=2u ? 1u : 0u);
            for(unsigned field=20;field<32;field+=4)
                test_mesh_word(triangles+t*32u+field,UINT32_MAX);
        }
        for(unsigned table=0;table<f.course.table_count;++table)
        {
            PortableCourse3DTable *view=&f.course.tables[table];
            if(view->tag==PORTABLE_COURSE_3D_TAG('C','O','L','V'))
                *view=(PortableCourse3DTable){view->tag,6,12,vertices};
            else if(view->tag==PORTABLE_COURSE_3D_TAG('C','O','L','T'))
                *view=(PortableCourse3DTable){view->tag,4,32,triangles};
            else if(view->tag==PORTABLE_COURSE_3D_TAG('S','U','R','F'))
                *view=(PortableCourse3DTable){view->tag,2,48,surfaces};
        }
        uint8_t header[32]={0},nodes[128]={0},edges[16]={0};
        test_mesh_word(header+8u,4);
        test_mesh_word(header+16u,1);
        const int32_t xy[4][2]={{0,10*65536},{0,0},{-8*65536,-16*65536},
            {-8*65536,10*65536}};
        const int32_t radius=wider ? 3*65536/2 : 4*65536/5;
        for(unsigned node=0;node<4;++node)
        {
            test_mesh_word(nodes+node*32u,(uint32_t)xy[node][0]);
            test_mesh_word(nodes+node*32u+4u,(uint32_t)xy[node][1]);
            test_mesh_word(nodes+node*32u+12u,(uint32_t)radius);
            test_mesh_word(nodes+node*32u+16u,10*65536);
            test_mesh_word(nodes+node*32u+20u,node);
            test_mesh_word(nodes+node*32u+24u,1);
            test_mesh_word(edges+node*4u,(node+1u)%4u);
        }
        f.course.tables[f.course.table_count++]=(PortableCourse3DTable){
            PORTABLE_COURSE_3D_TAG('R','T','H','D'),1,32,header};
        f.course.tables[f.course.table_count++]=(PortableCourse3DTable){
            PORTABLE_COURSE_3D_TAG('R','T','N','D'),4,32,nodes};
        f.course.tables[f.course.table_count++]=(PortableCourse3DTable){
            PORTABLE_COURSE_3D_TAG('R','T','E','D'),4,4,edges};
        PortableMeshRacer racer;
        PortableMeshCpuState cpu;
        char error[192];
        CHECK(portable_mesh_time_trial_init(&racer,&f.course,&f.rules,0,error,sizeof error)==PORTABLE_MESH_OK);
        racer.state.phase=PORTABLE_MESH_RACER_RACING;
        racer.state.countdown_ticks=0;
        racer.state.crossing_armed=1;
        const int32_t position[3]={11*65536/10,5*65536/2,49152};
        const int32_t forward[3]={0,-1073741824,0};
        CHECK(portable_mesh_kart_init(&racer.state.kart,&f.rules,position,forward)==PORTABLE_MESH_OK);
        racer.state.kart.grounded=1;
        racer.state.kart.speed_q16=10*65536;
        racer.state.kart.velocity_q16[1]=-10*65536;
        CHECK(portable_mesh_cpu_init(&cpu,&racer,0,error,sizeof error)==PORTABLE_MESH_OK);
        CHECK(cpu.from_node==0 && cpu.to_node==1 && cpu.recovery_ticks>24u);
        unsigned panels=0,panel_braking=0,crossed_gate=0,advanced=0,chasing_passed_node=0;
        int32_t peak_speed=racer.state.kart.speed_q16;
        for(unsigned tick=0;tick<24;++tick)
        {
            PortableMeshKartInput input;
            CHECK(portable_mesh_cpu_input(&cpu,&cpu,&racer,&input,error,sizeof error)==PORTABLE_MESH_OK);
            if(racer.state.kart.position_q16[1]<-65536 && cpu.from_node==0)
                chasing_passed_node=1;
            advanced|=cpu.from_node==1 && cpu.to_node==2 && cpu.visit_serial==1;
            PortableMeshRacerTurn turn;
            CHECK(portable_mesh_racer_move(&racer,input,&turn,error,sizeof error)==PORTABLE_MESH_OK);
            panels+=turn.events.panel;
            panel_braking|=turn.events.panel && !(input.buttons&PORTABLE_MESH_ACCELERATE);
            if(racer.state.kart.speed_q16>peak_speed) peak_speed=racer.state.kart.speed_q16;
            CHECK(portable_mesh_racer_publish(&racer,&turn,error,sizeof error)==PORTABLE_MESH_OK);
            crossed_gate|=racer.state.checkpoint==1;
            CHECK(!racer.state.kart.respawn_ticks && racer.state.kart.grounded);
        }
        CHECK(panels && panel_braking && peak_speed>10*65536 && crossed_gate &&
            racer.state.kart.position_q16[1]<-65536);
        if(wider)
            CHECK(advanced && !chasing_passed_node);
        else
            CHECK(!advanced && chasing_passed_node && cpu.from_node==0 && cpu.to_node==1);
    }
    return 0;
}

static int route_tick_budget(void)
{
    PortableTestMeshCourse fixture;
    test_mesh_course_init(&fixture);
    CHECK(portable_mesh_time_trial_pilot_tick_budget(NULL)==12000u);
    CHECK(portable_mesh_time_trial_pilot_tick_budget(&fixture.course)==12000u);
    uint8_t header[32]={0},nodes[128]={0},edges[16]={0};
    test_mesh_word(header+8u,4);
    test_mesh_word(header+16u,1);
    for(unsigned i=0;i<4;++i)
    {
        test_mesh_word(nodes+i*32+12u,65536);
        test_mesh_word(nodes+i*32+16u,10*65536);
        test_mesh_word(nodes+i*32+20u,i);
        test_mesh_word(nodes+i*32+24u,1);
        test_mesh_word(edges+i*4u,(i+1u)%4u);
    }
    fixture.course.tables[fixture.course.table_count++]=(PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('R','T','H','D'),1,32,header};
    fixture.course.tables[fixture.course.table_count++]=(PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('R','T','N','D'),4,32,nodes};
    fixture.course.tables[fixture.course.table_count++]=(PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('R','T','E','D'),4,4,edges};
    /* A 3x4m rectangle stays at the old minimum, including five laps. */
    test_mesh_word(nodes+32u,3*65536);
    test_mesh_word(nodes+64u,3*65536);
    test_mesh_word(nodes+64u+4u,4*65536);
    test_mesh_word(nodes+96u+4u,4*65536);
    test_mesh_word(fixture.meta+16u,5);
    CHECK(portable_mesh_time_trial_pilot_tick_budget(&fixture.course)==12000u);
    /* Independently known 1400m perimeter, five laps, 20ticks/metre+600. */
    test_mesh_word(nodes+32u,300*65536);
    test_mesh_word(nodes+64u,300*65536);
    test_mesh_word(nodes+64u+4u,400*65536);
    test_mesh_word(nodes+96u+4u,400*65536);
    CHECK(portable_mesh_time_trial_pilot_tick_budget(&fixture.course)==140600u);
    test_mesh_word(fixture.meta+16u,1);
    CHECK(portable_mesh_time_trial_pilot_tick_budget(&fixture.course)==28600u);
    /* XYZ distance, not horizontal distance: two 300/400/500 slopes. */
    test_mesh_word(nodes+32u+8u,400*65536);
    test_mesh_word(nodes+64u+8u,400*65536);
    CHECK(portable_mesh_time_trial_pilot_tick_budget(&fixture.course)==36600u);
    test_mesh_word(nodes+32u+8u,0);
    test_mesh_word(nodes+64u+8u,0);
    test_mesh_word(fixture.meta+16u,50);
    CHECK(portable_mesh_time_trial_pilot_tick_budget(&fixture.course)==180000u);
    /* Validate the entire bounded route even when its budget already caps. */
    test_mesh_word(edges+12u,4);
    CHECK(portable_mesh_time_trial_pilot_tick_budget(&fixture.course)==12000u);
    test_mesh_word(edges+12u,0);
    test_mesh_word(nodes+96u+8u,INT32_MAX);
    CHECK(portable_mesh_time_trial_pilot_tick_budget(&fixture.course)==12000u);
    test_mesh_word(nodes+96u+8u,0);
    fixture.course.tables[fixture.course.table_count-2u].count=16385;
    CHECK(portable_mesh_time_trial_pilot_tick_budget(&fixture.course)==12000u);
    fixture.course.tables[fixture.course.table_count-2u].count=4;
    fixture.course.tables[fixture.course.table_count-1u].stride=8;
    CHECK(portable_mesh_time_trial_pilot_tick_budget(&fixture.course)==12000u);
    fixture.course.tables[fixture.course.table_count-1u].stride=4;
    test_mesh_word(header+16u,0);
    CHECK(portable_mesh_time_trial_pilot_tick_budget(&fixture.course)==12000u);
    test_mesh_word(header+16u,1);
    test_mesh_word(fixture.meta+12u,16);
    CHECK(portable_mesh_time_trial_pilot_tick_budget(&fixture.course)==12000u);
    return 0;
}
static int continuous_pilot(void)
{
    PortableTestMeshCourse f;
    test_mesh_course_init(&f);
    uint8_t header[32]={0},nodes[128]={0},edges[16]={0},property[32]={0};
    const uint8_t strings[]="mksc:route-pilot-continuous";
    test_mesh_word(header+8u,4);
    test_mesh_word(header+16u,1);
    test_mesh_word(header+24u,1);
    test_mesh_word(property+4u,PORTABLE_COURSE_3D_PROPERTY_BOOLEAN);
    test_mesh_word(property+8u,1);
    /* Two diagonals cross at identical height. The first encounter owns
     * edge0, even when a later sample is nearer the disconnected edge2. */
    const int32_t xy[4][2]={{-8*65536,-8*65536},{8*65536,8*65536},
                            {-8*65536,8*65536},{8*65536,-8*65536}};
    for(unsigned i=0;i<4;++i)
    {
        test_mesh_word(nodes+i*32u,(uint32_t)xy[i][0]);
        test_mesh_word(nodes+i*32u+4u,(uint32_t)xy[i][1]);
        test_mesh_word(nodes+i*32u+12u,65536);
        test_mesh_word(nodes+i*32u+16u,8*65536);
        test_mesh_word(nodes+i*32u+20u,i);
        test_mesh_word(nodes+i*32u+24u,1);
        test_mesh_word(edges+i*4u,(i+1u)%4u);
    }
#define PILOT_TABLE(tag,n,stride,data) f.course.tables[f.course.table_count++]=(PortableCourse3DTable){ \
    PORTABLE_COURSE_3D_TAG(tag[0],tag[1],tag[2],tag[3]),n,stride,data}
    PILOT_TABLE("RTHD",1,32,header);
    PILOT_TABLE("RTND",4,32,nodes);
    PILOT_TABLE("RTED",4,4,edges);
    PILOT_TABLE("STRS",sizeof strings,1,strings);
    PILOT_TABLE("PROP",1,32,property);
#undef PILOT_TABLE
    PortableMeshTimeTrialSession session;
    char error[192];
    CHECK(portable_mesh_time_trial_init(&session,&f.course,&f.rules,0,error,sizeof error)==PORTABLE_MESH_OK);
    session.state.phase=PORTABLE_MESH_TT_RACING;
    session.state.countdown_ticks=0;
    session.state.tick=200;
    const int32_t position[3]={-4*65536,-4*65536,49152};
    const int32_t forward[3]={759250125,759250125,0};
    CHECK(portable_mesh_kart_init(&session.state.kart,&f.rules,position,forward)==PORTABLE_MESH_OK);
    session.state.kart.grounded=1;
    PortableMeshTimeTrialPilotState zero={0},planner,repeat;
    PortableMeshKartInput input,legacy;
    CHECK(portable_mesh_time_trial_pilot_step(&planner,&zero,&session,&input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(planner.active && planner.last_tick==200 && planner.cpu.from_node==0 && planner.cpu.to_node==1);
    ++session.state.tick;
    session.state.kart.position_q16[0]=65536/10;
    session.state.kart.position_q16[1]=-65536/5;
    const uint64_t checksum=portable_mesh_time_trial_checksum(&session.state);
    CHECK(portable_mesh_time_trial_pilot_step(&planner,&planner,&session,&input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(planner.cpu.from_node==0 && planner.cpu.to_node==1 && planner.cpu.visit_serial==0);
    PortableMeshCpuState acquired;
    CHECK(portable_mesh_cpu_init(&acquired,&session,PORTABLE_MESH_ABSENT,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(acquired.from_node==2 && acquired.to_node==3);
    CHECK(portable_mesh_time_trial_pilot(&session,&legacy,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(input.steering!=legacy.steering);
    repeat=planner;
    PortableMeshKartInput same;
    CHECK(portable_mesh_time_trial_pilot_step(&planner,&planner,&session,&same,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(!memcmp(&planner,&repeat,sizeof planner) && same.steering==input.steering && same.buttons==input.buttons);
    CHECK(checksum==portable_mesh_time_trial_checksum(&session.state));
    /* Identity changes reset even on an identical tick ordinal. */
    ++f.rules.revision;
    CHECK(portable_mesh_time_trial_pilot_step(&planner,&planner,&session,&input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(planner.cpu.profile_revision==f.rules.revision && planner.cpu.from_node==2);
    ++f.course.id;
    CHECK(portable_mesh_time_trial_pilot_step(&planner,&planner,&session,&input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(planner.cpu.course_id==f.course.id && planner.cpu.from_node==2);
    /* Retry rolls back the preturn clock and reacquires its original edge. */
    session.state.tick=0;
    memcpy(session.state.kart.position_q16,position,sizeof position);
    CHECK(portable_mesh_time_trial_pilot_step(&planner,&planner,&session,&input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(planner.last_tick==0 && planner.cpu.from_node==0 && planner.cpu.visit_serial==0);
    /* Wrong types, malformed booleans and unreadable spans never publish. */
    repeat=planner;
    const PortableMeshKartInput sentinel={123,15};
    input=sentinel;
    test_mesh_word(property+4u,PORTABLE_COURSE_3D_PROPERTY_INTEGER);
    CHECK(portable_mesh_time_trial_pilot_step(&planner,&planner,&session,&input,error,sizeof error)==PORTABLE_MESH_INVALID);
    CHECK(!memcmp(&planner,&repeat,sizeof planner) && input.buttons==sentinel.buttons && input.steering==sentinel.steering);
    test_mesh_word(property+4u,PORTABLE_COURSE_3D_PROPERTY_BOOLEAN);
    test_mesh_word(property+8u,2);
    CHECK(portable_mesh_time_trial_pilot_step(&planner,&planner,&session,&input,error,sizeof error)==PORTABLE_MESH_INVALID);
    CHECK(!memcmp(&planner,&repeat,sizeof planner));
    test_mesh_word(property+8u,1);
    test_mesh_word(header+20u,UINT32_MAX);
    CHECK(portable_mesh_time_trial_pilot_step(&planner,&planner,&session,&input,error,sizeof error)==PORTABLE_MESH_INVALID);
    CHECK(!memcmp(&planner,&repeat,sizeof planner));
    test_mesh_word(header+20u,0);
    /* Both absent and explicit false controls preserve legacy input and clear
     * the auxiliary planner; neither changes session/codec state. */
    for(unsigned absent=0;absent<2;++absent)
    {
        test_mesh_word(property+8u,0);
        test_mesh_word(header+24u,absent?0:1);
        CHECK(portable_mesh_time_trial_pilot(&session,&legacy,error,sizeof error)==PORTABLE_MESH_OK);
        CHECK(portable_mesh_time_trial_pilot_step(&planner,&repeat,&session,&input,error,sizeof error)==PORTABLE_MESH_OK);
        CHECK(!memcmp(&planner,&zero,sizeof planner));
        CHECK(input.buttons==legacy.buttons && input.steering==legacy.steering);
    }
    return 0;
}
static int nominal_lane_selection(void)
{
    PortableTestMeshCourse f;
    test_mesh_course_init(&f);
    uint8_t headers[64]={0},nodes[12*32]={0},edges[12*4]={0},property[32]={0};
    const uint8_t strings[]="mksc:route-pilot-continuous";
    /* A30m zigzag is shorter than a32m square, but needs eight corners
     * rather than four. Pure shortest-distance selection is the wrong oracle. */
    const int32_t xy[12][2]={{0,0},{3,0},{3,3},{6,3},{6,6},{3,6},{3,9},{0,9},
                            {0,0},{8,0},{8,8},{0,8}};
    test_mesh_word(headers+8u,8);
    test_mesh_word(headers+16u,1);
    test_mesh_word(headers+24u,1);
    test_mesh_word(headers+32u+4u,8);
    test_mesh_word(headers+32u+8u,4);
    test_mesh_word(headers+32u+12u,8);
    test_mesh_word(headers+32u+16u,1);
    test_mesh_word(property+4u,PORTABLE_COURSE_3D_PROPERTY_BOOLEAN);
    test_mesh_word(property+8u,1);
    for (unsigned i=0;i<12u;++i)
    {
        test_mesh_word(nodes+i*32u,(uint32_t)(xy[i][0]*65536));
        test_mesh_word(nodes+i*32u+4u,(uint32_t)(xy[i][1]*65536));
        test_mesh_word(nodes+i*32u+12u,65536);
        test_mesh_word(nodes+i*32u+16u,10*65536);
        test_mesh_word(nodes+i*32u+20u,i);
        test_mesh_word(nodes+i*32u+24u,1);
        test_mesh_word(edges+i*4u,i==7u?0u:i==11u?8u:i+1u);
    }
    const unsigned header_table=f.course.table_count;
#define LANE_TABLE(tag,n,stride,data) f.course.tables[f.course.table_count++]=(PortableCourse3DTable){ \
    PORTABLE_COURSE_3D_TAG(tag[0],tag[1],tag[2],tag[3]),n,stride,data}
    LANE_TABLE("RTHD",2,32,headers);
    LANE_TABLE("RTND",12,32,nodes);
    LANE_TABLE("RTED",12,4,edges);
    LANE_TABLE("STRS",sizeof strings,1,strings);
    LANE_TABLE("PROP",1,32,property);
#undef LANE_TABLE
    PortableMeshTimeTrialSession session;
    char error[192];
    CHECK(portable_mesh_time_trial_init(&session,&f.course,&f.rules,0,error,sizeof error)==PORTABLE_MESH_OK);
    test_mesh_word(f.meta+12u,3u);
    session.mode=1; /* Same immutable geometry, ordinary shared GP aid owner. */
    session.state.phase=PORTABLE_MESH_TT_RACING;
    session.state.countdown_ticks=0;
    session.state.tick=200;
    PortableMeshTimeTrialPilotState zero={0},planner,saved;
    PortableMeshKartInput input,same;
    CHECK(portable_mesh_time_trial_pilot_step(&planner,&zero,&session,&input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(planner.cpu.route==1u);
    /* Explicit choices initialize the unchanged production controller, not
     * simulation state. The default wrapper remains exactly ABSENT. */
    PortableMeshTimeTrialPilotState hinted,wrapped,explicit_default;
    PortableMeshKartInput hinted_input,wrapped_input,default_input;
    CHECK(portable_mesh_time_trial_pilot_step(&wrapped,&zero,&session,&wrapped_input,
        error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(portable_mesh_time_trial_pilot_step_route(&explicit_default,&zero,&session,
        PORTABLE_MESH_ABSENT,&default_input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(!memcmp(&wrapped,&explicit_default,sizeof wrapped) &&
        wrapped_input.buttons==default_input.buttons && wrapped_input.steering==default_input.steering);
    for (uint32_t choice=0;choice<2u;++choice)
    {
        PortableMeshCpuState initialized,driven;
        PortableMeshKartInput expected;
        CHECK(portable_mesh_cpu_init(&initialized,&session,choice,error,sizeof error)==PORTABLE_MESH_OK);
        CHECK(portable_mesh_cpu_input(&driven,&initialized,&session,&expected,error,sizeof error)==PORTABLE_MESH_OK);
        CHECK(portable_mesh_time_trial_pilot_step_route(&hinted,&zero,&session,choice,
            &hinted_input,error,sizeof error)==PORTABLE_MESH_OK);
        CHECK(hinted.requested_hint==choice && !memcmp(&hinted.cpu,&driven,sizeof driven) &&
            hinted_input.buttons==expected.buttons && hinted_input.steering==expected.steering);
    }
    PortableMeshTimeTrialPilotState hint_saved=hinted;
    CHECK(portable_mesh_time_trial_pilot_step_route(&hinted,&hinted,&session,1,
        &same,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(!memcmp(&hinted,&hint_saved,sizeof hinted) &&
        same.buttons==hinted_input.buttons && same.steering==hinted_input.steering);
    CHECK(portable_mesh_time_trial_pilot_step_route(&hinted,&hinted,&session,0,
        &same,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(hinted.cpu.route==0 && hinted.requested_hint==0); /* Same-turn choice change. */
    CHECK(portable_mesh_time_trial_pilot_step_route(&hinted,&hinted,&session,1,
        &same,error,sizeof error)==PORTABLE_MESH_OK);
    /* A real accepted RESP switches the CPU cursor, while the original request
     * stays1. It must not force the cursor back on subsequent ordinary turns. */
    uint8_t safe[48]={0};
    test_mesh_word(safe+24u,1073741824u);
    test_mesh_word(safe+32u,65536);
    test_mesh_word(safe+40u,0);
    f.course.tables[f.course.table_count++]=(PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('R','E','S','P'),1,48,safe};
    const PortableMeshTimeTrialSession before_hint=session;
    session.state.tick++;
    session.state.last_respawn=0;
    session.state.kart.respawn_ticks=1;
    CHECK(portable_mesh_time_trial_pilot_step_route(&hinted,&hinted,&session,1,
        &same,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(hinted.cpu.route==0 && hinted.requested_hint==1);
    session.state.tick++;
    session.state.kart.respawn_ticks=0;
    CHECK(portable_mesh_time_trial_pilot_step_route(&hinted,&hinted,&session,1,
        &same,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(hinted.cpu.route==0 && hinted.requested_hint==1);
    session=before_hint;
    session.state.tick=0;
    CHECK(portable_mesh_time_trial_pilot_step_route(&hinted,&hinted,&session,1,
        &same,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(hinted.cpu.route==1 && hinted.last_tick==0); /* Retry resets choice. */
    session=before_hint;
    --f.course.table_count;
    hint_saved=hinted;
    for (unsigned invalid=0;invalid<6u;++invalid)
    {
        const PortableMeshKartInput untouched={123,15};
        same=untouched;
        uint32_t choice=1;
        if (invalid==0) choice=2; /* Out of range. */
        if (invalid==1) test_mesh_word(headers+32u,1); /* ITEM route. */
        if (invalid==2) test_mesh_word(headers+32u+16u,0); /* Not closed. */
        if (invalid==3) test_mesh_word(edges+11u*4u,UINT32_MAX); /* Malformed graph. */
        if (invalid==4) session.mode=2; /* TT remains its first-route policy. */
        if (invalid==5) test_mesh_word(property+8u,0); /* Not continuous. */
        CHECK(portable_mesh_time_trial_pilot_step_route(&hinted,&zero,&session,choice,
            &same,error,sizeof error)!=PORTABLE_MESH_OK);
        CHECK(!memcmp(&hinted,&hint_saved,sizeof hinted) &&
            same.buttons==untouched.buttons && same.steering==untouched.steering && error[0]);
        test_mesh_word(headers+32u,0);
        test_mesh_word(headers+32u+16u,1);
        test_mesh_word(edges+11u*4u,8);
        test_mesh_word(property+8u,1);
        session.mode=1;
    }
    /* A second valid closed graph may branch: preserve the original ABSENT
     * controller rather than rejecting it or pricing an arbitrary branch. */
    PortableMeshTimeTrialPilotState fallback,fallback_saved;
    PortableMeshCpuState original,original_next;
    test_mesh_word(nodes+8u*32u+24u,2);
    CHECK(portable_mesh_cpu_init(&original,&session,PORTABLE_MESH_ABSENT,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(portable_mesh_cpu_input(&original_next,&original,&session,&same,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(portable_mesh_time_trial_pilot_step(&fallback,&zero,&session,&input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(fallback.cpu.route==0u && !memcmp(&fallback.cpu,&original_next,sizeof original_next));
    CHECK(input.buttons==same.buttons && input.steering==same.steering);
    fallback_saved=fallback;
    const PortableMeshKartInput branch_sentinel={123,15};
    input=branch_sentinel;
    test_mesh_word(edges+9u*4u,UINT32_MAX);
    CHECK(portable_mesh_time_trial_pilot_step(&fallback,&zero,&session,&input,error,sizeof error)!=PORTABLE_MESH_OK);
    CHECK(!memcmp(&fallback,&fallback_saved,sizeof fallback) &&
          input.buttons==branch_sentinel.buttons && input.steering==branch_sentinel.steering);
    test_mesh_word(edges+9u*4u,10);
    test_mesh_word(nodes+8u*32u+24u,1);
    /* A valid union of two cycles likewise has no unique lap to rank. */
    test_mesh_word(edges+9u*4u,8);
    test_mesh_word(edges+11u*4u,10);
    CHECK(portable_mesh_time_trial_pilot_step(&fallback,&zero,&session,&input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(!memcmp(&fallback.cpu,&original_next,sizeof original_next));
    CHECK(input.buttons==same.buttons && input.steering==same.steering);
    test_mesh_word(edges+9u*4u,10);
    test_mesh_word(edges+11u*4u,8);
    /* Restore the ranked input for the same-tick cache comparison below. */
    CHECK(portable_mesh_time_trial_pilot_step(&planner,&zero,&session,&input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(planner.cpu.route==1u);
    saved=planner;
    /* Authored slow nodes outweigh lane length, but never replan twice in an
     * accepted turn. Retry/tick rollback recomputes against current content. */
    for (unsigned i=8;i<12u;++i)
        test_mesh_word(nodes+i*32u+16u,65536);
    CHECK(portable_mesh_time_trial_pilot_step(&planner,&planner,&session,&same,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(!memcmp(&planner,&saved,sizeof planner) && input.buttons==same.buttons && input.steering==same.steering);
    session.state.tick=0;
    CHECK(portable_mesh_time_trial_pilot_step(&planner,&planner,&session,&input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(planner.cpu.route==0u);
    for (unsigned i=8;i<12u;++i)
        test_mesh_word(nodes+i*32u+16u,10*65536);
    ++f.rules.revision;
    CHECK(portable_mesh_time_trial_pilot_step(&planner,&planner,&session,&input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(planner.cpu.route==1u && planner.cpu.profile_revision==f.rules.revision);
    /* A malformed losing candidate must not silently disappear. */
    saved=planner;
    const PortableMeshKartInput sentinel={123,15};
    input=sentinel;
    test_mesh_word(edges+7u*4u,UINT32_MAX);
    ++f.rules.revision;
    CHECK(portable_mesh_time_trial_pilot_step(&planner,&planner,&session,&input,error,sizeof error)!=PORTABLE_MESH_OK);
    CHECK(!memcmp(&planner,&saved,sizeof planner) && input.buttons==sentinel.buttons && input.steering==sentinel.steering);
    test_mesh_word(edges+7u*4u,0);
    /* Identical geometry/speeds/tuning retain canonical route-index order. */
    test_mesh_word(headers+8u,4);
    for (unsigned i=0;i<4u;++i)
    {
        memcpy(nodes+i*32u,nodes+(i+8u)*32u,20);
        test_mesh_word(edges+i*4u,(i+1u)%4u);
    }
    CHECK(portable_mesh_time_trial_pilot_step(&planner,&zero,&session,&input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(planner.cpu.route==0u);
    /* One route is byte-exact with the original production initializer/input. */
    f.course.tables[header_table].count=1;
    PortableMeshCpuState cpu,next_cpu;
    CHECK(portable_mesh_cpu_init(&cpu,&session,PORTABLE_MESH_ABSENT,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(portable_mesh_cpu_input(&next_cpu,&cpu,&session,&same,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(portable_mesh_time_trial_pilot_step(&planner,&zero,&session,&input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(!memcmp(&next_cpu,&planner.cpu,sizeof next_cpu) && input.buttons==same.buttons && input.steering==same.steering);
    /* One candidate may branch: ranking must not narrow the existing CPU API. */
    test_mesh_word(nodes+24u,2);
    CHECK(portable_mesh_cpu_init(&cpu,&session,PORTABLE_MESH_ABSENT,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(portable_mesh_cpu_input(&next_cpu,&cpu,&session,&same,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(portable_mesh_time_trial_pilot_step(&planner,&zero,&session,&input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(!memcmp(&next_cpu,&planner.cpu,sizeof next_cpu) && input.buttons==same.buttons && input.steering==same.steering);
    test_mesh_word(nodes+24u,1);
    /* TT deliberately ignores nominal ranking, including a faster alternative. */
    f.course.tables[header_table].count=2;
    for (unsigned i=0;i<4u;++i)
        test_mesh_word(nodes+i*32u+16u,65536);
    session.mode=2;
    CHECK(portable_mesh_time_trial_pilot_step(&planner,&zero,&session,&input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(planner.cpu.route==0u);
    session.mode=4;
    CHECK(portable_mesh_time_trial_pilot_step(&planner,&planner,&session,&input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(planner.cpu.route==1u && planner.mode==4u);
    return 0;
}
static int airborne_ground_route(void)
{
    PortableTestMeshCourse f;
    test_mesh_course_init(&f);
    uint8_t header[32]={0},nodes[128]={0},edges[16]={0},property[32]={0};
    const uint8_t strings[]="mksc:cpu-ground-route";
    const int32_t xy[4][2]={{0,4*65536},{0,0},{4*65536,-7*32768},{-4*65536,-7*32768}};
    test_mesh_word(header+8u,4);
    test_mesh_word(header+16u,1);
    test_mesh_word(header+24u,1);
    test_mesh_word(property+4u,PORTABLE_COURSE_3D_PROPERTY_BOOLEAN);
    test_mesh_word(property+8u,1);
    for (unsigned i=0;i<4;++i)
    {
        test_mesh_word(nodes+i*32u,(uint32_t)xy[i][0]);
        test_mesh_word(nodes+i*32u+4u,(uint32_t)xy[i][1]);
        test_mesh_word(nodes+i*32u+12u,52429); /* .8m endpoint radius. */
        test_mesh_word(nodes+i*32u+16u,8*65536);
        test_mesh_word(nodes+i*32u+20u,i);
        test_mesh_word(nodes+i*32u+24u,1);
        test_mesh_word(edges+i*4u,(i+1u)%4u);
    }
#define GROUND_TABLE(tag,n,stride,data) f.course.tables[f.course.table_count++]=(PortableCourse3DTable){ \
    PORTABLE_COURSE_3D_TAG(tag[0],tag[1],tag[2],tag[3]),n,stride,data}
    GROUND_TABLE("RTHD",1,32,header);
    GROUND_TABLE("RTND",4,32,nodes);
    GROUND_TABLE("RTED",4,4,edges);
    GROUND_TABLE("STRS",sizeof strings,1,strings);
    GROUND_TABLE("PROP",1,32,property);
#undef GROUND_TABLE
    PortableMeshRacer racer;
    char error[192];
    CHECK(portable_mesh_time_trial_init(&racer,&f.course,&f.rules,0,error,sizeof error)==PORTABLE_MESH_OK);
    racer.state.phase=PORTABLE_MESH_RACER_RACING;
    racer.state.countdown_ticks=0;
    PortableMeshCpuState state,next,legacy,disabled,saved;
    PortableMeshKartInput input,legacy_input,disabled_input;
    CHECK(portable_mesh_cpu_init(&state,&racer,0,error,sizeof error)==PORTABLE_MESH_OK);
    const int32_t clearance=f.rules.kart_radius_q16+f.rules.ride_height_q16;
    racer.state.kart.position_q16[1]=-65536/5;
    racer.state.kart.position_q16[2]=clearance+5*32768;
    racer.state.kart.grounded=0;
    state.previous_foot_q16[1]=65536/5;
    state.previous_foot_q16[2]=5*32768;
    const uint64_t unchanged=portable_mesh_time_trial_checksum(&racer.state);
    test_mesh_word(header+24u,0); /* Absent retains the legacyXYZ behavior. */
    CHECK(portable_mesh_cpu_input(&legacy,&state,&racer,&legacy_input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(legacy.from_node==0 && legacy.to_node==1 && legacy.lost_ticks==1);
    test_mesh_word(header+24u,1);
    test_mesh_word(property+8u,0);
    CHECK(portable_mesh_cpu_input(&disabled,&state,&racer,&disabled_input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(!memcmp(&legacy,&disabled,sizeof legacy) && legacy_input.buttons==disabled_input.buttons &&
          legacy_input.steering==disabled_input.steering);
    test_mesh_word(property+8u,1);
    CHECK(portable_mesh_cpu_input(&next,&state,&racer,&input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(next.from_node==1 && next.to_node==2 && next.visit_serial==1 && next.lost_ticks==0);
    CHECK(input.steering>0 && next.previous_foot_q16[2]==5*32768);
    CHECK(unchanged==portable_mesh_time_trial_checksum(&racer.state));
    racer.state.kart.grounded=1; /* Grounded at another height is NOT planar arrival. */
    CHECK(portable_mesh_cpu_input(&next,&state,&racer,&input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(next.from_node==0 && next.to_node==1 && next.lost_ticks==1);
    racer.state.kart.grounded=0;
    memcpy(&saved,&next,sizeof saved);
    const PortableMeshKartInput sentinel={123,7};
    for (unsigned malformed=0;malformed<3;++malformed)
    {
        test_mesh_word(nodes+64u+8u,malformed==0?65536:0);
        test_mesh_word(property+4u,malformed==1?PORTABLE_COURSE_3D_PROPERTY_INTEGER:
                                             PORTABLE_COURSE_3D_PROPERTY_BOOLEAN);
        test_mesh_word(property+8u,malformed==2?2:1);
        input=sentinel;
        CHECK(portable_mesh_cpu_init(&next,&racer,0,error,sizeof error)==PORTABLE_MESH_UNSUPPORTED);
        CHECK(!memcmp(&next,&saved,sizeof saved));
        CHECK(portable_mesh_cpu_input(&next,&state,&racer,&input,error,sizeof error)==PORTABLE_MESH_UNSUPPORTED);
        CHECK(!memcmp(&next,&saved,sizeof saved) && input.buttons==sentinel.buttons &&
              input.steering==sentinel.steering);
    }
    test_mesh_word(nodes+64u+8u,0);
    test_mesh_word(property+4u,PORTABLE_COURSE_3D_PROPERTY_BOOLEAN);
    test_mesh_word(property+8u,1);
    /* Two crossing edges share height. Airborne route ownership must survive
     * longer than the normal lost-route timeout without jumping branches. */
    const int32_t crossing[4][2]={{-8*65536,-8*65536},{8*65536,8*65536},
                                 {-8*65536,8*65536},{8*65536,-8*65536}};
    for (unsigned i=0;i<4;++i)
    {
        test_mesh_word(nodes+i*32u,(uint32_t)crossing[i][0]);
        test_mesh_word(nodes+i*32u+4u,(uint32_t)crossing[i][1]);
    }
    const int32_t position[3]={-4*65536,-4*65536,clearance+5*32768};
    const int32_t forward[3]={759250125,759250125,0};
    CHECK(portable_mesh_kart_init(&racer.state.kart,&f.rules,position,forward)==PORTABLE_MESH_OK);
    racer.state.kart.grounded=0;
    CHECK(portable_mesh_cpu_init(&state,&racer,0,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(state.from_node==0 && state.to_node==1);
    racer.state.kart.position_q16[0]=65536/10;
    racer.state.kart.position_q16[1]=-65536/5;
    CHECK(portable_mesh_cpu_init(&next,&racer,0,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(next.from_node==2 && next.to_node==3); /* TrueXYZ global acquisition. */
    for (unsigned tick=0;tick<state.recovery_ticks+5u;++tick)
    {
        CHECK(portable_mesh_cpu_input(&state,&state,&racer,&input,error,sizeof error)==PORTABLE_MESH_OK);
        CHECK(state.from_node==0 && state.to_node==1 && state.visit_serial==0 && state.lost_ticks==0);
        CHECK(state.previous_foot_q16[2]==5*32768);
    }
    /* A genuine lateral miss still reaches normal trueXYZ acquisition. */
    racer.state.kart.position_q16[0]=8*65536;
    racer.state.kart.position_q16[1]=-6*65536;
    state.lost_ticks=state.recovery_ticks-1u;
    CHECK(portable_mesh_cpu_input(&next,&state,&racer,&input,error,sizeof error)==PORTABLE_MESH_OK);
    CHECK(next.from_node==2 && next.to_node==3 && next.lost_ticks==0);
    return 0;
}
static int production_cpu(void)
{
    PortableTestMeshCourse fixture;
    test_mesh_course_init(&fixture);
    uint8_t header[64] = {0}, nodes[224] = {0}, edges[32] = {0};
    test_mesh_word(header + 8u, 5);
    test_mesh_word(header + 16u, 1); /* Closed CPU route. */
    const int32_t positions[5][3] = {{0, 4 * 65536, 0},
                                     {0, 0, 0},
                                     {4 * 65536, -4 * 65536, 0},
                                     {0, 4 * 65536, 4 * 65536},
                                     {0, 0, 4 * 65536}};
    const uint32_t first[5] = {0, 2, 3, 4, 5}, counts[5] = {2, 1, 1, 1, 1};
    const uint32_t targets[6] = {1, 2, 2, 3, 4, 0};
    for (unsigned i = 0; i < 5u; ++i)
    {
        for (unsigned axis = 0; axis < 3u; ++axis)
            test_mesh_word(nodes + i * 32u + axis * 4u, (uint32_t)positions[i][axis]);
        test_mesh_word(nodes + i * 32u + 12u, 16384);
        test_mesh_word(nodes + i * 32u + 16u, 10 * 65536);
        test_mesh_word(nodes + i * 32u + 20u, first[i]);
        test_mesh_word(nodes + i * 32u + 24u, counts[i]);
    }
    for (unsigned i = 0; i < 6u; ++i)
        test_mesh_word(edges + i * 4u, targets[i]);
    fixture.course.tables[fixture.course.table_count++] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('R', 'T', 'H', 'D'), 1, 32, header};
    fixture.course.tables[fixture.course.table_count++] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('R', 'T', 'N', 'D'), 5, 32, nodes};
    fixture.course.tables[fixture.course.table_count++] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('R', 'T', 'E', 'D'), 6, 4, edges};
    PortableMeshRacer racer;
    char error[192];
    CHECK(portable_mesh_time_trial_init(&racer, &fixture.course, &fixture.rules, 0, error,
                                        sizeof error) == PORTABLE_MESH_OK);
    racer.state.phase = PORTABLE_MESH_RACER_RACING;
    racer.state.countdown_ticks = 0;
    PortableMeshCpuState state, next, repeat;
    PortableMeshKartInput input, same;
    CHECK(portable_mesh_cpu_init(&state, &racer, PORTABLE_MESH_ABSENT, error, sizeof error) ==
          PORTABLE_MESH_OK);
    CHECK(state.from_node == 0 && state.to_node == 1 && state.selected_edge == 0);
    const uint64_t before = portable_mesh_time_trial_checksum(&racer.state);
    CHECK(portable_mesh_cpu_input(&next, &state, &racer, &input, error, sizeof error) ==
          PORTABLE_MESH_OK);
    CHECK(portable_mesh_cpu_input(&repeat, &state, &racer, &same, error, sizeof error) ==
          PORTABLE_MESH_OK);
    CHECK(memcmp(&next, &repeat, sizeof next) == 0 && input.buttons == same.buttons &&
          input.steering == same.steering);
    CHECK(before == portable_mesh_time_trial_checksum(&racer.state) &&
          input.buttons == PORTABLE_MESH_ACCELERATE);
    /* Per-node speed is a control target, never an imposed velocity. */
    test_mesh_word(nodes + 32u + 16u, 65536);
    racer.state.kart.position_q16[1] = 2 * 65536;
    racer.state.kart.speed_q16 = 2 * 65536;
    CHECK(portable_mesh_cpu_input(&next, &state, &racer, &input, error, sizeof error) ==
              PORTABLE_MESH_OK &&
          input.buttons == PORTABLE_MESH_BRAKE);
    CHECK(racer.state.kart.speed_q16 == 2 * 65536);
    test_mesh_word(nodes + 32u + 16u, 10 * 65536);
    /* With weak authored brakes, the bend is still beyond the2m steering
     * target but inside v²/(2a), and faster than its ~6.4m/s arc. It must
     * already request braking. */
    const int32_t old_brake = fixture.rules.brake_deceleration_q16;
    const int32_t old_coast = fixture.rules.coast_deceleration_q16;
    fixture.rules.brake_deceleration_q16 = 65536;
    fixture.rules.coast_deceleration_q16 = 65536;
    racer.state.kart.position_q16[1] = 3 * 65536;
    racer.state.kart.speed_q16 = 8 * 65536;
    CHECK(portable_mesh_cpu_input(&next, &state, &racer, &input, error, sizeof error) ==
              PORTABLE_MESH_OK &&
          input.buttons == PORTABLE_MESH_BRAKE && input.steering == 0);
    for (unsigned variant = 0; variant < 2u; ++variant)
    {
        fixture.rules.brake_deceleration_q16 = variant ? 32768 : 0;
        fixture.rules.coast_deceleration_q16 = variant ? 2 * 65536 : 65536;
        PortableMeshRacer coasting = racer;
        coasting.state.kart.grounded = 1;
        coasting.state.kart.contact_surface = 0;
        coasting.state.kart.velocity_q16[1] = -8 * 65536;
        CHECK(portable_mesh_cpu_input(&next, &state, &coasting, &input, error, sizeof error) ==
                  PORTABLE_MESH_OK &&
              input.buttons == 0);
        PortableMeshRacerTurn turn;
        CHECK(portable_mesh_racer_move(&coasting, input, &turn, error, sizeof error) ==
              PORTABLE_MESH_OK);
        CHECK(portable_mesh_racer_publish(&coasting, &turn, error, sizeof error) ==
              PORTABLE_MESH_OK);
        CHECK(coasting.state.kart.grounded && coasting.state.kart.speed_q16 > 0 &&
              coasting.state.kart.speed_q16 < 8 * 65536 &&
              coasting.state.kart.velocity_q16[1] > -8 * 65536);
    }
    fixture.rules.brake_deceleration_q16 = 0;
    fixture.rules.coast_deceleration_q16 = 0;
    repeat = next;
    same = (PortableMeshKartInput){123, 7};
    input = same;
    CHECK(portable_mesh_cpu_init(&next, &racer, PORTABLE_MESH_ABSENT, error, sizeof error) ==
              PORTABLE_MESH_UNSUPPORTED &&
          memcmp(&next, &repeat, sizeof next) == 0 &&
          strstr(error, "no braking/coasting authority"));
    CHECK(portable_mesh_cpu_input(&next, &state, &racer, &input, error, sizeof error) ==
              PORTABLE_MESH_UNSUPPORTED &&
          memcmp(&next, &repeat, sizeof next) == 0 && input.buttons == same.buttons &&
          input.steering == same.steering);
    racer.state.kart.speed_q16 = 0;
    CHECK(portable_mesh_cpu_input(&next, &state, &racer, &input, error, sizeof error) ==
              PORTABLE_MESH_UNSUPPORTED &&
          memcmp(&next, &repeat, sizeof next) == 0 && input.buttons == same.buttons &&
          input.steering == same.steering);
    fixture.rules.brake_deceleration_q16 = old_brake;
    fixture.rules.coast_deceleration_q16 = old_coast;
    /* A swept arrival crosses a node even if no sampled foot lands inside its radius. */
    racer.state.kart.position_q16[1] = -65536;
    CHECK(portable_mesh_cpu_input(&next, &state, &racer, &input, error, sizeof error) ==
          PORTABLE_MESH_OK);
    CHECK(next.from_node == 1 && next.to_node == 2 && next.visit_serial == 1);
    /* Radius4 overlaps a4m edge ending at a right-angle corner. Being at
     * its start must not skip the edge or turn sideways before its endpoint. */
    uint8_t saved_nodes[160];
    memcpy(saved_nodes, nodes, sizeof saved_nodes);
    test_mesh_word(nodes + 32u + 4u, (uint32_t)(-4 * 65536));
    test_mesh_word(nodes + 64u, 4 * 65536);
    test_mesh_word(nodes + 64u + 4u, (uint32_t)(-4 * 65536));
    for (unsigned i = 0; i < 3u; ++i)
        test_mesh_word(nodes + i * 32u + 12u, 4 * 65536);
    racer.state.kart.position_q16[1] = 0;
    racer.state.kart.speed_q16 = 0;
    state.from_node = 0;
    state.to_node = 1;
    state.selected_edge = 0;
    state.previous_foot_q16[1] = 0;
    CHECK(portable_mesh_cpu_input(&next, &state, &racer, &input, error, sizeof error) ==
          PORTABLE_MESH_OK);
    CHECK(next.from_node == 0 && next.to_node == 1);
    /* Crossing the endpoint then advances exactly once, retaining the corner edge. */
    racer.state.kart.position_q16[1] = -4 * 65536;
    CHECK(portable_mesh_cpu_input(&next, &state, &racer, &input, error, sizeof error) ==
          PORTABLE_MESH_OK);
    CHECK(next.from_node == 1 && next.to_node == 2);
    memcpy(nodes, saved_nodes, sizeof saved_nodes);
    state.previous_foot_q16[1] = 4 * 65536;
    /* FullXYZ acquisition distinguishes the upper overlapping road. */
    racer.state.kart.position_q16[1] = 2 * 65536;
    racer.state.kart.position_q16[2] =
        4 * 65536 + fixture.rules.kart_radius_q16 + fixture.rules.ride_height_q16;
    CHECK(portable_mesh_cpu_init(&next, &racer, 0, error, sizeof error) == PORTABLE_MESH_OK);
    CHECK(next.from_node == 3 && next.to_node == 4);
    /* A displacement cannot immediately jump to a disconnected nearest edge. */
    CHECK(portable_mesh_cpu_input(&next, &state, &racer, &input, error, sizeof error) ==
          PORTABLE_MESH_OK);
    CHECK(next.from_node == 0 && next.to_node == 1 && next.lost_ticks == 1);
    for (unsigned i = 1; i < state.recovery_ticks; ++i)
        CHECK(portable_mesh_cpu_input(&next, &next, &racer, &input, error, sizeof error) ==
              PORTABLE_MESH_OK);
    CHECK(next.from_node == 3 && next.to_node == 4 && next.lost_ticks == 0);
    /* Entering respawn reacquires immediately and emits no driving action. */
    racer.state.kart.respawn_ticks = fixture.rules.respawn_ticks;
    CHECK(portable_mesh_cpu_input(&next, &state, &racer, &input, error, sizeof error) ==
          PORTABLE_MESH_OK);
    CHECK(next.from_node == 3 && next.to_node == 4 && input.buttons == 0 && input.steering == 0);
    /* Accepted RESP.route can switch to another independently owned CPU route. */
    test_mesh_word(header + 32u + 4u, 5);
    test_mesh_word(header + 32u + 8u, 2);
    test_mesh_word(header + 32u + 12u, 5);
    test_mesh_word(header + 32u + 16u, 1);
    for (unsigned i = 5; i < 7u; ++i)
    {
        test_mesh_word(nodes + i * 32u + 4u, i == 5u ? 4 * 65536 : 0);
        test_mesh_word(nodes + i * 32u + 8u, 8 * 65536);
        test_mesh_word(nodes + i * 32u + 12u, 16384);
        test_mesh_word(nodes + i * 32u + 16u, 65536);
        test_mesh_word(nodes + i * 32u + 20u, i + 1u);
        test_mesh_word(nodes + i * 32u + 24u, 1);
    }
    test_mesh_word(edges + 24u, 6);
    test_mesh_word(edges + 28u, 5);
    fixture.course.tables[7].count = 2;
    fixture.course.tables[8].count = 7;
    fixture.course.tables[9].count = 8;
    uint8_t respawn[48] = {0};
    test_mesh_word(respawn + 28u, 1);
    test_mesh_word(respawn + 32u, 65536);
    test_mesh_word(respawn + 40u, 1);
    fixture.course.tables[fixture.course.table_count++] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('R', 'E', 'S', 'P'), 1, 48, respawn};
    racer.state.last_respawn = 0;
    racer.state.kart.position_q16[2] =
        8 * 65536 + fixture.rules.kart_radius_q16 + fixture.rules.ride_height_q16;
    CHECK(portable_mesh_cpu_input(&next, &state, &racer, &input, error, sizeof error) ==
          PORTABLE_MESH_OK);
    CHECK(next.route == 1 && next.from_node == 5 && next.to_node == 6 && !input.buttons);
    /* Optional typed route controls are copied at preparation, not read every draw. */
    const uint8_t strings[] = "mksc:cpu-recovery-ticks";
    uint8_t property[32] = {0};
    test_mesh_word(property + 4u, PORTABLE_COURSE_3D_PROPERTY_INTEGER);
    test_mesh_word(property + 8u, 2);
    test_mesh_word(header + 32u + 24u, 1);
    fixture.course.tables[fixture.course.table_count++] = (PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('S', 'T', 'R', 'S'), sizeof strings, 1, strings};
    fixture.course.tables[fixture.course.table_count++] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('P', 'R', 'O', 'P'), 1, 32, property};
    CHECK(portable_mesh_cpu_init(&next, &racer, 1, error, sizeof error) == PORTABLE_MESH_OK &&
          next.recovery_ticks == 2);
    repeat = next;
    test_mesh_word(property + 4u, PORTABLE_COURSE_3D_PROPERTY_BOOLEAN);
    CHECK(portable_mesh_cpu_init(&next, &racer, 1, error, sizeof error) != PORTABLE_MESH_OK &&
          memcmp(&next, &repeat, sizeof next) == 0);
    /* A structurally valid boolean is still the wrong type for this control. */
    test_mesh_word(property + 8u, 1);
    CHECK(portable_mesh_cpu_init(&next, &racer, 1, error, sizeof error) != PORTABLE_MESH_OK &&
          memcmp(&next, &repeat, sizeof next) == 0);
    test_mesh_word(property + 4u, PORTABLE_COURSE_3D_PROPERTY_INTEGER);
    test_mesh_word(property + 8u, 2);
    PortableCourse3DRoute route;
    PortableCourse3DRouteNode node;
    uint32_t edge;
    CHECK(portable_course_3d_route(&fixture.course, 0, &route) && route.node_count == 5 &&
          route.flags == 1);
    CHECK(portable_course_3d_route_node(&fixture.course, 3, &node) &&
          node.position_q16[2] == 4 * 65536);
    CHECK(portable_course_3d_route_edge(&fixture.course, 1, &edge) && edge == 2);
    memset(&node, 0x5a, sizeof node);
    PortableCourse3DRouteNode untouched = node;
    CHECK(!portable_course_3d_route_node(&fixture.course, 7, &node) &&
          memcmp(&node, &untouched, sizeof node) == 0);
    /* Invalid content/state cannot publish a partial controller or input. */
    state = next;
    repeat = next;
    input = (PortableMeshKartInput){123, 7};
    same = input;
    test_mesh_word(edges, UINT32_MAX);
    CHECK(portable_mesh_cpu_init(&next, &racer, 0, error, sizeof error) != PORTABLE_MESH_OK);
    CHECK(memcmp(&next, &repeat, sizeof next) == 0);
    state.selected_edge = UINT32_MAX;
    racer.state.kart.respawn_ticks = 0;
    CHECK(portable_mesh_cpu_input(&next, &state, &racer, &input, error, sizeof error) !=
          PORTABLE_MESH_OK);
    CHECK(memcmp(&next, &repeat, sizeof next) == 0 && input.buttons == same.buttons &&
          input.steering == same.steering);
    return 0;
}
static int authored_pack_route(const char *path)
{
    int result = 1;
    FILE *file = fopen(path, "rb");
    uint8_t *bytes = NULL;
    char error[256] = {0};
    if (!file || fseek(file, 0, SEEK_END) != 0)
        goto cleanup;
    const long length = ftell(file);
    if (length < 1 || length > 64L * 1024L * 1024L || fseek(file, 0, SEEK_SET) != 0)
        goto cleanup;
    bytes = malloc((size_t)length);
    if (!bytes || fread(bytes, 1, (size_t)length, file) != (size_t)length)
        goto cleanup;
    if (fclose(file) != 0)
    {
        file = NULL;
        goto cleanup;
    }
    file = NULL;
    PortableAssetPackView pack;
    PortableCourse3DCatalog catalog;
    PortableCourse3DView course;
    PortableCourse3DMeta meta;
    PortableMeshKartRules rules;
    PortableMeshTimeTrialSession session;
    if (portable_asset_pack_open(bytes, (size_t)length, &pack) != PORTABLE_ASSET_PACK_OK ||
        !portable_course_3d_catalog_at(&pack, 0, &catalog, error, sizeof(error)) ||
        !portable_course_3d_open(&pack, catalog.course_id, &course, error, sizeof(error)) ||
        !portable_course_3d_meta(&course, &meta) ||
        !portable_mesh_kart_rules_asset_load(&pack, meta.physics_profile_id, &rules) ||
        portable_mesh_time_trial_init(&session, &course, &rules, 0, error, sizeof(error)) !=
            PORTABLE_MESH_OK)
        goto cleanup;
    uint32_t previous_gate = session.state.checkpoint;
    for (unsigned tick = 0; tick < 12000u && session.state.phase != PORTABLE_MESH_TT_FINISHED;
         ++tick)
    {
        PortableMeshKartInput input;
        PortableMeshKartEvents events;
        if (portable_mesh_time_trial_pilot(&session, &input, error, sizeof(error)) !=
                PORTABLE_MESH_OK ||
            portable_mesh_time_trial_tick(&session, input, &events, error, sizeof(error)) !=
                PORTABLE_MESH_OK)
            goto cleanup;
        if (session.state.tick % 100u == 0u || session.state.checkpoint != previous_gate)
        {
            const PortableMeshKartState *kart = &session.state.kart;
            printf(
                "mesh pilot tick=%u lap=%u gate=%u pos=%ld,%ld,%ld vel=%ld,%ld,%ld speed=%ld forward=%ld,%ld,%ld up=%ld,%ld,%ld input=%d/%u grounded=%u contact=%u\n",
                (unsigned)session.state.tick, (unsigned)session.state.lap,
                (unsigned)session.state.checkpoint, (long)kart->position_q16[0],
                (long)kart->position_q16[1], (long)kart->position_q16[2],
                (long)kart->velocity_q16[0], (long)kart->velocity_q16[1],
                (long)kart->velocity_q16[2], (long)kart->speed_q16, (long)kart->forward_q30[0],
                (long)kart->forward_q30[1], (long)kart->forward_q30[2], (long)kart->up_q30[0],
                (long)kart->up_q30[1], (long)kart->up_q30[2], (int)input.steering,
                (unsigned)input.buttons, (unsigned)kart->grounded,
                (unsigned)kart->contact_triangle);
            previous_gate = session.state.checkpoint;
        }
    }
    if (session.state.phase != PORTABLE_MESH_TT_FINISHED)
    {
        snprintf(error, sizeof(error), "authored route did not finish in 12000 accepted ticks");
        goto cleanup;
    }
    printf("mesh pilot PASS course=%u ticks=%u laps=%u checksum=%016llx\n", (unsigned)course.id,
           (unsigned)session.state.race_ticks, (unsigned)session.state.lap,
           (unsigned long long)portable_mesh_time_trial_checksum(&session.state));
    /* The production controller must complete the authored bridge too; a
     * nonzero cursor alone would not prove that it can reach every real gate. */
    PortableMeshRacer cpu_racer;
    PortableMeshCpuState cpu;
    if (portable_mesh_racer_init(&cpu_racer, &course, &rules, 1u, 4u, error, sizeof error) !=
            PORTABLE_MESH_OK ||
        portable_mesh_cpu_init(&cpu, &cpu_racer, PORTABLE_MESH_ABSENT, error, sizeof error) !=
            PORTABLE_MESH_OK)
        goto cleanup;
    uint32_t visits = 0, respawns = 0;
    for (unsigned tick = 0; tick < 12000u && cpu_racer.state.phase != PORTABLE_MESH_RACER_FINISHED;
         ++tick)
    {
        PortableMeshCpuState staged_cpu;
        PortableMeshKartInput control;
        PortableMeshRacer staged_racer = cpu_racer;
        PortableMeshRacerTurn turn;
        const uint64_t before = portable_mesh_time_trial_checksum(&cpu_racer.state);
        if (portable_mesh_cpu_input(&staged_cpu, &cpu, &cpu_racer, &control, error, sizeof error) !=
                PORTABLE_MESH_OK ||
            before != portable_mesh_time_trial_checksum(&cpu_racer.state) ||
            (control.buttons & PORTABLE_MESH_RESCUE) ||
            portable_mesh_racer_move(&staged_racer, control, &turn, error, sizeof error) !=
                PORTABLE_MESH_OK ||
            portable_mesh_racer_publish(&staged_racer, &turn, error, sizeof error) !=
                PORTABLE_MESH_OK)
            goto cleanup;
        if (staged_cpu.visit_serial > cpu.visit_serial)
            visits += staged_cpu.visit_serial - cpu.visit_serial;
        if (staged_racer.state.kart.respawn_ticks && !cpu_racer.state.kart.respawn_ticks)
        {
            ++respawns;
            printf(
                "mesh CPU respawn tick=%u gate=%u edge=%u->%u pos=%ld,%ld,%ld speed=%ld input=%d/%u\n",
                (unsigned)cpu_racer.state.tick, (unsigned)cpu_racer.state.checkpoint,
                (unsigned)cpu.from_node, (unsigned)cpu.to_node,
                (long)cpu_racer.state.kart.position_q16[0],
                (long)cpu_racer.state.kart.position_q16[1],
                (long)cpu_racer.state.kart.position_q16[2], (long)cpu_racer.state.kart.speed_q16,
                (int)control.steering, (unsigned)control.buttons);
        }
        cpu = staged_cpu;
        cpu_racer = staged_racer;
    }
    if (cpu_racer.state.phase != PORTABLE_MESH_RACER_FINISHED || cpu_racer.state.lap != meta.laps ||
        visits < meta.laps)
    {
        snprintf(
            error, sizeof error,
            "production CPU did not complete authored route: tick=%u lap=%u gate=%u edge=%u->%u visits=%u respawns=%u",
            (unsigned)cpu_racer.state.tick, (unsigned)cpu_racer.state.lap,
            (unsigned)cpu_racer.state.checkpoint, (unsigned)cpu.from_node, (unsigned)cpu.to_node,
            (unsigned)visits, (unsigned)respawns);
        goto cleanup;
    }
    printf("mesh CPU PASS course=%u ticks=%u laps=%u visits=%u respawns=%u checksum=%016llx\n",
           (unsigned)course.id, (unsigned)cpu_racer.state.race_ticks, (unsigned)cpu_racer.state.lap,
           (unsigned)visits, (unsigned)respawns,
           (unsigned long long)portable_mesh_time_trial_checksum(&cpu_racer.state));
    result = 0;
cleanup:
    if (file)
        fclose(file);
    free(bytes);
    if (result)
        fprintf(stderr, "mesh authored route: %s\n", error[0] ? error : "bounded pack read failed");
    return result;
}
static int hazard_seek(void)
{
    enum { Q = 65536 };
    static PortableMeshCourseActorsConfig config;
    static PortableMeshCourseActors actors;
    PortableTestMeshCourse floor;
    test_mesh_course_init(&floor);
    PortableMeshRacer racer = {.course = &floor.course, .rules = &floor.rules};
    const int32_t position[3] = {0, -8 * Q, 3 * Q / 4};
    const int32_t forward[3] = {0, 1073741824, 0};
    CHECK(portable_mesh_kart_init(&racer.state.kart, racer.rules, position, forward) == PORTABLE_MESH_OK);
    racer.state.phase = PORTABLE_MESH_RACER_RACING;
    racer.state.kart.grounded = 1;
    PortableMeshItems items = {0};
    CHECK(portable_mesh_item_status_init(&items.status, 1, 1) == PORTABLE_MESH_ITEM_STATUS_OK);
    PortableMeshCourseActorEvents events = {0};
    PortableMeshHazardSeekState seek = {0};
    PortableMeshKartInput input = {0};
    config = (PortableMeshCourseActorsConfig){.course_id = floor.course.id,
        .profile_id = 1, .revision = 1, .scale_q16 = Q / 8, .kamek_count = 1};
    CHECK(portable_course_prop_init(&config.initial_kameks[0], 100 * Q, 100 * Q, 0xa00000, 224, 0));
    for (unsigned i = 0; i < 1024; ++i)
    {
        int32_t sine, cosine;
        portable_fixed_3d_sincos((uint16_t)(i * 64), &sine, &cosine);
        config.sine[i] = sine / 32768;
    }
    CHECK(portable_mesh_course_actors_init(&config, &actors, NULL, 0));
#define SEEK_TURN() portable_mesh_time_trial_pilot_seek_hazard(&seek, &racer, 0, &items, \
        &config, &actors, &events, 0, &input)
    CHECK(SEEK_TURN() && seek.target == 1 && seek.overriding && !seek.imminent);
    CHECK(!portable_mesh_time_trial_pilot_seek_item(&seek, 1) && seek.item_pending);
    /* A low patrol supplies a meeting point, not a goal that retreats every
     * tick once the chase window has passed. Waiting at that point must not spend the Star while the actor is away. */
    int32_t goal[3];
    memcpy(goal, seek.goal_q16, sizeof goal);
    for (unsigned tick = 1; tick <= 65; ++tick)
    {
        CHECK(portable_mesh_course_actors_advance(&config, &actors, NULL, 0));
        ++racer.state.tick;
        CHECK(SEEK_TURN() && seek.target == 1 && !memcmp(goal, seek.goal_q16, sizeof goal));
        PortableMeshHazardSeekState cached = seek;
        CHECK(SEEK_TURN() && !memcmp(&cached, &seek, sizeof seek));
    }
    racer.state.kart.position_q16[1] = 0;
    ++racer.state.tick;
    CHECK(SEEK_TURN() && !seek.imminent);
    CHECK(!portable_mesh_time_trial_pilot_seek_item(&seek, 0) && seek.item_pending);
    for (unsigned tick = 0; tick < 256 && !seek.imminent; ++tick)
    {
        CHECK(portable_mesh_course_actors_advance(&config, &actors, NULL, 0));
        ++racer.state.tick;
        CHECK(SEEK_TURN() && seek.target == 1 && !memcmp(goal, seek.goal_q16, sizeof goal));
    }
    CHECK(seek.imminent && portable_mesh_time_trial_pilot_seek_item(&seek, 0) && !seek.item_pending);
    events.kamek_contacts = events.kamek_touch_mask = 1;
    ++racer.state.tick;
    CHECK(SEEK_TURN() && !seek.target && seek.contacts == 1);

    /* Holding an item for a walker, the kart waits short of the meeting
     * point facing it, so the walk crosses the line a Star drives along;
     * without one it drives on to the meeting point. */
    config = (PortableMeshCourseActorsConfig){.course_id = floor.course.id,
        .profile_id = 1, .revision = 1, .scale_q16 = Q / 8, .mechakoopa_count = 1};
    for (unsigned i = 0; i < 1024; ++i)
    {
        int32_t sine, cosine;
        portable_fixed_3d_sincos((uint16_t)(i * 64), &sine, &cosine);
        config.sine[i] = sine / 32768;
    }
    CHECK(portable_course_prop_init(&config.initial_mechakoopas[0], 100 * Q, 100 * Q, 0xa10000, 224, 0));
    config.mechakoopa_anchors_q16[0][0] = -3 * Q;
    CHECK(portable_mesh_course_actors_init(&config, &actors, NULL, 0));
    seek = (PortableMeshHazardSeekState){0};
    events = (PortableMeshCourseActorEvents){0};
    racer.state.kart.position_q16[0] = -3 * Q;
    racer.state.kart.position_q16[1] = -6 * Q / 5;
    ++racer.state.tick;
    CHECK(SEEK_TURN() && seek.target == 1 && seek.imminent && !seek.walked);
    memcpy(goal, seek.goal_q16, sizeof goal);
    for (unsigned tick = 0; tick < 16; ++tick)
    {
        CHECK(portable_mesh_course_actors_advance(&config, &actors, NULL, 0));
        ++racer.state.tick;
        input = (PortableMeshKartInput){0};
        CHECK(SEEK_TURN() && seek.target == 1 && !memcmp(goal, seek.goal_q16, sizeof goal));
    }
    CHECK(seek.walked && !seek.imminent && input.buttons == PORTABLE_MESH_ACCELERATE);
    CHECK(!portable_mesh_time_trial_pilot_seek_item(&seek, 1) && seek.item_pending);
    CHECK(portable_mesh_course_actors_advance(&config, &actors, NULL, 0));
    ++racer.state.tick;
    input = (PortableMeshKartInput){0};
    CHECK(SEEK_TURN() && seek.overriding && !input.buttons && !input.steering);
    racer.state.kart.position_q16[0] = 0;
    racer.state.kart.position_q16[1] = 0;

    /* Hidden Piranhas are collidable before their bite reveal. */
    config = (PortableMeshCourseActorsConfig){.course_id = floor.course.id,
        .profile_id = 1, .revision = 1, .scale_q16 = Q / 8, .piranha_count = 1};
    CHECK(portable_piranha_init(&config.initial_piranhas[0], 100 * Q, 100 * Q, 0, 0));
    config.piranha_anchors_q16[0][1] = 2 * Q;
    CHECK(portable_mesh_course_actors_init(&config, &actors, NULL, 0));
    seek = (PortableMeshHazardSeekState){0};
    events = (PortableMeshCourseActorEvents){0};
    CHECK(SEEK_TURN() && seek.target == 1 && !seek.visible && seek.imminent);
    CHECK(portable_mesh_time_trial_pilot_seek_item(&seek, 1));
    CHECK(portable_piranha_contact(&actors.piranhas[0], 0x2000, 0, 1, 1));
    CHECK(portable_mesh_course_actors_advance(&config, &actors, NULL, 0));
    ++racer.state.tick;
    CHECK(SEEK_TURN() && !seek.imminent);

    /* A grounded Thwomp rising out of reach must retain a pending item;
     * its later descent through the same line releases it. */
    config = (PortableMeshCourseActorsConfig){.course_id = floor.course.id,
        .profile_id = 1, .revision = 1, .scale_q16 = Q / 8, .thwomp_count = 1};
    CHECK(portable_thwomp_init(&config.initial_thwomps[0], 100 * Q, 100 * Q, 0, 0));
    config.thwomp_anchors_q16[0][1] = 2 * Q;
    for (unsigned i = 0; i < 5; ++i)
    {
        config.crush_rules.counts[i] = 1;
        config.crush_rules.rows[i][0] = (PortableDriverAffineRow){256, 256, 2};
    }
    config.crush_rules.rows[2][0].inverse_y_q8 = 512;
    config.crush_rules.rows[3][0].inverse_y_q8 = 512;
    CHECK(portable_mesh_course_actors_init(&config, &actors, NULL, 0));
    for (unsigned tick = 0; tick < 37; ++tick)
        CHECK(portable_mesh_course_actors_advance(&config, &actors, NULL, 0));
    CHECK(actors.thwomps[0].state == 2 && !actors.thwomps[0].height_q16);
    seek = (PortableMeshHazardSeekState){0};
    CHECK(SEEK_TURN() && seek.imminent);
    CHECK(portable_mesh_course_actors_advance(&config, &actors, NULL, 0));
    ++racer.state.tick;
    CHECK(SEEK_TURN() && !seek.imminent);
    CHECK(!portable_mesh_time_trial_pilot_seek_item(&seek, 1) && seek.item_pending);
    for (unsigned tick = 0; tick < 160 && actors.thwomps[0].state != 5; ++tick)
    {
        CHECK(portable_mesh_course_actors_advance(&config, &actors, NULL, 0));
        ++racer.state.tick;
        CHECK(SEEK_TURN());
        if (actors.thwomps[0].state == 3 || actors.thwomps[0].state == 4)
            CHECK(!seek.imminent); /* Top rumble is not the descending crush. */
        if (actors.thwomps[0].state == 3 && actors.thwomps[0].timer == 32)
        {
            /* Closing the vertical gap by driving uphill is not a falling actor. */
            racer.state.kart.position_q16[2] += Q / 8;
            CHECK(portable_mesh_course_actors_advance(&config, &actors, NULL, 0));
            ++racer.state.tick;
            CHECK(SEEK_TURN() && !seek.imminent);
            CHECK(!portable_mesh_time_trial_pilot_seek_item(&seek, 0) && seek.item_pending);
        }
    }
    CHECK(actors.thwomps[0].state == 5);
    CHECK(portable_mesh_course_actors_advance(&config, &actors, NULL, 0));
    ++racer.state.tick;
    CHECK(SEEK_TURN() && !seek.imminent);
    CHECK(portable_mesh_course_actors_advance(&config, &actors, NULL, 0));
    ++racer.state.tick;
    CHECK(SEEK_TURN() && seek.imminent);
    CHECK(portable_mesh_time_trial_pilot_seek_item(&seek, 0) && !seek.item_pending);
#undef SEEK_TURN
    return 0;
}
int main(int argc, char **argv)

{
    if (argc == 2)
        return authored_pack_route(argv[1]);
    if (argc != 1)
        return 2;
    CHECK(hazard_seek() == 0);
    CHECK(midrace_item_window() == 0);
    CHECK(defensive_green_hold() == 0);
    CHECK(final_finish_window() == 0);
    CHECK(panel_bend_arrival() == 0);
    CHECK(route_tick_budget() == 0);
    CHECK(continuous_pilot() == 0);
    CHECK(nominal_lane_selection() == 0);
    CHECK(production_cpu() == 0);
    CHECK(airborne_ground_route() == 0);
    PortableTestMeshCourse fixture;
    test_mesh_course_init(&fixture);
    uint8_t header[32] = {0}, nodes[64] = {0}, edge[4] = {0};
    test_mesh_word(header + 8u, 2);
    test_mesh_word(nodes + 4u, 4u * 65536u);
    test_mesh_word(nodes + 12u, 65536);
    test_mesh_word(nodes + 16u, 10u * 65536u);
    test_mesh_word(nodes + 24u, 1);
    test_mesh_word(nodes + 32u + 4u, (uint32_t)(-4 * 65536));
    test_mesh_word(nodes + 32u + 12u, 65536);
    test_mesh_word(nodes + 32u + 16u, 10u * 65536u);
    test_mesh_word(edge, 1);
    fixture.course.tables[fixture.course.table_count++] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('R', 'T', 'H', 'D'), 1, 32, header};
    fixture.course.tables[fixture.course.table_count++] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('R', 'T', 'N', 'D'), 2, 32, nodes};
    fixture.course.tables[fixture.course.table_count++] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('R', 'T', 'E', 'D'), 1, 4, edge};
    PortableMeshTimeTrial session;
    PortableMeshKartEvents events;
    char error[160];
    CHECK(portable_mesh_time_trial_init(&session, &fixture.course, &fixture.rules, 0, error,
                                        sizeof(error)) == PORTABLE_MESH_OK);
    for (unsigned tick = 0; tick < 500u && session.state.phase != PORTABLE_MESH_TT_FINISHED; ++tick)
    {
        PortableMeshKartInput input;
        const uint64_t before = portable_mesh_time_trial_checksum(&session.state);
        CHECK(portable_mesh_time_trial_pilot(&session, &input, error, sizeof(error)) ==
              PORTABLE_MESH_OK);
        CHECK(before == portable_mesh_time_trial_checksum(&session.state));
        CHECK(input.steering == 0 && !(input.buttons & PORTABLE_MESH_RESCUE));
        CHECK(portable_mesh_time_trial_tick(&session, input, &events, error, sizeof(error)) ==
              PORTABLE_MESH_OK);
    }
    CHECK(session.state.phase == PORTABLE_MESH_TT_FINISHED && session.state.lap == 1);
    PortableMeshKartInput input = {1, 15};
    CHECK(portable_mesh_time_trial_pilot(&session, &input, error, sizeof(error)) ==
              PORTABLE_MESH_OK &&
          !input.buttons && !input.steering);
    CHECK(portable_mesh_time_trial_init(&session, &fixture.course, &fixture.rules, 0, error,
                                        sizeof(error)) == PORTABLE_MESH_OK);
    for (unsigned tick = 0; tick < 180u; ++tick)
        CHECK(portable_mesh_time_trial_tick(&session, (PortableMeshKartInput){0, 0}, &events, error,
                                            sizeof(error)) == PORTABLE_MESH_OK);
    /* An authored yaw almost opposite the route must request a decisive turn,
     * not a tiny sine-derived correction that converges away from 180 degrees too slowly. */
    test_mesh_word(fixture.grid + 20u, 1073709056);
    test_mesh_word(fixture.grid + 24u, 8388608);
    CHECK(portable_mesh_time_trial_init(&session, &fixture.course, &fixture.rules, 0, error,
                                        sizeof(error)) == PORTABLE_MESH_OK);
    for (unsigned tick = 0; tick < 180u; ++tick)
        CHECK(portable_mesh_time_trial_tick(&session, (PortableMeshKartInput){0, 0}, &events, error,
                                            sizeof(error)) == PORTABLE_MESH_OK);
    CHECK(session.state.kart.forward_q30[1] > 1070000000 && session.state.kart.forward_q30[0] > 0);
    CHECK(portable_mesh_time_trial_pilot(&session, &input, error, sizeof(error)) ==
              PORTABLE_MESH_OK &&
          input.steering == -32767);
    test_mesh_word(edge, UINT32_MAX);
    CHECK(portable_mesh_time_trial_pilot(&session, &input, error, sizeof(error)) ==
              PORTABLE_MESH_UNSUPPORTED &&
          strstr(error, "CPU route"));
    return 0;
}
