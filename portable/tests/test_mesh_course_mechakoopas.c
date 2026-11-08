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
            fprintf(stderr, "Mechakoopas line %d: %s\n", __LINE__, #x);                            \
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
static int setup(Fixture *f, unsigned variant)
{
    memset(f, 0, sizeof *f);
    f->config = (PortableMeshCourseActorsConfig){
        .course_id = 7, .profile_id = 8, .revision = 1, .scale_q16 = Q / 8, .mechakoopa_count = 1};
    if (!portable_course_prop_init(&f->config.initial_mechakoopas[0], 100 * Q, 100 * Q,
                                   0xa10000u | variant, 224, 0))
        return 0;
    f->config.initial_mechakoopas[0].checkpoint = 9;
    f->config.initial_mechakoopas[0].placement_order = 30;
    f->config.mechakoopa_object_indices[0] = 11;
    f->config.mechakoopa_frame_ids[0][0] = 101;
    for (unsigned i = 0; i < 4; ++i)
        f->config.mechakoopa_frame_ids[0][i] = 101 + i;
    for (unsigned i = 0; i < 1024; ++i)
    {
        int32_t sine, cosine;
        portable_fixed_3d_sincos((uint16_t)(i * 64), &sine, &cosine);
        f->config.sine[i] = sine / 32768;
    }
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
    int32_t p[3] = {Q / 4, 0, f->kart_rules.kart_radius_q16 + f->kart_rules.ride_height_q16};
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
        fprintf(stderr, "Mechakoopa advance: %s\n", error);
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
        fprintf(stderr, "Mechakoopa resolve: %s\n", error);
        return 0;
    }
    return 1;
}
static int patrol_and_flight(void)
{
    for (unsigned variant = 0; variant < 2; ++variant)
    {
        Fixture f;
        CHECK(setup(&f, variant));
        PortableCourseProps source = {.count = 1};
        source.actors[0] = f.config.initial_mechakoopas[0];
        PortableMeshCourseActorView view;
        CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 0, &view) &&
              view.model_id == 101 && !view.has_view_art && view.affine_x_q8 == 224 &&
              view.affine_y_q8 == 224);
        for (unsigned tick = 1; tick <= 704; ++tick)
        {
            CHECK(portable_course_props_advance(&source, f.config.sine, 1024) && advance(&f));
            PortableCourseProp *a = &f.world.mechakoopas[0];
            CHECK(!memcmp(a, &source.actors[0], sizeof *a));
            CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 0, &view) &&
                  view.has_view_art &&
                  view.model_id == 101 + portable_course_prop_view_art(a, a->mechakoopa.heading) &&
                  view.alternate_model_id ==
                      101 + portable_course_prop_view_art(
                                a, (uint16_t)(a->mechakoopa.heading ^ 0x8000)) &&
                  view.view_heading == a->mechakoopa.heading);
            if (tick == 96 || tick == 176)
                CHECK(a->x_q16 == (variant ? 52 : 148) * Q);
            if (tick == 272 || tick == 352 || tick == 704)
                CHECK(a->x_q16 == 100 * Q);
            if (tick == 5)
                CHECK(a->mechakoopa.animation_ticks == 5 && !a->mechakoopa.animation_phase);
            if (tick == 6)
                CHECK(!a->mechakoopa.animation_ticks && a->mechakoopa.animation_phase);
            if (tick == 105)
                CHECK(a->mechakoopa.animation_ticks == 9 && !a->mechakoopa.animation_phase);
            if (tick == 106)
                CHECK(!a->mechakoopa.animation_ticks && a->mechakoopa.animation_phase);
        }
        /* A protected hit freezes the stride snapshot, then follows the exact
         * source33-update sine flight, including selected-frame inversion. */
        for (unsigned i = 0; i < 20; ++i)
            CHECK(advance(&f));
        CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 0, &view));
        f.racer.kart.position_q16[0] = view.position_q16[0] + Q / 4;
        CHECK(portable_mesh_item_status_activate(&f.items.status, &f.rules, 0,
                                                 PORTABLE_MESH_STATUS_STAR) ==
              PORTABLE_MESH_ITEM_STATUS_OK);
        PortableMeshRacerState racer = f.racer;
        PortableMeshItems items = f.items;
        const PortableMechakoopaMotion motion = f.world.mechakoopas[0].mechakoopa;
        CHECK(resolve(&f) && f.events.mechakoopa_protected_mask == 1 &&
              f.events.protected_mask == 1 && !f.events.hit_mask && !f.world.actor_grace[0] &&
              !memcmp(&racer, &f.racer, sizeof racer) && !memcmp(&items, &f.items, sizeof items));
        CHECK(f.world.mechakoopas[0].timer == 32 && f.world.mechakoopas[0].flags == 1);
        for (unsigned i = 0; i < 33; ++i)
        {
            CHECK(advance(&f));
            PortableCourseProp *a = &f.world.mechakoopas[0];
            CHECK(a->timer == 31 - (int)i && a->height_q16 == f.config.sine[(32 - i) * 16] * 48 &&
                  !memcmp(&motion, &a->mechakoopa, sizeof motion));
            CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 0, &view) && view.flip_y &&
                  view.affine_x_q8 == 224 && view.affine_y_q8 == 256 && view.visible == (i < 32));
        }
        PortableCourseProp hidden = f.world.mechakoopas[0];
        CHECK(advance(&f) && !memcmp(&hidden, &f.world.mechakoopas[0], sizeof hidden));
    }
    return 0;
}
static int responses(void)
{
    Fixture f;
    CHECK(setup(&f, 0));
    f.racer.kart.velocity_q16[0] = 3 * Q;
    f.racer.kart.velocity_q16[2] = Q / 3;
    const PortableMeshRacerState racer = f.racer;
    CHECK(resolve(&f) && f.events.mechakoopa_contacts == 1 && f.events.mechakoopa_hit_mask == 1 &&
          f.events.hit_mask == 1 && !f.events.mechakoopa_strong_mask &&
          f.items.status.racers[0].spin_ticks == 16 &&
          f.items.status.racers[0].hit_grace_ticks == 8 && !f.world.strong_hit[0].pending &&
          f.world.actor_grace[0] == 8 && !f.world.lava_burn_ticks[0] &&
          !memcmp(&racer, &f.racer, sizeof racer));
    CHECK(setup(&f, 0));
    f.item_config.human_mask = 0;
    CHECK(portable_mesh_item_inventory_init(&f.items.inventory, 1, 0, 1) == PORTABLE_MESH_ITEM_OK &&
          portable_mesh_item_status_init(&f.items.status, 1, 0) == PORTABLE_MESH_ITEM_STATUS_OK);
    PortableMeshRacerState before = f.racer;
    CHECK(resolve(&f) && f.events.mechakoopa_strong_mask == 1 && f.events.hit_mask == 1 &&
          !f.items.status.racers[0].spin_ticks && f.world.strong_hit[0].pending &&
          f.world.strong_hit[0].rate == 0x2000 && !memcmp(&before, &f.racer, sizeof before));
    int held = 0;
    CHECK(portable_mesh_course_strong_hit_tick(&f.world.strong_hit[0], 0, 0, &held) && !held);
    CHECK(portable_mesh_course_strong_hit_tick(&f.world.strong_hit[0], 1, 0, &held) && held);
    /* An actually ranked human ahead selects the source short strong clock. */
    CHECK(setup(&f, 1));
    PortableMeshRacerState racers[2] = {f.racer, f.racer};
    racers[0].kart.position_q16[0] = 20 * Q;
    f.item_config.count = 2;
    f.items.standings[0] = 0;
    f.items.standings[1] = 1;
    CHECK(portable_mesh_item_inventory_init(&f.items.inventory, 2, 1, 1) == PORTABLE_MESH_ITEM_OK &&
          portable_mesh_item_status_init(&f.items.status, 2, 1) == PORTABLE_MESH_ITEM_STATUS_OK);
    char error[160];
    CHECK(portable_mesh_course_actors_resolve(&f.config, &f.world, &f.items, &f.item_config, racers,
                                              2, &f.events, error, sizeof error) &&
          f.events.mechakoopa_strong_mask == 2 && f.world.strong_hit[1].rate == 0x1000);
    for (unsigned guard = 0; guard < 7; ++guard)
    {
        CHECK(setup(&f, 0));
        if (guard == 0)
            f.items.status.racers[0].boo_invisible_ticks = 2;
        if (guard == 1)
            f.items.status.racers[0].hit_grace_ticks = 2;
        if (guard == 2)
            f.racer.respawn_grace = 1;
        if (guard == 3)
            f.racer.phase = PORTABLE_MESH_RACER_FINISHED;
        if (guard == 4)
            f.racer.kart.position_q16[2] += 4 * Q;
        if (guard == 5)
            f.items.controls[0].recovering = 1;
        if (guard == 6)
            f.items.status.racers[0].lightning_ticks = 1;
        CHECK(resolve(&f) && !f.events.mechakoopa_contacts);
    }
    CHECK(setup(&f, 0));
    f.world.actor_grace[0] = 8;
    f.items.status.racers[0].hit_grace_ticks = 3;
    CHECK(portable_mesh_item_status_activate(&f.items.status, &f.rules, 0,
                                             PORTABLE_MESH_STATUS_STAR) ==
          PORTABLE_MESH_ITEM_STATUS_OK);
    f.items.status.racers[0].hit_grace_ticks = 3;
    CHECK(resolve(&f) && f.events.mechakoopa_protected_mask == 1);
    return 0;
}
static int rollback_and_hash(void)
{
    Fixture f;
    CHECK(setup(&f, 0));
    for (unsigned i = 0; i < 20; ++i)
        CHECK(advance(&f));
    for (unsigned field = 0; field < 12; ++field)
    {
        PortableMeshCourseActors good = f.world;
        PortableCourseProp *a = &f.world.mechakoopas[0];
        switch (field)
        {
        case 0:
            a->mechakoopa.cycle = 352;
            break;
        case 1:
            a->mechakoopa.animation_ticks = 6;
            break;
        case 2:
            a->mechakoopa.animation_phase ^= 1;
            break;
        case 3:
            a->mechakoopa.heading ^= 0x8000;
            break;
        case 4:
            a->mechakoopa.has_facing = 0;
            break;
        case 5:
            a->mechakoopa.velocity_x_q16 = -32768;
            break;
        case 6:
            a->x_q16 += 1;
            break;
        case 7:
            a->state = 2;
            break;
        case 8:
            a->height_q16 = 1;
            break;
        case 9:
            a->kamek.phase = 1;
            break;
        case 10:
            a->affine_x_q8 = 1;
            break;
        default:
            a->flags = 1;
            break;
        }
        PortableMeshCourseActors bad = f.world;
        PortableMeshItems items = f.items;
        PortableMeshRacerState racer = f.racer;
        PortableMeshCourseActorEvents events = f.events;
        char error[160];
        CHECK(!portable_mesh_course_actors_advance(&f.config, &f.world, error, sizeof error) &&
              !memcmp(&bad, &f.world, sizeof bad));
        CHECK(!portable_mesh_course_actors_resolve(&f.config, &f.world, &f.items, &f.item_config,
                                                   &f.racer, 1, &f.events, error, sizeof error) &&
              !memcmp(&bad, &f.world, sizeof bad) && !memcmp(&items, &f.items, sizeof items) &&
              !memcmp(&racer, &f.racer, sizeof racer) &&
              !memcmp(&events, &f.events, sizeof events));
        f.world = good;
    }
    uint64_t hash = portable_mesh_course_actors_checksum(&f.world);
    f.world.strong_hit[0].pending = 1;
    f.world.strong_hit[0].rate = 0x1000;
    CHECK(hash != portable_mesh_course_actors_checksum(&f.world));
    CHECK(setup(&f, 0));
    f.config.mechakoopa_count = 0;
    char error[160];
    CHECK(portable_mesh_course_actors_init(&f.config, &f.world, error, sizeof error));
    PortableMeshCourseActors empty = {0};
    CHECK(portable_mesh_course_actors_checksum(&f.world) ==
          portable_mesh_course_actors_checksum(&empty));
    return 0;
}
static int decoder(void)
{
    uint8_t meta[128] = {0}, object[64] = {0}, props[15 * 32] = {0}, strings[512] = {0},
            msa[4128] = {0};
    const char *keys[10] = {"mksc:actor-rules",       "source:metres-per-unit",
                            "mksc:controller",        "mksc:controller-version",
                            "source:x-q16",           "source:y-q16",
                            "source:variant",         "source:checkpoint",
                            "source:placement-order", "source:frame-count"};
    unsigned used = 1;
    for (unsigned i = 0; i < 15; ++i)
    {
        char key[32];
        const char *value;
        if (i < 10)
            value = keys[i];
        else if (i < 14)
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
    memcpy(strings + used, "mechakoopas", 12);
    used += 12;
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
    test_mesh_word(props + 192 + 8, 1);
    test_mesh_word(props + 288 + 8, 4);
    for (unsigned i = 10; i < 14; ++i)
    {
        test_mesh_word(props + 32 * i + 4, 4);
        test_mesh_word(props + 32 * i + 8, 96);
        test_mesh_word(props + 32 * i + 12, i - 9);
    }
    test_mesh_word(props + 14 * 32 + 8, 184);
    test_mesh_word(meta + 100, 2);
    test_mesh_word(object, 0x101c);
    test_mesh_word(object + 4, 96);
    test_mesh_word(object + 8, 1);
    test_mesh_word(object + 56, 2);
    test_mesh_word(object + 60, 12);
    memcpy(msa, "MSA1", 4);
    test_mesh_word(msa + 4, 1);
    test_mesh_word(msa + 8, 4128);
    test_mesh_word(msa + 12, 8);
    test_mesh_word(msa + 16, 1);
    test_mesh_word(msa + 20, 1024);
    PortableAssetSectionView sections[5] = {
        {.kind = 104, .content_id = 8, .version = 1, .bytes = msa, .size = sizeof msa},
        {.kind = 96, .content_id = 1, .version = 1},
        {.kind = 96, .content_id = 2, .version = 1},
        {.kind = 96, .content_id = 3, .version = 1},
        {.kind = 96, .content_id = 4, .version = 1}};
    PortableAssetPackView pack = {.resolved_sections = sections, .section_count = 5};
    PortableCourse3DView course = {.id = 7, .pack = &pack, .table_count = 4};
    course.tables[0] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('M', 'E', 'T', 'A'), 1, 128, meta};
    course.tables[1] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('O', 'B', 'J', 'S'), 1, 64, object};
    course.tables[2] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('P', 'R', 'O', 'P'), 15, 32, props};
    course.tables[3] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('S', 'T', 'R', 'S'), used, 1, strings};
    PortableMeshCourseActorsConfig c;
    char error[160];
    CHECK(portable_mesh_course_actors_config_open(&course, &c, error, sizeof error) &&
          c.mechakoopa_count == 1 && c.initial_mechakoopas[0].source_code == 0xa10001 &&
          c.initial_mechakoopas[0].affine_x_q8 == 224 &&
          c.initial_mechakoopas[0].mechakoopa.velocity_x_q16 == -32768 &&
          c.mechakoopa_frame_ids[0][3] == 4);
    test_mesh_word(object + 60, 13);
    CHECK(portable_mesh_course_actors_config_open(&course, &c, error, sizeof error) &&
          c.initial_mechakoopas[0].affine_x_q8 == 184);
    PortableMeshCourseActorsConfig old = c;
    for (unsigned invalid = 0; invalid < 6; ++invalid)
    {
        if (invalid == 0)
            test_mesh_word(props + 192 + 8, 2);
        if (invalid == 1)
            test_mesh_word(object, 0x101a);
        if (invalid == 2)
            test_mesh_word(props + 14 * 32 + 8, 257);
        if (invalid == 3)
            test_mesh_word(props + 288 + 8, 3);
        if (invalid == 4)
            test_mesh_word(props + 13 * 32 + 12, 99);
        if (invalid == 5)
            test_mesh_word(props + 96 + 8, 2);
        CHECK(!portable_mesh_course_actors_config_open(&course, &c, error, sizeof error) &&
              !memcmp(&c, &old, sizeof c));
        test_mesh_word(props + 192 + 8, 1);
        test_mesh_word(object, 0x101c);
        test_mesh_word(props + 14 * 32 + 8, 184);
        test_mesh_word(props + 288 + 8, 4);
        test_mesh_word(props + 13 * 32 + 12, 4);
        test_mesh_word(props + 96 + 8, 1);
    }
    return 0;
}
static int appended_family(void)
{
    Fixture f;
    CHECK(setup(&f, 0));
    f.config.common_prop_count = 1;
    CHECK(portable_course_prop_init(&f.config.initial_common_props[0], 100 * Q, 100 * Q, 0x060000,
                                    256, 0));
    f.config.initial_common_props[0].placement_order = 20;
    f.config.common_prop_anchors_q16[0][0] = 20 * Q;
    f.config.common_prop_frame_ids[0][0] = 201;
    char error[160];
    CHECK(portable_mesh_course_actors_init(&f.config, &f.world, error, sizeof error));
    PortableMeshCourseActorBinding binding;
    PortableMeshCourseActorView view;
    CHECK(portable_mesh_course_actors_binding(&f.config, 0, &binding) && binding.frame_count == 1);
    CHECK(portable_mesh_course_actors_binding(&f.config, 1, &binding) && binding.frame_count == 4);
    CHECK(advance(&f));
    CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 0, &view) &&
          view.position_q16[0] == 20 * Q && view.model_id == 201);
    CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 1, &view) &&
          view.position_q16[0] == Q / 16 && view.model_id == 104 && view.alternate_model_id == 102);
    CHECK(resolve(&f) && f.events.mechakoopa_hit_mask == 1 && !f.events.common_prop_contacts);
    return 0;
}
int main(void)
{
    CHECK(!patrol_and_flight());
    CHECK(!responses());
    CHECK(!rollback_and_hash());
    CHECK(!decoder());
    CHECK(!appended_family());
    return 0;
}
