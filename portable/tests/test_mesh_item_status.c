#include "mesh_item_status.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
#define OK PORTABLE_MESH_ITEM_STATUS_OK

static PortableMeshItemRules rules(void)
{
    PortableMeshItemRules r = {.profile_id = 76, .revision = 2};
#define MINIMUM(name, minimum, maximum) r.name = minimum;
    PORTABLE_MESH_ITEM_FIELDS(MINIMUM)
#undef MINIMUM
    r.mushroom_boost_ticks = 3;
    r.mushroom_golden_ticks = 5;
    r.star_player_ticks = 3;
    r.star_cpu_ticks = 2;
    r.hit_spin_ticks = 2;
    r.hit_invulnerability_ticks = 3;
    r.boo_invisibility_ticks = 4;
    r.boo_theft_lock_ticks = 2;
    r.mushroom_cap_ratio_q16 = 98304;
    r.star_cap_ratio_q16 = 131072;
    r.mushroom_acceleration_q16 = 2 * 65536;
    r.star_acceleration_q16 = 3 * 65536;
    r.lightning_speed_ratio_q16 = 32768;
    r.lightning_scale_ratio_q16 = 32768;
    for (unsigned i = 0; i < 10; ++i)
        r.roulette.preview[i] = (uint16_t)(0x1000 + i);
    for (unsigned i = 0; i < 27; ++i)
        r.roulette.timeline[i] = 2;
    for (unsigned i = 0; i < 8; ++i)
        r.player_weights[i][0] = r.cpu_weights[i][0] = (PortableGpItemWeight){0x1007, 1};
    return r;
}

static int identity(const PortableMeshKartEffects *e)
{
    return e->speed_ratio_q16 == 65536 && e->body_scale_q16 == 65536 &&
           e->boost_cap_ratio_q16 == 65536 && !e->boost_acceleration_q16 && !e->boost_active &&
           !e->star && !e->cancel_drift && !e->inhibit_controls;
}

static int queued_drive(const PortableMeshItemRules *r)
{
    PortableMeshItemStatusOwner s;
    CHECK(portable_mesh_item_status_init(&s, 1, 1) == OK);
    PortableMeshKartEffects e;
    CHECK(portable_mesh_item_status_prepare_drive(&s, r, 0, 1, &e) == OK && identity(&e));
    unsigned expired = 99;
    CHECK(portable_mesh_item_status_advance(&s, r, &expired) == OK && expired == 0);
    CHECK(portable_mesh_item_status_activate(&s, r, 0, PORTABLE_MESH_STATUS_MUSHROOM) == OK);
    CHECK(portable_mesh_item_status_prepare_drive(&s, r, 0, 0, &e) == OK && identity(&e));
    CHECK(s.racers[0].pending_mushroom && !s.racers[0].mushroom_ticks);
    /* Repeated airborne triple-mushroom requests coalesce; no hidden extra stock. */
    CHECK(portable_mesh_item_status_activate(&s, r, 0, PORTABLE_MESH_STATUS_MUSHROOM) == OK);
    CHECK(portable_mesh_item_status_advance(&s, r, &expired) == OK);
    CHECK(portable_mesh_item_status_prepare_drive(&s, r, 0, 0, &e) == OK && identity(&e));
    CHECK(portable_mesh_item_status_advance(&s, r, &expired) == OK);
    for (unsigned active = 0; active < 3; ++active)
    {
        CHECK(portable_mesh_item_status_prepare_drive(&s, r, 0, 1, &e) == OK && e.boost_active &&
              e.boost_cap_ratio_q16 == 98304 && e.boost_acceleration_q16 == 2 * 65536);
        CHECK(s.racers[0].mushroom_ticks == 3 - active && !s.racers[0].pending_mushroom);
        const PortableMeshItemStatusOwner paused = s;
        CHECK(portable_mesh_item_status_prepare_drive(&s, r, 0, 1, &e) == OK);
        CHECK(!memcmp(&s, &paused, sizeof s)); /* Reads while paused cannot age clocks. */
        CHECK(portable_mesh_item_status_advance(&s, r, &expired) == OK && expired == 0);
    }
    CHECK(portable_mesh_item_status_prepare_drive(&s, r, 0, 1, &e) == OK && identity(&e));
    return 0;
}

