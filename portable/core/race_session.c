#include "race_session.h"
#include "route_geometry.h"

#include <limits.h>
#include <string.h>

static int route_point_fits_q16(PortableRacePoint point)
{
    return point.x >= INT32_MIN / INT32_C(65536) && point.x <= INT32_MAX / INT32_C(65536) &&
           point.y >= INT32_MIN / INT32_C(65536) && point.y <= INT32_MAX / INT32_C(65536);
}

int portable_race_session_init(PortableRaceSession *session,
                               const PortableRaceRulesView *rules)
{
    PortableRaceSession next = {0};

    if (session == NULL || rules == NULL || rules->route == NULL ||
        rules->route_count < 2u || rules->route_count > UINT16_MAX ||
        rules->start_route_index >= rules->route_count ||
        !((rules->progress_kind == PORTABLE_RACE_PROGRESS_DEMO_V1 &&
           rules->finish_kind == PORTABLE_FINISH_NORTHBOUND_Y) ||
          (rules->progress_kind == PORTABLE_RACE_PROGRESS_SOURCE_RASTER_V1 &&
           rules->finish_kind == PORTABLE_FINISH_SOURCE_Y_CROSSING) ||
          (rules->progress_kind == PORTABLE_RACE_PROGRESS_ARENA_RASTER_V1 &&
           rules->finish_kind == PORTABLE_FINISH_NONE)) ||
        (rules->progress_kind != PORTABLE_RACE_PROGRESS_DEMO_V1 &&
         ((rules->start_checkpoint_raw & UINT8_C(0x7f)) !=
              rules->start_route_index ||
          (rules->start_checkpoint_raw & UINT8_C(0x7f)) >=
              rules->route_count)) ||
        ((rules->progress_kind == PORTABLE_RACE_PROGRESS_ARENA_RASTER_V1)
            ? rules->lap_count != 0u : rules->lap_count == 0u) ||
        rules->lap_count > PORTABLE_RACE_MAX_LAPS ||
        rules->countdown_ticks == 0u || rules->rescue_penalty_ticks == 0u)
        return 0;
    for (size_t index = 0; index < rules->route_count; ++index)
        if (!route_point_fits_q16(rules->route[index]))
            return 0;

    next.route = rules->route;
    next.route_count = rules->route_count;
    next.start_x_q16 = rules->start_x_q16;
    next.start_y_q16 = rules->start_y_q16;
    next.start_heading = rules->start_heading;
    next.start_route_index = rules->start_route_index;
    next.start_checkpoint_raw = rules->start_checkpoint_raw;
    next.progress_kind = rules->progress_kind;
    next.finish_kind = rules->finish_kind;
    next.finish_y_q16 = rules->finish_y_q16;
    next.lap_count = rules->lap_count;
    next.countdown_ticks = rules->countdown_ticks;
    next.rescue_penalty_ticks = rules->rescue_penalty_ticks;
    next.compatibility = rules->compatibility;
    portable_race_session_restart(&next);
    *session = next;
    return 1;
}

void portable_race_session_restart(PortableRaceSession *session)
{
    if (session == NULL || session->route == NULL || session->route_count < 2u)
        return;
    session->next_route_index = session->start_route_index;
    session->phase = PORTABLE_RACE_PHASE_COUNTDOWN;
    session->laps_completed = 0u;
    session->countdown_remaining = session->countdown_ticks;
    session->lap_started = 0u;
    session->finish_armed = 0u;
    session->motion_pending = 0u;
    session->checkpoint_raw =
        session->progress_kind != PORTABLE_RACE_PROGRESS_DEMO_V1
            ? session->start_checkpoint_raw
            : 0u;
    session->lap_raw =
        session->progress_kind != PORTABLE_RACE_PROGRESS_DEMO_V1
            ? UINT8_C(0x7f)
            : 0u;
    session->max_progress_raw =
        session->progress_kind != PORTABLE_RACE_PROGRESS_DEMO_V1
            ? (uint16_t)((UINT16_C(0x7f) << 8) |
                         session->start_checkpoint_raw)
            : 0u;
    session->checkpoint_next_index =
        session->progress_kind != PORTABLE_RACE_PROGRESS_DEMO_V1
            ? 0u
            : session->start_route_index;
    session->elapsed_ticks = 0u;
    session->lap_origin_ticks = 0u;
    memset(session->lap_ticks, 0, sizeof session->lap_ticks);
    session->rescue_count = 0u;
    session->rescue_x_q16 = session->start_x_q16;
    session->rescue_y_q16 = session->start_y_q16;
    session->rescue_heading = session->start_heading;
}

