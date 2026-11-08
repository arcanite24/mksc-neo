#include "mesh_course_coin_turn.h"
#include "mesh_race.h"
#include "mesh_time_trial.h"
#include "mesh_items_state.h"
#include "mesh_battle_pilot.h"
#include <stdio.h>
#include <string.h>
static uint32_t stages_per_lap(const PortableMeshRaceSession *session);

static PortableMeshStatus race_error(const PortableMeshRaceSession *session,
                                     PortableMeshStatus status, unsigned racer, const char *stage,
                                     char *error, size_t capacity)
{
    if (error && capacity)
        snprintf(error, capacity, "3D race mode=%u course=%u tick=%u racer=%u: %s (%s)",
                 session ? (unsigned)session->mode : 0u,
                 session && session->course ? (unsigned)session->course->id : 0u,
                 session ? (unsigned)session->state.tick : 0u, racer,
                 portable_mesh_status_name(status), stage);
    return status;
}

PortableMeshStatus portable_mesh_race_init(PortableMeshRaceSession *session,
                                           const PortableCourse3DView *course,
                                           const PortableMeshKartRules *rules, uint32_t players,
                                           const uint32_t *grids, char *error, size_t capacity)
{
    return portable_mesh_race_init_mode(session, course, rules, 8u, players, players, grids, error,
                                        capacity);
}

PortableMeshStatus portable_mesh_race_init_mode(PortableMeshRaceSession *session,
                                                const PortableCourse3DView *course,
                                                const PortableMeshKartRules *rules, uint32_t mode,
                                                uint32_t humans, uint32_t players,
                                                const uint32_t *grids, char *error, size_t capacity)
{
    const int local =
        mode == 8u && humans == players && players >= 2u && players <= PORTABLE_MESH_LOCAL_MAX;
    const int battle = mode == 16u && humans >= 1u && humans <= 4u && players >= 2u &&
                       players <= 8u && humans <= players;
    const int field = (mode == 1u || mode == 4u) && humans == 1u && players == 8u;
    if (!session || !course || (!local && !field && !battle) ||
        !portable_mesh_kart_rules_valid(rules))
        return race_error(NULL, PORTABLE_MESH_INVALID, 0, "mode/field setup", error, capacity);
    PortableMeshRaceSession next;
    memset(&next, 0, sizeof(next));
    next.course = course;
    next.rules = rules;
    next.mode = mode;
    next.human_count = humans;
    next.state.player_count = players;
    memset(next.state.finish_order, UINT8_MAX, sizeof(next.state.finish_order));
    for (uint32_t i = 0; i < players; ++i)
    {
        next.spawn_grids[i] = grids ? grids[i] : i;
        /* Sharing a start would make spawn overlap depend on player order. */
        for (uint32_t j = 0; j < i; ++j)
            if (next.spawn_grids[j] == next.spawn_grids[i])
                return race_error(&next, PORTABLE_MESH_INVALID, i, "duplicate grid", error,
                                  capacity);
        PortableMeshRacer racer;
        PortableMeshStatus status = portable_mesh_racer_init(
            &racer, course, rules, next.spawn_grids[i], mode, error, capacity);
        if (status != PORTABLE_MESH_OK)
            return status;
        next.fall_limit_q16 = racer.fall_limit_q16;
        next.state.racers[i] = racer.state;
        next.state.standing_order[i] = (uint8_t)i;
        next.state.standing_rank[i] = (uint8_t)(i + 1u);
        if (i >= humans)
        {
            status = portable_mesh_cpu_init(&next.cpu[i], &racer, PORTABLE_COURSE_3D_ABSENT, error,
                                            capacity);
            if (status != PORTABLE_MESH_OK)
                return status;
        }
    }
    if (mode == 16u)
    {
        const PortableMeshBattleConfig config = {(uint8_t)players, 3, 1};
        if (!portable_mesh_battle_init(&next.battle, &config, error, capacity))
            return PORTABLE_MESH_INVALID;
    }
    next.checkpoint_stages = mode == 16u ? 0u : stages_per_lap(&next);
    *session = next;
    if (error && capacity)
        error[0] = 0;
    return PORTABLE_MESH_OK;
}

static PortableMeshItemsConfig item_config(const PortableMeshRaceSession *session)
{
    PortableMeshItemsConfig config = {.course = session->course,
                                      .rules = session->item_rules,
                                      .kart_rules = session->rules,
                                      .mode = session->mode,
                                      .count = (uint8_t)session->state.player_count,
                                      .human_mask = (uint8_t)((1u << session->human_count) - 1u),
                                      .geometry = session->geometry};
    if (session->mode == 16u)
        for (unsigned i = 0; i < session->state.player_count; ++i)
        {
            if (!session->battle.balloons[i])
                config.inactive_mask |= (uint8_t)(1u << i);
            if (session->battle.elimination[i].revenge_bomb)
                config.bomb_mask |= (uint8_t)(1u << i);
        }
    return config;
}

static PortableMeshStatus item_status(PortableMeshItemsResult result)
{
    return result == PORTABLE_MESH_ITEMS_BUDGET ? PORTABLE_MESH_COLLISION_BUDGET
           : result == PORTABLE_MESH_ITEMS_OK   ? PORTABLE_MESH_OK
                                                : PORTABLE_MESH_INVALID;
}

