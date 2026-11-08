#include "gp_cup_session.h"
#include "gp_cup_scoring.h"
#include "track_set.h"

#include <stddef.h>
#include <string.h>

/* GP cups exist in the GBA, Extra and custom sets; arenas have none. */
static int gp_track_set(uint8_t track_set)
{
    return track_set < PORTABLE_TRACK_SET_COUNT &&
           track_set != PORTABLE_TRACK_SET_ARENA;
}

PortableGpCupFinishRoute portable_gp_cup_session_finish_route(
    const PortableGpRaceSession *completed_race)
{
    if (completed_race == NULL ||
        completed_race->runtime.session.phase != PORTABLE_RACE_PHASE_FINISHED ||
        completed_race->runtime.session.laps_completed !=
            completed_race->runtime.session.lap_count ||
        (completed_race->roster.race_phase_7d4 & UINT8_C(0x0f)) != 5u ||
        (completed_race->roster.racers[0].source_flags & UINT32_C(0x20)) == 0u)
        return PORTABLE_GP_CUP_FINISH_NOT_READY;

    return (completed_race->roster.racers[0].rank_flags_ba >> 1u) <= 3u
        ? PORTABLE_GP_CUP_FINISH_STANDINGS
        : PORTABLE_GP_CUP_FINISH_RETRY_MENU;
}

static int valid_order(const uint8_t order[PORTABLE_GP_RACER_COUNT])
{
    uint8_t seen = 0u;

    for (size_t rank = 0u; rank < PORTABLE_GP_RACER_COUNT; ++rank)
    {
        const uint8_t racer = order[rank];
        const uint8_t bit = racer < PORTABLE_GP_RACER_COUNT
                                ? (uint8_t)(1u << racer) : 0u;
        if (bit == 0u || (seen & bit) != 0u)
            return 0;
        seen = (uint8_t)(seen | bit);
    }
    return seen == UINT8_C(0xff);
}

static uint8_t standings_gap_class(const PortableGpCupSession *previous)
{
    uint8_t leader = 0u;
    uint8_t player = 0u;

    if (previous != NULL)
    {
        player = previous->cumulative_points[0];
        for (size_t racer = 0u; racer < PORTABLE_GP_RACER_COUNT; ++racer)
            if (previous->cumulative_points[racer] > leader)
                leader = previous->cumulative_points[racer];
    }
    if (leader <= (uint8_t)(player + 2u))
        return 0u;
    if (leader <= (uint8_t)(player + 8u))
        return 1u;
    return 2u;
}

