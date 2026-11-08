#include "mesh_items.h"
#include "fixed_3d.h"
#include "gp_item_inventory.h"
#include "mesh_query.h"
#include <stdio.h>
#include <string.h>
#define Q PORTABLE_FIXED_3D_ONE
#define U PORTABLE_FIXED_3D_UNIT
#define ABSENT UINT8_MAX

/* Items that fly and rest like a banana: thrown forward or dropped behind. */
static int dropped_item(uint16_t item)
{
    return item == 0x1005 || item == PORTABLE_MESH_ITEM_FAKE_BOX || item == PORTABLE_MESH_ITEM_BOMB;
}
/* Bananas attached in a line behind their owner (triples, bunches); the
 * actor's angle holds its place in the line. */
static int trailing(const PortableMeshItemActor *a)
{
    return a->item == 0x1005 &&
           (a->phase == PORTABLE_MESH_ACTOR_ORBIT || a->phase == PORTABLE_MESH_ACTOR_ORBIT_PENDING);
}
static int racer_protected(const PortableMeshItems *w, unsigned i)
{
    return w->status.racers[i].star_ticks || w->status.racers[i].bullet_ticks;
}

static PortableMeshItemsResult fail(const PortableMeshItemsConfig *c, const PortableMeshItems *w,
                                    unsigned actor, const char *reason, char *error, size_t cap)
{
    if (error && cap)
        snprintf(error, cap, "3D items course=%u mode=%u tick=%u actor=%u: %s",
                 c && c->course ? c->course->id : 0, c ? c->mode : 0, w ? w->tick : 0, actor,
                 reason);
    return PORTABLE_MESH_ITEMS_INVALID;
}
static PortableMeshItemsResult budget(const PortableMeshItemsConfig *c, const PortableMeshItems *w,
                                      unsigned actor, const char *reason, char *error,
                                      size_t capacity)
{
    (void)fail(c, w, actor, reason, error, capacity);
    return PORTABLE_MESH_ITEMS_BUDGET;
}
static int config_valid(const PortableMeshItemsConfig *c)
{
    return c && c->course && c->course->table_count <= PORTABLE_COURSE_3D_TABLE_LIMIT && c->count &&
           c->count <= 8 && !(c->human_mask >> c->count) &&
           !(c->inactive_mask >> c->count) &&
           !(c->bomb_mask & ~c->inactive_mask) &&
           (c->mode == 16u || !(c->inactive_mask | c->bomb_mask)) &&
           (c->mode == 1u || c->mode == 2u || c->mode == 4u || c->mode == 8u || c->mode == 16u) &&
           portable_mesh_item_rules_valid(c->rules) &&
           portable_mesh_kart_rules_valid(c->kart_rules) &&
           portable_mesh_item_rules_fit_profile(c->rules, c->kart_rules);
}
static int motion_active(const PortableMeshItems *w, const PortableMeshRacerState *r, unsigned i)
{
    return r[i].phase == PORTABLE_MESH_RACER_RACING && !r[i].kart.respawn_ticks &&
           !r[i].respawn_grace && !w->controls[i].recovering;
}
static int active(const PortableMeshItems *w, const PortableMeshItemsConfig *c,
                  const PortableMeshRacerState *r, unsigned i)
{
    return !(c->inactive_mask & (1u << i)) && motion_active(w, r, i);
}
static int bomb_contacted(const PortableMeshItemEvents *events, unsigned target)
{
    for(unsigned i=0;i<events->count;++i)
        if(events->values[i].kind==PORTABLE_MESH_EVENT_BOMB_CONTACT &&
           events->values[i].target==target)return 1;
    return 0;
}
static int event(PortableMeshItemEvents *e, unsigned kind, unsigned actor, unsigned racer,
                 unsigned target, unsigned item)
{
    if (e->count >= PORTABLE_MESH_ITEMS_EVENTS)
        return 0;
    e->values[e->count++] = (PortableMeshItemEvent){.kind = (uint8_t)kind,
                                                    .actor = actor,
                                                    .racer = (uint8_t)racer,
                                                    .target = (uint8_t)target,
                                                    .item = (uint16_t)item,
                                                    .placement = UINT32_MAX};
    return 1;
}
static int actor_event(const PortableMeshItems *w, PortableMeshItemEvents *e, unsigned kind,
                       unsigned actor, unsigned racer, unsigned target, unsigned item)
{
    if (!event(e, kind, actor, racer, target, item))
        return 0;
    if (actor < PORTABLE_MESH_ITEMS_ACTORS)
        e->values[e->count - 1].serial = w->actors[actor].serial;
    return 1;
}
static uint64_t distance2(const int32_t a[3], const int32_t b[3])
{
    uint64_t sum = 0;
    for (unsigned i = 0; i < 3; ++i)
    {
        int64_t d = (int64_t)a[i] - b[i];
        sum += (uint64_t)(d * d);
    }
    return sum;
}
static int triangle(const void *ctx, uint32_t i, PortableMeshTriangle *out)
{
    PortableCourse3DCollision row;
    if (!portable_course_3d_collision(ctx, i, &row))
        return 0;
    /* Moving rows collide items at their rest pose. */
    if (row.gravity_zone != PORTABLE_COURSE_3D_ABSENT)
        return 0;
    out->surface = row.surface;
    out->respawn = row.respawn;
    for (unsigned a = 0; a < 3; ++a)
        if (!portable_course_3d_collision_vertex(ctx, row.vertices[a], out->vertices_q16[a]))
            return 0;
    return 1;
}
static int surface(const void *ctx, uint32_t i, PortableMeshSurface *out)
{
    PortableCourse3DSurface row;
    if (!portable_course_3d_surface(ctx, i, &row))
        return 0;
    portable_mesh_surface_from_course(&row, out);
    return 1;
}
static int mesh(const PortableMeshItemsConfig *c, PortableMeshCourse *out)
{
    if (c->geometry)
    {
        *out = *c->geometry;
        return 1;
    }
    PortableCourse3DMeta meta;
    const PortableCourse3DTable *t =
        portable_course_3d_table(c->course, PORTABLE_COURSE_3D_TAG('C', 'O', 'L', 'T'));
    if (!t || !t->count || !portable_course_3d_meta(c->course, &meta))
        return 0;
    *out = (PortableMeshCourse){c->course, t->count, meta.gravity_q16, triangle, surface, NULL, NULL};
    return 1;
}
static int world_valid(const PortableMeshItems *w, const PortableMeshItemsConfig *c)
{
    if (!w || !config_valid(c) || w->inventory.count != c->count ||
        w->inventory.human_mask != c->human_mask || w->status.count != c->count ||
        w->status.human_mask != c->human_mask || w->prepared > 1)
        return 0;
    unsigned auxiliary;
    if (portable_mesh_item_status_auxiliary_count(&w->status, c->rules, &auxiliary) !=
        PORTABLE_MESH_ITEM_STATUS_OK)
        return 0;
    unsigned actors = 0;
    for (unsigned i = 0; i < 8; ++i)
    {
        if (i < c->count)
        {
            uint16_t hud;
            if (portable_mesh_item_inventory_hud(&w->inventory, i, &hud) != PORTABLE_MESH_ITEM_OK)
                return 0;
        }
        if (w->previous_item[i] > 1 || w->controls[i].item_pressed > 1 ||
            w->controls[i].aim_forward > 1 || w->controls[i].aim_backward > 1 ||
            w->controls[i].recovering > 1 ||
            w->release_lock[i] > c->rules->actors_release_lock_ticks ||
            w->cpu_wait[i] > c->rules->cpu_decision_ticks ||
            w->cpu_hold[i] > c->rules->cpu_hold_ticks)
            return 0;
    }
    for (unsigned i = 0; i < 64; ++i)
    {
        const PortableMeshItemActor *a = &w->actors[i];
        if (!a->phase)
            continue;
        ++actors;
        if (a->phase > PORTABLE_MESH_ACTOR_BLAST || !a->serial || a->serial > w->serial ||
            a->owner >= c->count || (a->target != ABSENT && a->target >= c->count) ||
            !portable_mesh_item_id_valid(a->item, 0) || !a->lifetime ||
            a->lifetime > c->rules->actors_lifetime_ticks || a->haunted > 1 ||
            a->motion.grounded > 1 || a->motion.mode > PORTABLE_MESH_ITEM_BANANA)
            return 0;
        for (unsigned j = 0; j < 3; ++j)
            if (a->motion.position_q16[j] < -PORTABLE_FIXED_3D_POSITION_LIMIT ||
                a->motion.position_q16[j] > PORTABLE_FIXED_3D_POSITION_LIMIT ||
                a->motion.velocity_q16[j] < -256 * Q || a->motion.velocity_q16[j] > 256 * Q)
                return 0;
    }
    return actors + auxiliary <= (unsigned)c->rules->actors_capacity;
}
static int racers_valid(const PortableMeshItemsConfig *c, const PortableMeshRacerState *r)
{
    if (!r)
        return 0;
    for (unsigned i = 0; i < c->count; ++i)
        if (r[i].phase > PORTABLE_MESH_RACER_FINISHED ||
            !portable_mesh_kart_state_valid(&r[i].kart))
            return 0;
    return 1;
}
static unsigned used(const PortableMeshItems *w)
{
    unsigned n = 0;
    for (unsigned i = 0; i < 64; ++i)
    {
        n += w->actors[i].phase != 0;
        n += w->status.auxiliaries[i].ticks != 0;
    }
    return n;
}
/* Keeps the button edge: a still-held press must not spend the next item. */
static void clear_inventory(PortableMeshItemInventory *inventory, unsigned i, uint16_t item)
{
    inventory->racers[i] = (PortableMeshItemRacer){.held_item = item, .preview_item = item,
        .settled_item = item, .previous_item_pressed = inventory->racers[i].previous_item_pressed};
}
static int attach(PortableMeshItemActor *a, const PortableMeshRacerState *r,
                  const PortableMeshItemRules *rules)
{
    int32_t p[3];
    int32_t sine = 0, cosine = U;
    const int line = trailing(a);
    if (!line && (a->phase == PORTABLE_MESH_ACTOR_ORBIT || a->phase == PORTABLE_MESH_ACTOR_ORBIT_PENDING))
        portable_fixed_3d_sincos(a->angle, &sine, &cosine);
    for (unsigned j = 0; j < 3; ++j)
    {
        int64_t offset = -(int64_t)r->kart.forward_q30[j] * rules->actors_hold_distance_q16 / U;
        if (line)
            offset = -(int64_t)r->kart.forward_q30[j] *
                     ((int64_t)rules->actors_hold_distance_q16 + (int64_t)a->angle * rules->actors_radius_q16 * 2) / U;
        else if (a->phase == PORTABLE_MESH_ACTOR_ORBIT || a->phase == PORTABLE_MESH_ACTOR_ORBIT_PENDING)
        {
            const int64_t right =
                (int64_t)r->kart.forward_q30[(j + 1) % 3] * r->kart.up_q30[(j + 2) % 3] -
                (int64_t)r->kart.forward_q30[(j + 2) % 3] * r->kart.up_q30[(j + 1) % 3];
            offset = ((int64_t)r->kart.forward_q30[j] * cosine / U + (right / U) * sine / U) *
                     rules->actors_orbit_radius_q16 / U;
        }
        offset += (int64_t)r->kart.up_q30[j] * rules->actors_hold_height_q16 / U;
        const int64_t v = (int64_t)r->kart.position_q16[j] + offset;
        if (v < -PORTABLE_FIXED_3D_POSITION_LIMIT || v > PORTABLE_FIXED_3D_POSITION_LIMIT)
            return 0;
        p[j] = (int32_t)v;
    }
    memcpy(a->motion.position_q16, p, sizeof p);
    memcpy(a->motion.support_normal_q30, r->kart.up_q30, sizeof r->kart.up_q30);
    a->motion.grounded = r->kart.grounded;
    return 1;
}
static unsigned rank_of(const PortableMeshItems *w, unsigned racer, unsigned count)
{
    for (unsigned i = 0; i < count; ++i)
        if (w->standings[i] == racer)
            return i;
    return count;
}
static int remove_actor(PortableMeshItems *w, unsigned slot, PortableMeshItemEvents *e)
{
    PortableMeshItemActor *a = &w->actors[slot];
    if (!a->phase)
        return 1;
    if (!actor_event(w, e, PORTABLE_MESH_EVENT_DESTROY, slot, a->owner, a->target, a->item))
        return 0;
    *a = (PortableMeshItemActor){0};
    return 1;
}
static int create_actor(PortableMeshItems *w, const PortableMeshItemsConfig *c,
                        PortableMeshRacerState *r, unsigned owner, uint16_t item, unsigned phase,
                        unsigned delay, unsigned target)
{
    if (w->serial == UINT32_MAX)
        return 0;
    for (unsigned i = 0; i < 64; ++i)
        if (!w->actors[i].phase)
        {
            PortableMeshItemActor a = {.item = item,
                                       .phase = (uint8_t)phase,
                                       .owner = (uint8_t)owner,
                                       .target = (uint8_t)target,
                                       .delay = (uint16_t)delay,
                                       .lifetime = (uint16_t)c->rules->actors_lifetime_ticks,
                                       .grace = (uint16_t)c->rules->actors_owner_grace_ticks,
                                       .serial = ++w->serial};
            const int32_t zero[3] = {0};
            if (portable_mesh_item_motion_init(
                    &a.motion,
                    dropped_item(item) ? PORTABLE_MESH_ITEM_BANANA : PORTABLE_MESH_ITEM_SHELL,
                    r[owner].kart.position_q16, zero) != PORTABLE_MESH_ITEM_MOTION_OK ||
                !attach(&a, &r[owner], c->rules))
                return 0;
            w->actors[i] = a;
            return 1;
        }
    return 0;
}
static int release_actor(PortableMeshItems *w, const PortableMeshItemsConfig *c,
                         PortableMeshRacerState *r, unsigned slot, int backward, int forward,
                         PortableMeshItemEvents *e)
{
    PortableMeshItemActor *a = &w->actors[slot];
    const PortableMeshKartState *kart = &r[a->owner].kart;
    if (!attach(a, &r[a->owner], c->rules))
        return 0;
    int32_t speed = c->rules->shell_speed_q16;
    if (dropped_item(a->item))
    {
        a->motion.mode = PORTABLE_MESH_ITEM_BANANA;
        speed = forward ? c->rules->banana_throw_speed_q16 : 0;
    }
    else
    {
        const int64_t launch =
            (int64_t)c->rules->shell_speed_q16 + c->rules->shell_launch_bonus_q16;
        speed = (int32_t)(launch > 256 * Q ? 256 * Q : launch);
    }
    for (unsigned j = 0; j < 3; ++j)
    {
        a->motion.velocity_q16[j] =
            (int32_t)((int64_t)kart->forward_q30[j] * speed / U) * (backward ? -1 : 1);
        if (dropped_item(a->item) && forward)
            a->motion.velocity_q16[j] +=
                (int32_t)((int64_t)kart->up_q30[j] * c->rules->banana_throw_up_speed_q16 / U);
        if (a->motion.velocity_q16[j] < -256 * Q || a->motion.velocity_q16[j] > 256 * Q)
            return 0;
    }
    /* Recovered green/red/banana release constructors reset contact grace
     * (+38) on launch, independently of time spent attached or orbiting. */
    a->grace = (uint16_t)c->rules->actors_owner_grace_ticks;
    a->checkpoint = r[a->owner].checkpoint;
    a->phase = PORTABLE_MESH_ACTOR_FLYING;
    if (a->item == PORTABLE_MESH_ITEM_BOMB)
        a->delay = PORTABLE_MESH_BOMB_FUSE_TICKS;
    if ((a->item == 0x1002 || a->item == 0x1004) && backward)
    {
        a->phase = PORTABLE_MESH_ACTOR_TRAP_ARMING;
        a->delay = (uint16_t)c->rules->shell_trap_arm_ticks;
    }
    if (a->item == 0x1002 || a->item == 0x1004)
    {
        const unsigned rank = rank_of(w, a->owner, c->count);
        a->target = rank ? (a->item == 0x1004 ? w->standings[0] : w->standings[rank - 1]) : ABSENT;
    }
    w->release_lock[a->owner] = (uint16_t)c->rules->actors_release_lock_ticks;
    if (!actor_event(w, e, PORTABLE_MESH_EVENT_RELEASE, slot, a->owner, a->target, a->item))
        return 0;
    e->values[e->count - 1].flags =
        (uint8_t)((backward ? PORTABLE_MESH_ITEM_EVENT_BACKWARD : 0) |
                  (dropped_item(a->item) && forward ? PORTABLE_MESH_ITEM_EVENT_FORWARD_THROW : 0));
    return 1;
}
static int apply_hit_policy(PortableMeshItems *w, const PortableMeshItemsConfig *c,
                            PortableMeshRacerState *r, unsigned victim, unsigned owner,
                            unsigned slot, uint16_t item, PortableMeshItemEvents *e, int lightning)
{
    PortableMeshItemDamage hit;
    const PortableMeshItemStatusResult status =
        lightning ? portable_mesh_item_status_lightning_impact(&w->status, c->rules, victim,
                                                               motion_active(w, r, victim), &hit)
                  : portable_mesh_item_status_damage(&w->status, c->rules, victim,
                                                     active(w, c, r, victim), &hit);
    if (status != PORTABLE_MESH_ITEM_STATUS_OK)
        return 0;
    if (hit == PORTABLE_MESH_DAMAGE_IGNORED)
        return 1;
    if (!actor_event(w, e,
                     hit == PORTABLE_MESH_DAMAGE_APPLIED ? PORTABLE_MESH_EVENT_HIT
                                                         : PORTABLE_MESH_EVENT_PROTECTED,
                     slot, owner, victim, item))
        return 0;
    if (hit == PORTABLE_MESH_DAMAGE_APPLIED)
    {
        PortableMeshKartState *k = &r[victim].kart;
        const int reproject = k->outside_drift_q16 != 0;
        k->speed_q16 = portable_fixed_3d_mul(k->speed_q16, c->rules->hit_speed_ratio_q16);
        for (unsigned j = 0; j < 3; ++j)
            k->velocity_q16[j] =
                portable_fixed_3d_mul(k->velocity_q16[j], c->rules->hit_speed_ratio_q16);
        k->drift_charge = 0;
        k->drift_direction = 0;
        k->drift_tier = 0;
        k->outside_drift_q16 = 0;
        if (reproject && portable_mesh_kart_project_travel_speed(k, &k->speed_q16) != PORTABLE_MESH_OK)
            return 0;
        memset(k->boost_ticks, 0, sizeof k->boost_ticks);
        k->panel_ticks = 0;
        const int64_t d[3] = {(int64_t)k->position_q16[0] - r[owner].kart.position_q16[0],
                              (int64_t)k->position_q16[1] - r[owner].kart.position_q16[1],
                              (int64_t)k->position_q16[2] - r[owner].kart.position_q16[2]};
        int32_t direction[3];
        if (portable_fixed_3d_normalize(d, direction))
            for (unsigned j = 0; j < 3; ++j)
            {
                int64_t v = (int64_t)k->velocity_q16[j] +
                            (int64_t)direction[j] * c->rules->hit_impulse_q16 / U;
                if (v > 256 * Q)
                    v = 256 * Q;
                if (v < -256 * Q)
                    v = -256 * Q;
                k->velocity_q16[j] = (int32_t)v;
            }
    }
    return 1;
}
static int apply_hit(PortableMeshItems *w, const PortableMeshItemsConfig *c,
                     PortableMeshRacerState *r, unsigned victim, unsigned owner, unsigned slot,
                     uint16_t item, PortableMeshItemEvents *e)
{
    return apply_hit_policy(w, c, r, victim, owner, slot, item, e, 0);
}
/* 1 admitted, 0 resource/eligibility refusal, -1 malformed operation. */
static int use_item(PortableMeshItems *w, const PortableMeshItemsConfig *c,
                    PortableMeshRacerState *r, const PortableMeshItemUse *use,
                    PortableMeshItemEvents *e)
{
    const unsigned owner = use->racer;
    const uint16_t item = use->item;
    unsigned need = 0;
    if (item <= 0x1005 || item == PORTABLE_MESH_ITEM_FAKE_BOX || item == PORTABLE_MESH_ITEM_BOMB)
        need = (item == 0x1001 || item == 0x1003) ? 3 : 1;
    if (item == PORTABLE_MESH_ITEM_TRIPLE_BANANAS)
        need = 3;
    if (item == PORTABLE_MESH_ITEM_BANANA_BUNCH)
        need = 5;
    if (item == 0x1009)
        need = 1;
    if (item == 0x100b && w->standings[0] != owner)
        need = 1;
    if (used(w) + need > (unsigned)c->rules->actors_capacity)
        return 0;
    if (item == 0x1000 || item == 0x1002 || item == 0x1004 || dropped_item(item))
    {
        if (!create_actor(w, c, r, owner, item, PORTABLE_MESH_ACTOR_HELD, 0, ABSENT))
            return -1;
    }
    else if (item == PORTABLE_MESH_ITEM_TRIPLE_BANANAS || item == PORTABLE_MESH_ITEM_BANANA_BUNCH)
    {
        /* A line of bananas trails the kart; each press drops the nearest. */
        const int32_t delays[3] = {c->rules->actors_orbit_delay_0, c->rules->actors_orbit_delay_1,
                                   c->rules->actors_orbit_delay_2};
        for (unsigned i = 0; i < need; ++i)
        {
            if (!create_actor(w, c, r, owner, 0x1005, PORTABLE_MESH_ACTOR_ORBIT_PENDING,
                              (unsigned)(i < 3u ? delays[i] : delays[2] + (int32_t)(i - 2u) * 4), ABSENT))
                return -1;
            for (unsigned a = PORTABLE_MESH_ITEMS_ACTORS; a-- > 0;)
                if (w->actors[a].serial == w->serial)
                    w->actors[a].angle = (uint16_t)i;
        }
    }
    else if (item == PORTABLE_MESH_ITEM_BULLET || item == PORTABLE_MESH_ITEM_BLOOPER)
    {
        if (item == PORTABLE_MESH_ITEM_BULLET)
        {
            if (portable_mesh_item_status_activate(&w->status, c->rules, owner, PORTABLE_MESH_STATUS_BULLET) !=
                PORTABLE_MESH_ITEM_STATUS_OK)
                return -1;
        }
        else
        {
            /* Ink every racer ahead of the user. */
            const unsigned rank = rank_of(w, owner, c->count);
            for (unsigned ahead = 0; ahead < rank; ++ahead)
            {
                const PortableMeshItemStatusResult status = portable_mesh_item_status_activate(
                    &w->status, c->rules, w->standings[ahead], PORTABLE_MESH_STATUS_INK);
                if (status != PORTABLE_MESH_ITEM_STATUS_OK && status != PORTABLE_MESH_ITEM_STATUS_INELIGIBLE)
                    return -1;
            }
        }
    }
    else if (item == 0x1001 || item == 0x1003)
    {
        const uint16_t base = item == 0x1001 ? 0x1000 : 0x1002;
        const int32_t delays[3] = {c->rules->actors_orbit_delay_0, c->rules->actors_orbit_delay_1,
                                   c->rules->actors_orbit_delay_2};
        for (unsigned i = 0; i < 3; ++i)
            if (!create_actor(w, c, r, owner, base, PORTABLE_MESH_ACTOR_ORBIT_PENDING,
                              (unsigned)delays[i], ABSENT))
                return -1;
    }
    else if (item == 0x1007 || item == 0x1008 || item == 0x100f || item == 0x1009 ||
             item == 0x100e || item == 0x100a || item == 0x100b)
    {
        PortableMeshItemActivation activation = PORTABLE_MESH_STATUS_MUSHROOM;
        if (item == 0x1009)
            activation = PORTABLE_MESH_STATUS_GOLDEN_START;
        if (item == 0x100e)
            activation = PORTABLE_MESH_STATUS_GOLDEN_REPEAT;
        if (item == 0x100a)
            activation = PORTABLE_MESH_STATUS_STAR;
        if (item == 0x100b)
            activation = PORTABLE_MESH_STATUS_BOO_INVISIBLE;
        const PortableMeshItemStatusResult status =
            portable_mesh_item_status_activate(&w->status, c->rules, owner, activation);
        if (status == PORTABLE_MESH_ITEM_STATUS_RESOURCE ||
            status == PORTABLE_MESH_ITEM_STATUS_INELIGIBLE)
            return 0;
        if (status != PORTABLE_MESH_ITEM_STATUS_OK)
            return -1;
        if (item == 0x100b)
        {
            clear_inventory(&w->inventory, owner, 0);
            for (unsigned rank = 0; rank < c->count; ++rank)
            {
                const unsigned victim = w->standings[rank];
                uint16_t stolen = w->inventory.racers[victim].held_item;
                if (victim == owner || stolen <= 2)
                    continue;
                if (stolen == 0x100e)
                    stolen = 0x1009;
                clear_inventory(&w->inventory, owner, stolen);
                clear_inventory(&w->inventory, victim, 0);
                if (!event(e, PORTABLE_MESH_EVENT_THEFT, UINT32_MAX, owner, victim, stolen))
                    return -1;
                break;
            }
            if (need &&
                !create_actor(w, c, r, owner, item, PORTABLE_MESH_ACTOR_HAUNT,
                              (unsigned)c->rules->actors_release_lock_ticks, w->standings[0]))
                return -1;
        }
    }
    else if (item == 0x100c)
    {
        uint8_t eligible[8] = {0};
        for (unsigned i = 0; i < c->count; ++i)
            eligible[i] = (uint8_t)motion_active(w, r, i);
        unsigned affected;
        if (portable_mesh_item_status_lightning(&w->status, c->rules, owner, eligible, c->count,
                                                &affected) != PORTABLE_MESH_ITEM_STATUS_OK)
            return -1;
        for (unsigned i = 0; i < c->count; ++i)
            if (affected & (1u << i))
            {
                if (!apply_hit_policy(w, c, r, i, owner, UINT32_MAX, item, e, 1))
                    return -1;
            }
    }
    else if (item != 0x1006)
        return -1;
    if (item != 0x100b)
    {
        if (use->inventory_action == PORTABLE_GP_ITEM_INVENTORY_CLEAR)
            clear_inventory(&w->inventory, owner, 0);
        else if (use->inventory_action == PORTABLE_GP_ITEM_INVENTORY_SUCCESSOR)
            clear_inventory(&w->inventory, owner, use->successor);
    }
    return event(e, PORTABLE_MESH_EVENT_USE, UINT32_MAX, owner, ABSENT, item) ? 1 : -1;
}
PortableMeshItemsResult portable_mesh_items_init(PortableMeshItems *out,
                                                 const PortableMeshItemsConfig *c, uint32_t seed,
                                                 char *error, size_t capacity)
{
    if (!out || !config_valid(c))
        return fail(c, NULL, UINT32_MAX, "invalid config", error, capacity);
    PortableMeshCourse geometry;
    PortableCourse3DMeta meta;
    if (!mesh(c, &geometry) || !geometry.triangle || !geometry.surface ||
        !portable_course_3d_meta(c->course, &meta) ||
        !(meta.modes & c->mode) || meta.gravity_q16 < 0 || meta.gravity_q16 > 1024 * Q)
        return fail(c, NULL, UINT32_MAX, "unsupported course mode/gravity/collision", error,
                    capacity);
    for (uint32_t i = 0; i < geometry.triangle_count; ++i)
    {
        PortableMeshTriangle t;
        PortableMeshSurface s;
        if (!geometry.triangle(geometry.context, i, &t) ||
            !geometry.surface(geometry.context, t.surface, &s))
            return fail(c, NULL, i, "unsupported gravity zone or invalid mesh",
                        error, capacity);
    }
    PortableMeshItems next = {0};
    if (portable_mesh_item_inventory_init(&next.inventory, c->count, c->human_mask, seed) !=
            PORTABLE_MESH_ITEM_OK ||
        portable_mesh_item_status_init(&next.status, c->count, c->human_mask) !=
            PORTABLE_MESH_ITEM_STATUS_OK ||
        portable_mesh_item_boxes_init(&next.boxes, c->course, error, capacity) !=
            PORTABLE_MESH_ITEM_OK)
        return fail(c, NULL, UINT32_MAX, "invalid item owner/content", error, capacity);
    for (unsigned i = 0; i < c->count; ++i)
    {
        next.standings[i] = (uint8_t)i;
        if (c->mode == 2u && (c->human_mask & (1u << i)))
            clear_inventory(&next.inventory, i, 0x1008u);
    }
    *out = next;
    if (error && capacity)
        error[0] = 0;
    return PORTABLE_MESH_ITEMS_OK;
}
static void cpu_controls(PortableMeshItems *w, const PortableMeshItemsConfig *c,
                         const PortableMeshRacerState *r, unsigned i)
{
    PortableMeshItemControls *control = &w->controls[i];
    const uint8_t backward = control->aim_backward, forward = control->aim_forward;
    *control = (PortableMeshItemControls){.recovering = control->recovering};
    if (w->cpu_wait[i])
        --w->cpu_wait[i];
    int held = 0;
    for (unsigned a = 0; a < 64; ++a)
        if (w->actors[a].owner == i && w->actors[a].phase == PORTABLE_MESH_ACTOR_HELD)
            held = 1;
    if (held)
    {
        if (w->cpu_hold[i])
            --w->cpu_hold[i];
        control->item_pressed = (uint8_t)(w->cpu_hold[i] != 0);
        control->aim_backward = backward;
        control->aim_forward = forward;
        return;
    }
    if (w->previous_item[i] || w->cpu_wait[i] || !active(w, c, r, i))
        return;
    const unsigned rank = rank_of(w, i, c->count);
    int near = 0, behind = 0;
    for (unsigned j = 0; j < c->count; ++j)
        if (j != i && active(w, c, r, j))
        {
            const int64_t range = rank_of(w, j, c->count) > rank
                                      ? c->rules->cpu_backward_distance_q16
                                      : c->rules->cpu_use_distance_q16;
            if (distance2(r[i].kart.position_q16, r[j].kart.position_q16) <=
                (uint64_t)(range * range))
            {
                near = 1;
                if (rank_of(w, j, c->count) > rank)
                    behind = 1;
            }
        }
    const uint16_t item = w->inventory.racers[i].held_item;
    const int immediate = (item >= 0x1007 && item <= 0x100f) || item == PORTABLE_MESH_ITEM_BULLET ||
                          item == PORTABLE_MESH_ITEM_BLOOPER;
    if (near || immediate)
    {
        control->item_pressed = 1;
        control->aim_backward = (uint8_t)behind;
        control->aim_forward = (uint8_t)!behind;
    }
    w->cpu_wait[i] = (uint16_t)c->rules->cpu_decision_ticks;
}
PortableMeshItemsResult portable_mesh_items_prepare_tick(
    PortableMeshItems *world, const PortableMeshItemsConfig *c, PortableMeshRacerState *racers,
    const PortableMeshItemControls *controls, const uint8_t *standings,
    PortableMeshKartEffects *effects, PortableMeshItemEvents *events, char *error, size_t capacity)
{
    if (!world_valid(world, c) || !racers_valid(c, racers) || !controls || !standings || !effects ||
        !events || world->prepared || world->tick == UINT32_MAX)
        return fail(c, world, UINT32_MAX, "invalid prepare state", error, capacity);
    unsigned seen = 0;
    for (unsigned i = 0; i < c->count; ++i)
    {
        if (standings[i] >= c->count || (seen & (1u << standings[i])) ||
            controls[i].item_pressed > 1 || controls[i].aim_backward > 1 ||
            controls[i].aim_forward > 1 || controls[i].recovering > 1)
            return fail(c, world, i, "invalid controls/standings", error, capacity);
        seen |= 1u << standings[i];
    }
    PortableMeshItems next = *world;
    PortableMeshRacerState moved[8];
    memcpy(moved, racers, c->count * sizeof *racers);
    PortableMeshKartEffects dynamics[8];
    PortableMeshItemEvents emitted = {0};
    memcpy(next.standings, standings, c->count);
    memcpy(next.controls, controls, c->count * sizeof *controls);
    for (unsigned i = 0; i < c->count; ++i)
        if (c->mode != 16u && !(c->human_mask & (1u << i)))
        {
            next.controls[i].aim_backward = world->controls[i].aim_backward;
            next.controls[i].aim_forward = world->controls[i].aim_forward;
        }
    unsigned expired;
    if (portable_mesh_item_status_advance(&next.status, c->rules, &expired) !=
        PORTABLE_MESH_ITEM_STATUS_OK)
        return fail(c, world, UINT32_MAX, "status clock", error, capacity);
    for (unsigned i = 0; i < c->count; ++i)
    {
        memcpy(next.before_q16[i], racers[i].kart.position_q16, sizeof next.before_q16[i]);
        if (expired & (1u << i))
        {
            clear_inventory(&next.inventory, i, 0);
            if (!event(&emitted, PORTABLE_MESH_EVENT_GOLDEN_EXPIRED, UINT32_MAX, i, ABSENT, 0x1009))
                return budget(c, world, i, "event budget", error, capacity);
        }
        if (next.release_lock[i])
            --next.release_lock[i];
        if (c->inactive_mask & (1u << i))
            clear_inventory(&next.inventory, i, 0);
        if (!active(&next, c, moved, i))
        {
            if (portable_mesh_item_status_clear_transient(&next.status, c->rules, i) !=
                PORTABLE_MESH_ITEM_STATUS_OK)
                return fail(c, world, i, "transient reset", error, capacity);
            for (unsigned a = 0; a < 64; ++a)
                if (next.actors[a].owner == i &&
                    (next.actors[a].phase == PORTABLE_MESH_ACTOR_HELD ||
                     next.actors[a].phase == PORTABLE_MESH_ACTOR_ORBIT ||
                     next.actors[a].phase == PORTABLE_MESH_ACTOR_ORBIT_PENDING))
                    if (!remove_actor(&next, a, &emitted))
                        return budget(c, world, a, "event budget", error, capacity);
        }
        if (c->mode != 16u && !(c->human_mask & (1u << i)))
            cpu_controls(&next, c, moved, i);
    }
    /* Existing attachments release before accepting a newly pressed inventory. */
    unsigned released = 0;
    for (unsigned a = 0; a < 64; ++a)
    {
        PortableMeshItemActor *actor = &next.actors[a];
        if (!actor->phase)
            continue;
        const unsigned owner = actor->owner;
        const PortableMeshItemControls *ctl = &next.controls[owner];
        int launch = actor->phase == PORTABLE_MESH_ACTOR_HELD && !ctl->item_pressed;
        const int orbit = actor->phase == PORTABLE_MESH_ACTOR_ORBIT;
        if (orbit && !next.release_lock[owner] && !(released & (1u << owner)) &&
            ctl->item_pressed && !next.previous_item[owner])
            launch = 1;
        if (launch)
        {
            const int line = trailing(actor);
            const uint16_t place = actor->angle;
            if (!release_actor(&next, c, moved, a, orbit ? 0 : ctl->aim_backward, ctl->aim_forward,
                               &emitted))
                return fail(c, world, a, "release", error, capacity);
            released |= 1u << owner;
            /* The rest of the line closes up behind the kart. */
            for (unsigned b = 0; line && b < 64; ++b)
                if (b != a && next.actors[b].owner == owner && trailing(&next.actors[b]) &&
                    next.actors[b].angle > place)
                    --next.actors[b].angle;
            if (line)
                actor->angle = 0;
        }
    }
    PortableMeshItemInput inputs[8] = {0};
    for (unsigned i = 0; i < c->count; ++i)
    {
        unsigned attached = 0;
        for (unsigned a = 0; a < 64; ++a)
            if (next.actors[a].owner == i &&
                (next.actors[a].phase == PORTABLE_MESH_ACTOR_HELD ||
                 next.actors[a].phase == PORTABLE_MESH_ACTOR_ORBIT ||
                 next.actors[a].phase == PORTABLE_MESH_ACTOR_ORBIT_PENDING))
                ++attached;
        inputs[i] = (PortableMeshItemInput){
            .rank = (uint8_t)(rank_of(&next, i, c->count) + 1),
            .strength_threshold = INT32_MAX,
            .item_pressed = next.controls[i].item_pressed,
            .eligible = (uint8_t)(active(&next, c, moved, i) && !attached && !next.release_lock[i] &&
                                  !next.status.racers[i].spin_ticks),
            .cpu_use = next.controls[i].item_pressed};
    }
    PortableMeshItemStep step;
    if (portable_mesh_item_inventory_tick(&next.inventory, c->rules, inputs, c->count, &step) !=
        PORTABLE_MESH_ITEM_OK)
        return fail(c, world, UINT32_MAX, "inventory tick", error, capacity);
    next.inventory = step.next;
    for (unsigned u = 0; u < step.use_count; ++u)
    {
        const PortableMeshItemUse *use = &step.uses[u];
        if (next.inventory.racers[use->racer].held_item != use->item)
            continue;
        int result = use_item(&next, c, moved, use, &emitted);
        if (result < 0)
            return emitted.count == PORTABLE_MESH_ITEMS_EVENTS || next.serial == UINT32_MAX
                       ? budget(c, world, use->racer, "activation event/serial budget", error,
                                capacity)
                       : fail(c, world, use->racer, "item activation", error, capacity);
        if (result && !(c->human_mask & (1u << use->racer)))
            next.cpu_hold[use->racer] = (uint16_t)c->rules->cpu_hold_ticks;
    }
    for (unsigned i = 0; i < c->count; ++i)
    {
        const uint8_t pending = next.status.racers[i].pending_mushroom;
        if (portable_mesh_item_status_prepare_drive(
                &next.status, c->rules, i, active(&next, c, moved, i) && moved[i].kart.grounded,
                &dynamics[i]) != PORTABLE_MESH_ITEM_STATUS_OK ||
            !portable_mesh_kart_effects_valid(c->kart_rules, &dynamics[i]))
            return fail(c, world, i, "drive effects", error, capacity);
        if (pending && !next.status.racers[i].pending_mushroom &&
            !event(&emitted, PORTABLE_MESH_EVENT_BOOST, UINT32_MAX, i, ABSENT, 0x1007))
            return budget(c, world, i, "boost event budget", error, capacity);
        if (!active(&next, c, moved, i) &&
            !((c->bomb_mask & (1u << i)) && motion_active(&next, moved, i)))
            dynamics[i] = (PortableMeshKartEffects){
                .speed_ratio_q16 = Q, .body_scale_q16 = Q, .boost_cap_ratio_q16 = Q};
        next.previous_item[i] = next.controls[i].item_pressed;
    }
    next.prepared = 1;
    if (!world_valid(&next, c) || !racers_valid(c, moved))
        return fail(c, world, UINT32_MAX, "prepared output", error, capacity);
    *world = next;
    memcpy(racers, moved, c->count * sizeof *racers);
    memcpy(effects, dynamics, c->count * sizeof *effects);
    *events = emitted;
    if (error && capacity)
        error[0] = 0;
    return PORTABLE_MESH_ITEMS_OK;
}
static int contact(const int32_t abefore[3], const int32_t aafter[3], const int32_t bbefore[3],
                   const int32_t bafter[3], int32_t radius, uint64_t *fraction)
{
    int32_t before[3], after[3];
    const int32_t zero[3] = {0};
    for (unsigned j = 0; j < 3; ++j)
    {
        const int64_t a = (int64_t)abefore[j] - bbefore[j], b = (int64_t)aafter[j] - bafter[j];
        if (a < INT32_MIN || a > INT32_MAX || b < INT32_MIN || b > INT32_MAX)
            return -1;
        before[j] = (int32_t)a;
        after[j] = (int32_t)b;
    }
    const PortableMeshQueryStatus status =
        portable_mesh_query_segment_sphere(before, after, zero, radius, fraction);
    return status == PORTABLE_MESH_QUERY_INVALID ? -1 : status == PORTABLE_MESH_QUERY_HIT;
}
static int32_t racer_radius(const PortableMeshItems *w, const PortableMeshItemsConfig *c,
                            unsigned i)
{
    const PortableMeshItemRacerStatus *s = &w->status.racers[i];
    return portable_fixed_3d_mul(
        c->kart_rules->kart_radius_q16,
        s->lightning_ticks && !s->star_ticks ? c->rules->lightning_scale_ratio_q16 : Q);
}
/* Mesh gates replace source raster checkpoint ownership. Cross the same
 * authored bounded gate rectangles using the item's own centerline; retain
 * launch progress until a gate is crossed, independently of its old owner. */
