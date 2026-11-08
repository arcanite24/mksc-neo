#include "../app/frontend_flow.h"
#include "../app/mesh_cup_menu.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "frontend flow failed line %d: %s\n", \
                __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static const PortableFrontendDriverSlot drivers[9] = {
    {1u,1u,0u,0u,0u,0u,0u}, {2u,0u,0u,0u,0u,0u,0u}, {3u,0u,0u,0u,0u,0u,0u},
    {4u,0u,0u,0u,0u,0u,0u}, {0u,1u,0u,0u,0u,0u,0u}, {5u,0u,0u,0u,0u,0u,0u},
    {6u,0u,0u,0u,0u,0u,0u}, {7u,0u,0u,0u,0u,0u,0u}, {8u,1u,0u,0u,0u,0u,0u},
};

static const PortableFrontendCourseSlot courses[] = {
    {24u, 0u, 0u, 1u}, {25u, 0u, 1u, 1u},
    {26u, 0u, 2u, 0u}, {27u, 0u, 3u, 0u},
    {40u, 4u, 0u, 1u},
};

static const PortableFrontendConfig config = {
    drivers, 9u, courses, sizeof courses / sizeof courses[0],
};

static int ticks(PortableFrontendFlow *flow, uint16_t buttons, unsigned count)
{
    while (count-- != 0u)
        if (!portable_frontend_flow_tick(flow, buttons))
            return 0;
    return 1;
}

static int settle(PortableFrontendFlow *flow);
static int tap(PortableFrontendFlow *flow, uint16_t button)
{
    /* An ordinary menu press waits until its preceding scene enters. Tests
     * of input during a lock use raw flow_tick instead. */
    if(!settle(flow))return 0;
    return portable_frontend_flow_tick(flow, button) &&
           portable_frontend_flow_tick(flow, 0u);
}

static int settle(PortableFrontendFlow *flow)
{
    unsigned remaining = 512u;
    while ((flow->transition_ticks != 0u||flow->cup_motion_phase!=0u) && remaining-- != 0u)
        if (!portable_frontend_flow_tick(flow, 0u))
            return 0;
    return flow->transition_ticks == 0u&&flow->cup_motion_phase==0u;
}

static int reach_mode(PortableFrontendFlow *flow)
{
    return ticks(flow, 0u, 32u) &&
           tap(flow, PORTABLE_FRONTEND_BUTTON_A) && settle(flow) &&
           flow->page == PORTABLE_FRONTEND_PAGE_TITLE_MENU &&
           ticks(flow, 0u, 70u) && tap(flow, PORTABLE_FRONTEND_BUTTON_A) &&
           settle(flow) && flow->page == PORTABLE_FRONTEND_PAGE_MODE;
}

static int reach_character(PortableFrontendFlow *flow)
{
    return reach_mode(flow) && tap(flow, PORTABLE_FRONTEND_BUTTON_RIGHT) &&
           tap(flow, PORTABLE_FRONTEND_BUTTON_A) && settle(flow) &&
           flow->page == PORTABLE_FRONTEND_PAGE_TIME_TRIAL &&
           tap(flow, PORTABLE_FRONTEND_BUTTON_A) && settle(flow) &&
           flow->page == PORTABLE_FRONTEND_PAGE_CHARACTER;
}

static int test_night_title_ambient(void)
{
    PortableFrontendFlow flow, replay, day;
    CHECK(portable_frontend_flow_init(&flow,&config));
    flow.page=PORTABLE_FRONTEND_PAGE_TITLE_MENU;flow.title_variant=2u;
    replay=flow;day=flow;day.title_variant=0u;
    CHECK(ticks(&flow,0u,1u));
    CHECK(flow.title_star_state==1u&&flow.title_ship_ticks==1u);
    CHECK(ticks(&flow,0u,1u));
    CHECK(flow.title_star_state==2u&&flow.title_star_counter==0u);
    CHECK(ticks(&flow,0u,32u));
    CHECK(flow.title_star_state==2u&&flow.title_star_counter==32u);
    CHECK(ticks(&flow,0u,1u));
    CHECK(flow.title_star_state==0u&&flow.title_star_counter==0u);
    CHECK(flow.title_star_wait>=255u&&flow.title_star_wait<=510u);
    CHECK(flow.title_ambient_rng==12345u);
    CHECK(ticks(&flow,0u,566u));
    CHECK(flow.title_ship_ticks==601u);
    CHECK(ticks(&flow,0u,1u));CHECK(flow.title_ship_ticks==602u);
    CHECK(ticks(&replay,0u,602u));
    CHECK(portable_frontend_flow_checksum(&flow)==portable_frontend_flow_checksum(&replay));
    CHECK(memcmp(&flow,&replay,sizeof flow)==0);
    CHECK(ticks(&day,0u,602u));
    CHECK(day.title_ship_ticks==0u && flow.title_ship_ticks==602u);
    CHECK(portable_frontend_flow_checksum(&day)==portable_frontend_flow_checksum(&flow));
    uint32_t ship=flow.title_ship_ticks,rng=flow.title_ambient_rng;
    flow.title_variant=0u;CHECK(ticks(&flow,0u,10u));
    CHECK(flow.title_ship_ticks==ship&&flow.title_ambient_rng==rng);
    flow.title_variant=2u;flow.page=PORTABLE_FRONTEND_PAGE_MODE;
    CHECK(ticks(&flow,0u,10u));CHECK(flow.title_ship_ticks==ship);
    return 0;
}

static int test_validation(void)
{
    PortableFrontendFlow flow;
    PortableFrontendConfig bad = config;
    PortableFrontendDriverSlot bad_drivers[9];

    CHECK(!portable_frontend_flow_init(NULL, &config));
    CHECK(!portable_frontend_flow_init(&flow, NULL));
    bad.driver_count = 8u;
    CHECK(!portable_frontend_flow_init(&flow, &bad));
    memcpy(bad_drivers, drivers, sizeof bad_drivers);
    bad_drivers[4].content_id = 9u;
    bad = config;
    bad.drivers = bad_drivers;
    CHECK(!portable_frontend_flow_init(&flow, &bad));
    CHECK(portable_frontend_flow_init(&flow, &config));
    CHECK(!portable_frontend_flow_tick(&flow, UINT16_C(0x8000)));
    return 0;
}

static int test_short_transitions(void)
{
    PortableFrontendFlow original,shortened;
    CHECK(portable_frontend_flow_init(&original,&config));
    CHECK(portable_frontend_flow_settle_title(&original));
    shortened=original;shortened.short_transitions=1u;
    CHECK(portable_frontend_flow_tick(&original,PORTABLE_FRONTEND_BUTTON_A));
    CHECK(portable_frontend_flow_tick(&shortened,PORTABLE_FRONTEND_BUTTON_A));
    CHECK(original.transition_ticks==PORTABLE_FRONTEND_TITLE_MODE_TICKS&&original.transition_out_ticks==18u);
    CHECK(shortened.transition_ticks==1u&&shortened.transition_out_ticks==0u);
    CHECK(portable_frontend_flow_tick(&shortened,0u));
    CHECK(shortened.page==PORTABLE_FRONTEND_PAGE_MODE&&shortened.transition_ticks==0u);
    CHECK(original.page==PORTABLE_FRONTEND_PAGE_TITLE_MENU);
    /* Default lock consumes input without moving or queuing it. */
    uint8_t cursor=original.mode_cursor;
    CHECK(portable_frontend_flow_tick(&original,PORTABLE_FRONTEND_BUTTON_RIGHT));
    CHECK(original.mode_cursor==cursor&&original.page==PORTABLE_FRONTEND_PAGE_TITLE_MENU);
    CHECK(settle(&original));
    CHECK(tap(&original,PORTABLE_FRONTEND_BUTTON_A));
    CHECK(original.mode_phase==1u&&original.pending_page==PORTABLE_FRONTEND_PAGE_MODE&&original.transition_ticks>1u);
    cursor=original.race_speed;
    CHECK(portable_frontend_flow_tick(&original,PORTABLE_FRONTEND_BUTTON_RIGHT));
    CHECK(original.race_speed==cursor);
    /* Applying the live option finishes an already running animation. */
    original.short_transitions=1u;
    CHECK(portable_frontend_flow_tick(&original,0u));
    CHECK(original.transition_ticks==0u);
    CHECK(tap(&shortened,PORTABLE_FRONTEND_BUTTON_A)&&shortened.mode_phase==1u);
    CHECK(shortened.transition_ticks==0u); /* tap includes its release turn. */
    /* Short Transitions removes presentation-only title locks too. */
    CHECK(portable_frontend_flow_init(&shortened,&config));shortened.short_transitions=1u;
    CHECK(tap(&shortened,PORTABLE_FRONTEND_BUTTON_A));
    CHECK(shortened.page==PORTABLE_FRONTEND_PAGE_TITLE_MENU);
    CHECK(tap(&shortened,PORTABLE_FRONTEND_BUTTON_A));
    CHECK(shortened.page==PORTABLE_FRONTEND_PAGE_MODE);
    return 0;
}

