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
            fprintf(stderr, "common props line %d: %s\n", __LINE__, #x);                           \
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
static int setup(Fixture *f, uint32_t code)
{
    memset(f, 0, sizeof *f);
    f->config = (PortableMeshCourseActorsConfig){
        .course_id = 7, .profile_id = 8, .revision = 1, .scale_q16 = Q / 8, .common_prop_count = 1};
    if (!portable_course_prop_init(&f->config.initial_common_props[0], 100 * Q, 100 * Q, code, 184,
                                   0))
        return 0;
    f->config.initial_common_props[0].checkpoint = 9;
    f->config.initial_common_props[0].placement_order = 30;
    f->config.common_prop_object_indices[0] = 11;
    f->config.common_prop_frame_ids[0][0] = 101;
    f->config.common_prop_frame_ids[0][1] = 102;
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
        fprintf(stderr, "common prop advance: %s\n", error);
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
        fprintf(stderr, "common prop resolve: %s\n", error);
        return 0;
    }
    return 1;
}
static int variants_and_flight(void)
{
    const unsigned classes[6] = {6, 8, 10, 12, 0x86, 0x98}, counts[6] = {2, 1, 8, 12, 1, 2};
    for (unsigned family = 0; family < 6; ++family)
        for (unsigned variant = 0; variant < counts[family]; ++variant)
        {
            Fixture f;
            CHECK(setup(&f, (classes[family] << 16) | variant));
            PortableMeshCourseActorView view;
            PortableMeshCourseActorBinding binding;
            CHECK(portable_mesh_course_actors_binding(&f.config, 0, &binding) &&
                  binding.frame_count == (classes[family] == 0x86 ? 2u : 1u));
            CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 0, &view) && view.visible &&
                  !view.flip_y && view.affine_x_q8 == 184 && view.affine_y_q8 == 184);
            CHECK(portable_mesh_item_status_activate(&f.items.status, &f.rules, 0,
                                                     PORTABLE_MESH_STATUS_STAR) ==
                  PORTABLE_MESH_ITEM_STATUS_OK);
            const PortableMeshRacerState racer = f.racer;
            const PortableMeshItems items = f.items;
            CHECK(resolve(&f) && f.events.common_prop_protected_mask == 1 &&
                  f.events.protected_mask == 1 && !f.events.bump_mask && !f.events.hit_mask &&
                  !f.world.actor_grace[0] && !memcmp(&racer, &f.racer, sizeof racer) &&
                  !memcmp(&items, &f.items, sizeof items));
            PortableCourseProp *a = &f.world.common_props[0];
            CHECK(a->state == 1 && a->timer == 32 && a->affine_y_q8 == -256 && !a->height_q16);
            CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 0, &view) && view.flip_y &&
                  view.affine_x_q8 == 184 && view.affine_y_q8 == 256);
            for (unsigned tick = 0; tick < 33; ++tick)
            {
                CHECK(advance(&f));
                CHECK(a->timer == 31 - (int)tick &&
                      a->height_q16 == f.config.sine[(32 - tick) * 16] * 48);
                CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 0, &view) &&
                      view.visible == (tick < 32));
            }
            CHECK(a->timer == -1 && !a->flags);
            PortableCourseProp ended = *a;
            CHECK(advance(&f) && !memcmp(a, &ended, sizeof ended));
        }
    return 0;
}
static int ordinary_and_umbrella(void)
{
    Fixture f;
    CHECK(setup(&f, 0x860000));
    f.racer.kart.velocity_q16[0] = -4 * Q;
    f.racer.kart.velocity_q16[2] = Q / 3;
    f.racer.kart.panel_ticks = 3;
    f.racer.kart.boost_ticks[0] = 3;
    f.racer.kart.drift_charge = 4;
    const PortableMeshItems items = f.items;
    CHECK(resolve(&f) && f.events.common_prop_contacts == 1 && f.events.common_prop_hit_mask == 1 &&
          f.events.bump_mask == 1 && !f.events.hit_mask && !f.events.lava_burn_mask &&
          f.events.common_prop_umbrella_open_mask == 1 &&
          !f.events.common_prop_umbrella_close_mask && f.world.common_props[0].contact_phase == 1 &&
          f.world.actor_grace[0] == 8 && f.racer.kart.velocity_q16[0] > 0 &&
          f.racer.kart.velocity_q16[2] == Q / 3 && f.racer.kart.panel_ticks == 3 &&
          f.racer.kart.boost_ticks[0] == 3 && f.racer.kart.drift_charge == 4 &&
          !memcmp(&items, &f.items, sizeof items));
    PortableMeshCourseActorView view;
    CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 0, &view) && view.model_id == 102);
    CHECK(resolve(&f) && !f.events.common_prop_contacts &&
          f.world.common_props[0].contact_phase == 1);
    for (unsigned i = 0; i < 8; ++i)
        CHECK(advance(&f));
    CHECK(resolve(&f) && f.events.common_prop_umbrella_close_mask == 1 &&
          !f.events.common_prop_umbrella_open_mask && f.world.common_props[0].contact_phase == 0);
    CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 0, &view) && view.model_id == 101);
    /* Star callbacks ignore the ordinary contact grace, but retain modern
     * explicit Boo/recovery/finished admission guards. */
    CHECK(portable_mesh_item_status_activate(&f.items.status, &f.rules, 0,
                                             PORTABLE_MESH_STATUS_STAR) ==
          PORTABLE_MESH_ITEM_STATUS_OK);
    CHECK(resolve(&f) && f.events.common_prop_protected_mask == 1 &&
          f.world.common_props[0].state == 1);
    for (unsigned kind = 0; kind < 2; ++kind)
    {
        CHECK(setup(&f, kind ? 0x080000 : 0x060000));
        CHECK(resolve(&f) && f.events.common_prop_hit_mask == 1); /* Both flags3 and flags1. */
    }
    return 0;
}
static int guards_and_rollback(void)
{
    Fixture f;
    for (unsigned guard = 0; guard < 7; ++guard)
    {
        CHECK(setup(&f, 0x0a0001));
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
        CHECK(resolve(&f) && !f.events.common_prop_contacts);
    }
    CHECK(setup(&f, 0x860000));
    CHECK(portable_mesh_item_status_activate(&f.items.status, &f.rules, 0,
                                             PORTABLE_MESH_STATUS_STAR) ==
          PORTABLE_MESH_ITEM_STATUS_OK);
    CHECK(resolve(&f) && advance(&f));
    for (unsigned field = 0; field < 12; ++field)
    {
        const PortableMeshCourseActors good = f.world;
        PortableCourseProp *a = &f.world.common_props[0];
        switch (field)
        {
        case 0:
            a->state = 2;
            break;
        case 1:
            a->timer = 33;
            break;
        case 2:
            a->height_q16 += 1;
            break;
        case 3:
            a->flags = 3;
            break;
        case 4:
            a->affine_y_q8 = 256;
            break;
        case 5:
            a->affine_x_q8 = 1;
            break;
        case 6:
            a->contact_phase = 2;
            break;
        case 7:
            a->mechakoopa.cycle = 1;
            break;
        case 8:
            a->x_q16 += 1;
            break;
        case 9:
            a->source_code = 0x980000;
            break;
        case 10:
            a->art_index = 1;
            break;
        default:
            a->placement_order += 1;
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
    CHECK(setup(&f, 0x860000));
    uint64_t hash = portable_mesh_course_actors_checksum(&f.world);
    f.world.common_props[0].contact_phase = 1;
    CHECK(hash != portable_mesh_course_actors_checksum(&f.world));
    f.config.common_prop_count = 0;
    char error[160];
    CHECK(portable_mesh_course_actors_init(&f.config, &f.world, error, sizeof error));
    PortableMeshCourseActors empty = {0};
    CHECK(portable_mesh_course_actors_checksum(&f.world) ==
          portable_mesh_course_actors_checksum(&empty));
    f.config.common_prop_count = 65;
    PortableMeshCourseActors old = f.world;
    CHECK(!portable_mesh_course_actors_init(&f.config, &f.world, error, sizeof error) &&
          !memcmp(&old, &f.world, sizeof old));
    return 0;
}
static int decoder(void)
{
    uint8_t meta[128] = {0}, object[64] = {0}, props[13 * 32] = {0}, strings[512] = {0},
            msa[4128] = {0};
    const char *keys[10] = {"mksc:actor-rules",
                            "source:metres-per-unit",
                            "mksc:controller",
                            "mksc:controller-version",
                            "source:x-q16",
                            "source:y-q16",
                            "source:code",
                            "source:checkpoint",
                            "source:placement-order",
                            "source:frame-count"};
    unsigned used = 1;
    for (unsigned i = 0; i < 13; ++i)
    {
        char key[32];
        const char *value;
        if (i < 10)
            value = keys[i];
        else if (i < 12)
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
    memcpy(strings + used, "common_props", 13);
    used += 13;
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
    test_mesh_word(props + 192 + 8, 0x860000);
    test_mesh_word(props + 288 + 8, 2);
    for (unsigned i = 10; i < 12; ++i)
    {
        test_mesh_word(props + 32 * i + 4, 4);
        test_mesh_word(props + 32 * i + 8, 96);
        test_mesh_word(props + 32 * i + 12, i - 9);
    }
    test_mesh_word(props + 12 * 32 + 8, 184);
    test_mesh_word(meta + 100, 2);
    test_mesh_word(object, 0x1019);
    test_mesh_word(object + 4, 96);
    test_mesh_word(object + 8, 1);
    test_mesh_word(object + 56, 2);
    test_mesh_word(object + 60, 10);
    memcpy(msa, "MSA1", 4);
    test_mesh_word(msa + 4, 1);
    test_mesh_word(msa + 8, 4128);
    test_mesh_word(msa + 12, 8);
    test_mesh_word(msa + 16, 1);
    test_mesh_word(msa + 20, 1024);
    PortableAssetSectionView sections[3] = {
        {.kind = 104, .content_id = 8, .version = 1, .bytes = msa, .size = sizeof msa},
        {.kind = 96, .content_id = 1, .version = 1},
        {.kind = 96, .content_id = 2, .version = 1}};
    PortableAssetPackView pack = {.resolved_sections = sections, .section_count = 3};
    PortableCourse3DView course = {.id = 7, .pack = &pack, .table_count = 4};
    course.tables[0] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('M', 'E', 'T', 'A'), 1, 128, meta};
    course.tables[1] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('O', 'B', 'J', 'S'), 1, 64, object};
    course.tables[2] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('P', 'R', 'O', 'P'), 13, 32, props};
    course.tables[3] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('S', 'T', 'R', 'S'), used, 1, strings};
    PortableMeshCourseActorsConfig c;
    char error[160];
    CHECK(portable_mesh_course_actors_config_open(&course, &c, error, sizeof error) &&
          c.common_prop_count == 1 && c.initial_common_props[0].source_code == 0x860000 &&
          c.initial_common_props[0].affine_x_q8 == 256);
    test_mesh_word(object + 60, 11);
    CHECK(portable_mesh_course_actors_config_open(&course, &c, error, sizeof error) &&
          c.initial_common_props[0].affine_x_q8 == 184);
    PortableMeshCourseActorsConfig old = c;
    for (unsigned invalid = 0; invalid < 7; ++invalid)
    {
        if (invalid == 0)
            test_mesh_word(props + 192 + 8, 0x030000);
        if (invalid == 1)
            test_mesh_word(props + 192 + 8, 0x0a0008);
        if (invalid == 2)
            test_mesh_word(object, 0x1018);
        if (invalid == 3)
            test_mesh_word(props + 12 * 32 + 8, 257);
        if (invalid == 4)
            test_mesh_word(props + 288 + 8, 1);
        if (invalid == 5)
            test_mesh_word(props + 11 * 32 + 12, 99);
        if (invalid == 6)
            test_mesh_word(props + 96 + 8, 2);
        CHECK(!portable_mesh_course_actors_config_open(&course, &c, error, sizeof error) &&
              !memcmp(&c, &old, sizeof c));
        test_mesh_word(props + 192 + 8, 0x860000);
        test_mesh_word(object, 0x1019);
        test_mesh_word(props + 12 * 32 + 8, 184);
        test_mesh_word(props + 288 + 8, 2);
        test_mesh_word(props + 11 * 32 + 12, 2);
        test_mesh_word(props + 96 + 8, 1);
    }
    return 0;
}
static int appended_family(void)
{
    Fixture f;
    CHECK(setup(&f, 0x860000));
    f.config.thundercloud_count = 1;
    CHECK(portable_thundercloud_init(&f.config.initial_thunderclouds[0], 100 * Q, 100 * Q, 0, 0));
    f.config.initial_thunderclouds[0].placement_order = 20;
    f.config.thundercloud_anchors_q16[0][0] = 20 * Q;
    for (unsigned i = 0; i < 72; ++i)
        f.config.thundercloud_frame_ids[0][i] = 201 + i;
    char error[160];
    CHECK(portable_mesh_course_actors_init(&f.config, &f.world, error, sizeof error));
    PortableMeshCourseActorBinding binding;
    PortableMeshCourseActorView view;
    CHECK(portable_mesh_course_actors_binding(&f.config, 0, &binding) && binding.frame_count == 72);
    CHECK(portable_mesh_course_actors_binding(&f.config, 1, &binding) && binding.frame_count == 2);
    CHECK(advance(&f));
    CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 0, &view) &&
          view.position_q16[0] == 20 * Q);
    CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 1, &view) &&
          view.position_q16[0] == 0 && view.model_id == 101);
    CHECK(resolve(&f) && f.events.common_prop_hit_mask == 1 && !f.events.thundercloud_contacts);
    return 0;
}
int main(void)
{
    CHECK(!variants_and_flight());
    CHECK(!ordinary_and_umbrella());
    CHECK(!guards_and_rollback());
    CHECK(!decoder());
    CHECK(!appended_family());
    return 0;
}
