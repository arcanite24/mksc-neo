#include "race_session.h"
#include "route_geometry.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "race session failed line %d: %s\n", \
                __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static PortableRaceRulesView rules_for(const PortableRacePoint *route,
                                       size_t route_count)
{
    PortableRaceRulesView rules = {0};
    rules.route = route;
    rules.route_count = route_count;
    rules.start_x_q16 = 100 * 65536;
    rules.start_y_q16 = 100 * 65536;
    rules.start_heading = 16384u;
    rules.start_route_index = 1u;
    rules.progress_kind = PORTABLE_RACE_PROGRESS_DEMO_V1;
    rules.finish_kind = PORTABLE_FINISH_NORTHBOUND_Y;
    rules.finish_y_q16 = 100 * 65536;
    rules.lap_count = 3u;
    rules.countdown_ticks = 180u;
    rules.rescue_penalty_ticks = 180u;
    return rules;
}

static void motion_tick(PortableRaceSession *session,
                        int32_t old_x, int32_t old_y,
                        int32_t new_x, int32_t new_y, uint8_t drivable)
{
    const PortableRaceMotionObservation motion = {
        .old_x_q16 = old_x,
        .old_y_q16 = old_y,
        .new_x_q16 = new_x,
        .new_y_q16 = new_y,
        .drivable = drivable,
    };
    CHECK(portable_race_session_begin_tick(session) ==
          PORTABLE_RACE_TICK_ADVANCE_MOTION);
    CHECK(portable_race_session_commit_motion(session, &motion));
}

static PortableRaceRulesView source_rules_for(
    const PortableRacePoint *route, size_t route_count)
{
    PortableRaceRulesView rules = rules_for(route, route_count);
    rules.start_route_index = (uint16_t)(route_count - 1u);
    rules.start_checkpoint_raw =
        (uint8_t)(UINT8_C(0x80) | rules.start_route_index);
    rules.progress_kind = PORTABLE_RACE_PROGRESS_SOURCE_RASTER_V1;
    rules.finish_kind = PORTABLE_FINISH_SOURCE_Y_CROSSING;
    return rules;
}

static void source_motion_tick(PortableRaceSession *session,
                               int32_t old_x, int32_t old_y,
                               int32_t new_x, int32_t new_y,
                               uint8_t track_cell)
{
    const PortableRaceMotionObservation motion = {
        .old_x_q16 = old_x,
        .old_y_q16 = old_y,
        .new_x_q16 = new_x,
        .new_y_q16 = new_y,
        .drivable = 1u,
        .track_cell = track_cell,
        .track_cell_valid = 1u,
    };
    CHECK(portable_race_session_begin_tick(session) ==
          PORTABLE_RACE_TICK_ADVANCE_MOTION);
    CHECK(portable_race_session_commit_motion(session, &motion));
}

static void begin_racing(PortableRaceSession *session)
{
    session->phase = PORTABLE_RACE_PHASE_RACING;
    session->countdown_remaining = 0u;
}

static uint16_t previous_bearing(int32_t x, int32_t y)
{
    double angle = atan2((double)x, -(double)y) * 65536.0 / 6.283185307179586;
    if (angle < 0.0)
        angle += 65536.0;
    return (uint16_t)(uint32_t)angle;
}

static size_t previous_nearest(const PortableRacePoint *route, size_t count, int32_t x_q16,
                               int32_t y_q16)
{
    const double x = x_q16 / 65536.0, y = y_q16 / 65536.0;
    double nearest = 1e30;
    size_t selected = 0u;
    for (size_t i = 0u; i < count; ++i)
    {
        const size_t next = (i + 1u) % count;
        const double dx = (double)route[next].x - route[i].x;
        const double dy = (double)route[next].y - route[i].y;
        const double length = dx * dx + dy * dy;
        if (length == 0.0)
            continue;
        double t = ((x - route[i].x) * dx + (y - route[i].y) * dy) / length;
        if (t < 0.0)
            t = 0.0;
        if (t > 1.0)
            t = 1.0;
        const double ex = x - route[i].x - t * dx, ey = y - route[i].y - t * dy;
        const double distance = ex * ex + ey * ey;
        if (distance < nearest)
        {
            nearest = distance;
            selected = t == 1.0 ? next : i;
        }
    }
    return selected;
}