static int independent_goldens(const PortableMeshItemRules *r)
{
    PortableMeshItemStatusOwner s;
    CHECK(portable_mesh_item_status_init(&s, 2, 3) == OK);
    CHECK(portable_mesh_item_status_activate(&s, r, 0, PORTABLE_MESH_STATUS_GOLDEN_REPEAT) ==
          PORTABLE_MESH_ITEM_STATUS_INELIGIBLE);
    CHECK(portable_mesh_item_status_activate(&s, r, 0, PORTABLE_MESH_STATUS_GOLDEN_START) == OK);
    unsigned mask, count;
    CHECK(portable_mesh_item_status_advance(&s, r, &mask) == OK && !mask);
    CHECK(portable_mesh_item_status_advance(&s, r, &mask) == OK && !mask);
    CHECK(portable_mesh_item_status_activate(&s, r, 0, PORTABLE_MESH_STATUS_GOLDEN_START) == OK);
    CHECK(s.racers[0].golden_ticks == 3);
    CHECK(portable_mesh_item_status_auxiliary_count(&s, r, &count) == OK && count == 2);
    CHECK(portable_mesh_item_status_activate(&s, r, 0, PORTABLE_MESH_STATUS_GOLDEN_REPEAT) == OK);
    CHECK(s.auxiliaries[0].ticks == 3 && s.auxiliaries[1].ticks == 5);
    CHECK(portable_mesh_item_status_clear_transient(&s, r, 0) == OK);
    CHECK(!s.racers[0].pending_mushroom && s.racers[0].golden_ticks == 3);
    uint16_t current_inventory = 0x100b; /* Stolen replacement, unrelated to expiry. */
    for (unsigned elapsed = 1; elapsed <= 5; ++elapsed)
    {
        CHECK(portable_mesh_item_status_advance(&s, r, &mask) == OK);
        CHECK(mask == ((elapsed == 3 || elapsed == 5) ? 1u : 0u));
        if (mask & 1u)
            current_inventory = 0;
        if (elapsed == 3)
        {
            CHECK(!current_inventory && s.racers[0].golden_ticks == 2);
            current_inventory = 0x100a; /* The later expiry must clear this too. */
        }
    }
    CHECK(!current_inventory && !s.racers[0].golden_ticks);
    CHECK(portable_mesh_item_status_auxiliary_count(&s, r, &count) == OK && count == 0);
    /* Fixed shared-pool pressure refuses atomically; it does not erase a timer. */
    for (unsigned i = 0; i < 64; ++i)
        CHECK(portable_mesh_item_status_activate(&s, r, i & 1u,
                                                 PORTABLE_MESH_STATUS_GOLDEN_START) == OK);
    const PortableMeshItemStatusOwner full = s;
    CHECK(portable_mesh_item_status_activate(&s, r, 0, PORTABLE_MESH_STATUS_GOLDEN_START) ==
              PORTABLE_MESH_ITEM_STATUS_RESOURCE &&
          !memcmp(&s, &full, sizeof s));
    CHECK(portable_mesh_item_status_auxiliary_count(&s, r, &count) == OK && count == 64);
    for (unsigned tick = 0; tick < 5; ++tick)
        CHECK(portable_mesh_item_status_advance(&s, r, &mask) == OK &&
              mask == (tick == 4 ? 3u : 0u));
    CHECK(portable_mesh_item_status_activate(&s, r, 1, PORTABLE_MESH_STATUS_GOLDEN_START) == OK);
    return 0;
}

