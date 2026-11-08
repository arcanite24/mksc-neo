#include "mesh_course_actors.h"
#include "mesh_course_fixture.h"
#include "mesh_item_fixture.h"
#include "mesh_item_rules_asset.h"
#include "fixed_3d.h"
#include <stdio.h>
#include <string.h>
#define Q 65536
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "teepees line %d: %s\n", __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
typedef struct
{
    PortableMeshCourseActorsConfig config;
    PortableMeshCourseActors world;
    PortableMeshItems items;
    PortableMeshItemRules rules;
    PortableMeshKartRules kart_rules;
    PortableCourse3DView course;
    PortableMeshItemsConfig item_config;
    PortableMeshRacerState racers[2];
    PortableMeshKartInput inputs[2];
    PortableMeshCourseActorEvents events;
} Fixture;
static int setup(Fixture *f, unsigned count)
{
    memset(f, 0, sizeof *f);
    f->config = (PortableMeshCourseActorsConfig){
        .course_id = 7, .profile_id = 8, .revision = 1, .scale_q16 = Q / 2, .teepee_count = 1};
    if (!portable_course_prop_init(&f->config.initial_teepees[0], 100 * Q, 100 * Q, 0x030000, 224,
                                   0))
        return 0;
    f->config.initial_teepees[0].placement_order = 30;
    f->config.teepee_anchors_q16[0][0] = 3 * Q;
    f->config.teepee_anchors_q16[0][1] = 4 * Q;
    f->config.teepee_anchors_q16[0][2] = Q;
    f->config.teepee_object_indices[0] = 9;
    for (unsigned i = 0; i < 11; ++i)
        f->config.teepee_frame_ids[0][i] = 101 + i;
    for (unsigned i = 0; i < 1024; ++i)
    {
        int32_t sine, cosine;
        portable_fixed_3d_sincos((uint16_t)(i * 64), &sine, &cosine);
        f->config.sine[i] = sine / 32768;
    }
    f->kart_rules = test_mesh_rules();
    f->course.id = 7;
    uint8_t payload[PORTABLE_MESH_ITEM_RULES_BYTES];
    test_mir_payload(payload);
    PortableAssetSectionView section = {
        .kind = 103, .content_id = 41, .version = 1, .bytes = payload, .size = sizeof payload};
    if (!portable_mesh_item_rules_asset_open(&section, &f->rules))
        return 0;
    f->rules.boo_theft_lock_ticks = 64;
    f->rules.hit_invulnerability_ticks = 8;
    f->rules.star_player_ticks = 64;
    f->rules.boo_invisibility_ticks = 8;
    f->item_config = (PortableMeshItemsConfig){.course = &f->course,
                                               .rules = &f->rules,
                                               .kart_rules = &f->kart_rules,
                                               .mode = 8,
                                               .count = count,
                                               .human_mask = (1u << count) - 1};
    if (portable_mesh_item_inventory_init(&f->items.inventory, count, (1u << count) - 1, 1) !=
            PORTABLE_MESH_ITEM_OK ||
        portable_mesh_item_status_init(&f->items.status, count, (1u << count) - 1) !=
            PORTABLE_MESH_ITEM_STATUS_OK)
        return 0;
    for (unsigned i = 0; i < count; ++i)
    {
        int32_t p[3] = {3 * Q + (int32_t)i * 10 * Q, 4 * Q,
                        Q + f->kart_rules.kart_radius_q16 + f->kart_rules.ride_height_q16};
        const int32_t forward[3] = {0, -1073741824, 0};
        if (portable_mesh_kart_init(&f->racers[i].kart, &f->kart_rules, p, forward) !=
            PORTABLE_MESH_OK)
            return 0;
        f->racers[i].kart.grounded = 1;
        f->racers[i].phase = PORTABLE_MESH_RACER_RACING;
    }
    char error[160];
    return portable_mesh_course_actors_init(&f->config, &f->world, error, sizeof error);
}
static int step(Fixture *f)
{
    char error[160] = {0};
    unsigned expired = 0;
    if (!portable_mesh_course_actors_advance_events(&f->config, &f->world, f->racers,
                                                    f->item_config.count, &f->events, error,
                                                    sizeof error) ||
        !portable_mesh_course_actors_prepare_drive(&f->config, &f->world, &f->items, f->inputs,
                                                   f->racers, f->item_config.count, &f->events,
                                                   error, sizeof error))
    {
        fprintf(stderr, "teepee preturn: %s\n", error);
        return 0;
    }
    if (portable_mesh_item_status_advance(&f->items.status, &f->rules, &expired) !=
        PORTABLE_MESH_ITEM_STATUS_OK)
    {
        fprintf(stderr, "teepee step: invalid fixture item status\n");
        return 0;
    }
    if (!portable_mesh_course_actors_resolve(&f->config, &f->world, &f->items, &f->item_config,
                                             f->racers, f->item_config.count, &f->events, error,
                                             sizeof error))
    {
        fprintf(stderr, "teepee resolve: %s\n", error);
        return 0;
    }
    for (unsigned i = 0; i < f->item_config.count; ++i)
        f->racers[i].kart.previous_buttons = f->inputs[i].buttons;
    return 1;
}
static int lifecycle(void)
{
    Fixture f;
    CHECK(setup(&f, 1));
    PortableMeshCourseActorView tent, rider;
    CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 0, &tent) && tent.visible &&
          tent.model_id == 101 && tent.affine_x_q8 == 224);
    CHECK(portable_mesh_course_teepee_rider_view(&f.config, &f.world, 0, 0, &rider) &&
          !rider.visible);
    PortableMeshRacerState old = f.racers[0];
    CHECK(step(&f));
    CHECK(f.events.teepee_contacts == 1 && f.events.teepee_attached == 1 &&
          f.events.teepee_assigned_mask == 1 && f.events.teepee_touch_mask == 1 &&
          !f.events.hit_mask && !f.events.bump_mask && !f.events.protected_mask &&
          !f.world.lava_burn_ticks[0]);
    CHECK(f.items.status.racers[0].boo_lock_ticks == 360 && !f.items.status.racers[0].spin_ticks &&
          f.world.actor_grace[0] == 8);
    CHECK(f.world.teepees[0].teepee.counter == 1 && f.world.teepees[0].teepee.frame == 1 &&
          f.world.teepees[0].flags == 1);
    old.kart.previous_buttons = f.racers[0].kart.previous_buttons;
    CHECK(!memcmp(&old, &f.racers[0], sizeof old));
    CHECK(portable_mesh_course_teepee_rider_view(&f.config, &f.world, 0, 0, &rider) &&
          rider.visible && rider.model_id == 107 && rider.position_q16[0] == 3 * Q &&
          rider.position_q16[1] == 7 * Q && rider.position_q16[2] == Q);
    const uint16_t camera[7] = {0, 0x4000, 0x8000, 0xa000, 0xc000, 0xe000, 0xf000};
    const uint32_t models[7] = {107, 106, 105, 109, 110, 111, 108};
    for (unsigned i = 0; i < 7; ++i)
        CHECK(portable_mesh_course_teepee_rider_view(&f.config, &f.world, 0, camera[i], &rider) &&
              rider.model_id == models[i]);
    for (unsigned counter = 2; counter <= 31; ++counter)
    {
        CHECK(step(&f));
        CHECK(f.world.teepees[0].teepee.counter == counter &&
              f.world.teepees[0].teepee.frame == (counter <= 10   ? 1
                                                  : counter <= 20 ? 2
                                                                  : 3));
    }
    CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 0, &tent) && !tent.visible);
    CHECK(portable_mesh_course_teepee_rider_view(&f.config, &f.world, 0, 0, &rider) &&
          rider.visible);
    CHECK(f.items.status.racers[0].boo_lock_ticks == 330);
    while (f.items.status.racers[0].boo_lock_ticks)
        CHECK(step(&f));
    CHECK(f.events.teepee_flung == 1 && f.events.teepee_release_mask == 1 &&
          !f.events.teepee_assigned_mask && f.world.teepees[0].teepee.rider_state == 2 &&
          f.world.teepees[0].teepee.rider_gravity == 0x1800);
    for (unsigned i = 0; i < 26; ++i)
        CHECK(step(&f));
    CHECK(!f.world.teepees[0].teepee.rider_state);
    CHECK(portable_mesh_course_teepee_rider_view(&f.config, &f.world, 0, 0, &rider) &&
          !rider.visible);
    return 0;
}
static int protection_and_victims(void)
{
    Fixture f;
    CHECK(setup(&f, 2));
    f.racers[1] = f.racers[0];
    f.racers[1].kart.velocity_q16[0] = 8 * Q;
    f.racers[1].kart.speed_q16 = 0;
    f.items.status.racers[0].hit_grace_ticks = 3;
    f.world.actor_grace[0] = 8;
    CHECK(step(&f));
    CHECK(f.world.teepees[0].teepee.kart == 1 && !f.items.status.racers[0].boo_lock_ticks &&
          f.items.status.racers[1].boo_lock_ticks == 360 && f.events.teepee_assigned_mask == 2);
    CHECK(setup(&f, 1));
    f.items.status.racers[0].hit_grace_ticks = 5;
    f.world.actor_grace[0] = 8;
    CHECK(step(&f) && f.events.teepee_attached == 1 &&
          f.items.status.racers[0].boo_lock_ticks == 360 && !f.events.teepee_touch_mask);
    CHECK(setup(&f, 1));
    CHECK(portable_mesh_item_status_activate(&f.items.status, &f.rules, 0,
                                             PORTABLE_MESH_STATUS_STAR) ==
          PORTABLE_MESH_ITEM_STATUS_OK);
    f.racers[0].kart.velocity_q16[0] = 8 * Q;
    CHECK(step(&f));
    CHECK(f.events.teepee_protected_mask == 1 && f.events.teepee_flung == 1 &&
          f.events.teepee_release_mask == 1 && !f.events.teepee_attached &&
          !f.events.teepee_assigned_mask && !f.items.status.racers[0].boo_lock_ticks &&
          !f.events.hit_mask);
    CHECK(f.world.teepees[0].teepee.rider_vx_q16 == 26112 &&
          f.world.teepees[0].teepee.rider_vz_q16 == 0x12000 &&
          f.world.teepees[0].teepee.rider_spin_rate == 0x1000 &&
          f.world.teepees[0].teepee.rider_gravity == 0x2000);
    for (unsigned i = 0; i < 19; ++i)
        CHECK(step(&f));
    CHECK(f.world.teepees[0].teepee.rider_state == 2 && !f.world.teepees[0].teepee.rider_z_q16);
    CHECK(step(&f) && !f.world.teepees[0].teepee.rider_state);
    /* Multiple fresh tents refresh one shared360 clock; they never sum it. */
    CHECK(setup(&f, 1));
    f.config.teepee_count = 2;
    f.config.initial_teepees[1] = f.config.initial_teepees[0];
    f.config.initial_teepees[1].placement_order = 31;
    memcpy(f.config.teepee_anchors_q16[1], f.config.teepee_anchors_q16[0],
           sizeof f.config.teepee_anchors_q16[0]);
    memcpy(f.config.teepee_frame_ids[1], f.config.teepee_frame_ids[0],
           sizeof f.config.teepee_frame_ids[0]);
    char error[160];
    CHECK(portable_mesh_course_actors_init(&f.config, &f.world, error, sizeof error));
    CHECK(step(&f) && f.events.teepee_attached == 3 &&
          f.items.status.racers[0].boo_lock_ticks == 360);
    return 0;
}
static int clocks_and_geometry(void)
{
    Fixture f;
    CHECK(setup(&f, 1));
    CHECK(portable_mesh_item_status_activate(&f.items.status, &f.rules, 0,
                                             PORTABLE_MESH_STATUS_RIDER_LOCK) ==
          PORTABLE_MESH_ITEM_STATUS_OK);
    f.inputs[0].buttons = PORTABLE_MESH_ACCELERATE;
    char error[160];
    CHECK(portable_mesh_course_actors_prepare_drive(&f.config, &f.world, &f.items, f.inputs,
                                                    f.racers, 1, &f.events, error, sizeof error));
    CHECK(f.items.status.racers[0].boo_lock_ticks == 350 && f.events.teepee_lock_clock[0] == 350 &&
          f.events.teepee_clock_mask == 1);
    f.racers[0].kart.previous_buttons = PORTABLE_MESH_ACCELERATE;
    CHECK(portable_mesh_course_actors_prepare_drive(&f.config, &f.world, &f.items, f.inputs,
                                                    f.racers, 1, &f.events, error, sizeof error) &&
          f.items.status.racers[0].boo_lock_ticks == 350);
    CHECK(setup(&f, 1));
    CHECK(portable_mesh_item_status_activate(&f.items.status, &f.rules, 0,
                                             PORTABLE_MESH_STATUS_BOO_LOCK) ==
          PORTABLE_MESH_ITEM_STATUS_OK);
    f.inputs[0].buttons = PORTABLE_MESH_ACCELERATE;
    CHECK(portable_mesh_course_actors_prepare_drive(&f.config, &f.world, &f.items, f.inputs,
                                                    f.racers, 1, &f.events, error, sizeof error) &&
          f.items.status.racers[0].boo_lock_ticks == 54 && f.events.teepee_lock_clock[0] == 54);
    f.config.cannonball_count = 1;
    portable_cannonball_init(&f.config.initial_cannonballs[0], 100 * Q, 100 * Q, 0, 0);
    f.config.initial_cannonballs[0].placement_order = 29;
    CHECK(portable_mesh_course_actors_init(&f.config, &f.world, error, sizeof error));
    CHECK(portable_mesh_course_strong_hit_admit(&f.world.strong_hit[0], 0x1000, 0));
    int held = 0;
    CHECK(portable_mesh_course_strong_hit_tick(&f.world.strong_hit[0], 1, 0, &held) && held);
    CHECK(portable_mesh_course_actors_prepare_drive(&f.config, &f.world, &f.items, f.inputs,
                                                    f.racers, 1, &f.events, error, sizeof error) &&
          !f.items.status.racers[0].boo_lock_ticks && !f.events.teepee_lock_clock[0]);
    CHECK(setup(&f, 1));
    f.config.teepee_count = 0;
    CHECK(portable_mesh_course_actors_init(&f.config, &f.world, error, sizeof error));
    CHECK(portable_mesh_item_status_activate(&f.items.status, &f.rules, 0,
                                             PORTABLE_MESH_STATUS_RIDER_LOCK) ==
          PORTABLE_MESH_ITEM_STATUS_OK);
    f.inputs[0].buttons = PORTABLE_MESH_ACCELERATE;
    CHECK(portable_mesh_course_actors_prepare_drive(&f.config, &f.world, &f.items, f.inputs,
                                                    f.racers, 1, &f.events, error, sizeof error) &&
          f.items.status.racers[0].boo_lock_ticks == 360);
    for (unsigned guard = 0; guard < 4; ++guard)
    {
        CHECK(setup(&f, 1));
        if (guard == 0)
            f.items.status.racers[0].boo_invisible_ticks = 2;
        if (guard == 1)
            f.racers[0].kart.position_q16[2] += 5 * Q;
        if (guard == 2)
            f.racers[0].respawn_grace = 1;
        if (guard == 3)
            f.racers[0].phase = PORTABLE_MESH_RACER_FINISHED;
        CHECK(step(&f) && !f.events.teepee_contacts && !f.world.teepees[0].state);
    }
    CHECK(setup(&f, 1));
    f.racers[0].kart.grounded = 0;
    f.racers[0].kart.position_q16[2] += Q / 2;
    CHECK(step(&f) && f.events.teepee_attached == 1 && f.world.teepee_plane_q16[0] == Q);
    PortableMeshCourseActorView airborne;
    CHECK(portable_mesh_course_teepee_rider_view(&f.config, &f.world, 0, 0, &airborne) &&
          airborne.position_q16[2] == 3 * Q / 2);
    CHECK(setup(&f, 1));
    const int32_t clearance = portable_fixed_3d_mul(f.kart_rules.kart_radius_q16, Q / 2) +
                              portable_fixed_3d_mul(f.kart_rules.ride_height_q16, Q / 2);
    f.racers[0].kart.position_q16[2] = Q + clearance;
    f.events.movement_body_scale_q16[0] = Q / 2;
    CHECK(portable_mesh_course_actors_resolve(&f.config, &f.world, &f.items, &f.item_config,
                                              f.racers, 1, &f.events, error, sizeof error) &&
          f.events.teepee_attached == 1 && f.world.teepee_plane_q16[0] == Q);
    /* Rider local XY rebases only to its victim; another human can be far
     * beyond the source16.16 global range on a valid wide modern course. */
    CHECK(setup(&f, 2));
    f.config.scale_q16 = 1311;
    f.racers[1].kart.position_q16[0] = 4090 * Q;
    CHECK(step(&f));
    f.racers[0].kart.position_q16[0] = 3000 * Q;
    CHECK(step(&f));
    PortableMeshCourseActorView view;
    CHECK(portable_mesh_course_teepee_rider_view(&f.config, &f.world, 0, 0, &view) &&
          view.visible && view.position_q16[0] == 3000 * Q);
    CHECK(setup(&f, 1));
    CHECK(step(&f));
    f.racers[0].kart.position_q16[2] += 2 * Q;
    CHECK(step(&f));
    CHECK(f.world.teepee_plane_q16[0] == 3 * Q);
    f.racers[0].kart.grounded = 0;
    f.racers[0].kart.position_q16[2] += Q;
    CHECK(step(&f));
    CHECK(f.world.teepee_plane_q16[0] == 3 * Q);
    CHECK(portable_mesh_course_teepee_rider_view(&f.config, &f.world, 0, 0, &view) &&
          view.position_q16[2] == 4 * Q);
    uint64_t hash = portable_mesh_course_actors_checksum(&f.world);
    ++f.world.teepee_plane_q16[0];
    CHECK(hash != portable_mesh_course_actors_checksum(&f.world));
    return 0;
}
static int rollback(void)
{
    Fixture f;
    CHECK(setup(&f, 1));
    CHECK(step(&f));
    char error[160];
    for (unsigned field = 0; field < 5; ++field)
    {
        PortableMeshCourseActors bad = f.world;
        if (field == 0)
            bad.teepees[0].teepee.frame = 4;
        if (field == 1)
            bad.teepees[0].teepee.kart = 8;
        if (field == 2)
            bad.teepees[0].teepee.counter = 31;
        if (field == 3)
            bad.teepees[0].teepee.velocity_x_q16 = 1;
        if (field == 4)
            bad.teepees[0].state = 1;
        PortableMeshCourseActors old = bad;
        PortableMeshItems old_items = f.items;
        PortableMeshCourseActorEvents old_events = f.events;
        PortableMeshRacerState old_racer = f.racers[0];
        CHECK(!portable_mesh_course_actors_resolve(&f.config, &bad, &f.items, &f.item_config,
                                                   f.racers, 1, &f.events, error, sizeof error));
        CHECK(!memcmp(&bad, &old, sizeof bad) && !memcmp(&f.items, &old_items, sizeof old_items) &&
              !memcmp(&f.events, &old_events, sizeof old_events) &&
              !memcmp(&f.racers[0], &old_racer, sizeof old_racer));
    }
    return 0;
}
static int decoder(void)
{
    uint8_t meta[128] = {0}, object[64] = {0}, props[22 * 32] = {0}, strings[512] = {0},
            msa[4128] = {0};
    const char *keys[10] = {"mksc:actor-rules",       "source:metres-per-unit",
                            "mksc:controller",        "mksc:controller-version",
                            "source:x-q16",           "source:y-q16",
                            "source:variant",         "source:checkpoint",
                            "source:placement-order", "source:frame-count"};
    unsigned used = 1;
    for (unsigned i = 0; i < 22; ++i)
    {
        char key[32];
        const char *value;
        if (i < 10)
            value = keys[i];
        else if (i < 21)
        {
            snprintf(key, sizeof key, "source:frame:%u", i - 10);
            value = key;
        }
        else
            value = "source:affine-q8";
        test_mesh_word(props + 32 * i, used);
        size_t n = strlen(value) + 1;
        memcpy(strings + used, value, n);
        used += (unsigned)n;
    }
    unsigned family = used;
    memcpy(strings + used, "teepees", 8);
    used += 8;
    test_mesh_word(props + 4, 4);
    test_mesh_word(props + 8, 104);
    test_mesh_word(props + 12, 8);
    test_mesh_word(props + 32 + 4, 1);
    test_mesh_word(props + 32 + 8, Q / 2);
    test_mesh_word(props + 64 + 4, 3);
    test_mesh_word(props + 64 + 8, family);
    test_mesh_word(props + 96 + 8, 1);
    test_mesh_word(props + 128 + 8, 100 * Q);
    test_mesh_word(props + 160 + 8, 100 * Q);
    test_mesh_word(props + 288 + 8, 11);
    for (unsigned i = 10; i < 21; ++i)
    {
        test_mesh_word(props + 32 * i + 4, 4);
        test_mesh_word(props + 32 * i + 8, 96);
        test_mesh_word(props + 32 * i + 12, i - 9);
    }
    test_mesh_word(props + 32 * 21 + 8, 224);
    test_mesh_word(meta + 100, 2);
    test_mesh_word(object, 0x1018);
    test_mesh_word(object + 4, 96);
    test_mesh_word(object + 8, 1);
    test_mesh_word(object + 56, 2);
    test_mesh_word(object + 60, 19);
    memcpy(msa, "MSA1", 4);
    test_mesh_word(msa + 4, 1);
    test_mesh_word(msa + 8, 4128);
    test_mesh_word(msa + 12, 8);
    test_mesh_word(msa + 16, 1);
    test_mesh_word(msa + 20, 1024);
    PortableAssetSectionView sections[12] = {
        {.kind = 104, .content_id = 8, .version = 1, .bytes = msa, .size = sizeof msa}};
    for (unsigned i = 1; i < 12; ++i)
        sections[i] = (PortableAssetSectionView){.kind = 96, .content_id = i, .version = 1};
    PortableAssetPackView pack = {.resolved_sections = sections, .section_count = 12};
    PortableCourse3DView course = {.id = 7, .pack = &pack, .table_count = 4};
    course.tables[0] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('M', 'E', 'T', 'A'), 1, 128, meta};
    course.tables[1] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('O', 'B', 'J', 'S'), 1, 64, object};
    course.tables[2] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('P', 'R', 'O', 'P'), 22, 32, props};
    course.tables[3] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('S', 'T', 'R', 'S'), used, 1, strings};
    PortableMeshCourseActorsConfig c;
    char error[160];
    CHECK(portable_mesh_course_actors_config_open(&course, &c, error, sizeof error) &&
          c.teepee_count == 1 && c.initial_teepees[0].affine_x_q8 == 256 &&
          c.teepee_frame_ids[0][10] == 11);
    test_mesh_word(object + 60, 20);
    CHECK(portable_mesh_course_actors_config_open(&course, &c, error, sizeof error) &&
          c.initial_teepees[0].affine_x_q8 == 224);
    PortableMeshCourseActorsConfig old = c;
    test_mesh_word(props + 192 + 8, 1);
    CHECK(!portable_mesh_course_actors_config_open(&course, &c, error, sizeof error) &&
          !memcmp(&c, &old, sizeof c));
    test_mesh_word(props + 192 + 8, 0);
    test_mesh_word(props + 32 * 21 + 8, 257);
    CHECK(!portable_mesh_course_actors_config_open(&course, &c, error, sizeof error));
    test_mesh_word(props + 32 * 21 + 8, 224);
    test_mesh_word(object, 0x101d);
    CHECK(!portable_mesh_course_actors_config_open(&course, &c, error, sizeof error));
    return 0;
}
int main(void)
{
    CHECK(!lifecycle());
    CHECK(!protection_and_victims());
    CHECK(!clocks_and_geometry());
    CHECK(!rollback());
    CHECK(!decoder());
    return 0;
}