static const PortableMeshKartRules *racer_rules(const PortableMeshRaceSession *session,
                                                 uint32_t racer)
{
    return session->racer_rules[racer] ? session->racer_rules[racer] : session->rules;
}

PortableMeshStatus portable_mesh_race_set_racer_rules(PortableMeshRaceSession *session,
                                                      uint32_t racer,
                                                      const PortableMeshKartRules *rules,
                                                      char *error, size_t capacity)
{
    if (!session || session->state.tick || racer >= session->state.player_count ||
        (rules && !portable_mesh_kart_rules_valid(rules)) ||
        (rules && session->items_enabled &&
         !portable_mesh_item_rules_fit_profile(session->item_rules, rules)))
        return race_error(session, PORTABLE_MESH_INVALID, racer, "driver profile", error,
                          capacity);
    session->racer_rules[racer] = rules;
    return PORTABLE_MESH_OK;
}

PortableMeshStatus portable_mesh_race_enable_items(PortableMeshRaceSession *session,
                                                   const PortableMeshItemRules *rules,
                                                   uint32_t seed, char *error, size_t capacity)
{
    if (!session || !session->course || session->state.tick || session->items_enabled ||
        session->state.player_count < 2u || session->state.player_count > PORTABLE_MESH_RACE_MAX ||
        !session->human_count || session->human_count > session->state.player_count ||
        !((session->mode == 8u && session->human_count == session->state.player_count &&
           session->human_count <= PORTABLE_MESH_LOCAL_MAX) ||
          (session->mode == 16u && session->human_count <= PORTABLE_MESH_LOCAL_MAX) ||
          ((session->mode == 1u || session->mode == 4u) && session->human_count == 1u &&
           session->state.player_count == PORTABLE_MESH_RACE_MAX)))
        return race_error(session, PORTABLE_MESH_INVALID, 0, "item owner setup", error, capacity);
    PortableMeshRaceSession next = *session;
    next.item_rules = rules;
    const PortableMeshItemsConfig config = item_config(&next);
    const PortableMeshItemsResult result =
        portable_mesh_items_init(&next.items, &config, seed, error, capacity);
    if (result != PORTABLE_MESH_ITEMS_OK)
        return item_status(result);
    next.items_enabled = 1;
    memset(&next.item_events, 0, sizeof next.item_events);
    *session = next;
    return PORTABLE_MESH_OK;
}

/* A general graph-based standings measure: stage first, then projection from
 * the last admitted gate centre towards its successors. This never grants a
 * gate or lap. Connected alternatives at the same stage share the same unit. */
static uint32_t fraction_q16(int64_t numerator, int64_t denominator)
{
    if (numerator <= 0 || denominator <= 0)
        return 0;
    if (numerator >= denominator)
        return 65536u;
    uint64_t remainder = (uint64_t)numerator;
    uint32_t result = 0;
    for (unsigned bit = 0; bit < 16u; ++bit)
    {
        remainder *= 2u;
        result *= 2u;
        if (remainder >= (uint64_t)denominator)
        {
            remainder -= (uint64_t)denominator;
            ++result;
        }
    }
    return result;
}
static uint32_t stages_per_lap(const PortableMeshRaceSession *session)
{
    const PortableCourse3DTable *table =
        portable_course_3d_table(session->course, PORTABLE_COURSE_3D_TAG('C', 'P', 'N', 'T'));
    uint32_t stages = 1;
    if (table)
        for (uint32_t i = 0; i < table->count; ++i)
        {
            PortableCourse3DCheckpoint gate;
            if (portable_course_3d_checkpoint(session->course, i, &gate) && gate.stage >= stages)
                stages = gate.stage + 1u;
        }
    return stages;
}
static uint64_t progress(const PortableMeshRaceSession *session, uint32_t index, uint32_t stages)
{
    const PortableMeshRacerState *racer = &session->state.racers[index];
    uint32_t along = 0;
    uint64_t nearest = UINT64_MAX;
    PortableCourse3DCheckpoint previous;
    if (portable_course_3d_checkpoint(session->course, racer->checkpoint, &previous))
        for (uint32_t edge = 0; edge < previous.edge_count; ++edge)
        {
            uint32_t target;
            PortableCourse3DCheckpoint next;
            if (!portable_course_3d_checkpoint_edge(session->course, previous.edge_first + edge,
                                                    &target) ||
                !portable_course_3d_checkpoint(session->course, target, &next))
                continue;
            int64_t numerator = 0, denominator = 0;
            for (unsigned axis = 0; axis < 3u; ++axis)
            {
                const int64_t delta = (int64_t)next.center_q16[axis] - previous.center_q16[axis];
                numerator +=
                    ((int64_t)racer->kart.position_q16[axis] - previous.center_q16[axis]) * delta;
                denominator += delta * delta;
            }
            const uint32_t candidate = fraction_q16(numerator, denominator);
            uint64_t square = 0;
            for (unsigned axis = 0; axis < 3u; ++axis)
            {
                const int64_t delta = (int64_t)next.center_q16[axis] - previous.center_q16[axis];
                const int64_t offset = (int64_t)racer->kart.position_q16[axis] -
                                       previous.center_q16[axis] - delta * candidate / 65536;
                square += (uint64_t)(offset * offset);
            }
            if (square < nearest || (square == nearest && candidate > along))
            {
                nearest = square;
                along = candidate;
            }
        }
    return ((uint64_t)racer->lap * stages + racer->checkpoint_count) * 65536u + along;
}
static void standings(PortableMeshRaceSession *session)
{
    const uint32_t count = session->state.player_count, stages = session->checkpoint_stages;
    uint64_t values[PORTABLE_MESH_RACE_MAX];
    for (uint32_t i = 0; i < count; ++i)
    {
        values[i] = progress(session, i, stages);
        unsigned at = i;
        while (at)
        {
            const unsigned prior = session->state.standing_order[at - 1u];
            const uint8_t a = session->state.finish_rank[i], b = session->state.finish_rank[prior];
            const int ahead = a ? (!b || a < b) : (!b && values[i] > values[prior]);
            if (!ahead)
                break;
            session->state.standing_order[at] = (uint8_t)prior;
            --at;
        }
        session->state.standing_order[at] = (uint8_t)i;
    }
    for (uint32_t rank = 0; rank < count; ++rank)
        session->state.standing_rank[session->state.standing_order[rank]] = (uint8_t)(rank + 1u);
}