static int item_checkpoint(PortableMeshItemActor *actor, const PortableCourse3DView *course,
                           const int32_t before[3])
{
    const PortableCourse3DTable *table =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('C', 'P', 'N', 'T'));
    if (!table || table->count > 1024)
        return 0;
    int64_t latest = -1;
    uint32_t checkpoint = actor->checkpoint;
    for (uint32_t i = 0; i < table->count; ++i)
    {
        PortableCourse3DCheckpoint gate;
        if (!portable_course_3d_checkpoint(course, i, &gate))
            return 0;
        int32_t a[3], b[3];
        for (unsigned j = 0; j < 3; ++j)
        {
            a[j] = before[j] - gate.center_q16[j];
            b[j] = actor->motion.position_q16[j] - gate.center_q16[j];
        }
        int64_t da = portable_fixed_3d_dot(a, gate.normal_q30) / U,
                db = portable_fixed_3d_dot(b, gate.normal_q30) / U;
        if (!((da < 0 && db >= 0) || (!(gate.flags & 4u) && da > 0 && db <= 0)))
            continue;
        const int64_t t = (-da) * U / (db - da);
        int32_t point[3], up[3];
        for (unsigned j = 0; j < 3; ++j)
        {
            point[j] = a[j] + (int32_t)(((int64_t)b[j] - a[j]) * (-da) / (db - da));
            up[j] =
                (int32_t)(((int64_t)gate.normal_q30[(j + 1) % 3] * gate.right_q30[(j + 2) % 3] -
                           (int64_t)gate.normal_q30[(j + 2) % 3] * gate.right_q30[(j + 1) % 3]) /
                          U);
        }
        const int64_t x = portable_fixed_3d_dot(point, gate.right_q30) / U,
                      z = portable_fixed_3d_dot(point, up) / U;
        if (x >= -gate.half_width_q16 && x <= gate.half_width_q16 && z >= -gate.half_height_q16 &&
            z <= gate.half_height_q16 && t > latest)
        {
            latest = t;
            checkpoint = i;
        }
    }
    actor->checkpoint = checkpoint;
    return 1;
}