PortableRaceTickDisposition portable_race_session_begin_tick(
    PortableRaceSession *session)
{
    if (session == NULL || session->route == NULL || session->motion_pending)
        return PORTABLE_RACE_TICK_INVALID;
    if (session->phase == PORTABLE_RACE_PHASE_FINISHED)
        return PORTABLE_RACE_TICK_FINISHED;
    if (session->phase == PORTABLE_RACE_PHASE_COUNTDOWN)
    {
        --session->countdown_remaining;
        if (session->countdown_remaining == 0u)
            session->phase = PORTABLE_RACE_PHASE_RACING;
        return PORTABLE_RACE_TICK_COUNTDOWN;
    }
    if (session->phase != PORTABLE_RACE_PHASE_RACING)
        return PORTABLE_RACE_TICK_INVALID;
    ++session->elapsed_ticks;
    session->motion_pending = 1u;
    return PORTABLE_RACE_TICK_ADVANCE_MOTION;
}

static void commit_finish_crossing(PortableRaceSession *session,
                                   const PortableRaceMotionObservation *motion)
{
    if (!session->finish_armed ||
        motion->old_y_q16 < session->finish_y_q16 ||
        motion->new_y_q16 >= session->finish_y_q16)
        return;

    session->finish_armed = 0u;
    if (!session->lap_started)
    {
        session->lap_started = 1u;
        session->lap_origin_ticks = session->elapsed_ticks;
        return;
    }
    session->lap_ticks[session->laps_completed] =
        session->elapsed_ticks - session->lap_origin_ticks;
    session->lap_origin_ticks = session->elapsed_ticks;
    ++session->laps_completed;
    if (session->laps_completed == session->lap_count)
        session->phase = PORTABLE_RACE_PHASE_FINISHED;
}

static int16_t source_world_q16(int32_t value)
{
    return (int16_t)(value / INT32_C(65536));
}

static int16_t source_high_half(int32_t value)
{
    const uint16_t bits = (uint16_t)((uint32_t)value >> 16);
    if (bits < UINT16_C(0x8000))
        return (int16_t)bits;
    return (int16_t)((int32_t)bits - INT32_C(65536));
}

static void source_accept_checkpoint(PortableRaceSession *session, uint8_t track_cell)
{
    const uint8_t checkpoint = track_cell & UINT8_C(0x7f);
    /* B98C writes masked +90/+36 directly, including backwards travel and
     * multi-band jumps. The finish high-water mark, not a checkpoint arm,
     * prevents a reverse/forward line oscillation from awarding another lap. */
    if (checkpoint >= session->route_count)
        return;
    session->checkpoint_raw = checkpoint;
    session->checkpoint_next_index =
        (uint16_t)((checkpoint + 1u) % session->route_count);
}

