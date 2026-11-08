#include "mesh_item_audio.h"
#include "recovered_pack.h"
#include "mesh_course_fixture.h"
#include "mesh_item_fixture.h"
#include "mesh_item_rules_asset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "item audio line %d: %s\n", __LINE__, #c);                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static PortableMeshItems world;
static PortableMeshItemEvents events;
static PortableMeshRacerState racers[8];
static PortableMeshItemAudio state;
static PortableMeshItemAudioOutput output, saved_output;
static uint16_t voices[8][2];
static PortableMeshItemAudioView view;

static void fixture(void)
{
    memset(&world, 0, sizeof world);
    memset(&events, 0, sizeof events);
    memset(racers, 0, sizeof racers);
    memset(&state, 0, sizeof state);
    world.inventory.count = world.status.count = 3;
    world.inventory.human_mask = world.status.human_mask = 3;
    for (unsigned i = 0; i < 8; ++i)
        for (unsigned j = 0; j < 2; ++j)
            voices[i][j] = (uint16_t)(300 + i * 2 + j);
    view = (PortableMeshItemAudioView){.world = &world,
                                       .events = &events,
                                       .racers = racers,
                                       .mushroom_voices = (const uint16_t (*)[2])voices,
                                       .human_mask = 3,
                                       .course_song = 28,
                                       .donor = {7, 2, 1}};
}
static void event(unsigned kind, unsigned item, unsigned racer, unsigned target, unsigned flags)
{
    events.values[events.count++] = (PortableMeshItemEvent){.kind = (uint8_t)kind,
                                                            .item = (uint16_t)item,
                                                            .racer = (uint8_t)racer,
                                                            .target = (uint8_t)target,
                                                            .flags = (uint8_t)flags,
                                                            .actor = UINT32_MAX};
}
static int step(void)
{
    return portable_mesh_item_audio_step(&state, &view, &output, NULL, 0);
}
static int cues_and_music(void)
{
    fixture();
    event(PORTABLE_MESH_EVENT_PICKUP, 1, 0, 255, 0);
    event(PORTABLE_MESH_EVENT_PICKUP, 1, 2, 255, 0);
    event(PORTABLE_MESH_EVENT_BOOST, 0x1007, 0, 255, 0);
    event(PORTABLE_MESH_EVENT_BOOST, 0x1007, 1, 255, 0);
    world.status.racers[1].lightning_ticks = 2;
    event(PORTABLE_MESH_EVENT_USE, 0x100a, 0, 255, 0);
    event(PORTABLE_MESH_EVENT_USE, 0x100a, 1, 255, 0);
    event(PORTABLE_MESH_EVENT_USE, 0x100c, 2, 255, 0);
    world.status.racers[0].star_ticks = 9;
    world.status.racers[0].boo_invisible_ticks = 3;
    world.status.racers[0].boo_lock_ticks = 2;
    CHECK(step() && output.count == 7);
    CHECK(output.cues[0].song == 121 && output.cues[0].player == 6);
    CHECK(output.cues[1].song == 103 && output.cues[1].player == 7);
    CHECK(output.cues[2].song == 314 && output.cues[3].song == 125);
    CHECK(output.cues[4].song == 305 && output.cues[5].song == 125);
    CHECK(output.cues[6].song == 126 && output.cues[6].player == 10);
    CHECK(output.music.star_uses == 1 && output.music.lightning_uses == 1);
    CHECK(output.music.star_active && output.music.boo_active && output.haunt_active);
    CHECK(step() && output.count == 0 && state.star_uses == 1 && state.lightning_uses == 1);
    return 0;
}
static int actor_sounds(void)
{
    fixture();
    /* Z alone attenuates; nearby second human is chosen only once. */
    racers[2].kart.position_q16[2] = 32 * 65536;
    event(PORTABLE_MESH_EVENT_RELEASE, 0x1002, 2, 255, PORTABLE_MESH_ITEM_EVENT_BACKWARD);
    event(PORTABLE_MESH_EVENT_RELEASE, 0x1005, 0, 255, 0);
    event(PORTABLE_MESH_EVENT_LANDED, 0x1005, 0, 255, 0);
    event(PORTABLE_MESH_EVENT_IMPACT, 0x1000, 0, 255, 0);
    event(PORTABLE_MESH_EVENT_HIT, 0x1005, 0, 2, 0);
    event(PORTABLE_MESH_EVENT_HIT, 0x1002, 0, 2, 0);
    event(PORTABLE_MESH_EVENT_DESTROY, 0x1000, 0, 255, 0);
    event(PORTABLE_MESH_EVENT_GOLDEN_EXPIRED, 0x100e, 0, 255, 0);
    CHECK(step() && output.count == 5);
    CHECK(output.cues[0].song == 134 && output.cues[0].player == 6);
    CHECK(output.cues[0].volume > 0 && output.cues[0].volume < 256);
    CHECK(output.cues[1].song == 122 && output.cues[2].song == 124);
    CHECK(output.cues[3].song == 135 && output.cues[4].song == 132);
    ++view.ordinal;
    events.count = 1;
    racers[1].kart.position_q16[2] = 32 * 65536;
    CHECK(step() && output.count == 1 && output.cues[0].volume == 256);
    ++view.ordinal;
    events.values[0].actor = 3;
    events.values[0].serial = 45;
    world.actors[3].serial = 45;
    world.actors[3].phase = PORTABLE_MESH_ACTOR_FLYING;
    world.actors[3].motion.position_q16[2] = 100 * 65536;
    CHECK(step() && output.count == 0);
    ++view.ordinal;
    world.actors[3].serial = 46; /* Reused slot is not the event's sound source. */
    CHECK(step() && output.count == 1 && output.cues[0].volume == 256);
    return 0;
}
static int source_crab_contact_cues(void)
{
    fixture();
    PortableMeshCourseActorEvents contacts={.hit_mask=4,.protected_mask=1};
    view.actor_events=&contacts;
    racers[2].kart.position_q16[2]=32*65536;
    CHECK(step() && output.count==2);
    CHECK(output.cues[0].song==160 && output.cues[0].player==PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER &&
        output.cues[0].volume==256);
    CHECK(output.cues[1].song==373 && output.cues[1].player==PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER &&
        output.cues[1].volume==192);
    CHECK(events.count==0); /* Typed actor contacts never manufacture item events. */
    const PortableMeshItemAudio accepted=state;
    CHECK(step() && output.count==0 && !memcmp(&accepted,&state,sizeof state));
    ++view.ordinal;
    racers[1].kart.position_q16[2]=32*65536; /* Nearest other human gets full cue. */
    CHECK(step() && output.count==2 && output.cues[1].volume==256);
    ++view.ordinal;
    racers[2].kart.position_q16[2]=100*65536;
    CHECK(step() && output.count==1 && output.cues[0].song==160);
    const PortableMeshItemAudio saved=state;
    saved_output=output;
    for(unsigned invalid=0;invalid<2;++invalid)
    {
        ++view.ordinal;
        contacts.hit_mask=invalid?1u:8u;
        CHECK(!step() && !memcmp(&saved,&state,sizeof state) &&
            !memcmp(&saved_output,&output,sizeof output));
    }
    contacts=(PortableMeshCourseActorEvents){.bump_mask=4};
    racers[2].kart.position_q16[2]=32*65536;
    racers[1].kart.position_q16[2]=0;
    ++view.ordinal;
    CHECK(step() && output.count==1 && output.cues[0].song==373 &&
        output.cues[0].player==PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER && output.cues[0].volume==192);
    CHECK(step() && output.count==0);
    const PortableMeshItemAudio bump_saved=state;
    saved_output=output;
    for(unsigned invalid=0;invalid<3;++invalid)
    {
        ++view.ordinal;
        contacts.bump_mask=invalid==0?8:4;
        contacts.hit_mask=invalid==1?4:0;
        contacts.protected_mask=invalid==2?4:0;
        CHECK(!step() && !memcmp(&bump_saved,&state,sizeof state) &&
            !memcmp(&saved_output,&output,sizeof output));
    }
    view.actor_events=NULL;
    CHECK(step() && output.count==0); /* Optional absent view retains old adapter behavior. */
    return 0;
}
static int penguin_contact_cues(void)
{
    fixture();
    PortableMeshCourseActorEvents contacts={.hit_mask=4,.protected_mask=1,
        .penguin_contacts=3,.penguin_defeated=2};
    view.actor_events=&contacts;
    racers[2].kart.position_q16[2]=32*65536;
    CHECK(step() && output.count==2 && output.cues[0].song==160 &&
        output.cues[0].player==PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER &&
        output.cues[1].song==373 && output.cues[1].volume==192 && !contacts.bump_mask);
    const PortableMeshItemAudio accepted=state;
    CHECK(step() && !output.count && !memcmp(&state,&accepted,sizeof state));
    ++view.ordinal;contacts.protected_mask=4;
    const PortableMeshItemAudioOutput unchanged=output;
    CHECK(!step() && !memcmp(&state,&accepted,sizeof state) &&
        !memcmp(&output,&unchanged,sizeof output));
    return 0;
}
static int mouse_contact_cues(void)
{
    fixture();
    PortableMeshCourseActorEvents contacts={.hit_mask=4,.protected_mask=1,
        .mouse_contacts=3,.mouse_defeated=2};
    view.actor_events=&contacts;
    racers[2].kart.position_q16[2]=32*65536;
    CHECK(step() && output.count==2 && output.cues[0].song==160 &&
        output.cues[0].player==PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER &&
        output.cues[1].song==373 && output.cues[1].volume==192 && !contacts.bump_mask);
    const PortableMeshItemAudio accepted=state;
    CHECK(step() && !output.count && !memcmp(&state,&accepted,sizeof state));
    ++view.ordinal;contacts.protected_mask=4;
    const PortableMeshItemAudioOutput unchanged=output;
    CHECK(!step() && !memcmp(&state,&accepted,sizeof state) &&
        !memcmp(&output,&unchanged,sizeof output));
    return 0;
}
static int piranha_contact_cues(void)
{
    fixture();
    PortableMeshCourseActorEvents contacts={.piranha_touch_mask=5,.piranha_bite_mask=5,
        .capture_mask=5,.launch_mask=5};
    view.actor_events=&contacts;
    CHECK(step() && output.count==4 && output.cues[0].song==373 &&
        output.cues[1].song==378 && output.cues[2].song==162 && output.cues[3].song==381 &&
        output.cues[3].player==6);
    for(unsigned i=0;i<4;++i)CHECK(output.cues[i].volume==256);
    const PortableMeshItemAudio accepted=state;
    CHECK(step() && !output.count && !memcmp(&state,&accepted,sizeof state));
    for(unsigned invalid=0;invalid<4;++invalid)
    {
        ++view.ordinal;
        contacts=(PortableMeshCourseActorEvents){0};
        if(invalid==0)contacts.piranha_touch_mask=8;
        if(invalid==1)contacts.piranha_bite_mask=8;
        if(invalid==2)contacts.capture_mask=8;
        if(invalid==3)contacts.launch_mask=8;
        const PortableMeshItemAudioOutput unchanged=output;
        CHECK(!step() && !memcmp(&state,&accepted,sizeof state) &&
            !memcmp(&output,&unchanged,sizeof output));
    }
    return 0;
}
static int seagull_contact_cues(void)
{
    fixture();
    PortableMeshCourseActorEvents contacts={.seagull_contacts=1,.seagull_changed=1,
        .seagull_touch_mask=5}; /* CPU bit2 never emits ordinary contact audio. */
    view.actor_events=&contacts;
    CHECK(step() && output.count==1 && output.cues[0].song==385 &&
        output.cues[0].player==PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER && output.cues[0].volume==256);
    const PortableMeshItemAudio accepted=state;
    CHECK(step() && !output.count && !memcmp(&state,&accepted,sizeof state));
    ++view.ordinal;contacts=(PortableMeshCourseActorEvents){.seagull_contacts=1,
        .seagull_defeated=1,.protected_mask=1};
    CHECK(step() && output.count==1 && output.cues[0].song==160 &&
        output.cues[0].player==PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER &&
        !contacts.hit_mask && !contacts.bump_mask);
    const PortableMeshItemAudio protected_state=state;
    CHECK(step() && !output.count && !memcmp(&state,&protected_state,sizeof state));
    ++view.ordinal;contacts.seagull_touch_mask=8;
    const PortableMeshItemAudioOutput unchanged=output;
    CHECK(!step() && !memcmp(&state,&protected_state,sizeof state) &&
        !memcmp(&output,&unchanged,sizeof output));
    return 0;
}
static int lava_contact_cues(void)
{
    fixture();
    PortableMeshCourseActorEvents contacts={.lava_contacts=3,.lava_defeated=2,
        .lava_burn_mask=4,.hit_mask=4,.protected_mask=1};
    view.actor_events=&contacts;racers[2].kart.position_q16[2]=32*65536;
    CHECK(step() && output.count==2 && output.cues[0].song==160 &&
        output.cues[1].song==377 && output.cues[1].volume==192 &&
        output.cues[1].player==PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER);
    const PortableMeshItemAudio accepted=state;
    CHECK(step() && !output.count && !memcmp(&state,&accepted,sizeof state));
    ++view.ordinal;contacts.lava_burn_mask=8;
    const PortableMeshItemAudioOutput unchanged=output;
    CHECK(!step() && !memcmp(&state,&accepted,sizeof state) &&
        !memcmp(&output,&unchanged,sizeof output));
    return 0;
}
static int append_rock(PortableMeshCourseActorsConfig *config, unsigned order, int x)
{
    config->rock_count=1;
    if (!portable_course_prop_init(&config->initial_rocks[0],100*65536,100*65536,
                                  0xa20000u,224u,0u)) return 0;
    config->initial_rocks[0].placement_order=(uint8_t)order;
    config->rock_anchors_q16[0][0]=x*65536;
    for (unsigned i=0;i<5;++i) config->rock_frame_ids[0][i]=120+i;
    return 1;
}
static int append_teepee(PortableMeshCourseActorsConfig *config, unsigned order)
{
    config->teepee_count=1;
    if (!portable_course_prop_init(&config->initial_teepees[0],100*65536,100*65536,
                                  0x030000u,256u,0u)) return 0;
    config->initial_teepees[0].placement_order=(uint8_t)order;
    config->teepee_anchors_q16[0][0]=160*65536;
    for(unsigned i=0;i<11;++i)config->teepee_frame_ids[0][i]=140+i;
    return 1;
}
static int append_falling_star(PortableMeshCourseActorsConfig *config,unsigned order)
{
    config->falling_star_count=1;
    if(!portable_falling_star_init(&config->initial_falling_stars[0],100*65536,100*65536,0,0))return 0;
    config->initial_falling_stars[0].placement_order=(uint8_t)order;
    config->falling_star_anchors_q16[0][0]=200*65536;
    for(unsigned i=0;i<5;++i)config->falling_star_frame_ids[0][i]=160+i;
    return 1;
}
static int append_thundercloud(PortableMeshCourseActorsConfig *config,unsigned order)
{
    config->thundercloud_count=1;
    if(!portable_thundercloud_init(&config->initial_thunderclouds[0],100*65536,100*65536,0,0))return 0;
    config->initial_thunderclouds[0].placement_order=(uint8_t)order;
    config->thundercloud_anchors_q16[0][0]=240*65536;
    for(unsigned i=0;i<72;++i)config->thundercloud_frame_ids[0][i]=180+i;
    return 1;
}
static int append_common_prop(PortableMeshCourseActorsConfig *config,unsigned order)
{
    config->common_prop_count=1;
    if(!portable_course_prop_init(&config->initial_common_props[0],100*65536,100*65536,0x060000u,256,0))return 0;
    config->initial_common_props[0].placement_order=(uint8_t)order;
    config->common_prop_anchors_q16[0][0]=280*65536;
    config->common_prop_frame_ids[0][0]=260;
    return 1;
}
static int append_mechakoopa(PortableMeshCourseActorsConfig *config,unsigned order)
{
    config->mechakoopa_count=1;
    if(!portable_course_prop_init(&config->initial_mechakoopas[0],100*65536,100*65536,0xa10000u,224,0))return 0;
    config->initial_mechakoopas[0].placement_order=(uint8_t)order;
    config->mechakoopa_anchors_q16[0][0]=320*65536;
    for(unsigned i=0;i<4;++i)config->mechakoopa_frame_ids[0][i]=280+i;
    return 1;
}
static int append_kamek(PortableMeshCourseActorsConfig *config,unsigned order)
{
    config->kamek_count=1;
    if(!portable_course_prop_init(&config->initial_kameks[0],100*65536,100*65536,0xa00000u,224,0))return 0;
    config->initial_kameks[0].placement_order=(uint8_t)order;
    config->kamek_anchors_q16[0][0]=360*65536;
    config->kamek_frame_ids[0][0]=300;config->kamek_frame_ids[0][1]=301;
    return 1;
}
static int append_pier_boo(PortableMeshCourseActorsConfig *config,unsigned order)
{
    config->pier_boo_count=1;config->actor_visual_seed=123;
    config->pier_boo_rules=(PortableBooRules){1,360,64,1024,1024,3*65536,16384};
    if(!portable_course_prop_init(&config->initial_pier_boos[0],100*65536,100*65536,0xa30000u,256,0))return 0;
    config->initial_pier_boos[0].placement_order=(uint8_t)order;
    config->pier_boo_anchors_q16[0][0]=400*65536;
    for(unsigned i=0;i<3;++i)config->pier_boo_frame_ids[0][i]=320+i;
    return 1;
}
static int thwomp_contact_cues(void)
{
    fixture();
    uint16_t impact[8][2][2];
    for(unsigned d=0;d<8;++d)for(unsigned r=0;r<2;++r)for(unsigned z=0;z<2;++z)
        impact[d][r][z]=(uint16_t)(300+d*4+r*2+z);
    view.impact_voices=(const uint16_t (*)[2][2])impact;
    PortableMeshCourseActorEvents contacts={.thwomp_bump_mask=1,.bump_mask=1};
    view.actor_events=&contacts;
    CHECK(step() && output.count==1 && output.cues[0].song==169 && !output.stop_164_mask);
    ++view.ordinal;contacts=(PortableMeshCourseActorEvents){.thwomp_crush_mask=1,.hit_mask=1};
    CHECK(step() && output.count==2 && output.cues[0].song==169 &&
        output.cues[1].song==impact[7][0][0] && output.stop_164_mask==1);
    const PortableMeshItemAudio accepted=state;
    CHECK(step() && !output.count && !output.stop_164_mask && !memcmp(&state,&accepted,sizeof state));
    ++view.ordinal;contacts=(PortableMeshCourseActorEvents){.thwomp_crush_mask=4,.hit_mask=4};
    world.status.racers[2].lightning_ticks=1;racers[2].kart.position_q16[2]=32*65536;
    CHECK(step() && output.count==2 && output.cues[0].song==169 &&
        output.cues[1].song==impact[1][1][1] && output.cues[1].volume==192 && !output.stop_164_mask);
    ++view.ordinal;contacts=(PortableMeshCourseActorEvents){.thwomp_recover_mask=3};
    CHECK(step() && output.count==2 && output.cues[0].song==133 && output.cues[1].song==133);
    ++view.ordinal;contacts=(PortableMeshCourseActorEvents){.thwomp_defeated=1,.protected_mask=1};
    CHECK(step() && output.count==1 && output.cues[0].song==160);
    PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
    PortableMeshCourseActors *actors=malloc(sizeof *actors);
    CHECK(config && actors);
    config->course_id=config->profile_id=config->revision=1;config->scale_q16=6554;
    config->thwomp_count=1;
    CHECK(portable_thwomp_init(&config->initial_thwomps[0],100*65536,100*65536,0,0));
    config->thwomp_frame_ids[0][0]=1;
    for(unsigned table=0;table<5;++table)
    {
        config->crush_rules.counts[table]=1;
        config->crush_rules.rows[table][0]=(PortableDriverAffineRow){256,256,1};
    }
    /* A later family must not shift the Thwomp landing listener origin. */
    config->cannonball_count=1;
    CHECK(portable_cannonball_init(&config->initial_cannonballs[0],0,0,0,0));
    config->initial_cannonballs[0].placement_order=1;
    config->cannonball_anchors_q16[0][0]=40*65536;
    for(unsigned frame=0;frame<7;++frame)config->cannonball_frame_ids[0][frame]=frame+10;
    config->boo_count=1;
    CHECK(portable_boo_init(&config->initial_boos[0],0,0,0,1));
    config->initial_boos[0].placement_order=2;
    config->boo_anchors_q16[0][0]=80*65536;
    config->boo_frame_ids[0][0]=110;config->boo_frame_ids[0][1]=111;
    CHECK(append_rock(config,config->thwomp_count ? 3u : 2u,120));
    CHECK(append_teepee(config,config->thwomp_count ? 4u : 3u));
    CHECK(append_falling_star(config,config->thwomp_count ? 5u : 4u));
    CHECK(append_thundercloud(config,config->thwomp_count ? 6u : 5u));
    CHECK(append_common_prop(config,config->thwomp_count ? 7u : 6u));
    CHECK(append_mechakoopa(config,config->thwomp_count ? 8u : 7u));
    CHECK(append_kamek(config,config->thwomp_count ? 9u : 8u));
    CHECK(append_pier_boo(config,config->thwomp_count ? 10u : 9u));
    CHECK(portable_mesh_course_actors_init(config,actors,NULL,0));
    view.source_actor_config=config;view.source_actors=actors;
    racers[0].phase=racers[1].phase=PORTABLE_MESH_RACER_RACING;
    ++view.ordinal;contacts=(PortableMeshCourseActorEvents){.thwomp_landed=1};
    CHECK(step() && output.count==1 && output.cues[0].song==384 && output.cues[0].volume==256);
    ++view.ordinal;racers[0].phase=racers[1].phase=PORTABLE_MESH_RACER_FINISHED;
    CHECK(step() && !output.count); /* No live listener, even at zero distance. */
    const PortableMeshItemAudio saved=state;
    const PortableMeshItemAudioOutput old=output;
    ++view.ordinal;contacts.thwomp_landed=2;
    CHECK(!step() && !memcmp(&state,&saved,sizeof state) && !memcmp(&output,&old,sizeof output));
    contacts=(PortableMeshCourseActorEvents){.thwomp_recover_mask=8};
    CHECK(!step() && !memcmp(&state,&saved,sizeof state) && !memcmp(&output,&old,sizeof output));
    free(config);free(actors);return 0;
}
static int cannonball_cues(void)
{
    fixture();
    PortableMeshCourseActorEvents contacts = {.cannonball_hit_mask=1,.hit_mask=1};
    view.actor_events = &contacts;
    CHECK(step() && output.count == 2 && output.cues[0].song == 170 && output.cues[1].song == 162);
    ++view.ordinal;
    contacts = (PortableMeshCourseActorEvents){.cannonball_protected_mask=1,.protected_mask=1};
    CHECK(step() && output.count == 1 && output.cues[0].song == 160);
    PortableMeshCourseActorsConfig *config = calloc(1,sizeof *config);
    PortableMeshCourseActors *actors = malloc(sizeof *actors);
    CHECK(config && actors);
    config->course_id = config->profile_id = config->revision = 1;
    config->scale_q16 = 6554; config->cannonball_count = 1;
    CHECK(portable_cannonball_init(&config->initial_cannonballs[0],100*65536,100*65536,0,0));
    for (unsigned i=0;i<7;++i) config->cannonball_frame_ids[0][i]=i+1;
    /* Appending Boo must not shift either launch/explosion listener origin. */
    config->boo_count=1;
    CHECK(portable_boo_init(&config->initial_boos[0],0,0,0,1));
    config->initial_boos[0].placement_order=1;
    config->boo_anchors_q16[0][0]=80*65536;
    config->boo_frame_ids[0][0]=110;config->boo_frame_ids[0][1]=111;
    CHECK(append_rock(config,config->thwomp_count ? 3u : 2u,120));
    CHECK(append_teepee(config,config->thwomp_count ? 4u : 3u));
    CHECK(append_falling_star(config,config->thwomp_count ? 5u : 4u));
    CHECK(append_thundercloud(config,config->thwomp_count ? 6u : 5u));
    CHECK(append_common_prop(config,config->thwomp_count ? 7u : 6u));
    CHECK(append_mechakoopa(config,config->thwomp_count ? 8u : 7u));
    CHECK(append_kamek(config,config->thwomp_count ? 9u : 8u));
    CHECK(append_pier_boo(config,config->thwomp_count ? 10u : 9u));
    CHECK(portable_mesh_course_actors_init(config,actors,NULL,0));
    view.source_actor_config = config; view.source_actors = actors;
    racers[0].phase = racers[1].phase = PORTABLE_MESH_RACER_RACING;
    ++view.ordinal; contacts = (PortableMeshCourseActorEvents){.cannonball_launched=1};
    CHECK(step() && output.count == 1 && output.cues[0].song == 382 &&
          output.cues[0].require_idle && output.cues[0].volume == 256);
    CHECK(step() && !output.count);
    ++view.ordinal; contacts = (PortableMeshCourseActorEvents){.cannonball_exploded=1};
    CHECK(step() && output.count == 1 && output.cues[0].song == 383 && !output.cues[0].require_idle);
    ++view.ordinal; racers[0].phase = racers[1].phase = PORTABLE_MESH_RACER_FINISHED;
    CHECK(step() && !output.count);
    const PortableMeshItemAudio saved = state;
    const PortableMeshItemAudioOutput before = output;
    ++view.ordinal; contacts.cannonball_exploded = 2;
    CHECK(!step() && !memcmp(&state,&saved,sizeof state) && !memcmp(&output,&before,sizeof output));
    contacts = (PortableMeshCourseActorEvents){.cannonball_hit_mask=8};
    CHECK(!step() && !memcmp(&state,&saved,sizeof state));
    free(actors); free(config);
    return 0;
}
static int rock_cues(void)
{
    fixture();
    PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
    PortableMeshCourseActors *actors=malloc(sizeof *actors);
    CHECK(config && actors);
    config->course_id=config->profile_id=config->revision=1;
    config->scale_q16=65536;
    CHECK(append_rock(config,0,0));
    CHECK(append_teepee(config,1));
    CHECK(append_falling_star(config,2));
    CHECK(append_thundercloud(config,3));
    CHECK(append_common_prop(config,4));
    CHECK(append_mechakoopa(config,5));
    CHECK(append_kamek(config,6));
    CHECK(append_pier_boo(config,7));
    CHECK(portable_mesh_course_actors_init(config,actors,NULL,0));
    PortableMeshCourseActorEvents contacts={0};
    view.actor_events=&contacts;view.source_actor_config=config;view.source_actors=actors;
    contacts=(PortableMeshCourseActorEvents){.rock_contacts=1,.rock_hit_mask=1,.hit_mask=1,.lava_burn_mask=1};
    CHECK(step() && output.count==1 && output.cues[0].song==377 && output.cues[0].volume==256);
    ++view.ordinal;
    contacts=(PortableMeshCourseActorEvents){.rock_contacts=1,.rock_protected_mask=1,.protected_mask=1};
    CHECK(step() && output.count==1 && output.cues[0].song==160 && output.cues[0].volume==256);
    ++view.ordinal;
    for (unsigned i=0;i<2;++i)
    {
        racers[i].phase=PORTABLE_MESH_RACER_RACING;
        racers[i].kart.forward_q30[1]=1073741824;
        racers[i].kart.up_q30[2]=1073741824;
    }
    for (unsigned k=0;k<4;++k)
    {
        contacts=(PortableMeshCourseActorEvents){0};contacts.rock_cue_masks[k]=1;
        CHECK(step() && output.count==1 && output.cues[0].song==386+k &&
              output.cues[0].volume==256 && output.cues[0].player==PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER &&
              output.cues[0].require_idle==(k==0));
        const PortableMeshItemAudio accepted=state;
        CHECK(step() && !output.count && !memcmp(&state,&accepted,sizeof state));
        ++view.ordinal;
    }
    /* Far outside any horizontal viewport, but still in positive chase depth. */
    config->rock_anchors_q16[0][0]=30*65536;
    contacts=(PortableMeshCourseActorEvents){0};contacts.rock_cue_masks[0]=1;
    CHECK(step() && output.count==1 && output.cues[0].song==386 &&
          output.cues[0].volume==(4096u-900u)*256u/(4096u-16u));
    ++view.ordinal;config->rock_anchors_q16[0][0]=56*65536;
    CHECK(step() && !output.count); /* Positive depth but volume<=63. */
    ++view.ordinal;
    config->rock_anchors_q16[0][0]=0;config->rock_anchors_q16[0][1]=-20*65536;
    CHECK(step() && !output.count); /* Behind camera suppresses only386. */
    for (unsigned k=1;k<4;++k)
    {
        ++view.ordinal;contacts=(PortableMeshCourseActorEvents){0};contacts.rock_cue_masks[k]=1;
        CHECK(step() && output.count==1 && output.cues[0].song==386+k &&
              output.cues[0].volume==(4096u-400u)*256u/(4096u-16u) && !output.cues[0].require_idle);
    }
    /* Use real source advances to put falling art above/behind camera depth.
     * The projected ground anchor remains in front, so386 still sounds. */
    config->rock_anchors_q16[0][1]=0;
    for (unsigned i=0;i<113;++i)
        CHECK(portable_mesh_course_actors_advance(config,actors,NULL,0));
    CHECK(actors->rocks[0].height_q16>47*65536);
    ++view.ordinal;contacts=(PortableMeshCourseActorEvents){0};contacts.rock_cue_masks[0]=1;
    CHECK(step() && output.count==1 && output.cues[0].song==386 &&
          output.cues[0].volume>63 && output.cues[0].volume<256);
    ++view.ordinal;racers[0].phase=racers[1].phase=PORTABLE_MESH_RACER_FINISHED;
    for (unsigned k=0;k<4;++k) contacts.rock_cue_masks[k]=1;
    CHECK(step() && !output.count);
    const PortableMeshItemAudio saved=state;
    const PortableMeshItemAudioOutput before=output;
    ++view.ordinal;view.source_actors=NULL;
    CHECK(!step() && !memcmp(&state,&saved,sizeof state) && !memcmp(&output,&before,sizeof output));
    view.source_actors=actors;contacts.rock_cue_masks[2]=2;
    CHECK(!step() && !memcmp(&state,&saved,sizeof state) && !memcmp(&output,&before,sizeof output));
    contacts.rock_cue_masks[2]=1;view.source_actor_config=NULL;
    CHECK(!step() && !memcmp(&state,&saved,sizeof state) && !memcmp(&output,&before,sizeof output));
    view.source_actor_config=config;
    contacts=(PortableMeshCourseActorEvents){.rock_hit_mask=8};
    CHECK(!step() && !memcmp(&state,&saved,sizeof state) && !memcmp(&output,&before,sizeof output));
    contacts=(PortableMeshCourseActorEvents){0};contacts.rock_cue_masks[1]=1;
    racers[0].phase=racers[1].phase=PORTABLE_MESH_RACER_RACING;
    CHECK(step() && output.count==1 && output.cues[0].song==387);
    free(config);free(actors);return 0;
}
static int coin_pickup_cues(void)
{
    fixture();
    PortableMeshCourseActorEvents contacts={0};
    view.actor_events=&contacts;
    contacts.coins.picked[0]=UINT64_C(0x8000000000000003);
    contacts.coins.before[0]=5;contacts.coins.after[0]=6;
    contacts.coins.before[7]=contacts.coins.after[7]=5; /* Fixed owner slots retain counts. */
    contacts.coins.picked[2]=4; /* CPU accepted pickup is silent. */
    CHECK(step() && output.count==1 && output.cues[0].song==101 &&
          output.cues[0].player==7 && output.cues[0].volume==256 && !output.cues[0].require_idle);
    const PortableMeshItemAudio accepted=state;
    CHECK(step() && !output.count && !memcmp(&accepted,&state,sizeof state));
    ++view.ordinal;contacts.coins.picked[1]=8;
    CHECK(step() && output.count==2);
    for (unsigned i=0;i<2;++i)
        CHECK(output.cues[i].song==101 && output.cues[i].player==7 && output.cues[i].volume==256);
    ++view.ordinal;memset(&contacts,0,sizeof contacts);
    contacts.coins.before[0]=6;contacts.coins.after[0]=2;
    contacts.coins.lost[0]=4;contacts.coins.causes[0]=1u<<PORTABLE_MESH_COIN_HIT;
    CHECK(step() && !output.count); /* Loss does not invent a pickup/loss song. */
    ++view.ordinal;contacts.coins.before[0]=99;contacts.coins.after[0]=0;
    contacts.coins.picked[0]=1;contacts.coins.lost[0]=100;
    contacts.coins.causes[0]=(1u<<PORTABLE_MESH_COIN_CONTACT)|(1u<<PORTABLE_MESH_COIN_HAUNT);
    CHECK(step() && output.count==1 && output.cues[0].song==101);
    const PortableMeshItemAudio saved=state;
    const PortableMeshItemAudioOutput before=output;
    ++view.ordinal;contacts.coins.picked[3]=1;
    CHECK(!step() && !memcmp(&state,&saved,sizeof state) && !memcmp(&output,&before,sizeof output));
    contacts.coins.picked[3]=0;contacts.coins.after[0]=100;
    CHECK(!step() && !memcmp(&state,&saved,sizeof state) && !memcmp(&output,&before,sizeof output));
    contacts.coins.after[0]=2;contacts.coins.lost[0]=101;
    CHECK(!step() && !memcmp(&state,&saved,sizeof state) && !memcmp(&output,&before,sizeof output));
    contacts.coins.lost[0]=100;contacts.coins.causes[0]=0x80;
    CHECK(!step() && !memcmp(&state,&saved,sizeof state) && !memcmp(&output,&before,sizeof output));
    memset(&contacts,0,sizeof contacts);contacts.coins.picked[1]=2;
    CHECK(step() && output.count==1 && output.cues[0].song==101);
    ++view.ordinal;memset(&contacts,0,sizeof contacts);
    CHECK(step() && !output.count);
    return 0;
}
static int pier_boo_cues(void)
{
    fixture();
    PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
    PortableMeshCourseActors *actors=malloc(sizeof *actors);
    CHECK(config && actors);config->course_id=config->profile_id=config->revision=1;config->scale_q16=6554;
    CHECK(append_pier_boo(config,0) && portable_mesh_course_actors_init(config,actors,NULL,0));
    view.source_actor_config=config;view.source_actors=actors;
    PortableMeshCourseActorEvents contacts={.pier_boo_touch_mask=1};view.actor_events=&contacts;
    CHECK(step() && output.count==1 && output.cues[0].song==380 && !output.haunt_active);
    contacts=(PortableMeshCourseActorEvents){0};
    PortableBooAttack *haunt=&actors->source_haunts[0];
    *haunt=(PortableBooAttack){.occupied=1,.target=0};
    PortableBooAttackTarget target={0};
    CHECK(portable_boo_attack_tick(haunt,&target,config->sine,1024,&config->pier_boo_rules));
    actors->source_haunt_lock_mask=1;world.status.racers[0].boo_lock_ticks=120;
    ++view.ordinal;CHECK(step() && !output.count && !output.haunt_active);
    actors->source_haunt_lock_mask=0; /* Existing MIR lock remains independent. */
    ++view.ordinal;CHECK(step() && output.haunt_active);
    actors->source_haunt_lock_mask=1;world.status.racers[0].boo_lock_ticks=0;
    for(unsigned n=1;n<64;++n)CHECK(portable_boo_attack_tick(haunt,&target,config->sine,1024,&config->pier_boo_rules));
    CHECK(haunt->phase==2 && target.lock_ticks==360);
    ++view.ordinal;CHECK(step() && output.haunt_active); /* Actual phase wins even without MIR clock. */
    const PortableMeshItemAudio accepted=state;
    CHECK(step() && !output.count && !memcmp(&state,&accepted,sizeof state));
    target.lock_ticks=0;CHECK(portable_boo_attack_tick(haunt,&target,config->sine,1024,&config->pier_boo_rules));
    CHECK(haunt->phase==4);++view.ordinal;CHECK(step() && !output.haunt_active);
    *haunt=(PortableBooAttack){.occupied=1,.target=0};target.protection=1;
    for(unsigned n=0;n<64;++n)CHECK(portable_boo_attack_tick(haunt,&target,config->sine,1024,&config->pier_boo_rules));
    CHECK(haunt->phase==6);++view.ordinal;CHECK(step() && !output.haunt_active);
    contacts=(PortableMeshCourseActorEvents){.pier_boo_protected_mask=1,.protected_mask=1};
    ++view.ordinal;CHECK(step() && output.count==1 && output.cues[0].song==160);
    contacts=(PortableMeshCourseActorEvents){.pier_boo_touch_mask=4};
    ++view.ordinal;CHECK(step() && !output.count);
    const PortableMeshItemAudio saved=state;const PortableMeshItemAudioOutput old=output;
    ++view.ordinal;contacts.pier_boo_touch_mask=8;
    CHECK(!step() && !memcmp(&state,&saved,sizeof state) && !memcmp(&output,&old,sizeof output));
    free(actors);free(config);return 0;
}
static int kamek_cues(void)
{
    fixture();
    PortableMeshCourseActorEvents contacts={.kamek_touch_mask=1};view.actor_events=&contacts;
    CHECK(step() && !output.count); /* Ordinary contact is intentionally silent. */
    ++view.ordinal;contacts=(PortableMeshCourseActorEvents){.kamek_protected_mask=1,.protected_mask=1};
    CHECK(step() && output.count==1 && output.cues[0].song==160 && output.cues[0].volume==256);
    const PortableMeshItemAudio accepted=state;
    CHECK(step() && !output.count && !memcmp(&state,&accepted,sizeof state));
    ++view.ordinal;contacts=(PortableMeshCourseActorEvents){.kamek_protected_mask=4,.protected_mask=4};
    CHECK(step() && !output.count);
    const PortableMeshItemAudio saved=state;const PortableMeshItemAudioOutput old=output;
    ++view.ordinal;contacts.kamek_touch_mask=8;
    CHECK(!step() && !memcmp(&state,&saved,sizeof state) && !memcmp(&output,&old,sizeof output));
    return 0;
}
static int mechakoopa_cues(void)
{
    fixture();
    PortableMeshCourseActorEvents contacts={.mechakoopa_hit_mask=1,.hit_mask=1};
    view.actor_events=&contacts;
    CHECK(step() && output.count==1 && output.cues[0].song==169 && output.cues[0].volume==256);
    const PortableMeshItemAudio accepted=state;
    CHECK(step() && !output.count && !memcmp(&state,&accepted,sizeof state));
    ++view.ordinal;contacts=(PortableMeshCourseActorEvents){.mechakoopa_protected_mask=2,.protected_mask=2};
    CHECK(step() && output.count==1 && output.cues[0].song==160);
    ++view.ordinal;contacts=(PortableMeshCourseActorEvents){.mechakoopa_hit_mask=4,.mechakoopa_strong_mask=4,.hit_mask=4};
    CHECK(step() && !output.count); /* CPU source strong spin has no local contact voice. */
    const PortableMeshItemAudio saved=state;const PortableMeshItemAudioOutput old=output;
    ++view.ordinal;contacts.mechakoopa_strong_mask=8;
    CHECK(!step() && !memcmp(&state,&saved,sizeof state) && !memcmp(&output,&old,sizeof output));
    return 0;
}
static int common_prop_cues(void)
{
    fixture();
    PortableMeshCourseActorEvents contacts={.common_prop_hit_mask=1,.bump_mask=1,
        .common_prop_umbrella_open_mask=1};
    view.actor_events=&contacts;
    CHECK(step() && output.count==2 && output.cues[0].song==169 && output.cues[1].song==376 &&
          output.cues[0].volume==256 && output.cues[1].volume==256 &&
          output.cues[0].player==PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER);
    const PortableMeshItemAudio accepted=state;
    CHECK(step() && !output.count && !memcmp(&state,&accepted,sizeof state));
    ++view.ordinal;contacts.common_prop_umbrella_open_mask=0;contacts.common_prop_umbrella_close_mask=1;
    CHECK(step() && output.count==2 && output.cues[0].song==169 && output.cues[1].song==375);
    ++view.ordinal;contacts=(PortableMeshCourseActorEvents){.common_prop_protected_mask=2,.protected_mask=2};
    CHECK(step() && output.count==1 && output.cues[0].song==160);
    ++view.ordinal;contacts=(PortableMeshCourseActorEvents){.common_prop_hit_mask=4,.bump_mask=4,
        .common_prop_umbrella_open_mask=4};
    CHECK(step() && !output.count);
    const PortableMeshItemAudio saved=state;const PortableMeshItemAudioOutput old=output;
    ++view.ordinal;contacts.common_prop_umbrella_close_mask=8;
    CHECK(!step() && !memcmp(&state,&saved,sizeof state) && !memcmp(&output,&old,sizeof output));
    return 0;
}
static int thundercloud_cues(void)
{
    fixture();
    PortableMeshCourseActorsConfig *config=calloc(1,sizeof *config);
    CHECK(config);config->thundercloud_count=1;view.source_actor_config=config;
    PortableMeshCourseActorEvents contacts={.thundercloud_hit_mask=1,.hit_mask=1};
    view.actor_events=&contacts;racers[0].phase=racers[1].phase=PORTABLE_MESH_RACER_RACING;
    world.status.racers[0].lightning_ticks=360;
    CHECK(step() && output.count==2 && output.cues[0].song==374 && output.cues[1].song==162);
    const PortableMeshItemAudio accepted=state;
    CHECK(step() && !output.count && !memcmp(&state,&accepted,sizeof state));
    ++view.ordinal;contacts=(PortableMeshCourseActorEvents){.thundercloud_protected_mask=2,.protected_mask=2};
    world.status.racers[0].star_ticks=1; /* Star retains the underlying shrink clock. */
    CHECK(step() && output.count==1 && output.cues[0].song==160);
    ++view.ordinal;contacts=(PortableMeshCourseActorEvents){0};
    world.status.racers[0].star_ticks=0;world.status.racers[0].lightning_ticks=1;
    world.status.racers[2].lightning_ticks=1;
    CHECK(step() && !output.count);
    world.status.racers[0].lightning_ticks=0; /* Same-ordinal view cannot emit expiry. */
    CHECK(step() && !output.count);
    ++view.ordinal;world.status.racers[2].lightning_ticks=0;
    CHECK(step() && output.count==1 && output.cues[0].song==133);
    CHECK(step() && !output.count);
    ++view.ordinal;contacts=(PortableMeshCourseActorEvents){.thundercloud_hit_mask=4,.hit_mask=4};
    CHECK(step() && !output.count);
    const PortableMeshItemAudio saved=state;
    const PortableMeshItemAudioOutput old=output;
    ++view.ordinal;contacts.thundercloud_hit_mask=8;
    CHECK(!step() && !memcmp(&saved,&state,sizeof state) && !memcmp(&old,&output,sizeof output));
    contacts=(PortableMeshCourseActorEvents){0};
    memset(&state,0,sizeof state); /* New generation does not invent prior expiry. */
    CHECK(step() && !output.count);
    ++view.ordinal;world.status.racers[0].lightning_ticks=1;
    CHECK(step() && !output.count);
    ++view.ordinal;world.status.racers[0].lightning_ticks=0;racers[0].phase=PORTABLE_MESH_RACER_FINISHED;
    CHECK(step() && !output.count);
    free(config);return 0;
}
static int falling_star_cues(void)
{
    fixture();
    PortableMeshCourseActorEvents contacts={.falling_star_hit_mask=1,.hit_mask=1};
    view.actor_events=&contacts;
    CHECK(step() && output.count==1 && output.cues[0].song==373 &&
          output.cues[0].player==PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER);
    const PortableMeshItemAudio accepted=state;
    CHECK(step() && !output.count && !memcmp(&state,&accepted,sizeof state));
    ++view.ordinal;contacts=(PortableMeshCourseActorEvents){.falling_star_protected_mask=3,.protected_mask=3};
    CHECK(step() && output.count==2 && output.cues[0].song==160 && output.cues[1].song==160);
    ++view.ordinal;contacts=(PortableMeshCourseActorEvents){.falling_star_hit_mask=4,.hit_mask=4};
    CHECK(step() && !output.count);
    const PortableMeshItemAudio saved=state;
    const PortableMeshItemAudioOutput before=output;
    ++view.ordinal;contacts.falling_star_hit_mask=8;
    CHECK(!step() && !memcmp(&saved,&state,sizeof state) && !memcmp(&before,&output,sizeof output));
    contacts=(PortableMeshCourseActorEvents){.falling_star_protected_mask=8};
    CHECK(!step() && !memcmp(&saved,&state,sizeof state) && !memcmp(&before,&output,sizeof output));
    return 0;
}
static int teepee_cues(void)
{
    fixture();
    PortableMeshCourseActorEvents contacts={.teepee_touch_mask=3};
    view.actor_events=&contacts;
    CHECK(step() && output.count==2);
    for(unsigned i=0;i<2;++i)
        CHECK(output.cues[i].song==371 && output.cues[i].volume==256 &&
              output.cues[i].player==PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER);
    const PortableMeshItemAudio accepted=state;
    CHECK(step() && !output.count && !memcmp(&state,&accepted,sizeof state));
    ++view.ordinal;contacts=(PortableMeshCourseActorEvents){.teepee_protected_mask=1,.teepee_release_mask=2};
    CHECK(step() && output.count==2 && output.cues[0].song==160 && output.cues[1].song==372);
    ++view.ordinal;contacts=(PortableMeshCourseActorEvents){.teepee_touch_mask=4,
        .teepee_protected_mask=4,.teepee_release_mask=4};
    CHECK(step() && !output.count); /* Only accepted human cues are audible. */
    const PortableMeshItemAudio saved=state;
    const PortableMeshItemAudioOutput before=output;
    for(unsigned kind=0;kind<3;++kind)
    {
        ++view.ordinal;memset(&contacts,0,sizeof contacts);
        if(kind==0)contacts.teepee_touch_mask=8;
        if(kind==1)contacts.teepee_protected_mask=8;
        if(kind==2)contacts.teepee_release_mask=8;
        CHECK(!step() && !memcmp(&state,&saved,sizeof state) && !memcmp(&output,&before,sizeof output));
    }
    contacts=(PortableMeshCourseActorEvents){.teepee_release_mask=1};
    CHECK(step() && output.count==1 && output.cues[0].song==372);
    return 0;
}
static int boo_contact_cues(void)
{
    fixture();
    PortableMeshCourseActorEvents contacts={.boo_contacts=1,.boo_touch_mask=3};
    view.actor_events=&contacts;
    CHECK(step() && output.count==2);
    for(unsigned i=0;i<2;++i)
        CHECK(output.cues[i].song==380 &&
              output.cues[i].player==PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER &&
              output.cues[i].volume==256);
    const PortableMeshItemAudio accepted=state;
    CHECK(step() && !output.count && !memcmp(&state,&accepted,sizeof state));
    ++view.ordinal;contacts.boo_touch_mask=4; /* CPU touches are silent. */
    CHECK(step() && !output.count);
    ++view.ordinal;contacts.boo_touch_mask=0;
    world.status.racers[0].star_ticks=5;
    CHECK(step() && !output.count && output.music.star_active);
    /* The source owner withholds the human touch cue during Star. A contact
     * alone never invents160 or a defeat/flight sound in this adapter. */
    const PortableMeshItemAudio saved=state;
    const PortableMeshItemAudioOutput before=output;
    ++view.ordinal;contacts.boo_touch_mask=9;
    CHECK(!step() && !memcmp(&state,&saved,sizeof state) &&
          !memcmp(&output,&before,sizeof output));
    contacts.boo_touch_mask=1;world.status.racers[0].star_ticks=0;
    CHECK(step() && output.count==1 && output.cues[0].song==380);
    return 0;
}
static int hud_and_rollback(void)
{
    fixture();
    world.inventory.racers[0].held_item = 1;
    world.inventory.racers[0].active = 1;
    CHECK(step() && output.count == 0);
    ++view.ordinal;
    world.inventory.racers[0].held_item = 0x1003;
    world.inventory.racers[0].active = 0;
    CHECK(step() && output.count == 1 && output.cues[0].song == 104);
    ++view.ordinal;
    CHECK(step() && output.count == 0); /* HUD clock, not repeated settled snapshot. */
    event(PORTABLE_MESH_EVENT_THEFT, 0x1000, 0, 1, 0);
    unsigned theft = 0, loss = 0;
    for (unsigned i = 0; i < 25; ++i)
    {
        ++view.ordinal;
        CHECK(step());
        events.count = 0;
        for (size_t j = 0; j < output.count; ++j)
        {
            theft += output.cues[j].song == 130;
            loss += output.cues[j].song == 131;
        }
    }
    CHECK(theft && loss);
    PortableMeshItemAudio saved = state;
    saved_output = output;
    view.ordinal = state.ordinal - 1;
    CHECK(!step() && !memcmp(&saved, &state, sizeof state) &&
          !memcmp(&saved_output, &output, sizeof output));
    view.ordinal = state.ordinal + 1;
    event(PORTABLE_MESH_EVENT_BOOST, 0x1007, 0, 255, 0);
    voices[7][0] = 0;
    CHECK(!step() && !memcmp(&saved, &state, sizeof state) &&
          !memcmp(&saved_output, &output, sizeof output));
    voices[7][0] = 314;
    CHECK(step());
    ++view.ordinal;
    events.count = PORTABLE_MESH_ITEMS_EVENTS + 1;
    saved = state;
    saved_output = output;
    CHECK(!step() && !memcmp(&saved, &state, sizeof state) &&
          !memcmp(&saved_output, &output, sizeof output));
    return 0;
}

