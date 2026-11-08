#include "mesh_course_coin_turn.h"
#include "mesh_time_trial.h"
#include "fixed_3d.h"
#include <string.h>
#include <stdio.h>
#define Q PORTABLE_FIXED_3D_ONE
#define U PORTABLE_FIXED_3D_UNIT
static uint32_t read_word(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) |
           ((uint32_t)p[3] << 24u);
}
static int32_t signed_word(uint32_t n)
{
    return n <= INT32_MAX ? (int32_t)n : -1 - (int32_t)(UINT32_MAX - n);
}
static void write_word(uint8_t *p, uint32_t n)
{
    for (unsigned i = 0; i < 4u; ++i)
        p[i] = (uint8_t)(n >> (i * 8u));
}
static void state_words(const PortableMeshTimeTrialState *s,
                        uint32_t out[PORTABLE_MESH_TIME_TRIAL_STATE_WORDS])
{
    size_t n = 0;
#define ADD(value) out[n++] = (uint32_t)(value)
    for (unsigned i = 0; i < 3u; ++i)
    {
        ADD(s->kart.position_q16[i]);
    }
    for (unsigned i = 0; i < 3u; ++i)
    {
        ADD(s->kart.velocity_q16[i]);
    }
    for (unsigned i = 0; i < 3u; ++i)
    {
        ADD(s->kart.forward_q30[i]);
    }
    for (unsigned i = 0; i < 3u; ++i)
    {
        ADD(s->kart.up_q30[i]);
    }
    ADD(s->kart.steer_filtered_q16);
    ADD(s->kart.speed_q16);
    ADD(s->kart.outside_drift_q16);
    ADD(s->kart.drift_charge);
    for (unsigned i = 0; i < 3u; ++i)
    {
        ADD(s->kart.boost_ticks[i]);
    }
    ADD(s->kart.panel_ticks);
    ADD(s->kart.air_ticks);
    ADD(s->kart.respawn_ticks);
    ADD(s->kart.contact_triangle);
    ADD(s->kart.contact_surface);
    ADD(s->kart.respawn_index);
    ADD(s->kart.previous_buttons);
    ADD(s->kart.drift_direction);
    ADD(s->kart.drift_tier);
    ADD(s->kart.grounded);
    ADD(s->kart.last_boost_tier);
    ADD(s->tick);
    ADD(s->race_ticks);
    ADD(s->phase);
    ADD(s->countdown_ticks);
    ADD(s->lap);
    ADD(s->checkpoint);
    ADD(s->checkpoint_count);
    ADD(s->last_respawn);
    ADD(s->respawn_grace);
    ADD(s->finish_ticks);
    for (unsigned i = 0; i < 3u; ++i)
    {
        ADD(s->previous_position_q16[i]);
    }
    ADD(s->crossing_armed);
#undef ADD
}
int portable_mesh_time_trial_state_write(const PortableMeshTimeTrialState *s, uint8_t *bytes,
                                         size_t capacity)
{
    if (!s || !bytes || capacity < PORTABLE_MESH_TIME_TRIAL_STATE_WORDS * 4u)
        return 0;
    uint32_t words[PORTABLE_MESH_TIME_TRIAL_STATE_WORDS];
    state_words(s, words);
    for (size_t i = 0; i < PORTABLE_MESH_TIME_TRIAL_STATE_WORDS; ++i)
        write_word(bytes + i * 4u, words[i]);
    return 1;
}
int portable_mesh_time_trial_state_read(PortableMeshTimeTrialState *state, const uint8_t *bytes,
                                        size_t size)
{
    if (!state || !bytes || size != PORTABLE_MESH_TIME_TRIAL_STATE_WORDS * 4u)
        return 0;
    uint32_t w[PORTABLE_MESH_TIME_TRIAL_STATE_WORDS];
    for (size_t i = 0; i < PORTABLE_MESH_TIME_TRIAL_STATE_WORDS; ++i)
        w[i] = read_word(bytes + i * 4u);
    PortableMeshTimeTrialState s;
    memset(&s, 0, sizeof(s));
    size_t n = 0;
#define SIGNED(value) value = signed_word(w[n++])
#define UNSIGNED(value) value = w[n++]
    for (unsigned i = 0; i < 3u; ++i)
    {
        SIGNED(s.kart.position_q16[i]);
    }
    for (unsigned i = 0; i < 3u; ++i)
    {
        SIGNED(s.kart.velocity_q16[i]);
    }
    for (unsigned i = 0; i < 3u; ++i)
    {
        SIGNED(s.kart.forward_q30[i]);
    }
    for (unsigned i = 0; i < 3u; ++i)
    {
        SIGNED(s.kart.up_q30[i]);
    }
    SIGNED(s.kart.steer_filtered_q16);
    SIGNED(s.kart.speed_q16);
    SIGNED(s.kart.outside_drift_q16);
    UNSIGNED(s.kart.drift_charge);
    for (unsigned i = 0; i < 3u; ++i)
    {
        UNSIGNED(s.kart.boost_ticks[i]);
    }
    UNSIGNED(s.kart.panel_ticks);
    UNSIGNED(s.kart.air_ticks);
    UNSIGNED(s.kart.respawn_ticks);
    UNSIGNED(s.kart.contact_triangle);
    UNSIGNED(s.kart.contact_surface);
    UNSIGNED(s.kart.respawn_index);
    if (w[n] > 15u)
        return 0;
    s.kart.previous_buttons = (uint16_t)w[n++];
    const int32_t direction = signed_word(w[n++]);
    if (direction < -1 || direction > 1)
        return 0;
    s.kart.drift_direction = (int8_t)direction;
    if (w[n] > 3u || w[n + 1u] > 1u || w[n + 2u] > 3u)
        return 0;
    s.kart.drift_tier = (uint8_t)w[n++];
    s.kart.grounded = (uint8_t)w[n++];
    s.kart.last_boost_tier = (uint8_t)w[n++];
    UNSIGNED(s.tick);
    UNSIGNED(s.race_ticks);
    UNSIGNED(s.phase);
    UNSIGNED(s.countdown_ticks);
    UNSIGNED(s.lap);
    UNSIGNED(s.checkpoint);
    UNSIGNED(s.checkpoint_count);
    UNSIGNED(s.last_respawn);
    UNSIGNED(s.respawn_grace);
    UNSIGNED(s.finish_ticks);
    for (unsigned i = 0; i < 3u; ++i)
    {
        SIGNED(s.previous_position_q16[i]);
    }
    UNSIGNED(s.crossing_armed);
#undef SIGNED
#undef UNSIGNED
    if (s.phase > PORTABLE_MESH_TT_FINISHED || s.crossing_armed > 1u ||
        s.kart.drift_charge > 8192u || s.kart.panel_ticks > 8192u || s.kart.respawn_ticks > 8192u)
        return 0;
    for (unsigned i = 0; i < 3u; ++i)
    {
        if (s.kart.position_q16[i] < -PORTABLE_FIXED_3D_POSITION_LIMIT ||
            s.kart.position_q16[i] > PORTABLE_FIXED_3D_POSITION_LIMIT ||
            s.previous_position_q16[i] < -PORTABLE_FIXED_3D_POSITION_LIMIT ||
            s.previous_position_q16[i] > PORTABLE_FIXED_3D_POSITION_LIMIT ||
            s.kart.velocity_q16[i] < -16777216 || s.kart.velocity_q16[i] > 16777216 ||
            s.kart.forward_q30[i] < -U || s.kart.forward_q30[i] > U || s.kart.up_q30[i] < -U ||
            s.kart.up_q30[i] > U || s.kart.boost_ticks[i] > 8192u)
            return 0;
    }
    if (!portable_mesh_kart_state_valid(&s.kart))
        return 0;
    *state = s;
    return 1;
}
uint64_t portable_mesh_time_trial_checksum(const PortableMeshTimeTrialState *s)
{
    if (!s)
        return 0;
    uint8_t bytes[PORTABLE_MESH_TIME_TRIAL_STATE_WORDS * 4u];
    (void)portable_mesh_time_trial_state_write(s, bytes, sizeof(bytes));
    uint64_t h = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < sizeof(bytes); ++i)
    {
        h ^= bytes[i];
        h *= UINT64_C(1099511628211);
    }
    return h;
}
PortableMeshStatus portable_mesh_time_trial_init(PortableMeshTimeTrial *session,
                                                 const PortableCourse3DView *course,
                                                 const PortableMeshKartRules *rules, uint32_t grid,
                                                 char *error, size_t capacity)
{
    return portable_mesh_racer_init(session, course, rules, grid, 2u, error, capacity);
}

