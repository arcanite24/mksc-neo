#include "mesh_course_actors.h"
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
            fprintf(stderr, "thunderclouds line %d: %s\n", __LINE__, #x);                          \
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
    f->config = (PortableMeshCourseActorsConfig){.course_id = 7,
                                                 .profile_id = 8,
                                                 .revision = 1,
                                                 .scale_q16 = Q / 8,
                                                 .thundercloud_count = 1};
    if (!portable_thundercloud_init(&f->config.initial_thunderclouds[0], 100 * Q, 100 * Q, 9,
                                    (uint16_t)variant))
        return 0;
    f->config.initial_thunderclouds[0].placement_order = 30;
    f->config.thundercloud_object_indices[0] = 11;
    for (unsigned i = 0; i < 72; ++i)
        f->config.thundercloud_frame_ids[0][i] = 1000 + i;
    f->kart_rules = test_mesh_rules();
    uint8_t payload[PORTABLE_MESH_ITEM_RULES_BYTES];
    test_mir_payload(payload);
    PortableAssetSectionView section = {
        .kind = 103, .content_id = 41, .version = 1, .bytes = payload, .size = sizeof payload};
    if (!portable_mesh_item_rules_asset_open(&section, &f->rules))
        return 0;
    f->rules.star_player_ticks = 64;
    f->rules.hit_invulnerability_ticks = 8;
    f->rules.hit_spin_ticks = 16;
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
    int32_t p[3] = {5 * Q / 4, 0, f->kart_rules.kart_radius_q16 + f->kart_rules.ride_height_q16};
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
        fprintf(stderr, "cloud advance: %s\n", error);
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
        fprintf(stderr, "cloud resolve: %s\n", error);
        return 0;
    }
    return 1;
}
static int at_collision(Fixture *f)
{
    for (unsigned i = 0; i < 268; ++i)
        if (!advance(f))
            return 0;
    return f->world.thunderclouds[0].flags == 3 && f->world.thunderclouds[0].timer == 235;
}
static int lifecycle(void)
{
    for (unsigned variant = 0; variant < 4; ++variant)
    {
        Fixture f;
        CHECK(setup(&f, variant));
        PortableThundercloud *a = &f.world.thunderclouds[0];
        PortableMeshCourseActorView view;
        CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 0, &view) && view.visible &&
              view.model_id == 1004 && view.position_q16[2] == Q / 2 && !view.rotation_angle);
        for (unsigned i = 0; i < 32; ++i)
            CHECK(advance(&f));
        CHECK(a->state == 1 && a->timer == 0 && a->art == 4 && a->flags == 1);
        CHECK(advance(&f) && a->state == 16 && a->timer == -1 && a->art == 4 &&
              a->palette_timer == (variant & 1 ? 0 : 1));
        CHECK(f.world.thundercloud_palette_phase[variant / 2] == (variant & 1 ? 0 : 1));
        CHECK(advance(&f) && a->state == 17 && a->timer == 1 && a->art == (variant < 2 ? 0 : 4) &&
              a->flags == 1);
        unsigned seen = 1u << a->art,
                 phases = 1u << f.world.thundercloud_palette_phase[variant / 2];
        for (unsigned tick = 35; tick <= 760; ++tick)
        {
            CHECK(advance(&f));
            seen |= 1u << a->art;
            phases |= 1u << f.world.thundercloud_palette_phase[variant / 2];
            if (tick == 94)
                CHECK(a->art == (variant < 2 ? 1 : 5)); /* First warning at processed60. */
            if (tick == 260)
                CHECK(a->art == (variant < 2 ? 2 : 6) && a->flags == 1);
            if (tick == 267)
                CHECK(a->flags == 1);
            if (tick == 268)
                CHECK(a->art == (variant < 2 ? 3 : 7) && a->flags == 3);
            if (tick == 275)
                CHECK(a->timer == 0 && a->flags == 3);
            if (tick == 276)
                CHECK(a->timer == 1 && a->flags == 1 && a->art == (variant < 2 ? 0 : 4));
            CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 0, &view) &&
                  view.model_id ==
                      1000 + f.world.thundercloud_palette_phase[variant / 2] * 8u + a->art);
        }
        CHECK((seen & (variant < 2 ? 15u : 240u)) == (variant < 2 ? 15u : 240u));
        CHECK(phases == (variant & 1 ? 1u : 510u)); /* Independent readers never write. */
    }
    return 0;
}
static int shared_palette_order(void)
{
    Fixture f;
    CHECK(setup(&f, 0));
    f.config.thundercloud_count = 3;
    for (unsigned i = 0; i < 3; ++i)
    {
        CHECK(portable_thundercloud_init(&f.config.initial_thunderclouds[i], 100 * Q, 100 * Q, 9,
                                         i == 2 ? 1 : 0));
        f.config.initial_thunderclouds[i].placement_order = (uint8_t)(10 + i * 10);
        for (unsigned j = 0; j < 72; ++j)
            f.config.thundercloud_frame_ids[i][j] = 1000 + j;
    }
    char error[160];
    CHECK(portable_mesh_course_actors_init(&f.config, &f.world, error, sizeof error));
    /* Reach two source-local writer phases independently; no private core
     * state is fabricated. The later placement must execute first. */
    for (unsigned i = 0; i < 2; ++i)
    {
        uint8_t phases[2] = {0};
        unsigned ticks = 0;
        const int goal = i ? 186 : 178;
        while (f.world.thunderclouds[i].palette_timer != goal)
        {
            CHECK(++ticks < 300 &&
                  portable_thundercloud_advance(&f.world.thunderclouds[i], phases));
        }
    }
    CHECK(advance(&f) && f.world.thundercloud_palette_phase[0] == 2);
    PortableMeshCourseActorView reader;
    CHECK(portable_mesh_course_actors_view(&f.config, &f.world, 2, &reader) &&
          reader.model_id == 1020);
    /* The other palette bank is independent. */
    f.world.thundercloud_palette_phase[1] = 8;
    CHECK(advance(&f) && f.world.thundercloud_palette_phase[1] == 8);
    return 0;
}
static int contacts(void)
{
    Fixture f;
    CHECK(setup(&f, 0) && at_collision(&f));
    f.racer.kart.velocity_q16[0] = 3 * Q;
    f.racer.kart.velocity_q16[2] = Q / 3;
    PortableMeshRacerState racer = f.racer;
    PortableThundercloud cloud = f.world.thunderclouds[0];
    CHECK(resolve(&f) && f.events.thundercloud_contacts == 1 &&
          f.events.thundercloud_hit_mask == 1 && f.events.hit_mask == 1 &&
          !f.events.protected_mask && f.world.strong_hit[0].pending &&
          f.world.strong_hit[0].rate == 0x1000 && !f.items.status.racers[0].spin_ticks &&
          f.items.status.racers[0].lightning_ticks == 360 && f.world.actor_grace[0] == 8 &&
          !memcmp(&racer, &f.racer, sizeof racer) &&
          !memcmp(&cloud, &f.world.thunderclouds[0],
                  sizeof cloud)); /* No radial/resultant impulse. */
    unsigned expired = 0;
    CHECK(portable_mesh_item_status_advance(&f.items.status, &f.rules, &expired) ==
              PORTABLE_MESH_ITEM_STATUS_OK &&
          f.items.status.racers[0].lightning_ticks == 359);
    CHECK(setup(&f, 1) && at_collision(&f));
    CHECK(portable_mesh_item_status_activate(&f.items.status, &f.rules, 0,
                                             PORTABLE_MESH_STATUS_STAR) ==
          PORTABLE_MESH_ITEM_STATUS_OK);
    racer = f.racer;
    cloud = f.world.thunderclouds[0];
    CHECK(resolve(&f) && f.events.thundercloud_protected_mask == 1 &&
          f.events.protected_mask == 1 && !f.events.hit_mask &&
          !f.items.status.racers[0].lightning_ticks && !memcmp(&racer, &f.racer, sizeof racer) &&
          !memcmp(&cloud, &f.world.thunderclouds[0], sizeof cloud));
    for (unsigned guard = 0; guard < 7; ++guard)
    {
        CHECK(setup(&f, 0) && at_collision(&f));
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
            f.items.status.racers[0].lightning_ticks = 2;
        CHECK(resolve(&f) && !f.events.thundercloud_contacts && !f.events.hit_mask);
    }
    CHECK(setup(&f, 0));
    for (unsigned i = 0; i < 267; ++i)
        CHECK(advance(&f));
    CHECK(resolve(&f) && !f.events.thundercloud_contacts);
    CHECK(advance(&f) && resolve(&f) && f.events.thundercloud_hit_mask == 1);
    return 0;
}
static int rollback_and_hash(void)
{
    Fixture f;
    CHECK(setup(&f, 0) && at_collision(&f));
    for (unsigned field = 0; field < 11; ++field)
    {
        PortableMeshCourseActors good = f.world;
        PortableThundercloud *a = &f.world.thunderclouds[0];
        switch (field)
        {
        case 0:
            a->state = 0;
            break;
        case 1:
            a->timer = -1;
            break;
        case 2:
            a->palette_timer = 0;
            break;
        case 3:
            a->flags = 1;
            break;
        case 4:
            a->art = 8;
            break;
        case 5:
            a->height_q16 += 1;
            break;
        case 6:
            a->x_q16 += 1;
            break;
        case 7:
            a->variant = 4;
            break;
        case 8:
            a->checkpoint += 1;
            break;
        case 9:
            a->placement_order += 1;
            break;
        default:
            f.world.thundercloud_palette_phase[0] = 9;
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
    const uint64_t h = portable_mesh_course_actors_checksum(&f.world);
    f.world.thundercloud_palette_phase[0] = 0;
    CHECK(h != portable_mesh_course_actors_checksum(&f.world));
    CHECK(setup(&f, 0));
    f.config.thundercloud_count = 0;
    char error[160];
    CHECK(portable_mesh_course_actors_init(&f.config, &f.world, error, sizeof error));
    PortableMeshCourseActors empty = {0};
    CHECK(portable_mesh_course_actors_checksum(&f.world) ==
          portable_mesh_course_actors_checksum(&empty));
    return 0;
}
static int decoder(void)
{
    uint8_t meta[128] = {0}, object[64] = {0}, props[82 * 32] = {0}, strings[2048] = {0},
            msa[4128] = {0};
    const char *keys[10] = {"mksc:actor-rules",       "source:metres-per-unit",
                            "mksc:controller",        "mksc:controller-version",
                            "source:x-q16",           "source:y-q16",
                            "source:variant",         "source:checkpoint",
                            "source:placement-order", "source:frame-count"};
    unsigned used = 1;
    for (unsigned i = 0; i < 82; ++i)
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
        const size_t n = strlen(value) + 1;
        memcpy(strings + used, value, n);
        used += (unsigned)n;
    }
    const unsigned family = used;
    memcpy(strings + used, "thunderclouds", 14);
    used += 14;
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
    test_mesh_word(props + 192 + 8, 3);
    test_mesh_word(props + 288 + 8, 72);
    for (unsigned i = 10; i < 82; ++i)
    {
        test_mesh_word(props + 32 * i + 4, 4);
        test_mesh_word(props + 32 * i + 8, 96);
        test_mesh_word(props + 32 * i + 12, i - 9);
    }
    test_mesh_word(meta + 100, 2);
    test_mesh_word(object, 0x1010);
    test_mesh_word(object + 4, 96);
    test_mesh_word(object + 8, 1);
    test_mesh_word(object + 56, 2);
    test_mesh_word(object + 60, 80);
    memcpy(msa, "MSA1", 4);
    test_mesh_word(msa + 4, 1);
    test_mesh_word(msa + 8, 4128);
    test_mesh_word(msa + 12, 8);
    test_mesh_word(msa + 16, 1);
    test_mesh_word(msa + 20, 1024);
    PortableAssetSectionView sections[73] = {
        {.kind = 104, .content_id = 8, .version = 1, .bytes = msa, .size = sizeof msa}};
    for (unsigned i = 1; i < 73; ++i)
        sections[i] = (PortableAssetSectionView){.kind = 96, .content_id = i, .version = 1};
    PortableAssetPackView pack = {.resolved_sections = sections, .section_count = 73};
    PortableCourse3DView course = {.id = 7, .pack = &pack, .table_count = 4};
    course.tables[0] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('M', 'E', 'T', 'A'), 1, 128, meta};
    course.tables[1] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('O', 'B', 'J', 'S'), 1, 64, object};
    course.tables[2] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('P', 'R', 'O', 'P'), 82, 32, props};
    course.tables[3] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('S', 'T', 'R', 'S'), used, 1, strings};
    PortableMeshCourseActorsConfig c;
    char error[160];
    CHECK(portable_mesh_course_actors_config_open(&course, &c, error, sizeof error) &&
          c.thundercloud_count == 1 && c.initial_thunderclouds[0].variant == 3 &&
          c.thundercloud_frame_ids[0][71] == 72);
    PortableMeshCourseActorBinding binding;
    CHECK(portable_mesh_course_actors_binding(&c, 0, &binding) && binding.frame_count == 72 &&
          binding.frame_ids[71] == 72);
    PortableMeshCourseActorsConfig old = c;
    for (unsigned invalid = 0; invalid < 5; ++invalid)
    {
        if (invalid == 0)
            test_mesh_word(props + 192 + 8, 4);
        if (invalid == 1)
            test_mesh_word(props + 96 + 8, 2);
        if (invalid == 2)
            test_mesh_word(object, 0x1011);
        if (invalid == 3)
            test_mesh_word(props + 81 * 32 + 12, 99);
        if (invalid == 4)
            sections[0].bytes = NULL;
        CHECK(!portable_mesh_course_actors_config_open(&course, &c, error, sizeof error) &&
              !memcmp(&c, &old, sizeof c));
        test_mesh_word(props + 192 + 8, 3);
        test_mesh_word(props + 96 + 8, 1);
        test_mesh_word(object, 0x1010);
        test_mesh_word(props + 81 * 32 + 12, 72);
        sections[0].bytes = msa;
    }
    return 0;
}
int main(void)
{
    CHECK(!lifecycle());
    CHECK(!shared_palette_order());
    CHECK(!contacts());
    CHECK(!rollback_and_hash());
    CHECK(!decoder());
    return 0;
}
