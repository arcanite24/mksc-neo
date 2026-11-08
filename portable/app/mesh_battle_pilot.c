#include "mesh_battle_pilot.h"
#include "mesh_time_trial_pilot.h"
#include "mesh_query.h"
#include "fixed_3d.h"
#include "mesh_kart_mk8.h"
#include <stdio.h>
#include <string.h>

#define Q PORTABLE_FIXED_3D_ONE
#define U PORTABLE_FIXED_3D_UNIT

static PortableMeshStatus fail(const PortableMeshRaceSession *race, unsigned racer,
    PortableMeshStatus status, const char *state, char *error, size_t capacity)
{
    if (error && capacity)
        snprintf(error, capacity, "3D Battle course=%u tick=%u racer=%u pilot: %s",
            race && race->course ? (unsigned)race->course->id : 0,
            race ? (unsigned)race->state.tick : 0, racer, state);
    return status;
}

static uint64_t squared(const int32_t a[3], const int32_t b[3])
{
    uint64_t result = 0;
    for (unsigned i = 0; i < 3; ++i)
    {
        const int64_t delta = (int64_t)a[i] - b[i];
        result += (uint64_t)(delta * delta);
    }
    return result;
}

static int triangle(const void *context, uint32_t index, PortableMeshTriangle *out)
{
    PortableCourse3DCollision row;
    if (!portable_course_3d_collision(context, index, &row) || row.gravity_zone != PORTABLE_COURSE_3D_ABSENT)
        return 0;
    for (unsigned i = 0; i < 3; ++i)
        if (!portable_course_3d_collision_vertex(context, row.vertices[i], out->vertices_q16[i]))
            return 0;
    out->surface = row.surface;
    out->respawn = row.respawn;
    return 1;
}
static int surface(const void *context, uint32_t index, PortableMeshSurface *out)
{
    PortableCourse3DSurface row;
    if (!portable_course_3d_surface(context, index, &row))
        return 0;
    *out = (PortableMeshSurface){.type = row.type};
    return 1;
}

static int eligible(const PortableMeshRaceSession *race, unsigned index, int unprotected)
{
    const PortableMeshRacerState *r = &race->state.racers[index];
    if (!race->battle.balloons[index] || r->phase != PORTABLE_MESH_RACER_RACING ||
        r->kart.respawn_ticks || r->respawn_grace || race->recovery_pending[index] ||
        race->bomb_recovery_ticks[index])
        return 0;
    if (race->items_enabled)
    {
        const PortableMeshItemRacerStatus *s = &race->items.status.racers[index];
        if (s->boo_invisible_ticks || (unprotected && (s->star_ticks || s->hit_grace_ticks)))
            return 0;
    }
    return 1;
}

static PortableMeshStatus patrol(const PortableMeshRaceSession *race, unsigned racer,
    PortableMeshKartInput *out, char *error, size_t capacity)
{
    const PortableCourse3DTable *headers = portable_course_3d_table(race->course,
        PORTABLE_COURSE_3D_TAG('R','T','H','D'));
    const PortableCourse3DTable *nodes = portable_course_3d_table(race->course,
        PORTABLE_COURSE_3D_TAG('R','T','N','D'));
    const PortableCourse3DTable *edges = portable_course_3d_table(race->course,
        PORTABLE_COURSE_3D_TAG('R','T','E','D'));
    if (!headers || !headers->bytes || headers->stride != 32 || headers->count > 16384 ||
        !nodes || !nodes->bytes || nodes->stride != 32 || nodes->count > 16384 ||
        !edges || !edges->bytes || edges->stride != 4 || edges->count > 16384)
        return fail(race, racer, PORTABLE_MESH_UNSUPPORTED, "no closed CPU patrol route", error, capacity);
    int found = 0;
    for (uint32_t i = 0; i < headers->count; ++i)
    {
        PortableCourse3DRoute route;
        if (!portable_course_3d_route(race->course, i, &route))
            return fail(race, racer, PORTABLE_MESH_INVALID, "invalid patrol header", error, capacity);
        if (route.kind)
            continue;
        if (route.flags != 1 || !route.node_count || route.node_first > nodes->count ||
            route.node_count > nodes->count - route.node_first)
            break; /* The shared route pilot selects this first CPU header. */
        for (uint32_t n = route.node_first; n < route.node_first + route.node_count; ++n)
        {
            PortableCourse3DRouteNode node;
            if (!portable_course_3d_route_node(race->course, n, &node) || !node.edge_count ||
                node.edge_first > edges->count || node.edge_count > edges->count - node.edge_first)
                return fail(race, racer, PORTABLE_MESH_INVALID, "invalid patrol node", error, capacity);
            for (uint32_t e = node.edge_first; e < node.edge_first + node.edge_count; ++e)
            {
                uint32_t target;
                if (!portable_course_3d_route_edge(race->course, e, &target) ||
                    target < route.node_first || target - route.node_first >= route.node_count)
                    return fail(race, racer, PORTABLE_MESH_INVALID, "disconnected patrol edge", error, capacity);
            }
        }
        found = 1;
        break;
    }
    if (!found)
        return fail(race, racer, PORTABLE_MESH_UNSUPPORTED, "no closed CPU patrol route", error, capacity);
    const PortableMeshTimeTrialSession borrowed = {.course = race->course, .rules = race->rules,
        .fall_limit_q16 = race->fall_limit_q16, .spawn_grid = race->spawn_grids[racer], .mode = 16,
        .state = race->state.racers[racer]};
    char detail[128];
    const PortableMeshStatus result = portable_mesh_time_trial_pilot(&borrowed, out, detail, sizeof detail);
    return result == PORTABLE_MESH_OK ? result
        : fail(race, racer, result, "CPU patrol geometry unavailable", error, capacity);
}