int portable_mesh_race_result_ticks(const PortableMeshRaceSession *session, uint32_t racer,
                                    uint32_t *ticks, int *estimated)
{
    if (!session || !ticks || !estimated || !session->course || session->mode == 16u ||
        !session->state.results_ready || racer >= session->state.player_count)
        return 0;
    const PortableMeshRacerState *state = &session->state.racers[racer];
    uint64_t value = state->finish_ticks;
    const int estimate = session->mode != 8u && state->phase != PORTABLE_MESH_RACER_FINISHED;
    if (estimate)
    {
        PortableCourse3DMeta meta;
        if (!portable_course_3d_meta(session->course, &meta))
            return 0;
        const uint32_t stages = session->checkpoint_stages;
        const uint64_t total = (uint64_t)meta.laps * stages * 65536u;
        if (!total)
            return 0;
        const uint64_t done = progress(session, racer, stages);
        const uint64_t remaining = done < total ? total - done : 0;
        const uint32_t ratio = fraction_q16((int64_t)remaining, (int64_t)total);
        value = state->race_ticks +
                ((uint64_t)session->state.racers[0].finish_ticks * ratio + 65535u) / 65536u;
    }
    *ticks = value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
    *estimated = estimate;
    return 1;
}

PortableMeshStatus portable_mesh_race_enable_actors(PortableMeshRaceSession *session,
                                                    const PortableMeshCourseActorsConfig *config,
                                                    char *error, size_t capacity)
{
    if (!session || !config || !session->course || config->course_id != session->course->id ||
        session->state.tick || session->actor_config ||
        (!session->items_enabled &&
         (!config->sky.enabled || portable_mesh_course_actors_needs_items(config))))
        return race_error(session, PORTABLE_MESH_INVALID, 0, "actor configuration", error,
                          capacity);
    PortableMeshCourseActors staged;
    if (!portable_mesh_course_actors_init_on_course(config, session->geometry, &staged, error, capacity))
        return PORTABLE_MESH_INVALID;
    session->actor_config = config;
    session->actors = staged;
    memset(&session->actor_events, 0, sizeof session->actor_events);
    return PORTABLE_MESH_OK;
}

