#include "mesh_items.h"
#include "mesh_items_state.h"
#include "mesh_course_fixture.h"
#include "mesh_item_fixture.h"
#include "mesh_item_rules_asset.h"
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
#define Q 65536
#define OK PORTABLE_MESH_ITEMS_OK

typedef struct
{
    PortableTestMeshCourse floor;
    PortableMeshItemRules rules;
    PortableMeshItemsConfig config;
    PortableMeshItems world;
    PortableMeshRacerState racers[8];
    PortableMeshItemControls controls[8];
    uint8_t standings[8];
    PortableMeshKartEffects effects[8];
    PortableMeshItemEvents events;
} Fixture;
static int setup(Fixture *f, unsigned count)
{
    memset(f, 0, sizeof *f);
    test_mesh_course_init(&f->floor);
    test_mesh_word(f->floor.meta + 12, 31);
    uint8_t bytes[PORTABLE_MESH_ITEM_RULES_BYTES];
    test_mir_payload(bytes);
    const PortableAssetSectionView section = {
        .kind = 103, .content_id = 41, .version = 1, .bytes = bytes, .size = sizeof bytes};
    if (!portable_mesh_item_rules_asset_open(&section, &f->rules))
        return 0;
    f->rules.actors_lifetime_ticks = 120;
    f->rules.actors_owner_grace_ticks = 30;
    f->rules.actors_hold_distance_q16 = Q;
    f->rules.actors_hold_height_q16 = 0;
    f->rules.actors_radius_q16 = Q / 4;
    f->rules.actors_orbit_radius_q16 = 2 * Q;
    f->rules.actors_orbit_turn_rate_q16 = Q / 2;
    f->rules.actors_orbit_delay_0 = 0;
    f->rules.actors_orbit_delay_1 = 2;
    f->rules.actors_orbit_delay_2 = 4;
    f->rules.shell_speed_q16 = 12 * Q;
    f->rules.shell_acceleration_q16 = Q;
    f->rules.shell_ground_probe_q16 = Q;
    f->rules.shell_ground_snap_q16 = Q / 4;
    f->rules.shell_restitution_q16 = Q;
    f->rules.shell_homing_range_q16 = 16 * Q;
    f->rules.shell_homing_turn_rate_q16 = Q;
    f->rules.shell_trap_arm_ticks = 3;
    f->rules.shell_trap_lock_ticks = 2;
    f->rules.shell_trap_lifetime_ticks = 60;
    f->rules.banana_throw_speed_q16 = 4 * Q;
    f->rules.banana_throw_up_speed_q16 = 2 * Q;
    f->rules.banana_gravity_q16 = 10 * Q;
    f->rules.mushroom_boost_ticks = 3;
    f->rules.mushroom_golden_ticks = 4;
    f->rules.star_player_ticks = 5;
    f->rules.star_cpu_ticks = 5;
    f->rules.boo_theft_lock_ticks = 3;
    f->rules.boo_invisibility_ticks = 6;
    f->rules.cpu_decision_ticks = 2;
    f->rules.cpu_hold_ticks = 3;
    f->rules.cpu_use_distance_q16 = 12 * Q;
    f->rules.cpu_backward_distance_q16 = 8 * Q;
    f->rules.hit_spin_ticks = 3;
    f->rules.hit_invulnerability_ticks = 5;
    f->rules.hit_speed_ratio_q16 = Q / 2;
    f->rules.hit_impulse_q16 = Q;
    f->rules.lightning_speed_ratio_q16 = Q / 2;
    f->rules.lightning_scale_ratio_q16 = Q / 2;
    f->config = (PortableMeshItemsConfig){&f->floor.course, &f->rules,
                                          &f->floor.rules,  8,
                                          (uint8_t)count,   (uint8_t)((1u << count) - 1u),0,0, NULL};
    const int32_t direction[3] = {0, -1073741824, 0};
    for (unsigned i = 0; i < count; ++i)
    {
        const int32_t position[3] = {(int32_t)i * 4 * Q, 0, Q};
        if (portable_mesh_kart_init(&f->racers[i].kart, &f->floor.rules, position, direction) !=
            PORTABLE_MESH_OK)
            return 0;
        f->racers[i].phase = PORTABLE_MESH_RACER_RACING;
        f->racers[i].kart.grounded = 1;
        f->standings[i] = (uint8_t)i;
    }
    return portable_mesh_items_init(&f->world, &f->config, 123, NULL, 0) == OK;
}
static void give(Fixture *f, unsigned racer, uint16_t item)
{
    f->world.inventory.racers[racer] =
        (PortableMeshItemRacer){.held_item = item, .settled_item = item, .preview_item = item};
}
static int prepare(Fixture *f)
{
    char error[256];
    PortableMeshItemsResult result =
        portable_mesh_items_prepare_tick(&f->world, &f->config, f->racers, f->controls,
                                         f->standings, f->effects, &f->events, error, sizeof error);
    if (result != OK)
        fprintf(stderr, "prepare: %s\n", error);
    return result == OK;
}
static int resolve(Fixture *f)
{
    char error[256];
    PortableMeshItemsResult result = portable_mesh_items_resolve_after_move(
        &f->world, &f->config, f->racers, &f->events, error, sizeof error);
    if (result != OK)
        fprintf(stderr, "resolve: %s\n", error);
    return result == OK;
}
static unsigned actors(const Fixture *f, unsigned phase)
{
    unsigned n = 0;
    for (unsigned i = 0; i < 64; ++i)
        n += f->world.actors[i].phase && (phase == 99 || f->world.actors[i].phase == phase);
    return n;
}
static unsigned events(const Fixture *f, unsigned kind)
{
    unsigned n = 0;
    for (unsigned i = 0; i < f->events.count; ++i)
        n += f->events.values[i].kind == kind;
    return n;
}
static int natural_box_roulette(void)
{
    Fixture f;
    CHECK(setup(&f, 1));
    uint8_t box[32] = {0};
    test_mir_word(box + 8, Q);
    test_mir_word(box + 12, Q / 2);
    test_mir_word(box + 20, 90);
    f.floor.course.tables[f.floor.course.table_count++] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('I', 'T', 'E', 'M'), 1, 32, box};
    for (unsigned rank = 0; rank < 8; ++rank)
        f.rules.player_weights[rank][0] = (PortableGpItemWeight){0x100c, 1};
    CHECK(portable_mesh_items_init(&f.world, &f.config, 123, NULL, 0) == OK);
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    CHECK(events(&f, PORTABLE_MESH_EVENT_PICKUP) == 1);
    CHECK(f.world.inventory.racers[0].active && f.world.boxes.cooldowns[0] == 90);
    unsigned settled = 0;
    for (unsigned t = 0; t < 80; ++t)
    {
        CHECK(prepare(&f));
        CHECK(resolve(&f));
        if (!f.world.inventory.racers[0].active)
        {
            settled = 1;
            break;
        }
    }
    CHECK(settled && f.world.inventory.racers[0].held_item == 0x100c);
    f.controls[0].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(!f.world.inventory.racers[0].held_item);
    CHECK(events(&f, PORTABLE_MESH_EVENT_USE) == 1);
    CHECK(resolve(&f));
    return 0;
}

