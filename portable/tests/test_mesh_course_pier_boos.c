#include "mesh_course_actors.h"
#include "mesh_course_fixture.h"
#include "mesh_item_fixture.h"
#include "mesh_item_rules_asset.h"
#include "fixed_3d.h"
#include "source_random.h"
#include <stdio.h>
#include <string.h>
#define Q 65536
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "Pier Boo line %d: %s\n", __LINE__, #x);                               \
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
    PortableMeshRacerState racer;
    PortableMeshCourseActorEvents events;
} Fixture;
static int setup(Fixture *f, unsigned unused)
{
    (void)unused;
    memset(f, 0, sizeof *f);
    f->config = (PortableMeshCourseActorsConfig){
        .course_id = 7, .profile_id = 8, .revision = 1, .scale_q16 = Q / 8, .pier_boo_count = 1};
    if (!portable_course_prop_init(&f->config.initial_pier_boos[0], 100 * Q, 100 * Q, 0xa30000u,
                                   256, 0))
        return 0;
    f->config.initial_pier_boos[0].checkpoint = 9;
    f->config.initial_pier_boos[0].placement_order = 30;
    f->config.pier_boo_object_indices[0] = 11;
    f->config.pier_boo_frame_ids[0][0] = 101;
    for (unsigned i = 0; i < 3; ++i)
        f->config.pier_boo_frame_ids[0][i] = 101 + i;
    for (unsigned i = 0; i < 1024; ++i)
    {
        int32_t sine, cosine;
        portable_fixed_3d_sincos((uint16_t)(i * 64), &sine, &cosine);
        f->config.sine[i] = sine / 32768;
    }
    f->config.pier_boo_rules = (PortableBooRules){64, 360, 64, 1024, 1024, 12 * Q, Q / 4};
    f->kart_rules = test_mesh_rules();
    uint8_t payload[PORTABLE_MESH_ITEM_RULES_BYTES];
    test_mir_payload(payload);
    PortableAssetSectionView section = {
        .kind = 103, .content_id = 41, .version = 1, .bytes = payload, .size = sizeof payload};
    if (!portable_mesh_item_rules_asset_open(&section, &f->rules))
        return 0;
    f->rules.hit_invulnerability_ticks = 8;
    f->rules.star_player_ticks = 64;
    f->rules.boo_invisibility_ticks = 64;
    f->rules.hit_spin_ticks = 16;
    f->course.id = 7;
    f->item_config = (PortableMeshItemsConfig){.course = &f->course,
                                               .rules = &f->rules,
                                               .kart_rules = &f->kart_rules,
                                               .mode = 8,
                                               .count = 1,
                                               .human_mask = 1};
    if (portable_mesh_item_inventory_init(&f->items.inventory, 1, 1, 1) != PORTABLE_MESH_ITEM_OK ||
        portable_mesh_item_status_init(&f->items.status, 1, 1) != PORTABLE_MESH_ITEM_STATUS_OK)
        return 0;
    int32_t p[3] = {Q / 4, 0, Q + f->kart_rules.kart_radius_q16 + f->kart_rules.ride_height_q16};
    const int32_t forward[3] = {0, -1073741824, 0};
    if (portable_mesh_kart_init(&f->racer.kart, &f->kart_rules, p, forward) != PORTABLE_MESH_OK)
        return 0;
    f->racer.kart.grounded = 1;
    f->racer.phase = PORTABLE_MESH_RACER_RACING;
    char error[160];
    return portable_mesh_course_actors_init(&f->config, &f->world, error, sizeof error);
}
static int advance(Fixture *f)
{
    char error[160];
    if (!portable_mesh_course_actors_advance(&f->config, &f->world, error, sizeof error))
    {
        fprintf(stderr, "Pier Boo advance: %s\n", error);
        return 0;
    }
    return 1;
}
static int resolve(Fixture *f)
{
    char error[160];
    if (!portable_mesh_course_actors_resolve(&f->config, &f->world, &f->items, &f->item_config,
                                             &f->racer, 1, &f->events, error, sizeof error))
    {
        fprintf(stderr, "Pier Boo resolve: %s\n", error);
        return 0;
    }
    return 1;
}
static int seeded_bob_and_star(void)
{
    Fixture f;
    CHECK(setup(&f, 0));
    uint32_t random = 0;
    CHECK(f.world.pier_boos[0].pier_boo.phase == (portable_source_random(&random) & 0xfc00));
    CHECK(f.world.pier_boo_visual_rng == random && f.items.inventory.seed == 1);
    PortableCourseProps source = {.count = 1};
    source.actors[0] = f.world.pier_boos[0];
    for (unsigned i = 0; i < 130; ++i)
    {
        CHECK(portable_course_props_advance(&source, f.config.sine, 1024));
        CHECK(advance(&f));
        CHECK(!memcmp(&source.actors[0], &f.world.pier_boos[0], sizeof source.actors[0]));
    }
    PortableMeshCourseActorView view;
    CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 0, &view));
    CHECK(view.model_id == 101 && view.visible && view.affine_x_q8 == 256 && !view.has_view_art);
    f.racer.kart.position_q16[2] =
        view.position_q16[2] + f.kart_rules.kart_radius_q16 + f.kart_rules.ride_height_q16;
    CHECK(portable_mesh_item_status_activate(&f.items.status, &f.rules, 0,
                                             PORTABLE_MESH_STATUS_STAR) ==
          PORTABLE_MESH_ITEM_STATUS_OK);
    f.world.actor_grace[0] = 8;
    f.items.status.racers[0].hit_grace_ticks = 8;
    CHECK(resolve(&f) && f.events.pier_boo_protected_mask == 1 && f.events.protected_mask == 1 &&
          !f.events.hit_mask);
    CHECK(f.world.pier_boos[0].state == 5 && !f.world.source_haunts[0].occupied);
    source.actors[0] = f.world.pier_boos[0];
    unsigned ticks = 0;
    do
    {
        CHECK(portable_course_props_advance(&source, f.config.sine, 1024));
        CHECK(advance(&f));
        ++ticks;
        CHECK(!memcmp(&source.actors[0], &f.world.pier_boos[0], sizeof source.actors[0]));
    } while (f.world.pier_boos[0].state != 8 && ticks < 128);
    CHECK(ticks > 96 && ticks < 110 && !f.world.pier_boos[0].flags);
    CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 0, &view) && !view.visible &&
          view.flip_y);
    return 0;
}
static int haunt_lifecycle(void)
{
    Fixture f;
    CHECK(setup(&f, 0));
    PortableMeshKartState kart = f.racer.kart;
    CHECK(resolve(&f) && f.events.pier_boo_touch_mask == 1 && f.events.pier_boo_contacts == 1);
    CHECK(!f.events.hit_mask && !f.events.bump_mask && !f.events.protected_mask &&
          !memcmp(&kart, &f.racer.kart, sizeof kart));
    CHECK(f.world.pier_boos[0].state == 8 && f.world.source_haunts[0].phase == 1 &&
          f.world.source_haunts[0].timer == 63 && !f.items.status.racers[0].boo_lock_ticks);
    PortableBooAttack source = f.world.source_haunts[0];
    PortableBooAttackTarget target = {.heading = 0, .lock_ticks = 0};
    for (unsigned i = 1; i < 64; ++i)
    {
        memset(&f.events, 0, sizeof f.events);
        CHECK(advance(&f));
        CHECK(portable_boo_attack_tick(&source, &target, f.config.sine, 1024,
                                       &f.config.pier_boo_rules));
        CHECK(resolve(&f));
        CHECK(!memcmp(&source, &f.world.source_haunts[0], sizeof source));
        CHECK(f.events.source_haunt_assigned_mask == (i == 63 ? 1 : 0));
    }
    CHECK(f.items.status.racers[0].boo_lock_ticks == 360 && f.world.source_haunt_lock_mask == 1 &&
          f.events.source_haunt_music_start_mask == 1 && f.world.source_haunts[0].phase == 2);
    PortableMeshCourseActorView view;
    CHECK(portable_mesh_course_actors_haunt_view(&f.config, &f.world, 0, &view));
    CHECK(view.visible && (view.model_id == 102 || view.model_id == 103) && !view.has_view_art);
    f.items.status.racers[0].boo_lock_ticks = 0;
    memset(&f.events, 0, sizeof f.events);
    CHECK(resolve(&f));
    CHECK(f.world.source_haunts[0].phase == 4 && f.events.source_haunt_music_release_mask == 1);
    for (unsigned i = 0; i < 64; ++i)
        CHECK(resolve(&f));
    const PortableBooAttack empty = {0};
    CHECK(!memcmp(&empty, &f.world.source_haunts[0], sizeof empty));
    CHECK(!f.world.source_haunt_source_indices[0]);
    for (unsigned j = 0; j < 3; ++j)
        CHECK(!f.world.source_haunt_origins_q16[0][j]);
    CHECK(portable_mesh_course_actors_haunt_view(&f.config, &f.world, 0, &view) && !view.visible);
    return 0;
}
static int clocks_and_provenance(void)
{
    Fixture f;
    CHECK(setup(&f, 0));
    char error[160];
    PortableMeshKartInput input = {.buttons = PORTABLE_MESH_ACCELERATE};
    f.world.source_haunt_lock_mask = 1;
    f.items.status.racers[0].boo_lock_ticks = 320;
    CHECK(portable_mesh_course_actors_prepare_drive(&f.config, &f.world, &f.items, &input, &f.racer,
                                                    1, &f.events, error, sizeof error));
    CHECK(f.items.status.racers[0].boo_lock_ticks == 310 && f.events.source_haunt_clock_mask == 1 &&
          f.events.source_haunt_lock_clock[0] == 310);
    unsigned expired;
    CHECK(portable_mesh_item_status_advance(&f.items.status, &f.rules, &expired) ==
          PORTABLE_MESH_ITEM_STATUS_OK);
    CHECK(f.items.status.racers[0].boo_lock_ticks == 309);
    f.racer.kart.previous_buttons = PORTABLE_MESH_ACCELERATE;
    CHECK(portable_mesh_course_actors_prepare_drive(&f.config, &f.world, &f.items, &input, &f.racer,
                                                    1, &f.events, error, sizeof error) &&
          f.items.status.racers[0].boo_lock_ticks == 309);
    f.racer.kart.previous_buttons = 0;
    f.world.source_haunt_lock_mask = 0;
    CHECK(portable_mesh_course_actors_prepare_drive(&f.config, &f.world, &f.items, &input, &f.racer,
                                                    1, &f.events, error, sizeof error) &&
          f.items.status.racers[0].boo_lock_ticks == 309); /* Ordinary MIR theft untouched. */
    f.world.source_haunt_lock_mask = 1;
    f.events.observed_item_haunt_mask = 1;
    f.racer.kart.position_q16[0] = 20 * Q;
    CHECK(resolve(&f) && !f.world.source_haunt_lock_mask);
    f.events.observed_item_haunt_mask = 0;
    f.world.source_haunt_lock_mask = 1;
    input.buttons = PORTABLE_MESH_RESCUE;
    CHECK(portable_mesh_course_actors_prepare_drive(&f.config, &f.world, &f.items, &input, &f.racer,
                                                    1, &f.events, error, sizeof error) &&
          !f.items.status.racers[0].boo_lock_ticks && !f.world.source_haunt_lock_mask);
    /* CPU source clocks get the ordinary decrement, never human tap escape. */
    f.items.status.human_mask = 0;
    f.world.source_haunt_lock_mask = 1;
    f.items.status.racers[0].boo_lock_ticks = 360;
    input.buttons = PORTABLE_MESH_ACCELERATE;
    CHECK(portable_mesh_course_actors_prepare_drive(&f.config, &f.world, &f.items, &input, &f.racer,
                                                    1, &f.events, error, sizeof error) &&
          f.items.status.racers[0].boo_lock_ticks == 360);
    return 0;
}
static int protected_approach_and_rebase(void)
{
    Fixture f;
    CHECK(setup(&f, 0));
    CHECK(resolve(&f));
    f.racer.kart.position_q16[0] = 3000 * Q;
    CHECK(resolve(&f));
    CHECK(f.world.source_haunt_origins_q16[0][0] == 3000 * Q);
    for (unsigned i = 0; i < 61; ++i)
        CHECK(resolve(&f));
    CHECK(f.world.source_haunts[0].timer == 1);
    CHECK(portable_mesh_item_status_activate(&f.items.status, &f.rules, 0,
                                             PORTABLE_MESH_STATUS_STAR) ==
          PORTABLE_MESH_ITEM_STATUS_OK);
    memset(&f.events, 0, sizeof f.events);
    CHECK(resolve(&f));
    CHECK(f.world.source_haunts[0].phase == 6 && !f.events.source_haunt_assigned_mask &&
          !f.events.source_haunt_music_start_mask && !f.events.source_haunt_music_release_mask);
    int32_t origin[3];
    memcpy(origin, f.world.source_haunt_origins_q16[0], sizeof origin);
    f.racer.kart.position_q16[0] = 3100 * Q;
    CHECK(resolve(&f) && f.world.source_haunts[0].phase == 7 &&
          f.world.source_haunts[0].affine_y_q8 == -256);
    CHECK(!memcmp(origin, f.world.source_haunt_origins_q16[0], sizeof origin));
    PortableMeshCourseActorView view;
    CHECK(portable_mesh_course_actors_haunt_view(&f.config, &f.world, 0, &view) && view.flip_y);
    for (unsigned i = 0; i < 160 && f.world.source_haunts[0].occupied; ++i)
        CHECK(resolve(&f));
    CHECK(!f.world.source_haunts[0].occupied);
    return 0;
}
static int guards_and_rollback(void)
{
    Fixture f;
    char error[160];
    for (unsigned guard = 0; guard < 7; ++guard)
    {
        CHECK(setup(&f, 0));
        if (guard == 0)
            f.items.status.racers[0].boo_invisible_ticks = 1;
        if (guard == 1)
            f.items.status.racers[0].hit_grace_ticks = 1;
        if (guard == 2)
            f.world.actor_grace[0] = 1;
        if (guard == 3)
            f.racer.respawn_grace = 1;
        if (guard == 4)
            f.racer.phase = PORTABLE_MESH_RACER_FINISHED;
        if (guard == 5)
            f.items.controls[0].recovering = 1;
        if (guard == 6)
            f.racer.kart.position_q16[2] += 4 * Q;
        CHECK(resolve(&f) && !f.events.pier_boo_contacts);
    }
    CHECK(setup(&f, 0));
    CHECK(resolve(&f));
    for (unsigned field = 0; field < 7; ++field)
    {
        PortableMeshCourseActors good = f.world;
        if (field == 0)
            f.world.pier_boos[0].pier_boo.phase |= 1;
        if (field == 1)
            f.world.pier_boos[0].state = 4;
        if (field == 2)
            f.world.source_haunts[0].target = 8;
        if (field == 3)
            f.world.source_haunts[0].frame = 2;
        if (field == 4)
            f.world.source_haunt_source_indices[0] = 1;
        if (field == 5)
            f.world.source_haunts[0].affine_y_q8 = 0;
        if (field == 6)
            f.world.source_haunts[0].target = 1;
        PortableMeshCourseActors bad = f.world;
        PortableMeshItems items = f.items;
        PortableMeshRacerState racer = f.racer;
        PortableMeshCourseActorEvents events = f.events;
        if (field != 6)
            CHECK(!portable_mesh_course_actors_advance(&f.config, &f.world, error, sizeof error) &&
                  !memcmp(&bad, &f.world, sizeof bad));
        CHECK(!portable_mesh_course_actors_resolve(&f.config, &f.world, &f.items, &f.item_config,
                                                   &f.racer, 1, &f.events, error, sizeof error) &&
              !memcmp(&bad, &f.world, sizeof bad) && !memcmp(&items, &f.items, sizeof items) &&
              !memcmp(&racer, &f.racer, sizeof racer) &&
              !memcmp(&events, &f.events, sizeof events));
        f.world = good;
    }
    /* Full pool refusal is late, after a real second prop touch, but publishes nothing. */
    CHECK(setup(&f, 0));
    CHECK(resolve(&f));
    for (unsigned i = 1; i < 16; ++i)
    {
        f.world.source_haunts[i] = f.world.source_haunts[0];
        memcpy(f.world.source_haunt_origins_q16[i], f.world.source_haunt_origins_q16[0],
               sizeof f.world.source_haunt_origins_q16[i]);
    }
    f.world.pier_boos[0] = f.config.initial_pier_boos[0];
    f.world.pier_boos[0].pier_boo.phase = 0;
    f.world.actor_grace[0] = 0;
    PortableMeshCourseActors before = f.world;
    PortableMeshItems items = f.items;
    PortableMeshCourseActorEvents events = f.events;
    CHECK(!portable_mesh_course_actors_resolve(&f.config, &f.world, &f.items, &f.item_config,
                                               &f.racer, 1, &f.events, error, sizeof error) &&
          strstr(error, "pool exhausted") && !memcmp(&before, &f.world, sizeof before) &&
          !memcmp(&items, &f.items, sizeof items) && !memcmp(&events, &f.events, sizeof events));
    CHECK(setup(&f, 0));
    uint64_t hash = portable_mesh_course_actors_checksum(&f.world);
    ++f.world.pier_boo_visual_rng;
    CHECK(hash != portable_mesh_course_actors_checksum(&f.world));
    f.config.pier_boo_count = 0;
    CHECK(portable_mesh_course_actors_init(&f.config, &f.world, error, sizeof error));
    PortableMeshCourseActors empty = {0};
    CHECK(portable_mesh_course_actors_checksum(&f.world) ==
          portable_mesh_course_actors_checksum(&empty));
    return 0;
}
static int decoder(void)
{
    uint8_t meta[128] = {0}, object[64] = {0}, props[14 * 32] = {0}, strings[512] = {0},
            msa[4128] = {0};
    const char *keys[10] = {"mksc:actor-rules",       "source:metres-per-unit",
                            "mksc:controller",        "mksc:controller-version",
                            "source:x-q16",           "source:y-q16",
                            "source:variant",         "source:checkpoint",
                            "source:placement-order", "source:frame-count"};
    unsigned used = 1;
    for (unsigned i = 0; i < 14; ++i)
    {
        char key[32];
        const char *value;
        if (i < 10)
            value = keys[i];
        else if (i < 13)
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
    const unsigned family = used;
    memcpy(strings + used, "pier_boos", 10);
    used += 10;
    test_mesh_word(props + 4, 4);
    test_mesh_word(props + 8, 104);
    test_mesh_word(props + 12, 8);
    test_mesh_word(props + 32 + 4, 1);
    test_mesh_word(props + 32 + 8, Q / 8);
    test_mesh_word(props + 64 + 4, 3);
    test_mesh_word(props + 64 + 8, family);
    test_mesh_word(props + 96 + 8, 1);
    test_mesh_word(props + 128 + 8, 100 * Q);
    test_mesh_word(props + 160 + 8, 100 * Q);
    test_mesh_word(props + 192 + 8, 0);
    test_mesh_word(props + 288 + 8, 3);
    for (unsigned i = 10; i < 13; ++i)
    {
        test_mesh_word(props + 32 * i + 4, 4);
        test_mesh_word(props + 32 * i + 8, 96);
        test_mesh_word(props + 32 * i + 12, i - 9);
    }
    test_mesh_word(props + 13 * 32 + 8, 184);
    test_mesh_word(meta + 100, 2);
    test_mesh_word(object, 0x1008);
    test_mesh_word(object + 4, 96);
    test_mesh_word(object + 8, 1);
    test_mesh_word(object + 56, 2);
    test_mesh_word(object + 60, 11);
    memcpy(msa, "MSA1", 4);
    test_mesh_word(msa + 4, 1);
    test_mesh_word(msa + 8, 4128);
    test_mesh_word(msa + 12, 8);
    test_mesh_word(msa + 16, 1);
    test_mesh_word(msa + 20, 1024);
    uint8_t boo[36] = {0};
    memcpy(boo, "BOO1", 4);
    test_mesh_word(boo + 4, 7);
    const uint32_t values[7] = {64, 360, 64, 1024, 1024, 12 * Q, Q / 4};
    for (unsigned i = 0; i < 7; ++i)
        test_mesh_word(boo + 8 + 4 * i, values[i]);
    PortableAssetSectionView sections[5] = {
        {.kind = 104, .content_id = 8, .version = 1, .bytes = msa, .size = sizeof msa},
        {.kind = 96, .content_id = 1, .version = 1},
        {.kind = 96, .content_id = 2, .version = 1},
        {.kind = 96, .content_id = 3, .version = 1},
        {.kind = 62, .content_id = 1, .version = 1, .bytes = boo, .size = 36}};
    PortableAssetPackView pack = {.resolved_sections = sections, .section_count = 5};
    PortableCourse3DView course = {.id = 7, .pack = &pack, .table_count = 4};
    course.tables[0] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('M', 'E', 'T', 'A'), 1, 128, meta};
    course.tables[1] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('O', 'B', 'J', 'S'), 1, 64, object};
    course.tables[2] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('P', 'R', 'O', 'P'), 14, 32, props};
    course.tables[3] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('S', 'T', 'R', 'S'), used, 1, strings};
    PortableMeshCourseActorsConfig c;
    char error[160];
    CHECK(portable_mesh_course_actors_config_open(&course, &c, error, sizeof error) &&
          c.pier_boo_count == 1 && c.initial_pier_boos[0].source_code == 0xa30000 &&
          c.initial_pier_boos[0].affine_x_q8 == 256 && c.pier_boo_frame_ids[0][2] == 3);
    test_mesh_word(object + 60, 12);
    CHECK(portable_mesh_course_actors_config_open(&course, &c, error, sizeof error) &&
          c.initial_pier_boos[0].affine_x_q8 == 184);
    PortableMeshCourseActorsConfig old = c;
    for (unsigned invalid = 0; invalid < 9; ++invalid)
    {
        if (invalid == 0)
            test_mesh_word(props + 192 + 8, 1);
        if (invalid == 1)
            test_mesh_word(object, 0x101a);
        if (invalid == 2)
            test_mesh_word(props + 13 * 32 + 8, 257);
        if (invalid == 3)
            test_mesh_word(props + 288 + 8, 1);
        if (invalid == 4)
            test_mesh_word(props + 11 * 32 + 12, 99);
        if (invalid == 5)
            test_mesh_word(props + 96 + 8, 2);
        if (invalid == 6)
            sections[4].bytes = NULL;
        if (invalid == 7)
            test_mesh_word(boo + 12, 32768);
        if (invalid == 8)
            sections[4].version = 2;
        CHECK(!portable_mesh_course_actors_config_open(&course, &c, error, sizeof error) &&
              !memcmp(&c, &old, sizeof c));
        test_mesh_word(props + 192 + 8, 0);
        test_mesh_word(object, 0x1008);
        test_mesh_word(props + 13 * 32 + 8, 184);
        test_mesh_word(props + 288 + 8, 3);
        test_mesh_word(props + 11 * 32 + 12, 2);
        test_mesh_word(props + 96 + 8, 1);
        sections[4].bytes = boo;
        sections[4].version = 1;
        test_mesh_word(boo + 12, 360);
    }
    return 0;
}