static int test_title_and_repeat(void)
{
    PortableFrontendFlow flow;
    size_t before;
    CHECK(portable_frontend_flow_init(&flow, &config));
    {
        PortableFrontendFlow settled=flow;
        CHECK(portable_frontend_flow_settle_title(&settled));
        CHECK(settled.page==PORTABLE_FRONTEND_PAGE_TITLE_MENU&&
              settled.page_ticks==70u);
        CHECK(!portable_frontend_flow_settle_title(&settled));
        CHECK(tap(&settled,PORTABLE_FRONTEND_BUTTON_DOWN));
        CHECK(settled.title_cursor==1u);
    }
    CHECK(ticks(&flow, 0u, 31u));
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_TITLE_INTRO);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_A));
    CHECK(settle(&flow));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_TITLE_MENU);
    CHECK(ticks(&flow, 0u, 70u));

    CHECK(portable_frontend_flow_tick(&flow, PORTABLE_FRONTEND_BUTTON_DOWN));
    CHECK(flow.title_cursor == 1u);
    CHECK(ticks(&flow, PORTABLE_FRONTEND_BUTTON_DOWN, 23u));
    CHECK(flow.title_cursor == 1u);
    CHECK(portable_frontend_flow_tick(&flow, PORTABLE_FRONTEND_BUTTON_DOWN));
    CHECK(flow.title_cursor == 2u);
    CHECK(portable_frontend_flow_tick(&flow, 0u));
    {
        /* Ghost exchange opens the Time Trial records and backs out to the
         * Time Trial page. */
        PortableFrontendFlow exchange=flow;
        CHECK(tap(&exchange, PORTABLE_FRONTEND_BUTTON_A) && settle(&exchange));
        CHECK(exchange.page == PORTABLE_FRONTEND_PAGE_RECORDS && exchange.mode_cursor == 1u &&
              exchange.players == 1u);
        CHECK(tap(&exchange, PORTABLE_FRONTEND_BUTTON_B) && settle(&exchange));
        CHECK(exchange.page == PORTABLE_FRONTEND_PAGE_TIME_TRIAL && exchange.time_trial_cursor == 1u);
    }
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_DOWN) && flow.title_cursor == 3u);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_DOWN) && flow.title_cursor == 3u);
    /* Options is the shell's own row; the flow alone reports it unavailable. */
    before = flow.event_count;
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_TITLE_MENU);
    CHECK(flow.event_count == before + 1u);
    CHECK(flow.events[flow.event_count - 1u].type ==
          PORTABLE_FRONTEND_EVENT_UNAVAILABLE);
    return 0;
}

static int test_route_and_topology(void)
{
    PortableFrontendFlow flow;
    CHECK(portable_frontend_flow_init(&flow, &config));
    CHECK(reach_character(&flow));
    CHECK(flow.character_cursor == 4u);
    {
        const size_t before = flow.event_count;
        CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_SELECT) && flow.event_count == before);
    }
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_UP));
    CHECK(flow.character_cursor == 1u);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_SELECT));
    CHECK(flow.events[flow.event_count-1u].type == PORTABLE_FRONTEND_EVENT_HORN &&
          flow.events[flow.event_count-1u].value == 1u);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_LEFT));
    CHECK(flow.character_cursor == 0u);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_LEFT));
    CHECK(flow.character_cursor == 2u);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_RIGHT));
    CHECK(flow.character_cursor == 0u);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_START));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_CHARACTER &&
          flow.character_locked == 0u);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_CHARACTER &&
          flow.character_locked == 1u && flow.selected_character == 1u);
    CHECK(flow.events[flow.event_count-1u].type ==
          PORTABLE_FRONTEND_EVENT_CHARACTER_LOCK);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_RIGHT));
    CHECK(flow.character_cursor == 0u);
    {
        PortableFrontendFlow confirm=flow;
        CHECK(tap(&confirm, PORTABLE_FRONTEND_BUTTON_A) && settle(&confirm) &&
              confirm.page == PORTABLE_FRONTEND_PAGE_COURSE);
    }
    {
        /* A locked driver starts on its own kart and turns through the list. */
        PortableFrontendFlow karts=flow;
        const PortableFrontendKartSlot slots[3]={{11u,2u,1u},{12u,1u,0u},{13u,1u,1u}};
        CHECK(!portable_frontend_flow_set_karts(&karts,slots,PORTABLE_FRONTEND_KART_CAPACITY+1u));
        CHECK(tap(&karts, PORTABLE_FRONTEND_BUTTON_B) && !karts.character_locked);
        CHECK(portable_frontend_flow_set_karts(&karts,slots,3u));
        karts.kart_choice=1u;
        CHECK(tap(&karts, PORTABLE_FRONTEND_BUTTON_A) && karts.selected_kart==13u);
        CHECK(tap(&karts, PORTABLE_FRONTEND_BUTTON_RIGHT) && karts.selected_kart==11u &&
              karts.character_cursor==0u);
        CHECK(tap(&karts, PORTABLE_FRONTEND_BUTTON_LEFT) && karts.selected_kart==13u);
        CHECK(tap(&karts, PORTABLE_FRONTEND_BUTTON_B) && karts.selected_kart==0u);
    }
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_START));
    CHECK(settle(&flow));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_COURSE);
    CHECK(flow.selected_character == 1u);

    CHECK(flow.course_phase == 0u);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.course_phase == 1u);
    CHECK(flow.events[flow.event_count - 1u].type ==
          PORTABLE_FRONTEND_EVENT_OPEN);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_DOWN));
    CHECK(flow.course_track == 1u);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_DOWN));
    CHECK(flow.course_track == 2u);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_COURSE);
    CHECK(flow.events[flow.event_count - 1u].type ==
          PORTABLE_FRONTEND_EVENT_UNAVAILABLE);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_UP));
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_COURSE &&
          flow.course_phase == 2u && flow.selected_course == 25u);
    CHECK(flow.events[flow.event_count - 1u].type ==
          PORTABLE_FRONTEND_EVENT_OPEN);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.pending_page == PORTABLE_FRONTEND_PAGE_RACE);
    CHECK(flow.selected_course == 25u);
    CHECK(settle(&flow));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_RACE);
    {
        PortableFrontendFlow cancelled = flow;
        CHECK(portable_frontend_flow_cancel_race(&cancelled));
        CHECK(cancelled.page == PORTABLE_FRONTEND_PAGE_COURSE);
        CHECK(cancelled.events[cancelled.event_count - 2u].type ==
              PORTABLE_FRONTEND_EVENT_RACE_CANCEL);
        CHECK(cancelled.events[cancelled.event_count - 1u].type ==
              PORTABLE_FRONTEND_EVENT_PAGE_ENTER);
        CHECK(!portable_frontend_flow_cancel_race(&cancelled));
        cancelled = flow;
        CHECK(portable_frontend_flow_cancel_race_to(
            &cancelled, PORTABLE_FRONTEND_PAGE_CHARACTER));
        CHECK(cancelled.page == PORTABLE_FRONTEND_PAGE_CHARACTER);
        cancelled = flow;
        CHECK(portable_frontend_flow_cancel_race_to(
            &cancelled, PORTABLE_FRONTEND_PAGE_MODE));
        CHECK(cancelled.page == PORTABLE_FRONTEND_PAGE_MODE);
        cancelled = flow;
        CHECK(!portable_frontend_flow_cancel_race_to(
            &cancelled, PORTABLE_FRONTEND_PAGE_RESULTS));
    }
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_START));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_PAUSE && flow.paused == 1u);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_B));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_PAUSE && flow.paused == 1u);
    CHECK(ticks(&flow, 0u, 9u));
    CHECK(flow.pause_menu.open_ticks == PORTABLE_PAUSE_MENU_OPEN_TICKS);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_RACE && flow.paused == 0u);
    CHECK(portable_frontend_flow_finish_race(&flow, 8085u));
    CHECK(settle(&flow));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_RESULTS &&
          flow.result_ticks == 8085u);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_B));
    CHECK(settle(&flow));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_COURSE);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_B));
    CHECK(settle(&flow));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_CHARACTER);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_B));
    CHECK(settle(&flow));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_TIME_TRIAL);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_B));
    CHECK(settle(&flow));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_MODE);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_B));
    CHECK(settle(&flow));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_TITLE_INTRO);
    return 0;
}