PortableGpCupSessionStatus portable_gp_cup_session_begin_standings(
    const PortableGpRaceSession *completed_race,
    const PortableGpCupSession *previous,
    uint8_t cup_index, uint8_t course_in_cup, uint8_t course_header_index,
    uint8_t track_set,
    PortableGpCupSession *session)
{
    PortableGpCupSession next = {0};
    const uint16_t flags = completed_race != NULL
        ? completed_race->roster.race_mode_flags_7da : 0u;

    if (completed_race == NULL || session == NULL)
        return PORTABLE_GP_CUP_SESSION_INVALID_ARGUMENT;
    if (completed_race->scoring_rules==NULL ||
        !gp_track_set(track_set) || cup_index >= 5u || course_in_cup >= 4u || completed_race->engine_class >= 3u ||
        completed_race->retry_credits > 3u ||
        !valid_order(completed_race->roster.standings))
        return PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE;
    if (completed_race->runtime.session.phase != PORTABLE_RACE_PHASE_FINISHED ||
        completed_race->runtime.session.laps_completed !=
            completed_race->runtime.session.lap_count ||
        (completed_race->roster.race_phase_7d4 & UINT8_C(0x0f)) != 5u)
        return PORTABLE_GP_CUP_SESSION_RACE_NOT_READY;
    /* The race finish controller changes GP mode 0x0100 to 0x2200 before
     * presenting standings.  Preserve 0x4100 for the recovered results
     * entry used by the typed cup route. */
    if ((flags & UINT16_C(0x0f00)) != UINT16_C(0x0100) &&
        flags != UINT16_C(0x2200))
        return PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE;
    if (previous != NULL &&
        (course_in_cup == 0u || previous->cup_index != cup_index || previous->track_set != track_set ||
         previous->course_in_cup != course_in_cup ||
         previous->stage != PORTABLE_GP_CUP_STAGE_NEXT_RACE ||
         previous->engine_class != completed_race->engine_class ||
         previous->course_id != completed_race->roster.course_id ||
         previous->course_header_index != course_header_index))
        return PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE;

    next.course_id = completed_race->roster.course_id;
    next.terrain_effect_counter = completed_race->surface_trails.eligible_counter;
    next.terrain_effect_phase = completed_race->surface_trails.side;
    next.item_art_clock = completed_race->item_art_clock;
    next.marker_clock = completed_race->marker_clock;
    next.marker_display_clock = completed_race->marker_display_clock;
    next.race_mode_flags = flags;
    next.cup_index = cup_index;
    next.track_set = track_set;
    next.engine_class = completed_race->engine_class;
    next.retry_credits = completed_race->retry_credits;
    next.course_in_cup = course_in_cup;
    next.course_header_index = course_header_index;
    next.selected_driver_class =
        completed_race->roster.racers[0].driver_class;
    /* Results state 26 calls track_getCupIndex (the within-cup course
     * position): rows 0-2 select scene 8, while the fourth selects scene 7. */
    next.next_mode = course_in_cup < 3u ? 8u : 7u;
    next.stage = PORTABLE_GP_CUP_STAGE_STANDINGS;
    if (previous != NULL)
        memcpy(next.cumulative_points, previous->cumulative_points,
               sizeof next.cumulative_points);

    for (size_t racer = 0u; racer < PORTABLE_GP_RACER_COUNT; ++racer)
    {
        next.racer_driver_classes[racer] =
            completed_race->roster.racers[racer].driver_class;
        next.rows[racer].racer_index = (uint8_t)racer;
        next.rows[racer].driver_class = next.racer_driver_classes[racer];
        next.rows[racer].points_before = next.cumulative_points[racer];
    }
    uint8_t awards[8], standing_ranks[8];
    if (!portable_gp_cup_scoring(completed_race->scoring_rules, completed_race->roster.standings,
                                 next.cumulative_points, next.cumulative_points, awards,
                                 next.standings_order, standing_ranks))
        return PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE;
    for (size_t rank = 0u; rank < PORTABLE_GP_RACER_COUNT; ++rank)
    {
        const uint8_t racer = completed_race->roster.standings[rank];
        PortableGpStandingRow *row = &next.rows[racer];
        next.race_order[rank] = racer;
        row->finish_rank = (uint8_t)rank;
        row->points_awarded = awards[racer];
        row->points_after = next.cumulative_points[racer];
        row->standing_rank = standing_ranks[racer];
    }
    if (completed_race->roster.racers[0].coin_count_178 > 99u)
        return PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE;
    if(previous!=NULL)memcpy(next.finishing_coins,previous->finishing_coins,
        sizeof next.finishing_coins);
    next.finishing_coins[course_in_cup]=(uint8_t)completed_race->roster.racers[0].coin_count_178;
    if(previous!=NULL){
        memcpy(next.course_ids,previous->course_ids,sizeof next.course_ids);
        memcpy(next.race_ticks,previous->race_ticks,sizeof next.race_ticks);
        memcpy(next.race_centiseconds,previous->race_centiseconds,sizeof next.race_centiseconds);
        memcpy(next.finishing_ranks,previous->finishing_ranks,sizeof next.finishing_ranks);
        memcpy(next.finishing_points,previous->finishing_points,sizeof next.finishing_points);
    }
    next.course_ids[course_in_cup]=completed_race->roster.course_id;
    next.race_ticks[course_in_cup]=completed_race->runtime.session.elapsed_ticks;
    next.race_centiseconds[course_in_cup]=completed_race->source_result_clock.finish_centiseconds;
    next.finishing_ranks[course_in_cup]=next.rows[0].finish_rank;
    next.finishing_points[course_in_cup]=next.rows[0].points_awarded;
    if (completed_race->rules.route_count != 0u &&
        completed_race->rules.route_count <= UINT16_MAX)
    {
        PortableGpDrivingQualityRace quality_race = {
            .rules = completed_race->scoring_rules != NULL &&
                completed_race->scoring_rules->has_quality_rules ?
                &completed_race->scoring_rules->quality : NULL,
            .course_linear_index = course_in_cup,
            .standings_gap_class = standings_gap_class(previous),
            .finish_rank = next.rows[0].finish_rank,
            .speed_class = completed_race->engine_class,
            .driver_class = next.selected_driver_class,
            .retro_course = track_set == PORTABLE_TRACK_SET_EXTRA,
            .transition_countdown = completed_race->retry_credits,
            .coins = completed_race->roster.racers[0].coin_count_178,
            .route_count = (uint16_t)completed_race->rules.route_count,
            .race_ticks = completed_race->runtime.session.elapsed_ticks,
        };
        int16_t ignored_score;
        next.driving_quality = completed_race->driving_quality;
        if (previous != NULL && previous->driving_quality_sample_mask != 0u)
            memcpy(next.driving_quality.samples,
                   previous->driving_quality.samples,
                   sizeof next.driving_quality.samples);
        if (portable_gp_driving_quality_finish_race(
                &next.driving_quality, &quality_race, &ignored_score) !=
            PORTABLE_GP_DRIVING_QUALITY_OK)
            return PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE;
        next.driving_quality_sample_mask = (uint8_t)(
            (previous != NULL ? previous->driving_quality_sample_mask : 0u) |
            (1u << course_in_cup));
    }
    *session = next;
    return PORTABLE_GP_CUP_SESSION_OK;
}

