#include "mesh_item_status.h"

static int valid(const PortableMeshItemStatusOwner *owner, const PortableMeshItemRules *rules)
{
    if (!owner || !portable_mesh_item_rules_valid(rules) || !owner->count ||
        owner->count > PORTABLE_MESH_ITEM_RACERS || (owner->human_mask >> owner->count))
        return 0;
    uint16_t earliest[PORTABLE_MESH_ITEM_RACERS] = {0};
    for (unsigned i = 0; i < PORTABLE_MESH_ITEM_STATUS_AUXILIARIES; ++i)
    {
        const PortableMeshItemStatusAuxiliary *a = &owner->auxiliaries[i];
        if (!a->ticks)
        {
            if (a->owner)
                return 0;
            continue;
        }
        if (a->owner >= owner->count || a->ticks > rules->mushroom_golden_ticks)
            return 0;
        if (!earliest[a->owner] || a->ticks < earliest[a->owner])
            earliest[a->owner] = a->ticks;
    }
    for (unsigned i = 0; i < PORTABLE_MESH_ITEM_RACERS; ++i)
    {
        const PortableMeshItemRacerStatus *r = &owner->racers[i];
        if (r->golden_ticks != earliest[i] || r->pending_mushroom > 1u ||
            r->mushroom_ticks > rules->mushroom_boost_ticks ||
            r->golden_ticks > rules->mushroom_golden_ticks ||
            r->star_ticks > ((owner->human_mask & (1u << i)) ? rules->star_player_ticks
                                                             : rules->star_cpu_ticks) ||
            r->lightning_ticks > (rules->lightning_duration_ticks > PORTABLE_MESH_SOURCE_CLOUD_LIGHTNING_TICKS
                ? rules->lightning_duration_ticks : PORTABLE_MESH_SOURCE_CLOUD_LIGHTNING_TICKS) ||
            r->spin_ticks > rules->hit_spin_ticks ||
            r->hit_grace_ticks > rules->hit_invulnerability_ticks ||
            r->boo_invisible_ticks > rules->boo_invisibility_ticks ||
            r->boo_lock_ticks > 32767u || r->bullet_ticks > PORTABLE_MESH_BULLET_TICKS ||
            r->ink_ticks > PORTABLE_MESH_INK_TICKS)
            return 0;
        if (i >= owner->count &&
            (r->pending_mushroom || r->mushroom_ticks || r->golden_ticks || r->star_ticks ||
             r->lightning_ticks || r->spin_ticks || r->hit_grace_ticks || r->boo_invisible_ticks ||
             r->boo_lock_ticks || r->bullet_ticks || r->ink_ticks))
            return 0;
    }
    return 1;
}

PortableMeshItemStatusResult portable_mesh_item_status_init(PortableMeshItemStatusOwner *out,
                                                            unsigned count, unsigned human_mask)
{
    if (!out || !count || count > PORTABLE_MESH_ITEM_RACERS || (human_mask >> count))
        return PORTABLE_MESH_ITEM_STATUS_INVALID;
    const PortableMeshItemStatusOwner next = {.count = (uint8_t)count,
                                              .human_mask = (uint8_t)human_mask};
    *out = next;
    return PORTABLE_MESH_ITEM_STATUS_OK;
}

PortableMeshItemStatusResult portable_mesh_item_status_source_haunt_lock(
    PortableMeshItemStatusOwner *owner, const PortableMeshItemRules *rules,
    unsigned racer, uint16_t duration)
{
    if (!valid(owner, rules) || racer >= owner->count || !duration || duration > 32767u)
        return PORTABLE_MESH_ITEM_STATUS_INVALID;
    if (owner->racers[racer].star_ticks)
        return PORTABLE_MESH_ITEM_STATUS_INELIGIBLE;
    PortableMeshItemStatusOwner next = *owner;
    next.racers[racer].boo_lock_ticks = duration;
    *owner = next;
    return PORTABLE_MESH_ITEM_STATUS_OK;
}

static void decrement(uint16_t *timer)
{
    if (*timer)
        --*timer;
}