static int test_gp_route(void)
{
    PortableFrontendFlow flow;
    CHECK(portable_frontend_flow_init(&flow, &config));
    CHECK(reach_mode(&flow));
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_MODE && flow.mode_phase == 1u);
    CHECK(flow.events[flow.event_count - 1u].type ==
          PORTABLE_FRONTEND_EVENT_OPEN);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_RIGHT));
    CHECK(flow.race_speed == 1u);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.pending_page == PORTABLE_FRONTEND_PAGE_CHARACTER);
    CHECK(settle(&flow));
    CHECK(flow.race_speed == 1u);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_B));
    CHECK(settle(&flow));
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_RIGHT));
    CHECK(flow.race_speed == 2u);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.pending_page == PORTABLE_FRONTEND_PAGE_CHARACTER);
    CHECK(settle(&flow));
    CHECK(flow.race_speed == 2u);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_B));
    CHECK(settle(&flow));
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_B));
    CHECK(flow.mode_phase == 0u && flow.mode_cursor == 0u);

    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.mode_phase == 1u && flow.race_speed == 0u);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.pending_page == PORTABLE_FRONTEND_PAGE_CHARACTER);
    CHECK(settle(&flow));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_CHARACTER);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_B));
    CHECK(settle(&flow));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_MODE && flow.mode_phase == 1u);

    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_A));
    CHECK(settle(&flow));
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_UP));
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_LEFT));
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_A));
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_START));
    CHECK(settle(&flow));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_COURSE &&
          flow.course_phase == 0u);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.course_phase==1u&&flow.pending_page==0u);
    CHECK(tap(&flow, PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.pending_page == PORTABLE_FRONTEND_PAGE_RACE &&
          flow.selected_course == 24u);
    CHECK(flow.events[flow.event_count - 1u].type ==
          PORTABLE_FRONTEND_EVENT_RACE_LAUNCH);
    CHECK(settle(&flow));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_RACE);
    CHECK(portable_frontend_flow_cancel_race(&flow));
    CHECK(flow.page == PORTABLE_FRONTEND_PAGE_COURSE &&
          flow.course_phase == 0u);
    return 0;
}

static size_t append(PortableFrontendReplayFrame *frames, size_t at,
                     uint16_t buttons, unsigned count)
{
    while (count-- != 0u)
        frames[at++] = (PortableFrontendReplayFrame){buttons, 0u, 0u};
    return at;
}

static size_t append_tap(PortableFrontendReplayFrame *frames, size_t at,
                         uint16_t button)
{
    frames[at++] = (PortableFrontendReplayFrame){button, 0u, 0u};
    frames[at++] = (PortableFrontendReplayFrame){0u, 0u, 0u};
    return at;
}

static int test_deterministic_replay(void)
{
    PortableFrontendReplayFrame frames[1536] = {{0}};
    PortableFrontendFlow left;
    PortableFrontendFlow right;
    size_t count = 0u;
    uint32_t checksum;

    count = append(frames, count, 0u, 32u);
    count = append_tap(frames, count, PORTABLE_FRONTEND_BUTTON_A);
    count = append(frames, count, 0u,
                   28u + 70u);
    count = append_tap(frames, count, PORTABLE_FRONTEND_BUTTON_A);
    count = append(frames, count, 0u, PORTABLE_FRONTEND_TITLE_MODE_TICKS);
    count = append_tap(frames, count, PORTABLE_FRONTEND_BUTTON_RIGHT);
    count = append_tap(frames, count, PORTABLE_FRONTEND_BUTTON_A);
    count = append(frames, count, 0u, PORTABLE_FRONTEND_MODE_TRIAL_TICKS);
    count = append_tap(frames, count, PORTABLE_FRONTEND_BUTTON_A);
    count = append(frames, count, 0u,
                   PORTABLE_FRONTEND_TRIAL_CHARACTER_TICKS);
    count = append_tap(frames, count, PORTABLE_FRONTEND_BUTTON_UP);
    count = append_tap(frames, count, PORTABLE_FRONTEND_BUTTON_LEFT);
    count = append_tap(frames, count, PORTABLE_FRONTEND_BUTTON_A);
    count = append_tap(frames, count, PORTABLE_FRONTEND_BUTTON_START);
    count = append(frames, count, 0u,
                   PORTABLE_FRONTEND_CHARACTER_COURSE_TICKS);
    count = append_tap(frames, count, PORTABLE_FRONTEND_BUTTON_A);
    count = append_tap(frames, count, PORTABLE_FRONTEND_BUTTON_A);
    count = append_tap(frames, count, PORTABLE_FRONTEND_BUTTON_A);
    count = append(frames, count, 0u,
                   PORTABLE_FRONTEND_COURSE_RACE_TICKS);
    frames[count++] = (PortableFrontendReplayFrame){
        0u, PORTABLE_FRONTEND_REPLAY_FINISH, 8085u};
    count = append(frames, count, 0u,
                   PORTABLE_FRONTEND_SHORT_TRANSITION_TICKS);

    CHECK(portable_frontend_flow_init(&left, &config));
    CHECK(portable_frontend_flow_init(&right, &config));
    CHECK(portable_frontend_flow_replay(&left, frames, count));
    CHECK(portable_frontend_flow_replay(&right, frames, count));
    checksum = portable_frontend_flow_checksum(&left);
    CHECK(checksum != 0u && checksum == portable_frontend_flow_checksum(&right));
    CHECK(left.page == PORTABLE_FRONTEND_PAGE_RESULTS);
    frames[count - 17u].value = 8086u;
    CHECK(portable_frontend_flow_init(&right, &config));
    CHECK(portable_frontend_flow_replay(&right, frames, count));
    CHECK(checksum != portable_frontend_flow_checksum(&right));
    return 0;
}

static int test_cup_access(void)
{
    PortableFrontendFlow flow;
    uint8_t access[3]={15u,31u,15u};
    CHECK(portable_frontend_flow_init(&flow,&config));
    CHECK(portable_frontend_flow_set_cup_access(&flow,access));
    flow.page=PORTABLE_FRONTEND_PAGE_COURSE;flow.mode_cursor=0u;
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_LEFT)&&flow.course_cup==3u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT)&&flow.course_cup==0u);
    flow.race_speed=1u;
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_LEFT)&&flow.course_cup==4u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_phase==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.selected_course==40u);
    flow.transition_ticks=0u;flow.page=PORTABLE_FRONTEND_PAGE_COURSE;
    flow.course_phase=0u;flow.cup_motion_phase=flow.cup_motion_tick=0u;
    flow.mode_cursor=1u;
    for(uint8_t speed=0u;speed<3u;++speed){
        flow.race_speed=speed;
        CHECK(portable_frontend_flow_cup_access(&flow)==15u);
    }
    CHECK(portable_frontend_flow_set_cup_access(&flow,access)&&flow.course_cup==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_LEFT)&&flow.course_cup==3u);
    flow.course_cup=4u;
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&
          flow.events[flow.event_count-1u].type==PORTABLE_FRONTEND_EVENT_UNAVAILABLE&&
          flow.transition_ticks==0u);
    access[2]=31u;
    CHECK(portable_frontend_flow_set_cup_access(&flow,access));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_phase==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_phase==2u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.selected_course==40u&&
          flow.pending_page==PORTABLE_FRONTEND_PAGE_RACE);
    access[1]=0u;
    CHECK(!portable_frontend_flow_set_cup_access(&flow,access));
    return 0;
}

