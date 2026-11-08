#include "gp_cup_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "GP cup session failed line %d: %s\n", __LINE__, #condition); \
    exit(1); \
} } while (0)

static void prepare_race(PortableGpRaceSession *race, uint32_t course_id,
                         const uint8_t order[PORTABLE_GP_RACER_COUNT])
{
    memset(race, 0, sizeof *race);
    static PortableGpScoringRules scoring={
        .finish_points={9u,6u,3u,1u,0u,0u,0u,0u},
        .initial_retry_credits=3u,.has_quality_rules=1u,
        .quality={.rank_bonus={[2]={[0]=60}},.speed_target={167}}};
    race->scoring_rules=&scoring;
    race->retry_credits=3u;
    race->roster.racers[0].coin_count_178=0u;
    race->runtime.session.phase = PORTABLE_RACE_PHASE_FINISHED;
    race->runtime.session.lap_count = 3u;
    race->runtime.session.laps_completed = 3u;
    race->runtime.session.elapsed_ticks = 7000u;
    race->source_result_clock.finish_centiseconds=11666u;
    race->rules.route_count = 47u;
    race->roster.course_id = course_id;
    race->roster.race_mode_flags_7da = UINT16_C(0x4100);
    race->roster.race_phase_7d4 = 5u;
    for (size_t index = 0u; index < PORTABLE_GP_RACER_COUNT; ++index)
    {
        race->roster.racers[index].driver_class = (uint8_t)((index + 7u) & 7u);
        race->roster.standings[index] = order[index];
    }
}