static int canonical_uses(void)
{
    Fixture f;
    for (unsigned item = 0x1000; item <= 0x100c; ++item)
    {
        CHECK(setup(&f, 1));
        give(&f, 0, (uint16_t)item);
        f.controls[0].item_pressed = 1;
        CHECK(prepare(&f));
        CHECK(events(&f, PORTABLE_MESH_EVENT_USE) == 1);
        if (item == 0x1001 || item == 0x1003)
            CHECK(actors(&f, 99) == 3);
        else if (item <= 0x1005)
            CHECK(actors(&f, PORTABLE_MESH_ACTOR_HELD) == 1);
        if (item == 0x1008)
            CHECK(f.world.inventory.racers[0].held_item == 0x100f);
        else if (item == 0x1009)
            CHECK(f.world.inventory.racers[0].held_item == 0x100e);
        else
            CHECK(!f.world.inventory.racers[0].held_item);
        if (item == 0x1006)
            CHECK(!f.effects[0].boost_active && !actors(&f, 99));
        if (item >= 0x1007 && item <= 0x1009)
            CHECK(f.effects[0].boost_active);
        if (item == 0x100a)
            CHECK(f.world.status.racers[0].star_ticks == 5);
        if (item == 0x100b)
            CHECK(f.world.status.racers[0].boo_invisible_ticks == 6 &&
                  !events(&f, PORTABLE_MESH_EVENT_THEFT));
        CHECK(resolve(&f));
    }
    /* Triple successors use the normal fresh-edge path, never fabricated stock. */
    CHECK(setup(&f, 1));
    give(&f, 0, 0x1008);
    for (unsigned use = 0; use < 3; ++use)
    {
        f.controls[0].item_pressed = 1;
        CHECK(prepare(&f));
        CHECK(resolve(&f));
        CHECK(f.world.inventory.racers[0].held_item == (use == 0 ? 0x100f : use == 1 ? 0x1007 : 0));
        f.controls[0].item_pressed = 0;
        CHECK(prepare(&f));
        CHECK(resolve(&f));
    }
    /* Holding the button spends one mushroom, not the whole stack. */
    CHECK(setup(&f, 1));
    give(&f, 0, 0x1008);
    f.controls[0].item_pressed = 1;
    for (unsigned tick = 0; tick < 4; ++tick)
    {
        CHECK(prepare(&f));
        CHECK(resolve(&f));
        CHECK(f.world.inventory.racers[0].held_item == 0x100f);
    }
    return 0;
}
static int attachments_motion_and_refusal(void)
{
    Fixture f;
    CHECK(setup(&f, 1));
    give(&f, 0, 0x1000);
    f.controls[0].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    CHECK(actors(&f, PORTABLE_MESH_ACTOR_HELD) == 1);
    f.racers[0].kart.position_q16[0] += Q;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    CHECK(f.world.actors[0].motion.position_q16[0] == Q);
    f.controls[0].item_pressed = 0;
    f.controls[0].aim_backward = 1;
    CHECK(prepare(&f));
    CHECK(f.world.actors[0].motion.velocity_q16[1] > 0);
    CHECK(resolve(&f));
    CHECK(f.world.actors[0].motion.position_q16[1] > Q);
    CHECK(setup(&f, 1));
    give(&f, 0, 0x1005);
    f.controls[0].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    f.controls[0].item_pressed = 0;
    f.controls[0].aim_forward = 1;
    CHECK(prepare(&f));
    CHECK(f.world.actors[0].motion.velocity_q16[2] > 0);
    CHECK(resolve(&f));
    for (unsigned tick = 0; tick < 80 && actors(&f, 99); ++tick)
    {
        CHECK(prepare(&f));
        CHECK(resolve(&f));
    }
    CHECK(actors(&f, 99) == 1 && f.world.actors[0].motion.grounded &&
          f.world.actors[0].motion.velocity_q16[2] == 0);
    CHECK(setup(&f, 1));
    give(&f, 0, 0x1001);
    f.controls[0].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    for (unsigned t = 0; t < 5; ++t)
    {
        f.controls[0].item_pressed = 0;
        CHECK(prepare(&f));
        CHECK(resolve(&f));
    }
    CHECK(actors(&f, PORTABLE_MESH_ACTOR_ORBIT) == 3);
    f.controls[0].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(events(&f, PORTABLE_MESH_EVENT_RELEASE) == 1);
    CHECK(resolve(&f));
    CHECK(actors(&f, PORTABLE_MESH_ACTOR_ORBIT) == 2);
    CHECK(prepare(&f));
    CHECK(!events(&f, PORTABLE_MESH_EVENT_RELEASE));
    CHECK(resolve(&f));
    /* A shared auxiliary occupies the sole slot; refused use preserves inventory/RNG. */
    CHECK(setup(&f, 1));
    f.rules.actors_capacity = 1;
    give(&f, 0, 0x1009);
    f.controls[0].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    give(&f, 0, 0x1000);
    f.controls[0].item_pressed = 0;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    const uint32_t seed = f.world.inventory.seed;
    f.controls[0].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(f.world.inventory.racers[0].held_item == 0x1000 && f.world.inventory.seed == seed &&
          !actors(&f, 99));
    return 0;
}
static int cpu_and_expiry(void)
{
    Fixture f;
    CHECK(setup(&f, 2));
    f.config.human_mask = 1;
    CHECK(portable_mesh_items_init(&f.world, &f.config, 123, NULL, 0) == OK);
    give(&f, 1, 0x1000);
    CHECK(prepare(&f));
    CHECK(actors(&f, PORTABLE_MESH_ACTOR_HELD) == 1);
    CHECK(resolve(&f));
    for (unsigned t = 0; t < 2; ++t)
    {
        CHECK(prepare(&f));
        CHECK(actors(&f, PORTABLE_MESH_ACTOR_HELD) == 1);
        CHECK(resolve(&f));
    }
    CHECK(prepare(&f));
    CHECK(events(&f, PORTABLE_MESH_EVENT_RELEASE) == 1);
    CHECK(resolve(&f));
    CHECK(actors(&f, PORTABLE_MESH_ACTOR_FLYING) == 1);
    CHECK(setup(&f, 1));
    give(&f, 0, 0x1009);
    f.controls[0].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    f.controls[0].item_pressed = 0;
    give(&f, 0, 0x1002);
    for (unsigned t = 1; t <= 4; ++t)
    {
        CHECK(prepare(&f));
        CHECK(events(&f, PORTABLE_MESH_EVENT_GOLDEN_EXPIRED) == (t == 4 ? 1u : 0u));
        if (t < 4)
            CHECK(f.world.inventory.racers[0].held_item == 0x1002);
        else
            CHECK(!f.world.inventory.racers[0].held_item);
        CHECK(resolve(&f));
    }
    return 0;
}

static int presentation_events(void)
{
    Fixture f;
    CHECK(setup(&f, 1));
    give(&f, 0, 0x1007);
    f.controls[0].item_pressed = 1;
    f.racers[0].kart.grounded = 0;
    CHECK(prepare(&f));
    CHECK(!events(&f, PORTABLE_MESH_EVENT_BOOST) && f.world.status.racers[0].pending_mushroom);
    CHECK(resolve(&f));
    f.controls[0].item_pressed = 0;
    for (unsigned t = 0; t < 2; ++t)
    {
        CHECK(prepare(&f));
        CHECK(resolve(&f));
        CHECK(!events(&f, PORTABLE_MESH_EVENT_BOOST));
    }
    f.racers[0].kart.grounded = 1;
    CHECK(prepare(&f));
    CHECK(events(&f, PORTABLE_MESH_EVENT_BOOST) == 1);
    const PortableMeshItems frozen = f.world;
    const PortableMeshItemEvents frozen_events = f.events;
    for (unsigned view = 0; view < 4; ++view)
    {
        uint16_t hud;
        PortableMeshItemPose poses[64];
        CHECK(portable_mesh_items_hud(&f.world, 0, &hud) == OK);
        (void)portable_mesh_items_actor_poses(&f.world, poses, 64);
    }
    CHECK(!memcmp(&f.world, &frozen, sizeof frozen) &&
          !memcmp(&f.events, &frozen_events, sizeof frozen_events));
    for (unsigned i = 0; i < f.events.count; ++i)
        if (f.events.values[i].kind == PORTABLE_MESH_EVENT_BOOST)
            CHECK(!f.events.values[i].serial && f.events.values[i].actor == UINT32_MAX &&
                  f.events.values[i].racer == 0);
    CHECK(resolve(&f));
    CHECK(events(&f, PORTABLE_MESH_EVENT_BOOST) == 1);
    CHECK(prepare(&f));
    CHECK(!events(&f, PORTABLE_MESH_EVENT_BOOST));
    CHECK(resolve(&f));
    CHECK(setup(&f, 1));
    give(&f, 0, 0x1001);
    f.controls[0].item_pressed = 1;
    unsigned ready = 0, serials = 0;
    for (unsigned tick = 0; tick < 10; ++tick)
    {
        CHECK(prepare(&f));
        CHECK(resolve(&f));
        for (unsigned i = 0; i < f.events.count; ++i)
            if (f.events.values[i].kind == PORTABLE_MESH_EVENT_ORBIT_READY)
            {
                const unsigned serial = f.events.values[i].serial;
                CHECK(serial >= 1 && serial <= 3 && !(serials & (1u << serial)));
                serials |= 1u << serial;
                ++ready;
            }
    }
    CHECK(ready == 3 && serials == 14);
    CHECK(setup(&f, 1));
    give(&f, 0, 0x1002);
    f.controls[0].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    const uint32_t serial = f.world.actors[0].serial;
    f.controls[0].item_pressed = 0;
    f.controls[0].aim_backward = 1;
    CHECK(prepare(&f));
    CHECK(events(&f, PORTABLE_MESH_EVENT_RELEASE) == 1);
    for (unsigned i = 0; i < f.events.count; ++i)
        if (f.events.values[i].kind == PORTABLE_MESH_EVENT_RELEASE)
            CHECK(f.events.values[i].serial == serial &&
                  f.events.values[i].flags == PORTABLE_MESH_ITEM_EVENT_BACKWARD);
    CHECK(resolve(&f));
    CHECK(setup(&f, 1));
    give(&f, 0, 0x1005);
    f.controls[0].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    f.controls[0].item_pressed = 0;
    f.controls[0].aim_forward = 1;
    CHECK(prepare(&f));
    for (unsigned i = 0; i < f.events.count; ++i)
        if (f.events.values[i].kind == PORTABLE_MESH_EVENT_RELEASE)
            CHECK(f.events.values[i].flags == PORTABLE_MESH_ITEM_EVENT_FORWARD_THROW);
    CHECK(resolve(&f));
    unsigned landed = events(&f, PORTABLE_MESH_EVENT_LANDED);
    for (unsigned tick = 0; tick < 80; ++tick)
    {
        CHECK(prepare(&f));
        CHECK(resolve(&f));
        landed += events(&f, PORTABLE_MESH_EVENT_LANDED);
    }
    CHECK(landed == 1);
    /* Silent lifetime cleanup retains the destroyed actor generation. */
    CHECK(setup(&f, 1));
    give(&f, 0, 0x1000);
    f.controls[0].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    f.world.actors[0].lifetime = 1;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    CHECK(events(&f, PORTABLE_MESH_EVENT_DESTROY) == 1 && !events(&f, PORTABLE_MESH_EVENT_IMPACT));
    const uint32_t old_serial = f.events.values[f.events.count - 1].serial;
    CHECK(old_serial == 1);
    f.controls[0].item_pressed = 0;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    give(&f, 0, 0x1000);
    f.controls[0].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(f.world.actors[0].serial == 2);
    CHECK(resolve(&f));
    CHECK(old_serial != f.world.actors[0].serial);
    return 0;
}