static int test_kart_tricks(void)
{
    PortableFrontendFlow flow;
    CHECK(portable_frontend_flow_init(&flow,&config));
    flow.page=PORTABLE_FRONTEND_PAGE_CHARACTER;flow.character_cursor=0u;
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_R)&&
          flow.events[flow.event_count-1u].type==PORTABLE_FRONTEND_EVENT_KART_TRICK&&
          flow.events[flow.event_count-1u].value==0u);
    {
        const size_t before=flow.event_count;
        CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_L)&&flow.event_count==before);
    }
    for(unsigned i=0u;i<16u;++i)CHECK(portable_frontend_flow_tick(&flow,0u));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_L)&&
          flow.events[flow.event_count-1u].value==1u);
    return 0;
}

static int test_extra_courses(void)
{
    const PortableFrontendCourseSlot extra_courses[]={
        {24u,0u,0u,1u},{64u,5u,0u,1u},{83u,9u,3u,1u}};
    const PortableFrontendConfig extra_config={drivers,9u,extra_courses,3u};
    PortableFrontendFlow flow;
    uint8_t masks[3]={31u,31u,31u},extra[3]={31u,31u,0u};
    CHECK(portable_frontend_flow_init(&flow,&extra_config));
    CHECK(portable_frontend_flow_set_cup_access(&flow,masks));
    CHECK(portable_frontend_flow_set_extra_cup_access(&flow,extra));
    flow.page=PORTABLE_FRONTEND_PAGE_COURSE;flow.mode_cursor=1u;
    {
        const size_t before=flow.event_count;
        CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_L)&&flow.course_cup==0u&&
              flow.event_count==before);
    }
    extra[2]=17u;
    CHECK(portable_frontend_flow_set_extra_cup_access(&flow,extra));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_R)&&flow.course_cup==5u&&
          flow.events[flow.event_count-1u].type==PORTABLE_FRONTEND_EVENT_TRACK_SET);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_LEFT)&&flow.course_cup==9u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT)&&flow.course_cup==5u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_phase==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT)&&flow.course_cup==5u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_phase==2u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.selected_course==64u&&
          flow.pending_page==PORTABLE_FRONTEND_PAGE_RACE);
    flow.transition_ticks=0u;flow.page=PORTABLE_FRONTEND_PAGE_COURSE;flow.course_phase=0u;
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT)&&flow.course_cup==0u);
    flow.mode_cursor=0u;flow.race_speed=0u;
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT)&&flow.course_cup==5u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_phase==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.selected_course==64u&&
          flow.pending_page==PORTABLE_FRONTEND_PAGE_RACE);
    flow.transition_ticks=0u;flow.page=PORTABLE_FRONTEND_PAGE_COURSE;
    flow.course_phase=0u;flow.course_cup=0u;flow.race_speed=2u;
    extra[2]=0u;
    CHECK(portable_frontend_flow_set_extra_cup_access(&flow,extra));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT)&&flow.course_cup==0u);
    flow.mode_cursor=1u;extra[2]=17u;
    CHECK(portable_frontend_flow_set_extra_cup_access(&flow,extra));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT)&&flow.course_cup==5u);
    extra[2]=0u;
    CHECK(portable_frontend_flow_set_extra_cup_access(&flow,extra)&&flow.course_cup==0u);
    extra[2]=32u;
    CHECK(!portable_frontend_flow_set_extra_cup_access(&flow,extra));
    return 0;
}

static int test_records_route(void)
{
    PortableFrontendFlow flow;
    CHECK(portable_frontend_flow_init(&flow,&config));
    CHECK(reach_character(&flow));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B)&&settle(&flow));
    CHECK(flow.page==PORTABLE_FRONTEND_PAGE_TIME_TRIAL);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&settle(&flow));
    /* 0800C78C opens straight on its course list, first row selected. */
    uint8_t entries[PORTABLE_FRONTEND_RECORDS_MAX];
    CHECK(flow.page==PORTABLE_FRONTEND_PAGE_RECORDS&&flow.course_phase==2u&&
          flow.selected_course==24u&&flow.records_top==0u);
    const unsigned listed=portable_frontend_flow_records_list(&flow,entries);
    CHECK(listed>=2u&&entries[0]==0u&&entries[1]==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.pending_page==0u&&flow.course_phase==4u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT)&&flow.course_phase==5u&&flow.record_erase_choice==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_phase==4u&&
        flow.events[flow.event_count-1u].type==PORTABLE_FRONTEND_EVENT_BACK);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B)&&flow.course_phase==4u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT)&&flow.record_erase_choice==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_phase==4u&&
        flow.events[flow.event_count-1u].type==PORTABLE_FRONTEND_EVENT_GHOST_ERASE&&
        flow.events[flow.event_count-1u].value==24u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B)&&flow.course_phase==2u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT)&&flow.course_phase==3u&&
        flow.record_erase_choice==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_phase==2u&&
        flow.events[flow.event_count-1u].type==PORTABLE_FRONTEND_EVENT_BACK);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT)&&flow.record_erase_choice==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B)&&flow.course_phase==2u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT)&&flow.record_erase_choice==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_phase==2u);
    CHECK(flow.events[flow.event_count-1u].type==PORTABLE_FRONTEND_EVENT_RECORD_ERASE&&
        flow.events[flow.event_count-1u].value==24u);
    /* Up/Down walk the list and stop at its ends; L/R skip empty sets. */
    size_t before=flow.event_count;
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_UP)&&flow.selected_course==24u&&flow.event_count==before);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_DOWN)&&flow.selected_course==25u&&
        flow.events[flow.event_count-1u].type==PORTABLE_FRONTEND_EVENT_MOVE);
    for(unsigned step=2u;step<listed+2u;++step)CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_DOWN));
    CHECK(flow.course_cup*4u+flow.course_track==entries[listed-1u]&&flow.course_phase==2u);
    before=flow.event_count;
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_R)&&tap(&flow,PORTABLE_FRONTEND_BUTTON_L)&&
        flow.event_count==before&&flow.course_cup*4u+flow.course_track==entries[listed-1u]);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_UP));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B)&&settle(&flow)&&
        flow.page==PORTABLE_FRONTEND_PAGE_TIME_TRIAL&&flow.time_trial_cursor==1u);
    /* Reopening keeps the course the list was left on. */
    const uint8_t left=(uint8_t)(flow.course_cup*4u+flow.course_track);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&settle(&flow)&&flow.page==PORTABLE_FRONTEND_PAGE_RECORDS&&
        flow.course_cup*4u+flow.course_track==left&&flow.course_phase==2u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B)&&settle(&flow));
    for(size_t i=0u;i<flow.event_count;++i)
        CHECK(flow.events[i].type!=PORTABLE_FRONTEND_EVENT_RACE_LAUNCH);
    return 0;
}

static int test_received_ghost_route(void)
{
    PortableFrontendFlow flow;
    CHECK(portable_frontend_flow_init(&flow,&config)&&reach_character(&flow));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B)&&settle(&flow));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&settle(&flow));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.course_phase==4u&&flow.record_ghost_slot==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.events[flow.event_count-1].type==PORTABLE_FRONTEND_EVENT_GHOST_EXPORT);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_LEFT)&&flow.record_ghost_slot==2u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT)&&flow.record_ghost_slot==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT)&&flow.record_ghost_slot==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_phase==6u&&!flow.record_erase_choice);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_phase==4u&&flow.events[flow.event_count-1].type==PORTABLE_FRONTEND_EVENT_BACK);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&!flow.record_erase_choice);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B)&&flow.course_phase==4u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&!flow.record_erase_choice);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_phase==4u&&
        flow.events[flow.event_count-1].type==PORTABLE_FRONTEND_EVENT_GHOST_RECEIVE&&flow.events[flow.event_count-1].value==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT)&&flow.course_phase==5u&&!flow.record_erase_choice);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.events[flow.event_count-1].type==PORTABLE_FRONTEND_EVENT_RECEIVED_GHOST_ERASE&&flow.events[flow.event_count-1].value==1u);
    return 0;
}