static int ignite(PortableMeshItems *w, unsigned slot, PortableMeshItemEvents *e);
static int update_actor(PortableMeshItems *w, const PortableMeshItemsConfig *c,
                        PortableMeshRacerState *r, const PortableMeshCourse *terrain, unsigned slot,
                        PortableMeshItemEvents *e, const char **reason, int *exhausted,
                        PortableMeshItemMotionFault *fault)
{
    PortableMeshItemActor *a = &w->actors[slot];
    if (!a->phase)
        return 1;
    if (a->lifetime == 1)
    {
        /* A released Bob-omb goes off when its time runs out, never vanishes. */
        if (a->item == PORTABLE_MESH_ITEM_BOMB && a->phase == PORTABLE_MESH_ACTOR_FLYING)
        {
            a->lifetime = (uint16_t)(c->rules->actors_lifetime_ticks < PORTABLE_MESH_BOMB_BLAST_TICKS + 1
                                         ? c->rules->actors_lifetime_ticks : PORTABLE_MESH_BOMB_BLAST_TICKS + 1);
            return ignite(w, slot, e);
        }
        return remove_actor(w, slot, e);
    }
    --a->lifetime;
    if (a->grace)
        --a->grace;
    if (a->phase == PORTABLE_MESH_ACTOR_HAUNT)
    {
        if (a->target >= c->count || !active(w, c, r, a->target) ||
            w->status.racers[a->target].star_ticks)
            return remove_actor(w, slot, e);
        memcpy(a->motion.position_q16, r[a->target].kart.position_q16,
               sizeof a->motion.position_q16);
        if (a->delay)
            --a->delay;
        if (!a->delay && !a->haunted)
        {
            const PortableMeshItemStatusResult status = portable_mesh_item_status_activate(
                &w->status, c->rules, a->target, PORTABLE_MESH_STATUS_BOO_LOCK);
            if (status == PORTABLE_MESH_ITEM_STATUS_INELIGIBLE)
                return remove_actor(w, slot, e);
            if (status != PORTABLE_MESH_ITEM_STATUS_OK)
                return 0;
            a->haunted = 1;
            if (!actor_event(w, e, PORTABLE_MESH_EVENT_HAUNT, slot, a->owner, a->target, a->item))
                return 0;
        }
        else if (a->haunted && !w->status.racers[a->target].boo_lock_ticks)
            return remove_actor(w, slot, e);
        return 1;
    }
    if (a->phase == PORTABLE_MESH_ACTOR_HELD || a->phase == PORTABLE_MESH_ACTOR_ORBIT_PENDING ||
        a->phase == PORTABLE_MESH_ACTOR_ORBIT)
    {
        if (!active(w, c, r, a->owner))
            return remove_actor(w, slot, e);
        if (a->phase == PORTABLE_MESH_ACTOR_ORBIT_PENDING)
        {
            if (a->delay)
                --a->delay;
            else
            {
                a->phase = PORTABLE_MESH_ACTOR_ORBIT;
                if (!actor_event(w, e, PORTABLE_MESH_EVENT_ORBIT_READY, slot, a->owner, a->target,
                                 a->item))
                    return 0;
            }
        }
        if (a->phase == PORTABLE_MESH_ACTOR_ORBIT && !trailing(a))
            a->angle =
                (uint16_t)(a->angle + portable_fixed_3d_dt(c->rules->actors_orbit_turn_rate_q16));
        return attach(a, &r[a->owner], c->rules);
    }
    if (a->phase == PORTABLE_MESH_ACTOR_BLAST)
    {
        /* The blast stays where it went off until it fades. */
        if (a->delay <= 1)
            return remove_actor(w, slot, e);
        --a->delay;
        return 1;
    }
    if (a->item == PORTABLE_MESH_ITEM_BOMB && a->phase == PORTABLE_MESH_ACTOR_FLYING)
    {
        if (a->delay > 1)
            --a->delay;
        else
            return ignite(w, slot, e);
    }
    if (a->phase == PORTABLE_MESH_ACTOR_TRAP_ARMING)
    {
        if (a->delay)
            --a->delay;
        if (!a->delay)
        {
            a->phase = PORTABLE_MESH_ACTOR_TRAP_WAITING;
            a->delay = (uint16_t)c->rules->shell_trap_lifetime_ticks;
            /* Arm in place horizontally; airborne traps still fall onto the
             * actual support surface instead of acquiring an imaginary floor. */
            a->motion.velocity_q16[0] = a->motion.velocity_q16[1] = 0;
        }
    }
    else if (a->phase == PORTABLE_MESH_ACTOR_TRAP_WAITING)
    {
        if (a->delay)
            --a->delay;
        if (!a->delay)
            return remove_actor(w, slot, e);
        unsigned target = ABSENT;
        for (unsigned i = 0; i < c->count; ++i)
            if ((active(w,c,r,i) || ((c->bomb_mask & (1u<<i)) &&
                 motion_active(w,r,i) && !bomb_contacted(e,i))) &&
                !w->status.racers[i].boo_invisible_ticks &&
                (!a->grace || i != a->owner) && r[i].checkpoint == a->checkpoint &&
                distance2(a->motion.position_q16, r[i].kart.position_q16) <=
                    (uint64_t)((int64_t)c->rules->shell_homing_range_q16 *
                               c->rules->shell_homing_range_q16))
                target = i;
        if (a->item == 0x1004)
            target = w->standings[0];
        if (target != ABSENT)
        {
            a->target = (uint8_t)target;
            a->phase = PORTABLE_MESH_ACTOR_TRAP_LOCKING;
            a->delay = (uint16_t)c->rules->shell_trap_lock_ticks;
        }
    }
    else if (a->phase == PORTABLE_MESH_ACTOR_TRAP_LOCKING)
    {
        if (a->delay)
            --a->delay;
        if (!a->delay)
            a->phase = PORTABLE_MESH_ACTOR_FLYING;
    }
    if (a->phase == PORTABLE_MESH_ACTOR_TRAP_WAITING ||
        a->phase == PORTABLE_MESH_ACTOR_TRAP_LOCKING)
        a->motion.velocity_q16[0] = a->motion.velocity_q16[1] = 0;
    if ((a->item == 0x1002 || a->item == 0x1004) && a->phase == PORTABLE_MESH_ACTOR_FLYING)
    {
        if (a->item == 0x1004 && a->target != ABSENT)
            a->target = w->standings[0];
        if (a->target != ABSENT && w->status.racers[a->target].boo_invisible_ticks)
            a->target = ABSENT;
        int32_t velocity[3];
        PortableMeshItemTargetCursor cursor;
        const PortableMeshItemTargetStatus status = portable_mesh_item_target_steer_geometry(
            &cursor, &a->route, c->course, c->rules, &a->motion,
            a->target == ABSENT ? NULL : r[a->target].kart.position_q16,
            a->target != ABSENT && r[a->target].checkpoint == a->checkpoint, velocity, c->geometry);
        if (status == PORTABLE_MESH_ITEM_TARGET_INVALID ||
            status == PORTABLE_MESH_ITEM_TARGET_BUDGET)
        {
            *reason = portable_mesh_item_target_status_name(status);
            *exhausted = status == PORTABLE_MESH_ITEM_TARGET_BUDGET;
            return 0;
        }
        if (status == PORTABLE_MESH_ITEM_TARGET_OK)
        {
            a->route = cursor;
            memcpy(a->motion.velocity_q16, velocity, sizeof velocity);
        }
        /* Valid missing/terminal routes retain the ballistic velocity. */
    }
    int32_t previous[3];
    memcpy(previous, a->motion.position_q16, sizeof previous);
    const uint32_t previous_support = a->motion.contact_triangle;
    PortableMeshItemMotionEvents motion_events;
    const PortableMeshItemMotionStatus status = portable_mesh_item_motion_tick_diagnostic(
        &a->motion, c->rules, terrain, c->kart_rules->ground_normal_cutoff_q30, &motion_events, fault);
    if (status == PORTABLE_MESH_ITEM_MOTION_BOUNCE_LIMIT)
    {
        if (!actor_event(w, e, PORTABLE_MESH_EVENT_IMPACT, slot, a->owner, a->target, a->item))
            return 0;
        return remove_actor(w, slot, e);
    }
    if (status == PORTABLE_MESH_ITEM_MOTION_OUTSIDE)
        return remove_actor(w, slot, e);
    if (status != PORTABLE_MESH_ITEM_MOTION_OK)
    {
        *reason = portable_mesh_item_motion_status_name(status);
        *exhausted = status == PORTABLE_MESH_ITEM_MOTION_COLLISION_BUDGET;
        return 0;
    }
    if (motion_events.wall_impacts &&
        !actor_event(w, e, PORTABLE_MESH_EVENT_IMPACT, slot, a->owner, a->target, a->item))
        return 0;
    if (dropped_item(a->item) && previous_support == UINT32_MAX && a->motion.grounded &&
        motion_events.resting &&
        !actor_event(w, e, PORTABLE_MESH_EVENT_LANDED, slot, a->owner, a->target, a->item))
        return 0;
    if (!item_checkpoint(a, c->course, previous))
    {
        *reason = "invalid item gate progress";
        return 0;
    }
    return 1;
}
/* A Bob-omb blast hits every racer inside its radius once, its thrower too. */
static int blast(PortableMeshItems *w, const PortableMeshItemsConfig *c, PortableMeshRacerState *r,
                 unsigned slot, PortableMeshItemEvents *e)
{
    PortableMeshItemActor *a = &w->actors[slot];
    for (unsigned i = 0; i < c->count; ++i)
    {
        if ((a->struck & (1u << i)) || !active(w, c, r, i))
            continue;
        const int64_t reach = (int64_t)PORTABLE_MESH_BOMB_BLAST_RADIUS_Q16 + racer_radius(w, c, i);
        if (distance2(a->motion.position_q16, r[i].kart.position_q16) > (uint64_t)(reach * reach))
            continue;
        a->struck |= (uint8_t)(1u << i);
        if (!apply_hit(w, c, r, i, a->owner, slot, a->item, e))
            return 0;
    }
    return 1;
}
static int ignite(PortableMeshItems *w, unsigned slot, PortableMeshItemEvents *e)
{
    PortableMeshItemActor *a = &w->actors[slot];
    a->phase = PORTABLE_MESH_ACTOR_BLAST;
    a->delay = PORTABLE_MESH_BOMB_BLAST_TICKS;
    a->grace = 0;
    memset(a->motion.velocity_q16, 0, sizeof a->motion.velocity_q16);
    return actor_event(w, e, PORTABLE_MESH_EVENT_IMPACT, slot, a->owner, a->target, a->item);
}
PortableMeshItemsResult portable_mesh_items_resolve_after_move(PortableMeshItems *world,
                                                               const PortableMeshItemsConfig *c,
                                                               PortableMeshRacerState *racers,
                                                               PortableMeshItemEvents *events,
                                                               char *error, size_t capacity)
{
    if (!world_valid(world, c) || !world->prepared || !racers_valid(c, racers) || !events ||
        events->count > PORTABLE_MESH_ITEMS_EVENTS)
        return fail(c, world, UINT32_MAX, "invalid resolve state", error, capacity);
    PortableMeshCourse terrain;
    if (!mesh(c, &terrain))
        return fail(c, world, UINT32_MAX, "invalid collision mesh", error, capacity);
    PortableMeshItems next = *world;
    PortableMeshRacerState moved[8];
    memcpy(moved, racers, c->count * sizeof *racers);
    PortableMeshItemEvents emitted = *events;
    int32_t before[64][3];
    unsigned order[64], count = 0;
    for (unsigned a = 0; a < 64; ++a)
        if (next.actors[a].phase)
        {
            memcpy(before[a], next.actors[a].motion.position_q16, sizeof before[a]);
            unsigned at = count;
            while (at && next.actors[order[at - 1]].serial > next.actors[a].serial)
            {
                order[at] = order[at - 1];
                --at;
            }
            order[at] = a;
            ++count;
        }
    for (unsigned turn = 0; turn < count; ++turn)
    {
        unsigned a = order[turn];
        const char *reason = "invalid actor attachment/event state";
        int exhausted = 0;
        PortableMeshItemMotionFault fault = {0};
        if (!update_actor(&next, c, moved, &terrain, a, &emitted, &reason, &exhausted, &fault))
        {
            char detail[512];
            if (fault.phase != PORTABLE_MESH_ITEM_FAULT_NONE)
            {
                snprintf(detail, sizeof detail,
                    "COLLISION_BUDGET %s%u p=%d,%d,%d v=%d,%d,%d r=%d tri=%u n=%d,%d,%d f=%llu rem=%d,%d,%d",
                    portable_mesh_item_motion_fault_phase_name(fault.phase), fault.iteration,
                    fault.position_q16[0], fault.position_q16[1], fault.position_q16[2],
                    fault.velocity_q16[0], fault.velocity_q16[1], fault.velocity_q16[2],
                    fault.radius_q16, fault.triangle, fault.normal_q30[0], fault.normal_q30[1],
                    fault.normal_q30[2], (unsigned long long)fault.fraction_q32,
                    fault.remaining_q16[0], fault.remaining_q16[1], fault.remaining_q16[2]);
                reason = detail;
            }
            return exhausted || emitted.count == PORTABLE_MESH_ITEMS_EVENTS
                       ? budget(c, world, a, reason, error, capacity)
                       : fail(c, world, a, reason, error, capacity);
        }
    }
    /* Actor contacts precede kart contacts so dragged/orbiting items defend.
     * Stable creation order resolves simultaneous destructible actor pairs. */
    for (unsigned x = 0; x < count; ++x)
        for (unsigned y = x + 1; y < count; ++y)
        {
            unsigned a = order[x], b = order[y];
            PortableMeshItemActor *first = &next.actors[a], *second = &next.actors[b];
            if (!first->phase || !second->phase || first->owner == second->owner ||
                first->phase == PORTABLE_MESH_ACTOR_HAUNT ||
                second->phase == PORTABLE_MESH_ACTOR_HAUNT ||
                first->phase == PORTABLE_MESH_ACTOR_BLAST || second->phase == PORTABLE_MESH_ACTOR_BLAST ||
                first->phase == PORTABLE_MESH_ACTOR_ORBIT_PENDING ||
                second->phase == PORTABLE_MESH_ACTOR_ORBIT_PENDING)
                continue;
            uint64_t fraction;
            int hit =
                contact(before[a], first->motion.position_q16, before[b],
                        second->motion.position_q16, c->rules->actors_radius_q16 * 2, &fraction);
            if (hit < 0)
                return fail(c, world, a, "actor pair query", error, capacity);
            if (hit && (!actor_event(&next, &emitted, PORTABLE_MESH_EVENT_IMPACT, a, first->owner,
                                     second->owner, first->item) ||
                        !remove_actor(&next, a, &emitted) || !remove_actor(&next, b, &emitted)))
                return budget(c, world, a, "event budget", error, capacity);
        }
    for (unsigned turn = 0; turn < count; ++turn)
    {
        const unsigned slot = order[turn];
        PortableMeshItemActor *a = &next.actors[slot];
        if (!a->phase || a->phase == PORTABLE_MESH_ACTOR_HAUNT ||
            a->phase == PORTABLE_MESH_ACTOR_ORBIT_PENDING)
            continue;
        if (a->phase == PORTABLE_MESH_ACTOR_BLAST)
        {
            if (!blast(&next, c, moved, slot, &emitted))
                return fail(c, world, slot, "Bob-omb blast", error, capacity);
            continue;
        }
        unsigned hit_mask = 0;
        for (unsigned attempt = 0; attempt < c->count && a->phase; ++attempt)
        {
            unsigned victim = ABSENT;
            uint64_t earliest = 0;
            for (unsigned i = 0; i < c->count; ++i)
            {
                if ((hit_mask & (1u << i)) ||
                    (!active(&next, c, moved, i) &&
                     !((c->bomb_mask & (1u << i)) && motion_active(&next, moved, i))) ||
                    ((c->bomb_mask & (1u << i)) &&
                     (bomb_contacted(&emitted,i) || next.status.racers[i].hit_grace_ticks)) ||
                    (i == a->owner && (a->grace || a->phase == PORTABLE_MESH_ACTOR_HELD ||
                                       a->phase == PORTABLE_MESH_ACTOR_ORBIT)) ||
                    next.status.racers[i].boo_invisible_ticks)
                    continue;
                uint64_t fraction;
                int hit =
                    contact(before[slot], a->motion.position_q16, next.before_q16[i],
                            moved[i].kart.position_q16,
                            c->rules->actors_radius_q16 + racer_radius(&next, c, i), &fraction);
                if (hit < 0)
                    return fail(c, world, slot, "actor/racer query", error, capacity);
                if (hit && (victim == ABSENT || fraction < earliest))
                {
                    victim = i;
                    earliest = fraction;
                }
            }
            if (victim == ABSENT)
                break;
            hit_mask |= 1u << victim;
            if (a->item == PORTABLE_MESH_ITEM_BOMB)
            {
                /* Touching a Bob-omb sets it off where it is. */
                if (!ignite(&next, slot, &emitted) || !blast(&next, c, moved, slot, &emitted))
                    return fail(c, world, slot, "Bob-omb contact", error, capacity);
                break;
            }
            if (c->bomb_mask & (1u << victim))
            {
                const unsigned kind=next.status.racers[victim].star_ticks
                    ? PORTABLE_MESH_EVENT_PROTECTED : PORTABLE_MESH_EVENT_BOMB_CONTACT;
                if(!actor_event(&next,&emitted,kind,slot,a->owner,victim,a->item))
                    return budget(c,world,slot,"Bomb contact event budget",error,capacity);
            }
            else if (!apply_hit(&next, c, moved, victim, a->owner, slot, a->item, &emitted))
                return fail(c, world, slot, "racer hit", error, capacity);
            const int keep = a->item == 0x1004 && a->target != ABSENT &&
                             victim != next.standings[0] && !racer_protected(&next, victim);
            if (!keep && !remove_actor(&next, slot, &emitted))
                return budget(c, world, slot, "event budget", error, capacity);
        }
    }
    /* Time Trial owns its initial three mushrooms; authored ITEM placements
     * remain preview-only and cannot replenish stock or consume roulette RNG. */
    if (c->mode != 2u)
    {
        PortableMeshItemBoxSample samples[8];
        for (unsigned i = 0; i < c->count; ++i)
        {
            memcpy(samples[i].before_q16, next.before_q16[i], sizeof samples[i].before_q16);
            memcpy(samples[i].after_q16, moved[i].kart.position_q16, sizeof samples[i].after_q16);
            samples[i].collision_radius_q16 = racer_radius(&next, c, i);
            samples[i].eligible = (uint8_t)active(&next, c, moved, i);
        }
        PortableMeshItemBoxEvents pickups;
        if (portable_mesh_item_boxes_tick(&next.boxes, &next.inventory, c->course, c->rules,
                                          samples, c->count, &pickups, error,
                                          capacity) != PORTABLE_MESH_ITEM_OK)
            return fail(c, world, UINT32_MAX, "box collection", error, capacity);
        for (unsigned i = 0; i < pickups.count; ++i)
        {
            const PortableMeshItemBoxPickup *p = &pickups.pickups[i];
            if (!event(&emitted, PORTABLE_MESH_EVENT_PICKUP, UINT32_MAX, p->racer, ABSENT, 0))
                return budget(c, world, UINT32_MAX, "event budget", error, capacity);
            emitted.values[emitted.count - 1].placement = p->placement;
            emitted.values[emitted.count - 1].fraction_q32 = p->fraction_q32;
        }
    }
    next.prepared = 0;
    ++next.tick;
    if (!world_valid(&next, c) || !racers_valid(c, moved))
        return fail(c, world, UINT32_MAX, "resolved output", error, capacity);
    *world = next;
    memcpy(racers, moved, c->count * sizeof *racers);
    *events = emitted;
    if (error && capacity)
        error[0] = 0;
    return PORTABLE_MESH_ITEMS_OK;
}
PortableMeshItemsResult portable_mesh_items_clear_inactive(PortableMeshItems *world,
    const PortableMeshItemsConfig *c, PortableMeshItemEvents *events, char *error, size_t capacity)
{
    if(!world_valid(world,c) || world->prepared || !events ||
       events->count>PORTABLE_MESH_ITEMS_EVENTS)
        return fail(c,world,UINT32_MAX,"invalid inactive cleanup",error,capacity);
    PortableMeshItems next=*world;
    PortableMeshItemEvents emitted=*events;
    for(unsigned i=0;i<c->count;++i)
        if(c->inactive_mask & (1u<<i))
        {
            clear_inventory(&next.inventory,i,0);
            if(portable_mesh_item_status_clear_transient(&next.status,c->rules,i)!=
               PORTABLE_MESH_ITEM_STATUS_OK)
                return fail(c,world,i,"inactive transient reset",error,capacity);
        }
    for(unsigned slot=0;slot<PORTABLE_MESH_ITEMS_ACTORS;++slot)
    {
        const PortableMeshItemActor *a=&next.actors[slot];
        if((a->phase==PORTABLE_MESH_ACTOR_HELD || a->phase==PORTABLE_MESH_ACTOR_ORBIT ||
            a->phase==PORTABLE_MESH_ACTOR_ORBIT_PENDING) &&
           (c->inactive_mask & (1u<<a->owner)))
            if(!remove_actor(&next,slot,&emitted))
                return budget(c,world,slot,"inactive cleanup event budget",error,capacity);
    }
    if(!world_valid(&next,c))
        return fail(c,world,UINT32_MAX,"inactive cleanup output",error,capacity);
    *world=next;*events=emitted;
    if(error && capacity)error[0]=0;
    return PORTABLE_MESH_ITEMS_OK;
}
PortableMeshItemsResult portable_mesh_items_hud(const PortableMeshItems *world, unsigned racer,
                                                uint16_t *out)
{
    return world && portable_mesh_item_inventory_hud(&world->inventory, racer, out) ==
                        PORTABLE_MESH_ITEM_OK
               ? PORTABLE_MESH_ITEMS_OK
               : PORTABLE_MESH_ITEMS_INVALID;
}
size_t portable_mesh_items_actor_poses(const PortableMeshItems *world, PortableMeshItemPose *out,
                                       size_t capacity)
{
    if (!world || (!out && capacity))
        return 0;
    size_t count = 0;
    for (unsigned i = 0; i < 64; ++i)
    {
        const PortableMeshItemActor *a = &world->actors[i];
        if (!a->phase)
            continue;
        if (count < capacity)
        {
            PortableMeshItemPose p = {.serial = a->serial,
                                      .item = a->item,
                                      .angle = a->angle,
                                      .delay = a->delay,
                                      .owner = a->owner,
                                      .phase = a->phase};
            memcpy(p.position_q16, a->motion.position_q16, sizeof p.position_q16);
            memcpy(p.velocity_q16, a->motion.velocity_q16, sizeof p.velocity_q16);
            out[count] = p;
        }
        ++count;
    }
    return count;
}