int main(void)
{
    static const uint8_t first_order[8] = {7u, 5u, 3u, 1u, 0u, 2u, 4u, 6u};
    static const uint8_t second_order[8] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u};
    static const uint8_t first_scores[8] = {0u, 1u, 0u, 3u, 0u, 6u, 0u, 9u};
    static const uint8_t first_standings[8] = {7u, 5u, 3u, 1u, 0u, 2u, 6u, 4u};
    static const uint8_t second_scores[8] = {9u, 7u, 3u, 4u, 0u, 6u, 0u, 9u};
    static const uint8_t second_standings[8] = {0u, 7u, 1u, 5u, 3u, 2u, 6u, 4u};
    const PortableGpCupDefinition definition={{24u,25u,29u,27u},{4u,5u,9u,7u},0u,0u};
    PortableGpRaceSession race;
    PortableGpCupSession cup;
    PortableGpCupSession next;
    PortableGpCupSession second;
    PortableGpCupSession unchanged;

    {
        PortableGpScoringRules rules={.finish_points={63u,20u,10u,4u,3u,2u,1u,0u},
            .initial_retry_credits=3u,.has_quality_rules=1u};
        prepare_race(&race,24u,second_order);race.scoring_rules=&rules;
        CHECK(portable_gp_cup_session_begin_standings(&race,NULL,0u,0u,4u,0u,&cup)==PORTABLE_GP_CUP_SESSION_OK);
        for(unsigned rank=0u;rank<8u;++rank)
            CHECK(cup.rows[rank].points_awarded==rules.finish_points[rank]&&cup.cumulative_points[rank]==rules.finish_points[rank]);
        PortableGpCupSession progress={0};
        for(uint8_t track=0u;track<4u;++track){
            prepare_race(&race,definition.course_ids[track],second_order);race.scoring_rules=&rules;
            CHECK(portable_gp_cup_session_begin_standings(&race,track?&progress:NULL,0u,track,definition.source_headers[track],0u,&cup)==PORTABLE_GP_CUP_SESSION_OK);
            CHECK(cup.cumulative_points[0]==63u*(track+1u));
            if(track<3u)CHECK(portable_gp_cup_session_advance(&cup,&definition,&progress)==PORTABLE_GP_CUP_SESSION_OK);
        }
        prepare_race(&race,24u,second_order);race.scoring_rules=&rules;
        rules.finish_points[0]=64u;
        CHECK(portable_gp_cup_session_begin_standings(&race,NULL,0u,0u,4u,0u,&cup)==PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE);
    }
    memset(&unchanged, 0xa5, sizeof unchanged);
    prepare_race(&race, 24u, first_order);

    CHECK(portable_gp_cup_session_finish_route(NULL) ==
          PORTABLE_GP_CUP_FINISH_NOT_READY);
    CHECK(portable_gp_cup_session_finish_route(&race) ==
          PORTABLE_GP_CUP_FINISH_NOT_READY);
    race.roster.racers[0].source_flags = UINT32_C(0x20);
    for (uint16_t rank = 0u; rank < PORTABLE_GP_RACER_COUNT; ++rank)
    {
        race.roster.racers[0].rank_flags_ba = (uint16_t)(rank << 1u);
        CHECK(portable_gp_cup_session_finish_route(&race) ==
              (rank <= 3u ? PORTABLE_GP_CUP_FINISH_STANDINGS :
                            PORTABLE_GP_CUP_FINISH_RETRY_MENU));
    }
    race.runtime.session.laps_completed = 2u;
    CHECK(portable_gp_cup_session_finish_route(&race) ==
          PORTABLE_GP_CUP_FINISH_NOT_READY);
    race.runtime.session.laps_completed = 3u;
    race.marker_clock=97531u;
    race.marker_display_clock=97530u;

    CHECK(portable_gp_cup_session_begin_standings(
              &race, NULL, 0u, 0u, 4u,0u, &cup) == PORTABLE_GP_CUP_SESSION_OK);
    CHECK(cup.stage == PORTABLE_GP_CUP_STAGE_STANDINGS &&
          cup.next_mode == 8u && cup.race_mode_flags == UINT16_C(0x4100));
    CHECK(cup.course_id == 24u && cup.course_header_index == 4u &&
          cup.cup_index == 0u && cup.course_in_cup == 0u);
    CHECK(cup.selected_driver_class == 7u);
    CHECK(cup.marker_clock==97531u&&cup.marker_display_clock==97530u);
    CHECK(cup.course_ids[0]==24u&&cup.race_ticks[0]==7000u&&cup.race_centiseconds[0]==11666u);
    CHECK(cup.finishing_ranks[0]==cup.rows[0].finish_rank&&
          cup.finishing_points[0]==cup.rows[0].points_awarded);
    for(unsigned racer=0u;racer<8u;++racer)
        if(cup.cumulative_points[racer]==0u)CHECK(cup.rows[racer].standing_rank==4u);
    race.roster.race_mode_flags_7da = UINT16_C(0x2200);
    CHECK(portable_gp_cup_session_begin_standings(
              &race, NULL, 0u, 0u, 4u,0u, &cup) == PORTABLE_GP_CUP_SESSION_OK &&
          cup.race_mode_flags == UINT16_C(0x2200));
    race.roster.race_mode_flags_7da = UINT16_C(0x4100);
    CHECK(portable_gp_cup_session_begin_standings(
              &race, NULL, 0u, 0u, 4u,0u, &cup) == PORTABLE_GP_CUP_SESSION_OK);
    race.roster.racers[0].coin_count_178=99u;
    CHECK(portable_gp_cup_session_begin_standings(&race,NULL,0u,0u,4u,0u,&second)==
        PORTABLE_GP_CUP_SESSION_OK&&second.finishing_coins[0]==99u);
    race.roster.racers[0].coin_count_178=100u;
    CHECK(portable_gp_cup_session_begin_standings(&race,NULL,0u,0u,4u,0u,&second)==
        PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE);
    race.roster.racers[0].coin_count_178=0u;
    CHECK(cup.driving_quality_sample_mask == 1u &&
          cup.driving_quality.samples[0] == 106);
    CHECK(portable_gp_cup_session_begin_standings(&race,NULL,0u,0u,32u,1u,&second)==
        PORTABLE_GP_CUP_SESSION_OK&&second.track_set==1u&&
        second.driving_quality.samples[0]==206);
    /* Custom cups score like GBA courses (no retro bonus); arenas have no GP. */
    CHECK(portable_gp_cup_session_begin_standings(&race,NULL,0u,0u,200u,3u,&second)==
        PORTABLE_GP_CUP_SESSION_OK&&second.track_set==3u&&
        second.driving_quality.samples[0]==106);
    CHECK(portable_gp_cup_session_begin_standings(&race,NULL,0u,0u,44u,2u,&second)==
        PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE);
    CHECK(portable_gp_cup_session_begin_standings(&race,NULL,0u,0u,4u,4u,&second)==
        PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE);
    for(uint8_t credits=0u;credits<3u;++credits){
        PortableGpCupSession penalized;
        race.retry_credits=credits;
        CHECK(portable_gp_cup_session_begin_standings(&race,NULL,0u,0u,4u,0u,
            &penalized)==PORTABLE_GP_CUP_SESSION_OK);
        CHECK(penalized.retry_credits==credits &&
              penalized.driving_quality.samples[0]==106-(3-credits)*120);
    }
    race.retry_credits=3u;
    CHECK(memcmp(cup.race_order, first_order, sizeof first_order) == 0);
    CHECK(memcmp(cup.cumulative_points, first_scores, sizeof first_scores) == 0);
    CHECK(memcmp(cup.standings_order, first_standings,
                 sizeof first_standings) == 0);
    for (size_t rank = 0u; rank < PORTABLE_GP_RACER_COUNT; ++rank)
    {
        const uint8_t racer = first_order[rank];
        const uint8_t expected_points[8] = {9u, 6u, 3u, 1u, 0u, 0u, 0u, 0u};
        CHECK(cup.rows[racer].racer_index == racer &&
              cup.rows[racer].driver_class == race.roster.racers[racer].driver_class &&
              cup.rows[racer].finish_rank == rank &&
              cup.rows[racer].points_before == 0u &&
              cup.rows[racer].points_awarded == expected_points[rank] &&
              cup.rows[racer].points_after == first_scores[racer]);
    }

    CHECK(portable_gp_cup_session_advance(&cup, &definition, &next) ==
          PORTABLE_GP_CUP_SESSION_OK);
    CHECK(next.stage == PORTABLE_GP_CUP_STAGE_NEXT_RACE &&
          next.next_mode == 8u && next.course_in_cup == 1u &&
          next.course_header_index == 5u && next.course_id == 25u &&
          next.race_mode_flags == UINT16_C(0x4100));
    CHECK(memcmp(next.cumulative_points, first_scores, sizeof first_scores) == 0);
    CHECK(next.marker_clock==97531u&&next.marker_display_clock==97530u);

    PortableGpRaceSession resumed={0};
    resumed.runtime.session.phase=PORTABLE_RACE_PHASE_COUNTDOWN;
    resumed.runtime.session.countdown_ticks=180u;
    resumed.runtime.session.countdown_remaining=180u;
    resumed.roster.course_id=next.course_id;
    for(unsigned racer=0u;racer<8u;++racer)
        resumed.roster.racers[racer].driver_class=next.racer_driver_classes[racer];
    next.item_art_clock.calls=UINT32_MAX;
    next.marker_clock=12345u;
    next.marker_display_clock=12344u;
    next.terrain_effect_counter=3u;next.terrain_effect_phase=1u;
    resumed.source_rng_state=1234u;resumed.source_frame_clock=999u;
    CHECK(portable_gp_cup_session_resume_race(&next,&resumed)==PORTABLE_GP_CUP_SESSION_OK);
    CHECK(resumed.source_rng_state==0u&&resumed.source_frame_clock==0u&&
          resumed.marker_clock==12345u&&resumed.marker_display_clock==12344u&&
          resumed.item_art_clock.calls==UINT32_MAX&&resumed.surface_trails.eligible_counter==3u&&
          resumed.surface_trails.side==1u&&resumed.runtime.session.countdown_remaining==180u);
    CHECK(next.retry_credits==3u&&resumed.retry_credits==3u);
    for(uint8_t credits=0u;credits<3u;++credits){
        next.retry_credits=credits;
        CHECK(portable_gp_cup_session_resume_race(&next,&resumed)==PORTABLE_GP_CUP_SESSION_OK&&
              resumed.retry_credits==credits);
    }
    next.retry_credits=4u;
    CHECK(portable_gp_cup_session_resume_race(&next,&resumed)==PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE);
    next.retry_credits=3u;
    next.item_art_clock.pending=1u;
    CHECK(portable_gp_cup_session_resume_race(&next,&resumed)==PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE);
    next.item_art_clock.pending=0u;resumed.engine_class=1u;
    CHECK(portable_gp_cup_session_resume_race(&next,&resumed)==PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE);
    resumed.engine_class=0u;resumed.runtime.session.countdown_remaining=179u;
    CHECK(portable_gp_cup_session_resume_race(&next,&resumed)==PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE);
    CHECK(portable_gp_cup_session_resume_race(NULL,&resumed)==PORTABLE_GP_CUP_SESSION_INVALID_ARGUMENT);
    next.terrain_effect_counter=0u;next.terrain_effect_phase=0u;next.item_art_clock.calls=0u;
    next.finishing_coins[0]=37u;
    next.course_ids[0]=24u;next.race_ticks[0]=6500u;next.race_centiseconds[0]=10833u;
    next.finishing_ranks[0]=2u;next.finishing_points[0]=3u;
    prepare_race(&race, 25u, second_order);
    race.roster.racers[0].coin_count_178=26u;
    CHECK(portable_gp_cup_session_begin_standings(
              &race, &next, 0u, 1u, 5u,0u, &second) ==
          PORTABLE_GP_CUP_SESSION_OK);
    CHECK(second.finishing_coins[0]==37u&&second.finishing_coins[1]==26u);
    CHECK(second.finishing_ranks[0]==2u&&second.finishing_points[0]==3u&&
          second.finishing_ranks[1]==second.rows[0].finish_rank&&
          second.finishing_points[1]==second.rows[0].points_awarded);
    CHECK(second.course_ids[0]==24u&&second.course_ids[1]==25u&&
          second.race_ticks[0]==6500u&&second.race_ticks[1]==7000u&&
          second.race_centiseconds[0]==10833u&&second.race_centiseconds[1]==11666u);
    race.roster.racers[0].coin_count_178=0u;
    CHECK(portable_gp_cup_session_begin_standings(&race,&next,0u,1u,5u,0u,&second)==
        PORTABLE_GP_CUP_SESSION_OK);
    CHECK(memcmp(second.cumulative_points, second_scores,
                 sizeof second_scores) == 0);
    CHECK(memcmp(second.standings_order, second_standings,
                 sizeof second_standings) == 0);
    CHECK(second.rows[0].points_before == 0u &&
          second.rows[0].points_awarded == 9u &&
          second.rows[0].points_after == 9u &&
          second.rows[0].standing_rank == 0u);
    CHECK(second.rows[7].points_before == 9u &&
          second.rows[7].points_awarded == 0u &&
          second.rows[7].points_after == 9u &&
          second.rows[7].standing_rank == 0u);
    CHECK(second.driving_quality_sample_mask == 3u &&
          second.driving_quality.samples[0] == 106 &&
          second.driving_quality.samples[1] == 166);

    CHECK(portable_gp_cup_session_advance(&second, &definition, &cup) ==
          PORTABLE_GP_CUP_SESSION_OK && cup.course_header_index == 9u &&
          cup.course_id == 29u);
    cup.stage = PORTABLE_GP_CUP_STAGE_STANDINGS;
    CHECK(portable_gp_cup_session_advance(&cup, &definition, &next) ==
          PORTABLE_GP_CUP_SESSION_OK && next.course_header_index == 7u &&
          next.course_id == 27u);
    prepare_race(&race, 27u, second_order);
    CHECK(portable_gp_cup_session_begin_standings(
              &race, &next, 0u, 3u, 7u,0u, &second) ==
          PORTABLE_GP_CUP_SESSION_OK && second.next_mode == 7u &&
          second.stage == PORTABLE_GP_CUP_STAGE_STANDINGS);
    next.stage = PORTABLE_GP_CUP_STAGE_STANDINGS;
    cup = unchanged;
    CHECK(portable_gp_cup_session_advance(&next, &definition, &cup) ==
          PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE &&
          memcmp(&cup, &unchanged, sizeof cup) == 0);

    prepare_race(&race, 24u, first_order);
    cup = unchanged;
    race.runtime.session.phase = PORTABLE_RACE_PHASE_RACING;
    CHECK(portable_gp_cup_session_begin_standings(
              &race, NULL, 0u, 0u, 4u,0u, &cup) ==
              PORTABLE_GP_CUP_SESSION_RACE_NOT_READY &&
          memcmp(&cup, &unchanged, sizeof cup) == 0);
    race.runtime.session.phase = PORTABLE_RACE_PHASE_FINISHED;
    race.roster.race_mode_flags_7da = UINT16_C(0x0200);
    CHECK(portable_gp_cup_session_begin_standings(
              &race, NULL, 0u, 0u, 4u,0u, &cup) ==
              PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE);
    race.roster.race_mode_flags_7da = UINT16_C(0x0100);
    race.roster.standings[7] = race.roster.standings[6];
    CHECK(portable_gp_cup_session_begin_standings(
              &race, NULL, 0u, 0u, 4u,0u, &cup) ==
              PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE);
    CHECK(portable_gp_cup_session_begin_standings(
              NULL, NULL, 0u, 0u, 4u,0u, &cup) ==
              PORTABLE_GP_CUP_SESSION_INVALID_ARGUMENT);
    CHECK(portable_gp_cup_session_advance(NULL, &definition, &cup) ==
          PORTABLE_GP_CUP_SESSION_INVALID_ARGUMENT);

    /* Manifest IDs deliberately have no arithmetic relationship to source
     * headers. All cups/classes preserve roster, points and engine profile. */
    for(uint8_t track_set=0u;track_set<4u;++track_set)
    for (uint8_t cup_index=0u;cup_index<5u;++cup_index)
        for (uint8_t engine=0u;engine<3u;++engine) {
            if(track_set==2u)continue;
            /* Custom sets key their rows with any unique byte. */
            PortableGpCupDefinition custom={{101u,500u,73u,9001u},
                {track_set==3u?255u:3u,11u,track_set==3u?52u:26u,40u},cup_index,track_set};
            PortableGpCupSession progress={0},result;
            for (uint8_t track=0u;track<4u;++track) {
                prepare_race(&race,custom.course_ids[track],second_order);
                race.engine_class=engine;
                CHECK(portable_gp_cup_session_begin_standings(&race,
                    track!=0u?&progress:NULL,cup_index,track,custom.source_headers[track],track_set,
                    &result)==PORTABLE_GP_CUP_SESSION_OK);
                CHECK(result.engine_class==engine&&result.cup_index==cup_index&&
                      result.cumulative_points[0]==9u*(track+1u));
                if(track<3u) {
                    CHECK(portable_gp_cup_session_advance(&result,&custom,&progress)==
                          PORTABLE_GP_CUP_SESSION_OK);
                    CHECK(progress.course_id==custom.course_ids[track+1u]&&
                          progress.engine_class==engine&&progress.racer_driver_classes[0]==7u);
                    race.engine_class=(uint8_t)((engine+1u)%3u);
                    CHECK(portable_gp_cup_session_begin_standings(&race,&progress,cup_index,
                        track+1u,custom.source_headers[track+1u],track_set,&cup)==
                        PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE);
                } else {
                    CHECK(result.track_set==track_set&&result.next_mode==7u&&result.driving_quality_sample_mask==15u);
                    int32_t sum=0;
                    for(unsigned slot=0;slot<4u;++slot)sum+=result.driving_quality.samples[slot];
                    CHECK(result.driving_quality.average_score==sum/4);
                }
            }
            custom.course_ids[1]=custom.course_ids[0];
            result.course_in_cup=0u;result.next_mode=8u;result.course_id=101u;
            result.course_header_index=custom.source_headers[0];
            CHECK(portable_gp_cup_session_advance(&result,&custom,&progress)==
                  PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE);
        }
    {
        /* Stock source headers stay bounded by their ROM track tables. */
        PortableGpCupDefinition extra={{64u,65u,66u,67u},{32u,33u,52u,35u},0u,1u};
        PortableGpCupSession standings={0},extra_next;
        prepare_race(&race,64u,second_order);race.engine_class=0u;
        CHECK(portable_gp_cup_session_begin_standings(&race,NULL,0u,0u,32u,1u,&standings)==
              PORTABLE_GP_CUP_SESSION_OK);
        CHECK(portable_gp_cup_session_advance(&standings,&extra,&extra_next)==
              PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE);
        extra.source_headers[2]=34u;
        CHECK(portable_gp_cup_session_advance(&standings,&extra,&extra_next)==PORTABLE_GP_CUP_SESSION_OK);
    }
    puts("portable GP cup session: PASS");
    return 0;
}
