#include "gp_results_flow.h"
#include "time_trial_results.h"

#include <stdio.h>

/* Authored record-art order, independent of the source donor permutation. */
static const uint8_t record_identities[8]={7,6,5,4,3,2,1,0};
static const uint8_t menu_option_codes[5]={0,3,1,2,4};
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition); \
    return __LINE__; \
} } while (0)

static int check_quick_opening(void)
{
    PortableQuickResultsOpening f;
    portable_quick_results_opening_init(&f);
    CHECK(f.state==5u&&f.row_x[0]==32u&&f.row_delay[7]==21u);
    CHECK(portable_quick_results_opening_tick(&f,0u)==PORTABLE_GP_RESULTS_OK);
    CHECK(f.row_x[0]==31u&&f.row_x[1]==32u&&f.row_delay[1]==2u);
    for(unsigned tick=1u;tick<52u;++tick)
        CHECK(portable_quick_results_opening_tick(&f,0u)==PORTABLE_GP_RESULTS_OK);
    CHECK(f.row_x[0]==0u&&f.row_x[7]==1u&&f.state==5u);
    for(unsigned tick=52u;tick<61u;++tick)
        CHECK(portable_quick_results_opening_tick(&f,0u)==PORTABLE_GP_RESULTS_OK);
    CHECK(f.state==6u&&f.row_x[7]==0u);
    CHECK(portable_quick_results_opening_tick(&f,0u)==PORTABLE_GP_RESULTS_OK&&f.state==7u);
    CHECK(portable_quick_results_opening_tick(&f,0u)==PORTABLE_GP_RESULTS_OK&&f.state==9u);
    for(unsigned step=1u;step<=5u;++step){
        CHECK(portable_quick_results_opening_tick(&f,0u)==PORTABLE_GP_RESULTS_OK);
        CHECK(f.state==9u&&f.window_progress==step);
    }
    CHECK(portable_quick_results_opening_tick(&f,0u)==PORTABLE_GP_RESULTS_OK&&f.state==8u);
    CHECK(portable_quick_results_opening_tick(&f,0u)==PORTABLE_GP_RESULTS_OK&&f.state==10u);
    const PortableQuickResultsOpening saved=f;
    CHECK(portable_quick_results_opening_tick(&f,1u)==PORTABLE_GP_RESULTS_OK&&!memcmp(&f,&saved,sizeof f));
    portable_quick_results_opening_init(&f);
    CHECK(portable_quick_results_opening_tick(&f,1u)==PORTABLE_GP_RESULTS_OK&&f.state==6u);
    CHECK(portable_quick_results_opening_tick(&f,0u)==PORTABLE_GP_RESULTS_OK);
    CHECK(portable_quick_results_opening_tick(&f,0u)==PORTABLE_GP_RESULTS_OK);
    CHECK(portable_quick_results_opening_tick(&f,1u)==PORTABLE_GP_RESULTS_OK&&f.state==8u);
    CHECK(portable_quick_results_opening_tick(&f,0u)==PORTABLE_GP_RESULTS_OK&&f.window_progress==6u);
    CHECK(portable_quick_results_opening_tick(NULL,0u)==PORTABLE_GP_RESULTS_INVALID_ARGUMENT);
    return 0;
}

