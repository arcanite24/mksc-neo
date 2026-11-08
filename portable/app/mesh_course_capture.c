#include "mesh_course_capture.h"
#include "mesh_course_actors.h"
#include "fixed_3d.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#define Q 65536
#define U INT32_C(1073741824)
static int fail(char *error, size_t cap, uint32_t course, const char *detail)
{
    if (error && cap)
        snprintf(error, cap, "3D course=%u capture: %s", course, detail);
    return 0;
}
static int near_tile(int64_t kart_q16, int32_t actor_q16)
{
    const int64_t difference = kart_q16 / Q / 8 - actor_q16 / Q / 8;
    return difference >= -4 && difference <= 4;
}
static int active_valid(const PortableMeshCourseActorsConfig *c, const PortableMeshCourseActors *w,
                         const PortableMeshCaptureState *s)
{
    if (!s->remaining)
    {
        const PortableMeshCaptureState zero = {0};
        return !memcmp(s, &zero, sizeof zero);
    }
    if (s->remaining > 92 || !s->actor_index_plus1 || s->actor_index_plus1 > c->piranha_count ||
        s->started_tick > w->tick || s->spin > 65535)
        return 0;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (s->forward_q30[axis] < -U || s->forward_q30[axis] > U ||
            s->up_q30[axis] < -U || s->up_q30[axis] > U ||
            s->pull_q16[axis] < -2 * (int64_t)c->scale_q16 ||
            s->pull_q16[axis] > 2 * (int64_t)c->scale_q16)
            return 0;
    return 1;
}
int portable_mesh_course_capture_admit(const PortableMeshCourseActorsConfig *c,
    PortableMeshCourseActors *w, const PortableCourse3DView *course,
    const PortableMeshKartRules *rules, const PortableMeshItemRules *item_rules,
    const PortableMeshItemStatusOwner *status, const PortableMeshRacerState *racers,
    size_t count, uint8_t recovering, PortableMeshCourseActorEvents *events, char *error, size_t cap)
{
    if (!c || !w || !course || !rules || !status || !racers || !events || !count || count > 8 ||
        status->count != count || (recovering >> count) || c->course_id != course->id ||
        c->piranha_count > 64 || w->piranha_count != c->piranha_count || c->capture_surface_count > 256)
        return fail(error, cap, c ? c->course_id : 0, "invalid admission owner");
    if (!c->piranha_count)
        return 1;
    if (c->scale_q16 < 1311 || c->scale_q16 > 131072 ||
        !portable_mesh_kart_rules_valid(rules) || !portable_mesh_item_rules_valid(item_rules))
        return fail(error, cap, c->course_id, "invalid SI profile");
    PortableMeshCaptureState staged[8];
    memcpy(staged, w->capture, sizeof staged);
    uint8_t captured = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        PortableMeshCaptureState *s = &staged[i];
        const PortableMeshRacerState *r = &racers[i];
        if (!active_valid(c, w, s) || !portable_mesh_kart_state_valid(&r->kart))
            return fail(error, cap, c->course_id, "invalid capture/racer state");
        if (r->phase != PORTABLE_MESH_RACER_RACING || r->kart.respawn_ticks || r->respawn_grace ||
            (recovering & (1u << i)))
        {
            memset(s, 0, sizeof *s);
            continue;
        }
        if (s->remaining || !r->kart.grounded || status->racers[i].star_ticks ||
            status->racers[i].spin_ticks || status->racers[i].hit_grace_ticks ||
            status->racers[i].boo_invisible_ticks)
            continue;
        int surface = 0;
        for (unsigned j = 0; j < c->capture_surface_count; ++j)
            surface |= c->capture_surfaces[j] == r->kart.contact_surface;
        if (!surface)
            continue;
        unsigned target = UINT_MAX;
        const int32_t body_scale = status->racers[i].lightning_ticks
                                      ? item_rules->lightning_scale_ratio_q16 : Q;
        const int32_t suspension = portable_fixed_3d_mul(rules->ride_height_q16, body_scale);
        const int32_t radius = portable_fixed_3d_mul(rules->kart_radius_q16, body_scale);
        for (unsigned a = 0; a < c->piranha_count; ++a)
        {
            const int32_t *anchor = c->piranha_anchors_q16[a];
            const PortablePiranha *initial = &c->initial_piranhas[a];
            /* Invert this placement's authored source-to-SI translation. Tile
             * division truncates toward zero, as the original search does. */
            const int64_t x = initial->x_q16 +
                ((int64_t)r->kart.position_q16[0] - anchor[0]) * Q / c->scale_q16;
            const int64_t y = initial->y_q16 +
                ((int64_t)r->kart.position_q16[1] - anchor[1]) * Q / c->scale_q16;
            int64_t vertical = 0;
            for (unsigned axis = 0; axis < 3; ++axis)
                vertical += ((int64_t)r->kart.position_q16[axis] - anchor[axis]) * r->kart.up_q30[axis] / U;
            vertical -= (int64_t)radius + suspension;
            if (!near_tile(x, initial->x_q16) || !near_tile(y, initial->y_q16) ||
                vertical < -2 * (int64_t)c->scale_q16 ||
                vertical > 2 * (int64_t)c->scale_q16)
                continue;
            if (target == UINT_MAX || initial->placement_order < c->initial_piranhas[target].placement_order)
                target = a;
        }
        /* Deliberately test collision only AFTER choosing the first spatial
         * candidate: a disabled first actor shadows a later enabled actor. */
        if (target == UINT_MAX || !(w->piranhas[target].flags & 2))
            continue;
        if (c->capture_forward_speed_q16 < 1 || c->capture_forward_speed_q16 > 256 * Q ||
            c->capture_up_speed_q16 < 1 || c->capture_up_speed_q16 > 256 * Q)
            return fail(error, cap, c->course_id, "missing bounded launch speeds");
        *s = (PortableMeshCaptureState){.remaining = 92, .actor_index_plus1 = target + 1,
                                         .started_tick = w->tick};
        memcpy(s->forward_q30, r->kart.forward_q30, sizeof s->forward_q30);
        memcpy(s->up_q30, r->kart.up_q30, sizeof s->up_q30);
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            const int64_t destination = c->piranha_anchors_q16[target][axis] +
                ((int64_t)suspension + radius) * s->up_q30[axis] / U;
            s->pull_q16[axis] = (int32_t)((destination - r->kart.position_q16[axis]) / 32);
        }
        if (!active_valid(c, w, s))
            return fail(error, cap, c->course_id, "pull outside supported envelope");
        captured |= (uint8_t)(1u << i);
    }
    memcpy(w->capture, staged, sizeof staged);
    events->capture_mask |= captured;
    return 1;
}
int portable_mesh_course_capture_move(const PortableMeshCourseActorsConfig *c,
    PortableMeshCourseActors *w, unsigned index, PortableMeshRacer *racer, PortableMeshKartInput input,
    PortableMeshRacerTurn *turn, int *handled, PortableMeshCourseActorEvents *events, char *error, size_t cap)
{
    if (!c || !w || !racer || !turn || !handled || !events || index >= 8 || input.buttons > 15 ||
        c->piranha_count > 64 || w->piranha_count != c->piranha_count ||
        !active_valid(c, w, &w->capture[index]))
        return fail(error, cap, c ? c->course_id : 0, "invalid motion owner");
    PortableMeshCaptureState s = w->capture[index];
    if (s.remaining && (c->scale_q16 < 1311 || c->scale_q16 > 131072 ||
        !c->capture_surface_count || c->capture_forward_speed_q16 < 1 ||
        c->capture_forward_speed_q16 > 256 * Q || c->capture_up_speed_q16 < 1 ||
        c->capture_up_speed_q16 > 256 * Q))
        return fail(error, cap, c->course_id, "invalid active SI profile");
    if (!s.remaining)
    {
        *handled = 0;
        return 1;
    }
    PortableMeshRacer next = *racer;
    PortableMeshKartState *kart = &next.state.kart;
    if (!portable_mesh_kart_state_valid(kart) || next.state.phase != PORTABLE_MESH_RACER_RACING ||
        next.state.tick == UINT32_MAX || next.state.race_ticks == UINT32_MAX)
        return fail(error, cap, c->course_id, "invalid held kart");
    memset(kart->boost_ticks, 0, sizeof kart->boost_ticks);
    kart->panel_ticks = kart->drift_charge = kart->drift_tier = 0;
    kart->drift_direction = 0;
    kart->outside_drift_q16 = 0;
    kart->speed_q16 = 0;
    memset(kart->velocity_q16, 0, sizeof kart->velocity_q16);
    if (w->tick != s.started_tick)
    {
        if (s.remaining > 59)
        {
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                int64_t position = (int64_t)kart->position_q16[axis] + s.pull_q16[axis];
                if (position < -4096 * Q || position > 4096 * Q)
                    return fail(error, cap, c->course_id, "pull outside world bounds");
                kart->position_q16[axis] = (int32_t)position;
            }
            s.spin = (s.spin + 0xc00u) & 65535u;
        }
        if (!--s.remaining)
        {
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                int64_t velocity = ((int64_t)c->capture_forward_speed_q16 * s.forward_q30[axis] +
                                    (int64_t)c->capture_up_speed_q16 * s.up_q30[axis]) / U;
                if (velocity < -256 * Q || velocity > 256 * Q)
                    return fail(error, cap, c->course_id, "launch velocity outside SI bounds");
                kart->velocity_q16[axis] = (int32_t)velocity;
            }
            kart->speed_q16 = c->capture_forward_speed_q16;
            kart->grounded = 0;
            kart->air_ticks = 0;
            kart->contact_triangle = kart->contact_surface = PORTABLE_MESH_ABSENT;
            if (!portable_mesh_kart_state_valid(kart))
                return fail(error, cap, c->course_id, "invalid launched kart");
            *racer = next;
            memset(&w->capture[index], 0, sizeof w->capture[index]);
            events->launch_mask |= (uint8_t)(1u << index);
            *handled = 0;
            return 1;
        }
    }
    memcpy(next.state.previous_position_q16, racer->state.kart.position_q16,
           sizeof next.state.previous_position_q16);
    ++next.state.tick;
    ++next.state.race_ticks;
    kart->previous_buttons = input.buttons;
    if (!portable_mesh_kart_state_valid(kart))
        return fail(error, cap, c->course_id, "invalid pulled kart");
    const PortableMeshRacerTurn held_turn = {.finish_fraction_q30 = UINT32_MAX};
    *racer = next;
    w->capture[index] = s;
    *turn = held_turn;
    *handled = 1;
    return 1;
}