PortableGpCupSessionStatus portable_gp_cup_session_advance(
    const PortableGpCupSession *standings,
    const PortableGpCupDefinition *definition,
    PortableGpCupSession *next_race)
{
    PortableGpCupSession next;
    uint8_t next_index;

    if (standings == NULL || definition == NULL || next_race == NULL)
        return PORTABLE_GP_CUP_SESSION_INVALID_ARGUMENT;
    if (!gp_track_set(definition->track_set) || standings->track_set != definition->track_set ||
        definition->cup_index >= 5u || standings->cup_index != definition->cup_index ||
        standings->course_in_cup >= 3u ||
        standings->course_header_index !=
            definition->source_headers[standings->course_in_cup] ||
        standings->course_id != definition->course_ids[standings->course_in_cup] ||
        standings->stage != PORTABLE_GP_CUP_STAGE_STANDINGS ||
        standings->next_mode != 8u ||
        (standings->race_mode_flags & UINT16_C(0x0f00)) != UINT16_C(0x0100))
        return PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE;

    for (size_t track = 0u; track < 4u; ++track)
    {
        if (definition->course_ids[track] == 0u || definition->source_headers[track] >=
                portable_track_set_header_limit((PortableTrackSet)definition->track_set))
            return PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE;
        for (size_t previous = 0u; previous < track; ++previous)
            if (definition->course_ids[previous] == definition->course_ids[track] ||
                definition->source_headers[previous] == definition->source_headers[track])
                return PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE;
    }
    next = *standings;
    next_index = (uint8_t)(standings->course_in_cup + 1u);
    next.course_in_cup = next_index;
    next.course_header_index = definition->source_headers[next_index];
    next.course_id = definition->course_ids[next_index];
    next.next_mode = 8u;
    next.stage = PORTABLE_GP_CUP_STAGE_NEXT_RACE;
    *next_race = next;
    return PORTABLE_GP_CUP_SESSION_OK;
}

PortableGpCupSessionStatus portable_gp_cup_session_resume_race(
    const PortableGpCupSession *next_race,PortableGpRaceSession *race)
{
    if(next_race==NULL||race==NULL)return PORTABLE_GP_CUP_SESSION_INVALID_ARGUMENT;
    if(next_race->stage!=PORTABLE_GP_CUP_STAGE_NEXT_RACE||
       next_race->course_in_cup==0u||next_race->course_in_cup>=4u||
       !gp_track_set(next_race->track_set)||next_race->retry_credits>3u||next_race->cup_index>=5u||next_race->engine_class!=race->engine_class||
       next_race->course_id!=race->roster.course_id||
       next_race->terrain_effect_counter>=4u||next_race->terrain_effect_phase>=2u||
       next_race->item_art_clock.pending!=0u||
       race->runtime.session.phase!=PORTABLE_RACE_PHASE_COUNTDOWN||
       race->runtime.session.elapsed_ticks!=0u||
       race->runtime.session.countdown_remaining!=race->runtime.session.countdown_ticks)
        return PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE;
    for(size_t index=0u;index<PORTABLE_GP_RACER_COUNT;++index)
        if(next_race->racer_driver_classes[index]!=race->roster.racers[index].driver_class)
            return PORTABLE_GP_CUP_SESSION_UNSUPPORTED_STATE;
    /* 48D44 calls43830(0) on every race setup; next-course loading also
     * clears the scene clock. 4389C's art clock and terrain effect phase
     * remain independent of that scene-owned state. */
    race->retry_credits=next_race->retry_credits;
    race->source_rng_state=0u;
    race->source_frame_clock=0u;
    race->surface_trails.eligible_counter=(uint8_t)next_race->terrain_effect_counter;
    race->surface_trails.side=(uint8_t)next_race->terrain_effect_phase;
    race->item_art_clock=next_race->item_art_clock;
    race->marker_clock=next_race->marker_clock;
    race->marker_display_clock=next_race->marker_display_clock;
    return PORTABLE_GP_CUP_SESSION_OK;
}