static int test_ghost_race_selection(void)
{
    PortableFrontendFlow flow;
    CHECK(portable_frontend_flow_init(&flow,&config)&&reach_character(&flow));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_START)&&settle(&flow));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_phase==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_phase==2u);
    CHECK(!portable_frontend_flow_set_ghost_choices(NULL,1u)&&!portable_frontend_flow_set_ghost_choices(&flow,8u));
    CHECK(portable_frontend_flow_set_ghost_choices(&flow,6u)&&flow.ghost_choice==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_DOWN)&&flow.ghost_choice==2u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_DOWN)&&flow.ghost_choice==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_UP)&&flow.ghost_choice==2u);
    CHECK(portable_frontend_flow_set_ghost_choices(&flow,3u)&&flow.ghost_choice==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_UP)&&flow.ghost_choice==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B)&&flow.course_phase==1u&&flow.ghost_choices==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_phase==2u);
    CHECK(portable_frontend_flow_set_ghost_choices(&flow,4u)&&flow.ghost_choice==2u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&settle(&flow)&&flow.page==PORTABLE_FRONTEND_PAGE_RACE&&flow.ghost_choice==2u);
    CHECK(portable_frontend_flow_cancel_race(&flow)&&flow.ghost_choices==0u);
    return 0;
}

static int test_quick_run_route(void)
{
    PortableFrontendFlow flow;
    CHECK(portable_frontend_flow_init(&flow,&config) && reach_mode(&flow));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT) && !flow.quick_options_open);
    /* 42C4 rows wrap: Up/Left step back, Down/Right forward. */
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_UP) && flow.mode_cursor==3u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_DOWN) && flow.mode_cursor==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && flow.mode_phase==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_LEFT) && flow.race_speed==2u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_DOWN) && flow.race_speed==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B) && flow.mode_phase==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT) && flow.mode_cursor==1u);
    {
        PortableFrontendFlow trial=flow;
        CHECK(tap(&trial,PORTABLE_FRONTEND_BUTTON_A) && settle(&trial) &&
              trial.page==PORTABLE_FRONTEND_PAGE_TIME_TRIAL && trial.time_trial_cursor==0u);
        CHECK(tap(&trial,PORTABLE_FRONTEND_BUTTON_UP) && trial.time_trial_cursor==1u);
        CHECK(tap(&trial,PORTABLE_FRONTEND_BUTTON_RIGHT) && trial.time_trial_cursor==0u);
    }
    /* Time Trial's GHOST DISPLAY window: two wrapping rows of ON/OFF. */
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT) && flow.quick_options_open);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_UP) && flow.quick_options_cursor==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT) && flow.ghost_display_off==2u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_DOWN) && flow.quick_options_cursor==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_LEFT) && flow.ghost_display_off==3u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && !flow.quick_options_open && flow.mode_phase==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT) && flow.mode_cursor==2u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT) && flow.quick_options_open);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_UP) && flow.quick_options_cursor==2u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_LEFT) && flow.quick_run_options.coins_off);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_DOWN) && flow.quick_options_cursor==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT) && flow.quick_run_options.five_laps);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_DOWN) && flow.quick_options_cursor==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_LEFT) && flow.quick_run_options.items_off);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B) && !flow.quick_options_open && flow.mode_phase==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT) && flow.quick_options_cursor==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_START) && !flow.quick_options_open);

    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && flow.mode_phase==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT) && flow.race_speed==2u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && settle(&flow));
    CHECK(flow.page==PORTABLE_FRONTEND_PAGE_CHARACTER);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B) && settle(&flow));
    CHECK(flow.page==PORTABLE_FRONTEND_PAGE_MODE && flow.mode_cursor==2u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && settle(&flow));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_START) && settle(&flow));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && flow.course_phase==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_DOWN) && flow.course_track==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && flow.course_phase==2u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && settle(&flow));
    CHECK(flow.page==PORTABLE_FRONTEND_PAGE_RACE && flow.selected_course==25u);
    CHECK(portable_frontend_flow_cancel_race(&flow));
    CHECK(flow.page==PORTABLE_FRONTEND_PAGE_COURSE && flow.mode_cursor==2u);
    CHECK(flow.quick_run_options.five_laps && flow.quick_run_options.items_off &&
        flow.quick_run_options.coins_off && !flow.quick_options_open);
    return 0;
}

static int test_battle_route(void)
{
    PortableFrontendCourseSlot slots[4]={{99u,PORTABLE_FRONTEND_ARENA_CUP,0u,1u},
        {97u,PORTABLE_FRONTEND_ARENA_CUP,1u,1u},{96u,PORTABLE_FRONTEND_ARENA_CUP,2u,1u},
        {98u,PORTABLE_FRONTEND_ARENA_CUP,3u,1u}};
    const PortableFrontendConfig battle={drivers,9u,slots,4u};
    PortableFrontendFlow flow;
    CHECK(portable_frontend_flow_init(&flow,&battle));
    CHECK(reach_mode(&flow));
    for(unsigned mode=0u;mode<3u;++mode)CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT));
    CHECK(flow.mode_cursor==3u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && flow.mode_phase==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && settle(&flow));
    CHECK(flow.page==PORTABLE_FRONTEND_PAGE_CHARACTER);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && tap(&flow,PORTABLE_FRONTEND_BUTTON_START) && settle(&flow));
    CHECK(flow.page==PORTABLE_FRONTEND_PAGE_COURSE && flow.course_phase==1u && flow.course_cup==PORTABLE_FRONTEND_ARENA_CUP);
    for(unsigned arena=0u;arena<4u;++arena) {
        CHECK(flow.course_track==arena);
        CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && flow.course_phase==2u);
        CHECK(flow.selected_course==slots[arena].content_id);
        CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B) && flow.course_phase==1u);
        CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_DOWN));
    }
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && settle(&flow));
    CHECK(flow.page==PORTABLE_FRONTEND_PAGE_RACE && flow.selected_course==99u);
    CHECK(portable_frontend_flow_cancel_race(&flow));
    CHECK(flow.page==PORTABLE_FRONTEND_PAGE_COURSE && flow.course_phase==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B) && settle(&flow));
    CHECK(flow.page==PORTABLE_FRONTEND_PAGE_CHARACTER);
    return 0;
}

static int test_custom_courses(void)
{
    const PortableFrontendCourseSlot custom_courses[]={
        {24u,0u,0u,1u},{64u,5u,0u,1u},
        {128u,11u,0u,1u},{129u,11u,1u,1u},{130u,11u,2u,1u},{131u,11u,3u,1u},
        {140u,13u,0u,1u}};
    const PortableFrontendConfig custom_config={drivers,9u,custom_courses,7u};
    PortableFrontendFlow flow;
    uint8_t masks[3]={1u,1u,1u},extra[3]={0u,0u,0u};
    CHECK(portable_track_set_of_cup(10u)==PORTABLE_TRACK_SET_ARENA&&
          portable_track_set_of_cup(15u)==PORTABLE_TRACK_SET_CUSTOM&&
          portable_track_set_of_cup(16u)==PORTABLE_TRACK_SET_COUNT);
    CHECK(portable_frontend_flow_init(&flow,&custom_config));
    CHECK(portable_frontend_flow_set_cup_access(&flow,masks));
    CHECK(portable_frontend_flow_set_extra_cup_access(&flow,extra));
    flow.page=PORTABLE_FRONTEND_PAGE_COURSE;flow.mode_cursor=0u;
    /* Locked Extra is skipped; custom cups ignore saved unlocks. GP needs a
     * complete cup, so the partial cup 13 stays unavailable. */
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_R)&&flow.course_cup==PORTABLE_FRONTEND_CUSTOM_CUP&&
          flow.events[flow.event_count-1u].type==PORTABLE_FRONTEND_EVENT_TRACK_SET&&
          flow.events[flow.event_count-1u].value==44u);
    CHECK(portable_frontend_flow_cup_access(&flow)==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT)&&flow.course_cup==PORTABLE_FRONTEND_CUSTOM_CUP);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_R)&&flow.course_cup==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_L)&&flow.course_cup==PORTABLE_FRONTEND_CUSTOM_CUP);
    extra[0]=1u;
    CHECK(portable_frontend_flow_set_extra_cup_access(&flow,extra)&&
          flow.course_cup==PORTABLE_FRONTEND_CUSTOM_CUP);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT)&&flow.course_cup==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT)&&flow.course_cup==5u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT)&&flow.course_cup==PORTABLE_FRONTEND_CUSTOM_CUP);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_L)&&flow.course_cup==5u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_L)&&flow.course_cup==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_L)&&flow.course_cup==PORTABLE_FRONTEND_CUSTOM_CUP);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_phase==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.selected_course==128u&&
          flow.pending_page==PORTABLE_FRONTEND_PAGE_RACE);
    /* Other modes accept a cup with any custom course. */
    flow.transition_ticks=0u;flow.page=PORTABLE_FRONTEND_PAGE_COURSE;flow.course_phase=0u;
    flow.mode_cursor=1u;
    CHECK(portable_frontend_flow_cup_access(&flow)==5u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT)&&flow.course_cup==PORTABLE_FRONTEND_CUSTOM_CUP+2u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_phase==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_phase==2u&&flow.selected_course==140u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B)&&flow.course_phase==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_DOWN)&&tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&
          flow.events[flow.event_count-1u].type==PORTABLE_FRONTEND_EVENT_UNAVAILABLE);
    /* Without a custom catalog the set never appears. */
    const PortableFrontendConfig stock={drivers,9u,custom_courses,2u};
    CHECK(portable_frontend_flow_init(&flow,&stock));
    extra[0]=0u;
    CHECK(portable_frontend_flow_set_extra_cup_access(&flow,extra));
    flow.page=PORTABLE_FRONTEND_PAGE_COURSE;
    {
        const size_t before=flow.event_count;
        CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_R)&&flow.course_cup==0u&&flow.event_count==before);
    }
    const PortableFrontendCourseSlot invalid[]={{24u,16u,0u,1u}};
    const PortableFrontendConfig invalid_config={drivers,9u,invalid,1u};
    CHECK(!portable_frontend_flow_init(&flow,&invalid_config));
    return 0;
}