static void write32(uint8_t *bytes, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        bytes[i] = (uint8_t)(value >> (i * 8));
}

static int imported_voices(void)
{
    uint8_t a[1502] = {0}, b[1502] = {0};
    write32(a, UINT32_C(0x3154414d));
    write32(a + 4, 256);
    for (unsigned i = 0; i < 16; ++i)
    {
        const unsigned value = 200 + i;
        a[1470 + i * 2] = (uint8_t)value;
        a[1471 + i * 2] = (uint8_t)(value >> 8);
    }
    memcpy(b, a, sizeof b);
    PortableAssetSectionView sections[2] = {{.kind = PORTABLE_ASSET_SECTION_MATERIAL_FLAGS,
                                             .content_id = 7,
                                             .version = 11,
                                             .bytes = a,
                                             .size = sizeof a},
                                            {.kind = PORTABLE_ASSET_SECTION_MATERIAL_FLAGS,
                                             .content_id = 100000,
                                             .version = 11,
                                             .bytes = b,
                                             .size = sizeof b}};
    PortableAssetPackView pack = {.section_count = 2, .resolved_sections = sections};
    uint16_t table[8][2], saved[8][2];
    CHECK(portable_recovered_mushroom_voices(&pack, table));
    CHECK(table[0][0] == 200 && table[7][1] == 215);
    memcpy(saved, table, sizeof saved);
    b[1500] ^= 1;
    CHECK(!portable_recovered_mushroom_voices(&pack, table) && !memcmp(table, saved, sizeof table));
    b[1500] ^= 1;
    sections[1].size--;
    CHECK(!portable_recovered_mushroom_voices(&pack, table) && !memcmp(table, saved, sizeof table));
    sections[1].size++;
    a[1470] = a[1471] = 0;
    CHECK(!portable_recovered_mushroom_voices(&pack, table) && !memcmp(table, saved, sizeof table));
    a[1470] = 200;
    a[1471] = 0;
    /* Both imported games composed over the base pack exceed 4096 sections. */
    enum { WIDE = 4200 };
    static PortableAssetSectionView wide[WIDE];
    for (unsigned i = 0; i < WIDE; ++i)
        wide[i] = (PortableAssetSectionView){.kind = PORTABLE_ASSET_SECTION_MATERIAL_FLAGS + 1u,
                                             .content_id = 0x10000u + i};
    wide[WIDE - 1] = sections[0];
    PortableAssetPackView composed = {.section_count = WIDE, .resolved_sections = wide};
    memset(table, 0, sizeof table);
    CHECK(portable_recovered_mushroom_voices(&composed, table) && !memcmp(table, saved, sizeof table));
    sections[0].version = sections[1].version = 10;
    sections[0].size = sections[1].size = 1470;
    CHECK(!portable_recovered_mushroom_voices(&pack, table) && !memcmp(table, saved, sizeof table));
    return 0;
}