static int declares_actors(const PortableMeshTimeTrial *session)
{
    PortableCourse3DMeta meta;
    PortableCourse3DProperty property;
    if (!session || !session->course || !portable_course_3d_meta(session->course, &meta))
        return 0;
    return portable_course_3d_property_find(session->course, meta.property_first,
                                            meta.property_count, "mksc:actor-rules", &property) ||
           portable_course_3d_property_find(session->course, meta.property_first,
                                            meta.property_count, "mksc:coin-rules", &property) ||
           portable_course_3d_property_find(session->course, meta.property_first,
                                            meta.property_count, "mksc:sky-motion", &property);
}
static PortableMeshStatus missing_actor_owner(const PortableMeshTimeTrial *session, char *error,
                                              size_t capacity)
{
    if (error && capacity)
        snprintf(error, capacity, "3D TT course=%u: declared source actor owner not enabled",
                 session && session->course ? session->course->id : 0u);
    return PORTABLE_MESH_UNSUPPORTED;
}
static PortableMeshStatus tick_without_worlds(PortableMeshTimeTrial *session,
                                              PortableMeshKartInput input,
                                              PortableMeshKartEvents *events, char *error,
                                              size_t capacity)
{
    PortableMeshRacerTurn turn = {0};
    if (session == NULL || events == NULL)
        return portable_mesh_racer_move(session, input, NULL, error, capacity);
    PortableMeshTimeTrial next = *session;
    PortableMeshStatus status = portable_mesh_racer_move(&next, input, &turn, error, capacity);
    if (status == PORTABLE_MESH_OK)
        status = portable_mesh_racer_publish(&next, &turn, error, capacity);
    if (status != PORTABLE_MESH_OK)
        return status;
    *session = next;
    *events = turn.events;
    return PORTABLE_MESH_OK;
}