static int protection_and_damage(const PortableMeshItemRules *r)
{
    PortableMeshItemStatusOwner s;
    CHECK(portable_mesh_item_status_init(&s, 4, 3) == OK);
    PortableMeshKartEffects locked;
    CHECK(portable_mesh_item_status_activate(&s, r, 0, PORTABLE_MESH_STATUS_BOO_LOCK) == OK);
    CHECK(portable_mesh_item_status_prepare_drive(&s, r, 0, 1, &locked) == OK &&
          locked.speed_ratio_q16 == 32768 && locked.body_scale_q16 == 65536 &&
          !locked.inhibit_controls);
    CHECK(portable_mesh_item_status_activate(&s, r, 0, PORTABLE_MESH_STATUS_STAR) == OK);
    CHECK(!s.racers[0].boo_lock_ticks);
    CHECK(portable_mesh_item_status_prepare_drive(&s, r, 0, 1, &locked) == OK &&
          locked.speed_ratio_q16 == 65536);
    CHECK(portable_mesh_item_status_init(&s, 4, 3) == OK);

    CHECK(portable_mesh_item_status_activate(&s, r, 1, PORTABLE_MESH_STATUS_STAR) == OK);
    uint8_t eligible[4] = {1, 1, 1, 0}; /* Racer2 airborne, racer3 respawning. */
    unsigned mask;
    CHECK(portable_mesh_item_status_lightning(&s, r, 0, eligible, 4, &mask) == OK && mask == 4);
    CHECK(!s.racers[0].lightning_ticks && !s.racers[1].lightning_ticks &&
          s.racers[2].lightning_ticks == 48 && !s.racers[3].lightning_ticks);
    PortableMeshItemDamage damage;
    CHECK(portable_mesh_item_status_activate(&s, r, 2, PORTABLE_MESH_STATUS_BOO_INVISIBLE) == OK);
    CHECK(portable_mesh_item_status_damage(&s, r, 2, 1, &damage) == OK &&
          damage == PORTABLE_MESH_DAMAGE_IGNORED);
    CHECK(portable_mesh_item_status_activate(&s, r, 2, PORTABLE_MESH_STATUS_BOO_LOCK) == OK);
    CHECK(portable_mesh_item_status_activate(&s, r, 2, PORTABLE_MESH_STATUS_STAR) == OK);
    CHECK(s.racers[2].star_ticks == 2 && !s.racers[2].boo_lock_ticks &&
          !s.racers[2].boo_invisible_ticks && s.racers[2].lightning_ticks == 48);
    CHECK(portable_mesh_item_status_damage(&s, r, 2, 1, &damage) == OK &&
          damage == PORTABLE_MESH_DAMAGE_PROTECTED);
    CHECK(portable_mesh_item_status_activate(&s, r, 2, PORTABLE_MESH_STATUS_BOO_LOCK) ==
          PORTABLE_MESH_ITEM_STATUS_INELIGIBLE);
    PortableMeshKartEffects e;
    CHECK(portable_mesh_item_status_prepare_drive(&s, r, 2, 0, &e) == OK &&
          e.speed_ratio_q16 == 65536 && e.body_scale_q16 == 65536 && e.star && !e.boost_active &&
          e.star_cap_ratio_q16 == 131072 && e.star_acceleration_q16 == 3 * 65536);
    for (unsigned tick = 1; tick <= 2; ++tick)
    {
        CHECK(portable_mesh_item_status_advance(&s, r, &mask) == OK);
        CHECK(portable_mesh_item_status_prepare_drive(&s, r, 2, 0, &e) == OK);
        CHECK((e.speed_ratio_q16 == 65536) == (tick < 2));
    }
    CHECK(s.racers[2].lightning_ticks == 46 && e.speed_ratio_q16 == 32768 &&
          e.body_scale_q16 == 32768);
    CHECK(portable_mesh_item_status_activate(&s, r, 0, PORTABLE_MESH_STATUS_MUSHROOM) == OK);
    CHECK(portable_mesh_item_status_prepare_drive(&s, r, 0, 1, &e) == OK && e.boost_active);
    CHECK(portable_mesh_item_status_damage(&s, r, 0, 1, &damage) == OK &&
          damage == PORTABLE_MESH_DAMAGE_APPLIED);
    CHECK(!s.racers[0].mushroom_ticks && !s.racers[0].pending_mushroom);
    const PortableMeshItemStatusOwner once = s;
    CHECK(portable_mesh_item_status_damage(&s, r, 0, 1, &damage) == OK &&
          damage == PORTABLE_MESH_DAMAGE_IGNORED && !memcmp(&s, &once, sizeof s));
    CHECK(portable_mesh_item_status_prepare_drive(&s, r, 0, 1, &e) == OK && e.cancel_drift &&
          e.inhibit_controls && !e.boost_active);
    CHECK(portable_mesh_item_status_advance(&s, r, &mask) == OK);
    CHECK(portable_mesh_item_status_advance(&s, r, &mask) == OK);
    CHECK(portable_mesh_item_status_prepare_drive(&s, r, 0, 1, &e) == OK && identity(&e));
    CHECK(s.racers[0].hit_grace_ticks == 1);
    CHECK(portable_mesh_item_status_damage(&s, r, 0, 1, &damage) == OK &&
          damage == PORTABLE_MESH_DAMAGE_IGNORED);
    CHECK(portable_mesh_item_status_advance(&s, r, &mask) == OK);
    CHECK(portable_mesh_item_status_damage(&s, r, 0, 1, &damage) == OK &&
          damage == PORTABLE_MESH_DAMAGE_APPLIED);
    CHECK(portable_mesh_item_status_activate(&s, r, 0, PORTABLE_MESH_STATUS_BOO_INVISIBLE) == OK);
    CHECK(portable_mesh_item_status_activate(&s, r, 0, PORTABLE_MESH_STATUS_STAR) == OK);
    CHECK(!s.racers[0].spin_ticks && !s.racers[0].hit_grace_ticks &&
          !s.racers[0].boo_invisible_ticks);
    CHECK(portable_mesh_item_status_clear_transient(&s, r, 0) == OK && s.racers[0].star_ticks == 3);
    /* Shrink expires independently, after48 admitted active turns. */
    for (unsigned tick = 5; tick < 48; ++tick)
        CHECK(portable_mesh_item_status_advance(&s, r, &mask) == OK);
    CHECK(!s.racers[2].lightning_ticks);
    return 0;
}