static int wall_impact_event(void)
{
    Fixture f;
    CHECK(setup(&f, 1));
    uint8_t vertices[84] = {0}, triangles[96] = {0};
    memcpy(vertices, f.floor.vertices, sizeof f.floor.vertices);
    memcpy(triangles, f.floor.triangles, sizeof f.floor.triangles);
    const int32_t wall[3][3] = {{-10 * Q, -2 * Q, 0}, {10 * Q, -2 * Q, 0}, {0, -2 * Q, 10 * Q}};
    for (unsigned i = 0; i < 3; ++i)
    {
        for (unsigned axis = 0; axis < 3; ++axis)
            test_mesh_word(vertices + (i + 4) * 12 + axis * 4, (uint32_t)wall[i][axis]);
        test_mesh_word(triangles + 64 + i * 4, i + 4);
    }
    test_mesh_word(triangles + 64 + 20, UINT32_MAX);
    test_mesh_word(triangles + 64 + 24, UINT32_MAX);
    test_mesh_word(triangles + 64 + 28, UINT32_MAX);
    for (unsigned i = 0; i < f.floor.course.table_count; ++i)
    {
        PortableCourse3DTable *table = &f.floor.course.tables[i];
        if (table->tag == PORTABLE_COURSE_3D_TAG('C', 'O', 'L', 'V'))
        {
            table->bytes = vertices;
            table->count = 7;
        }
        if (table->tag == PORTABLE_COURSE_3D_TAG('C', 'O', 'L', 'T'))
        {
            table->bytes = triangles;
            table->count = 3;
        }
    }
    CHECK(portable_mesh_items_init(&f.world, &f.config, 123, NULL, 0) == OK);
    give(&f, 0, 0x1000);
    f.controls[0].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    f.controls[0].item_pressed = 0;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    unsigned impacts = events(&f, PORTABLE_MESH_EVENT_IMPACT);
    for (unsigned tick = 0; tick < 25 && !impacts; ++tick)
    {
        CHECK(prepare(&f));
        CHECK(resolve(&f));
        impacts += events(&f, PORTABLE_MESH_EVENT_IMPACT);
    }
    CHECK(impacts == 1);
    for (unsigned i = 0; i < f.events.count; ++i)
        if (f.events.values[i].kind == PORTABLE_MESH_EVENT_IMPACT)
            CHECK(f.events.values[i].serial == 1 && f.events.values[i].actor == 0 &&
                  f.events.values[i].item == 0x1000);
    return 0;
}

static int airborne_traps(void)
{
    for (unsigned locking = 0; locking < 2; ++locking)
    {
        Fixture f;
        CHECK(setup(&f, locking ? 2 : 1));
        f.rules.actors_lifetime_ticks = 180;
        f.rules.shell_trap_lifetime_ticks = 120;
        f.rules.shell_trap_lock_ticks = 90;
        f.rules.shell_homing_range_q16 = locking ? 16 * Q : 1;
        for (unsigned i = 0; i < f.config.count; ++i)
        {
            f.racers[i].kart.position_q16[2] = 5 * Q;
            f.racers[i].kart.grounded = 0;
        }
        give(&f, 0, 0x1002);
        f.controls[0].item_pressed = 1;
        CHECK(prepare(&f));
        CHECK(resolve(&f));
        f.controls[0].item_pressed = 0;
        f.controls[0].aim_backward = 1;
        CHECK(prepare(&f));
        CHECK(f.world.actors[0].phase == PORTABLE_MESH_ACTOR_TRAP_ARMING);
        CHECK(resolve(&f));
        for (unsigned tick = 0; tick < 2; ++tick)
        {
            CHECK(prepare(&f));
            CHECK(resolve(&f));
        }
        CHECK(f.world.actors[0].phase == PORTABLE_MESH_ACTOR_TRAP_WAITING &&
              !f.world.actors[0].motion.grounded);
        const int32_t height = f.world.actors[0].motion.position_q16[2];
        const int32_t velocity = f.world.actors[0].motion.velocity_q16[2];
        CHECK(velocity < 0 && height < 5 * Q);
        CHECK(prepare(&f));
        CHECK(resolve(&f));
        CHECK(f.world.actors[0].phase ==
              (locking ? PORTABLE_MESH_ACTOR_TRAP_LOCKING : PORTABLE_MESH_ACTOR_TRAP_WAITING));
        CHECK(f.world.actors[0].motion.position_q16[2] < height &&
              f.world.actors[0].motion.velocity_q16[2] < velocity &&
              !f.world.actors[0].motion.velocity_q16[0] &&
              !f.world.actors[0].motion.velocity_q16[1]);
        const int32_t x = f.world.actors[0].motion.position_q16[0],
                      y = f.world.actors[0].motion.position_q16[1];
        unsigned landed = 0;
        for (unsigned tick = 0; tick < 80; ++tick)
        {
            CHECK(prepare(&f));
            CHECK(resolve(&f));
            CHECK(actors(&f, 99) == 1);
            if (f.world.actors[0].motion.grounded)
            {
                landed = 1;
                break;
            }
        }
        CHECK(landed && f.world.actors[0].motion.position_q16[2] == f.rules.actors_radius_q16);
        CHECK(f.world.actors[0].phase ==
              (locking ? PORTABLE_MESH_ACTOR_TRAP_LOCKING : PORTABLE_MESH_ACTOR_TRAP_WAITING));
        for (unsigned tick = 0; tick < 3; ++tick)
        {
            CHECK(prepare(&f));
            CHECK(resolve(&f));
            CHECK(f.world.actors[0].motion.grounded &&
                  f.world.actors[0].motion.position_q16[2] == f.rules.actors_radius_q16 &&
                  f.world.actors[0].motion.position_q16[0] == x &&
                  f.world.actors[0].motion.position_q16[1] == y);
        }
    }
    return 0;
}