static void derive_golden(PortableMeshItemStatusOwner *owner)
{
    for (unsigned i = 0; i < owner->count; ++i)
        owner->racers[i].golden_ticks = 0;
    for (unsigned i = 0; i < PORTABLE_MESH_ITEM_STATUS_AUXILIARIES; ++i)
    {
        const PortableMeshItemStatusAuxiliary *a = &owner->auxiliaries[i];
        if (!a->ticks)
            continue;
        uint16_t *timer = &owner->racers[a->owner].golden_ticks;
        if (!*timer || a->ticks < *timer)
            *timer = a->ticks;
    }
}

PortableMeshItemStatusResult portable_mesh_item_status_advance(PortableMeshItemStatusOwner *owner,
                                                               const PortableMeshItemRules *rules,
                                                               unsigned *golden_expired_mask)
{
    if (!golden_expired_mask || !valid(owner, rules))
        return PORTABLE_MESH_ITEM_STATUS_INVALID;
    PortableMeshItemStatusOwner next = *owner;
    unsigned expired = 0;
    for (unsigned i = 0; i < PORTABLE_MESH_ITEM_STATUS_AUXILIARIES; ++i)
    {
        PortableMeshItemStatusAuxiliary *a = &next.auxiliaries[i];
        if (!a->ticks)
            continue;
        if (a->ticks == 1u)
            expired |= 1u << a->owner;
        decrement(&a->ticks);
        if (!a->ticks)
            a->owner = 0;
    }
    derive_golden(&next);
    for (unsigned i = 0; i < next.count; ++i)
    {
        PortableMeshItemRacerStatus *r = &next.racers[i];
        decrement(&r->mushroom_ticks);
        decrement(&r->star_ticks);
        decrement(&r->lightning_ticks);
        decrement(&r->spin_ticks);
        decrement(&r->hit_grace_ticks);
        decrement(&r->boo_invisible_ticks);
        decrement(&r->boo_lock_ticks);
        decrement(&r->bullet_ticks);
        decrement(&r->ink_ticks);
    }
    *owner = next;
    *golden_expired_mask = expired;
    return PORTABLE_MESH_ITEM_STATUS_OK;
}

PortableMeshItemStatusResult
portable_mesh_item_status_activate(PortableMeshItemStatusOwner *owner,
                                   const PortableMeshItemRules *rules, unsigned racer,
                                   PortableMeshItemActivation activation)
{
    if (!valid(owner, rules) || racer >= owner->count)
        return PORTABLE_MESH_ITEM_STATUS_INVALID;
    PortableMeshItemStatusOwner next = *owner;
    PortableMeshItemRacerStatus *r = &next.racers[racer];
    switch (activation)
    {
    case PORTABLE_MESH_STATUS_MUSHROOM:
        r->pending_mushroom = 1;
        break;
    case PORTABLE_MESH_STATUS_GOLDEN_START:
    {
        unsigned slot;
        for (slot = 0; slot < PORTABLE_MESH_ITEM_STATUS_AUXILIARIES; ++slot)
            if (!next.auxiliaries[slot].ticks)
                break;
        if (slot == PORTABLE_MESH_ITEM_STATUS_AUXILIARIES)
            return PORTABLE_MESH_ITEM_STATUS_RESOURCE;
        next.auxiliaries[slot] = (PortableMeshItemStatusAuxiliary){
            .ticks = (uint16_t)rules->mushroom_golden_ticks, .owner = (uint8_t)racer};
        derive_golden(&next);
        r->pending_mushroom = 1;
        break;
    }
    case PORTABLE_MESH_STATUS_GOLDEN_REPEAT:
        if (!r->golden_ticks)
            return PORTABLE_MESH_ITEM_STATUS_INELIGIBLE;
        r->pending_mushroom = 1;
        break;
    case PORTABLE_MESH_STATUS_STAR:
        r->star_ticks = (uint16_t)((owner->human_mask & (1u << racer)) ? rules->star_player_ticks
                                                                       : rules->star_cpu_ticks);
        r->boo_invisible_ticks = r->boo_lock_ticks = r->spin_ticks = r->hit_grace_ticks = 0;
        break;
    case PORTABLE_MESH_STATUS_BULLET:
        r->bullet_ticks = PORTABLE_MESH_BULLET_TICKS;
        r->spin_ticks = r->boo_lock_ticks = r->lightning_ticks = 0;
        break;
    case PORTABLE_MESH_STATUS_INK:
        if (r->star_ticks || r->bullet_ticks)
            return PORTABLE_MESH_ITEM_STATUS_INELIGIBLE;
        r->ink_ticks = PORTABLE_MESH_INK_TICKS;
        break;
    case PORTABLE_MESH_STATUS_BOO_INVISIBLE:
        r->boo_invisible_ticks = (uint16_t)rules->boo_invisibility_ticks;
        break;
    case PORTABLE_MESH_STATUS_CLOUD_LIGHTNING:
        if (r->star_ticks)
            return PORTABLE_MESH_ITEM_STATUS_INELIGIBLE;
        r->lightning_ticks = PORTABLE_MESH_SOURCE_CLOUD_LIGHTNING_TICKS;
        break;
    case PORTABLE_MESH_STATUS_RIDER_LOCK:
        if (r->star_ticks)
            return PORTABLE_MESH_ITEM_STATUS_INELIGIBLE;
        r->boo_lock_ticks = PORTABLE_MESH_SOURCE_RIDER_LOCK_TICKS;
        break;
    case PORTABLE_MESH_STATUS_BOO_LOCK:
        if (r->star_ticks)
            return PORTABLE_MESH_ITEM_STATUS_INELIGIBLE;
        r->boo_lock_ticks = (uint16_t)rules->boo_theft_lock_ticks;
        break;
    default:
        return PORTABLE_MESH_ITEM_STATUS_INVALID;
    }
    *owner = next;
    return PORTABLE_MESH_ITEM_STATUS_OK;
}