static int lightning_impact(const PortableMeshItemRules *r)
{
    PortableMeshItemStatusOwner s;
    CHECK(portable_mesh_item_status_init(&s, 2, 3) == OK);
    CHECK(portable_mesh_item_status_activate(&s, r, 1, PORTABLE_MESH_STATUS_BOO_INVISIBLE) == OK);
    s.racers[1].hit_grace_ticks = 2;
    PortableMeshItemDamage hit;
    CHECK(portable_mesh_item_status_damage(&s, r, 1, 1, &hit) == OK &&
          hit == PORTABLE_MESH_DAMAGE_IGNORED);
    CHECK(portable_mesh_item_status_lightning_impact(&s, r, 1, 1, &hit) == OK &&
          hit == PORTABLE_MESH_DAMAGE_APPLIED);
    CHECK(s.racers[1].boo_invisible_ticks == r->boo_invisibility_ticks &&
          s.racers[1].spin_ticks == r->hit_spin_ticks);
    CHECK(portable_mesh_item_status_activate(&s, r, 1, PORTABLE_MESH_STATUS_STAR) == OK);
    CHECK(portable_mesh_item_status_lightning_impact(&s, r, 1, 1, &hit) == OK &&
          hit == PORTABLE_MESH_DAMAGE_PROTECTED);
    CHECK(portable_mesh_item_status_lightning_impact(&s, r, 0, 0, &hit) == OK &&
          hit == PORTABLE_MESH_DAMAGE_IGNORED);
    const PortableMeshItemStatusOwner before = s;
    CHECK(portable_mesh_item_status_lightning_impact(&s, r, 1, 2, &hit) ==
          PORTABLE_MESH_ITEM_STATUS_INVALID);
    CHECK(hit == PORTABLE_MESH_DAMAGE_IGNORED && !memcmp(&before, &s, sizeof s));
    return 0;
}