static void test_route_geometry(void)
{
    /* Compare the replaced libm contract in all quadrants and around zero,
     * then cover the full signed input range without negating INT32_MIN. */
    for (int32_t x = -64; x <= 64; ++x)
        for (int32_t y = -64; y <= 64; ++y)
            CHECK(portable_route_bearing(x, y) == previous_bearing(x, y));
    static const int32_t limits[] = {INT32_MIN, INT32_MIN + 1, -1, 0, 1, INT32_MAX};
    for (size_t x = 0u; x < sizeof limits / sizeof limits[0]; ++x)
        for (size_t y = 0u; y < sizeof limits / sizeof limits[0]; ++y)
            CHECK(portable_route_bearing(limits[x], limits[y]) ==
                  previous_bearing(limits[x], limits[y]));
    CHECK(portable_route_bearing(0, 0) == 32768u);

    const PortableRacePoint square[] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    CHECK(portable_route_nearest_segment(square, 4u, 0, 0) == 0u);
    CHECK(portable_route_nearest_segment(square, 4u, 65536, -65536) == 1u);
    CHECK(portable_route_nearest_segment(square, 4u, -65536, 65536) == 3u);
    const PortableRacePoint repeated[] = {{0, 0}, {0, 0}, {1, 0}};
    CHECK(portable_route_nearest_segment(repeated, 3u, 0, 0) == 1u);
    CHECK(portable_route_nearest_segment(repeated, 3u, 65536, 0) == 2u);
    const PortableRacePoint collapsed[] = {{7, -3}, {7, -3}};
    CHECK(portable_route_nearest_segment(collapsed, 2u, INT32_MAX, INT32_MIN) == 0u);

    /* These comparisons exceed 64-bit squared/cross-product capacity. */
    const PortableRacePoint full[] = {
        {-32768, -32768}, {32767, -32768}, {32767, 32767}, {-32768, 32767}};
    CHECK(portable_route_nearest_segment(full, 4u, INT32_MIN, INT32_MIN) == 0u);
    CHECK(portable_route_nearest_segment(full, 4u, INT32_MAX, INT32_MIN) == 1u);
    CHECK(portable_route_nearest_segment(full, 4u, INT32_MAX, INT32_MAX) == 2u);
    CHECK(portable_route_nearest_segment(full, 4u, INT32_MIN, INT32_MAX) == 3u);
    CHECK(portable_route_nearest_segment(full, 4u, -32768, -32768) == 0u);
    CHECK(portable_route_nearest_segment(full, 4u, -32767, -32768) == 1u);

    const PortableRacePoint irregular[] = {{-4, -2}, {2, -3}, {5, 2}, {0, 6}, {-5, 1}};
    for (int32_t x = -24; x <= 24; ++x)
        for (int32_t y = -24; y <= 24; ++y)
        {
            const int32_t px = x * 16384 + 37, py = y * 16384 - 19;
            CHECK(portable_route_nearest_segment(irregular, 5u, px, py) ==
                  previous_nearest(irregular, 5u, px, py));
        }
}