static int time_trial_inventory(void)
{
    Fixture f;
    CHECK(setup(&f, 1));
    uint8_t box[32] = {0};
    test_mir_word(box + 8, Q);
    test_mir_word(box + 12, Q / 2);
    test_mir_word(box + 20, 90);
    f.floor.course.tables[f.floor.course.table_count++] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('I', 'T', 'E', 'M'), 1, 32, box};
    f.config.mode = 2;
    f.config.human_mask = 1;
    CHECK(portable_mesh_items_init(&f.world, &f.config, 123, NULL, 0) == OK);
    CHECK(f.world.inventory.racers[0].held_item == 0x1008 && f.world.boxes.count == 1);
    f.world.boxes.cooldowns[0] = 3; /* TT must not even step an existing box clock. */
    for (unsigned use = 0; use < 3; ++use)
    {
        f.controls[0].item_pressed = 1;
        CHECK(prepare(&f));
        CHECK(resolve(&f));
        CHECK(f.world.inventory.racers[0].held_item == (use == 0 ? 0x100f : use == 1 ? 0x1007 : 0));
        CHECK(!events(&f, PORTABLE_MESH_EVENT_PICKUP) && f.world.boxes.cooldowns[0] == 3 &&
              f.world.inventory.seed == 123);
        f.controls[0].item_pressed = 0;
        CHECK(prepare(&f));
        CHECK(resolve(&f));
    }
    for (unsigned tick = 0; tick < 4; ++tick)
    {
        CHECK(prepare(&f));
        CHECK(resolve(&f));
        CHECK(!f.world.inventory.racers[0].held_item && !events(&f, PORTABLE_MESH_EVENT_PICKUP) &&
              f.world.boxes.cooldowns[0] == 3 && f.world.inventory.seed == 123);
    }
    CHECK(portable_mesh_items_init(&f.world, &f.config, 123, NULL, 0) == OK);
    CHECK(f.world.inventory.racers[0].held_item == 0x1008 && !f.world.boxes.cooldowns[0] &&
          !f.world.tick && f.world.inventory.seed == 123);
    return 0;
}

static int targets_boo_lightning(void)
{
    Fixture f;
    CHECK(setup(&f, 3));
    f.standings[0] = 2;
    f.standings[1] = 0;
    f.standings[2] = 1;
    give(&f, 1, 0x1002);
    f.controls[1].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    f.controls[1].item_pressed = 0;
    CHECK(prepare(&f));
    CHECK(f.world.actors[0].target == 0);
    CHECK(resolve(&f));
    CHECK(setup(&f, 3));
    give(&f, 2, 0x1004);
    f.controls[2].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    f.controls[2].item_pressed = 0;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    CHECK(f.world.actors[0].target == 0);
    f.standings[0] = 1;
    f.standings[1] = 0;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    CHECK(f.world.actors[0].target == 1);
    CHECK(setup(&f, 3));
    give(&f, 2, 0x100b);
    give(&f, 0, 0x100e);
    give(&f, 1, 0x1002);
    f.controls[2].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(f.world.inventory.racers[2].held_item == 0x1009 &&
          !f.world.inventory.racers[0].held_item &&
          f.world.inventory.racers[1].held_item == 0x1002 &&
          actors(&f, PORTABLE_MESH_ACTOR_HAUNT) == 1);
    CHECK(resolve(&f));
    f.controls[2].item_pressed = 0;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    CHECK(f.world.status.racers[0].boo_lock_ticks);
    CHECK(setup(&f, 3));
    give(&f, 0, 0x100c);
    f.controls[0].item_pressed = 1;
    CHECK(portable_mesh_item_status_activate(&f.world.status, &f.rules, 1,
                                             PORTABLE_MESH_STATUS_STAR) ==
          PORTABLE_MESH_ITEM_STATUS_OK);
    f.racers[2].kart.grounded = 0;
    CHECK(prepare(&f));
    CHECK(!f.world.status.racers[1].lightning_ticks &&
          f.world.status.racers[2].lightning_ticks == 48 && f.world.status.racers[2].spin_ticks &&
          events(&f, PORTABLE_MESH_EVENT_HIT) == 1);
    CHECK(resolve(&f));
    return 0;
}
static int actor_contacts(void)
{
    Fixture f;
    CHECK(setup(&f, 2));
    give(&f, 0, 0x1000);
    f.controls[0].item_pressed = 1;
    CHECK(prepare(&f));
    f.racers[1].kart.position_q16[0] = 0;
    f.racers[1].kart.position_q16[1] = Q;
    CHECK(resolve(&f));
    CHECK(!actors(&f, 99) && f.world.status.racers[1].spin_ticks &&
          events(&f, PORTABLE_MESH_EVENT_HIT) == 1);
    CHECK(setup(&f, 2));
    give(&f, 0, 0x1000);
    f.controls[0].item_pressed = 1;
    CHECK(portable_mesh_item_status_activate(&f.world.status, &f.rules, 1,
                                             PORTABLE_MESH_STATUS_STAR) ==
          PORTABLE_MESH_ITEM_STATUS_OK);
    CHECK(prepare(&f));
    f.racers[1].kart.position_q16[0] = 0;
    f.racers[1].kart.position_q16[1] = Q;
    CHECK(resolve(&f));
    CHECK(!actors(&f, 99) && !f.world.status.racers[1].spin_ticks &&
          events(&f, PORTABLE_MESH_EVENT_PROTECTED) == 1);
    /* Both held defenders collide before either kart's damage callback. */
    CHECK(setup(&f, 2));
    give(&f, 0, 0x1000);
    give(&f, 1, 0x1002);
    f.controls[0].item_pressed = f.controls[1].item_pressed = 1;
    CHECK(prepare(&f));
    f.racers[1].kart.position_q16[0] = 0;
    CHECK(resolve(&f));
    CHECK(!actors(&f, 99) && events(&f, PORTABLE_MESH_EVENT_DESTROY) == 2 &&
          !events(&f, PORTABLE_MESH_EVENT_HIT));
    CHECK(events(&f, PORTABLE_MESH_EVENT_IMPACT) == 1);
    unsigned destroyed = 0;
    for (unsigned i = 0; i < f.events.count; ++i)
        if (f.events.values[i].kind == PORTABLE_MESH_EVENT_DESTROY)
        {
            CHECK(f.events.values[i].serial == 1 || f.events.values[i].serial == 2);
            destroyed |= 1u << f.events.values[i].serial;
        }
    CHECK(destroyed == 6);

    /* Spiny hits a passing nonleader without exploding; leader contact retires it. */
    CHECK(setup(&f, 3));
    give(&f, 2, 0x1004);
    f.controls[2].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    f.controls[2].item_pressed = 0;
    CHECK(prepare(&f));
    f.world.actors[0].motion.position_q16[0] = 4 * Q;
    f.world.actors[0].motion.position_q16[1] = 0;
    f.world.actors[0].motion.position_q16[2] = Q / 2;
    memset(f.world.actors[0].motion.velocity_q16, 0, sizeof f.world.actors[0].motion.velocity_q16);
    f.racers[1].kart.position_q16[2] = Q / 2;
    CHECK(resolve(&f));
    CHECK(actors(&f, 99) == 1 && f.world.status.racers[1].spin_ticks &&
          !f.world.status.racers[0].spin_ticks);
    CHECK(prepare(&f));
    f.world.actors[0].motion.position_q16[0] = 0;
    f.world.actors[0].motion.position_q16[1] = 0;
    f.world.actors[0].motion.position_q16[2] = Q / 2;
    memset(f.world.actors[0].motion.velocity_q16, 0, sizeof f.world.actors[0].motion.velocity_q16);
    f.racers[0].kart.position_q16[2] = Q / 2;
    CHECK(resolve(&f));
    CHECK(!actors(&f, 99) && f.world.status.racers[0].spin_ticks);
    return 0;
}