static int check_gp_time_board(void)
{
    PortableGpTimeBoardFlow board;
    portable_gp_time_board_init(&board);
    CHECK(board.state==3u&&!board.visible&&!portable_gp_time_board_can_confirm(&board));
    CHECK(portable_gp_time_board_tick(&board,1u)==PORTABLE_GP_RESULTS_OK);
    CHECK(board.state==4u&&!board.visible);
    CHECK(portable_gp_time_board_tick(&board,0u)==PORTABLE_GP_RESULTS_OK);
    CHECK(board.state==5u&&board.visible&&board.row_x[7]==32u&&board.row_delay[7]==21u);
    CHECK(portable_gp_time_board_can_confirm(&board));
    CHECK(portable_gp_time_board_tick(&board,0u)==PORTABLE_GP_RESULTS_OK);
    CHECK(board.row_x[0]==31u&&board.row_x[1]==32u&&board.row_delay[1]==2u);
    for(unsigned i=1u;i<61u;++i)CHECK(portable_gp_time_board_tick(&board,0u)==PORTABLE_GP_RESULTS_OK);
    CHECK(board.state==6u&&board.row_x[7]==0u&&!board.skip_transfer);
    CHECK(portable_gp_time_board_tick(&board,0u)==PORTABLE_GP_RESULTS_OK);
    CHECK(board.state==7u&&!board.visible);
    CHECK(portable_gp_time_board_tick(&board,0u)==PORTABLE_GP_RESULTS_OK);
    CHECK(board.state==8u&&board.visible&&!portable_gp_time_board_can_confirm(&board));
    /* The settled eight-racer timeboard has a source minimum hold, even
     * when the previous slide was confirmed early or A remains held. */
    for(unsigned i=0u;i<60u;++i)CHECK(portable_gp_time_board_tick(&board,1u)==PORTABLE_GP_RESULTS_OK);
    CHECK(board.state==8u&&board.counter==60u);
    CHECK(portable_gp_time_board_tick(&board,1u)==PORTABLE_GP_RESULTS_OK);
    CHECK(board.state==9u&&!board.skip_transfer);
    for(unsigned i=0u;i<15u;++i)CHECK(portable_gp_time_board_tick(&board,0u)==PORTABLE_GP_RESULTS_OK);
    CHECK(board.state==10u&&board.progress==0u);
    for(unsigned i=0u;i<12u;++i)CHECK(portable_gp_time_board_tick(&board,0u)==PORTABLE_GP_RESULTS_OK);
    CHECK(board.state==11u);
    for(unsigned i=0u;i<31u;++i)CHECK(portable_gp_time_board_tick(&board,0u)==PORTABLE_GP_RESULTS_OK);
    CHECK(board.state==12u&&board.ticks==184u&&!board.skip_transfer);
    CHECK(portable_gp_time_board_tick(&board,1u)==PORTABLE_GP_RESULTS_OK);
    CHECK(board.state==12u&&!board.skip_transfer);
    for(unsigned phase=9u;phase<=11u;++phase){
        portable_gp_time_board_init(&board);board.state=(uint8_t)phase;
        CHECK(portable_gp_time_board_can_confirm(&board));
        CHECK(portable_gp_time_board_tick(&board,1u)==PORTABLE_GP_RESULTS_OK);
        CHECK(board.state==13u&&board.skip_transfer);
        CHECK(portable_gp_time_board_tick(&board,0u)==PORTABLE_GP_RESULTS_OK);
        CHECK(board.state==13u&&board.skip_transfer);
    }
    portable_gp_time_board_init(&board);board.state=5u;
    CHECK(portable_gp_time_board_tick(&board,1u)==PORTABLE_GP_RESULTS_OK);
    CHECK(board.state==6u&&!board.skip_transfer);
    board.state=2u;
    CHECK(portable_gp_time_board_tick(&board,0u)==PORTABLE_GP_RESULTS_STATE_UNSUPPORTED);
    CHECK(portable_gp_time_board_tick(NULL,0u)==PORTABLE_GP_RESULTS_INVALID_ARGUMENT);
    CHECK(!portable_gp_time_board_can_confirm(NULL));
    /* 1E7C8 case0 waits for the human's 1E248 stage4 before case3.
     * GP therefore keeps its own lap slides and hold before the eight rows. */
    PortableGpResultsFlow laps;
    portable_gp_results_flow_init(&laps);portable_gp_time_board_init(&board);
    unsigned callbacks=0u;
    while(laps.state<4u){
        PortableGpResultsStatus status;
        if(laps.state<=1u)status=portable_gp_results_flow_tick_entry(&laps,0u,0u);
        else if(laps.state==2u)status=portable_gp_results_flow_tick_reveal(&laps,3u);
        else status=portable_gp_results_flow_tick_lap_hold(&laps);
        CHECK(status==PORTABLE_GP_RESULTS_OK);
        ++callbacks;
        CHECK(board.state==3u&&!board.visible);
    }
    CHECK(callbacks==541u&&!laps.song_pending);
    while(board.state<12u){
        CHECK(portable_gp_time_board_tick(&board,0u)==PORTABLE_GP_RESULTS_OK);
        ++callbacks;
    }
    CHECK(callbacks==725u&&board.state==12u&&!board.skip_transfer);
    return 0;
}