static int maximum_events(void)
{
    fixture();
    for (unsigned i = 0; i < PORTABLE_MESH_ITEMS_EVENTS; ++i)
        event(PORTABLE_MESH_EVENT_BOOST, 0x1007, i % 2, 255, 0);
    CHECK(step() && output.count == PORTABLE_MESH_ITEMS_EVENTS * 2);
    CHECK(output.count <= PORTABLE_MESH_ITEM_AUDIO_CUES);
    return 0;
}

static int accepted_world_boundary(void)
{
    fixture();
    static PortableTestMeshCourse floor;
    test_mesh_course_init(&floor);
    test_mesh_word(floor.meta + 12, 31);
    uint8_t bytes[PORTABLE_MESH_ITEM_RULES_BYTES];
    test_mir_payload(bytes);
    const PortableAssetSectionView section = {
        .kind = 103, .content_id = 41, .version = 1, .bytes = bytes, .size = sizeof bytes};
    PortableMeshItemRules rules;
    CHECK(portable_mesh_item_rules_asset_open(&section, &rules));
    const PortableMeshItemsConfig config = {&floor.course, &rules, &floor.rules, 8, 3, 3, 0, 0, NULL};
    const int32_t forward[3] = {0, -1073741824, 0};
    for (unsigned i = 0; i < 3; ++i)
    {
        const int32_t position[3] = {(int32_t)i * 4 * 65536, 0, 65536};
        CHECK(portable_mesh_kart_init(&racers[i].kart, &floor.rules, position, forward) ==
              PORTABLE_MESH_OK);
        racers[i].phase = PORTABLE_MESH_RACER_RACING;
        racers[i].kart.grounded = 1;
    }
    CHECK(portable_mesh_items_init(&world, &config, 123, NULL, 0) == PORTABLE_MESH_ITEMS_OK);
    CHECK(world.prepared == 0 && step());
    PortableMeshItemControls controls[3] = {{0}};
    PortableMeshKartEffects effects[3];
    const uint8_t standings[3] = {0, 1, 2};
    CHECK(portable_mesh_items_prepare_tick(&world, &config, racers, controls, standings, effects,
                                           &events, NULL, 0) == PORTABLE_MESH_ITEMS_OK);
    CHECK(world.prepared == 1);
    ++view.ordinal;
    const PortableMeshItemAudio saved = state;
    saved_output = output;
    CHECK(!step() && !memcmp(&state, &saved, sizeof state) &&
          !memcmp(&output, &saved_output, sizeof output));
    CHECK(portable_mesh_items_resolve_after_move(&world, &config, racers, &events, NULL, 0) ==
          PORTABLE_MESH_ITEMS_OK);
    CHECK(world.prepared == 0 && step() && state.ordinal == view.ordinal);
    CHECK(step() && output.count == 0);
    return 0;
}