static int recovery_admission_and_size(void)
{
    Fixture f;
    CHECK(setup(&f, 2));
    give(&f, 0, 0x1007);
    give(&f, 1, 0x100c);
    f.controls[0].item_pressed = f.controls[1].item_pressed = 1;
    f.controls[0].recovering = 1;
    CHECK(portable_mesh_item_status_activate(&f.world.status, &f.rules, 0,
                                             PORTABLE_MESH_STATUS_STAR) ==
          PORTABLE_MESH_ITEM_STATUS_OK);
    CHECK(prepare(&f));
    CHECK(f.racers[0].phase == PORTABLE_MESH_RACER_RACING);
    CHECK(f.world.inventory.racers[0].held_item == 0x1007 && !f.effects[0].boost_active &&
          f.effects[0].speed_ratio_q16 == Q && f.effects[0].body_scale_q16 == Q &&
          !f.world.status.racers[0].lightning_ticks);
    CHECK(resolve(&f));
    CHECK(setup(&f, 2));
    f.racers[1].kart.position_q16[0] = Q / 2;
    f.world.status.racers[1].lightning_ticks = 48;
    CHECK(prepare(&f));
    const int32_t first = f.racers[0].kart.position_q16[0],
                  second = f.racers[1].kart.position_q16[0];
    CHECK(portable_mesh_items_pair(&f.world, &f.config, f.racers, 0, 1, &f.events, NULL, 0) == OK);
    CHECK(!f.world.status.racers[0].spin_ticks && f.world.status.racers[1].spin_ticks &&
          f.racers[0].kart.position_q16[0] == first && f.racers[1].kart.position_q16[0] == second);
    CHECK(resolve(&f));
    CHECK(setup(&f, 2));
    f.racers[1].kart.position_q16[0] = Q / 2;
    for (unsigned i = 0; i < 2; ++i)
        CHECK(portable_mesh_item_status_activate(&f.world.status, &f.rules, i,
                                                 PORTABLE_MESH_STATUS_STAR) ==
              PORTABLE_MESH_ITEM_STATUS_OK);
    CHECK(prepare(&f));
    CHECK(portable_mesh_items_pair(&f.world, &f.config, f.racers, 0, 1, &f.events, NULL, 0) == OK);
    CHECK(!events(&f, PORTABLE_MESH_EVENT_HIT) && f.racers[0].kart.position_q16[0] != 0);
    CHECK(resolve(&f));
    CHECK(setup(&f, 1));
    const PortableMeshItems old = f.world;
    f.config.mode = 128;
    CHECK(portable_mesh_items_init(&f.world, &f.config, 123, NULL, 0) != OK &&
          !memcmp(&old, &f.world, sizeof old));
    return 0;
}

static int observed_pairs(void)
{
    static Fixture f, legacy;
    static const uint8_t expected[] = {3,0,0,0,0,0,0,0,2,0,0};
    for (unsigned scenario=0;scenario<sizeof expected;++scenario)
    {
        CHECK(setup(&f,2));
        f.racers[1].kart.position_q16[0]=Q/2;
        if (scenario==1) f.racers[1].kart.position_q16[0]=4*Q;
        if (scenario==2) f.racers[1].kart.position_q16[2]+=4*Q;
        if (scenario==10) f.racers[1].kart.position_q16[0]=2*f.floor.rules.kart_radius_q16;
        if (scenario==3 || scenario==4)
            f.world.status.racers[0].star_ticks=4;
        if (scenario==3) f.world.status.racers[1].star_ticks=4;
        if (scenario==5) f.world.status.racers[1].lightning_ticks=4;
        if (scenario==6) f.world.status.racers[0].boo_invisible_ticks=4;
        if (scenario==7) f.controls[0].recovering=1;
        CHECK(prepare(&f));
        if (scenario==8 || scenario==9)
        {
            f.world.status.racers[0].hit_grace_ticks=2;
            f.world.status.racers[1].hit_grace_ticks=scenario==8 ? 1 : 2;
        }
        legacy=f; /* Both copies borrow the same immutable fixture course/rules. */
        uint8_t observed=0xa5;
        CHECK(portable_mesh_items_pair_observed(&f.world,&f.config,f.racers,0,1,
                &f.events,&observed,NULL,0)==OK && observed==expected[scenario]);
        CHECK(portable_mesh_items_pair(&legacy.world,&legacy.config,legacy.racers,0,1,
                &legacy.events,NULL,0)==OK);
        CHECK(!memcmp(&legacy.world,&f.world,sizeof f.world) &&
              !memcmp(legacy.racers,f.racers,sizeof f.racers) &&
              !memcmp(&legacy.events,&f.events,sizeof f.events));
        if (scenario==0)
            CHECK(f.racers[0].kart.position_q16[0]!=0 && !events(&f,PORTABLE_MESH_EVENT_HIT));
        if (scenario==4 || scenario==5) CHECK(events(&f,PORTABLE_MESH_EVENT_HIT)==1);
    }
    CHECK(setup(&f,2));
    f.racers[1].kart.position_q16[0]=Q/2;
    f.world.status.racers[0].star_ticks=4;
    CHECK(prepare(&f));
    f.events.count=PORTABLE_MESH_ITEMS_EVENTS; /* Damage cannot append its HIT. */
    legacy=f;
    uint8_t observed=0xa5;
    CHECK(portable_mesh_items_pair_observed(&f.world,&f.config,f.racers,0,1,
                &f.events,&observed,NULL,0)!=OK && observed==0xa5);
    CHECK(!memcmp(&legacy.world,&f.world,sizeof f.world) &&
          !memcmp(legacy.racers,f.racers,sizeof f.racers) &&
          !memcmp(&legacy.events,&f.events,sizeof f.events));
    CHECK(portable_mesh_items_pair_observed(&f.world,&f.config,f.racers,1,0,
                &f.events,&observed,NULL,0)!=OK && observed==0xa5);
    CHECK(!memcmp(&legacy.world,&f.world,sizeof f.world) &&
          !memcmp(legacy.racers,f.racers,sizeof f.racers) &&
          !memcmp(&legacy.events,&f.events,sizeof f.events));
    return 0;
}