static int check_time_trial_results(void)
{
    const PortableTimeTrialRecordDefaults defaults={.rows={{0u,6000u},{1u,9000u},{2u,12000u},
        {3u,15000u},{4u,18000u},{7u,2000u}}};
    CHECK(portable_time_trial_record_defaults_valid(&defaults));
    PortableGpResultsRecord actual[6]={{.flags=2u,.time_centiseconds=7000u,.roster_driver_id=9u},
        {.flags=3u,.time_centiseconds=8000u},{0},{0},{0},{.flags=4u,.time_centiseconds=2100u}};
    PortableGpResultsRecord frozen_actual[6];memcpy(frozen_actual,actual,sizeof actual);
    PortableGpResultsRecord merged[6];portable_time_trial_results_merge(&defaults,actual,merged);
    CHECK(merged[0].flags==0x10u&&merged[0].time_centiseconds==6000u&&
        merged[1].flags==2u&&merged[1].time_centiseconds==7000u&&merged[1].roster_driver_id==9u&&merged[2].flags==3u&&
        merged[3].flags==0x11u&&merged[3].time_centiseconds==9000u&&
        merged[4].time_centiseconds==12000u&&merged[5].flags==0x17u&&merged[5].time_centiseconds==2000u);
    CHECK(memcmp(actual,frozen_actual,sizeof actual)==0);
    actual[5].time_centiseconds=1900u;actual[5].roster_driver_id=9u;
    portable_time_trial_results_merge(&defaults,actual,merged);
    CHECK(merged[5].roster_driver_id==9u&&merged[5].time_centiseconds==1900u);
    actual[0].time_centiseconds=9000u;actual[1].time_centiseconds=0u;
    portable_time_trial_results_merge(&defaults,actual,merged);
    CHECK(merged[1].flags==0x11u&&merged[2].flags==2u&&merged[2].time_centiseconds==9000u);
    portable_time_trial_results_merge(NULL,actual,merged);
    for(unsigned row=0u;row<6u;++row)CHECK(actual[row].flags==merged[row].flags&&
        actual[row].time_centiseconds==merged[row].time_centiseconds&&actual[row].emphasized==merged[row].emphasized&&actual[row].roster_driver_id==merged[row].roster_driver_id);
    PortableTimeTrialRecordDefaults invalid=defaults;invalid.rows[1].centiseconds=1u;
    CHECK(!portable_time_trial_record_defaults_valid(&invalid));
    const uint32_t old[6]={100u,100u,120u,140u,160u,30u};uint8_t emphasis[6];
    portable_time_trial_results_emphasis(old,100u,30u,emphasis);
    CHECK(!emphasis[0]&&!emphasis[1]&&emphasis[2]&&!emphasis[3]&&!emphasis[4]&&!emphasis[5]);
    portable_time_trial_results_emphasis(old,200u,29u,emphasis);
    CHECK(!emphasis[0]&&!emphasis[1]&&!emphasis[2]&&!emphasis[3]&&!emphasis[4]&&emphasis[5]);
    PortableGpResultsRecord records[6]={{.flags=1u,.time_centiseconds=12345u,.emphasized=1u,.roster_driver_id=9u},
        {.flags=0x10u},{.flags=3u,.time_centiseconds=23456u},
        {.flags=4u},{.flags=5u},{.flags=7u,.time_centiseconds=4000u,.emphasized=1u}};
    PortableGpResultsFlow flow;portable_gp_results_flow_init(&flow);
    while(flow.total_ticks<540u){
        /* Early A/Start cannot bypass the source lap wait or hold. */
        CHECK(portable_time_trial_results_tick(&flow,3u,records,record_identities,menu_option_codes,9u,9u,0u)==PORTABLE_GP_RESULTS_OK);
    }
    CHECK(flow.state==3u&&flow.records_changed&&flow.total_ticks==540u);
    CHECK(portable_time_trial_results_tick(&flow,3u,records,record_identities,menu_option_codes,0u,0u,0u)==PORTABLE_GP_RESULTS_OK&&flow.state==4u);
    while(flow.state!=11u)
        CHECK(portable_time_trial_results_tick(&flow,3u,records,record_identities,menu_option_codes,0u,0u,0u)==PORTABLE_GP_RESULTS_OK);
    CHECK(flow.total_ticks==655u&&flow.record_row_count==6u&&
        flow.record_rows[0].time_centiseconds==12345u&&flow.record_rows[0].emphasized&&flow.record_rows[0].roster_driver_id==9u&&
        flow.record_rows[1].identity==9u&&flow.record_rows[5].display_row==6u);
    const PortableGpResultsFlow menu=flow;
    const uint8_t dispatch[5]={1u,6u,2u,3u,4u};
    for(unsigned option=0u;option<5u;++option){
        flow=menu;flow.menu_selection=(uint8_t)option;
        CHECK(portable_time_trial_results_tick(&flow,3u,records,record_identities,menu_option_codes,0u,1u,0u)==PORTABLE_GP_RESULTS_OK&&flow.state==12u);
        CHECK(portable_time_trial_results_tick(&flow,3u,records,record_identities,menu_option_codes,0u,0u,0u)==PORTABLE_GP_RESULTS_OK&&
            flow.state==13u&&flow.action_dispatch_code==dispatch[option]);
        const PortableGpResultsFlow terminal=flow;
        CHECK(portable_time_trial_results_tick(&flow,3u,records,record_identities,menu_option_codes,0u,0u,0u)==PORTABLE_GP_RESULTS_OK&&
            flow.total_ticks==terminal.total_ticks&&flow.action_dispatch_code==terminal.action_dispatch_code);
    }
    flow=menu;flow.menu_selection=1u;
    CHECK(portable_time_trial_results_tick(&flow,3u,records,record_identities,menu_option_codes,0u,1u,1u)==PORTABLE_GP_RESULTS_OK&&
        flow.state==11u&&flow.menu_confirm_song_id==145u&&!flow.scene_request_pending);
    CHECK(portable_time_trial_results_tick(&flow,0u,records,record_identities,menu_option_codes,0u,0u,0u)==PORTABLE_GP_RESULTS_INVALID_ARGUMENT);
    puts("Time Trial results: PASS source655-turn reveal, strict inserted-row emphasis and five actions");
    return 0;
}