/* Title row 1 picks the player count, then VS/Battle and one lock per player. */
static int test_multiplayer_route(void)
{
    PortableFrontendCourseSlot slots[4]={{99u,PORTABLE_FRONTEND_ARENA_CUP,0u,1u},
        {97u,PORTABLE_FRONTEND_ARENA_CUP,1u,1u},{96u,PORTABLE_FRONTEND_ARENA_CUP,2u,1u},
        {98u,PORTABLE_FRONTEND_ARENA_CUP,3u,1u}};
    const PortableFrontendConfig battle={drivers,9u,slots,4u};
    PortableFrontendFlow flow;
    CHECK(portable_frontend_flow_init(&flow,&battle) && flow.players==1u);
    flow.max_players=3u;
    CHECK(portable_frontend_flow_settle_title(&flow) && tap(&flow,PORTABLE_FRONTEND_BUTTON_DOWN));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && flow.player_count_open && flow.players==2u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B) && !flow.player_count_open && flow.players==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && tap(&flow,PORTABLE_FRONTEND_BUTTON_LEFT) && flow.players==2u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_UP) && flow.players==3u &&
          tap(&flow,PORTABLE_FRONTEND_BUTTON_UP) && flow.players==3u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_DOWN) && tap(&flow,PORTABLE_FRONTEND_BUTTON_DOWN) &&
          flow.players==2u && flow.player_count_open);
    for(unsigned i=0u;i<3u;++i)CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT));
    CHECK(flow.players==3u && tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && settle(&flow));
    CHECK(flow.page==PORTABLE_FRONTEND_PAGE_MODE && flow.mode_cursor==2u && flow.players==3u);
    /* The VS/Battle pair wraps in every direction, like 42C4's rows. */
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_LEFT) && flow.mode_cursor==3u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_UP) && flow.mode_cursor==2u);
    /* 58B0 action 2: VS keeps its own four rows; mushroom start is the third. */
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT) && flow.quick_options_open);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_UP) && flow.quick_options_cursor==3u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT) && flow.vs_options.coins_off);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_UP) && flow.quick_options_cursor==2u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_LEFT) && flow.vs_options.mushroom_start);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B) && !flow.quick_options_open && flow.mode_cursor==2u);
    CHECK(!flow.quick_run_options.coins_off && !flow.vs_options.five_laps);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_DOWN) && flow.mode_cursor==3u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT) && !flow.quick_options_open);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && flow.mode_phase==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && settle(&flow) && flow.page==PORTABLE_FRONTEND_PAGE_CHARACTER);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_START) && flow.page==PORTABLE_FRONTEND_PAGE_CHARACTER);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_UP) && tap(&flow,PORTABLE_FRONTEND_BUTTON_LEFT) &&
          flow.character_cursor==0u && tap(&flow,PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.character_player==1u && flow.player_characters[0]==1u && flow.selected_character==1u &&
          flow.character_cursor==PORTABLE_FRONTEND_RANDOM_SLOT && !flow.character_locked);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && flow.character_player==2u && flow.player_characters[1]!=0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B) && flow.character_player==1u && flow.player_characters[1]==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && flow.character_locked);
    for(unsigned p=0u;p<3u;++p)CHECK(flow.player_characters[p]!=0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_START) && settle(&flow) && flow.page==PORTABLE_FRONTEND_PAGE_COURSE);
    CHECK(flow.course_cup==PORTABLE_FRONTEND_ARENA_CUP && flow.course_phase==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && tap(&flow,PORTABLE_FRONTEND_BUTTON_A) && settle(&flow));
    CHECK(flow.page==PORTABLE_FRONTEND_PAGE_RACE && flow.selected_course==99u && flow.players==3u);
    CHECK(portable_frontend_flow_cancel_race_to(&flow,PORTABLE_FRONTEND_PAGE_MODE) && flow.players==3u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B) && settle(&flow) && flow.page==PORTABLE_FRONTEND_PAGE_TITLE_INTRO);
    CHECK(flow.players==1u);
    return 0;
}

