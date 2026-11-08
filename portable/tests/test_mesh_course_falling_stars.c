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
            fprintf(stderr, "falling stars line %d: %s\n", __LINE__, #x);                          \
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
        .course_id = 7, .profile_id = 8, .revision = 1, .scale_q16 = 8192, .falling_star_count = 1};
    if (!portable_falling_star_init(&f->config.initial_falling_stars[0], 100 * Q, 100 * Q, 9,
                                    (uint16_t)variant))
        return 0;
    f->config.initial_falling_stars[0].placement_order = 30;
    f->config.falling_star_object_indices[0] = 11;
    for (unsigned i = 0; i < 5; ++i)
        f->config.falling_star_frame_ids[0][i] = 101 + i;
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
    f->rules.hit_spin_ticks = 16;
    f->rules.lightning_duration_ticks = 64;
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
    int32_t p[3] = {Q / 2, 0, f->kart_rules.kart_radius_q16 + f->kart_rules.ride_height_q16};
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
        fprintf(stderr, "star advance: %s\n", error);
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
        fprintf(stderr, "star resolve: %s\n", error);
        return 0;
    }
    return 1;
}
static int at_ground(Fixture *f)
{
    for (unsigned i = 0; i < 65; ++i)
        if (!advance(f))
            return 0;
    return f->world.falling_stars[0].timer == 0 && f->world.falling_stars[0].state == 3 &&
           !f->world.falling_stars[0].height_q16;
}
static int lifecycle(void)
{
    for (unsigned variant = 0; variant < 6; ++variant)
    {
        Fixture f;
        CHECK(setup(&f, variant));
        PortableMeshCourseActorView view;
        CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 0, &view) && !view.visible &&
              view.affine_x_q8 == 224 && !view.rotation_angle && view.object_index == 11);
        CHECK(advance(&f));
        PortableFallingStar *a = &f.world.falling_stars[0];
        CHECK(a->state == 3 && a->timer == 64 && a->height_q16 == 64 * Q && a->affine_q8 == 960 &&
              a->x_q16 == (variant & 1 ? 60 : 140) * Q);
        CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 0, &view) && view.visible &&
              view.affine_x_q8 == 960 && view.rotation_angle == (variant & 1 ? 1024 : 0xfc00) &&
              view.position_q16[2] == 8 * Q && view.position_q16[0] == (variant & 1 ? -5 : 5) * Q);
        for (unsigned i = 1; i < 65; ++i)
            CHECK(advance(&f));
        CHECK(a->state == 3 && a->timer == 0 && !a->height_q16 && a->x_q16 == 100 * Q &&
              a->affine_q8 == 256 && a->angle == (uint16_t)(65u * (variant & 1 ? 1024u : 0xfc00u)));
        CHECK(advance(&f));
        CHECK(a->state == 5 && a->timer == 40 && a->height_q16 == 5 * Q / 2 &&
              a->velocity_x_q16 == (variant & 1 ? 40960 : -40960) && a->affine_q8 == 256);
        CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 0, &view) &&
              view.rotation_angle == (variant & 1 ? 768 : 0xfd00) &&
              view.position_q16[2] == 5 * Q / 16);
        unsigned seen = 1u << a->art;
        for (unsigned i = 0; i < 40; ++i)
        {
            CHECK(advance(&f));
            seen |= 1u << a->art;
        }
        CHECK(seen == 31 && a->timer == 0 && !a->height_q16 && a->art == 4);
        CHECK(advance(&f));
        CHECK(a->state == 1 && !a->flags && a->timer == 32 + (int)(variant / 2) * 16);
        const uint16_t prior_rotation = a->angle;
        const int16_t delay = a->delay;
        for (int i = 0; i <= delay; ++i)
            CHECK(advance(&f));
        CHECK(a->state == 3 && a->timer == 64 &&
              a->angle == (uint16_t)(prior_rotation + (variant & 1 ? 1024u : 0xfc00u)) &&
              a->velocity_x_q16 == (variant & 1 ? 40960 : -40960) &&
              a->velocity_z_q16 == 0x28000 - 41 * 0x2800);
        for (unsigned i = 0; i < 450; ++i)
            CHECK(advance(&f)); /* Includes retained-state later cycles. */
        PortableMeshCourseActors initial;
        char error[160];
        CHECK(portable_mesh_course_actors_init(&f.config, &initial, error, sizeof error));
        CHECK(initial.tick == 0 &&
              !memcmp(&initial.falling_stars[0], &f.config.initial_falling_stars[0], sizeof *a));
    }
    return 0;
}
static int contacts(void)
{
    Fixture f;
    CHECK(setup(&f, 0) && at_ground(&f));
    f.racer.kart.velocity_q16[0] = -4 * Q;
    f.racer.kart.velocity_q16[2] = Q / 3;
    f.racer.kart.panel_ticks = 3;
    f.racer.kart.boost_ticks[0] = 3;
    f.racer.kart.drift_charge = 4;
    PortableFallingStar original = f.world.falling_stars[0];
    CHECK(resolve(&f));
    CHECK(f.events.falling_star_contacts == 1 && f.events.falling_star_hit_mask == 1 &&
          f.events.hit_mask == 1 && !f.events.falling_star_protected_mask &&
          f.world.strong_hit[0].pending && f.world.strong_hit[0].rate == 0x1000 &&
          !f.items.status.racers[0].spin_ticks && f.items.status.racers[0].hit_grace_ticks == 8 &&
          f.world.actor_grace[0] == 8 && f.racer.kart.velocity_q16[0] > 0 &&
          f.racer.kart.velocity_q16[2] == Q / 3 && !f.racer.kart.panel_ticks &&
          !f.racer.kart.boost_ticks[0] && !f.racer.kart.drift_charge &&
          !memcmp(&original, &f.world.falling_stars[0], sizeof original));
    CHECK(setup(&f, 1) && at_ground(&f));
    CHECK(portable_mesh_item_status_activate(&f.items.status, &f.rules, 0,
                                             PORTABLE_MESH_STATUS_STAR) ==
          PORTABLE_MESH_ITEM_STATUS_OK);
    PortableMeshRacerState kart = f.racer;
    original = f.world.falling_stars[0];
    CHECK(resolve(&f) && f.events.falling_star_protected_mask == 1 &&
          f.events.protected_mask == 1 && !f.events.hit_mask && !f.world.strong_hit[0].pending &&
          !memcmp(&kart, &f.racer, sizeof kart) &&
          !memcmp(&original, &f.world.falling_stars[0], sizeof original));
    for (unsigned guard = 0; guard < 6; ++guard)
    {
        CHECK(setup(&f, 0) && at_ground(&f));
        if (guard == 0)
            f.items.status.racers[0].boo_invisible_ticks = 3;
        if (guard == 1)
            f.items.status.racers[0].hit_grace_ticks = 3;
        if (guard == 2)
            f.racer.respawn_grace = 1;
        if (guard == 3)
            f.racer.phase = PORTABLE_MESH_RACER_FINISHED;
        if (guard == 4)
            f.racer.kart.position_q16[2] += 4 * Q;
        if (guard == 5)
            f.items.controls[0].recovering = 1;
        CHECK(resolve(&f) && !f.events.falling_star_contacts && !f.events.hit_mask);
    }
    /* A normal body overlaps this horizontal separation; shrinking the body
     * changes its real suspension center too, leaving a fullXYZ miss. */
    CHECK(setup(&f, 0) && at_ground(&f));
    f.racer.kart.position_q16[0] = Q / 2;
    f.items.status.racers[0].lightning_ticks = 3;
    CHECK(resolve(&f) && !f.events.falling_star_contacts);
    /* Pending strong damage in the air becomes active only after landing. */
    CHECK(setup(&f, 0) && at_ground(&f));
    f.racer.kart.grounded = 0;
    CHECK(resolve(&f) && f.world.strong_hit[0].pending);
    int held = -1;
    CHECK(portable_mesh_course_strong_hit_tick(&f.world.strong_hit[0], 0, 0, &held) && !held);
    CHECK(portable_mesh_course_strong_hit_tick(&f.world.strong_hit[0], 1, 0, &held) && held);
    return 0;
}
static int rollback_and_hash(void)
{
    Fixture f;
    CHECK(setup(&f, 0) && at_ground(&f));
    for (unsigned field = 0; field < 14; ++field)
    {
        PortableMeshCourseActors old = f.world;
        PortableFallingStar *a = &f.world.falling_stars[0];
        switch (field)
        {
        case 0:
            a->state = 2;
            break;
        case 1:
            a->timer = 65;
            break;
        case 2:
            a->height_q16 = 1;
            break;
        case 3:
            a->x_q16 += 1;
            break;
        case 4:
            a->velocity_z_q16 = 1;
            break;
        case 5:
            a->variant = 6;
            break;
        case 6:
            a->initial_x_q16 += 1;
            break;
        case 7:
            a->offset_q16 += 1;
            break;
        case 8:
            a->flags = 1;
            break;
        case 9:
            a->art = 5;
            break;
        case 10:
            a->affine_q8 = 0;
            break;
        case 11:
            a->angle += 1;
            break;
        case 12:
            a->placement_order += 1;
            break;
        default:
            a->checkpoint += 1;
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
        f.world = old;
    }
    uint64_t initial = portable_mesh_course_actors_checksum(&f.world);
    f.world.strong_hit[0].pending = 1;
    f.world.strong_hit[0].rate = 0x1000;
    CHECK(initial != portable_mesh_course_actors_checksum(&f.world));
    CHECK(setup(&f, 0));
    f.config.falling_star_count = 0;
    char error[160];
    CHECK(portable_mesh_course_actors_init(&f.config, &f.world, error, sizeof error));
    /* The absent family does not append even zero-valued family bytes. */
    PortableMeshCourseActors empty = {0};
    CHECK(portable_mesh_course_actors_checksum(&f.world) ==
          portable_mesh_course_actors_checksum(&empty));
    f.config.falling_star_count = 65;
    PortableMeshCourseActors old = f.world;
    CHECK(!portable_mesh_course_actors_init(&f.config, &f.world, error, sizeof error) &&
          !memcmp(&old, &f.world, sizeof old));
    return 0;
}
static int appended_family(void)
{
    Fixture f;
    CHECK(setup(&f, 0));
    f.config.teepee_count = 1;
    CHECK(portable_course_prop_init(&f.config.initial_teepees[0], 100 * Q, 100 * Q, 0x030000, 256,
                                    0));
    f.config.initial_teepees[0].placement_order = 31;
    f.config.teepee_anchors_q16[0][0] = 10 * Q;
    for (unsigned i = 0; i < 11; ++i)
        f.config.teepee_frame_ids[0][i] = 201 + i;
    char error[160];
    CHECK(portable_mesh_course_actors_init(&f.config, &f.world, error, sizeof error));
    CHECK(portable_mesh_course_actors_count(&f.world) == 2);
    PortableMeshCourseActorBinding binding;
    CHECK(portable_mesh_course_actors_binding(&f.config, 0, &binding) &&
          binding.frame_count == 11 && binding.frame_ids[10] == 211);
    CHECK(portable_mesh_course_actors_binding(&f.config, 1, &binding) && binding.frame_count == 5 &&
          binding.frame_ids[4] == 105);
    CHECK(at_ground(&f));
    PortableMeshCourseActorView tent, star;
    CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 0, &tent) &&
          tent.position_q16[0] == 10 * Q && !tent.rotation_angle);
    CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 1, &star) &&
          star.position_q16[0] == 0 && star.rotation_angle == 0xfc00);
    CHECK(resolve(&f) && f.events.falling_star_hit_mask == 1 && !f.events.teepee_contacts);
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
        else
        {
            snprintf(key, sizeof key, "source:frame:%u", i - 10);
            value = key;
        }
        test_mesh_word(props + 32 * i, used);
        size_t n = strlen(value) + 1;
        memcpy(strings + used, value, n);
        used += (unsigned)n;
    }
    unsigned family = used;
    memcpy(strings + used, "falling_stars", 14);
    used += 14;
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
    test_mesh_word(props + 288 + 8, 5);
    for (unsigned i = 10; i < 15; ++i)
    {
        test_mesh_word(props + 32 * i + 4, 4);
        test_mesh_word(props + 32 * i + 8, 96);
        test_mesh_word(props + 32 * i + 12, i - 9);
    }
    test_mesh_word(meta + 100, 2);
    test_mesh_word(object, 0x1011);
    test_mesh_word(object + 4, 96);
    test_mesh_word(object + 8, 1);
    test_mesh_word(object + 56, 2);
    test_mesh_word(object + 60, 13);
    memcpy(msa, "MSA1", 4);
    test_mesh_word(msa + 4, 1);
    test_mesh_word(msa + 8, 4128);
    test_mesh_word(msa + 12, 8);
    test_mesh_word(msa + 16, 1);
    test_mesh_word(msa + 20, 1024);
    PortableAssetSectionView sections[6] = {
        {.kind = 104, .content_id = 8, .version = 1, .bytes = msa, .size = sizeof msa}};
    for (unsigned i = 1; i < 6; ++i)
        sections[i] = (PortableAssetSectionView){.kind = 96, .content_id = i, .version = 1};
    PortableAssetPackView pack = {.resolved_sections = sections, .section_count = 6};
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
          c.falling_star_count == 1 && c.initial_falling_stars[0].variant == 0 &&
          c.falling_star_frame_ids[0][4] == 5);
    PortableMeshCourseActorBinding binding;
    CHECK(portable_mesh_course_actors_binding(&c, 0, &binding) && binding.frame_count == 5 &&
          binding.frame_ids[4] == 5);
    PortableMeshCourseActorsConfig old = c;
    for (unsigned invalid = 0; invalid < 5; ++invalid)
    {
        if (invalid == 0)
            test_mesh_word(props + 192 + 8, 6);
        if (invalid == 1)
            test_mesh_word(props + 96 + 8, 2);
        if (invalid == 2)
            test_mesh_word(object, 0x1013);
        if (invalid == 3)
            test_mesh_word(props + 14 * 32 + 12, 99);
        if (invalid == 4)
            sections[0].bytes = NULL;
        CHECK(!portable_mesh_course_actors_config_open(&course, &c, error, sizeof error) &&
              !memcmp(&c, &old, sizeof c));
        test_mesh_word(props + 192 + 8, 0);
        test_mesh_word(props + 96 + 8, 1);
        test_mesh_word(object, 0x1011);
        test_mesh_word(props + 14 * 32 + 12, 5);
        sections[0].bytes = msa;
    }
    return 0;
}
int main(void)
{
    CHECK(!lifecycle());
    CHECK(!contacts());
    CHECK(!rollback_and_hash());
    CHECK(!appended_family());
    CHECK(!decoder());
    return 0;
}