static int rollback(const PortableMeshItemRules *r)
{
    PortableMeshItemStatusOwner s;
    CHECK(portable_mesh_item_status_init(&s, 2, 3) == OK);
    const PortableMeshItemStatusOwner before = s;
    PortableMeshKartEffects e;
    memset(&e, 0xa5, sizeof e);
    const PortableMeshKartEffects old_e = e;
    unsigned mask = 0xabc;
    PortableMeshItemDamage damage = PORTABLE_MESH_DAMAGE_PROTECTED;
    uint8_t eligible[2] = {1, 2};
    CHECK(portable_mesh_item_status_lightning(&s, r, 0, eligible, 2, &mask) ==
          PORTABLE_MESH_ITEM_STATUS_INVALID);
    CHECK(mask == 0xabc && !memcmp(&s, &before, sizeof s));
    CHECK(portable_mesh_item_status_prepare_drive(&s, r, 0, 2, &e) ==
          PORTABLE_MESH_ITEM_STATUS_INVALID);
    CHECK(!memcmp(&e, &old_e, sizeof e) && !memcmp(&s, &before, sizeof s));
    CHECK(portable_mesh_item_status_activate(&s, r, 0, (PortableMeshItemActivation)99) ==
          PORTABLE_MESH_ITEM_STATUS_INVALID);
    CHECK(portable_mesh_item_status_damage(&s, r, 0, 2, &damage) ==
          PORTABLE_MESH_ITEM_STATUS_INVALID);
    CHECK(damage == PORTABLE_MESH_DAMAGE_PROTECTED && !memcmp(&s, &before, sizeof s));
    /* A late malformed racer/auxiliary rejects the whole clock turn. */
    s.racers[1].pending_mushroom = 2;
    PortableMeshItemStatusOwner bad = s;
    CHECK(portable_mesh_item_status_advance(&s, r, &mask) == PORTABLE_MESH_ITEM_STATUS_INVALID);
    CHECK(!memcmp(&s, &bad, sizeof s) && mask == 0xabc);
    s = before;
    s.auxiliaries[63] = (PortableMeshItemStatusAuxiliary){.owner = 2, .ticks = 1};
    bad = s;
    CHECK(portable_mesh_item_status_advance(&s, r, &mask) == PORTABLE_MESH_ITEM_STATUS_INVALID);
    CHECK(!memcmp(&s, &bad, sizeof s));
    return 0;
}
static int source_cloud_lightning(const PortableMeshItemRules *base)
{
    PortableMeshItemRules r=*base;
    r.lightning_duration_ticks=120;
    PortableMeshItemStatusOwner s;
    const uint8_t eligible[2]={1,1};unsigned affected=0;
    CHECK(portable_mesh_item_status_init(&s,2,1)==OK);
    CHECK(portable_mesh_item_status_lightning(&s,&r,1,eligible,2,&affected)==OK && s.racers[0].lightning_ticks==120);
    CHECK(portable_mesh_item_status_activate(&s,&r,0,PORTABLE_MESH_STATUS_CLOUD_LIGHTNING)==OK &&
          s.racers[0].lightning_ticks==360);
    unsigned expired=99;
    for(unsigned remaining=359;;--remaining)
    {
        CHECK(portable_mesh_item_status_advance(&s,&r,&expired)==OK && !expired &&
              s.racers[0].lightning_ticks==remaining);
        if(!remaining)break;
    }
    CHECK(portable_mesh_item_status_activate(&s,&r,0,PORTABLE_MESH_STATUS_CLOUD_LIGHTNING)==OK);
    CHECK(portable_mesh_item_status_activate(&s,&r,0,PORTABLE_MESH_STATUS_STAR)==OK &&
          s.racers[0].lightning_ticks==360); /* Star hides shrink, not its independent clock. */
    const PortableMeshItemStatusOwner protected=s;
    CHECK(portable_mesh_item_status_activate(&s,&r,0,PORTABLE_MESH_STATUS_CLOUD_LIGHTNING)==
          PORTABLE_MESH_ITEM_STATUS_INELIGIBLE && !memcmp(&s,&protected,sizeof s));
    r.lightning_duration_ticks=500;
    CHECK(portable_mesh_item_status_init(&s,2,1)==OK);
    CHECK(portable_mesh_item_status_lightning(&s,&r,1,eligible,2,&affected)==OK && s.racers[0].lightning_ticks==500);
    CHECK(portable_mesh_item_status_activate(&s,&r,0,PORTABLE_MESH_STATUS_CLOUD_LIGHTNING)==OK &&
          s.racers[0].lightning_ticks==360);
    for(unsigned high=0;high<2;++high)
    {
        r.lightning_duration_ticks=high?500:120;
        s.racers[0].lightning_ticks=(uint16_t)((high?500:360)+1);
        const PortableMeshItemStatusOwner bad=s;expired=99;
        CHECK(portable_mesh_item_status_advance(&s,&r,&expired)==PORTABLE_MESH_ITEM_STATUS_INVALID &&
              !memcmp(&s,&bad,sizeof s) && expired==99);
    }
    return 0;
}
static int source_rider_lock(const PortableMeshItemRules *base)
{
    PortableMeshItemRules r=*base;
    r.boo_theft_lock_ticks=120;
    PortableMeshItemStatusOwner s;
    CHECK(portable_mesh_item_status_init(&s,1,1)==OK);
    CHECK(portable_mesh_item_status_activate(&s,&r,0,PORTABLE_MESH_STATUS_BOO_LOCK)==OK &&
          s.racers[0].boo_lock_ticks==120);
    CHECK(portable_mesh_item_status_activate(&s,&r,0,PORTABLE_MESH_STATUS_RIDER_LOCK)==OK &&
          s.racers[0].boo_lock_ticks==360);
    unsigned expired=99;
    for(unsigned remaining=359;;--remaining)
    {
        CHECK(portable_mesh_item_status_advance(&s,&r,&expired)==OK && !expired &&
              s.racers[0].boo_lock_ticks==remaining);
        if(!remaining)break;
    }
    CHECK(portable_mesh_item_status_activate(&s,&r,0,PORTABLE_MESH_STATUS_RIDER_LOCK)==OK);
    CHECK(portable_mesh_item_status_activate(&s,&r,0,PORTABLE_MESH_STATUS_BOO_LOCK)==OK &&
          s.racers[0].boo_lock_ticks==120); /* Shared clock overwrites; never sums. */
    CHECK(portable_mesh_item_status_activate(&s,&r,0,PORTABLE_MESH_STATUS_STAR)==OK &&
          !s.racers[0].boo_lock_ticks);
    const PortableMeshItemStatusOwner protected=s;
    CHECK(portable_mesh_item_status_activate(&s,&r,0,PORTABLE_MESH_STATUS_RIDER_LOCK)==
          PORTABLE_MESH_ITEM_STATUS_INELIGIBLE && !memcmp(&s,&protected,sizeof s));
    r.boo_theft_lock_ticks=500;
    CHECK(portable_mesh_item_status_init(&s,1,1)==OK);
    CHECK(portable_mesh_item_status_activate(&s,&r,0,PORTABLE_MESH_STATUS_BOO_LOCK)==OK &&
          s.racers[0].boo_lock_ticks==500);
    CHECK(portable_mesh_item_status_activate(&s,&r,0,PORTABLE_MESH_STATUS_RIDER_LOCK)==OK &&
          s.racers[0].boo_lock_ticks==360);
    for(unsigned high=0;high<2;++high)
    {
        r.boo_theft_lock_ticks=high?500:120;
        s.racers[0].boo_lock_ticks=32768;
        const PortableMeshItemStatusOwner bad=s;expired=99;
        CHECK(portable_mesh_item_status_advance(&s,&r,&expired)==PORTABLE_MESH_ITEM_STATUS_INVALID &&
              !memcmp(&s,&bad,sizeof s) && expired==99);
    }
    return 0;
}
static int source_haunt_lock(const PortableMeshItemRules *r)
{
    PortableMeshItemStatusOwner s;CHECK(portable_mesh_item_status_init(&s,1,1)==OK);
    CHECK(portable_mesh_item_status_source_haunt_lock(&s,r,0,360)==OK && s.racers[0].boo_lock_ticks==360);
    CHECK(portable_mesh_item_status_source_haunt_lock(&s,r,0,32767)==OK && s.racers[0].boo_lock_ticks==32767);
    unsigned expired=99;
    CHECK(portable_mesh_item_status_advance(&s,r,&expired)==OK && s.racers[0].boo_lock_ticks==32766);
    PortableMeshItemStatusOwner old=s;
    CHECK(portable_mesh_item_status_source_haunt_lock(&s,r,0,0)==PORTABLE_MESH_ITEM_STATUS_INVALID && !memcmp(&s,&old,sizeof s));
    CHECK(portable_mesh_item_status_source_haunt_lock(&s,r,0,32768)==PORTABLE_MESH_ITEM_STATUS_INVALID && !memcmp(&s,&old,sizeof s));
    CHECK(portable_mesh_item_status_source_haunt_lock(&s,r,1,360)==PORTABLE_MESH_ITEM_STATUS_INVALID && !memcmp(&s,&old,sizeof s));
    CHECK(portable_mesh_item_status_activate(&s,r,0,PORTABLE_MESH_STATUS_BOO_LOCK)==OK &&
          s.racers[0].boo_lock_ticks==r->boo_theft_lock_ticks);
    CHECK(portable_mesh_item_status_activate(&s,r,0,PORTABLE_MESH_STATUS_STAR)==OK && !s.racers[0].boo_lock_ticks);
    old=s;CHECK(portable_mesh_item_status_source_haunt_lock(&s,r,0,360)==PORTABLE_MESH_ITEM_STATUS_INELIGIBLE && !memcmp(&s,&old,sizeof s));
    return 0;
}
int main(void)
{
    const PortableMeshItemRules r = rules();
    CHECK(portable_mesh_item_rules_valid(&r));
    CHECK(!source_haunt_lock(&r));
    CHECK(!source_cloud_lightning(&r));
    CHECK(!source_rider_lock(&r));
    CHECK(!queued_drive(&r));
    CHECK(!independent_goldens(&r));
    CHECK(!protection_and_damage(&r));
    CHECK(!lightning_impact(&r));
    CHECK(!rollback(&r));
    puts("portable mesh item status: PASS");
    return 0;
}