PortableMeshStatus portable_mesh_time_trial_tick(PortableMeshTimeTrial *session,
                                                 PortableMeshKartInput input,
                                                 PortableMeshKartEvents *events, char *error,
                                                 size_t capacity)
{
    if (declares_actors(session))
        return missing_actor_owner(session, error, capacity);
    return tick_without_worlds(session, input, events, error, capacity);
}

static PortableMeshStatus item_trial_error(const PortableMeshTimeTrial *session, char *error,
                                           size_t capacity)
{
    if (error && capacity)
        snprintf(error, capacity,
                 "3D TT items course=%u mode=%u tick=%u: invalid owner/config/input",
                 session && session->course ? session->course->id : 0u,
                 session ? session->mode : 0u, session ? session->state.tick : 0u);
    return PORTABLE_MESH_INVALID;
}

static PortableMeshStatus item_trial_status(PortableMeshItemsResult status)
{
    return status == PORTABLE_MESH_ITEMS_BUDGET ? PORTABLE_MESH_COLLISION_BUDGET
           : status == PORTABLE_MESH_ITEMS_OK   ? PORTABLE_MESH_OK
                                                : PORTABLE_MESH_INVALID;
}

static int advance_sky(const PortableMeshTimeTrial *session,const PortableMeshSkyConfig *config,
    PortableMeshSky *out,const PortableMeshSky *before)
{
    const PortableMeshSkyProgressInput pose={session->state.checkpoint_count,
        session->state.lap,session->state.crossing_armed};
    return portable_mesh_sky_advance_racers(config,out,before,&pose,1,0);
}