static int test_gp_cup_confirmation_and_preview(void)
{
    PortableFrontendFlow flow;
    CHECK(portable_frontend_flow_init(&flow,&config));
    flow.page=PORTABLE_FRONTEND_PAGE_COURSE;flow.mode_cursor=0u;
    CHECK(ticks(&flow,0u,64u)&&flow.gp_preview_track==0u&&flow.course_track==0u);
    CHECK(ticks(&flow,0u,1u)&&flow.gp_preview_track==1u&&flow.gp_preview_ticks==0u);
    CHECK(portable_frontend_flow_tick(&flow,PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.course_phase==1u&&flow.cup_motion_phase==1u&&flow.cup_motion_tick==0u);
    CHECK(flow.selected_course==0u&&flow.transition_ticks==0u);
    CHECK(portable_frontend_flow_tick(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT));
    CHECK(flow.course_cup==0u&&flow.cup_motion_tick==1u);
    CHECK(ticks(&flow,0u,15u)&&flow.cup_motion_tick==16u);
    CHECK(portable_frontend_flow_tick(&flow,PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.cup_motion_phase==0u&&flow.pending_page==0u); /* terminal lock consumes A */
    CHECK(portable_frontend_flow_tick(&flow,0u));
    CHECK(portable_frontend_flow_tick(&flow,PORTABLE_FRONTEND_BUTTON_B));
    CHECK(flow.course_phase==0u&&flow.cup_motion_phase==2u);
    CHECK(ticks(&flow,0u,17u)&&flow.cup_motion_phase==0u);
    CHECK(portable_frontend_flow_tick(&flow,PORTABLE_FRONTEND_BUTTON_A));
    CHECK(ticks(&flow,0u,17u)&&flow.course_phase==1u);
    flow.gp_preview_track=3u;
    CHECK(portable_frontend_flow_tick(&flow,PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.pending_page==PORTABLE_FRONTEND_PAGE_RACE&&flow.selected_course==24u&&flow.course_track==0u);
    CHECK(flow.events[flow.event_count-1u].type==PORTABLE_FRONTEND_EVENT_RACE_LAUNCH);
    CHECK(portable_frontend_flow_init(&flow,&config));
    flow.page=PORTABLE_FRONTEND_PAGE_COURSE;flow.short_transitions=1u;
    CHECK(portable_frontend_flow_tick(&flow,PORTABLE_FRONTEND_BUTTON_A));
    CHECK(portable_frontend_flow_tick(&flow,0u)&&flow.cup_motion_phase==0u);
    CHECK(portable_frontend_flow_tick(&flow,PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.pending_page==PORTABLE_FRONTEND_PAGE_RACE);
    return 0;
}

static int test_title_cursor_motion(void)
{
    /* Invented Q15 samples isolate source widths, duration and retargeting. */
    int32_t samples[8];for(unsigned i=0u;i<8u;++i)samples[i]=(int32_t)(i+1u)*4096;
    PortableFrontendFlow flow;
    CHECK(portable_frontend_flow_init(&flow,&config));
    CHECK(portable_frontend_flow_settle_title(&flow));
    CHECK(portable_frontend_flow_set_title_cursor_sine(&flow,samples));
    portable_frontend_flow_consume_events(&flow);
    CHECK(portable_frontend_flow_tick(&flow,PORTABLE_FRONTEND_BUTTON_DOWN|PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.title_cursor==1u&&!flow.player_count_open&&!flow.transition_ticks);
    CHECK(flow.title_cursor_motion.y==132&&flow.title_cursor_motion.ticks==0u);
    for(unsigned tick=1u;tick<=8u;++tick){
        CHECK(portable_frontend_flow_tick(&flow,0u));
        CHECK(flow.title_cursor_motion.y==132-(int)tick*2);
        CHECK(flow.title_cursor_motion.phase==2u&&flow.title_cursor_motion.ticks==tick);
    }
    CHECK(portable_frontend_flow_tick(&flow,0u));
    CHECK(flow.title_cursor_motion.phase==0u&&flow.title_cursor_motion.ticks==0u);
    CHECK(portable_frontend_flow_tick(&flow,PORTABLE_FRONTEND_BUTTON_UP));
    CHECK(ticks(&flow,0u,4u)&&flow.title_cursor_motion.y==124);
    CHECK(portable_frontend_flow_tick(&flow,PORTABLE_FRONTEND_BUTTON_DOWN));
    CHECK(flow.title_cursor_motion.y==124&&flow.title_cursor_motion.ticks==0u);
    CHECK(portable_frontend_flow_tick(&flow,0u)&&flow.title_cursor_motion.y==123);
    /* Visual ownership never changes the v1/v2 navigation checksum contract. */
    PortableFrontendFlow other=flow;other.title_cursor_motion.y=97;
    other.title_cursor_motion.phase=0u;other.title_cursor_motion.sine_q15[0]=0;
    CHECK(portable_frontend_flow_checksum(&flow)==portable_frontend_flow_checksum(&other));
    flow.short_transitions=1u;
    CHECK(portable_frontend_flow_tick(&flow,0u)&&flow.title_cursor_motion.y==116);
    CHECK(flow.title_cursor_motion.phase==0u);
    samples[0]=32769;other=flow;
    CHECK(!portable_frontend_flow_set_title_cursor_sine(&flow,samples));
    CHECK(memcmp(&flow,&other,sizeof flow)==0);
    CHECK(portable_frontend_flow_set_title_cursor_sine(&flow,NULL));
    CHECK(!flow.title_cursor_motion.configured);
    CHECK(portable_frontend_flow_tick(&flow,PORTABLE_FRONTEND_BUTTON_UP|PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.title_cursor==0u&&!flow.transition_ticks);
    CHECK(portable_frontend_flow_tick(&flow,0u));
    CHECK(portable_frontend_flow_tick(&flow,PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.transition_ticks==1u);
    return 0;
}


/* 3D cups follow the 2D sets on L/R, one group per providing mod. */
static int test_mesh_cups(void)
{
    static const char *const groups[2]={"Mario Kart DS","Bridge"};
    PortableFrontendMeshCup cups[4];
    memset(cups,0,sizeof cups);
    cups[0]=(PortableFrontendMeshCup){"Mushroom Cup",0x4d4b4301u,{11u,12u,13u,14u},{15u,15u,15u,15u},
        {"A","B","C","D"},0u,4u,1u,0u};
    cups[1]=(PortableFrontendMeshCup){"Flower Cup",0x4d4b4302u,{21u,22u},{14u,2u},{"E","F"},0u,2u,0u,2u};
    cups[2]=(PortableFrontendMeshCup){"Battle Stages",0x4d4b4342u,{31u,32u,33u,34u,35u},{16u,16u,16u,16u,16u},
        {"G","H","I","J","K"},0u,5u,0u,0u};
    cups[3]=(PortableFrontendMeshCup){"",7u,{65536u},{2u},{"Bridge"},1u,1u,0u,0u};
    const PortableFrontendCourseSlot arena[]={{24u,0u,0u,1u},{99u,PORTABLE_FRONTEND_ARENA_CUP,0u,1u}};
    const PortableFrontendConfig mesh={drivers,9u,arena,2u};
    PortableFrontendFlow flow;
    CHECK(portable_frontend_flow_init(&flow,&mesh)&&portable_frontend_flow_set_mesh_cups(&flow,cups,4u,groups,NULL,2u));
    flow.page=PORTABLE_FRONTEND_PAGE_COURSE;flow.mode_cursor=0u;
    /* GP lists only the complete cup; L/R skips groups with nothing open. */
    CHECK(portable_frontend_flow_mesh_group_open(&flow,1u)&&!portable_frontend_flow_mesh_group_open(&flow,2u));
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_R)&&flow.mesh_group==1u&&flow.mesh_cup==0u&&
          flow.events[flow.event_count-1u].type==PORTABLE_FRONTEND_EVENT_TRACK_SET);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT)&&flow.mesh_cup==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_phase==1u&&flow.mesh_request==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_SELECT)&&flow.mesh_request==PORTABLE_FRONTEND_MESH_EXPLORE&&
          flow.mesh_request_course==11u);
    /* A pending request holds input until the platform takes it. */
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B)&&flow.course_phase==1u);
    flow.mesh_request=0u;
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.mesh_request==PORTABLE_FRONTEND_MESH_RACE&&
          flow.mesh_request_course==11u&&flow.page==PORTABLE_FRONTEND_PAGE_COURSE);
    flow.mesh_request=0u;
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B)&&flow.course_phase==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_L)&&flow.mesh_group==0u&&flow.course_cup==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_L)&&flow.mesh_group==1u);
    /* Time Trial: both race cups, rows filtered by mode; explore-only rows explore. */
    flow.mode_cursor=1u;flow.course_phase=0u;
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT)&&flow.mesh_cup==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT)&&flow.mesh_cup==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_LEFT)&&flow.mesh_cup==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_phase==1u&&flow.course_track==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_DOWN)&&flow.course_track==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_START)&&flow.mesh_request==PORTABLE_FRONTEND_MESH_EXPLORE&&
          flow.mesh_request_course==22u);
    flow.mesh_request=0u;
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B)&&tap(&flow,PORTABLE_FRONTEND_BUTTON_R)&&flow.mesh_group==2u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_R)&&flow.mesh_group==0u);
    /* Quick Run (one player, bit 4) skips TT-only rows. */
    flow.mode_cursor=2u;flow.course_phase=0u;
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_R)&&flow.mesh_group==1u&&flow.mesh_cup==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT)&&flow.mesh_cup==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.course_track==0u&&
          tap(&flow,PORTABLE_FRONTEND_BUTTON_DOWN)&&flow.course_track==0u);
    flow.course_phase=0u;flow.mesh_group=0u;
    /* Battle: the arena page reaches 3D arena cups from its course list. */
    flow.mode_cursor=3u;flow.course_cup=PORTABLE_FRONTEND_ARENA_CUP;flow.course_phase=1u;
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_R)&&flow.mesh_group==1u&&flow.mesh_cup==2u&&flow.course_phase==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&tap(&flow,PORTABLE_FRONTEND_BUTTON_UP)&&flow.course_track==4u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.mesh_request_course==35u);
    flow.mesh_request=0u;
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B)&&tap(&flow,PORTABLE_FRONTEND_BUTTON_R)&&flow.mesh_group==0u&&
          flow.course_phase==1u&&flow.course_cup==PORTABLE_FRONTEND_ARENA_CUP);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_L)&&flow.mesh_group==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B)&&settle(&flow)&&flow.page==PORTABLE_FRONTEND_PAGE_CHARACTER);
    /* Groups need valid cups. */
    cups[3].group=2u;
    CHECK(!portable_frontend_flow_set_mesh_cups(&flow,cups,4u,groups,NULL,2u));
    cups[3].group=1u;cups[3].course_ids[0]=0u;
    CHECK(!portable_frontend_flow_set_mesh_cups(&flow,cups,4u,groups,NULL,2u));
    CHECK(portable_frontend_flow_set_mesh_cups(&flow,NULL,0u,NULL,NULL,0u)&&flow.mesh_group_count==0u);
    return 0;
}