PortableMeshItemStatusResult
portable_mesh_item_status_lightning(PortableMeshItemStatusOwner *owner,
                                    const PortableMeshItemRules *rules, unsigned user,
                                    const uint8_t *eligible, size_t count, unsigned *affected_mask)
{
    if (!valid(owner, rules) || !eligible || !affected_mask || count != owner->count ||
        user >= count)
        return PORTABLE_MESH_ITEM_STATUS_INVALID;
    for (size_t i = 0; i < count; ++i)
        if (eligible[i] > 1u)
            return PORTABLE_MESH_ITEM_STATUS_INVALID;
    PortableMeshItemStatusOwner next = *owner;
    unsigned affected = 0;
    for (unsigned i = 0; i < next.count; ++i)
        if (i != user && eligible[i] && !next.racers[i].star_ticks && !next.racers[i].bullet_ticks)
        {
            next.racers[i].lightning_ticks = (uint16_t)rules->lightning_duration_ticks;
            affected |= 1u << i;
        }
    *owner = next;
    *affected_mask = affected;
    return PORTABLE_MESH_ITEM_STATUS_OK;
}

static PortableMeshItemStatusResult damage(PortableMeshItemStatusOwner *owner,
                                           const PortableMeshItemRules *rules, unsigned racer,
                                           int eligible, PortableMeshItemDamage *out, int lightning)
{
    if (!valid(owner, rules) || !out || racer >= owner->count || (eligible != 0 && eligible != 1))
        return PORTABLE_MESH_ITEM_STATUS_INVALID;
    PortableMeshItemStatusOwner next = *owner;
    PortableMeshItemRacerStatus *r = &next.racers[racer];
    PortableMeshItemDamage result = PORTABLE_MESH_DAMAGE_IGNORED;
    if (eligible && (r->star_ticks || r->bullet_ticks))
        result = PORTABLE_MESH_DAMAGE_PROTECTED;
    else if (eligible && (lightning || (!r->boo_invisible_ticks && !r->hit_grace_ticks)))
    {
        r->spin_ticks = (uint16_t)rules->hit_spin_ticks;
        r->hit_grace_ticks = (uint16_t)rules->hit_invulnerability_ticks;
        r->pending_mushroom = 0;
        r->mushroom_ticks = 0;
        r->boo_lock_ticks = 0;
        result = PORTABLE_MESH_DAMAGE_APPLIED;
    }
    *owner = next;
    *out = result;
    return PORTABLE_MESH_ITEM_STATUS_OK;
}

