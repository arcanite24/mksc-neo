#include "mesh_course_coin_turn.h"
#include <stdio.h>
#include <string.h>

static int fail(const PortableMeshCourseCoinsConfig *config, char *error, size_t cap,
                const char *reason)
{
    if (error && cap)
        snprintf(error, cap, "3D course=%u coins: %s", config ? config->course_id : 0u, reason);
    return 0;
}
static int add(uint8_t *value)
{
    if (*value == UINT8_MAX)
        return 0;
    ++*value;
    return 1;
}
int portable_mesh_course_coin_turn(
    const PortableMeshCourseCoinsConfig *config, PortableMeshCourseCoins *world, uint32_t mode,
    const PortableMeshKartRules *rules, const int32_t body_scale[8],
    const PortableMeshRacerState *before, const PortableMeshRacerState *after, size_t count,
    const PortableMeshItems *before_items, const PortableMeshItems *after_items,
    const PortableMeshCourseActorEvents *actors, const PortableMeshItemEvents *items,
    uint8_t ordinary_contact, PortableMeshCourseCoinEvents *events, char *error, size_t cap)
{
    if (!portable_mesh_course_coins_config_valid(config) || !world || !before || !after ||
        !before_items || !after_items || !actors || !items || !events || !count || count > 8 ||
        before_items->status.count != count || after_items->status.count != count ||
        before_items->inventory.count != count || after_items->inventory.count != count ||
        before_items->prepared || after_items->prepared ||
        items->count > PORTABLE_MESH_ITEMS_EVENTS ||
        ((ordinary_contact | actors->hit_mask | actors->thwomp_crush_mask |
          actors->teepee_clock_mask | actors->teepee_assigned_mask |
          actors->source_haunt_clock_mask | actors->source_haunt_assigned_mask) >> count))
        return fail(config, error, cap, "invalid accepted turn");
    PortableMeshCourseCoinRequests requests = {0};
    uint8_t eligible = 0, fresh_haunt = actors->teepee_assigned_mask |
        actors->source_haunt_assigned_mask, item_hit = 0;
    for (size_t n = 0; n < items->count; ++n)
    {
        const PortableMeshItemEvent *event = &items->values[n];
        if (event->kind > PORTABLE_MESH_EVENT_BOMB_CONTACT || event->racer >= count ||
            (event->target != UINT8_MAX && event->target >= count))
            return fail(config, error, cap, "invalid item outcome");
        if ((event->kind == PORTABLE_MESH_EVENT_HIT || event->kind == PORTABLE_MESH_EVENT_HAUNT) &&
            (event->target >= count || before[event->target].phase != PORTABLE_MESH_RACER_RACING))
            return fail(config, error, cap, "item outcome targets inactive racer");
        if (event->kind == PORTABLE_MESH_EVENT_HIT)
        {
            item_hit |= (uint8_t)(1u << event->target);
            if (event->target >= count ||
                !add(&requests.post[PORTABLE_MESH_COIN_HIT][event->target]))
                return fail(config, error, cap, "item damage request overflow");
        }
        if (event->kind == PORTABLE_MESH_EVENT_HAUNT)
        {
            if (event->target >= count)
                return fail(config, error, cap, "invalid haunt victim");
            fresh_haunt |= (uint8_t)(1u << event->target);
        }
    }
    for (unsigned i = 0; i < count; ++i)
    {
        const unsigned bit = 1u << i;
        if (before[i].phase != PORTABLE_MESH_RACER_RACING)
            continue;
        /* A real manual or automatic entry is one recovery; a held rescue
         * button and the following recovery turns never create extra losses. */
        if (!before[i].kart.respawn_ticks && after[i].kart.respawn_ticks)
            requests.pre[PORTABLE_MESH_COIN_RESCUE][i] = 1;
        if (ordinary_contact & bit)
            requests.pre[PORTABLE_MESH_COIN_CONTACT][i] = 1;
        if (actors->thwomp_crush_mask & bit)
            requests.pre[PORTABLE_MESH_COIN_CRUSH][i] = 1;
        else if (actors->hit_mask & bit)
            requests.pre[PORTABLE_MESH_COIN_HIT][i] = 1;
        const PortableMeshItemRacerStatus *status = &after_items->status.racers[i];
        /* Course/pair outcomes precede the pickup; new item damage follows
         * it. A newly admitted item spin must not retroactively remove an
         * earlier pickup, while pre-existing spin and source hits still do. */
        if (!after_items->controls[i].recovering && !before_items->status.racers[i].spin_ticks &&
            !((actors->hit_mask | actors->thwomp_crush_mask) & bit) &&
            (!status->spin_ticks || (item_hit & bit)) &&
            after[i].phase == PORTABLE_MESH_RACER_RACING)
            eligible |= (uint8_t)bit;
        /* MIR advances clocks before driving, but admits a new haunt after
         * movement. Its first driver-owned parity pulse belongs to the next
         * turn; counting the fresh value now would repeat it next turn. */
        const unsigned lock = (actors->source_haunt_clock_mask & bit) ? actors->source_haunt_lock_clock[i]
            : (actors->teepee_clock_mask & bit) ? actors->teepee_lock_clock[i]
            : before_items->status.racers[i].boo_lock_ticks;
        if (lock > 32767u) return fail(config, error, cap, "invalid source lock clock");
        if (!(fresh_haunt & bit) && lock && !(lock & 63u) && status->boo_lock_ticks &&
            !status->star_ticks && !after_items->controls[i].recovering &&
            !after[i].kart.respawn_ticks && after[i].phase == PORTABLE_MESH_RACER_RACING)
            requests.post[PORTABLE_MESH_COIN_HAUNT][i] = 1;
    }
    return portable_mesh_course_coins_resolve(config, world, mode, rules, body_scale, before, after,
                                              count, eligible, &requests, events, error, cap);
}