static PortableMeshStatus
tick_with_worlds(PortableMeshTimeTrial *session, PortableMeshItems *world,
                 const PortableMeshItemsConfig *config, PortableMeshCourseActors *actors,
                 const PortableMeshCourseActorsConfig *actor_config, PortableMeshKartInput input,
                 PortableMeshItemControls item_controls, PortableMeshKartEvents *events,
                 PortableMeshItemEvents *item_events, PortableMeshCourseActorEvents *actor_events,
                 char *error, size_t capacity)
{
    uint16_t hud;
    if (!actors && declares_actors(session))
        return missing_actor_owner(session, error, capacity);
    if ((actors || actor_config || actor_events) &&
        (!actors || !actor_config || !actor_events || !session || !session->course ||
         actor_config->course_id != session->course->id || actors->count != actor_config->count ||
         actors->snowman_count != actor_config->snowman_count ||
         actors->penguin_count != actor_config->penguin_count ||
         actors->mouse_count != actor_config->mouse_count ||
         actors->piranha_count != actor_config->piranha_count ||
         actors->seagull_count != actor_config->seagull_count ||
         actors->lava_count != actor_config->lava_count ||
         actors->thwomp_count != actor_config->thwomp_count ||
         actors->cannonball_count != actor_config->cannonball_count ||
         actors->boo_count != actor_config->boo_count ||
         actors->rock_count != actor_config->rock_count ||
         actors->teepee_count != actor_config->teepee_count ||
         actors->falling_star_count != actor_config->falling_star_count ||
         actors->thundercloud_count != actor_config->thundercloud_count ||
         actors->common_prop_count != actor_config->common_prop_count ||
         actors->mechakoopa_count != actor_config->mechakoopa_count ||
         actors->kamek_count != actor_config->kamek_count ||
         actors->pier_boo_count != actor_config->pier_boo_count ||
         !portable_mesh_sky_valid(&actor_config->sky, &actors->sky) ||
         !portable_mesh_course_coins_valid(&actor_config->coins, &actors->coins)))
        return item_trial_error(session, error, capacity);
    PortableMeshCourseActorEvents actor_emitted = {0};
    if (!world && !config)
    {
        PortableCourse3DMeta meta;
        PortableCourse3DProperty declaration;
        if (session && session->course && portable_course_3d_meta(session->course,&meta) &&
            portable_course_3d_property_find(session->course,meta.property_first,meta.property_count,
                                            "mksc:item-rules",&declaration))
        {
            if (error && capacity)snprintf(error,capacity,
                "3D TT course=%u: declared MIR item owner not enabled",session->course->id);
            return PORTABLE_MESH_UNSUPPORTED;
        }
        if (!actors || !actor_config || !actor_config->sky.enabled ||
            portable_mesh_course_actors_needs_items(actor_config) || !events || !item_events ||
            item_controls.item_pressed || item_controls.aim_backward || item_controls.aim_forward ||
            item_controls.recovering)
            return item_trial_error(session, error, capacity);
        PortableMeshTimeTrial next = *session;
        PortableMeshCourseActors staged = *actors;
        PortableMeshKartEvents emitted;
        if (session->state.phase <= PORTABLE_MESH_TT_RACING &&
            !advance_sky(session, &actor_config->sky, &staged.sky, &actors->sky))
            return item_trial_error(session, error, capacity);
        const PortableMeshStatus status =
            tick_without_worlds(&next, input, &emitted, error, capacity);
        if (status != PORTABLE_MESH_OK)
            return status;
        *session = next;
        *actors = staged;
        *events = emitted;
        *actor_events = actor_emitted;
        memset(item_events, 0, sizeof *item_events);
        return PORTABLE_MESH_OK;
    }
    if (!session || !world || !config || !events || !item_events || !session->course ||
        config->course != session->course || config->kart_rules != session->rules ||
        config->geometry != session->geometry || session->mode != 2u || config->mode != 2u ||
        config->count != 1u || config->human_mask != 1u || world->inventory.count != 1u ||
        world->inventory.human_mask != 1u || world->status.count != 1u ||
        world->status.human_mask != 1u || world->prepared ||
        !portable_mesh_kart_rules_valid(session->rules) ||
        !portable_mesh_item_rules_valid(config->rules) ||
        !portable_mesh_item_rules_fit_profile(config->rules, session->rules) ||
        session->state.phase > PORTABLE_MESH_TT_FINISHED || session->state.tick == UINT32_MAX ||
        input.buttons > 15u || item_controls.item_pressed > 1u || item_controls.aim_backward > 1u ||
        item_controls.aim_forward > 1u ||
        portable_mesh_items_hud(world, 0, &hud) != PORTABLE_MESH_ITEMS_OK)
        return item_trial_error(session, error, capacity);
    PortableMeshTimeTrial next = *session;
    PortableMeshKartEvents emitted;
    PortableMeshItemEvents item_emitted = {0};
    PortableMeshCourseActors staged_actors;
    if (actors)
    {
        staged_actors = *actors;
        if (session->state.phase <= PORTABLE_MESH_TT_RACING &&
            !advance_sky(session, &actor_config->sky, &staged_actors.sky, &actors->sky))
            return item_trial_error(session, error, capacity);
    }
    if (session->state.phase != PORTABLE_MESH_TT_RACING)
    {
        const PortableMeshStatus status =
            tick_without_worlds(&next, input, &emitted, error, capacity);
        if (status != PORTABLE_MESH_OK)
            return status;
        *session = next;
        *events = emitted;
        *item_events = item_emitted;
        if (actor_events)
            *actor_events = actor_emitted;
        if (actors)
            *actors = staged_actors;
        return PORTABLE_MESH_OK;
    }
    if (actors)
    {
        if (!portable_mesh_course_actors_advance_events_on_course(actor_config, session->geometry, &staged_actors,
                &next.state, 1u, &actor_emitted, error, capacity))
            return PORTABLE_MESH_INVALID;
    }
    PortableMeshItems staged_world = *world;
    PortableMeshKartEffects effects;
    const uint8_t standings[1] = {0};
    item_controls.recovering =
        (uint8_t)((input.buttons & PORTABLE_MESH_RESCUE) &&
                  !(session->state.kart.previous_buttons & PORTABLE_MESH_RESCUE));
    const uint8_t capture_recovering = item_controls.recovering;
    int crush_held = 0, crush_cue = 0;
    if (actors && actor_config->thwomp_count)
    {
        if (!portable_mesh_course_crush_tick(&actor_config->crush_rules, &staged_actors.crush[0],
                capture_recovering || next.state.kart.respawn_ticks || next.state.respawn_grace,
                &crush_held, &crush_cue, error, capacity)) return PORTABLE_MESH_INVALID;
        if (crush_cue) actor_emitted.thwomp_recover_mask |= 1;
        item_controls.recovering |= (uint8_t)crush_held;
    }
    int strong_held = 0;
    if (actors && (actor_config->cannonball_count || actor_config->falling_star_count ||
                   actor_config->thundercloud_count || actor_config->mechakoopa_count))
    {
        if (!portable_mesh_course_strong_hit_tick(&staged_actors.strong_hit[0],
                next.state.kart.grounded, capture_recovering || next.state.kart.respawn_ticks ||
                next.state.respawn_grace, &strong_held)) return item_trial_error(session, error, capacity);
        item_controls.recovering |= (uint8_t)strong_held;
    }
    if (actors && capture_recovering)
    {
        staged_actors.lava_burn_ticks[0] = 0;
        staged_actors.boo_pending_mask &= UINT8_C(0xfe);
        staged_actors.boo_flash_mask &= UINT8_C(0xfe);
    }
    if (actors)
        item_controls.recovering |= staged_actors.capture[0].remaining > 0;
    if (actors && !portable_mesh_course_actors_prepare_drive(actor_config, &staged_actors,
            &staged_world, &input, &next.state, 1u, &actor_emitted, error, capacity))
        return PORTABLE_MESH_INVALID;
    PortableMeshItemsResult result =
        portable_mesh_items_prepare_tick(&staged_world, config, &next.state, &item_controls,
                                         standings, &effects, &item_emitted, error, capacity);
    if (result != PORTABLE_MESH_ITEMS_OK)
        return item_trial_status(result);
    if (actors)
    {
        if (!portable_mesh_course_capture_admit(actor_config, &staged_actors, next.course,
                next.rules, config->rules, &staged_world.status, &next.state, 1u, capture_recovering,
                &actor_emitted, error, capacity))
            return PORTABLE_MESH_INVALID;
        staged_world.controls[0].recovering |= staged_actors.capture[0].remaining > 0;
    }
    if (actors && !portable_mesh_course_coins_effect(&actor_config->coins,
            &staged_actors.coins, 2u, 0u, &effects))
        return item_trial_error(session, error, capacity);
    if (crush_held) portable_mesh_course_crush_hold(&next.state.kart, &effects);
    if (strong_held)
    {
        const PortableMeshStatus held =
            portable_mesh_course_strong_hit_hold(&next.state.kart, &effects);
        if (held != PORTABLE_MESH_OK)
        {
            if (error && capacity)
                snprintf(error, capacity,
                         "3D Time Trial course=%u tick=%u: strong-hit hold projection (%s)",
                         session->course->id, session->state.tick, portable_mesh_status_name(held));
            return held;
        }
    }
    PortableMeshRacerTurn turn = {0};
    int handled = 0;
    if (actors)
    {
        if (!portable_mesh_course_capture_move(actor_config, &staged_actors, 0, &next,
                input, &turn, &handled, &actor_emitted, error, capacity))
            return PORTABLE_MESH_INVALID;
        if (actor_emitted.launch_mask & 1u)
            effects.inhibit_controls = effects.cancel_drift = 1;
    }
    PortableMeshStatus status = PORTABLE_MESH_OK;
    if (!handled)
    {
        status = portable_mesh_racer_move_effects(&next, input, &effects, &turn, error, capacity);
        if (status != PORTABLE_MESH_OK)
            return status;
    }
    if (!handled && turn.events.hazard && turn.events.hazard_reaction >= PORTABLE_MESH_HAZARD_SPIN)
    {
        PortableMeshItemDamage hit;
        if (portable_mesh_item_status_damage(&staged_world.status, config->rules, 0, 1, &hit) !=
            PORTABLE_MESH_ITEM_STATUS_OK)
            return item_trial_error(session, error, capacity);
    }
    result = portable_mesh_items_resolve_after_move(&staged_world, config, &next.state,
                                                    &item_emitted, error, capacity);
    if (result != PORTABLE_MESH_ITEMS_OK)
        return item_trial_status(result);
    if (actors)
    {
        actor_emitted.movement_body_scale_q16[0] = effects.body_scale_q16;
        if (actor_config->pier_boo_count)
            for (unsigned event = 0; event < item_emitted.count; ++event)
                if (item_emitted.values[event].kind == PORTABLE_MESH_EVENT_HAUNT &&
                    item_emitted.values[event].target == 0)
                    actor_emitted.observed_item_haunt_mask |= 1u;
        if (!portable_mesh_course_actors_resolve(actor_config, &staged_actors, &staged_world,
                                                 config, &next.state, 1u, &actor_emitted, error,
                                                 capacity))
            return PORTABLE_MESH_INVALID;
    }
    if (actors && actor_config->coins.enabled)
    {
        const int32_t body_scale[8] = {effects.body_scale_q16};
        if (!portable_mesh_course_coin_turn(&actor_config->coins, &staged_actors.coins,
                2u, next.rules, body_scale, &session->state, &next.state, 1u,
                world, &staged_world, &actor_emitted, &item_emitted, 0u,
                &actor_emitted.coins, error, capacity))
            return PORTABLE_MESH_INVALID;
    }
    status = portable_mesh_racer_publish(&next, &turn, error, capacity);
    if (status != PORTABLE_MESH_OK)
        return status;
    *session = next;
    *world = staged_world;
    if (actors)
    {
        *actors = staged_actors;
        *actor_events = actor_emitted;
    }
    *events = turn.events;
    *item_events = item_emitted;
    if (error && capacity)
        error[0] = 0;
    return PORTABLE_MESH_OK;
}

PortableMeshStatus portable_mesh_time_trial_tick_items(
    PortableMeshTimeTrial *session, PortableMeshItems *world, const PortableMeshItemsConfig *config,
    PortableMeshKartInput input, PortableMeshItemControls controls, PortableMeshKartEvents *events,
    PortableMeshItemEvents *item_events, char *error, size_t capacity)
{
    return tick_with_worlds(session, world, config, NULL, NULL, input, controls, events,
                            item_events, NULL, error, capacity);
}
PortableMeshStatus portable_mesh_time_trial_tick_actors(
    PortableMeshTimeTrial *session, PortableMeshItems *world, const PortableMeshItemsConfig *config,
    PortableMeshCourseActors *actors, const PortableMeshCourseActorsConfig *actor_config,
    PortableMeshKartInput input, PortableMeshItemControls controls, PortableMeshKartEvents *events,
    PortableMeshItemEvents *item_events, PortableMeshCourseActorEvents *actor_events, char *error,
    size_t capacity)
{
    if (!actors || !actor_config || !actor_events)
        return item_trial_error(session, error, capacity);
    return tick_with_worlds(session, world, config, actors, actor_config, input, controls, events,
                            item_events, actor_events, error, capacity);
}