PortableMeshItemStatusResult portable_mesh_item_status_damage(PortableMeshItemStatusOwner *owner,
                                                              const PortableMeshItemRules *rules,
                                                              unsigned racer, int eligible,
                                                              PortableMeshItemDamage *out)
{
    return damage(owner, rules, racer, eligible, out, 0);
}
PortableMeshItemStatusResult
portable_mesh_item_status_lightning_impact(PortableMeshItemStatusOwner *owner,
                                           const PortableMeshItemRules *rules, unsigned racer,
                                           int eligible, PortableMeshItemDamage *out)
{
    return damage(owner, rules, racer, eligible, out, 1);
}

PortableMeshItemStatusResult
portable_mesh_item_status_prepare_drive(PortableMeshItemStatusOwner *owner,
                                        const PortableMeshItemRules *rules, unsigned racer,
                                        int eligible_ground, PortableMeshKartEffects *out)
{
    if (!valid(owner, rules) || !out || racer >= owner->count ||
        (eligible_ground != 0 && eligible_ground != 1))
        return PORTABLE_MESH_ITEM_STATUS_INVALID;
    PortableMeshItemStatusOwner next = *owner;
    PortableMeshItemRacerStatus *r = &next.racers[racer];
    if (eligible_ground && r->pending_mushroom && !r->spin_ticks)
    {
        r->pending_mushroom = 0;
        r->mushroom_ticks = (uint16_t)rules->mushroom_boost_ticks;
    }
    PortableMeshKartEffects effects = {
        .speed_ratio_q16 = 65536, .body_scale_q16 = 65536, .boost_cap_ratio_q16 = 65536};
    /* MK8: Star scales top speed and replaces acceleration; boosts still run under it. */
    if (r->star_ticks || r->bullet_ticks)
    {
        effects.star = 1;
        effects.star_cap_ratio_q16 = rules->star_cap_ratio_q16;
        effects.star_acceleration_q16 = rules->star_acceleration_q16;
    }
    else
    {
        /* Source +AC theft lock shares the shrink slow cap, not its body size. */
        if (r->lightning_ticks || r->boo_lock_ticks)
            effects.speed_ratio_q16 = rules->lightning_speed_ratio_q16;
        if (r->lightning_ticks)
            effects.body_scale_q16 = rules->lightning_scale_ratio_q16;
    }
    if (r->mushroom_ticks)
    {
        effects.boost_active = 1;
        effects.boost_cap_ratio_q16 = rules->mushroom_cap_ratio_q16;
        effects.boost_acceleration_q16 = rules->mushroom_acceleration_q16;
    }
    if (r->spin_ticks)
    {
        effects.boost_active = effects.star = 0;
        effects.boost_cap_ratio_q16 = 65536;
        effects.boost_acceleration_q16 = 0;
        effects.cancel_drift = effects.inhibit_controls = 1;
    }
    *owner = next;
    *out = effects;
    return PORTABLE_MESH_ITEM_STATUS_OK;
}

PortableMeshItemStatusResult
portable_mesh_item_status_clear_transient(PortableMeshItemStatusOwner *owner,
                                          const PortableMeshItemRules *rules, unsigned racer)
{
    if (!valid(owner, rules) || racer >= owner->count)
        return PORTABLE_MESH_ITEM_STATUS_INVALID;
    PortableMeshItemStatusOwner next = *owner;
    next.racers[racer].pending_mushroom = 0;
    next.racers[racer].mushroom_ticks = 0;
    next.racers[racer].spin_ticks = 0;
    *owner = next;
    return PORTABLE_MESH_ITEM_STATUS_OK;
}

PortableMeshItemStatusResult
portable_mesh_item_status_auxiliary_count(const PortableMeshItemStatusOwner *owner,
                                          const PortableMeshItemRules *rules, unsigned *out)
{
    if (!out || !valid(owner, rules))
        return PORTABLE_MESH_ITEM_STATUS_INVALID;
    unsigned count = 0;
    for (unsigned i = 0; i < PORTABLE_MESH_ITEM_STATUS_AUXILIARIES; ++i)
        count += owner->auxiliaries[i].ticks != 0;
    *out = count;
    return PORTABLE_MESH_ITEM_STATUS_OK;
}