static void test_arena_progress(const PortableRacePoint *route)
{
    PortableRaceRulesView rules = source_rules_for(route, 4u);
    PortableRaceSession session;
    rules.progress_kind = PORTABLE_RACE_PROGRESS_ARENA_RASTER_V1;
    rules.finish_kind = PORTABLE_FINISH_NONE;
    rules.lap_count = 0u;
    CHECK(portable_race_session_init(&session, &rules));
    for (unsigned tick = 0u; tick < rules.countdown_ticks; ++tick)
        CHECK(portable_race_session_begin_tick(&session) == PORTABLE_RACE_TICK_COUNTDOWN);
    CHECK(session.phase == PORTABLE_RACE_PHASE_RACING);
    /* Arena high-bit regions accept new checkpoints without invoking B7B8.
     * Repeated forward/reverse crossings cannot manufacture race results. */
    for (unsigned tick = 0u; tick < 240u; ++tick)
    {
        const int32_t from = (tick & 1u) ? 99 : 101, to = (tick & 1u) ? 101 : 99;
        source_motion_tick(&session, 100 * 65536, from * 65536, 100 * 65536, to * 65536,
                           (uint8_t)(0x80u | (tick % 4u)));
        CHECK(session.checkpoint_raw == tick % 4u && session.laps_completed == 0u &&
              !session.lap_started && session.phase == PORTABLE_RACE_PHASE_RACING &&
              session.lap_raw == 0x7fu && session.elapsed_ticks == tick + 1u);
    }
    const PortableRaceMotionObservation high = {.track_cell = 2u,
                                                .track_cell_valid = 1u,
                                                .checkpoint_mode = 0x80u,
                                                .height_q16 = 0x40001,
                                                .hop_mode = 6u};
    CHECK(portable_race_session_resample_checkpoint(&session, &high));
    CHECK(session.checkpoint_raw == 3u);
    PortableRaceMotionObservation grounded = high;
    grounded.height_q16 = 0x40000;
    CHECK(portable_race_session_resample_checkpoint(&session, &grounded));
    CHECK(session.checkpoint_raw == 2u && session.phase == PORTABLE_RACE_PHASE_RACING);
    grounded.track_cell = 0xffu;
    CHECK(portable_race_session_resample_checkpoint(&session, &grounded));
    CHECK(session.checkpoint_raw == 2u);
    portable_race_session_restart(&session);
    CHECK(session.phase==PORTABLE_RACE_PHASE_COUNTDOWN && session.elapsed_ticks==0u &&
          session.checkpoint_raw==rules.start_checkpoint_raw && session.laps_completed==0u);
    rules.lap_count=1u;
    CHECK(!portable_race_session_init(&session,&rules));
    rules.lap_count=0u;rules.finish_kind=PORTABLE_FINISH_SOURCE_Y_CROSSING;
    CHECK(!portable_race_session_init(&session,&rules));
    rules.finish_kind=PORTABLE_FINISH_NONE;rules.progress_kind=PORTABLE_RACE_PROGRESS_SOURCE_RASTER_V1;
    CHECK(!portable_race_session_init(&session,&rules));
}

static void test_source_progress(const PortableRacePoint *route)
{
    static const PortableRacePoint skipped_route[8] = {
        {100, 100}, {200, 100}, {300, 100}, {400, 100},
        {400, 200}, {300, 200}, {200, 200}, {100, 200},
    };
    PortableRaceRulesView rules = source_rules_for(route, 4u);
    PortableRaceSession session;
    PortableRaceSession tight;
    PortableRaceSession skipped;

    CHECK(portable_race_session_init(&session, &rules));
    CHECK(session.checkpoint_raw == UINT8_C(0x83) &&
          session.lap_raw == UINT8_C(0x7f) &&
          session.max_progress_raw == UINT16_C(0x7f83));
    begin_racing(&session);

    /* The source start crossing changes 0x7f/3 to 0x80/0. */
    source_motion_tick(&session, 160 * 65536, 101 * 65536,
                       160 * 65536, 99 * 65536, UINT8_C(0x80));
    CHECK(session.lap_started && session.lap_raw == UINT8_C(0x80) &&
          session.checkpoint_raw == 0u &&
          session.checkpoint_next_index == 1u &&
          session.laps_completed == 0u && session.lap_origin_ticks == 0u);

    /* The same Y crossing outside a high-bit source raster cell is not the
     * finish line. Shy Guy Beach crosses finish_y on its western side too. */
    source_motion_tick(&session, 40 * 65536, 99 * 65536,
                       40 * 65536, 101 * 65536, UINT8_C(0x00));
    CHECK(session.lap_raw == UINT8_C(0x80) && session.laps_completed == 0u);

    /* Raster checkpoint identity, rather than centerline distance, accepts
     * both wide and tight legal lines. */
    source_motion_tick(&session, 40 * 65536, 80 * 65536,
                       42 * 65536, 80 * 65536, UINT8_C(0x81));
    CHECK(session.checkpoint_next_index == 2u);
    source_motion_tick(&session, 360 * 65536, 280 * 65536,
                       362 * 65536, 280 * 65536, UINT8_C(0x82));
    source_motion_tick(&session, 40 * 65536, 280 * 65536,
                       42 * 65536, 280 * 65536, UINT8_C(0x83));
    CHECK(session.checkpoint_next_index == 0u);
    source_motion_tick(&session, 160 * 65536, 101 * 65536,
                       160 * 65536, 99 * 65536, UINT8_C(0x80));
    CHECK(session.laps_completed == 1u &&
          session.lap_ticks[0] == session.elapsed_ticks &&
          !session.finish_armed);

    /* The high-water mark rejects a repeated lap after reversing. */
    source_motion_tick(&session, 160 * 65536, 99 * 65536,
                       160 * 65536, 101 * 65536, UINT8_C(0x80));
    CHECK(!session.finish_armed);
    source_motion_tick(&session, 160 * 65536, 101 * 65536,
                       160 * 65536, 99 * 65536, UINT8_C(0x80));
    CHECK(session.laps_completed == 1u);

    CHECK(portable_race_session_init(&tight, &rules));
    begin_racing(&tight);
    source_motion_tick(&tight, 160 * 65536, 101 * 65536,
                       160 * 65536, 99 * 65536, UINT8_C(0x80));
    source_motion_tick(&tight, 299 * 65536, 80 * 65536,
                       301 * 65536, 80 * 65536, UINT8_C(0x81));
    CHECK(tight.checkpoint_next_index == 2u);

    rules = source_rules_for(skipped_route, 8u);
    CHECK(portable_race_session_init(&skipped, &rules));
    begin_racing(&skipped);
    source_motion_tick(&skipped, 160 * 65536, 101 * 65536,
                       160 * 65536, 99 * 65536, UINT8_C(0x80));
    source_motion_tick(&skipped, 40 * 65536, 280 * 65536,
                       42 * 65536, 280 * 65536, UINT8_C(0x84));
    CHECK(skipped.checkpoint_next_index == 5u && skipped.checkpoint_raw == 4u);
    source_motion_tick(&skipped, 160 * 65536, 101 * 65536,
                       160 * 65536, 99 * 65536, UINT8_C(0x80));
    /* No invented final-checkpoint arm: the source accepts the next new
     * forward crossing even when intermediate raster bands were skipped. */
    CHECK(skipped.laps_completed == 1u);
    source_motion_tick(&skipped, 160 * 65536, 99 * 65536,
                       160 * 65536, 101 * 65536, UINT8_C(0x80));
    source_motion_tick(&skipped, 160 * 65536, 101 * 65536,
                       160 * 65536, 99 * 65536, UINT8_C(0x80));
    CHECK(skipped.laps_completed == 1u);
}