static int constructor_order(void)
{
    Fixture f;
    CHECK(setup(&f, 0));
    f.config.pier_boo_count = 2;
    f.config.initial_pier_boos[1] = f.config.initial_pier_boos[0];
    f.config.initial_pier_boos[0].placement_order = 20;
    f.config.initial_pier_boos[1].placement_order = 10;
    f.config.actor_visual_seed = 17;
    memcpy(f.config.pier_boo_frame_ids[1], f.config.pier_boo_frame_ids[0],
           sizeof f.config.pier_boo_frame_ids[1]);
    char error[160];
    CHECK(portable_mesh_course_actors_init(&f.config, &f.world, error, sizeof error));
    uint32_t seed = 17;
    uint16_t first = (uint16_t)(portable_source_random(&seed) & 0xfc00);
    uint16_t second = (uint16_t)(portable_source_random(&seed) & 0xfc00);
    CHECK(f.world.pier_boos[1].pier_boo.phase == first &&
          f.world.pier_boos[0].pier_boo.phase == second && f.world.pier_boo_visual_rng == seed);
    PortableMeshCourseActors reset;
    CHECK(portable_mesh_course_actors_init(&f.config, &reset, error, sizeof error));
    CHECK(portable_mesh_course_actors_checksum(&reset) ==
          portable_mesh_course_actors_checksum(&f.world));
    return 0;
}
int main(void)
{
    CHECK(!seeded_bob_and_star());
    CHECK(!haunt_lifecycle());
    CHECK(!clocks_and_provenance());
    CHECK(!protected_approach_and_rebase());
    CHECK(!guards_and_rollback());
    CHECK(!decoder());
    CHECK(!constructor_order());
    puts("mesh course Pier Boos: PASS");
    return 0;
}
