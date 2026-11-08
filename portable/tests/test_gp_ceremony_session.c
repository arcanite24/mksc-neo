#include "character_audio_fixture.h"
#include "gp_ceremony_session.h"

static const PortableCeremonyMotionRules audio_motion={.trophy_delay={103u,137u,73u}};

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr,"GP ceremony session failed line %d: %s\n", \
            __LINE__,#condition); exit(1); \
} } while (0)

static void source_presentation_owner(void)
{
    PortableCeremonyMotionRules rules={0};PortableCeremonyWorldRules world={0};
    int32_t sine[1024];for(unsigned i=0;i<1024u;++i)sine[i]=32768;
    rules.frame_count=1u;rules.frames[0]=(PortableCeremonySequenceFrame){43u,5u};
    rules.trophy_delay[0]=3u;rules.trophy_delay[1]=5u;rules.trophy_delay[2]=2u;
    for(unsigned i=0;i<8u;++i){rules.tracks[i]=(PortableCeremonyTrack){.count=1u,.initial_remaining=5u};
        world.grid[i]=(PortableCeremonyWorldPoint){(int32_t)i*65536,(int32_t)i*2*65536,0};}
    for(unsigned i=0;i<3u;++i)world.podium_offset[i]=(PortableCeremonyWorldPoint){(int32_t)i*65536,0,65536};
    for(unsigned rank=0;rank<8u;++rank){
        PortableGpCeremonyPresentation view;
        PortableGpCeremonySession session={.player_standing_rank=(uint8_t)rank,
            .podium_driver_classes={0u,1u,2u},.selected_driver_class=3u};
        CHECK(portable_gp_ceremony_presentation_init(&view,120u,180u,32u));
        CHECK(portable_gp_ceremony_source_init(&view,&session,&rules,&world));
        CHECK(view.motion.world.count==(rank<3u?3u:4u)&&view.road_line==153&&view.camera_vertical_offset==80);
        CHECK(!view.motion.world.drivers[0].visible&&!view.motion.zoom.visible&&!view.motion.trophy.visible);
        unsigned ticks=0u;
        while(view.phase!=PORTABLE_CEREMONY_WAIT&&ticks<5000u){
            CHECK(portable_gp_ceremony_source_step(&view,&session,&rules,&world,sine,1024u,NULL,NULL,NULL,1));
            ++ticks;
            if(rank<3u&&view.source_tick==980u)CHECK(view.motion.flypast.tick==170);
            if(rank<3u&&view.source_tick==1180u+rules.trophy_delay[rank])CHECK(view.motion.zoom.tick==9);
            if(view.camera_return_tick==1u){
                CHECK(view.driver_script[0]==9u&&view.motion.world.drivers[0].position.z_q16>0&&
                    view.motion.world.drivers[0].position.z_q16<=8*65536);
                CHECK(view.road_line==73+view.camera_vertical_offset);
            }
            if(view.camera_return_tick==33u&&view.source_phase<5u)CHECK(view.road_line==73);
        }
        CHECK(ticks<5000u&&ticks>500u&&view.source_phase==14u&&view.stats_reveal==16u);
        CHECK(view.arrival_completed>0u&&view.motion.world.drivers[0].visible&&view.road_line==73);
        CHECK(view.header_active&&view.zoom_stage==3u);
        const PortableGpCeremonyPresentation snapshot=view;
        CHECK(portable_gp_ceremony_source_step(&view,&session,&rules,&world,sine,1024u,NULL,NULL,NULL,0));
        CHECK(view.source_phase==14u&&view.phase==PORTABLE_CEREMONY_WAIT);
        CHECK(portable_gp_ceremony_source_step(&view,&session,&rules,&world,sine,1024u,NULL,NULL,NULL,1));
        CHECK(view.phase==PORTABLE_CEREMONY_FADE);
        for(unsigned i=0;i<32u;++i)CHECK(portable_gp_ceremony_source_step(&view,&session,&rules,&world,sine,1024u,NULL,NULL,NULL,0));
        CHECK(view.phase==PORTABLE_CEREMONY_FINISHED);
        CHECK(snapshot.phase==PORTABLE_CEREMONY_WAIT&&snapshot.source_phase==14u);
    }
    PortableGpCeremonyPresentation fast;PortableGpCeremonySession session={0};
    CHECK(portable_gp_ceremony_presentation_init(&fast,120u,180u,32u));
    CHECK(portable_gp_ceremony_source_init(&fast,&session,&rules,&world));
    CHECK(portable_gp_ceremony_presentation_skip(&fast));
    CHECK(portable_gp_ceremony_source_step(&fast,&session,&rules,&world,sine,1024u,NULL,NULL,NULL,0));
    CHECK(fast.source_phase==14u&&fast.stats_reveal==16u&&fast.motion.world.drivers[0].visible);
}