PortableMeshItemsResult portable_mesh_items_pair_observed(PortableMeshItems *world,
                                                 const PortableMeshItemsConfig *c,
                                                 PortableMeshRacerState *racers, unsigned first,
                                                 unsigned second, PortableMeshItemEvents *events,
                                                 uint8_t *ordinary_mask, char *error, size_t capacity)
{
    if (!world_valid(world, c) || !world->prepared || !racers_valid(c, racers) || !events ||
        events->count > PORTABLE_MESH_ITEMS_EVENTS || first >= second || second >= c->count)
        return fail(c, world, UINT32_MAX, "invalid pair", error, capacity);
    if (!motion_active(world, racers, first) || !motion_active(world, racers, second) ||
        ((c->inactive_mask & (1u<<first)) && !(c->bomb_mask & (1u<<first))) ||
        ((c->inactive_mask & (1u<<second)) && !(c->bomb_mask & (1u<<second))) ||
        ((c->bomb_mask & (1u<<first)) && bomb_contacted(events,first)) ||
        ((c->bomb_mask & (1u<<second)) && bomb_contacted(events,second)) ||
        world->status.racers[first].boo_invisible_ticks ||
        world->status.racers[second].boo_invisible_ticks)
    {
        if (ordinary_mask) *ordinary_mask = 0;
        return PORTABLE_MESH_ITEMS_OK;
    }
    uint8_t observed = 0;
    PortableMeshItems next = *world;
    PortableMeshRacerState moved[8];
    memcpy(moved, racers, c->count * sizeof *racers);
    PortableMeshItemEvents emitted = *events;
    const int32_t radius = racer_radius(&next, c, first) + racer_radius(&next, c, second);
    const uint64_t squared = distance2(moved[first].kart.position_q16, moved[second].kart.position_q16);
    if (squared > (uint64_t)((int64_t)radius * radius))
    {
        if (ordinary_mask) *ordinary_mask = 0;
        return PORTABLE_MESH_ITEMS_OK;
    }
    const int a_star = racer_protected(&next, first), b_star = racer_protected(&next, second);
    if(c->bomb_mask & ((1u<<first)|(1u<<second)))
    {
        if(next.status.racers[first].hit_grace_ticks ||
           next.status.racers[second].hit_grace_ticks)
        {
            if (ordinary_mask) *ordinary_mask = 0;
            return PORTABLE_MESH_ITEMS_OK;
        }
        const unsigned pair[2]={first,second};
        for(unsigned i=0;i<2;++i)
            if(c->bomb_mask & (1u<<pair[i]))
                if(!event(&emitted,PORTABLE_MESH_EVENT_BOMB_CONTACT,UINT32_MAX,
                          pair[1u-i],pair[i],0))
                    return budget(c,world,UINT32_MAX,"Bomb pair event budget",error,capacity);
        if(!(c->bomb_mask & (1u<<first)) &&
           !apply_hit(&next,c,moved,first,second,UINT32_MAX,0,&emitted))
            return fail(c,world,UINT32_MAX,"Bomb victim hit",error,capacity);
        if(!(c->bomb_mask & (1u<<second)) &&
           !apply_hit(&next,c,moved,second,first,UINT32_MAX,0,&emitted))
            return fail(c,world,UINT32_MAX,"Bomb victim hit",error,capacity);
    }
    else if (a_star != b_star)
    {
        if (!apply_hit(&next, c, moved, a_star ? second : first, a_star ? first : second,
                       UINT32_MAX, 0x100a, &emitted))
            return fail(c, world, UINT32_MAX, "Star pair hit", error, capacity);
    }
    else
    {
        const int a_small = next.status.racers[first].lightning_ticks && !a_star;
        const int b_small = next.status.racers[second].lightning_ticks && !b_star;
        if (a_small != b_small &&
            !apply_hit(&next, c, moved, a_small ? first : second, a_small ? second : first,
                       UINT32_MAX, 0x100c, &emitted))
            return fail(c, world, UINT32_MAX, "small pair hit", error, capacity);
        if (a_small == b_small)
        {
            PortableMeshKartEffects a, b;
            PortableMeshItemStatusOwner snapshot = next.status;
            if (portable_mesh_item_status_prepare_drive(&snapshot, c->rules, first, 0, &a) !=
                    PORTABLE_MESH_ITEM_STATUS_OK ||
                portable_mesh_item_status_prepare_drive(&snapshot, c->rules, second, 0, &b) !=
                    PORTABLE_MESH_ITEM_STATUS_OK ||
                portable_mesh_karts_contact_effects(&moved[first].kart, c->kart_rules, &a,
                                                    &moved[second].kart, c->kart_rules,
                                                    &b) != PORTABLE_MESH_OK)
                return fail(c, world, UINT32_MAX, "physical pair", error, capacity);
            /* Core physical contact excludes exact tangency. Retain the
             * legacy inclusive damage checks, observing actual overlap only. */
            if (!a_star && !b_star && squared < (uint64_t)((int64_t)radius * radius))
            {
                if (next.status.racers[first].hit_grace_ticks <= 1)
                    observed |= (uint8_t)(1u << first);
                if (next.status.racers[second].hit_grace_ticks <= 1)
                    observed |= (uint8_t)(1u << second);
            }
        }
    }
    if (!world_valid(&next, c) || !racers_valid(c, moved))
        return fail(c, world, UINT32_MAX, "pair output", error, capacity);
    *world = next;
    memcpy(racers, moved, c->count * sizeof *racers);
    *events = emitted;
    if (ordinary_mask) *ordinary_mask = observed;
    if (error && capacity)
        error[0] = 0;
    return PORTABLE_MESH_ITEMS_OK;
}

PortableMeshItemsResult portable_mesh_items_pair(PortableMeshItems *world,
    const PortableMeshItemsConfig *config, PortableMeshRacerState *racers,
    unsigned first, unsigned second, PortableMeshItemEvents *events,
    char *error, size_t capacity)
{
    return portable_mesh_items_pair_observed(world, config, racers, first, second,
                                             events, NULL, error, capacity);
}