/* A platform driver locks the course screen to its platform's group and
 * the other players to its pages; the base game never lists that group. */
static int test_platform_lock(void)
{
    static const char *const groups[2]={"Mario Kart DS","Bridge"};
    static const uint8_t platforms[2]={1u,0u};
    PortableFrontendDriverSlot roster[18];
    memcpy(roster,drivers,sizeof drivers);
    for(unsigned slot=0u;slot<9u;++slot)
        roster[9u+slot]=(PortableFrontendDriverSlot){slot==4u?0u:(slot<4u?slot+1u:200u+slot),1u,1u,0u,0u,1u,1u};
    PortableFrontendMeshCup cups[2];
    memset(cups,0,sizeof cups);
    cups[0]=(PortableFrontendMeshCup){"Mushroom Cup",0x4d4b4301u,{11u,12u,13u,14u},{15u,15u,15u,15u},
        {"A","B","C","D"},0u,4u,1u,0u};
    cups[1]=(PortableFrontendMeshCup){"",7u,{65536u},{15u},{"Bridge"},1u,1u,0u,0u};
    const PortableFrontendConfig config={roster,18u,courses,sizeof courses/sizeof courses[0]};
    PortableFrontendFlow flow;
    CHECK(portable_frontend_flow_init(&flow,&config)&&
          portable_frontend_flow_set_mesh_cups(&flow,cups,2u,groups,platforms,2u));
    CHECK(portable_frontend_flow_page_platform(&flow,0u)==0u&&portable_frontend_flow_page_platform(&flow,1u)==1u);
    CHECK(portable_frontend_flow_platform_group(&flow,1u)==1u&&portable_frontend_flow_platform_group(&flow,2u)==0u);
    /* Platform page: Random and a pick both lock platform 1, which opens on its group. */
    flow.mode_cursor=1u;flow.page=PORTABLE_FRONTEND_PAGE_CHARACTER;flow.character_page=1u;flow.character_cursor=0u;
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.character_locked&&flow.platform==1u&&flow.selected_character==1u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_START)&&settle(&flow)&&flow.page==PORTABLE_FRONTEND_PAGE_COURSE);
    CHECK(flow.mesh_group==1u&&!portable_frontend_flow_group_allowed(&flow,0u)&&
          !portable_frontend_flow_group_allowed(&flow,2u));
    const size_t events=flow.event_count;
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_R)&&tap(&flow,PORTABLE_FRONTEND_BUTTON_L)&&flow.mesh_group==1u&&
          flow.event_count==events);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B)&&settle(&flow)&&flow.page==PORTABLE_FRONTEND_PAGE_CHARACTER&&
          flow.platform==0u);
    /* The base game: 2D sets and the platform-free group, never platform 1's. */
    flow.character_page=0u;flow.character_cursor=0u;
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.platform==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_START)&&settle(&flow)&&flow.page==PORTABLE_FRONTEND_PAGE_COURSE&&
          flow.mesh_group==0u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_R)&&flow.mesh_group==2u);
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_R)&&flow.mesh_group==0u);
    /* Random on a platform page stays on that platform. */
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B)&&settle(&flow)&&flow.page==PORTABLE_FRONTEND_PAGE_CHARACTER);
    flow.character_page=1u;flow.character_cursor=PORTABLE_FRONTEND_RANDOM_SLOT;
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.platform==1u&&flow.selected_character==1u);
    /* Local players: the second player browses only the first one's platform. */
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_B)&&!flow.character_locked&&flow.platform==0u);
    flow.players=2u;flow.mode_cursor=2u;flow.character_cursor=0u;
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.character_player==1u&&flow.character_page==1u&&
          flow.platform==1u);
    flow.character_cursor=2u;
    CHECK(tap(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT)&&flow.character_page==1u);
    return 0;
}

/* Catalog rows fold into per-group cups ordered by first course. */
static int test_mesh_cup_menu(void)
{
    const PortableMeshCupMenuRow rows[]={
        {104u,2u,3u,15u,40u,"D","Flower",0u,1u},
        {101u,1u,0u,15u,10u,"A","Mushroom",0u,1u},
        {103u,2u,2u,15u,30u,"C","Flower",0u,1u},
        {102u,1u,1u,15u,20u,"B","Mushroom",0u,0u},
        {201u,9u,0u,2u,1u,"E","",1u,1u},
        {105u,2u,0u,15u,50u,"F","Flower",0u,1u},
        {106u,2u,1u,15u,60u,"G","Flower",0u,1u},
        {301u,3u,0u,16u,5u,"H0","Arenas",0u,1u},{302u,3u,1u,16u,5u,"H1","Arenas",0u,1u},
        {303u,3u,2u,16u,5u,"H2","Arenas",0u,1u},{304u,3u,3u,16u,5u,"H3","Arenas",0u,1u},
        {305u,3u,4u,16u,5u,"H4","Arenas",0u,1u},{306u,3u,5u,16u,5u,"H5","Arenas",0u,1u},
        {307u,3u,6u,16u,5u,"H6","Arenas",0u,1u},{308u,3u,7u,16u,5u,"H7","Arenas",0u,1u},
        {309u,3u,8u,16u,5u,"H8","Arenas",0u,1u}};
    PortableFrontendMeshCup cups[8];
    int overflow=1;
    const size_t count=portable_mesh_cup_menu_build(rows,sizeof rows/sizeof rows[0],cups,8u,&overflow);
    CHECK(count==5u&&!overflow);
    /* Group 0: Arenas (sort 5) split 8+1, Mushroom (10), Flower (30); then group 1. */
    CHECK(cups[0].cup_id==3u&&cups[0].course_count==8u&&cups[0].course_ids[7]==308u&&!cups[0].gp);
    CHECK(cups[1].cup_id==3u&&cups[1].course_count==1u&&cups[1].course_ids[0]==309u);
    CHECK(cups[2].cup_id==1u&&cups[2].course_count==2u&&cups[2].course_ids[0]==101u&&
          cups[2].explore_only==2u&&!cups[2].gp&&!strcmp(cups[2].name,"Mushroom"));
    CHECK(cups[3].cup_id==2u&&cups[3].course_count==4u&&cups[3].gp&&
          cups[3].course_ids[0]==105u&&cups[3].course_ids[3]==104u&&!strcmp(cups[3].course_names[1],"G"));
    CHECK(cups[4].group==1u&&cups[4].course_ids[0]==201u&&cups[4].course_modes[0]==2u);
    CHECK(portable_mesh_cup_menu_build(rows,sizeof rows/sizeof rows[0],cups,4u,&overflow)==0u&&overflow);
    return 0;
}

int main(void)
{
    CHECK(test_mesh_cup_menu()==0);
    CHECK(test_mesh_cups()==0);
    CHECK(test_platform_lock()==0);
    CHECK(test_title_cursor_motion()==0);
    CHECK(test_gp_cup_confirmation_and_preview()==0);
    CHECK(test_multiplayer_route()==0);
    CHECK(test_battle_route()==0);
    CHECK(test_records_route()==0);
    CHECK(test_ghost_race_selection()==0);
    CHECK(test_received_ghost_route()==0);
    CHECK(test_validation() == 0);
    CHECK(test_night_title_ambient()==0);
    CHECK(test_short_transitions() == 0);
    CHECK(test_cup_access() == 0);
    CHECK(test_extra_courses() == 0);
    CHECK(test_custom_courses() == 0);
    CHECK(test_kart_tricks() == 0);
    CHECK(test_title_and_repeat() == 0);
    CHECK(test_route_and_topology() == 0);
    CHECK(test_gp_route() == 0);
    CHECK(test_quick_run_route() == 0);
    CHECK(test_deterministic_replay() == 0);
    puts("portable frontend flow: PASS");
    return 0;
}