PortableMeshStatus portable_mesh_race_tick_items(PortableMeshRaceSession *session,
                                                 const PortableMeshKartInput *inputs,
                                                 const PortableMeshItemControls *item_controls,
                                                 PortableMeshKartEvents *events, char *error,
                                                 size_t capacity)
{
    if (!session || !inputs || !events || !session->course ||
        !portable_mesh_kart_rules_valid(session->rules) || session->state.player_count < 2u ||
        session->state.player_count > PORTABLE_MESH_RACE_MAX || session->human_count < 1u ||
        session->human_count > session->state.player_count ||
        session->state.finish_count > session->state.player_count ||
        session->state.tick == UINT32_MAX)
        return race_error(session, PORTABLE_MESH_INVALID, 0, "unprepared world", error, capacity);
    uint32_t declared_items = 0;
    PortableCourse3DMeta meta;
    if (session->items_enabled > 1u || !portable_course_3d_meta(session->course, &meta))
        return race_error(session, PORTABLE_MESH_INVALID, 0, "item metadata", error, capacity);
    /* No properties means no MIR declaration. Preserve the existing standalone
     * borrowed-course API without demanding a pack for unrelated KPH metadata. */
    if (meta.property_count &&
        !portable_course_3d_item_rules(session->course, &declared_items, error, capacity))
        return PORTABLE_MESH_INVALID;
    PortableCourse3DProperty actor_declaration;
    if ((portable_course_3d_property_find(session->course, meta.property_first, meta.property_count,
                                          "mksc:actor-rules", &actor_declaration) ||
         portable_course_3d_property_find(session->course, meta.property_first, meta.property_count,
                                          "mksc:coin-rules", &actor_declaration) ||
         portable_course_3d_property_find(session->course, meta.property_first, meta.property_count,
                                          "mksc:sky-motion", &actor_declaration)) &&
        !session->actor_config)
        return race_error(session, PORTABLE_MESH_UNSUPPORTED, 0,
                          "declared source actor owner not enabled", error, capacity);
    if (session->actor_config &&
        ((!session->items_enabled &&
          (!session->actor_config->sky.enabled ||
           portable_mesh_course_actors_needs_items(session->actor_config))) ||
         session->actor_config->course_id != session->course->id ||
         session->actors.count != session->actor_config->count ||
         session->actors.snowman_count != session->actor_config->snowman_count ||
         session->actors.penguin_count != session->actor_config->penguin_count ||
         session->actors.mouse_count != session->actor_config->mouse_count ||
         session->actors.piranha_count != session->actor_config->piranha_count ||
         session->actors.seagull_count != session->actor_config->seagull_count ||
         session->actors.lava_count != session->actor_config->lava_count ||
         session->actors.thwomp_count != session->actor_config->thwomp_count ||
         session->actors.cannonball_count != session->actor_config->cannonball_count ||
         session->actors.boo_count != session->actor_config->boo_count ||
         session->actors.rock_count != session->actor_config->rock_count ||
         session->actors.teepee_count != session->actor_config->teepee_count ||
         session->actors.falling_star_count != session->actor_config->falling_star_count ||
         session->actors.thundercloud_count != session->actor_config->thundercloud_count ||
         session->actors.common_prop_count != session->actor_config->common_prop_count ||
         session->actors.mechakoopa_count != session->actor_config->mechakoopa_count ||
         session->actors.kamek_count != session->actor_config->kamek_count ||
         session->actors.pier_boo_count != session->actor_config->pier_boo_count ||
         !portable_mesh_sky_valid(&session->actor_config->sky, &session->actors.sky) ||
         !portable_mesh_course_coins_valid(&session->actor_config->coins, &session->actors.coins)))
        return race_error(session, PORTABLE_MESH_INVALID, 0, "actor profile rebind", error,
                          capacity);
    if (declared_items && !session->items_enabled)
        return race_error(session, PORTABLE_MESH_UNSUPPORTED, 0,
                          "declared MIR item owner not enabled", error, capacity);
    if (session->items_enabled &&
        (!portable_mesh_item_rules_valid(session->item_rules) ||
         !portable_mesh_item_rules_fit_profile(session->item_rules, session->rules)))
        return race_error(session, PORTABLE_MESH_INVALID, 0, "item profile rebind", error,
                          capacity);
    const uint32_t count = session->state.player_count;
    for (uint32_t i = 0; i < count; ++i)
        if (session->racer_rules[i] &&
            (!portable_mesh_kart_rules_valid(session->racer_rules[i]) ||
             (session->items_enabled &&
              !portable_mesh_item_rules_fit_profile(session->item_rules, session->racer_rules[i]))))
            return race_error(session, PORTABLE_MESH_INVALID, i, "driver profile rebind", error,
                              capacity);
    for (uint32_t i = 0; i < session->human_count; ++i)
        if (inputs[i].buttons > 15u ||
            (session->items_enabled && item_controls &&
             (item_controls[i].item_pressed > 1u || item_controls[i].aim_backward > 1u ||
              item_controls[i].aim_forward > 1u)))
            return race_error(session, PORTABLE_MESH_INVALID, i, "input", error, capacity);
    if (session->state.results_ready)
    {
        memset(events, 0, count * sizeof(*events));
        if (error && capacity)
            error[0] = 0;
        return PORTABLE_MESH_OK;
    }
    PortableMeshRaceSession next = *session;
    PortableMeshRacer racers[PORTABLE_MESH_RACE_MAX];
    PortableMeshRacerTurn turns[PORTABLE_MESH_RACE_MAX];
    PortableMeshKartInput controls[PORTABLE_MESH_RACE_MAX];
    PortableMeshItemControls cpu_items[PORTABLE_MESH_RACE_MAX] = {0};
    memcpy(controls, inputs, session->human_count * sizeof(*inputs));
    /* Bullet Bill autopilots its human rider along the CPU route, as the
     * original does; the route controller starts where the ride starts. */
    next.bullet_pilots = 0;
    for (uint32_t i = 0; session->items_enabled && session->mode != 16u && i < session->human_count; ++i)
    {
        if (!session->items.status.racers[i].bullet_ticks)
            continue;
        const PortableMeshRacer racer = {session->course,         racer_rules(session, i),
                                         session->fall_limit_q16, session->spawn_grids[i],
                                         session->mode,           session->state.racers[i],
                                         session->geometry};
        PortableMeshCpuState pilot = session->cpu[i];
        if (!(session->bullet_pilots & (1u << i)) &&
            portable_mesh_cpu_init(&pilot, &racer, PORTABLE_COURSE_3D_ABSENT, error, capacity) != PORTABLE_MESH_OK)
            return race_error(session, PORTABLE_MESH_INVALID, i, "Bullet Bill route", error, capacity);
        const PortableMeshStatus status =
            portable_mesh_cpu_input(&next.cpu[i], &pilot, &racer, &controls[i], error, capacity);
        if (status != PORTABLE_MESH_OK)
            return status;
        controls[i].buttons = (uint16_t)((controls[i].buttons & ~PORTABLE_MESH_RESCUE) | PORTABLE_MESH_ACCELERATE);
        next.bullet_pilots |= (uint8_t)(1u << i);
    }
    for (uint32_t i = session->human_count; i < count; ++i)
    {
        if (session->mode == 16u)
        {
            const PortableMeshStatus status = portable_mesh_battle_pilot(
                session, i, &controls[i], &cpu_items[i], error, capacity);
            if (status != PORTABLE_MESH_OK)
                return status;
            continue;
        }
        /* Every controller observes the same accepted preturn world. */
        const PortableMeshRacer racer = {session->course,         racer_rules(session, i),
                                         session->fall_limit_q16, session->spawn_grids[i],
                                         session->mode,           session->state.racers[i],
                                         session->geometry};
        const PortableMeshStatus status = portable_mesh_cpu_input(
            &next.cpu[i], &session->cpu[i], &racer, &controls[i], error, capacity);
        if (status != PORTABLE_MESH_OK)
            return status;
    }
    ++next.state.tick;
    memset(&next.actor_events, 0, sizeof next.actor_events);
    if (next.actor_config)
    {
        int active = 0;
        for (uint32_t i = 0; i < count; ++i)
            active |= session->state.racers[i].phase <= PORTABLE_MESH_RACER_RACING;
        PortableMeshSkyProgressInput poses[PORTABLE_MESH_RACE_MAX];
        for(uint32_t i=0;i<count;++i)
            poses[i]=(PortableMeshSkyProgressInput){session->state.racers[i].checkpoint_count,
                session->state.racers[i].lap,session->state.racers[i].crossing_armed};
        if (active && !portable_mesh_sky_advance_racers(&next.actor_config->sky, &next.actors.sky,
            &session->actors.sky,poses,count,session->state.standing_order[0]))
            return race_error(session, PORTABLE_MESH_INVALID, 0, "sky clock turn", error, capacity);
    }
    PortableMeshKartEffects effects[PORTABLE_MESH_RACE_MAX];
    uint8_t crush_held = 0, strong_held = 0;
    int item_turn = 0;
    if (next.items_enabled)
        for (uint32_t i = 0; i < count; ++i)
            item_turn |= next.state.racers[i].phase == PORTABLE_MESH_RACER_RACING;
    const PortableMeshItemsConfig config = item_config(&next);
    if (item_turn)
    {
        PortableMeshItemControls held[PORTABLE_MESH_RACE_MAX] = {0};
        if (item_controls)
            memcpy(held, item_controls, next.human_count * sizeof *held);
        if (next.mode == 16u)
            for (unsigned i = next.human_count; i < count; ++i)
                held[i] = cpu_items[i];
        for (uint32_t i = 0; i < count; ++i)
            held[i].recovering =
                (uint8_t)(next.bomb_recovery_ticks[i] ||
                          ((controls[i].buttons & PORTABLE_MESH_RESCUE) &&
                           !(next.state.racers[i].kart.previous_buttons & PORTABLE_MESH_RESCUE)));
        uint8_t capture_recovering = 0;
        for (uint32_t i = 0; i < count; ++i)
            if (held[i].recovering) capture_recovering |= (uint8_t)(1u << i);
        if (next.actor_config)
        {
            if (!portable_mesh_course_actors_advance_events_on_course(next.actor_config, next.geometry, &next.actors,
                    next.state.racers, count, &next.actor_events, error, capacity))
                return PORTABLE_MESH_INVALID;
            next.actors.boo_pending_mask &= (uint8_t)~capture_recovering;
            next.actors.boo_flash_mask &= (uint8_t)~capture_recovering;
            for (uint32_t i = 0; i < count; ++i)
            {
                if (capture_recovering & (1u << i)) next.actors.lava_burn_ticks[i] = 0;
                if (next.actor_config->thwomp_count)
                {
                    int hold = 0, cue = 0;
                    if (!portable_mesh_course_crush_tick(&next.actor_config->crush_rules,
                            &next.actors.crush[i], (capture_recovering & (1u << i)) ||
                            next.state.racers[i].kart.respawn_ticks || next.state.racers[i].respawn_grace,
                            &hold, &cue, error, capacity)) return PORTABLE_MESH_INVALID;
                    if (hold) crush_held |= (uint8_t)(1u << i);
                    if (cue) next.actor_events.thwomp_recover_mask |= (uint8_t)(1u << i);
                    held[i].recovering |= (uint8_t)hold;
                }
                if (next.actor_config->cannonball_count || next.actor_config->falling_star_count ||
                    next.actor_config->thundercloud_count || next.actor_config->mechakoopa_count)
                {
                    int hold = 0;
                    if (!portable_mesh_course_strong_hit_tick(&next.actors.strong_hit[i],
                            next.state.racers[i].kart.grounded,
                            (capture_recovering & (1u << i)) || next.state.racers[i].kart.respawn_ticks ||
                            next.state.racers[i].respawn_grace, &hold))
                        return race_error(session, PORTABLE_MESH_INVALID, i, "source strong-hit turn", error, capacity);
                    if (hold) strong_held |= (uint8_t)(1u << i);
                    held[i].recovering |= (uint8_t)hold;
                }
                held[i].recovering |= next.actors.capture[i].remaining > 0;
            }
        }
        if (next.actor_config && !portable_mesh_course_actors_prepare_drive(next.actor_config,
                &next.actors, &next.items, controls, next.state.racers, count,
                &next.actor_events, error, capacity))
            return PORTABLE_MESH_INVALID;
        const PortableMeshItemsResult result = portable_mesh_items_prepare_tick(
            &next.items, &config, next.state.racers, held, next.state.standing_order, effects,
            &next.item_events, error, capacity);
        if (result != PORTABLE_MESH_ITEMS_OK)
            return item_status(result);
        /* Fresh protection is admitted before new captures. Existing captures
         * suppress item use while all independent status clocks still advance. */
        if (next.actor_config)
        {
            if (!portable_mesh_course_capture_admit(next.actor_config, &next.actors, next.course,
                    next.rules, next.item_rules, &next.items.status, next.state.racers, count,
                    capture_recovering, &next.actor_events, error, capacity))
                return PORTABLE_MESH_INVALID;
            for (uint32_t i = 0; i < count; ++i)
                next.items.controls[i].recovering |= next.actors.capture[i].remaining > 0;
        }
    }
    if (item_turn && next.actor_config)
        for (uint32_t i = 0; i < count; ++i)
            if (!portable_mesh_course_coins_effect(&next.actor_config->coins,
                    &next.actors.coins, next.mode, i, &effects[i]))
                return race_error(session, PORTABLE_MESH_INVALID, i, "coin speed cap", error, capacity);
    uint8_t ordinary_contacts = 0;
    /* No externally visible racer changes until every phase succeeds. */
    for (uint32_t i = 0; i < count; ++i)
    {
        racers[i] = (PortableMeshRacer){next.course, racer_rules(&next, i), next.fall_limit_q16,
                                        next.spawn_grids[i], next.mode,  next.state.racers[i],
                                        next.geometry};
        if (next.mode == 16u && next.bomb_recovery_ticks[i])
        {
            --next.bomb_recovery_ticks[i];
            if (next.bomb_recovery_ticks[i])
            {
                memset(&turns[i], 0, sizeof turns[i]);
                turns[i].finish_fraction_q30 = UINT32_MAX;
                ++racers[i].state.tick;
                ++racers[i].state.race_ticks;
                racers[i].state.kart.previous_buttons = controls[i].buttons;
                continue;
            }
            const PortableMeshStatus recovery =
                portable_mesh_racer_respawn(&racers[i], error, capacity);
            if (recovery != PORTABLE_MESH_OK)
                return recovery;
            next.recovery_pending[i] = 1;
        }
        if (item_turn && next.actor_config)
        {
            int handled = 0;
            if (!portable_mesh_course_capture_move(next.actor_config, &next.actors, i,
                    &racers[i], controls[i], &turns[i], &handled, &next.actor_events,
                    error, capacity))
                return PORTABLE_MESH_INVALID;
            if (handled) continue;
            if (next.actor_events.launch_mask & (1u << i))
                effects[i].inhibit_controls = effects[i].cancel_drift = 1;
        }
        if (crush_held & (1u << i))
            portable_mesh_course_crush_hold(&racers[i].state.kart, &effects[i]);
        if (strong_held & (1u << i))
        {
            const PortableMeshStatus held =
                portable_mesh_course_strong_hit_hold(&racers[i].state.kart, &effects[i]);
            if (held != PORTABLE_MESH_OK)
                return race_error(session, held, i, "strong-hit hold projection", error, capacity);
        }
        PortableMeshStatus status = portable_mesh_racer_move_effects(
            &racers[i], controls[i], item_turn ? &effects[i] : NULL, &turns[i], error, capacity);
        if (status != PORTABLE_MESH_OK)
            return status;
    }
    /* World APIs require contiguous racer states, rather than the state
     * members interleaved inside each temporary PortableMeshRacer owner. */
    if (item_turn)
        for (uint32_t i = 0; i < count; ++i)
            next.state.racers[i] = racers[i].state;
    for (uint32_t i = 0; i < count; ++i)
        for (uint32_t j = i + 1u; j < count; ++j)
            if (turns[i].contact && turns[j].contact)
            {
                if (item_turn)
                {
                    uint8_t pair_contacts = 0;
                    const PortableMeshItemsResult result =
                        portable_mesh_items_pair_observed(&next.items, &config, next.state.racers,
                            i, j, &next.item_events, &pair_contacts, error, capacity);
                    ordinary_contacts |= pair_contacts;
                    if (result != PORTABLE_MESH_ITEMS_OK)
                        return item_status(result);
                }
                else
                {
                    const PortableMeshStatus status = portable_mesh_karts_contact(
                        &racers[i].state.kart, racers[i].rules, &racers[j].state.kart, racers[j].rules);
                    if (status != PORTABLE_MESH_OK)
                        return race_error(&next, status, i, "kart pair contact", error, capacity);
                }
            }
    /* Spinning, tumbling and squashing hazards hold the kart like an item hit. */
    for (uint32_t i = 0; item_turn && i < count; ++i)
        if (turns[i].events.hazard && turns[i].events.hazard_reaction >= PORTABLE_MESH_HAZARD_SPIN)
        {
            PortableMeshItemDamage hit;
            if (portable_mesh_item_status_damage(&next.items.status, config.rules, i, 1, &hit) !=
                PORTABLE_MESH_ITEM_STATUS_OK)
                return race_error(&next, PORTABLE_MESH_INVALID, i, "hazard hit status", error, capacity);
        }
    if (item_turn)
    {
        const PortableMeshItemsResult result = portable_mesh_items_resolve_after_move(
            &next.items, &config, next.state.racers, &next.item_events, error, capacity);
        if (result != PORTABLE_MESH_ITEMS_OK)
            return item_status(result);
        for (uint32_t i = 0; i < count; ++i)
            next.actor_events.movement_body_scale_q16[i] = effects[i].body_scale_q16;
        if (next.actor_config && next.actor_config->pier_boo_count)
            for (unsigned event = 0; event < next.item_events.count; ++event)
                if (next.item_events.values[event].kind == PORTABLE_MESH_EVENT_HAUNT &&
                    next.item_events.values[event].target < count)
                    next.actor_events.observed_item_haunt_mask |=
                        (uint8_t)(1u << next.item_events.values[event].target);
        if (next.actor_config && !portable_mesh_course_actors_resolve(
                                     next.actor_config, &next.actors, &next.items, &config,
                                     next.state.racers, count, &next.actor_events, error, capacity))
            return PORTABLE_MESH_INVALID;
        if (next.actor_config && next.actor_config->coins.enabled)
        {
            int32_t body_scale[8] = {0};
            for (uint32_t i = 0; i < count; ++i) body_scale[i] = effects[i].body_scale_q16;
            if (!portable_mesh_course_coin_turn(&next.actor_config->coins, &next.actors.coins,
                    next.mode, next.rules, body_scale, session->state.racers, next.state.racers,
                    count, &session->items, &next.items, &next.actor_events, &next.item_events,
                    ordinary_contacts, &next.actor_events.coins, error, capacity))
                return PORTABLE_MESH_INVALID;
        }
        for (uint32_t i = 0; i < count; ++i)
            racers[i].state = next.state.racers[i];
    }
    /* MK8 slipstream: racers 5 s past GO drop one box of their strip a tick,
     * cycling through five; the next move consumes who sits in one. */
    for (uint32_t i = 0; i < count; ++i)
    {
        uint8_t in = 0;
        for (uint32_t j = 0; j < count && !in; ++j)
            in = (uint8_t)(j != i && racers[j].state.phase == PORTABLE_MESH_RACER_RACING &&
                           racers[j].state.race_ticks > 300u &&
                           portable_mesh_kart_drafting(&racers[i].state.kart, racers[i].rules,
                                                       &racers[j].state.kart, racers[j].rules,
                                                       next.state.tick % 5u));
        racers[i].state.kart.slip_in = in;
    }
    uint8_t finished[PORTABLE_MESH_RACE_MAX];
    unsigned newly_finished = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        PortableMeshStatus status =
            portable_mesh_racer_publish(&racers[i], &turns[i], error, capacity);
        if (status != PORTABLE_MESH_OK)
            return status;
        next.state.racers[i] = racers[i].state;
        if (!next.state.finish_rank[i] && racers[i].state.phase == PORTABLE_MESH_RACER_FINISHED)
        {
            unsigned at = newly_finished;
            while (at &&
                   turns[finished[at - 1u]].finish_fraction_q30 > turns[i].finish_fraction_q30)
            {
                finished[at] = finished[at - 1u];
                --at;
            }
            finished[at] = (uint8_t)i;
            ++newly_finished;
        }
    }
    for (unsigned i = 0; i < newly_finished; ++i)
    {
        const uint8_t racer = finished[i];
        next.state.finish_order[next.state.finish_count++] = racer;
        next.state.finish_rank[racer] = (uint8_t)next.state.finish_count;
    }
    /* Like original VS, rank the last racer where it stands; do not invent a lap/time. */
    if (next.mode == 8u && next.state.finish_count + 1u >= count)
    {
        for (uint32_t i = 0; i < count; ++i)
            if (!next.state.finish_rank[i])
            {
                next.state.retired[i] = 1;
                next.state.finish_order[next.state.finish_count++] = (uint8_t)i;
                next.state.finish_rank[i] = (uint8_t)next.state.finish_count;
            }
        next.state.results_ready = 1;
    }
    if (next.mode == 16u)
    {
        uint32_t damage = next.recovery_hit_mask | next.actor_events.hit_mask;
        next.recovery_hit_mask = 0;
        for (unsigned i = 0; i < count; ++i)
        {
            if (next.state.racers[i].kart.respawn_ticks &&
                !session->state.racers[i].kart.respawn_ticks)
                next.recovery_pending[i] = 1;
            if (next.recovery_pending[i] && !next.state.racers[i].kart.respawn_ticks &&
                next.state.racers[i].kart.grounded)
            {
                /* Source retrieval touchdown queues +74 bit0 for the next
                 * C444 pass. Mesh manual rescue uses the same return policy. */
                next.recovery_pending[i] = 0;
                next.recovery_hit_mask |= (uint8_t)(1u << i);
            }
        }
        memset(&next.battle_events, 0, sizeof next.battle_events);
        if (item_turn)
            for (uint32_t i = 0; i < next.item_events.count; ++i)
            {
                const PortableMeshItemEvent *event = &next.item_events.values[i];
                if (event->kind == PORTABLE_MESH_EVENT_HIT && event->target < count &&
                    next.battle.balloons[event->target])
                    damage |= 1u << event->target;
                if (event->kind == PORTABLE_MESH_EVENT_BOMB_CONTACT && event->target < count &&
                    next.battle.elimination[event->target].revenge_bomb &&
                    !next.bomb_recovery_ticks[event->target])
                {
                    /* E7F0 keeps the role, stops motion, then waits60 ticks
                     * before authored retrieval. Never fake FINISHED. */
                    next.bomb_recovery_ticks[event->target] = 60;
                    PortableMeshKartState *kart = &next.state.racers[event->target].kart;
                    memset(kart->velocity_q16, 0, sizeof kart->velocity_q16);
                    memset(kart->boost_ticks, 0, sizeof kart->boost_ticks);
                    kart->speed_q16 = kart->steer_filtered_q16 = kart->outside_drift_q16 = 0;
                    kart->drift_charge = kart->panel_ticks = 0;
                    kart->drift_direction = 0;
                    kart->drift_tier = 0;
                }
            }
        if (next.state.racers[0].phase == PORTABLE_MESH_RACER_RACING &&
            session->state.racers[0].phase == PORTABLE_MESH_RACER_RACING &&
            !portable_mesh_battle_step(&next.battle, next.state.tick, damage, &next.battle_events,
                                       error, capacity))
            return PORTABLE_MESH_INVALID;
        if (next.items_enabled && next.battle_events.eliminated_mask)
        {
            const PortableMeshItemsConfig roles = item_config(&next);
            const PortableMeshItemsResult cleanup = portable_mesh_items_clear_inactive(
                &next.items, &roles, &next.item_events, error, capacity);
            if (cleanup != PORTABLE_MESH_ITEMS_OK)
                return item_status(cleanup);
        }
        memcpy(next.state.standing_order, next.battle.standings, count);
        memcpy(next.state.standing_rank, next.battle.ranks, count);
        if (next.battle.finished)
        {
            memcpy(next.state.finish_order, next.battle.standings, count);
            memcpy(next.state.finish_rank, next.battle.ranks, count);
            next.state.finish_count = count;
            next.state.results_ready = 1;
        }
    }
    else
        standings(&next);
    if (next.mode != 8u && next.mode != 16u &&
        next.state.racers[0].phase == PORTABLE_MESH_RACER_FINISHED)
    {
        /* GP/Quick stop on the human finish. Rivals keep their real states;
         * the result view explicitly estimates unfinished rivals' times. */
        memcpy(next.state.finish_order, next.state.standing_order, count);
        memcpy(next.state.finish_rank, next.state.standing_rank, count);
        next.state.results_ready = 1;
    }
    *session = next;
    for (uint32_t i = 0; i < count; ++i)
        events[i] = turns[i].events;
    if (error && capacity)
        error[0] = 0;
    return PORTABLE_MESH_OK;
}