static int pairs_and_rollback(void)
{
    Fixture f;
    CHECK(setup(&f, 2));
    f.racers[1].kart.position_q16[0] = Q / 2;
    CHECK(portable_mesh_item_status_activate(&f.world.status, &f.rules, 0,
                                             PORTABLE_MESH_STATUS_BOO_INVISIBLE) ==
          PORTABLE_MESH_ITEM_STATUS_OK);
    CHECK(prepare(&f));
    const PortableMeshKartState first = f.racers[0].kart, second = f.racers[1].kart;
    CHECK(portable_mesh_items_pair(&f.world, &f.config, f.racers, 0, 1, &f.events, NULL, 0) == OK);
    CHECK(!memcmp(&first, &f.racers[0].kart, sizeof first) &&
          !memcmp(&second, &f.racers[1].kart, sizeof second));
    CHECK(resolve(&f));
    CHECK(setup(&f, 2));
    f.racers[1].kart.position_q16[0] = Q / 2;
    give(&f, 0, 0x100a);
    f.controls[0].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(portable_mesh_items_pair(&f.world, &f.config, f.racers, 0, 1, &f.events, NULL, 0) == OK);
    CHECK(!f.world.status.racers[0].spin_ticks && f.world.status.racers[1].spin_ticks &&
          events(&f, PORTABLE_MESH_EVENT_HIT) == 1);
    CHECK(portable_mesh_items_pair(&f.world, &f.config, f.racers, 0, 1, &f.events, NULL, 0) == OK);
    CHECK(events(&f, PORTABLE_MESH_EVENT_HIT) == 1);
    CHECK(resolve(&f));
    CHECK(setup(&f, 2));
    give(&f, 0, 0x1000);
    f.controls[0].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    f.controls[0].item_pressed = 0;
    CHECK(prepare(&f));
    /* Malformed late terrain must preserve prepared world/racers/events entirely. */
    test_mesh_word(f.floor.triangles + 32, 99);
    const PortableMeshItems saved = f.world;
    PortableMeshRacerState saved_racers[8];
    memcpy(saved_racers, f.racers, sizeof saved_racers);
    const PortableMeshItemEvents saved_events = f.events;
    CHECK(portable_mesh_items_resolve_after_move(&f.world, &f.config, f.racers, &f.events, NULL,
                                                 0) != OK);
    CHECK(!memcmp(&f.world, &saved, sizeof saved) &&
          !memcmp(f.racers, saved_racers, sizeof saved_racers) &&
          !memcmp(&f.events, &saved_events, sizeof saved_events));
    CHECK(setup(&f, 2));
    f.standings[1] = 0;
    const PortableMeshItems unprepared = f.world;
    CHECK(portable_mesh_items_prepare_tick(&f.world, &f.config, f.racers, f.controls, f.standings,
                                           f.effects, &f.events, NULL, 0) != OK);
    CHECK(!memcmp(&unprepared, &f.world, sizeof unprepared));
    return 0;
}
static int launch_grace(void)
{
    for(unsigned orbit=0;orbit<2;++orbit)
    {
        Fixture f;CHECK(setup(&f,2));
        f.racers[1].kart.position_q16[0]=12*Q;
        give(&f,0,orbit?0x1001:0x1002);
        f.controls[0].item_pressed=1;
        for(unsigned tick=0;tick<f.rules.actors_owner_grace_ticks+6u;++tick)
        {
            CHECK(prepare(&f));CHECK(resolve(&f));
        }
        CHECK(f.world.actors[0].grace==0);
        if(orbit)
        {
            f.controls[0].item_pressed=0;
            CHECK(prepare(&f));CHECK(resolve(&f));
        }
        const uint32_t serial=f.world.actors[0].serial;
        const uint16_t lifetime=f.world.actors[0].lifetime;
        f.controls[0].item_pressed=(uint8_t)orbit;
        f.controls[0].aim_backward=(uint8_t)!orbit;
        /* Invalid release controls preserve all staged owners and events. */
        PortableMeshItems before=f.world;
        PortableMeshItemEvents old_events=f.events;
        PortableMeshRacerState old_racers[8];memcpy(old_racers,f.racers,sizeof old_racers);
        PortableMeshKartEffects old_effects[8];memcpy(old_effects,f.effects,sizeof old_effects);
        f.controls[0].aim_forward=2;
        CHECK(portable_mesh_items_prepare_tick(&f.world,&f.config,f.racers,f.controls,
            f.standings,f.effects,&f.events,NULL,0)!=OK);
        CHECK(!memcmp(&before,&f.world,sizeof before) &&
              !memcmp(&old_events,&f.events,sizeof old_events) &&
              !memcmp(old_racers,f.racers,sizeof old_racers) &&
              !memcmp(old_effects,f.effects,sizeof old_effects));
        f.controls[0].aim_forward=0;
        CHECK(prepare(&f));
        CHECK(events(&f,PORTABLE_MESH_EVENT_RELEASE)==1 &&
              f.world.actors[0].grace==f.rules.actors_owner_grace_ticks &&
              f.world.actors[0].serial==serial && f.world.actors[0].lifetime==lifetime);
        CHECK(resolve(&f));
        CHECK(f.world.actors[0].grace==f.rules.actors_owner_grace_ticks-1u);
        /* Place the accepted released actor and stationary owner together on
         * real floor support every turn, making immunity independent of aim. */
        f.controls[0].item_pressed=0;
        f.racers[0].kart.position_q16[2]=f.rules.actors_radius_q16;
        while(f.world.actors[0].grace)
        {
            CHECK(f.world.actors[0].phase!=PORTABLE_MESH_ACTOR_FREE);
            memcpy(f.world.actors[0].motion.position_q16,f.racers[0].kart.position_q16,
                   sizeof f.world.actors[0].motion.position_q16);
            memset(f.world.actors[0].motion.velocity_q16,0,
                   sizeof f.world.actors[0].motion.velocity_q16);
            CHECK(prepare(&f));CHECK(resolve(&f));
            unsigned owner_hits=0;
            for(unsigned i=0;i<f.events.count;++i)
                owner_hits+=f.events.values[i].kind==PORTABLE_MESH_EVENT_HIT &&
                            f.events.values[i].target==0;
            if(f.world.actors[0].grace)CHECK(owner_hits==0);
            else CHECK(owner_hits==1 && f.world.status.racers[0].spin_ticks>0);
        }
    }
    /* Launch immunity is owner-only: another racer remains hittable. */
    Fixture f;CHECK(setup(&f,2));give(&f,0,0x1002);
    f.controls[0].item_pressed=1;
    for(unsigned tick=0;tick<36;++tick){CHECK(prepare(&f));CHECK(resolve(&f));}
    f.controls[0].item_pressed=0;f.controls[0].aim_backward=1;
    CHECK(prepare(&f));
    memcpy(f.racers[1].kart.position_q16,f.world.actors[0].motion.position_q16,
           sizeof f.racers[1].kart.position_q16);
    /* Released shells snap to their radius above actual floor support. */
    f.racers[1].kart.position_q16[2]=f.rules.actors_radius_q16;
    CHECK(resolve(&f));
    CHECK(events(&f,PORTABLE_MESH_EVENT_HIT)==1 && f.world.status.racers[1].spin_ticks>0 &&
          f.world.status.racers[0].spin_ticks==0);
    return 0;
}
static int battle_roles(void)
{
    Fixture f;CHECK(setup(&f,2));f.config.mode=16;
    give(&f,0,0x1000);f.controls[0].item_pressed=1;
    CHECK(prepare(&f));CHECK(resolve(&f));CHECK(actors(&f,99)==1);
    f.config.inactive_mask=1;
    give(&f,0,0x100a);f.world.status.racers[0].pending_mushroom=1;
    CHECK(prepare(&f));
    CHECK(!f.world.inventory.racers[0].held_item && !actors(&f,99) &&
          !f.world.status.racers[0].pending_mushroom &&
          !events(&f,PORTABLE_MESH_EVENT_USE) && f.effects[0].speed_ratio_q16==Q &&
          f.effects[0].body_scale_q16==Q);
    CHECK(resolve(&f));
    /* Config masks are Battle-only, bounded and Bomb is a subset. */
    const PortableMeshItems before=f.world;
    f.config.mode=8;
    CHECK(portable_mesh_items_prepare_tick(&f.world,&f.config,f.racers,f.controls,
        f.standings,f.effects,&f.events,NULL,0)!=OK);
    CHECK(!memcmp(&before,&f.world,sizeof before));
    f.config.mode=16;f.config.bomb_mask=2;
    CHECK(portable_mesh_items_prepare_tick(&f.world,&f.config,f.racers,f.controls,
        f.standings,f.effects,&f.events,NULL,0)!=OK);
    f.config.inactive_mask=4;f.config.bomb_mask=0;
    CHECK(portable_mesh_items_prepare_tick(&f.world,&f.config,f.racers,f.controls,
        f.standings,f.effects,&f.events,NULL,0)!=OK);
    CHECK(!memcmp(&before,&f.world,sizeof before));
    /* Lightning reaches a live Bomb, but does not request detonation. */
    CHECK(setup(&f,2));f.config.mode=16;f.config.inactive_mask=f.config.bomb_mask=2;
    give(&f,0,0x100c);f.controls[0].item_pressed=1;
    CHECK(prepare(&f));
    CHECK(f.world.status.racers[1].lightning_ticks>0 &&
          f.effects[1].body_scale_q16==f.rules.lightning_scale_ratio_q16 &&
          !events(&f,PORTABLE_MESH_EVENT_BOMB_CONTACT));
    CHECK(resolve(&f));
    for(unsigned both=0;both<2;++both)
    {
        CHECK(setup(&f,2));f.config.mode=16;
        f.config.inactive_mask=f.config.bomb_mask=(uint8_t)(both?3:2);
        memcpy(f.racers[1].kart.position_q16,f.racers[0].kart.position_q16,
               sizeof f.racers[0].kart.position_q16);
        /* A Star victim protects itself, never prevents Bomb detonation. */
        if(!both)f.world.status.racers[0].star_ticks=5;
        CHECK(prepare(&f));
        const PortableMeshRacerState a=f.racers[0],b=f.racers[1];
        CHECK(portable_mesh_items_pair(&f.world,&f.config,f.racers,0,1,&f.events,NULL,0)==OK);
        CHECK(events(&f,PORTABLE_MESH_EVENT_BOMB_CONTACT)==(both?2u:1u));
        CHECK(!memcmp(&a,&f.racers[0],sizeof a) && !memcmp(&b,&f.racers[1],sizeof b));
        CHECK(!events(&f,PORTABLE_MESH_EVENT_HIT));
        const unsigned count=f.events.count;
        CHECK(portable_mesh_items_pair(&f.world,&f.config,f.racers,0,1,&f.events,NULL,0)==OK);
        CHECK(f.events.count==count); /* A Bomb detonates only once in the turn. */
        CHECK(resolve(&f));
    }
    /* Boo and recovery suppress the entire pair, including detonation. */
    for(unsigned recovery=0;recovery<2;++recovery)
    {
        CHECK(setup(&f,2));f.config.mode=16;f.config.inactive_mask=f.config.bomb_mask=2;
        memcpy(f.racers[1].kart.position_q16,f.racers[0].kart.position_q16,
               sizeof f.racers[0].kart.position_q16);
        if(recovery)f.controls[1].recovering=1;
        else f.world.status.racers[0].boo_invisible_ticks=5;
        CHECK(prepare(&f));
        CHECK(portable_mesh_items_pair(&f.world,&f.config,f.racers,0,1,&f.events,NULL,0)==OK);
        CHECK(!events(&f,PORTABLE_MESH_EVENT_BOMB_CONTACT));CHECK(resolve(&f));
    }
    return 0;
}
static int battle_projectile_contacts(void)
{
    const uint16_t weapons[]={0x1000,0x1002,0x1004,0x1005};
    for(unsigned weapon=0;weapon<4;++weapon)
        for(unsigned protected=0;protected<2;++protected)
        {
            Fixture f;CHECK(setup(&f,2));f.config.mode=16;
            f.config.inactive_mask=f.config.bomb_mask=2;
            give(&f,0,weapons[weapon]);f.controls[0].item_pressed=1;
            CHECK(prepare(&f));CHECK(resolve(&f));
            f.controls[0].item_pressed=0;
            if(protected)f.world.status.racers[1].star_ticks=5;
            CHECK(prepare(&f));
            const uint32_t serial=f.world.actors[0].serial;
            memcpy(f.racers[1].kart.position_q16,f.world.actors[0].motion.position_q16,
                   sizeof f.racers[1].kart.position_q16);
            f.racers[1].kart.position_q16[2]=f.rules.actors_radius_q16;
            CHECK(resolve(&f));
            CHECK(events(&f,PORTABLE_MESH_EVENT_BOMB_CONTACT)==(protected?0u:1u));
            CHECK(!f.world.status.racers[1].spin_ticks);
            CHECK(events(&f,PORTABLE_MESH_EVENT_PROTECTED)==(protected?1u:0u));
            for(unsigned i=0;i<f.events.count;++i)
                if(f.events.values[i].kind==PORTABLE_MESH_EVENT_BOMB_CONTACT)
                    CHECK(f.events.values[i].racer==0 && f.events.values[i].target==1 &&
                          f.events.values[i].actor==0 && f.events.values[i].serial==serial &&
                          f.events.values[i].item==weapons[weapon]);
        }
    return 0;
}
static int post_resolve_inactive(void)
{
    Fixture f;CHECK(setup(&f,2));f.config.mode=16;
    give(&f,0,0x1001);give(&f,1,0x1000);
    f.controls[0].item_pressed=f.controls[1].item_pressed=1;
    CHECK(prepare(&f));CHECK(resolve(&f));
    CHECK(actors(&f,99)==4);
    /* Preserve one released actor while retiring all three attachment phases. */
    f.world.actors[3].phase=PORTABLE_MESH_ACTOR_FLYING;
    f.world.status.racers[0].pending_mushroom=1;
    f.world.status.racers[0].mushroom_ticks=2;
    f.world.status.racers[0].spin_ticks=2;
    f.world.status.racers[0].star_ticks=4;
    give(&f,0,0x100a);give(&f,1,0x1007);
    const uint32_t tick=f.world.tick,seed=f.world.inventory.seed,serial=f.world.serial;
    const PortableMeshItemActor released=f.world.actors[3];
    const PortableMeshItemControls controls[8]={f.world.controls[0],f.world.controls[1]};
    f.config.inactive_mask=3;
    const unsigned existing=f.events.count;
    CHECK(portable_mesh_items_clear_inactive(&f.world,&f.config,&f.events,NULL,0)==OK);
    CHECK(!f.world.inventory.racers[0].held_item && !f.world.inventory.racers[1].held_item &&
          actors(&f,99)==1 && !memcmp(&released,&f.world.actors[3],sizeof released));
    CHECK(f.world.tick==tick && f.world.inventory.seed==seed && f.world.serial==serial &&
          f.world.status.racers[0].star_ticks==4 && !f.world.status.racers[0].pending_mushroom &&
          !f.world.status.racers[0].mushroom_ticks && !f.world.status.racers[0].spin_ticks &&
          !memcmp(controls,f.world.controls,2*sizeof controls[0]));
    CHECK(f.events.count==existing+3);
    for(unsigned i=0;i<3;++i)
        CHECK(f.events.values[existing+i].kind==PORTABLE_MESH_EVENT_DESTROY &&
              f.events.values[existing+i].actor==i);
    const uint64_t hash=portable_mesh_items_checksum(&f.world);
    const PortableMeshItems cleaned=f.world;
    const PortableMeshItemEvents emitted=f.events;
    CHECK(portable_mesh_items_clear_inactive(&f.world,&f.config,&f.events,NULL,0)==OK);
    CHECK(hash==portable_mesh_items_checksum(&f.world) &&
          !memcmp(&cleaned,&f.world,sizeof cleaned) && !memcmp(&emitted,&f.events,sizeof emitted));
    CHECK(setup(&f,1));f.config.mode=16;give(&f,0,0x1000);f.controls[0].item_pressed=1;
    CHECK(prepare(&f));CHECK(resolve(&f));f.config.inactive_mask=1;
    f.events.count=PORTABLE_MESH_ITEMS_EVENTS;
    const PortableMeshItems before=f.world;
    const PortableMeshItemEvents full=f.events;
    CHECK(portable_mesh_items_clear_inactive(&f.world,&f.config,&f.events,NULL,0)==PORTABLE_MESH_ITEMS_BUDGET);
    CHECK(!memcmp(&before,&f.world,sizeof before) && !memcmp(&full,&f.events,sizeof full));
    f.world.prepared=1;
    CHECK(portable_mesh_items_clear_inactive(&f.world,&f.config,&f.events,NULL,0)==PORTABLE_MESH_ITEMS_INVALID);
    return 0;
}
static int battle_cpu_controls(void)
{
    Fixture f;CHECK(setup(&f,2));f.config.mode=16;f.config.human_mask=1;
    CHECK(portable_mesh_items_init(&f.world,&f.config,123,NULL,0)==OK);
    give(&f,1,0x1000);
    f.controls[1].aim_forward=1;
    CHECK(prepare(&f));
    CHECK(!actors(&f,99) && f.world.inventory.racers[1].held_item==0x1000 &&
          !f.world.controls[1].item_pressed && f.world.controls[1].aim_forward &&
          !f.world.controls[1].aim_backward);
    CHECK(resolve(&f));
    f.controls[1]=(PortableMeshItemControls){.item_pressed=1,.aim_backward=1};
    for(unsigned tick=0;tick<f.rules.cpu_hold_ticks+4u;++tick)
    {
        CHECK(prepare(&f));
        CHECK(actors(&f,PORTABLE_MESH_ACTOR_HELD)==1 &&
              f.world.controls[1].item_pressed && f.world.controls[1].aim_backward &&
              !f.world.controls[1].aim_forward && !events(&f,PORTABLE_MESH_EVENT_RELEASE));
        CHECK(resolve(&f));
    }
    f.controls[1]=(PortableMeshItemControls){.aim_backward=1};
    CHECK(prepare(&f));
    CHECK(events(&f,PORTABLE_MESH_EVENT_RELEASE)==1 &&
          !f.world.controls[1].item_pressed && f.world.controls[1].aim_backward &&
          f.world.actors[0].motion.velocity_q16[1]>0);
    CHECK(resolve(&f));
    return 0;
}
static int held_item_during_boo_slowdown(void)
{
    Fixture f;
    const uint16_t items[]={0x100a,0x1000,0x1007};
    for (unsigned duration=0;duration<2;++duration)
    for (unsigned item=0;item<3;++item)
    {
        CHECK(setup(&f,1));give(&f,0,items[item]);
        if (duration)
            CHECK(portable_mesh_item_status_source_haunt_lock(&f.world.status,&f.rules,0,360)==
                  PORTABLE_MESH_ITEM_STATUS_OK);
        else
            CHECK(portable_mesh_item_status_activate(&f.world.status,&f.rules,0,
                  PORTABLE_MESH_STATUS_BOO_LOCK)==PORTABLE_MESH_ITEM_STATUS_OK);
        f.controls[0].item_pressed=1;
        CHECK(prepare(&f) && events(&f,PORTABLE_MESH_EVENT_USE)==1 &&
              !f.world.inventory.racers[0].held_item);
        if (!item)
            CHECK(f.world.status.racers[0].star_ticks==f.rules.star_player_ticks &&
                  !f.world.status.racers[0].boo_lock_ticks);
        else
            CHECK(f.world.status.racers[0].boo_lock_ticks==
                  (duration?359u:f.rules.boo_theft_lock_ticks-1u));
        if (item==1)
            CHECK(actors(&f,PORTABLE_MESH_ACTOR_HELD)==1);
        if (item==2)
            CHECK(f.world.status.racers[0].mushroom_ticks==f.rules.mushroom_boost_ticks &&
                  events(&f,PORTABLE_MESH_EVENT_BOOST)==1);
        CHECK(resolve(&f));
    }
    /* Removing the slowdown gate must not bypass any other use guard. */
    for (unsigned guard=0;guard<6;++guard)
    {
        CHECK(setup(&f,2));
        if (guard==2)
        {
            give(&f,0,0x1000);f.controls[0].item_pressed=1;
            CHECK(prepare(&f) && resolve(&f));
            CHECK(actors(&f,PORTABLE_MESH_ACTOR_HELD)==1);
            f.world.previous_item[0]=0; /* A fresh edge still has an attachment. */
        }
        give(&f,0,0x100a);
        CHECK(portable_mesh_item_status_activate(&f.world.status,&f.rules,0,
              PORTABLE_MESH_STATUS_BOO_LOCK)==PORTABLE_MESH_ITEM_STATUS_OK);
        f.controls[0].item_pressed=1;
        if (guard==0)f.world.status.racers[0].spin_ticks=3;
        if (guard==1)f.controls[0].recovering=1;
        if (guard==3)
        {
            f.rules.actors_release_lock_ticks=2;
            f.world.release_lock[0]=2;
        }
        if (guard==4)f.racers[0].phase=PORTABLE_MESH_RACER_FINISHED;
        if (guard==5){f.config.mode=16;f.config.inactive_mask=1;f.config.bomb_mask=1;}
        const int prepared=prepare(&f);
        if (!prepared)fprintf(stderr,"Boo held-item guard fixture=%u failed prepare\n",guard);
        CHECK(prepared && !events(&f,PORTABLE_MESH_EVENT_USE) &&
              !f.world.status.racers[0].star_ticks &&
              f.world.inventory.racers[0].held_item==(guard==5?0:0x100a));
        CHECK(resolve(&f));
    }
    /* Accepted use order still governs same-turn theft: Star clears its own
     * inventory before a later Boo selects a victim, but an earlier Boo can
     * steal a retained Star before that racer's queued use is dispatched. */
    for (unsigned thief_first=0;thief_first<2;++thief_first)
    {
        CHECK(setup(&f,3));
        const unsigned star=thief_first?1:0,thief=1u-star;
        f.standings[0]=(uint8_t)star;f.standings[1]=(uint8_t)thief;
        give(&f,star,0x100a);give(&f,thief,0x100b);give(&f,2,0x1002);
        f.controls[star].item_pressed=f.controls[thief].item_pressed=1;
        CHECK(portable_mesh_item_status_activate(&f.world.status,&f.rules,star,
              PORTABLE_MESH_STATUS_BOO_LOCK)==PORTABLE_MESH_ITEM_STATUS_OK);
        CHECK(prepare(&f) && events(&f,PORTABLE_MESH_EVENT_THEFT)==1);
        CHECK(f.world.status.racers[star].star_ticks==
              (thief_first?0:f.rules.star_player_ticks) &&
              !f.world.inventory.racers[star].held_item &&
              f.world.inventory.racers[thief].held_item==(thief_first?0x100a:0x1002) &&
              f.world.inventory.racers[2].held_item==(thief_first?0x1002:0));
        CHECK(resolve(&f));
    }
    /* Construct a real MIR HAUNT, let it assign slowdown, then use a newly
     * retained Star. Its following movement pass retires the unlocked HAUNT.
     * An exhausted event queue must leave that entire resolve unpublished. */
    CHECK(setup(&f,2));f.rules.actors_release_lock_ticks=1;
    give(&f,1,0x100b);f.controls[1].item_pressed=1;
    CHECK(prepare(&f) && resolve(&f) && actors(&f,PORTABLE_MESH_ACTOR_HAUNT)==1 &&
          f.world.status.racers[0].boo_lock_ticks);
    give(&f,0,0x100a);f.controls[0].item_pressed=1;f.controls[1].item_pressed=0;
    CHECK(prepare(&f) && f.world.status.racers[0].star_ticks &&
          !f.world.status.racers[0].boo_lock_ticks && actors(&f,PORTABLE_MESH_ACTOR_HAUNT)==1);
    const PortableMeshItemEvents accepted_events=f.events;
    f.events.count=PORTABLE_MESH_ITEMS_EVENTS;
    const PortableMeshItems before=f.world;
    const PortableMeshItemEvents full=f.events;
    PortableMeshRacerState old_racers[8];memcpy(old_racers,f.racers,sizeof old_racers);
    char error[160];
    CHECK(portable_mesh_items_resolve_after_move(&f.world,&f.config,f.racers,&f.events,error,sizeof error)==
          PORTABLE_MESH_ITEMS_BUDGET);
    CHECK(!memcmp(&f.world,&before,sizeof before) && !memcmp(&f.events,&full,sizeof full) &&
          !memcmp(f.racers,old_racers,sizeof old_racers));
    f.events=accepted_events;
    CHECK(resolve(&f) && !actors(&f,PORTABLE_MESH_ACTOR_HAUNT) &&
          !f.world.status.racers[0].boo_lock_ticks &&
          events(&f,PORTABLE_MESH_EVENT_DESTROY)==1);
    return 0;
}