static void source_observation_tick(PortableRaceSession *session,
                                     PortableRaceMotionObservation motion)
{
    CHECK(portable_race_session_begin_tick(session) ==
          PORTABLE_RACE_TICK_ADVANCE_MOTION);
    CHECK(portable_race_session_commit_motion(session, &motion));
}

static void test_source_progress_edges(const PortableRacePoint *route)
{
    PortableRaceRulesView rules = source_rules_for(route, 4u);
    PortableRaceSession session;
    PortableRaceMotionObservation motion = {
        .old_x_q16 = 160 * 65536, .new_x_q16 = 160 * 65536,
        .old_y_q16 = 110 * 65536, .new_y_q16 = 90 * 65536,
        .track_cell = UINT8_C(0xff), .track_cell_valid = 1u,
    };
    CHECK(portable_race_session_init(&session, &rules));
    begin_racing(&session);
    /* B98C still checks the line in 0xff and does not apply the host's
     * four-unit motion guard or terrain drivable flag to lap counting. */
    source_observation_tick(&session, motion);
    CHECK(session.lap_raw == 0x80u && session.lap_started);
    /* The original's signed Q8 components can exceed four world units on a
     * boost. The route aid must keep tracking these legal fast movements. */
    session.next_route_index = 0u;
    motion.old_x_q16 = 299 * 65536;
    motion.new_x_q16 = 305 * 65536;
    motion.old_y_q16 = motion.new_y_q16 = 100 * 65536;
    motion.drivable = 1u;
    source_observation_tick(&session, motion);
    CHECK(session.next_route_index == 2u);
    motion.old_x_q16 = motion.new_x_q16 = 160 * 65536;

    motion.old_y_q16 = 99 * 65536;
    motion.new_y_q16 = 100 * 65536;
    source_observation_tick(&session, motion);
    CHECK(session.lap_raw == 0x7fu); /* touching from below is reverse */
    motion.old_y_q16 = 100 * 65536;
    motion.new_y_q16 = 101 * 65536;
    source_observation_tick(&session, motion);
    CHECK(session.lap_raw == 0x7fu); /* leaving the line is not reverse */
    motion.old_y_q16 = 100 * 65536;
    motion.new_y_q16 = 99 * 65536;
    source_observation_tick(&session, motion);
    CHECK(session.lap_raw == 0x80u && session.laps_completed == 0u);

    /* A carry may update a checkpoint but cannot advance a lap. */
    motion.hop_mode = 6u;
    motion.track_cell = UINT8_C(0x82);
    source_observation_tick(&session, motion);
    CHECK(session.checkpoint_raw == 2u && session.lap_raw == 0x80u);
    motion.hop_mode = 0u;
    motion.track_cell_valid = 0u;
    source_observation_tick(&session, motion);
    CHECK(session.lap_raw == 0x80u);

    /* The original cut gate is low-half mode bit7 plus height >4 units. */
    motion.track_cell_valid = 1u;
    motion.track_cell = 1u;
    motion.checkpoint_mode = 0x80u;
    motion.height_q16 = 4 * 65536 + 1;
    source_observation_tick(&session, motion);
    CHECK(session.checkpoint_raw == 2u);
    motion.height_q16 = 4 * 65536;
    source_observation_tick(&session, motion);
    CHECK(session.checkpoint_raw == 1u);
    motion.track_cell = 0u; /* backwards progress must be accepted too */
    source_observation_tick(&session, motion);
    CHECK(session.checkpoint_raw == 0u);
    motion.track_cell = 0x82u;
    motion.height_q16 = 40 * 65536;
    motion.old_y_q16 = motion.new_y_q16 = 90 * 65536;
    source_observation_tick(&session, motion);
    CHECK(session.checkpoint_raw == 2u); /* high-half bypasses height */

    /* Current +6 is a signed high halfword; previous +F0 truncates toward
     * zero. A negative fractional current Y can cross a zero finish. */
    rules.finish_y_q16 = 0;
    CHECK(portable_race_session_init(&session, &rules));
    begin_racing(&session);
    motion.old_y_q16 = 1;
    motion.new_y_q16 = -1;
    motion.track_cell = 0x80u;
    source_observation_tick(&session, motion);
    CHECK(session.lap_raw == 0x80u && session.lap_started);
    session.lap_raw = 0x74u;
    motion.old_y_q16 = -65536;
    motion.new_y_q16 = 0;
    source_observation_tick(&session, motion);
    CHECK(session.lap_raw == 0x74u);
}