PortableMeshStatus portable_mesh_battle_pilot(const PortableMeshRaceSession *race, unsigned racer,
    PortableMeshKartInput *driving, PortableMeshItemControls *items, char *error, size_t capacity)
{
    if (!race || !driving || !items || race->mode != 16 || !race->course ||
        race->course->table_count > PORTABLE_COURSE_3D_TABLE_LIMIT ||
        !portable_mesh_kart_rules_valid(race->rules) || race->state.player_count < 2 ||
        race->state.player_count > 8 || racer >= race->state.player_count ||
        race->battle.config.racer_count != race->state.player_count || race->items_enabled > 1)
        return fail(race, racer, PORTABLE_MESH_INVALID, "invalid world/profile/outputs", error, capacity);
    for (unsigned i = 0; i < race->state.player_count; ++i)
        if (!portable_mesh_kart_state_valid(&race->state.racers[i].kart))
            return fail(race, racer, PORTABLE_MESH_INVALID, "invalid racer pose", error, capacity);
    if (race->items_enabled && (!portable_mesh_item_rules_valid(race->item_rules) ||
        race->items.inventory.count != race->state.player_count ||
        race->items.status.count != race->state.player_count || race->items.prepared))
        return fail(race, racer, PORTABLE_MESH_INVALID, "invalid accepted item world", error, capacity);
    PortableMeshKartInput drive = {0};
    PortableMeshItemControls control = {0};
    const PortableMeshRacerState *state = &race->state.racers[racer];
    if (race->state.results_ready || race->battle.finished ||
        state->phase != PORTABLE_MESH_RACER_RACING || state->kart.respawn_ticks ||
        state->respawn_grace || race->recovery_pending[racer] || race->bomb_recovery_ticks[racer] ||
        (!race->battle.balloons[racer] && !race->battle.elimination[racer].revenge_bomb) ||
        (race->items_enabled && (race->items.status.racers[racer].spin_ticks ||
                                race->items.status.racers[racer].boo_lock_ticks)))
        goto publish;
    const int bomb = race->battle.elimination[racer].revenge_bomb != 0;
    if (!bomb && !race->items_enabled)
        return fail(race, racer, PORTABLE_MESH_UNSUPPORTED, "live combat requires enabled item rules", error, capacity);
    int held_actor = 0, orbit = 0, pending = 0;
    uint16_t held = race->items_enabled ? race->items.inventory.racers[racer].held_item : 0;
    if (held > 2 && !portable_mesh_item_id_valid(held, 1))
        return fail(race, racer, PORTABLE_MESH_INVALID, "invalid held inventory", error, capacity);
    if (race->items_enabled)
        for (unsigned i = 0; i < PORTABLE_MESH_ITEMS_ACTORS; ++i)
        {
            const PortableMeshItemActor *actor = &race->items.actors[i];
            if (actor->phase > PORTABLE_MESH_ACTOR_HAUNT ||
                (actor->phase && actor->owner >= race->state.player_count))
                return fail(race, racer, PORTABLE_MESH_INVALID, "invalid actor role", error, capacity);
            if (actor->owner == racer)
            {
                held_actor |= actor->phase == PORTABLE_MESH_ACTOR_HELD;
                orbit |= actor->phase == PORTABLE_MESH_ACTOR_ORBIT;
                pending |= actor->phase == PORTABLE_MESH_ACTOR_ORBIT_PENDING;
                if (actor->phase == PORTABLE_MESH_ACTOR_HELD)
                    held = actor->item;
            }
        }
    const int armed = held > 2 || held_actor || orbit || pending;
    control.item_pressed = (uint8_t)held_actor; /* Never blindly drop while patrolling/turning. */
    int32_t target[3] = {0};
    int32_t radius = race->rules->kart_radius_q16;
    uint64_t nearest = UINT64_MAX;
    int found = 0;
    if (bomb || armed)
    {
        for (unsigned i = 0; i < race->state.player_count; ++i)
            if (i != racer && eligible(race, i, !bomb))
            {
                const uint64_t distance = squared(state->kart.position_q16, race->state.racers[i].kart.position_q16);
                if (distance < nearest)
                {
                    nearest = distance;
                    memcpy(target, race->state.racers[i].kart.position_q16, sizeof target);
                    found = 1;
                }
            }
    }
    else
    {
        const PortableCourse3DTable *placements = portable_course_3d_table(race->course,
            PORTABLE_COURSE_3D_TAG('I','T','E','M'));
        if (!placements || placements->count > PORTABLE_MESH_ITEM_BOX_CAPACITY ||
            race->items.boxes.count != placements->count)
            return fail(race, racer, PORTABLE_MESH_INVALID, "invalid ITEM/cooldown count", error, capacity);
        for (uint32_t i = 0; i < placements->count; ++i)
        {
            PortableCourse3DItem box;
            if (!portable_course_3d_item(race->course, i, &box))
                return fail(race, racer, PORTABLE_MESH_INVALID, "invalid ITEM placement", error, capacity);
            if (race->items.boxes.cooldowns[i])
                continue;
            const uint64_t distance = squared(state->kart.position_q16, box.position_q16);
            if (distance < nearest)
            {
                nearest = distance;
                memcpy(target, box.position_q16, sizeof target);
                radius = box.radius_q16 / 2;
                found = 1;
            }
        }
    }
    if (found)
    {
        const PortableCourse3DTable *triangles = portable_course_3d_table(race->course,
            PORTABLE_COURSE_3D_TAG('C','O','L','T'));
        if (!triangles || !triangles->count || triangles->count > 262144)
            return fail(race, racer, PORTABLE_MESH_INVALID, "missing collision faces", error, capacity);
        const PortableMeshCourse linear = {race->course, triangles->count, 0, triangle, surface, NULL, NULL};
        const PortableMeshCourse *geometry = race->geometry ? race->geometry : &linear;
        PortableMeshFaceHit hit;
        const PortableMeshQueryStatus clear = portable_mesh_query_sweep_faces(geometry,
            state->kart.position_q16, target, 1, &hit);
        if (clear == PORTABLE_MESH_QUERY_INVALID)
            return fail(race, racer, PORTABLE_MESH_INVALID, "invalid line-of-sight geometry", error, capacity);
        found = clear == PORTABLE_MESH_QUERY_MISS;
    }
    if (!found)
    {
        const PortableMeshStatus result = patrol(race, racer, &drive, error, capacity);
        if (result != PORTABLE_MESH_OK)
            return result;
        goto publish;
    }
    int64_t raw[3];
    for (unsigned i = 0; i < 3; ++i)
        raw[i] = (int64_t)target[i] - state->kart.position_q16[i];
    const int64_t normal = (raw[0] * state->kart.up_q30[0] + raw[1] * state->kart.up_q30[1] +
                            raw[2] * state->kart.up_q30[2]) / U;
    for (unsigned i = 0; i < 3; ++i)
        raw[i] -= normal * state->kart.up_q30[i] / U;
    int32_t desired[3], right[3];
    if (!portable_fixed_3d_normalize(raw, desired))
    {
        drive.buttons = PORTABLE_MESH_BRAKE;
        goto publish;
    }
    for (unsigned i = 0; i < 3; ++i)
        right[i] = (int32_t)(((int64_t)state->kart.up_q30[(i + 1) % 3] * state->kart.forward_q30[(i + 2) % 3] -
                              (int64_t)state->kart.up_q30[(i + 2) % 3] * state->kart.forward_q30[(i + 1) % 3]) / U);
    const int64_t sine = portable_fixed_3d_dot(right, desired) / U;
    const int64_t cosine = portable_fixed_3d_dot(state->kart.forward_q30, desired) / U;
    /* Slowing never reaches a target inside the tightest turning circle on
     * its side; such a kart orbits it forever. Drive straight out until the
     * target clears the circle. */
    const int32_t arc = MESH_KART_MK8_UNIT_SPEED_Q16 / 2;
    const int64_t turn_radius = portable_mesh_kart_min_turn_radius(race->rules);
    int64_t ahead = 0, lateral = 0;
    for (unsigned i = 0; i < 3; ++i)
    {
        ahead += raw[i] * state->kart.forward_q30[i] / U;
        lateral += raw[i] * right[i] / U;
    }
    const int64_t side = lateral < 0 ? -lateral : lateral;
    const int inside =
        ahead * ahead + lateral * lateral < 2 * turn_radius * side;
    int64_t steering = sine * 65534 / U;
    if (cosine < 0)
        steering = sine < 0 ? -32767 : 32767;
    drive.steering = (int16_t)(steering < -32767 ? -32767 : steering > 32767 ? 32767 : steering);
    int32_t speed = race->rules->forward_speed_q16;
    if (speed > 8 * Q)
        speed = 8 * Q;
    if (cosine < 7 * (int64_t)U / 8 && speed > 3 * Q)
        speed = 3 * Q;
    if (cosine < 0 && speed > Q)
        speed = Q;
    /* A target inside the current turning circle but outside the tightest
     * one is reached by slowing to the low-speed arc. */
    if (!bomb && state->kart.speed_q16 > arc && speed > arc)
    {
        const int64_t now = (int64_t)portable_mesh_kart_turn_rate(race->rules, state->kart.speed_q16) *
            INT64_C(411775) / Q;
        const int64_t radius = now > 0 ? (int64_t)state->kart.speed_q16 * Q / now : 0;
        if (radius > 0 && ahead * ahead + lateral * lateral < 2 * radius * side)
            speed = arc;
    }
    const int32_t deceleration = race->rules->brake_deceleration_q16 > race->rules->coast_deceleration_q16
        ? race->rules->brake_deceleration_q16 : race->rules->coast_deceleration_q16;
    if (!deceleration)
        return fail(race, racer, PORTABLE_MESH_UNSUPPORTED, "no authored brake/coast deceleration", error, capacity);
    const int64_t distance = (int64_t)portable_fixed_3d_sqrt(nearest);
    int64_t gap = distance - radius;
    if (gap < Q / 4)
        gap = Q / 4;
    const uint64_t stopping_speed = portable_fixed_3d_sqrt((uint64_t)(2 * (int64_t)deceleration * gap));
    /* A revenge bomb wants the contact, so it never brakes for the gap. */
    if (!bomb && stopping_speed < (uint64_t)speed)
        speed = (int32_t)stopping_speed;
    drive.buttons = state->kart.speed_q16 > speed + Q / 4
        ? (race->rules->brake_deceleration_q16 >= race->rules->coast_deceleration_q16
            ? PORTABLE_MESH_BRAKE : 0) : PORTABLE_MESH_ACCELERATE;
    if (!bomb && armed && !pending)
    {
        const int64_t range = race->item_rules->cpu_use_distance_q16;
        const int64_t back = race->item_rules->cpu_backward_distance_q16;
        const int aligned = sine >= -U / 4 && sine <= U / 4;
        const int forward = aligned && cosine >= 7 * (int64_t)U / 8 && nearest <= (uint64_t)(range * range);
        const int backward = aligned && cosine <= -7 * (int64_t)U / 8 &&
            (held == 0x1000 || held == 0x1002 || held == 0x1005) && nearest <= (uint64_t)(back * back);
        const int fire = forward || backward;
        if (!fire && nearest <= (uint64_t)(range * range))
        {
            /* Equal-speed pursuit can orbit forever. Within authored launch
             * range, shed travel while steering into the unchanged aim cone.
             * Do not hold BRAKE through zero: the kart would reverse. Karts
             * cannot pivot in place, so creep on the accelerator to turn;
             * MK8 turning needs about 1 m/s, so braking stops there. */
            int32_t brake_step = portable_fixed_3d_dt(race->rules->brake_deceleration_q16);
            if (brake_step < Q)
                brake_step = Q;
            drive.buttons =
                race->rules->brake_deceleration_q16 >= race->rules->coast_deceleration_q16 &&
                        state->kart.speed_q16 > brake_step
                    ? PORTABLE_MESH_BRAKE
                    : PORTABLE_MESH_ACCELERATE;
        }
        control.aim_backward = (uint8_t)backward;
        control.aim_forward = (uint8_t)(forward && held == 0x1005);
        if (held_actor)
            control.item_pressed = (uint8_t)!fire;
        else if (fire && !race->items.release_lock[racer])
            control.item_pressed = (uint8_t)!race->items.previous_item[racer];
    }
    if (inside)
    {
        drive.steering = 0;
        drive.buttons = PORTABLE_MESH_ACCELERATE;
    }
publish:
    *driving = drive;
    *items = control;
    if (error && capacity)
        error[0] = 0;
    return PORTABLE_MESH_OK;
}
