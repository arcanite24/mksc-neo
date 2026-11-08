#include "mesh_cup.h"
#include "gp_cup_scoring.h"
#include <stdio.h>
#include <string.h>

static PortableMeshStatus fail(const PortableMeshCupSession *cup, const char *detail, char *error,
                               size_t capacity)
{
    if (error && capacity)
        snprintf(error, capacity, "3D GP cup=%u course=%u state=%u: %s",
                 cup ? (unsigned)cup->definition.cup_id : 0u,
                 cup && cup->course_index < 4u
                     ? (unsigned)cup->definition.course_ids[cup->course_index]
                     : 0u,
                 cup ? (unsigned)cup->stage : 0u, detail);
    return PORTABLE_MESH_UNSUPPORTED;
}
static int permutation(const uint8_t order[8])
{
    unsigned seen = 0;
    for (unsigned i = 0; i < 8u; ++i)
    {
        if (order[i] >= 8u || (seen & (1u << order[i])))
            return 0;
        seen |= 1u << order[i];
    }
    return seen == 255u;
}
static int valid(const PortableMeshCupSession *cup)
{
    if (!cup || !cup->definition.cup_id || cup->course_index >= 4u || cup->engine_class > 2u ||
        cup->retry_credits > 3u || (uint32_t)cup->stage > PORTABLE_MESH_CUP_GAME_OVER ||
        !permutation(cup->grid_slots) || !permutation(cup->standing_order) ||
        !permutation(cup->race_order) || cup->scoring.initial_retry_credits > 3u)
        return 0;
    for (unsigned i = 0; i < 4u; ++i)
    {
        if (!cup->definition.course_ids[i])
            return 0;
        for (unsigned j = 0; j < i; ++j)
            if (cup->definition.course_ids[j] == cup->definition.course_ids[i])
                return 0;
    }
    for (unsigned i = 0; i < 8u; ++i)
    {
        if (!cup->driver_ids[i] || cup->scoring.finish_points[i] > 63u ||
            cup->standing_rank[i] > 7u)
            return 0;
        for (unsigned j = 0; j < i; ++j)
            if (cup->driver_ids[j] == cup->driver_ids[i])
                return 0;
    }
    return 1;
}
PortableMeshStatus portable_mesh_cup_init(PortableMeshCupSession *out,
                                          const PortableMeshCupDefinition *definition,
                                          const uint32_t driver_ids[8], unsigned engine_class,
                                          const PortableGpScoringRules *rules, char *error,
                                          size_t capacity)
{
    if (!out || !definition || !driver_ids || !rules)
        return fail(NULL, "missing cup/roster/scoring", error, capacity);
    PortableMeshCupSession next;
    memset(&next, 0, sizeof next);
    next.definition = *definition;
    memcpy(next.driver_ids, driver_ids, sizeof next.driver_ids);
    next.engine_class = engine_class;
    next.scoring = *rules;
    next.retry_credits = rules->initial_retry_credits;
    next.stage = PORTABLE_MESH_CUP_RACE;
    next.player_rank = UINT8_MAX;
    memset(next.race_ranks, UINT8_MAX, sizeof next.race_ranks);
    for (unsigned i = 0; i < 8u; ++i)
    {
        next.grid_slots[i] = (uint8_t)(7u - i);
        next.race_order[i] = (uint8_t)(7u - i);
        next.standing_order[i] = (uint8_t)i;
    }
    if (!valid(&next))
        return fail(&next, "invalid definition/roster/class/scoring", error, capacity);
    *out = next;
    if (error && capacity)
        error[0] = 0;
    return PORTABLE_MESH_OK;
}
PortableMeshStatus portable_mesh_cup_finish(PortableMeshCupSession *cup,
                                            const PortableMeshRaceSession *race, char *error,
                                            size_t capacity)
{
    if (!valid(cup) || !race || cup->stage != PORTABLE_MESH_CUP_RACE || race->mode != 1u ||
        race->human_count != 1u || race->state.player_count != 8u || !race->state.results_ready ||
        !race->course || race->course->id != cup->definition.course_ids[cup->course_index] ||
        !permutation(race->state.finish_order) ||
        race->state.racers[0].phase != PORTABLE_MESH_RACER_FINISHED ||
        !race->state.racers[0].finish_ticks || race->state.retired[0] ||
        race->state.racers[0].finish_ticks != race->state.racers[0].race_ticks ||
        !portable_mesh_kart_rules_valid(race->rules) ||
        !portable_course_3d_has_physics_profile(race->course, race->rules->profile_id))
        return fail(cup, "race is not a completed cup entry", error, capacity);
    for (unsigned rank = 0; rank < 8u; ++rank)
    {
        const unsigned racer = race->state.finish_order[rank];
        if (race->state.finish_rank[racer] != rank + 1u ||
            race->spawn_grids[racer] != cup->grid_slots[racer])
            return fail(cup, "race finish ranks or start grid mismatch", error, capacity);
    }
    PortableCourse3DMeta meta;
    if (!portable_course_3d_meta(race->course, &meta) || !(meta.modes & 1u) ||
        meta.cup_id != cup->definition.cup_id || meta.cup_order != cup->course_index ||
        race->state.racers[0].lap != meta.laps)
        return fail(cup, "course/cup/slot/lap mismatch", error, capacity);
    PortableMeshCupSession next = *cup;
    memcpy(next.race_order, race->state.finish_order, 8);
    next.finish_ticks = race->state.racers[0].finish_ticks;
    for (unsigned rank = 0; rank < 8u; ++rank)
        if (next.race_order[rank] == 0u)
            next.player_rank = (uint8_t)rank;
    memset(next.awards, 0, sizeof next.awards);
    if (next.player_rank >= 4u)
        next.stage = next.retry_credits ? PORTABLE_MESH_CUP_RETRY : PORTABLE_MESH_CUP_GAME_OVER;
    else
    {
        if (!portable_gp_cup_scoring(&next.scoring, next.race_order, cup->points, next.points,
                                     next.awards, next.standing_order, next.standing_rank))
            return fail(cup, "invalid points/order", error, capacity);
        next.race_ticks[next.course_index] = next.finish_ticks;
        next.race_ranks[next.course_index] = next.player_rank;
        next.stage = PORTABLE_MESH_CUP_STANDINGS;
    }
    *cup = next;
    if (error && capacity)
        error[0] = 0;
    return PORTABLE_MESH_OK;
}
PortableMeshStatus portable_mesh_cup_advance(PortableMeshCupSession *cup, char *error,
                                             size_t capacity)
{
    if (!valid(cup) || cup->stage != PORTABLE_MESH_CUP_STANDINGS)
        return fail(cup, "advance requires successful standings", error, capacity);
    PortableMeshCupSession next = *cup;
    if (next.course_index == 3u)
        next.stage = PORTABLE_MESH_CUP_COMPLETE;
    else
    {
        for (unsigned rank = 0; rank < 8u; ++rank)
            next.grid_slots[next.race_order[rank]] = (uint8_t)rank;
        ++next.course_index;
        next.player_rank = UINT8_MAX;
        next.finish_ticks = 0;
        memset(next.awards, 0, sizeof next.awards);
        next.stage = PORTABLE_MESH_CUP_RACE;
    }
    *cup = next;
    if (error && capacity)
        error[0] = 0;
    return PORTABLE_MESH_OK;
}
PortableMeshStatus portable_mesh_cup_retry(PortableMeshCupSession *cup, char *error,
                                           size_t capacity)
{
    if (!valid(cup) ||
        (cup->stage != PORTABLE_MESH_CUP_RETRY && cup->stage != PORTABLE_MESH_CUP_RACE) ||
        !cup->retry_credits)
        return fail(cup, "retry requires an active/failed race and credit", error, capacity);
    PortableMeshCupSession next = *cup;
    --next.retry_credits;
    next.stage = PORTABLE_MESH_CUP_RACE;
    next.player_rank = UINT8_MAX;
    next.finish_ticks = 0;
    memset(next.awards, 0, sizeof next.awards);
    *cup = next;
    if (error && capacity)
        error[0] = 0;
    return PORTABLE_MESH_OK;
}