static void coin_transfer_owner(void)
{
    PortableCeremonyMotionRules rules={0};PortableCeremonyWorldRules world={0};
    int32_t sine[1024];for(unsigned i=0;i<1024u;++i)sine[i]=(int32_t)i*128;
    rules.frame_count=1u;rules.frames[0]=(PortableCeremonySequenceFrame){43u,5u};
    for(unsigned i=0;i<8u;++i){rules.tracks[i].count=1u;rules.tracks[i].initial_remaining=5u;}
    PortableGpCeremonySession session={.finishing_coins={7u,19u,0u,83u},
        .finishing_coin_total=109u,.extra_cup_unlocked=1u};
    PortableGpCeremonyPresentation v;
    CHECK(portable_gp_ceremony_presentation_init(&v,120u,180u,32u));
    CHECK(portable_gp_ceremony_source_init(&v,&session,&rules,&world));
    v.source_phase=14u;v.phase=PORTABLE_CEREMONY_WAIT;v.stats_reveal=16u;
    for(unsigned t=0;t<=344u;++t){
        const PortableGpCeremonyPresentation before=v;
        CHECK(portable_gp_ceremony_source_step(&v,&session,&rules,&world,sine,1024u,NULL,NULL,NULL,0));
        CHECK(before.points_ticks==t&&v.points_ticks==t+1u);
        if(t<120u)CHECK(v.coin_total==0u&&v.coins[0].y==104);
        if(t==120u)CHECK(v.coins[0].tick==1u&&v.coins[0].y==102&&v.coins[1].y==120);
        if(t==131u)CHECK(v.coins[0].y==80&&v.coins[0].phase==0u);
        if(t==132u)CHECK(v.coins[0].phase==1u&&v.coins[0].tick==0u);
        if(t==139u)CHECK(v.coins[0].phase==2u&&v.coin_total==0u);
        if(t==140u)CHECK(v.coin_total==7u&&v.coins[0].value==-1&&v.coins[1].tick==0u);
        if(t==141u)CHECK(v.coins[1].tick==1u);
        if(t==161u)CHECK(v.coin_total==26u&&v.coins[1].value==-1);
        if(t==182u)CHECK(v.coin_total==26u&&v.coins[2].value==-1);
        if(t==203u)CHECK(v.coin_total==109u&&v.coins[3].value==-1&&v.coin_stage==1u);
        if(t==204u)CHECK(v.coin_stage==2u);
        if(t==325u)CHECK(v.unlock_stage==2u&&v.unlock_reveal==0u);
        if(t==326u)CHECK(v.unlock_reveal==1u);
        if(t==341u)CHECK(v.unlock_reveal==16u);
        if(t==343u)CHECK(v.unlock_stage==4u);
    }
    CHECK(v.coin_total==session.finishing_coin_total);
    PortableGpCeremonyPresentation fast;
    CHECK(portable_gp_ceremony_presentation_init(&fast,120u,180u,32u));
    CHECK(portable_gp_ceremony_source_init(&fast,&session,&rules,&world));
    CHECK(portable_gp_ceremony_presentation_skip(&fast));
    CHECK(portable_gp_ceremony_source_step(&fast,&session,&rules,&world,sine,1024u,NULL,NULL,NULL,0));
    CHECK(fast.coin_total==109u&&fast.unlock_reveal==16u&&fast.unlock_stage==4u);
    for(unsigned i=0;i<4u;++i)CHECK(fast.coins[i].value==-1);
}