static int bomb_audio(void)
{
    fixture();
    /* A far attacker and recycled actor cannot move the detonation sound. */
    racers[2].kart.position_q16[0]=100*65536;
    event(PORTABLE_MESH_EVENT_BOMB_CONTACT,0x1002,2,1,0);
    events.values[0].actor=0;events.values[0].serial=12;
    world.actors[0].phase=PORTABLE_MESH_ACTOR_FLYING;
    world.actors[0].serial=13;
    world.actors[0].motion.position_q16[0]=100*65536;
    CHECK(step() && output.count==1 && output.cues[0].song==386 &&
          output.cues[0].player==PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER &&
          output.cues[0].volume==256);
    CHECK(step() && output.count==0); /* Same accepted ordinal is silent. */
    ++view.ordinal;
    racers[1].kart.position_q16[2]=32*65536;
    /* Only human0 listens, so the Bomb's height attenuates independently. */
    view.human_mask=world.inventory.human_mask=world.status.human_mask=1;
    CHECK(step() && output.count==1 && output.cues[0].volume>0 && output.cues[0].volume<256);
    const PortableMeshItemAudio before=state;
    saved_output=output;
    ++view.ordinal;events.values[0].kind=PORTABLE_MESH_EVENT_BOMB_CONTACT+1;
    CHECK(!step() && !memcmp(&before,&state,sizeof before) &&
          !memcmp(&saved_output,&output,sizeof output));
    events.values[0].kind=PORTABLE_MESH_EVENT_BOMB_CONTACT;
    events.values[0].target=3;
    CHECK(!step() && !memcmp(&before,&state,sizeof before));
    events.values[0].target=UINT8_MAX;
    CHECK(!step() && !memcmp(&before,&state,sizeof before));
    events.values[0].target=1;
    CHECK(step() && output.count==1 && output.cues[0].song==386);
    return 0;
}
int main(void)
{
    CHECK(!bomb_audio() && !cues_and_music() && !actor_sounds() && !source_crab_contact_cues() && !penguin_contact_cues() && !mouse_contact_cues() && !piranha_contact_cues() && !seagull_contact_cues() && !lava_contact_cues() && !thwomp_contact_cues() && !cannonball_cues() && !rock_cues() && !coin_pickup_cues() && !teepee_cues() && !falling_star_cues() && !thundercloud_cues() && !common_prop_cues() && !mechakoopa_cues() && !kamek_cues() && !pier_boo_cues() && !boo_contact_cues() && !hud_and_rollback() && !imported_voices() &&
          !maximum_events() && !accepted_world_boundary());
    puts("Mesh item audio: PASS (accepted cues, HUD clocks, XYZ, music, rollback)");
    return 0;
}