int main(void)
{
    CHECK(check_quick_opening()==0);
    CHECK(check_gp_time_board()==0);
    CHECK(check_time_trial_results()==0);
    CHECK(!portable_gp_results_advance_allowed(0u,0u));
    CHECK(!portable_gp_results_advance_allowed(0x21cu,0u));
    CHECK(portable_gp_results_advance_allowed(0x21du,0u));
    CHECK(portable_gp_results_advance_allowed(1u,1u));

    PortableGpResultsFlow flow;
    portable_gp_results_flow_init(&flow);
    for (unsigned tick = 0u; tick < 180u; ++tick)
        CHECK(portable_gp_results_flow_tick_entry(&flow, 1u, 1u) ==
              PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 0u && flow.state_counter == 180u &&
          flow.total_ticks == 180u && !flow.records_changed);
    CHECK(portable_gp_results_flow_tick_entry(&flow, 0u, 1u) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 1u && flow.state_counter == 0u &&
          flow.total_ticks == 181u && flow.records_changed &&
          !flow.background_copy_pending && !flow.background_clear_pending);
    CHECK(portable_gp_results_flow_tick_entry(&flow, 0u, 0u) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 2u && flow.total_ticks == 182u &&
          flow.background_copy_pending && flow.background_clear_pending);
    CHECK(portable_gp_results_flow_tick_reveal(&flow, 3u) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 2u && flow.state_counter == 1u &&
          !flow.reveal_pending);
    CHECK(portable_gp_results_flow_tick_reveal(&flow, 3u) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.reveal_pending && flow.background_clear_pending &&
          flow.reveal_progress == 1u && flow.row_count == 0u &&
          flow.header_x == 17u && flow.header_y == 1u &&
          flow.header_variant == 0u);
    for (unsigned progress = 2u; progress <= 8u; ++progress)
    {
        CHECK(portable_gp_results_flow_tick_reveal(&flow, 3u) ==
              PORTABLE_GP_RESULTS_OK);
        CHECK(!flow.reveal_pending);
        CHECK(portable_gp_results_flow_tick_reveal(&flow, 3u) ==
              PORTABLE_GP_RESULTS_OK);
        CHECK(flow.reveal_pending && flow.reveal_progress == progress &&
              flow.row_count == progress / 2u &&
              flow.header_x == 18u - (9u * progress) / 8u &&
              flow.header_y == progress &&
              flow.header_variant == (progress > 7u));
        for (unsigned row = 0u; row < flow.row_count; ++row)
            CHECK(flow.rows[row].racer_index == row &&
                  flow.rows[row].x == flow.header_x - (row == 3u) &&
                  flow.rows[row].y == 1u + 2u * row);
    }
    CHECK(flow.row_count == 4u && flow.header_x == 9u &&
          flow.rows[3].x == 8u &&
          flow.rows[3].y == 7u);
    CHECK(portable_gp_results_flow_tick_reveal(&flow, 3u) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(portable_gp_results_flow_tick_reveal(&flow, 3u) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 3u && flow.reveal_progress == 0u &&
          flow.row_count == 0u && !flow.reveal_pending &&
          flow.total_ticks == 200u);
    CHECK(portable_gp_results_flow_tick_reveal(&flow, 3u) ==
          PORTABLE_GP_RESULTS_STATE_UNSUPPORTED);
    for (unsigned tick = 0u; tick < 340u; ++tick)
        CHECK(portable_gp_results_flow_tick_hold(&flow, UINT16_C(0x2200)) ==
              PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 3u && flow.total_ticks == 540u &&
          !flow.song_pending && !flow.background_copy_pending);
    CHECK(portable_gp_results_flow_tick_hold(&flow, UINT16_C(0x2200)) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 4u && flow.total_ticks == 541u && flow.song_pending &&
          flow.song_id == 14u && flow.result_delay == 15u &&
          flow.background_copy_pending && !flow.exit_pending);
    CHECK(portable_gp_results_flow_tick_hold(&flow, UINT16_C(0x2200)) ==
          PORTABLE_GP_RESULTS_STATE_UNSUPPORTED);
    CHECK(portable_gp_results_flow_tick_exit_wipe(&flow, 0u) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 4u && flow.total_ticks == 542u &&
          flow.state_counter == 0u && flow.exit_wipe_pending &&
          flow.exit_wipe_progress == 1u && flow.exit_band_count == 1u &&
          flow.exit_bands[0].source_y == 1u &&
          flow.exit_bands[0].row_count == 2u &&
          flow.exit_bands[0].destination_x == -1 &&
          !flow.advance_skip_pending);
    for (unsigned tick = 2u; tick <= 13u; ++tick)
        CHECK(portable_gp_results_flow_tick_exit_wipe(&flow, 0u) ==
              PORTABLE_GP_RESULTS_OK);
    CHECK(flow.exit_wipe_progress == 13u && flow.exit_band_count == 5u);
    for (unsigned band = 0u; band < PORTABLE_GP_RESULTS_EXIT_BAND_COUNT; ++band)
    {
        static const unsigned source_y[] = {1u, 3u, 5u, 7u, 8u};
        static const unsigned rows[] = {2u, 2u, 2u, 1u, 2u};
        static const int offsets[] = {0, 3, 6, 9, 12};
        CHECK(flow.exit_bands[band].source_y == source_y[band] &&
              flow.exit_bands[band].row_count == rows[band] &&
              flow.exit_bands[band].destination_x == offsets[band] - 13);
    }
    for (unsigned tick = 14u; tick <= 43u; ++tick)
        CHECK(portable_gp_results_flow_tick_exit_wipe(&flow, 0u) ==
              PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 4u && flow.exit_wipe_progress == 43u &&
          flow.exit_wipe_pending && flow.exit_band_count == 5u);
    CHECK(portable_gp_results_flow_tick_exit_wipe(&flow, 0u) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 5u && flow.total_ticks == 585u &&
          flow.exit_wipe_progress == 0u && !flow.exit_wipe_pending &&
          flow.exit_band_count == 0u);
    CHECK(portable_gp_results_flow_tick_exit_wipe(&flow, 0u) ==
          PORTABLE_GP_RESULTS_STATE_UNSUPPORTED);
    CHECK(portable_gp_results_flow_tick_followup_setup(&flow) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 6u && flow.total_ticks == 586u &&
          flow.followup_graphics_pending && flow.followup_map_pending &&
          flow.scroll_map_pending);
    CHECK(portable_gp_results_flow_tick_followup_setup(&flow) ==
          PORTABLE_GP_RESULTS_STATE_UNSUPPORTED);
    {
        const PortableGpResultsRecord records[
            PORTABLE_GP_RESULTS_RECORD_ROW_COUNT] = {
            {0u,100u,0u,0u}, {1u,200u,1u,0u}, {2u,300u,0u,0u},
            {3u,400u,1u,0u}, {7u,500u,0u,0u},
            {UINT32_C(0x10),600u,1u,0u}};
        static const uint8_t identities[] = {7u, 6u, 5u, 4u, 0u, 9u};
        const PortableGpResultsFlow before=flow;
        CHECK(portable_gp_results_flow_tick_record_setup(&flow,records,NULL)==PORTABLE_GP_RESULTS_INVALID_ARGUMENT);
        CHECK(memcmp(&flow,&before,sizeof flow)==0);
        CHECK(portable_gp_results_flow_tick_record_setup(&flow, records,record_identities) ==
              PORTABLE_GP_RESULTS_OK);
        CHECK(flow.state == 7u && flow.total_ticks == 587u &&
              flow.track_name_pending && flow.followup_object_pending &&
              flow.record_rows_pending && flow.record_row_count == 6u &&
              flow.scroll_x == UINT16_C(0x016c) && flow.scroll_y == 0u &&
              flow.state_counter == 0u);
        for (unsigned index = 0u; index < 6u; ++index)
            CHECK(flow.record_rows[index].display_row ==
                      (index < 5u ? index : 6u) &&
                  flow.record_rows[index].identity == identities[index] &&
                  flow.record_rows[index].time_centiseconds ==
                      records[index].time_centiseconds &&
                  flow.record_rows[index].emphasized == (index & 1u));
        CHECK(portable_gp_results_flow_tick_record_setup(&flow, records,record_identities) ==
              PORTABLE_GP_RESULTS_STATE_UNSUPPORTED);
        CHECK(portable_gp_results_flow_tick_record_setup(NULL, records,record_identities) ==
              PORTABLE_GP_RESULTS_INVALID_ARGUMENT);
        flow.state = 6u;
        CHECK(portable_gp_results_flow_tick_record_setup(&flow, NULL,record_identities) ==
              PORTABLE_GP_RESULTS_INVALID_ARGUMENT);
        flow.state = 7u;
    }
    CHECK(portable_gp_results_flow_tick_record_slide(&flow, 0u) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 7u && flow.record_slide_pending &&
          flow.map_dirty_pending && flow.record_slide_progress == 1u &&
          flow.slide_band_count == 9u && flow.scroll_x == 356u &&
          flow.slide_bands[0].source_y == 0u &&
          flow.slide_bands[0].destination_x == 31u &&
          flow.slide_bands[8].source_y == 17u &&
          flow.slide_bands[8].destination_x == 58u);
    for (unsigned tick = 2u; tick <= 32u; ++tick)
        CHECK(portable_gp_results_flow_tick_record_slide(&flow, 0u) ==
              PORTABLE_GP_RESULTS_OK);
    CHECK(flow.record_slide_progress == 32u && flow.scroll_x == 108u &&
          flow.slide_bands[0].destination_x == 0u &&
          flow.slide_bands[8].destination_x == 27u);
    for (unsigned tick = 33u; tick <= 59u; ++tick)
        CHECK(portable_gp_results_flow_tick_record_slide(&flow, 0u) ==
              PORTABLE_GP_RESULTS_OK);
    CHECK(flow.record_slide_progress == 59u && flow.scroll_x == 108u &&
          flow.slide_bands[8].destination_x == 0u);
    CHECK(portable_gp_results_flow_tick_record_slide(&flow, 0u) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 8u && flow.total_ticks == 647u &&
          flow.record_slide_progress == 0u && !flow.record_slide_pending &&
          !flow.map_dirty_pending && flow.slide_band_count == 0u);
    CHECK(portable_gp_results_flow_tick_record_slide(&flow, 0u) ==
          PORTABLE_GP_RESULTS_STATE_UNSUPPORTED);
    CHECK(portable_gp_results_flow_tick_record_slide(NULL, 0u) ==
          PORTABLE_GP_RESULTS_INVALID_ARGUMENT);
    for (unsigned progress = 1u; progress <= 5u; ++progress)
    {
        CHECK(portable_gp_results_flow_tick_menu_reveal(&flow, 0u) ==
              PORTABLE_GP_RESULTS_OK);
        CHECK(flow.state == 8u && flow.menu_reveal_pending &&
              flow.map_dirty_pending &&
              flow.menu_reveal_progress == progress);
    }
    CHECK(portable_gp_results_flow_tick_menu_reveal(&flow, 0u) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 9u && flow.total_ticks == 653u &&
          flow.menu_reveal_progress == 0u && !flow.menu_reveal_pending &&
          !flow.map_dirty_pending);
    CHECK(portable_gp_results_flow_tick_menu_reveal(&flow, 0u) ==
          PORTABLE_GP_RESULTS_STATE_UNSUPPORTED);
    CHECK(portable_gp_results_flow_tick_menu_reveal(NULL, 0u) ==
          PORTABLE_GP_RESULTS_INVALID_ARGUMENT);
    CHECK(portable_gp_results_flow_tick_menu_setup(&flow, UINT16_C(2)) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 10u && flow.total_ticks == 654u &&
          flow.menu_setup_graphics_pending && flow.menu_setup_map_pending &&
          flow.menu_setup_scroll_map_pending && flow.menu_palette_pending &&
          flow.menu_highlight_pending && flow.menu_highlight_x == 22u &&
          flow.menu_highlight_y == 11u && flow.menu_highlight_width == 7u &&
          flow.menu_highlight_height == 2u &&
          flow.menu_highlight_palette_bank == 4u);
    CHECK(portable_gp_results_flow_tick_menu_record_setup(&flow) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 11u && flow.total_ticks == 655u &&
          flow.track_name_pending && flow.followup_object_pending &&
          flow.record_rows_pending && flow.map_dirty_pending &&
          flow.scroll_x == UINT16_C(0x006c) && flow.scroll_y == 0u &&
          flow.state_counter == 0u && flow.exit_wipe_progress == 0u);
    CHECK(portable_gp_results_flow_tick_menu_selection(
              &flow, 0u, 0u, UINT16_C(2),menu_option_codes) == PORTABLE_GP_RESULTS_OK);
    {
        PortableGpResultsFlow changed=flow;const PortableGpResultsFlow before=changed;
        CHECK(portable_gp_results_flow_tick_menu_selection(&changed,0u,0u,0u,NULL)==PORTABLE_GP_RESULTS_INVALID_ARGUMENT);
        CHECK(memcmp(&before,&changed,sizeof changed)==0);
        const uint8_t custom_codes[5]={4u,0u,2u,1u,3u};
        CHECK(portable_gp_results_flow_tick_menu_selection(&changed,0u,1u,0u,custom_codes)==PORTABLE_GP_RESULTS_OK);
        CHECK(changed.menu_action_code==4u&&changed.state==12u);
    }
    CHECK(flow.state == 11u && flow.menu_selection == 0u &&
          flow.menu_cursor_pending && flow.menu_selection_highlight_pending &&
          !flow.menu_selection_changed && !flow.menu_disabled_pending);
    CHECK(portable_gp_results_flow_tick_menu_selection(
              &flow, UINT16_C(0x80), 0u, UINT16_C(2),menu_option_codes) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.menu_selection == 1u && flow.menu_previous_selection == 0u &&
          flow.menu_selection_changed && !flow.menu_cursor_pending &&
          flow.menu_navigation_song_pending &&
          flow.menu_navigation_song_id == 141u);
    CHECK(portable_gp_results_flow_tick_menu_selection(
              &flow, 0u, UINT16_C(9), UINT16_C(2),menu_option_codes) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 11u && flow.menu_selection == 1u &&
          flow.menu_cursor_pending && flow.menu_disabled_pending &&
          !flow.menu_selection_highlight_pending &&
          flow.menu_confirm_song_pending && flow.menu_confirm_song_id == 145u);
    CHECK(portable_gp_results_flow_tick_menu_selection(
              &flow, UINT16_C(0x80), 0u, UINT16_C(2),menu_option_codes) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.menu_selection == 2u && flow.menu_action_code == 1u);
    CHECK(portable_gp_results_flow_tick_menu_selection(
              &flow, 0u, UINT16_C(1), UINT16_C(2),menu_option_codes) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 12u && flow.menu_confirm_song_pending &&
          flow.menu_confirm_song_id == 143u);
    CHECK(portable_gp_results_flow_tick_action_dispatch(&flow) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 13u && flow.action_dispatch_pending &&
          flow.action_dispatch_code == 2u && flow.scene_request_pending &&
          flow.scene_request_value == UINT32_C(1));
    CHECK(portable_gp_results_flow_tick_action_dispatch(&flow) ==
          PORTABLE_GP_RESULTS_STATE_UNSUPPORTED);
    CHECK(portable_gp_results_flow_tick_action_dispatch(NULL) ==
          PORTABLE_GP_RESULTS_INVALID_ARGUMENT);
    for (uint8_t action = 0u; action < 5u; ++action)
    {
        static const uint8_t expected[5] = {1u, 2u, 3u, 6u, 4u};
        portable_gp_results_flow_init(&flow);
        flow.state = 12u;
        flow.menu_action_code = action;
        CHECK(portable_gp_results_flow_tick_action_dispatch(&flow) ==
              PORTABLE_GP_RESULTS_OK);
        CHECK(flow.state == 13u && flow.action_dispatch_pending &&
              flow.action_dispatch_code == expected[action] &&
              flow.scene_request_pending && flow.scene_request_value == 1u);
    }
    CHECK(portable_gp_results_flow_tick_menu_selection(
              &flow, 0u, 0u, 0u,menu_option_codes) == PORTABLE_GP_RESULTS_STATE_UNSUPPORTED);
    CHECK(portable_gp_results_flow_tick_menu_selection(
              NULL, 0u, 0u, 0u,menu_option_codes) == PORTABLE_GP_RESULTS_INVALID_ARGUMENT);
    CHECK(portable_gp_results_flow_tick_menu_record_setup(&flow) ==
          PORTABLE_GP_RESULTS_STATE_UNSUPPORTED);
    CHECK(portable_gp_results_flow_tick_menu_record_setup(NULL) ==
          PORTABLE_GP_RESULTS_INVALID_ARGUMENT);
    CHECK(portable_gp_results_flow_tick_menu_setup(&flow, UINT16_C(2)) ==
          PORTABLE_GP_RESULTS_STATE_UNSUPPORTED);
    CHECK(portable_gp_results_flow_tick_menu_setup(NULL, UINT16_C(2)) ==
          PORTABLE_GP_RESULTS_INVALID_ARGUMENT);
    portable_gp_results_flow_init(&flow);
    flow.state = 9u;
    CHECK(portable_gp_results_flow_tick_menu_setup(&flow, 0u) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 10u && !flow.menu_palette_pending &&
          !flow.menu_highlight_pending);
    CHECK(portable_gp_results_flow_tick_followup_setup(NULL) ==
          PORTABLE_GP_RESULTS_INVALID_ARGUMENT);
    CHECK(portable_gp_results_flow_tick_reveal(NULL, 3u) ==
          PORTABLE_GP_RESULTS_INVALID_ARGUMENT);
    flow.state = 2u;
    CHECK(portable_gp_results_flow_tick_reveal(&flow, 6u) ==
          PORTABLE_GP_RESULTS_INVALID_ARGUMENT);
    portable_gp_results_flow_init(&flow);
    flow.state = 3u;
    flow.total_ticks = 540u;
    CHECK(portable_gp_results_flow_tick_hold(&flow, UINT16_C(0x2200)) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 4u && flow.song_id == 15u && flow.song_pending);
    portable_gp_results_flow_init(&flow);
    flow.state = 3u;
    flow.total_ticks = 540u;
    CHECK(portable_gp_results_flow_tick_hold(&flow, UINT16_C(0x6200)) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 3u && flow.exit_pending && flow.exit_code == 9u &&
          flow.background_clear_pending && !flow.song_pending);
    portable_gp_results_flow_init(&flow);
    flow.state = 4u;
    CHECK(portable_gp_results_flow_tick_exit_wipe(&flow, 1u) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 9u && flow.exit_wipe_progress == 1u &&
          flow.exit_wipe_pending && flow.advance_skip_pending);
    portable_gp_results_flow_init(&flow);
    flow.state = 7u;
    CHECK(portable_gp_results_flow_tick_record_slide(&flow, 1u) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 9u && flow.record_slide_progress == 1u &&
          flow.record_slide_pending && flow.advance_skip_pending);
    portable_gp_results_flow_init(&flow);
    flow.state = 8u;
    CHECK(portable_gp_results_flow_tick_menu_reveal(&flow, 1u) ==
          PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 9u && flow.menu_reveal_progress == 1u &&
          flow.menu_reveal_pending && flow.advance_skip_pending);
    CHECK(portable_gp_results_flow_tick_exit_wipe(NULL, 0u) ==
          PORTABLE_GP_RESULTS_INVALID_ARGUMENT);
    CHECK(portable_gp_results_flow_tick_hold(NULL, UINT16_C(0x2200)) ==
          PORTABLE_GP_RESULTS_INVALID_ARGUMENT);
    portable_gp_results_flow_init(&flow);
    for (unsigned tick = 0u; tick < 181u; ++tick)
        CHECK(portable_gp_results_flow_tick_entry(&flow, 0u, 0u) ==
              PORTABLE_GP_RESULTS_OK);
    CHECK(flow.state == 1u && !flow.records_changed);
    CHECK(portable_gp_results_flow_tick_entry(NULL, 0u, 0u) ==
          PORTABLE_GP_RESULTS_INVALID_ARGUMENT);
    puts("GP results entry: PASS states=0>1>2>3>4>5>6>7>8>9>10>11>12>13 reveal=1..8 hold=541 wipe=1..43/44 setup=586 records=587 slide=1..59/60 menu=1..5/6 setup=654/655 selection=0>1-disabled>2 dispatch=1/2/3/6/4 songs=14/15/141/143/145");
    return 0;
}