int main(void)
{
    const PortableCharacterAudioRules character_rules=character_audio_fixture();
    source_presentation_owner();
    coin_transfer_owner();
    PortableGpCeremonyPresentation view;
    CHECK(!portable_gp_ceremony_presentation_init(&view,0,180,32));
    CHECK(portable_gp_ceremony_presentation_init(&view,120,180,32));
    for(unsigned tick=0;tick<300u;++tick)
        portable_gp_ceremony_presentation_tick(&view,1);
    CHECK(view.phase==PORTABLE_CEREMONY_WAIT&&view.tick==0u);
    for(unsigned tick=0;tick<10000u;++tick)
        portable_gp_ceremony_presentation_tick(&view,0);
    CHECK(view.phase==PORTABLE_CEREMONY_WAIT);
    portable_gp_ceremony_presentation_tick(&view,1);
    CHECK(view.phase==PORTABLE_CEREMONY_FADE&&view.tick==0u);
    for(unsigned tick=0;tick<32u;++tick)
        portable_gp_ceremony_presentation_tick(&view,0);
    CHECK(view.phase==PORTABLE_CEREMONY_FINISHED);
    portable_gp_ceremony_presentation_tick(&view,1);
    CHECK(view.phase==PORTABLE_CEREMONY_FINISHED);
    /* Fast menus skip only the entrance/celebration, never the wait or fade. */
    CHECK(!portable_gp_ceremony_presentation_skip(&view));
    CHECK(portable_gp_ceremony_presentation_init(&view,120,180,32));
    portable_gp_ceremony_presentation_tick(&view,1);
    CHECK(view.phase==PORTABLE_CEREMONY_ENTRANCE&&view.tick==1u);
    CHECK(portable_gp_ceremony_presentation_skip(&view)&&view.phase==PORTABLE_CEREMONY_WAIT&&view.tick==0u);
    CHECK(!portable_gp_ceremony_presentation_skip(&view));
    portable_gp_ceremony_presentation_tick(&view,1);
    CHECK(view.phase==PORTABLE_CEREMONY_FADE);
    CHECK(!portable_gp_ceremony_presentation_skip(&view)&&view.phase==PORTABLE_CEREMONY_FADE);
    static const uint8_t order[PORTABLE_GP_RACER_COUNT] =
        {3u,0u,7u,2u,1u,4u,5u,6u};
    static const uint8_t classes[PORTABLE_GP_RACER_COUNT] =
        {7u,2u,4u,5u,0u,6u,1u,3u};
    PortableGpCupSession cup = {0};
    PortableGpCeremonySession session;
    PortableGpCeremonySession unchanged;

    cup.race_mode_flags = UINT16_C(0x0100);
    cup.cup_index = 4u;
    cup.track_set = 1u;
    cup.engine_class = 2u;
    cup.course_in_cup = 3u;
    cup.course_header_index = 7u;
    cup.selected_driver_class = 7u;
    cup.next_mode = 7u;
    cup.stage = PORTABLE_GP_CUP_STAGE_STANDINGS;
    cup.cumulative_points[0] = 28u;
    memcpy(cup.standings_order, order, sizeof order);
    memcpy(cup.racer_driver_classes, classes, sizeof classes);

    cup.driving_quality_sample_mask = 15u;
    cup.driving_quality.average_score = 200;
    memcpy(cup.finishing_coins,(uint8_t[]){25u,24u,26u,25u},4u);
    CHECK(portable_gp_ceremony_session_init(&session,&cup) ==
          PORTABLE_GP_CEREMONY_OK);
    CHECK(session.track_set == 1u && session.cup_index == 4u && session.engine_class == 2u && session.course_header_index == 7u &&
          session.selected_driver_class == 7u && session.finishing_coin_total==100u &&
          session.player_standing_rank == 1u &&
          session.podium_driver_classes[0] == classes[3] &&
          session.podium_driver_classes[1] == classes[0] &&
          session.podium_driver_classes[2] == classes[7] &&
          session.performance_rating == 2u && session.driving_score == 200 &&
          session.result_song_id == UINT16_C(0x38) &&
          session.state5_next_state == 6u &&
          session.save_evaluation_pending &&
          session.unlock_evaluation_pending);
    {
        /* Live contact-heavy races can legitimately saturate the worst rating;
         * a seeded cup standing does not fix the final driving score. */
        PortableGpCupSession rough = cup;
        PortableGpCeremonySession rough_session;
        rough.cumulative_points[0] = 24u;
        rough.driving_quality.average_score = -660;
        CHECK(portable_gp_ceremony_session_init(&rough_session,&rough) ==
              PORTABLE_GP_CEREMONY_OK && rough_session.performance_rating == 7u);
    }
    {
        /* Custom cups reach the ceremony; arenas never do. */
        PortableGpCupSession custom = cup;
        PortableGpCeremonySession custom_session;
        custom.track_set = 3u;
        CHECK(portable_gp_ceremony_session_init(&custom_session,&custom) ==
              PORTABLE_GP_CEREMONY_OK && custom_session.track_set == 3u);
        custom.track_set = 2u;
        CHECK(portable_gp_ceremony_session_init(&custom_session,&custom) ==
              PORTABLE_GP_CEREMONY_UNSUPPORTED_STATE);
        custom.track_set = 4u;
        CHECK(portable_gp_ceremony_session_init(&custom_session,&custom) ==
              PORTABLE_GP_CEREMONY_UNSUPPORTED_STATE);
    }
    CHECK(portable_gp_ceremony_song_at(&session,0u) == 55u &&
          portable_gp_ceremony_song_at(&session,729u) == 0u &&
          portable_gp_ceremony_song_at(&session,730u) == 0x38u &&
          portable_gp_ceremony_song_at(&session,1338u) == 0u);
    {
        /* Rank 1 (second place): C=1301; authored class7 voice247. */
        uint16_t found[64],cues[3];size_t total=0u;uint32_t at[64];
        session.extra_cup_unlocked=1u;
        for(uint32_t tick=0u;tick<2000u;++tick){
            const size_t count=portable_gp_ceremony_cues_at(&session,&audio_motion,&character_rules,tick,cues);
            for(size_t i=0u;i<count&&total<64u;++i){found[total]=cues[i];at[total++]=tick;}
        }
        static const uint16_t songs[]={174,175,175,175,156,156,156,120,120,120,120,120,120,120,
                                       109,247,101,101,101,101,110};
        static const uint32_t ticks[]={564,667,683,691,720,730,740,829,857,885,913,941,969,997,
                                       1308,1432,1448,1469,1490,1511,1633};
        CHECK(total==sizeof songs/sizeof songs[0]);
        for(size_t i=0u;i<total;++i)CHECK(found[i]==songs[i]&&at[i]==ticks[i]);
        /* A roster driver's voice driver speaks instead (authored class1 voice241). */
        session.voice_override=2u;
        CHECK(portable_gp_ceremony_cues_at(&session,&audio_motion,&character_rules,1432u,cues)==1u&&cues[0]==241u);
        session.voice_override=0u;
        CHECK(portable_gp_ceremony_input_tick(&session,&audio_motion)==1654u);
        session.extra_cup_unlocked=0u;
    }

    cup.standings_order[1] = 7u;
    cup.standings_order[2] = 2u;
    cup.standings_order[3] = 0u;
    cup.driving_quality.average_score = -121;
    CHECK(portable_gp_ceremony_session_init(&session,&cup) ==
          PORTABLE_GP_CEREMONY_OK &&
          session.player_standing_rank == 3u &&
          session.performance_rating == 6u &&
          session.result_song_id == UINT16_C(0x3b));
    {
        uint16_t cues[3];size_t total=0u;
        for(uint32_t tick=0u;tick<2000u;++tick)
            total+=portable_gp_ceremony_cues_at(&session,&audio_motion,&character_rules,tick,cues);
        CHECK(total==8u&&portable_gp_ceremony_cues_at(&session,&audio_motion,&character_rules,807u,cues)==1u&&cues[0]==174u&&
              portable_gp_ceremony_cues_at(&session,&audio_motion,&character_rules,1094u,cues)==1u&&cues[0]==101u&&
              portable_gp_ceremony_input_tick(&session,&audio_motion)==1300u);
    }
    CHECK(session.state5_next_state == 11u);
    CHECK(portable_gp_ceremony_song_at(&session,0u) == 57u &&
          portable_gp_ceremony_song_at(&session,730u) == 0u &&
          portable_gp_ceremony_song_at(&session,1338u) == 0x3bu);

    memset(&unchanged,0xa5,sizeof unchanged);
    session=unchanged;
    cup.driving_quality_sample_mask = 7u;
    CHECK(portable_gp_ceremony_session_init(&session,&cup) ==
              PORTABLE_GP_CEREMONY_UNSUPPORTED_STATE &&
          memcmp(&session,&unchanged,sizeof session)==0);
    cup.driving_quality_sample_mask = 15u;
    cup.finishing_coins[2]=100u;
    CHECK(portable_gp_ceremony_session_init(&session,&cup)==PORTABLE_GP_CEREMONY_UNSUPPORTED_STATE);
    cup.finishing_coins[2]=26u;
    cup.next_mode=8u;
    CHECK(portable_gp_ceremony_session_init(&session,&cup) ==
              PORTABLE_GP_CEREMONY_UNSUPPORTED_STATE &&
          memcmp(&session,&unchanged,sizeof session)==0);
    cup.next_mode=7u;
    cup.standings_order[0]=cup.standings_order[1];
    CHECK(portable_gp_ceremony_session_init(&session,&cup) ==
          PORTABLE_GP_CEREMONY_UNSUPPORTED_STATE);
    CHECK(portable_gp_ceremony_session_init(NULL,&cup) ==
          PORTABLE_GP_CEREMONY_INVALID_ARGUMENT);
    puts("portable GP ceremony session: PASS");
    return 0;
}
