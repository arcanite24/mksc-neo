#include "gp_race_results.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr,"GP race results failed line %d: %s\n",__LINE__,#condition); \
    return 1; \
} } while (0)

int main(void)
{
    PortableGpRaceSession race;
    PortableGpResultsSessionConfig config;
    memset(&race, 0, sizeof race);
    CHECK(portable_gp_race_results_config(NULL, &config) ==
          PORTABLE_GP_RACE_RESULTS_INVALID_ARGUMENT);
    CHECK(portable_gp_race_results_config(&race, &config) ==
          PORTABLE_GP_RACE_RESULTS_NOT_READY);

    race.runtime.session.phase = PORTABLE_RACE_PHASE_FINISHED;
    race.runtime.session.lap_count = 3u;
    race.runtime.session.laps_completed = 3u;
    race.runtime.session.elapsed_ticks = 6000u;
    race.runtime.session.lap_ticks[0] = 2100u;
    race.runtime.session.lap_ticks[1] = 1900u;
    race.runtime.session.lap_ticks[2] = 2000u;
    race.roster.race_phase_7d4 = UINT8_C(0x85);
    race.roster.race_mode_flags_7da = UINT16_C(0x2200);
    race.roster.racers[0].source_flags = UINT32_C(0x20);
    race.roster.racers[0].driver_class = 7u;
    CHECK(portable_gp_race_results_config(&race, &config) ==
          PORTABLE_GP_RACE_RESULTS_OK);
    CHECK(config.race_flags == UINT16_C(0x2200) &&
          config.course_in_cup == 0u && config.highlighted_row == 0u &&
          config.course_record_changed == 1u &&
          config.cup_record_changed == 0u);
    CHECK(config.times_centiseconds[0] == 10045u &&
          config.times_centiseconds[1] == UINT16_C(0xea5f));
    CHECK(config.records[0].flags == 7u &&
          config.records[0].time_centiseconds == 10045u &&
          config.records[0].emphasized == 1u);
    CHECK(config.records[1].flags == UINT32_C(0x10) &&
          config.records[1].time_centiseconds == UINT16_C(0xea5f));
    CHECK(config.records[5].flags == 7u &&
          config.records[5].time_centiseconds == 3181u &&
          config.records[5].emphasized == 1u);
    race.runtime.session.lap_count=5u;race.runtime.session.laps_completed=3u;
    CHECK(portable_gp_race_results_config(&race,&config)==PORTABLE_GP_RACE_RESULTS_NOT_READY);
    race.runtime.session.laps_completed=5u;race.runtime.session.lap_ticks[3]=1800u;
    race.runtime.session.lap_ticks[4]=1700u;race.runtime.session.elapsed_ticks=9500u;
    CHECK(portable_gp_race_results_config(&race,&config)==PORTABLE_GP_RACE_RESULTS_OK&&
          config.records[5].time_centiseconds==2846u);
    race.runtime.session.lap_count=6u;race.runtime.session.laps_completed=6u;
    CHECK(portable_gp_race_results_config(&race,&config)==PORTABLE_GP_RACE_RESULTS_NOT_READY);
    /* 2123C freezes all eight rows in rank order. Finished rivals sum their
     * DCF0 splits; unfinished rivals use D7B8 checkpoint estimates. */
    race.rules.route_count=80u;race.rules.lap_count=3u;race.engine_class=2u;
    race.source_result_clock.laps_recorded=3u;
    race.source_result_clock.centiseconds=10000u;
    race.source_result_clock.lap_centiseconds[0]=3000u;
    race.source_result_clock.lap_centiseconds[1]=3200u;
    race.source_result_clock.lap_centiseconds[2]=3100u;
    for(unsigned i=0u;i<8u;++i){
        race.roster.standings[i]=(uint8_t)((i+1u)%8u);
        race.roster.racers[i].driver_class=(uint8_t)(7u-i);
        race.roster.racers[i].rank_flags_ba=(uint16_t)(i*2u);
        race.roster.racers[i].route_wrap_91=0x82u;
        race.roster.racers[i].route_index=19u;
    }
    race.roster.racers[1].source_flags=0x20u;
    race.rival_lap_centiseconds[0][1]=5000u;
    race.rival_lap_centiseconds[0][2]=4000u;
    PortableQuickRunResults board;
    CHECK(portable_quick_run_results_build(&race,&board)==PORTABLE_GP_RACE_RESULTS_OK);
    CHECK(board.engine_class==2u && board.rows[0].racer_index==1u && board.rows[0].driver_class==6u &&
          board.rows[0].centiseconds==9000u && !board.rows[0].estimated);
    CHECK(board.rows[1].racer_index==2u && board.rows[1].estimated &&
          board.rows[1].centiseconds==10782u);
    CHECK(board.rows[7].racer_index==0u && !board.rows[7].estimated &&
          board.rows[7].centiseconds==9300u);
    race.rules.lap_count=5u;race.source_result_clock.laps_recorded=5u;
    race.source_result_clock.lap_centiseconds[3]=30000u;
    race.source_result_clock.lap_centiseconds[4]=30000u;
    race.source_result_clock.centiseconds=60000u;
    CHECK(portable_quick_run_results_build(&race,&board)==PORTABLE_GP_RACE_RESULTS_OK);
    CHECK(board.rows[7].centiseconds==0xea5fu && board.rows[1].centiseconds==0xea5fu);
    /* The source masks the checkpoint delta to u16 before multiplying. */
    race.source_result_clock.centiseconds=0u;
    race.roster.racers[2].route_wrap_91=0x86u;
    CHECK(portable_quick_run_results_build(&race,&board)==PORTABLE_GP_RACE_RESULTS_OK &&
          board.rows[1].centiseconds==0xea5fu);
    race.roster.standings[1]=1u;
    CHECK(portable_quick_run_results_build(&race,&board)==PORTABLE_GP_RACE_RESULTS_INVALID_ARGUMENT);
    /* Battle freezes the four source standings without lap records. */
    memset(&race,0,sizeof race);
    race.player.battle_enabled=1u;race.runtime.session.phase=PORTABLE_RACE_PHASE_FINISHED;
    race.roster.race_phase_7d4=4u;race.roster.racer_count=4u;race.roster.race_mode_flags_7da=0x800u;
    PortableBattleResults battle;
    CHECK(portable_battle_results_build(&race,&battle)==PORTABLE_GP_RACE_RESULTS_NOT_READY);
    race.roster.race_phase_7d4=5u;
    for(unsigned row=0u;row<4u;++row) {
        const unsigned owner=3u-row;
        race.roster.standings[row]=(uint8_t)owner;
        race.roster.racers[owner].driver_class=(uint8_t)(owner+2u);
        race.roster.racers[owner].rank_flags_ba=(uint16_t)(row*2u);
        race.roster.racers[owner].balloons_remaining=(uint8_t)(row==0u?2u:0u);
    }
    CHECK(portable_battle_results_build(&race,&battle)==PORTABLE_GP_RACE_RESULTS_OK);
    CHECK(battle.count==4u && battle.rows[0].racer_index==3u && battle.rows[0].balloons==2u &&
        battle.rows[3].racer_index==0u && battle.rows[3].rank==3u);
    race.roster.standings[1]=race.roster.standings[0];
    CHECK(portable_battle_results_build(&race,&battle)==PORTABLE_GP_RACE_RESULTS_INVALID_ARGUMENT);
    /* VS lists finishers by standings, then the retired local. */
    memset(&race,0,sizeof race);
    PortableVsResults vs;
    race.local_count=3u;race.roster.racer_count=3u;race.roster.race_mode_flags_7da=0x400u;
    race.roster.race_phase_7d4=0x84u;
    CHECK(portable_vs_results_build(&race,&vs)==PORTABLE_GP_RACE_RESULTS_NOT_READY);
    race.roster.race_phase_7d4=0x85u;
    const uint8_t order[3]={1u,2u,0u};
    for(unsigned rank=0u;rank<3u;++rank)race.roster.standings[rank]=order[rank];
    race.roster.racers[1].source_flags=0x20u;race.vs_finish_centiseconds[1]=9000u;
    race.roster.racers[0].source_flags=0x20u;race.vs_finish_centiseconds[0]=9500u;
    race.roster.racers[2].driver_class=5u;race.vs_retired[2]=1u;
    CHECK(portable_vs_results_build(&race,&vs)==PORTABLE_GP_RACE_RESULTS_OK);
    CHECK(vs.count==3u && vs.rows[0].racer_index==1u && vs.rows[0].centiseconds==9000u &&
          vs.rows[1].racer_index==0u && vs.rows[1].centiseconds==9500u && !vs.rows[1].retired &&
          vs.rows[2].racer_index==2u && vs.rows[2].retired && vs.rows[2].driver_class==5u);
    race.roster.standings[1]=1u;
    CHECK(portable_vs_results_build(&race,&vs)==PORTABLE_GP_RACE_RESULTS_INVALID_ARGUMENT);
    race.local_count=1u;race.roster.racer_count=1u;
    CHECK(portable_vs_results_build(&race,&vs)==PORTABLE_GP_RACE_RESULTS_INVALID_ARGUMENT);
    puts("portable GP race results: PASS");
    return 0;
}