int main(void)
{
    test_route_geometry();
    static const PortableRacePoint route[] = {
        {100, 100}, {300, 100}, {300, 300}, {100, 300},
    };
    PortableRaceRulesView rules = rules_for(route, 4u);
    PortableRaceSession session;

    for (unsigned outside = 0u; outside < 2u; ++outside)
    {
        CHECK(portable_race_session_init(&session, &rules));
        begin_racing(&session);
        motion_tick(&session, 296 * 65536 - (int32_t)outside, 100 * 65536,
                    300 * 65536, 100 * 65536, 1u);
        CHECK(session.next_route_index == (outside ? 1u : 2u));
    }

    {
        static const struct
        {
            int32_t finish, old_y, new_y;
            uint8_t lap;
        } cases[] = {
            {-1, 0, -1, 0x7fu},
            {-1, 0, -65537, 0x80u},
            {INT32_MIN, INT32_MIN, INT32_MAX, 0x7fu},
            {INT32_MIN + 65536, INT32_MIN, INT32_MAX, 0x7eu},
            {INT32_MAX, INT32_MAX, INT32_MAX - 65535, 0x7fu},
            {INT32_MAX, INT32_MAX, INT32_MAX - 65536, 0x80u},
        };
        for (size_t index = 0u; index < sizeof cases / sizeof cases[0]; ++index)
        {
            PortableRaceRulesView edge = source_rules_for(route, 4u);
            edge.finish_y_q16 = cases[index].finish;
            CHECK(portable_race_session_init(&session, &edge));
            begin_racing(&session);
            source_motion_tick(&session, 0, cases[index].old_y, 0, cases[index].new_y, 0x80u);
            CHECK(session.lap_raw == cases[index].lap);
        }
    }
    CHECK(portable_race_session_init(&session, &rules));
    CHECK(session.phase == PORTABLE_RACE_PHASE_COUNTDOWN);
    CHECK(session.next_route_index == 1u);
    for (unsigned tick = 0; tick < 180u; ++tick)
        CHECK(portable_race_session_begin_tick(&session) == PORTABLE_RACE_TICK_COUNTDOWN);
    CHECK(session.phase == PORTABLE_RACE_PHASE_RACING);
    CHECK(session.elapsed_ticks == 0u);

    /* Stationary, reverse and teleport observations cannot progress. */
    motion_tick(&session, 100 * 65536, 100 * 65536,
                100 * 65536, 100 * 65536, 1u);
    CHECK(session.next_route_index == 1u);
    motion_tick(&session, 100 * 65536, 300 * 65536,
                100 * 65536, 100 * 65536, 1u);
    CHECK(session.next_route_index == 1u);
    motion_tick(&session, 300 * 65536, 98 * 65536,
                297 * 65536, 98 * 65536, 1u);
    CHECK(session.next_route_index == 1u);

    /* A wide legal line advances the expected route region. */
    motion_tick(&session, 299 * 65536, 40 * 65536,
                302 * 65536, 40 * 65536, 1u);
    CHECK(session.next_route_index == 2u);

    portable_race_session_restart(&session);
    session.phase = PORTABLE_RACE_PHASE_RACING;
    session.countdown_remaining = 0u;
    CHECK(portable_race_session_apply_rescue(&session));
    CHECK(session.rescue_count == 1u && session.elapsed_ticks == 180u);
    CHECK(session.rescue_x_q16 == route[0].x * 65536);
    CHECK(session.rescue_y_q16 == route[0].y * 65536);

    rules.route_count = 1u;
    CHECK(!portable_race_session_init(&session, &rules));
    rules = rules_for(route, 4u);
    rules.start_route_index = 4u;
    CHECK(!portable_race_session_init(&session, &rules));
    rules = rules_for(route, 4u);
    rules.lap_count = 6u;
    CHECK(!portable_race_session_init(&session, &rules));
    rules = rules_for(route, 4u);
    rules.countdown_ticks = 0u;
    CHECK(!portable_race_session_init(&session, &rules));
    rules = rules_for(route, 4u);
    rules.progress_kind = (PortableRaceProgressKind)99;
    CHECK(!portable_race_session_init(&session, &rules));

    /* Extra courses use five source-raster laps, without finishing at three. */
    rules=source_rules_for(route,4u);rules.lap_count=5u;
    CHECK(portable_race_session_init(&session,&rules));
    begin_racing(&session);
    source_motion_tick(&session,160*65536,101*65536,160*65536,99*65536,0x80u);
    for(unsigned lap=0u;lap<5u;++lap){
        source_motion_tick(&session,160*65536,90*65536,160*65536,90*65536,0x81u);
        source_motion_tick(&session,160*65536,90*65536,160*65536,90*65536,0x82u);
        source_motion_tick(&session,160*65536,90*65536,160*65536,90*65536,0x83u);
        source_motion_tick(&session,160*65536,101*65536,160*65536,99*65536,0x80u);
        CHECK(session.laps_completed==lap+1u);
        CHECK(session.phase==(lap==4u?PORTABLE_RACE_PHASE_FINISHED:PORTABLE_RACE_PHASE_RACING));
        CHECK(session.lap_ticks[lap]>0u);
    }
    CHECK(portable_race_session_total_ticks(&session)==session.elapsed_ticks);
    /* A finished kart still accepts source checkpoints for autonomous
     * steering/recovery, while the completed result remains immutable. */
    const PortableRaceSession finished=session;
    PortableRaceMotionObservation after_finish={.track_cell=1u,.track_cell_valid=1u,
        .checkpoint_mode=0x80u,.height_q16=5*65536};
    CHECK(portable_race_session_update_finished_checkpoint(&session,&after_finish));
    CHECK(session.checkpoint_raw==finished.checkpoint_raw);
    after_finish.height_q16=4*65536;
    CHECK(portable_race_session_update_finished_checkpoint(&session,&after_finish));
    CHECK(session.checkpoint_raw==1u && session.phase==PORTABLE_RACE_PHASE_FINISHED &&
        session.elapsed_ticks==finished.elapsed_ticks &&
        portable_race_session_total_ticks(&session)==portable_race_session_total_ticks(&finished) &&
        session.laps_completed==finished.laps_completed && session.max_progress_raw==finished.max_progress_raw &&
        memcmp(session.lap_ticks,finished.lap_ticks,sizeof session.lap_ticks)==0);

    test_arena_progress(route);
    test_source_progress(route);
    test_source_progress_edges(route);

    puts("portable race session: PASS");
    return 0;
}
