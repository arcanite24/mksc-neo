#include "mesh_course_coin_turn.h"
#include "mesh_course_fixture.h"
#include "mesh_item_fixture.h"
#include "mesh_item_rules_asset.h"
#include <stdio.h>
#include <string.h>
#define Q 65536
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "coin turn line %d: %s\n", __LINE__, #x);                              \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
typedef struct
{
    PortableMeshCourseCoinsConfig config;
    PortableMeshCourseCoins world;
    PortableMeshKartRules kart_rules;
    PortableMeshItemRules item_rules;
    PortableMeshItems before_items, after_items;
    PortableMeshRacerState before[2], after[2];
    PortableMeshCourseActorEvents actors;
    PortableMeshItemEvents items;
    PortableMeshCourseCoinEvents events;
} Fixture;
static int setup(Fixture *f)
{
    memset(f, 0, sizeof *f);
    f->config = (PortableMeshCourseCoinsConfig){.course_id = 7,
                                                .enabled = 1,
                                                .profile_id = 5,
                                                .revision = 1,
                                                .modes = 13,
                                                .cap = 99,
                                                .initial = 5,
                                                .placement_count = 1,
                                                .loss = {4, 2, 1, 8, 1}};
    for (unsigned i = 0; i < 100; ++i)
        f->config.ratios_q16[i] = Q;
    f->config.placements[0] =
        (PortableMeshCourseCoin){.id = 1, .model_id = 1, .half_q16 = {Q / 4, Q / 4, Q / 4}};
    f->kart_rules = test_mesh_rules();
    uint8_t payload[PORTABLE_MESH_ITEM_RULES_BYTES];
    test_mir_payload(payload);
    PortableAssetSectionView section = {
        .kind = 103, .content_id = 41, .version = 1, .bytes = payload, .size = sizeof payload};
    if (!portable_mesh_item_rules_asset_open(&section, &f->item_rules))
        return 0;
    f->item_rules.boo_theft_lock_ticks = 256;
    if (portable_mesh_item_inventory_init(&f->before_items.inventory, 2, 1, 123) !=
            PORTABLE_MESH_ITEM_OK ||
        portable_mesh_item_status_init(&f->before_items.status, 2, 1) !=
            PORTABLE_MESH_ITEM_STATUS_OK)
        return 0;
    f->after_items = f->before_items;
    for (unsigned i = 0; i < 2; ++i)
    {
        const int32_t p[3] = {-Q, (int32_t)i * 10 * Q,
                              f->kart_rules.kart_radius_q16 + f->kart_rules.ride_height_q16};
        const int32_t forward[3] = {1073741824, 0, 0};
        if (portable_mesh_kart_init(&f->before[i].kart, &f->kart_rules, p, forward) !=
            PORTABLE_MESH_OK)
            return 0;
        f->before[i].phase = PORTABLE_MESH_RACER_RACING;
        f->before[i].kart.grounded = 1;
        f->after[i] = f->before[i];
        f->after[i].kart.position_q16[0] = Q;
    }
    return portable_mesh_course_coins_init(&f->config, &f->world);
}
static int turn(Fixture *f, uint32_t mode, unsigned contact)
{
    char error[160];
    return portable_mesh_course_coin_turn(
        &f->config, &f->world, mode, &f->kart_rules, NULL, f->before, f->after, 2, &f->before_items,
        &f->after_items, &f->actors, &f->items, (uint8_t)contact, &f->events, error, sizeof error);
}
static int source_haunt_order(void)
{
    Fixture f;CHECK(setup(&f));f.world.count[0]=98;
    CHECK(portable_mesh_item_status_source_haunt_lock(&f.after_items.status,&f.item_rules,0,64)==PORTABLE_MESH_ITEM_STATUS_OK);
    f.actors.source_haunt_assigned_mask=1;
    CHECK(turn(&f,1,0) && f.events.picked[0]==1 && !f.events.lost[0] && f.world.count[0]==99);
    CHECK(setup(&f));f.world.count[0]=98;
    CHECK(portable_mesh_item_status_source_haunt_lock(&f.before_items.status,&f.item_rules,0,74)==PORTABLE_MESH_ITEM_STATUS_OK);
    f.after_items=f.before_items;f.after_items.status.racers[0].boo_lock_ticks=63;
    f.actors.source_haunt_clock_mask=1;f.actors.source_haunt_lock_clock[0]=64;
    CHECK(turn(&f,1,0) && f.events.picked[0]==1 && f.events.lost[0]==1 && f.world.count[0]==98);
    CHECK(setup(&f));f.world.count[0]=0;f.world.available=0;
    CHECK(portable_mesh_item_status_source_haunt_lock(&f.before_items.status,&f.item_rules,0,64)==PORTABLE_MESH_ITEM_STATUS_OK);
    f.after_items=f.before_items;f.after_items.status.racers[0].boo_lock_ticks=63;
    f.actors.source_haunt_clock_mask=1;f.actors.source_haunt_lock_clock[0]=64;
    CHECK(turn(&f,1,0) && !f.events.lost[0] && !f.world.count[0] && !f.actors.hit_mask);
    const PortableMeshCourseCoins old=f.world;const PortableMeshCourseCoinEvents before=f.events;
    f.actors.source_haunt_clock_mask=4;
    CHECK(!turn(&f,1,0) && !memcmp(&f.world,&old,sizeof old) && !memcmp(&f.events,&before,sizeof before));
    return 0;
}
static int haunt_order(void)
{
    Fixture f;
    CHECK(setup(&f));
    CHECK(portable_mesh_item_status_activate(&f.before_items.status, &f.item_rules, 0,
                                             PORTABLE_MESH_STATUS_BOO_LOCK) ==
          PORTABLE_MESH_ITEM_STATUS_OK);
    f.after_items = f.before_items;
    unsigned expired = 0;
    CHECK(portable_mesh_item_status_advance(&f.after_items.status, &f.item_rules, &expired) ==
          PORTABLE_MESH_ITEM_STATUS_OK);
    CHECK(f.before_items.status.racers[0].boo_lock_ticks == 256 &&
          f.after_items.status.racers[0].boo_lock_ticks == 255);
    f.world.count[0] = 99;
    CHECK(turn(&f, 1, 0));
    CHECK(f.world.count[0] == 98 && f.world.available == 1 && !f.events.picked[0] &&
          f.events.before[0] == 99 && f.events.after[0] == 98 && f.events.lost[0] == 1 &&
          f.events.causes[0] == 16);
    CHECK(setup(&f));
    CHECK(portable_mesh_item_status_activate(&f.after_items.status, &f.item_rules, 0,
                                             PORTABLE_MESH_STATUS_BOO_LOCK) ==
          PORTABLE_MESH_ITEM_STATUS_OK);
    f.items.count = 1;
    f.items.values[0] =
        (PortableMeshItemEvent){.kind = PORTABLE_MESH_EVENT_HAUNT, .racer = 1, .target = 0};
    f.world.count[0] = 98;
    CHECK(turn(&f, 4, 0));
    CHECK(!f.world.available && f.events.picked[0] == 1 && f.world.count[0] == 99 &&
          !f.events.lost[0]);
    /* A fresh64/256 lock is admitted after movement. The next accepted
     * decrement owns its one parity pulse, not both adjacent turns. */
    for (unsigned duration = 64; duration <= 256; duration *= 4)
    {
        CHECK(setup(&f));
        f.item_rules.boo_theft_lock_ticks = (uint16_t)duration;
        CHECK(portable_mesh_item_status_activate(&f.after_items.status, &f.item_rules, 0,
                                                 PORTABLE_MESH_STATUS_BOO_LOCK) ==
              PORTABLE_MESH_ITEM_STATUS_OK);
        f.items.count = 1;
        f.items.values[0] =
            (PortableMeshItemEvent){.kind = PORTABLE_MESH_EVENT_HAUNT, .racer = 1, .target = 0};
        f.world.count[0] = 98;
        CHECK(turn(&f, 1, 0) && f.world.count[0] == 99 && f.events.picked[0] == 1 &&
              !f.events.lost[0]);
        for (unsigned tick = 1; tick <= 2; ++tick)
        {
            f.before_items = f.after_items;
            CHECK(portable_mesh_item_status_advance(&f.after_items.status, &f.item_rules,
                                                    &expired) == PORTABLE_MESH_ITEM_STATUS_OK);
            f.items.count = 0;
            memcpy(f.before, f.after, sizeof f.before);
            f.after[0].kart.position_q16[0] += Q;
            CHECK(turn(&f, 1, 0));
            CHECK(f.world.count[0] == 98 && f.events.lost[0] == (tick == 1 ? 1u : 0u) &&
                  !f.events.picked[0]);
            CHECK(f.after_items.status.racers[0].boo_lock_ticks == duration - tick);
        }
    }
    /* A refreshed boundary also belongs to the new admission; the superseded
     * old lock cannot produce a second pulse on that same accepted turn. */
    CHECK(setup(&f));
    f.before_items.status.racers[0].boo_lock_ticks = 64;
    CHECK(portable_mesh_item_status_activate(&f.after_items.status, &f.item_rules, 0,
                                             PORTABLE_MESH_STATUS_BOO_LOCK) ==
          PORTABLE_MESH_ITEM_STATUS_OK);
    f.items.count = 1;
    f.items.values[0] =
        (PortableMeshItemEvent){.kind = PORTABLE_MESH_EVENT_HAUNT, .racer = 1, .target = 0};
    CHECK(turn(&f, 1, 0) && f.world.count[0] == 6 && !f.events.lost[0]);
    /* Teepee's source driver phase subtracts a fresh accelerator edge before
     * MIR's decrement. Parity observes that adjusted clock, not the old one. */
    CHECK(setup(&f));
    f.before_items.status.racers[0].boo_lock_ticks = 74;
    f.after_items.status.racers[0].boo_lock_ticks = 63;
    f.actors.teepee_clock_mask = 1;
    f.actors.teepee_lock_clock[0] = 64;
    CHECK(turn(&f, 1, 0) && f.world.count[0] == 5 && f.events.picked[0] == 1 &&
          f.events.lost[0] == 1 && f.events.causes[0] == 16);
    CHECK(setup(&f));
    f.before_items.status.racers[0].boo_lock_ticks = 64;
    f.after_items.status.racers[0].boo_lock_ticks = 360;
    f.actors.teepee_assigned_mask = 1;
    CHECK(turn(&f, 1, 0) && f.world.count[0] == 6 && !f.events.lost[0]);
    /* A non-boundary clock, protection or capture/recovery holds suppresses
     * drain. The helper never advances immutable MIR status itself. */
    for (unsigned guard = 0; guard < 3; ++guard)
    {
        CHECK(setup(&f));
        f.before_items.status.racers[0].boo_lock_ticks = guard ? 64 : 63;
        f.after_items.status.racers[0].boo_lock_ticks = guard ? 63 : 62;
        if (guard == 1)
            f.after_items.status.racers[0].star_ticks = 1;
        if (guard == 2)
            f.after_items.controls[0].recovering = 1;
        PortableMeshItems old = f.after_items;
        CHECK(turn(&f, 8, 0) && !f.events.lost[0] && !memcmp(&old, &f.after_items, sizeof old));
    }
    return 0;
}
static int recovery_contact(void)
{
    Fixture f;
    CHECK(setup(&f));
    f.after[0].kart.respawn_ticks = f.kart_rules.respawn_ticks;
    CHECK(turn(&f, 1, 0) && f.world.count[0] == 3 && f.events.lost[0] == 2 &&
          f.world.available == 1 && !f.events.picked[0]);
    f.before[0] = f.after[0];
    if (f.after[0].kart.respawn_ticks > 1)
        --f.after[0].kart.respawn_ticks;
    CHECK(turn(&f, 1, 0) && !f.events.lost[0] && f.world.count[0] == 3);
    CHECK(setup(&f));
    f.config.placement_count = 0;
    CHECK(portable_mesh_course_coins_init(&f.config, &f.world));
    CHECK(turn(&f, 1, 1) && f.world.count[0] == 4 && f.world.contact_grace[0] == 8 &&
          f.events.causes[0] == 4);
    for (unsigned n = 0; n < 7; ++n)
    {
        CHECK(portable_mesh_course_coins_advance(&f.config, &f.world));
        CHECK(turn(&f, 1, 1) && !f.events.lost[0]);
    }
    CHECK(portable_mesh_course_coins_advance(&f.config, &f.world));
    CHECK(turn(&f, 1, 1) && f.world.count[0] == 3 && f.events.lost[0] == 1);
    /* Generic wall events are not an input to this helper: no proximity,
     * velocity, or an empty event list can manufacture a contact loss. */
    CHECK(turn(&f, 1, 0) && !f.events.lost[0]);
    return 0;
}
static int source_and_item_damage(void)
{
    Fixture f;
    CHECK(setup(&f));
    f.world.count[0] = 99;
    f.actors.hit_mask = 1;
    /* Source-course damage is a pre-pickup request; accepted status damage
     * normally prevents collection this turn. Loss still has exact cause. */
    PortableMeshItemDamage damage;
    CHECK(portable_mesh_item_status_damage(&f.after_items.status, &f.item_rules, 0, 1, &damage) ==
              PORTABLE_MESH_ITEM_STATUS_OK &&
          damage == PORTABLE_MESH_DAMAGE_APPLIED);
    CHECK(turn(&f, 1, 0) && f.world.count[0] == 95 && !f.events.picked[0] &&
          f.events.causes[0] == 1);
    CHECK(setup(&f));
    f.world.count[0] = 99;
    f.actors.hit_mask = f.actors.thwomp_crush_mask = 1;
    CHECK(turn(&f, 1, 0) && f.world.count[0] == 91 && !f.events.picked[0] &&
          f.events.lost[0] == 8 && f.events.causes[0] == 8);
    CHECK(setup(&f));
    f.world.count[0] = 99;
    f.items.count = 1;
    f.items.values[0] = (PortableMeshItemEvent){
        .kind = PORTABLE_MESH_EVENT_HIT, .racer = 1, .target = 0, .item = 0x1000};
    CHECK(portable_mesh_item_status_damage(&f.after_items.status, &f.item_rules, 0, 1, &damage) ==
              PORTABLE_MESH_ITEM_STATUS_OK &&
          damage == PORTABLE_MESH_DAMAGE_APPLIED);
    CHECK(turn(&f, 1, 0) && f.world.count[0] == 95 && f.world.available == 1 &&
          f.events.lost[0] == 4);
    /* At98 there is room before the accepted item hit: collect to99, then
     * lose4. New spin cannot erase this chronologically earlier pickup. */
    CHECK(setup(&f));
    f.world.count[0] = 98;
    f.items.count = 1;
    f.items.values[0] = (PortableMeshItemEvent){
        .kind = PORTABLE_MESH_EVENT_HIT, .racer = 1, .target = 0, .item = 0x1000};
    CHECK(portable_mesh_item_status_damage(&f.after_items.status, &f.item_rules, 0, 1, &damage) ==
              PORTABLE_MESH_ITEM_STATUS_OK &&
          damage == PORTABLE_MESH_DAMAGE_APPLIED);
    CHECK(turn(&f, 1, 0) && f.world.count[0] == 95 && !f.world.available &&
          f.events.picked[0] == 1 && f.events.lost[0] == 4);
    CHECK(setup(&f));
    CHECK(portable_mesh_item_status_damage(&f.before_items.status, &f.item_rules, 0, 1, &damage) ==
              PORTABLE_MESH_ITEM_STATUS_OK &&
          damage == PORTABLE_MESH_DAMAGE_APPLIED);
    f.after_items = f.before_items;
    CHECK(turn(&f, 1, 0) && f.world.available == 1 && !f.events.picked[0] && f.world.count[0] == 5);
    return 0;
}
static int phases_modes_rollback(void)
{
    Fixture f;
    CHECK(setup(&f));
    f.config.placement_count = 0;
    CHECK(portable_mesh_course_coins_init(&f.config, &f.world));
    f.actors.hit_mask = 1;
    for (unsigned phase = PORTABLE_MESH_RACER_COUNTDOWN; phase <= PORTABLE_MESH_RACER_FINISHED;
         phase += 2)
    {
        f.before[0].phase = f.after[0].phase = phase;
        CHECK(turn(&f, 1, 1) && !f.events.lost[0] && f.world.count[0] == 5);
    }
    CHECK(setup(&f));
    f.actors.hit_mask = 1;
    PortableMeshCourseCoins saved = f.world;
    CHECK(turn(&f, 2, 0) && !memcmp(&f.world, &saved, sizeof saved));
    CHECK(turn(&f, 16, 0) && !memcmp(&f.world, &saved, sizeof saved));
    for (unsigned mode = 2; mode <= 16; mode *= 8)
    {
        CHECK(setup(&f));
        f.config.modes = 31;
        CHECK(turn(&f, mode, 0) && f.events.picked[0] == 1 && f.world.count[0] == 6);
    }
    for (unsigned kind = 0; kind < 2; ++kind)
    {
        CHECK(setup(&f));
        f.before[0].phase = PORTABLE_MESH_RACER_FINISHED;
        f.items.count = 1;
        f.items.values[0] = (PortableMeshItemEvent){.kind = kind ? PORTABLE_MESH_EVENT_HAUNT
                                                                 : PORTABLE_MESH_EVENT_HIT,
                                                    .racer = 1,
                                                    .target = 0};
        saved = f.world;
        memset(&f.events, 0x5a, sizeof f.events);
        PortableMeshCourseCoinEvents old = f.events;
        CHECK(!turn(&f, 1, 0) && !memcmp(&f.world, &saved, sizeof saved) &&
              !memcmp(&f.events, &old, sizeof old));
    }
    CHECK(setup(&f));
    f.items.count = 2;
    f.items.values[0] =
        (PortableMeshItemEvent){.kind = PORTABLE_MESH_EVENT_HIT, .racer = 1, .target = 0};
    f.items.values[1] =
        (PortableMeshItemEvent){.kind = PORTABLE_MESH_EVENT_HIT, .racer = 1, .target = 7};
    saved = f.world;
    memset(&f.events, 0xa5, sizeof f.events);
    PortableMeshCourseCoinEvents old = f.events;
    CHECK(!turn(&f, 1, 0) && !memcmp(&f.world, &saved, sizeof saved) &&
          !memcmp(&f.events, &old, sizeof old));
    return 0;
}
int main(void)
{
    CHECK(!source_haunt_order());
    CHECK(!haunt_order());
    CHECK(!recovery_contact());
    CHECK(!source_and_item_damage());
    CHECK(!phases_modes_rollback());
    return 0;
}