static void source_commit_finish_crossing(PortableRaceSession *session,
                                          const PortableRaceMotionObservation *motion)
{
    const int16_t finish_y = source_high_half(session->finish_y_q16);
    const int16_t old_y = source_world_q16(motion->old_y_q16);
    /* B7B8 reads the current signed high halfword (+6), unlike C4C0's
     * previous-world anchor (+F0), which truncates negative fractions. */
    const int16_t new_y = source_high_half(motion->new_y_q16);
    const int forward = old_y >= finish_y && new_y < finish_y;
    const int reverse = old_y < finish_y && new_y >= finish_y;
    uint16_t encoded;

    if (reverse)
    {
        if (session->lap_raw > UINT8_C(0x74))
            --session->lap_raw;
        session->finish_armed = 0u;
        return;
    }
    if (!forward)
        return;

    ++session->lap_raw;
    session->checkpoint_raw = 0u;
    session->checkpoint_next_index = session->route_count > 1u ? 1u : 0u;
    encoded = (uint16_t)session->lap_raw << 8;

    if (encoded <= session->max_progress_raw)
        return;
    session->max_progress_raw = encoded;
    if (session->lap_raw == UINT8_C(0x80))
    {
        /* The source lap clock already runs before the first line crossing. */
        session->lap_started = 1u;
        return;
    }
    if (session->lap_raw < UINT8_C(0x81))
        return;
    session->lap_ticks[session->laps_completed] =
        session->elapsed_ticks - session->lap_origin_ticks;
    session->lap_origin_ticks = session->elapsed_ticks;
    ++session->laps_completed;
    if (session->laps_completed == session->lap_count)
        session->phase = PORTABLE_RACE_PHASE_FINISHED;
}

static void source_sample_checkpoint_position(PortableRaceSession *session,
    const PortableRaceMotionObservation *motion)
{
    if(session->progress_kind!=PORTABLE_RACE_PROGRESS_DEMO_V1 && motion->track_cell_valid &&
       ((motion->track_cell&UINT8_C(0x80))!=0u ||
        (motion->checkpoint_mode&UINT8_C(0x80))==0u || motion->height_q16<=INT32_C(0x40000)))
        source_accept_checkpoint(session,motion->track_cell);
}

int portable_race_session_update_finished_checkpoint(PortableRaceSession *session,
    const PortableRaceMotionObservation *motion)
{
    if(!session || !motion || session->phase!=PORTABLE_RACE_PHASE_FINISHED || session->motion_pending)
        return 0;
    source_sample_checkpoint_position(session,motion);
    return 1;
}

static void source_sample_checkpoint(PortableRaceSession *session,
    const PortableRaceMotionObservation *motion)
{
    if (session->progress_kind != PORTABLE_RACE_PROGRESS_DEMO_V1 &&
        motion->track_cell_valid)
    {
        const int finish_region = (motion->track_cell & UINT8_C(0x80)) != 0u;
        /* Low-half modes with bit7 reject airborne shortcuts above4 units.
         * High-half cells bypass that height gate, even the outside-route
         * sentinel 0xff: B98C still calls B7B8 there. */
        source_sample_checkpoint_position(session,motion);
        /* B7B8 exits immediately for mode800; arena high-bit cells still
         * update checkpoints, but never start or finish a lap. */
        if (session->finish_kind == PORTABLE_FINISH_SOURCE_Y_CROSSING &&
            finish_region && motion->hop_mode != 6u)
            source_commit_finish_crossing(session, motion);
    }
}

int portable_race_session_resample_checkpoint(PortableRaceSession *session,
    const PortableRaceMotionObservation *motion)
{
    if (session == NULL || motion == NULL || motion->hop_mode != 6u ||
        session->motion_pending || session->phase != PORTABLE_RACE_PHASE_RACING ||
        session->progress_kind == PORTABLE_RACE_PROGRESS_DEMO_V1)
        return 0;
    source_sample_checkpoint(session,motion);
    /* Arenas have checkpoint regions for recovery, not a circular course
     * centerline. Their result condition belongs to the Battle roster. */
    if (session->progress_kind == PORTABLE_RACE_PROGRESS_ARENA_RASTER_V1)
        return 1;
    return 1;
}