PortableMeshStatus portable_mesh_race_tick(PortableMeshRaceSession *session,
                                           const PortableMeshKartInput *inputs,
                                           PortableMeshKartEvents *events, char *error,
                                           size_t capacity)
{
    return portable_mesh_race_tick_items(session, inputs, NULL, events, error, capacity);
}

static uint64_t hash_word(uint64_t hash, uint32_t word)
{
    for (unsigned byte = 0; byte < 4; ++byte)
        hash = (hash ^ ((word >> (byte * 8u)) & 255u)) * UINT64_C(1099511628211);
    return hash;
}
uint64_t portable_mesh_race_battle_checksum(const PortableMeshRaceSession *s)
{
    if (!s || !s->course || !s->rules || s->mode != 16u || s->state.player_count > 8u)
        return 0;
    uint64_t hash = UINT64_C(14695981039346656037);
    const uint32_t header[] = {UINT32_C(0x3153424d),
                               s->course->id,
                               s->rules->profile_id,
                               s->rules->revision,
                               s->mode,
                               s->human_count,
                               s->state.player_count,
                               s->state.tick,
                               s->state.results_ready,
                               s->state.finish_count,
                               s->items_enabled,
                               s->recovery_hit_mask,
                               s->battle.config.racer_count,
                               s->battle.config.starting_balloons,
                               s->battle.config.elimination_ticks,
                               s->battle.accepted_tick,
                               s->battle.tick_valid,
                               s->battle.finished};
    for (unsigned i = 0; i < sizeof header / sizeof header[0]; ++i)
        hash = hash_word(hash, header[i]);
    const uint64_t items = s->items_enabled ? portable_mesh_items_checksum(&s->items) : 0;
    hash = hash_word(hash, (uint32_t)items);
    hash = hash_word(hash, (uint32_t)(items >> 32u));
    for (unsigned i = 0; i < s->state.player_count; ++i)
    {
        const uint64_t kart = portable_mesh_time_trial_checksum(&s->state.racers[i]);
        hash = hash_word(hash, (uint32_t)kart);
        hash = hash_word(hash, (uint32_t)(kart >> 32u));
        const uint32_t words[] = {s->spawn_grids[i],
                                  s->battle.balloons[i],
                                  s->battle.hit_flags[i],
                                  s->battle.elimination[i].remaining_ticks,
                                  s->battle.elimination[i].revenge_bomb,
                                  s->bomb_recovery_ticks[i],
                                  s->recovery_pending[i],
                                  s->battle.standings[i],
                                  s->battle.ranks[i],
                                  s->state.standing_order[i],
                                  s->state.standing_rank[i],
                                  s->state.finish_order[i],
                                  s->state.finish_rank[i],
                                  s->state.retired[i]};
        for (unsigned word = 0; word < sizeof words / sizeof words[0]; ++word)
            hash = hash_word(hash, words[word]);
    }
    return hash;
}