/* Items other games award: triple bananas trail three bananas, the fake box
 * is held like a banana, Bullet Bill rides, the Blooper inks the racers ahead
 * and a released Bob-omb blasts racers within its radius. */
static int original_items(void)
{
    Fixture f;
    CHECK(setup(&f, 1));
    give(&f, 0, PORTABLE_MESH_ITEM_TRIPLE_BANANAS);
    f.controls[0].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    CHECK(events(&f, PORTABLE_MESH_EVENT_USE) == 1 && actors(&f, 99) == 3);
    CHECK(setup(&f, 1));
    give(&f, 0, PORTABLE_MESH_ITEM_FAKE_BOX);
    f.controls[0].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    CHECK(actors(&f, PORTABLE_MESH_ACTOR_HELD) == 1 && f.world.actors[0].item == PORTABLE_MESH_ITEM_FAKE_BOX);
    CHECK(setup(&f, 1));
    give(&f, 0, PORTABLE_MESH_ITEM_BULLET);
    f.controls[0].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(f.world.status.racers[0].bullet_ticks == PORTABLE_MESH_BULLET_TICKS);
    CHECK(resolve(&f));
    CHECK(setup(&f, 3));
    give(&f, 2, PORTABLE_MESH_ITEM_BLOOPER);
    f.controls[2].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(f.world.status.racers[0].ink_ticks && f.world.status.racers[1].ink_ticks &&
          !f.world.status.racers[2].ink_ticks);
    CHECK(resolve(&f));
    CHECK(setup(&f, 2));
    give(&f, 0, PORTABLE_MESH_ITEM_BOMB);
    f.controls[0].item_pressed = 1;
    CHECK(prepare(&f));
    CHECK(resolve(&f));
    f.controls[0].item_pressed = 0;
    unsigned hits = 0, blasts = 0;
    for (unsigned tick = 0; tick < PORTABLE_MESH_BOMB_FUSE_TICKS + PORTABLE_MESH_BOMB_BLAST_TICKS + 10u; ++tick)
    {
        CHECK(prepare(&f));
        CHECK(resolve(&f));
        blasts += actors(&f, PORTABLE_MESH_ACTOR_BLAST) != 0;
        for (unsigned i = 0; i < f.events.count; ++i)
            hits += f.events.values[i].kind == PORTABLE_MESH_EVENT_HIT && f.events.values[i].target == 1u;
    }
    CHECK(blasts && hits == 1 && !actors(&f, 99));
    return 0;
}
int main(void)
{
    CHECK(!original_items());
    CHECK(!held_item_during_boo_slowdown());
    CHECK(!battle_cpu_controls());
    CHECK(!post_resolve_inactive());
    CHECK(!battle_roles());
    CHECK(!battle_projectile_contacts());
    CHECK(!launch_grace());
    CHECK(!natural_box_roulette());
    CHECK(!canonical_uses());
    CHECK(!attachments_motion_and_refusal());
    CHECK(!cpu_and_expiry());
    CHECK(!presentation_events());
    CHECK(!wall_impact_event());
    CHECK(!airborne_traps());
    CHECK(!time_trial_inventory());
    CHECK(!targets_boo_lightning());
    CHECK(!actor_contacts());
    CHECK(!recovery_admission_and_size());
    CHECK(!pairs_and_rollback());
    CHECK(!observed_pairs());
    puts("portable mesh items: PASS");
    return 0;
}