int portable_race_session_commit_motion(PortableRaceSession *session,
                                        const PortableRaceMotionObservation *motion)
{
    int64_t move_x;
    int64_t move_y;
    size_t segment;

    if (session == NULL || motion == NULL || !session->motion_pending ||
        session->phase != PORTABLE_RACE_PHASE_RACING)
        return 0;
    session->motion_pending = 0u;

    source_sample_checkpoint(session, motion);

    /* Centerline tracking is a host route/rescue aid. Its geometric movement
     * guard must not suppress the independent original raster/lap rules. */
    move_x = (int64_t)motion->new_x_q16 - motion->old_x_q16;
    move_y = (int64_t)motion->new_y_q16 - motion->old_y_q16;
    if ((move_x == 0 && move_y == 0) || !motion->drivable)
        return 1;
    if (session->progress_kind == PORTABLE_RACE_PROGRESS_DEMO_V1)
    {
        const int64_t limit = INT64_C(4) * INT64_C(65536);
        /* Bound components before squaring: an int32 position difference
         * can span 33 bits, but this legacy gate admits only four units. */
        if (move_x < -limit || move_x > limit || move_y < -limit || move_y > limit ||
            move_x * move_x + move_y * move_y > limit * limit)
            return 1;
    }
    segment = portable_route_nearest_segment(session->route, session->route_count,
                                             motion->new_x_q16, motion->new_y_q16);

    {
        const size_t passed =
            (segment + session->route_count - session->next_route_index) % session->route_count;
        const PortableRacePoint before =
            session->route[(segment + session->route_count - 1u) % session->route_count];
        const PortableRacePoint after = session->route[(segment + 1u) % session->route_count];

        if (passed <= 1u &&
            move_x * ((int64_t)after.x - before.x) + move_y * ((int64_t)after.y - before.y) > 0)
        {
            for (size_t index = 0; index <= passed; ++index)
            {
                if (session->progress_kind == PORTABLE_RACE_PROGRESS_DEMO_V1 &&
                    session->next_route_index == session->route_count - 1u)
                    session->finish_armed = 1u;
                session->next_route_index = (session->next_route_index + 1u) % session->route_count;
            }
        }
    }
    if (session->progress_kind == PORTABLE_RACE_PROGRESS_DEMO_V1)
        commit_finish_crossing(session, motion);
    return 1;
}

int portable_race_session_apply_rescue(PortableRaceSession *session)
{
    size_t previous;
    PortableRacePoint from;
    PortableRacePoint to;

    if (session == NULL || session->route == NULL || session->phase != PORTABLE_RACE_PHASE_RACING ||
        session->motion_pending)
        return 0;
    previous = (session->next_route_index + session->route_count - 1u) % session->route_count;
    from = session->route[previous];
    to = session->route[session->next_route_index];
    session->rescue_x_q16 = from.x * INT32_C(65536);
    session->rescue_y_q16 = from.y * INT32_C(65536);
    session->rescue_heading = portable_route_bearing(to.x - from.x, to.y - from.y);
    ++session->rescue_count;
    session->elapsed_ticks += session->rescue_penalty_ticks;
    return 1;
}

uint32_t portable_race_session_total_ticks(const PortableRaceSession *session)
{
    uint32_t total;

    if (session == NULL || !session->lap_started ||
        session->elapsed_ticks < session->lap_origin_ticks)
        return 0u;
    if (session->phase == PORTABLE_RACE_PHASE_FINISHED)
    {
        total = 0u;
        for (uint16_t lap = 0u; lap < session->lap_count; ++lap)
            total += session->lap_ticks[lap];
        return total;
    }
    total = session->elapsed_ticks - session->lap_origin_ticks;
    for (uint16_t lap = 0u; lap < session->laps_completed; ++lap)
        total += session->lap_ticks[lap];
    return total;
}

uint32_t portable_race_session_current_lap_ticks(
    const PortableRaceSession *session)
{
    if (session == NULL || !session->lap_started ||
        session->phase == PORTABLE_RACE_PHASE_FINISHED ||
        session->elapsed_ticks < session->lap_origin_ticks)
        return 0u;
    return session->elapsed_ticks - session->lap_origin_ticks;
}
