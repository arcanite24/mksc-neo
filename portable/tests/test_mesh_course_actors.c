#include "mesh_course_actors.h"
#include "fixed_3d.h"
#include "mesh_query.h"
#include "mesh_course_fixture.h"
#include <stdio.h>
#include <string.h>
#define Q 65536
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "actors line %d: %s\n", __LINE__, #x);                                 \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static PortableMeshCourseActorsConfig config(void)
{
    PortableMeshCourseActorsConfig c = {
        .course_id = 7, .count = 1, .profile_id = 8, .revision = 1, .scale_q16 = Q / 2};
    portable_crab_init(&c.initial[0], 100 * Q, 100 * Q, 0, 0);
    c.initial[0].placement_order = 9;
    c.anchors_q16[0][0] = 3 * Q;
    c.anchors_q16[0][1] = 4 * Q;
    c.anchors_q16[0][2] = Q;
    for (unsigned i = 0; i < 1024; ++i)
        c.sine[i] = i <= 512 ? (int32_t)i * 64 : (int32_t)(1024 - i) * 64;
    return c;
}
static PortableMeshItemRules rules(void)
{
    PortableMeshItemRules r = {.profile_id = 1, .revision = 1};
#define MINIMUM(name, minimum, maximum) r.name = minimum;
    PORTABLE_MESH_ITEM_FIELDS(MINIMUM)
#undef MINIMUM
    r.hit_speed_ratio_q16 = Q / 2;
    r.hit_spin_ticks = 3;
    r.hit_invulnerability_ticks = 5;
    r.star_player_ticks = 5;
    r.boo_invisibility_ticks = 3;
    for (unsigned i = 0; i < 10; ++i)
        r.roulette.preview[i] = (uint16_t)(0x1000 + i);
    for (unsigned i = 0; i < 27; ++i)
        r.roulette.timeline[i] = 2;
    for (unsigned i = 0; i < 8; ++i)
        r.player_weights[i][0] = r.cpu_weights[i][0] = (PortableGpItemWeight){0x1007, 1};
    return r;
}
static int motion(void)
{
    char error[160];
    PortableMeshCourseActorsConfig c = config();
    PortableMeshCourseActors a, b;
    CHECK(portable_mesh_course_actors_init(&c, &a, error, sizeof error));
    b = a;
    uint64_t h = portable_mesh_course_actors_checksum(&a);
    CHECK(h == UINT64_C(3156023113846653106)); /* Original crab-only explicit MAS1 bytes. */
    int32_t p[3];
    for (unsigned i = 0; i < 6; ++i)
        CHECK(portable_mesh_course_actors_advance(&c, &a, error, sizeof error));
    CHECK(a.crabs[0].x_q16 == 103 * Q && a.crabs[0].art == 1 && a.tick == 6);
    CHECK(portable_mesh_course_actors_pose(&c, &a, 0, p));
    CHECK(p[0] == 9 * Q / 2 && p[1] == 4 * Q && p[2] == Q);
    CHECK(h != portable_mesh_course_actors_checksum(&a));
    CHECK(portable_mesh_course_actors_init(&c, &a, error, sizeof error));
    CHECK(!memcmp(&a, &b, sizeof a));
    c.scale_q16 = Q / 2;
    a.crabs[0].x_q16 = c.initial[0].x_q16 - 1;
    CHECK(portable_mesh_course_actors_pose(&c, &a, 0, p));
    CHECK(p[0] == 3 * Q - 1);
    a.crabs[0].x_q16 = c.initial[0].x_q16 + 1;
    CHECK(portable_mesh_course_actors_pose(&c, &a, 0, p));
    CHECK(p[0] == 3 * Q + 1);
    b = a;
    a.crabs[0].timer = 400;
    b = a;
    CHECK(!portable_mesh_course_actors_advance(&c, &a, error, sizeof error));
    CHECK(!memcmp(&a, &b, sizeof a));
    c.profile_id = UINT32_MAX;
    CHECK(!portable_mesh_course_actors_init(&c, &a, error, sizeof error));
    CHECK(!memcmp(&a, &b, sizeof a));
    c.profile_id = 8;
    c.sine[800] = 32769;
    CHECK(!portable_mesh_course_actors_init(&c, &a, error, sizeof error));
    CHECK(!memcmp(&a, &b, sizeof a));
    return 0;
}
static int contact(void)
{
    PortableMeshCourseActorsConfig c = config();
    PortableMeshCourseActors w;
    char error[160];
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    c.scale_q16 = 6554; /* Typical converted .1 metre/source unit. */
    PortableMeshItemRules mir = rules();
    mir.lightning_scale_ratio_q16 = Q / 2;
    CHECK(portable_mesh_item_rules_valid(&mir));
    PortableMeshKartRules kart = test_mesh_rules();
    PortableCourse3DView course = {.id = 7};
    PortableMeshItemsConfig ic = {.course = &course,
                                  .rules = &mir,
                                  .kart_rules = &kart,
                                  .count = 1,
                                  .human_mask = 1,
                                  .mode = 2};
    PortableMeshItems items = {0};
    CHECK(portable_mesh_item_status_init(&items.status, 1, 1) == PORTABLE_MESH_ITEM_STATUS_OK);
    PortableMeshRacerState racer = {.phase = PORTABLE_MESH_RACER_RACING};
    memcpy(racer.kart.position_q16, c.anchors_q16[0], sizeof racer.kart.position_q16);
    racer.kart.up_q30[2] = 1073741824;
    racer.kart.position_q16[2] += kart.kart_radius_q16 + kart.ride_height_q16;
    racer.kart.speed_q16 = 8 * Q;
    racer.kart.velocity_q16[0] = 8 * Q;
    racer.kart.panel_ticks = 9;
    PortableMeshCourseActorEvents e = {0};
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &racer, 1, &e, error,
                                              sizeof error));
    CHECK(e.hit_mask == 1 && e.hit_actors == 1 && racer.kart.speed_q16 == 4 * Q &&
          !racer.kart.panel_ticks && items.status.racers[0].spin_ticks == 3);
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &racer, 1, &e, error,
                                              sizeof error));
    CHECK(!e.hit_mask && racer.kart.speed_q16 == 4 * Q);
    memset(&items.status.racers[0], 0, sizeof items.status.racers[0]);
    items.status.racers[0].star_ticks = 5;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &racer, 1, &e, error,
                                              sizeof error));
    CHECK(e.protected_mask == 1 && !(w.crabs[0].flags & 2) && w.crabs[0].flip_y);
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.crabs[0].height_q16 > 0);
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    memset(&items.status.racers[0], 0, sizeof items.status.racers[0]);
    racer.kart.position_q16[2] += 4 * Q;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &racer, 1, &e, error,
                                              sizeof error));
    CHECK(!e.hit_mask);
    memcpy(racer.kart.position_q16, c.anchors_q16[0], sizeof racer.kart.position_q16);
    items.status.racers[0].boo_invisible_ticks = 3;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &racer, 1, &e, error,
                                              sizeof error));
    CHECK(!e.hit_mask);
    items.status.racers[0].boo_invisible_ticks = 0;
    racer.respawn_grace = 1;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &racer, 1, &e, error,
                                              sizeof error));
    CHECK(!e.hit_mask);
    /* Same lateral point overlaps the full body, but misses the genuinely
     * smaller Lightning body, with both grounded suspension heights correct. */
    racer.respawn_grace = 0;
    memset(&items.status.racers[0], 0, sizeof items.status.racers[0]);
    racer.kart.position_q16[0] = c.anchors_q16[0][0] + 29491;
    racer.kart.position_q16[2] = c.anchors_q16[0][2] + kart.kart_radius_q16 + kart.ride_height_q16;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &racer, 1, &e, error,
                                              sizeof error));
    CHECK(e.hit_mask == 1);
    memset(&items.status.racers[0], 0, sizeof items.status.racers[0]);
    items.status.racers[0].lightning_ticks = 1;
    racer.kart.position_q16[2] =
        c.anchors_q16[0][2] + (kart.kart_radius_q16 + kart.ride_height_q16) / 2;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &racer, 1, &e, error,
                                              sizeof error));
    CHECK(!e.hit_mask);
    /* Exact scaled vertical boundary hits; one Q16 unit above it misses. */
    racer.kart.position_q16[0] = c.anchors_q16[0][0];
    const int32_t boundary =
        c.anchors_q16[0][2] + kart.ride_height_q16 / 2 + kart.kart_radius_q16 / 2 + 2 * c.scale_q16;
    racer.kart.position_q16[2] = boundary + 1;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &racer, 1, &e, error,
                                              sizeof error));
    CHECK(!e.hit_mask);
    racer.kart.position_q16[2] = boundary;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &racer, 1, &e, error,
                                              sizeof error));
    CHECK(e.hit_mask == 1);
    PortableMeshCourseActors before = w;
    PortableMeshItems saved = items;
    PortableMeshRacerState saved_racer = racer;
    PortableMeshCourseActorEvents saved_events = e;
    ic.count = 2;
    CHECK(!portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &racer, 1, &e, error,
                                               sizeof error));
    CHECK(!memcmp(&before, &w, sizeof w) && !memcmp(&saved, &items, sizeof items) &&
          !memcmp(&saved_racer, &racer, sizeof racer) && !memcmp(&saved_events, &e, sizeof e));
    return 0;
}

static int decode(void)
{
    uint8_t meta[128] = {0}, object[64] = {0}, props[37 * 32] = {0}, strings[512] = {0},
            msa[4128] = {0};
    const char *keys[] = {
        "mksc:actor-rules",        "source:metres-per-unit", "mksc:controller",
        "mksc:controller-version", "source:x-q16",           "source:y-q16",
        "source:variant",          "source:checkpoint",      "source:placement-order",
        "source:frame-count",      "source:frame:0",         "source:frame:1",
        "source:frame:2",          "source:frame:3"};
    unsigned used = 1;
    for (unsigned i = 0; i < 14; ++i)
    {
        test_mesh_word(props + 32 * i, used);
        size_t n = strlen(keys[i]) + 1;
        memcpy(strings + used, keys[i], n);
        used += (unsigned)n;
    }
    unsigned crab_string = used;
    memcpy(strings + used, "crabs", 6);
    used += 6;
    test_mesh_word(props + 4, 4);
    test_mesh_word(props + 8, 104);
    test_mesh_word(props + 12, 8);
    test_mesh_word(props + 32 + 4, 1);
    test_mesh_word(props + 32 + 8, Q / 2);
    test_mesh_word(props + 64 + 4, 3);
    test_mesh_word(props + 64 + 8, crab_string);
    for (unsigned i = 3; i < 10; ++i)
        test_mesh_word(props + 32 * i + 8, i == 3             ? 1
                                           : i == 4 || i == 5 ? 100 * Q
                                           : i == 9           ? 4
                                                              : 0);
    for (unsigned i = 10; i < 14; ++i)
    {
        test_mesh_word(props + 32 * i + 4, 4);
        test_mesh_word(props + 32 * i + 8, 96);
        test_mesh_word(props + 32 * i + 12, i - 9);
    }
    test_mesh_word(meta + 96, 0);
    test_mesh_word(meta + 100, 2);
    test_mesh_word(object + 56, 2);
    test_mesh_word(object + 60, 12);
    memcpy(msa, "MSA1", 4);
    test_mesh_word(msa + 4, 1);
    test_mesh_word(msa + 8, 4128);
    test_mesh_word(msa + 12, 8);
    test_mesh_word(msa + 16, 1);
    test_mesh_word(msa + 20, 1024);
    PortableAssetSectionView sections[6] = {
        {.kind = 104, .content_id = 8, .version = 1, .bytes = msa, .size = sizeof msa}};
    for (unsigned i = 1; i < 5; ++i)
        sections[i] = (PortableAssetSectionView){.kind = 96, .content_id = i, .version = 1};
    PortableAssetPackView pack = {.resolved_sections = sections, .section_count = 5};
    PortableCourse3DView view = {.id = 7, .pack = &pack, .table_count = 4};
    view.tables[0] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('M', 'E', 'T', 'A'), 1, 128, meta};
    view.tables[1] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('O', 'B', 'J', 'S'), 1, 64, object};
    view.tables[2] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('P', 'R', 'O', 'P'), 14, 32, props};
    view.tables[3] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('S', 'T', 'R', 'S'), used, 1, strings};
    PortableMeshCourseActorsConfig c, saved;
    char error[160];
    CHECK(portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(c.count == 1 && c.profile_id == 8 && c.frame_ids[0][3] == 4);
    saved = c;
    /* META opt-in is explicit version1; malformed/duplicate values never
     * publish a partially changed source configuration. */
    memcpy(strings + used, "mksc:actor-ground-follow", 25);
    test_mesh_word(props + 14 * 32, used);
    test_mesh_word(props + 14 * 32 + 8, 1);
    view.tables[2].count = 15;
    view.tables[3].count = used + 25;
    test_mesh_word(meta + 100, 15);
    CHECK(portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(c.ground_follow_version == 1);
    PortableMeshCourseActorsConfig ground_saved = c;
    for (unsigned bad = 0; bad < 4; ++bad)
    {
        test_mesh_word(props + 14 * 32 + 4, bad == 3 ? 2 : 0);
        test_mesh_word(props + 14 * 32 + 8, bad == 0 ? 0 : bad == 1 ? 2 : bad == 2 ? UINT32_MAX : 1);
        CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
        CHECK(!memcmp(&c, &ground_saved, sizeof c));
    }
    test_mesh_word(props + 14 * 32 + 4, 0);
    test_mesh_word(props + 14 * 32 + 8, 1);
    memcpy(props + 15 * 32, props + 14 * 32, 32);
    view.tables[2].count = 16;
    test_mesh_word(meta + 100, 16);
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(!memcmp(&c, &ground_saved, sizeof c));
    memset(props + 14 * 32, 0, 64);
    view.tables[2].count = 14;
    view.tables[3].count = used;
    test_mesh_word(meta + 100, 2);
    c = saved;
    /* Legacy replay paths must not omit a required actor owner, even before
     * ordinary kart/item validation. Nothing is admitted or published. */
    PortableMeshTimeTrial trial = {.course = &view};
    memset(&trial.state, 0x35, sizeof trial.state);
    PortableMeshTimeTrial before_trial = trial;
    PortableMeshKartEvents kart_events, before_kart_events;
    memset(&kart_events, 0x61, sizeof kart_events);
    before_kart_events = kart_events;
    CHECK(portable_mesh_time_trial_tick(&trial, (PortableMeshKartInput){0}, &kart_events, error,
                                        sizeof error) == PORTABLE_MESH_UNSUPPORTED);
    CHECK(strstr(error, "course=7") && strstr(error, "actor owner"));
    CHECK(!memcmp(&trial, &before_trial, sizeof trial));
    CHECK(!memcmp(&kart_events, &before_kart_events, sizeof kart_events));
    PortableMeshItems items, before_items;
    memset(&items, 0x42, sizeof items);
    before_items = items;
    PortableMeshItemsConfig item_config = {.course = &view};
    PortableMeshItemEvents item_events, before_item_events;
    memset(&item_events, 0x73, sizeof item_events);
    before_item_events = item_events;
    CHECK(portable_mesh_time_trial_tick_items(
              &trial, &items, &item_config, (PortableMeshKartInput){0},
              (PortableMeshItemControls){0}, &kart_events, &item_events, error,
              sizeof error) == PORTABLE_MESH_UNSUPPORTED);
    CHECK(strstr(error, "course=7") && strstr(error, "actor owner"));
    CHECK(!memcmp(&trial, &before_trial, sizeof trial));
    CHECK(!memcmp(&items, &before_items, sizeof items));
    CHECK(!memcmp(&kart_events, &before_kart_events, sizeof kart_events));
    CHECK(!memcmp(&item_events, &before_item_events, sizeof item_events));
    sections[0].bytes = NULL;
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(!memcmp(&c, &saved, sizeof c));
    sections[0].bytes = msa;
    view.tables[1].count = 0;
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(strstr(error, "without controllers"));
    CHECK(!memcmp(&c, &saved, sizeof c));
    test_mesh_word(meta + 100, 0);
    CHECK(portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(c.count == 0 && c.profile_id == 0);
    PortableMeshCourseActors empty;
    CHECK(portable_mesh_course_actors_init(&c, &empty, error, sizeof error));
    CHECK(empty.count == 0 && empty.tick == 0);
    test_mesh_word(meta + 100, 2);
    view.tables[1].count = 1;
    c = saved;
    test_mesh_word(msa + 12, UINT32_MAX);
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(!memcmp(&c, &saved, sizeof c));
    test_mesh_word(msa + 12, 8);
    sections[0].version = 2;
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(!memcmp(&c, &saved, sizeof c));
    sections[0].version = 1;
    test_mesh_word(msa + 12, 9);
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    test_mesh_word(msa + 12, 8);
    test_mesh_word(msa + 24, 1);
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    test_mesh_word(msa + 24, 0);
    test_mesh_word(msa + 32, 32769);
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    test_mesh_word(msa + 32, 0);
    strings[crab_string] = 'X';
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(strstr(error, "object=0") && strstr(error, "Xrabs"));
    strings[crab_string] = 'c';
    test_mesh_word(props + 3 * 32 + 8, 2);
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(strstr(error, "version=2"));
    test_mesh_word(props + 3 * 32 + 8, 1);
    test_mesh_word(props + 32 + 8, 1310);
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(!memcmp(&c, &saved, sizeof c));
    test_mesh_word(props + 32 + 8, Q / 2);
    memcpy(strings + used, "snowmen", 8);
    test_mesh_word(props + 64 + 8, used);
    view.tables[3].count = used + 8;
    test_mesh_word(props + 9 * 32 + 8, 2);
    CHECK(portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(c.count == 0 && c.snowman_count == 1 && c.initial_snowmen[0].state == 17);
    CHECK(c.snowman_frame_ids[0][1] == 2);
    saved = c;
    test_mesh_word(props + 6 * 32 + 8, 1);
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(!memcmp(&c, &saved, sizeof c));
    const unsigned penguin_string = used + 8;
    memcpy(strings + penguin_string, "penguins", 9);
    unsigned end = penguin_string + 9;
    for (unsigned f = 4; f < 6; ++f)
    {
        char key[32];
        snprintf(key, sizeof key, "source:frame:%u", f);
        test_mesh_word(props + (10 + f) * 32, end);
        size_t length = strlen(key) + 1;
        memcpy(strings + end, key, length);
        end += (unsigned)length;
        test_mesh_word(props + (10 + f) * 32 + 4, 4);
        test_mesh_word(props + (10 + f) * 32 + 8, 96);
        test_mesh_word(props + (10 + f) * 32 + 12, f - 3);
    }
    view.tables[2].count = 16;
    view.tables[3].count = end;
    test_mesh_word(object + 60, 14);
    test_mesh_word(props + 64 + 8, penguin_string);
    test_mesh_word(props + 9 * 32 + 8, 6);
    CHECK(portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(c.count == 0 && c.snowman_count == 0 && c.penguin_count == 1 &&
          c.initial_penguins[0].affine_y_q8 == 256);
    CHECK(c.penguin_frame_ids[0][5] == 2);
    saved = c;
    test_mesh_word(props + 6 * 32 + 8, 2);
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(!memcmp(&c, &saved, sizeof c));
    memcpy(strings + end, "mice", 5);
    view.tables[3].count = end + 5;
    test_mesh_word(props + 64 + 8, end);
    test_mesh_word(props + 9 * 32 + 8, 2);
    const uint16_t variants[3] = {0, 1, 0x8001};
    for (unsigned i = 0; i < 3; ++i)
    {
        test_mesh_word(props + 6 * 32 + 8, variants[i]);
        CHECK(portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
        CHECK(c.count == 0 && c.snowman_count == 0 && c.penguin_count == 0 && c.mouse_count == 1);
        CHECK(c.initial_mice[0].variant == variants[i] && c.mouse_frame_ids[0][1] == 2);
    }
    saved = c;
    test_mesh_word(props + 6 * 32 + 8, 2);
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(!memcmp(&c, &saved, sizeof c));
    unsigned plant_string = end + 5;
    memcpy(strings + plant_string, "piranhas", 9);
    unsigned tail = plant_string + 9;
    test_mesh_word(props + 64 + 8, plant_string);
    test_mesh_word(props + 9 * 32 + 8, 5);
    test_mesh_word(props + 6 * 32 + 8, 0);
    view.tables[3].count = tail;
    CHECK(portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(c.piranha_count == 1 && !c.mouse_count && c.initial_piranhas[0].flags == 2 &&
          c.piranha_frame_ids[0][4] == 1);
    const char *capture_keys[3] = {"mksc:capture", "mksc:capture-forward-speed",
                                   "mksc:capture-up-speed"};
    for (unsigned i = 0; i < 3; ++i)
    {
        test_mesh_word(props + (16 + i) * 32, tail);
        size_t length = strlen(capture_keys[i]) + 1;
        memcpy(strings + tail, capture_keys[i], length);
        tail += (unsigned)length;
    }
    view.tables[2].count = 19;
    view.tables[3].count = tail;
    test_mesh_word(props + 16 * 32 + 4, 2);
    test_mesh_word(props + 16 * 32 + 8, 1);
    test_mesh_word(props + 17 * 32 + 4, 1);
    test_mesh_word(props + 17 * 32 + 8, 31 * Q);
    test_mesh_word(props + 18 * 32 + 4, 1);
    test_mesh_word(props + 18 * 32 + 8, 8 * Q);
    test_mesh_word(meta + 100, 19);
    uint8_t surface[48] = {0};
    test_mesh_word(surface + 40, 16);
    test_mesh_word(surface + 44, 1);
    view.tables[view.table_count++] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('S', 'U', 'R', 'F'), 1, 48, surface};
    CHECK(portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(c.capture_surface_count == 1 && c.capture_surfaces[0] == 0 &&
          c.capture_forward_speed_q16 == 31 * Q && c.capture_up_speed_q16 == 8 * Q);
    saved = c;
    test_mesh_word(props + 18 * 32 + 4, 0);
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(!memcmp(&c, &saved, sizeof c));
    test_mesh_word(props + 18 * 32 + 4, 1);
    test_mesh_word(props + 18 * 32 + 8, 256 * Q + 1);
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(!memcmp(&c, &saved, sizeof c));
    test_mesh_word(props + 18 * 32 + 8, 8 * Q);
    test_mesh_word(props + 16 * 32 + 4, 0);
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(!memcmp(&c, &saved, sizeof c));
    test_mesh_word(props + 16 * 32 + 4, 2);
    memcpy(strings + tail, "seagulls", 9);
    view.tables[3].count = tail + 9;
    test_mesh_word(props + 64 + 8, tail);
    test_mesh_word(props + 9 * 32 + 8, 3);
    CHECK(portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(c.seagull_count == 1 && !c.piranha_count && c.initial_seagulls[0].flags == 3 &&
          c.seagull_frame_ids[0][2] == 3);
    saved = c;
    test_mesh_word(props + 6 * 32 + 8, 1);
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(!memcmp(&c, &saved, sizeof c));
    memcpy(strings + tail + 9, "lava_bubbles", 13);
    test_mesh_word(props + 64 + 8, tail + 9);
    view.tables[3].count = tail + 22;
    test_mesh_word(props + 9 * 32 + 8, 2);
    test_mesh_word(props + 6 * 32 + 8, 12);
    CHECK(portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(c.lava_count == 1 && c.initial_lava[0].timer == 50 && c.lava_frame_ids[0][1] == 2);
    saved = c;
    test_mesh_word(props + 6 * 32 + 8, 13);
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(!memcmp(&c, &saved, sizeof c));
    const unsigned twp_string = tail + 22;
    memcpy(strings + twp_string, "thwomps", 8);
    const unsigned affine_key = twp_string + 8;
    memcpy(strings + affine_key, "mksc:actor-affine-rules", 24);
    test_mesh_word(props + 64 + 8, twp_string);
    test_mesh_word(props + 9 * 32 + 8, 1);
    test_mesh_word(props + 6 * 32 + 8, 11);
    test_mesh_word(props + 19 * 32, affine_key);
    test_mesh_word(props + 19 * 32 + 4, 4);
    test_mesh_word(props + 19 * 32 + 8, 81);
    test_mesh_word(props + 19 * 32 + 12, 9);
    test_mesh_word(meta + 100, 20);
    view.tables[2].count = 20;
    view.tables[3].count = affine_key + 24;
    uint8_t affine[78] = {0};
    memcpy(affine, "AFR1", 4);
    test_mesh_word(affine + 4, 5);
    const unsigned modes[5] = {1, 4, 5, 6, 7};
    for (unsigned j = 0; j < 5; ++j)
    {
        uint8_t *row = affine + 8 + j * 14;
        test_mesh_word(row, modes[j]);
        test_mesh_word(row + 4, 1);
        row[9] = row[11] = 1;
        row[12] = 1; /* inverse256,duration1 */
    }
    sections[5] = (PortableAssetSectionView){
        .kind = 81, .content_id = 9, .version = 1, .bytes = affine, .size = sizeof affine};
    pack.section_count = 6;
    CHECK(portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(c.thwomp_count == 1 && c.initial_thwomps[0].variant == 11 &&
          c.thwomp_frame_ids[0][0] == 1);
    saved = c;
    test_mesh_word(props + 6 * 32 + 8, 12);
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error) &&
          !memcmp(&c, &saved, sizeof c));
    test_mesh_word(props + 6 * 32 + 8, 11);
    affine[12 + 8] = 0;
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error) &&
          !memcmp(&c, &saved, sizeof c));
    const unsigned cannon_string = affine_key + 24;
    memcpy(strings + cannon_string, "cannonballs", 12);
    const unsigned frame6_key = cannon_string + 12;
    memcpy(strings + frame6_key, "source:frame:6", 15);
    /* PROP is dense: the gap before this independent object span must also
     * contain a valid row, even though neither object nor META references it. */
    memcpy(props + 20 * 32, props + 9 * 32, 32);
    for (unsigned j = 0; j < 8; ++j)
        memcpy(props + (21 + j) * 32, props + (2 + j) * 32, 32);
    for (unsigned j = 0; j < 6; ++j)
        memcpy(props + (29 + j) * 32, props + (10 + j) * 32, 32);
    test_mesh_word(props + 21 * 32 + 8, cannon_string);
    test_mesh_word(props + 25 * 32 + 8, 3);
    test_mesh_word(props + 28 * 32 + 8, 7);
    test_mesh_word(props + 35 * 32, frame6_key);
    test_mesh_word(props + 35 * 32 + 4, 4);
    test_mesh_word(props + 35 * 32 + 8, 96);
    test_mesh_word(props + 35 * 32 + 12, 1);
    test_mesh_word(object + 56, 21);
    test_mesh_word(object + 60, 15);
    view.tables[2].count = 36;
    view.tables[3].count = frame6_key + 15;
    CHECK(portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(c.cannonball_count == 1 && c.cannonball_variants[0] == 3 &&
          c.initial_cannonballs[0].timer == 96 && c.cannonball_frame_ids[0][6] == 1);
    saved = c;
    test_mesh_word(props + 25 * 32 + 8, 4);
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error) &&
          !memcmp(&c, &saved, sizeof c));
    const unsigned boo_string = frame6_key + 15;
    memcpy(strings + boo_string, "boos", 5);
    test_mesh_word(props + 21 * 32 + 8, boo_string);
    test_mesh_word(props + 25 * 32 + 8, 1);
    test_mesh_word(props + 28 * 32 + 8, 2);
    view.tables[3].count = boo_string + 5;
    CHECK(portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(c.boo_count == 1 && c.initial_boos[0].state == 16 &&
          c.initial_boos[0].heading == 0x4000 && c.boo_frame_ids[0][1] == 2);
    saved = c;
    test_mesh_word(props + 25 * 32 + 8, 2);
    CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error) &&
          !memcmp(&c, &saved, sizeof c));
    const unsigned rock_string = boo_string + 5;
    memcpy(strings + rock_string, "rocks", 6);
    test_mesh_word(props + 21 * 32 + 8, rock_string);
    test_mesh_word(props + 25 * 32 + 8, 3);
    test_mesh_word(props + 28 * 32 + 8, 5);
    view.tables[3].count = rock_string + 6;
    CHECK(portable_mesh_course_actors_config_open(&view, &c, error, sizeof error));
    CHECK(c.rock_count == 1 && c.initial_rocks[0].source_code == 0xa20003 &&
          c.initial_rocks[0].lake_rock.delay == 160 && c.rock_frame_ids[0][4] == 1 &&
          c.initial_rocks[0].affine_x_q8 == 256);
    const unsigned rock_affine_key = rock_string + 6;
    memcpy(strings + rock_affine_key, "source:affine-q8", 17);
    test_mesh_word(props + 36 * 32, rock_affine_key);
    test_mesh_word(props + 36 * 32 + 8, 224);
    test_mesh_word(object + 60, 16);
    view.tables[2].count = 37;
    view.tables[3].count = rock_affine_key + 17;
    CHECK(portable_mesh_course_actors_config_open(&view, &c, error, sizeof error) &&
          c.initial_rocks[0].affine_x_q8 == 224);
    saved = c;
    const uint32_t invalid_affines[] = {0, 257, UINT32_MAX};
    for (unsigned j = 0; j < 3; ++j)
    {
        test_mesh_word(props + 36 * 32 + 8, invalid_affines[j]);
        CHECK(!portable_mesh_course_actors_config_open(&view, &c, error, sizeof error) &&
              !memcmp(&c, &saved, sizeof c));
    }
    test_mesh_word(props + 36 * 32 + 8, 256);
    CHECK(portable_mesh_course_actors_config_open(&view, &c, error, sizeof error) &&
          c.initial_rocks[0].affine_x_q8 == 256);
    return 0;
}

static PortableMeshCourseActorsConfig snowy_config(void)
{
    PortableMeshCourseActorsConfig c = config();
    c.count = 0;
    c.snowman_count = 1;
    c.scale_q16 = 6554;
    portable_snowman_init(&c.initial_snowmen[0], 100 * Q, 100 * Q, 0, 0);
    c.initial_snowmen[0].placement_order = 10;
    memcpy(c.snowman_anchors_q16[0], c.anchors_q16[0], sizeof c.anchors_q16[0]);
    c.snowman_frame_ids[0][0] = 41;
    c.snowman_frame_ids[0][1] = 42;
    c.snowman_object_indices[0] = 7;
    return c;
}
static int snowman_motion(void)
{
    PortableMeshCourseActorsConfig c = snowy_config();
    PortableMeshCourseActors w;
    char error[160];
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_config_count(&c) == 1 &&
          portable_mesh_course_actors_count(&w) == 1);
    PortableMeshCourseActorBinding binding;
    PortableMeshCourseActorView view;
    CHECK(portable_mesh_course_actors_binding(&c, 0, &binding));
    CHECK(binding.object_index == 7 && binding.frame_count == 2 &&
          binding.frame_ids == c.snowman_frame_ids[0]);
    CHECK(portable_mesh_course_actors_view(&c, &w, 0, &view));
    CHECK(view.visible && view.model_id == 41 && view.object_index == 7 && !view.flip_y);
    CHECK(!memcmp(view.position_q16, c.snowman_anchors_q16[0], sizeof view.position_q16));
    const uint64_t fresh = portable_mesh_course_actors_checksum(&w);
    CHECK(fresh == UINT64_C(477122064282243033)); /* Unchanged pre-penguin snow-only MAS1. */
    CHECK(portable_snowman_contact(&w.snowmen[0], 0x2000, 0));
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.snowmen[0].state == 33 && w.snowmen[0].timer == 0 && w.snowmen[0].flags == 3);
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_view(&c, &w, 0, &view));
    CHECK(!view.visible && (w.snowmen[0].flags & 2));
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_view(&c, &w, 0, &view));
    CHECK(view.visible && view.model_id == 42);
    for (unsigned i = 3; i < 35; ++i)
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.snowmen[0].state == 34 && w.snowmen[0].timer == 35 && w.snowmen[0].art == 1 &&
          (w.snowmen[0].flags & 2));
    CHECK(portable_snowman_contact(&w.snowmen[0], 0x2000, 0));
    CHECK(w.snowmen[0].state == 48);
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.snowmen[0].state == 49 && !(w.snowmen[0].flags & 2));
    for (unsigned i = 0; i < 39; ++i)
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.snowmen[0].state == 49);
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.snowmen[0].state == 34 && !(w.snowmen[0].flags & 3));
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_checksum(&w) == fresh);
    PortableMeshCourseActors before = w;
    w.snowmen[0].timer = 42;
    before = w;
    CHECK(!portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(!memcmp(&w, &before, sizeof w));
    c.count = 1; /* Crab order9, snowman10; duplicates fail initialization. */
    c.initial_snowmen[0].placement_order = 9;
    CHECK(!portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(!memcmp(&w, &before, sizeof w));
    return 0;
}
static int snowman_contacts(void)
{
    PortableMeshCourseActorsConfig c = snowy_config();
    PortableMeshCourseActors w;
    char error[160];
    PortableMeshItemRules mir = rules();
    mir.lightning_scale_ratio_q16 = Q / 2;
    PortableMeshKartRules kart = test_mesh_rules();
    PortableCourse3DView course = {.id = 7};
    PortableMeshItemsConfig ic = {.course = &course,
                                  .rules = &mir,
                                  .kart_rules = &kart,
                                  .mode = 2,
                                  .count = 1,
                                  .human_mask = 1};
    PortableMeshItems items = {0};
    CHECK(portable_mesh_item_status_init(&items.status, 1, 1) == PORTABLE_MESH_ITEM_STATUS_OK);
    PortableMeshRacerState racer = {.phase = PORTABLE_MESH_RACER_RACING};
    racer.kart.up_q30[2] = 1073741824;
    racer.kart.forward_q30[0] = 1073741824;
    memcpy(racer.kart.position_q16, c.snowman_anchors_q16[0], sizeof racer.kart.position_q16);
    racer.kart.position_q16[0] += Q / 4;
    racer.kart.position_q16[2] += kart.kart_radius_q16 + kart.ride_height_q16;
    racer.kart.velocity_q16[0] = -4 * Q;
    racer.kart.velocity_q16[1] = Q;
    racer.kart.speed_q16 = -4 * Q;
    racer.kart.panel_ticks = 13;
    racer.kart.boost_ticks[0] = 17;
    racer.kart.drift_charge = 31;
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    PortableMeshCourseActorEvents e = {0};
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &racer, 1, &e, error,
                                              sizeof error));
    CHECK(e.bump_mask == 1 && !e.hit_mask && e.snowman_contacts == 1 && e.snowman_changed == 1);
    CHECK(racer.kart.velocity_q16[0] == 2 * Q && racer.kart.velocity_q16[1] == Q &&
          racer.kart.speed_q16 == 2 * Q);
    CHECK(!items.status.racers[0].spin_ticks && !items.status.racers[0].hit_grace_ticks &&
          w.actor_grace[0] == 8);
    CHECK(racer.kart.panel_ticks == 13 && racer.kart.boost_ticks[0] == 17 &&
          racer.kart.drift_charge == 31);
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &racer, 1, &e, error,
                                              sizeof error));
    CHECK(!e.bump_mask && !e.snowman_contacts);
    for (unsigned i = 0; i < 8; ++i)
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.actor_grace[0] == 0);
    /* A protected racer breaks the two stages on consecutive accepted turns. */
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    items.status.racers[0].star_ticks = 5;
    PortableMeshRacerState protected_before = racer;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &racer, 1, &e, error,
                                              sizeof error));
    CHECK(e.snowman_changed == 1 && e.protected_mask == 1 && !e.bump_mask && !e.hit_mask &&
          !w.actor_grace[0]);
    CHECK(!memcmp(&racer, &protected_before, sizeof racer));
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &racer, 1, &e, error,
                                              sizeof error));
    CHECK(w.snowmen[0].state == 48 && e.snowman_changed == 1 && e.protected_mask == 1 &&
          !e.hit_mask && !e.bump_mask);
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(!(w.snowmen[0].flags & 2));
    /* Mixed-family source order: snowman10 sets common grace before crab9. */
    c.count = 1;
    c.initial[0].placement_order = 9;
    memcpy(c.anchors_q16[0], c.snowman_anchors_q16[0], sizeof c.anchors_q16[0]);
    items.status.racers[0].star_ticks = 0;
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &racer, 1, &e, error,
                                              sizeof error));
    CHECK(e.bump_mask == 1 && !e.hit_mask);
    c.initial[0].placement_order = 11;
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &racer, 1, &e, error,
                                              sizeof error));
    CHECK(e.hit_mask == 1 && !e.bump_mask && !e.snowman_contacts);
    /* Upper deck and shrunken lateral body remain genuinely separate. */
    c.count = 0;
    memset(&items.status.racers[0], 0, sizeof items.status.racers[0]);
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    racer.kart.position_q16[2] += 4 * Q;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &racer, 1, &e, error,
                                              sizeof error));
    CHECK(!e.bump_mask);
    racer.kart.position_q16[0] = c.snowman_anchors_q16[0][0] + 29491;
    racer.kart.position_q16[2] =
        c.snowman_anchors_q16[0][2] + (kart.kart_radius_q16 + kart.ride_height_q16) / 2;
    items.status.racers[0].lightning_ticks = 1;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &racer, 1, &e, error,
                                              sizeof error));
    CHECK(!e.bump_mask);
    PortableMeshCourseActors saved = w;
    PortableMeshRacerState saved_racer = racer;
    PortableMeshItems saved_items = items;
    PortableMeshCourseActorEvents saved_e = e;
    w.snowmen[0].state = 99;
    saved = w;
    CHECK(!portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &racer, 1, &e, error,
                                               sizeof error));
    CHECK(!memcmp(&saved, &w, sizeof w) && !memcmp(&racer, &saved_racer, sizeof racer) &&
          !memcmp(&items, &saved_items, sizeof items) && !memcmp(&e, &saved_e, sizeof e));
    return 0;
}

static PortableMeshCourseActorsConfig penguin_config(void)
{
    PortableMeshCourseActorsConfig c = snowy_config();
    c.snowman_count = 0;
    c.penguin_count = 1;
    portable_penguin_init(&c.initial_penguins[0], 100 * Q, 100 * Q, 0, 1);
    c.initial_penguins[0].placement_order = 12;
    memcpy(c.penguin_anchors_q16[0], c.snowman_anchors_q16[0], sizeof c.penguin_anchors_q16[0]);
    for (unsigned i = 0; i < 6; ++i)
        c.penguin_frame_ids[0][i] = 51 + i;
    c.penguin_object_indices[0] = 8;
    return c;
}
static int penguin_motion(void)
{
    PortableMeshCourseActorsConfig c = penguin_config();
    PortableMeshCourseActors w;
    char error[160];
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_config_count(&c) == 1 &&
          portable_mesh_course_actors_count(&w) == 1);
    PortableMeshCourseActorBinding binding;
    PortableMeshCourseActorView v;
    CHECK(portable_mesh_course_actors_binding(&c, 0, &binding));
    CHECK(binding.frame_count == 6 && binding.frame_ids == c.penguin_frame_ids[0] &&
          binding.object_index == 8);
    const uint64_t fresh = portable_mesh_course_actors_checksum(&w);
    CHECK(fresh == UINT64_C(9119787650000586800)); /* Pre-mouse PNG1 fixture hash. */
    for (unsigned i = 0; i < 6; ++i)
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.penguins[0].timer == 6 && w.penguins[0].art == 0);
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.penguins[0].art == 1);
    CHECK(portable_mesh_course_actors_view(&c, &w, 0, &v));
    CHECK(v.visible && !v.flip_y && v.model_id == 52);
    for (unsigned i = 7; i < 42; ++i)
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.penguins[0].timer == 0 && w.penguins[0].art == 5);
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.penguins[0].timer == 1 && w.penguins[0].art == 0);
    CHECK(portable_penguin_contact(&w.penguins[0], 0x2000, 0, 1));
    CHECK(portable_mesh_course_actors_view(&c, &w, 0, &v));
    CHECK(v.visible && v.flip_y);
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.penguins[0].height_q16 == 24 * Q && w.penguins[0].timer == 31);
    for (unsigned i = 1; i < 32; ++i)
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.penguins[0].timer == 0 && (w.penguins[0].flags & 1));
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(!(w.penguins[0].flags & 1));
    CHECK(portable_mesh_course_actors_view(&c, &w, 0, &v));
    CHECK(!v.visible);
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_checksum(&w) == fresh);
    w.penguins[0].art = 6;
    PortableMeshCourseActors saved = w;
    CHECK(!portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(!memcmp(&w, &saved, sizeof w));
    /* Corrupt each normal/flight/finished envelope independently. Neither
     * advancing nor reading a frame may publish a partially valid actor. */
    for (unsigned bad = 0; bad < 11; ++bad)
    {
        CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
        PortablePenguin *actor = &w.penguins[0];
        if (bad == 0)
            actor->flags = 2;
        if (bad == 1)
            actor->affine_y_q8 = -256;
        if (bad == 2)
            actor->height_q16 = 1;
        if (bad == 3)
            actor->timer = 42;
        if (bad >= 4 && bad <= 6)
        {
            actor->flags = 1;
            actor->affine_y_q8 = -256;
            actor->timer = 32;
            if (bad == 4)
                actor->affine_y_q8 = 256;
            if (bad == 5)
                actor->height_q16 = 24 * Q + 1;
            if (bad == 6)
                actor->height_q16 = -24 * Q - 1;
        }
        if (bad >= 7 && bad <= 9)
        {
            actor->flags = 0;
            actor->affine_y_q8 = -256;
            actor->timer = -1;
            actor->height_q16 = c.sine[0] * 48;
            if (bad == 7)
                actor->timer = 0;
            if (bad == 8)
                actor->affine_y_q8 = 256;
            if (bad == 9)
                ++actor->height_q16;
        }
        if (bad == 10)
            actor->flags = 7;
        saved = w;
        PortableMeshCourseActorView untouched = {.model_id = 123};
        const PortableMeshCourseActorView before_view = untouched;
        CHECK(!portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
        CHECK(!memcmp(&w, &saved, sizeof w));
        CHECK(!portable_mesh_course_actors_view(&c, &w, 0, &untouched));
        CHECK(!memcmp(&untouched, &before_view, sizeof untouched));
    }
    /* Signed authored sine is allowed in the protected flight envelope. */
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    c.sine[512] = -32768;
    CHECK(portable_penguin_contact(&w.penguins[0], 0x2000, 0, 1));
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.penguins[0].height_q16 == -24 * Q);
    CHECK(portable_mesh_course_actors_view(&c, &w, 0, &v));
    CHECK(v.visible && v.flip_y);
    c.sine[0] = -32768;
    for (unsigned i = 0; i < 32; ++i)
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.penguins[0].flags == 0 && w.penguins[0].timer == -1 &&
          w.penguins[0].height_q16 == -24 * Q);
    CHECK(portable_mesh_course_actors_view(&c, &w, 0, &v));
    CHECK(!v.visible);
    c.count = 1;
    c.snowman_count = 1;
    c.initial_penguins[0].placement_order = 10;
    saved = w;
    CHECK(!portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(!memcmp(&w, &saved, sizeof w));
    return 0;
}
static int penguin_contacts(void)
{
    PortableMeshCourseActorsConfig c = penguin_config();
    PortableMeshCourseActors w;
    char error[160];
    PortableMeshItemRules mir = rules();
    mir.lightning_scale_ratio_q16 = Q / 2;
    PortableMeshKartRules kart = test_mesh_rules();
    PortableCourse3DView course = {.id = 7};
    PortableMeshItemsConfig ic = {.course = &course,
                                  .rules = &mir,
                                  .kart_rules = &kart,
                                  .mode = 2,
                                  .count = 1,
                                  .human_mask = 1};
    PortableMeshItems items = {0};
    CHECK(portable_mesh_item_status_init(&items.status, 1, 1) == PORTABLE_MESH_ITEM_STATUS_OK);
    PortableMeshRacerState r = {.phase = PORTABLE_MESH_RACER_RACING};
    r.kart.up_q30[2] = 1073741824;
    r.kart.forward_q30[0] = 1073741824;
    memcpy(r.kart.position_q16, c.penguin_anchors_q16[0], sizeof r.kart.position_q16);
    r.kart.position_q16[2] += kart.kart_radius_q16 + kart.ride_height_q16;
    r.kart.speed_q16 = 8 * Q;
    r.kart.velocity_q16[0] = 8 * Q;
    r.kart.panel_ticks = 5;
    r.kart.drift_charge = 17;
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    PortableMeshCourseActorEvents e = {0};
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(e.hit_mask == 1 && e.penguin_contacts == 1 && !e.hit_actors && !e.penguin_defeated &&
          !e.bump_mask);
    CHECK(items.status.racers[0].spin_ticks == 3 && r.kart.speed_q16 == 4 * Q &&
          !r.kart.panel_ticks && !r.kart.drift_charge && (w.penguins[0].flags & 2));
    memset(&items.status.racers[0], 0, sizeof items.status.racers[0]);
    items.status.racers[0].star_ticks = 5;
    const PortableMeshRacerState protected_r = r;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(e.protected_mask == 1 && e.penguin_contacts == 1 && e.penguin_defeated == 1 &&
          !e.hit_mask && !e.protected_actors);
    CHECK(w.penguins[0].affine_y_q8 == -256 && !(w.penguins[0].flags & 2));
    CHECK(!memcmp(&r, &protected_r, sizeof r));
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    memset(&items.status.racers[0], 0, sizeof items.status.racers[0]);
    r.kart.position_q16[2] += 4 * Q;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!e.penguin_contacts);
    r.kart.position_q16[2] =
        c.penguin_anchors_q16[0][2] + kart.kart_radius_q16 + kart.ride_height_q16;
    items.status.racers[0].boo_invisible_ticks = 3;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!e.penguin_contacts);
    items.status.racers[0].boo_invisible_ticks = 0;
    r.respawn_grace = 1;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!e.penguin_contacts);
    r.respawn_grace = 0;
    r.kart.position_q16[0] += 29491;
    r.kart.position_q16[2] =
        c.penguin_anchors_q16[0][2] + (kart.kart_radius_q16 + kart.ride_height_q16) / 2;
    items.status.racers[0].lightning_ticks = 1;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!e.penguin_contacts);
    /* A snowman ordered before the penguin grants common actor grace; reverse
     * order gives the real penguin spin and blocks the snowman bump. */
    c.snowman_count = 1;
    memcpy(c.snowman_anchors_q16[0], c.penguin_anchors_q16[0], sizeof c.snowman_anchors_q16[0]);
    c.initial_snowmen[0].placement_order = 13;
    memset(&items.status.racers[0], 0, sizeof items.status.racers[0]);
    r.kart.position_q16[0] = c.penguin_anchors_q16[0][0];
    r.kart.position_q16[2] =
        c.penguin_anchors_q16[0][2] + kart.kart_radius_q16 + kart.ride_height_q16;
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(e.bump_mask == 1 && !e.penguin_contacts && !e.hit_mask);
    c.initial_snowmen[0].placement_order = 11;
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(e.hit_mask == 1 && e.penguin_contacts == 1 && !e.bump_mask);
    PortableMeshCourseActors before = w;
    PortableMeshItems saved_items = items;
    PortableMeshRacerState saved_r = r;
    PortableMeshCourseActorEvents saved_e = e;
    w.penguins[0].affine_y_q8 = 0;
    before = w;
    CHECK(
        !portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!memcmp(&w, &before, sizeof w) && !memcmp(&items, &saved_items, sizeof items) &&
          !memcmp(&r, &saved_r, sizeof r) && !memcmp(&e, &saved_e, sizeof e));
    return 0;
}

static PortableMeshCourseActorsConfig mouse_config(uint16_t variant)
{
    PortableMeshCourseActorsConfig c = penguin_config();
    c.penguin_count = 0;
    c.mouse_count = 1;
    portable_mouse_init(&c.initial_mice[0], 100 * Q, 100 * Q, 0, variant);
    c.initial_mice[0].placement_order = 14;
    memcpy(c.mouse_anchors_q16[0], c.penguin_anchors_q16[0], sizeof c.mouse_anchors_q16[0]);
    c.mouse_object_indices[0] = 9;
    c.mouse_frame_ids[0][0] = 61;
    c.mouse_frame_ids[0][1] = 62;
    return c;
}
static int mouse_motion(void)
{
    PortableMeshCourseActorsConfig c = mouse_config(0);
    c.mouse_count = 3;
    portable_mouse_init(&c.initial_mice[1], 100 * Q, 100 * Q, 0, 1);
    c.initial_mice[1].placement_order = 15;
    portable_mouse_init(&c.initial_mice[2], 100 * Q, 100 * Q, 0, 0x8001);
    c.initial_mice[2].placement_order = 16;
    for (unsigned i = 1; i < 3; ++i)
    {
        memcpy(c.mouse_anchors_q16[i], c.mouse_anchors_q16[0], sizeof c.mouse_anchors_q16[0]);
        c.mouse_frame_ids[i][0] = 61;
        c.mouse_frame_ids[i][1] = 62;
        c.mouse_object_indices[i] = 9 + i;
    }
    PortableMeshCourseActors w;
    char error[160];
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    const uint64_t fresh = portable_mesh_course_actors_checksum(&w);
    PortableMeshCourseActorView v;
    PortableMeshCourseActorBinding binding;
    CHECK(portable_mesh_course_actors_config_count(&c) == 3 &&
          portable_mesh_course_actors_count(&w) == 3);
    CHECK(portable_mesh_course_actors_binding(&c, 1, &binding));
    CHECK(binding.frame_ids == c.mouse_frame_ids[1] && binding.frame_count == 2);
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.mice[0].x_q16 == 100 * Q + Q / 2 && w.mice[1].y_q16 == 100 * Q + Q / 2);
    CHECK(portable_mesh_course_actors_view(&c, &w, 0, &v));
    CHECK(v.has_facing && v.facing_x_q16 == -Q / 2 && !v.facing_y_q16 && !v.turn_flip);
    CHECK(w.mice[0].velocity_x_q16 == Q / 2); /* Facing snapshots precede reversal. */
    CHECK(portable_mesh_course_actors_view(&c, &w, 2, &v));
    CHECK(!v.has_facing);
    for (unsigned i = 1; i < 59; ++i)
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.mice[2].timer == 59 && !w.mice[2].hop_phase && !w.mice[2].height_q16);
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.mice[2].timer == 60 && w.mice[2].hop_phase && !w.mice[2].height_q16);
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.mice[2].height_q16 == 2 * Q);
    for (unsigned i = 61; i < 77; ++i)
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.mice[2].timer == 0 && !w.mice[2].hop_phase && !w.mice[2].height_q16);
    for (unsigned i = 77; i < 192; ++i)
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_view(&c, &w, 0, &v));
    CHECK(v.turn_flip);
    for (unsigned i = 192; i < 621; ++i)
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.mice[0].timer == 0 && w.mice[1].timer == 0);
    CHECK(w.mice[0].x_q16 == 100 * Q + Q / 2 && w.mice[1].y_q16 == 100 * Q + Q / 2);
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_checksum(&w) == fresh);
    portable_mouse_contact(&w.mice[0], 0x2000, 0, 1);
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.mice[0].height_q16 == 24 * Q && w.mice[0].timer == 31);
    CHECK(portable_mesh_course_actors_view(&c, &w, 0, &v));
    CHECK(v.flip_y && v.visible && v.has_facing);
    for (unsigned i = 1; i < 33; ++i)
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.mice[0].timer == -1 && !(w.mice[0].flags & 1));
    /* Protect the hopping variant at the actual nine-unit apex: it follows a
     * longer ballistic arc, not the walking variant's sine-removal clock. */
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    for (unsigned i = 0; i < 68; ++i)
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.mice[2].height_q16 == 9 * Q);
    portable_mouse_contact(&w.mice[2], 0x2000, 0, 1);
    for (unsigned i = 0; i < 27; ++i)
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.mice[2].height_q16 > 0 && (w.mice[2].flags & 1) && w.mice[2].timer == 32);
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(!w.mice[2].height_q16 && !(w.mice[2].flags & 1) && w.mice[2].timer == 32);
    CHECK(portable_mesh_course_actors_view(&c, &w, 2, &v));
    CHECK(!v.visible && v.flip_y && !v.has_facing);
    return 0;
}
static int mouse_hop_contacts_all_phases(void)
{
    PortableMeshCourseActorsConfig c = mouse_config(0x8001);
    char error[160];
    /* Includes the initial cycle, apex, descent and post-landing timer0/1
     * whose retained negative vertical velocity differs from fresh init. */
    for (unsigned phase = 0; phase <= 78; ++phase)
    {
        PortableMeshCourseActors w;
        CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
        for (unsigned tick = 0; tick < phase; ++tick)
            CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
        portable_mouse_contact(&w.mice[0], 0x2000, 0, 1);
        PortableMeshCourseActorView v;
        CHECK(portable_mesh_course_actors_view(&c, &w, 0, &v));
        CHECK(v.visible && v.flip_y && !v.has_facing);
        unsigned steps = 0;
        while (w.mice[0].flags & 1)
        {
            CHECK(++steps <= 28);
            CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
        }
        CHECK(steps >= 25 && w.mice[0].timer == 32 && !w.mice[0].height_q16);
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
        CHECK(portable_mesh_course_actors_view(&c, &w, 0, &v));
        CHECK(!v.visible);
    }
    /* Walking protected flight permits authored signed sine, including its
     * retained final height after visibility is cleared. */
    c = mouse_config(1);
    c.sine[512] = -32768;
    c.sine[0] = -32768;
    PortableMeshCourseActors w;
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    portable_mouse_contact(&w.mice[0], 0x2000, 0, 1);
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.mice[0].height_q16 == -24 * Q);
    for (unsigned i = 1; i < 33; ++i)
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.mice[0].height_q16 == -24 * Q && w.mice[0].timer == -1 && !w.mice[0].flags);
    return 0;
}
static int mouse_contact(void)
{
    PortableMeshCourseActorsConfig c = mouse_config(0);
    PortableMeshCourseActors w;
    char error[160];
    PortableMeshItemRules mir = rules();
    mir.lightning_scale_ratio_q16 = Q / 2;
    PortableMeshKartRules kart = test_mesh_rules();
    PortableCourse3DView course = {.id = 7};
    PortableMeshItemsConfig ic = {.course = &course,
                                  .rules = &mir,
                                  .kart_rules = &kart,
                                  .mode = 2,
                                  .count = 1,
                                  .human_mask = 1};
    PortableMeshItems items = {0};
    CHECK(portable_mesh_item_status_init(&items.status, 1, 1) == PORTABLE_MESH_ITEM_STATUS_OK);
    PortableMeshRacerState r = {.phase = PORTABLE_MESH_RACER_RACING};
    r.kart.up_q30[2] = 1073741824;
    r.kart.forward_q30[0] = 1073741824;
    memcpy(r.kart.position_q16, c.mouse_anchors_q16[0], sizeof r.kart.position_q16);
    r.kart.position_q16[2] += kart.kart_radius_q16 + kart.ride_height_q16;
    r.kart.speed_q16 = 8 * Q;
    r.kart.velocity_q16[0] = 8 * Q;
    r.kart.boost_ticks[0] = 17;
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    PortableMeshCourseActorEvents e = {0};
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(e.mouse_contacts == 1 && e.hit_mask == 1 && !e.mouse_defeated && !e.hit_actors);
    CHECK(items.status.racers[0].spin_ticks == 3 && r.kart.speed_q16 == 4 * Q &&
          !r.kart.boost_ticks[0]);
    memset(&items.status.racers[0], 0, sizeof items.status.racers[0]);
    items.status.racers[0].star_ticks = 5;
    PortableMeshRacerState protected = r;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(e.protected_mask == 1 && e.mouse_defeated == 1 && !e.hit_mask &&
          w.mice[0].affine_y_q8 == -256 && !(w.mice[0].flags & 2));
    CHECK(!memcmp(&r, &protected, sizeof r));
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    memset(&items.status.racers[0], 0, sizeof items.status.racers[0]);
    r.kart.position_q16[2] += 4 * Q;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!e.mouse_contacts);
    r.kart.position_q16[2] =
        c.mouse_anchors_q16[0][2] + kart.kart_radius_q16 + kart.ride_height_q16;
    items.status.racers[0].boo_invisible_ticks = 3;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!e.mouse_contacts);
    items.status.racers[0].boo_invisible_ticks = 0;
    items.controls[0].recovering = 1;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!e.mouse_contacts);
    items.controls[0].recovering = 0;
    r.kart.position_q16[0] += 29491;
    r.kart.position_q16[2] =
        c.mouse_anchors_q16[0][2] + (kart.kart_radius_q16 + kart.ride_height_q16) / 2;
    items.status.racers[0].lightning_ticks = 1;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!e.mouse_contacts);
    c.snowman_count = 1;
    c.initial_snowmen[0].placement_order = 15;
    memcpy(c.snowman_anchors_q16[0], c.mouse_anchors_q16[0], sizeof c.snowman_anchors_q16[0]);
    memset(&items.status.racers[0], 0, sizeof items.status.racers[0]);
    r.kart.position_q16[0] = c.mouse_anchors_q16[0][0];
    r.kart.position_q16[2] =
        c.mouse_anchors_q16[0][2] + kart.kart_radius_q16 + kart.ride_height_q16;
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(e.bump_mask == 1 && !e.mouse_contacts && !e.hit_mask);
    c.initial_snowmen[0].placement_order = 13;
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(e.hit_mask == 1 && e.mouse_contacts == 1 && !e.bump_mask);
    return 0;
}
static int mouse_invalid(void)
{
    char error[160];
    const uint16_t variants[3] = {0, 1, 0x8001};
    for (unsigned variant = 0; variant < 3; ++variant)
    {
        PortableMeshCourseActorsConfig c = mouse_config(variants[variant]);
        PortableMeshCourseActors w;
        for (unsigned bad = 0; bad < 8; ++bad)
        {
            CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
            PortableMouse *a = &w.mice[0];
            if (bad == 0)
                a->variant = 2;
            if (bad == 1)
                a->flags = 2;
            if (bad == 2)
                a->affine_y_q8 = 0;
            if (bad == 3)
                a->art = 2;
            if (bad == 4)
                a->timer = 621;
            if (bad == 5)
                a->walk_ticks = 10;
            if (bad == 6)
                a->height_q16 = 1;
            if (bad == 7)
                a->velocity_x_q16 = 1;
            const PortableMeshCourseActors before = w;
            PortableMeshCourseActorView v = {.model_id = 123}, saved_v = v;
            CHECK(!portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
            CHECK(!memcmp(&before, &w, sizeof w));
            CHECK(!portable_mesh_course_actors_view(&c, &w, 0, &v));
            CHECK(!memcmp(&v, &saved_v, sizeof v));
        }
        CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
        portable_mouse_contact(&w.mice[0], 0x2000, 0, 1);
        w.mice[0].timer = 31;
        const PortableMeshCourseActors before = w;
        CHECK(!portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
        CHECK(!memcmp(&before, &w, sizeof w));
    }
    return 0;
}

static PortableMeshCourseActorsConfig piranha_config(void)
{
    PortableMeshCourseActorsConfig c = mouse_config(0);
    c.mouse_count = 0;
    c.piranha_count = 1;
    portable_piranha_init(&c.initial_piranhas[0], 100 * Q, 100 * Q, 0, 0);
    c.initial_piranhas[0].placement_order = 17;
    memcpy(c.piranha_anchors_q16[0], c.mouse_anchors_q16[0], sizeof c.piranha_anchors_q16[0]);
    c.piranha_object_indices[0] = 10;
    for (unsigned i = 0; i < 5; ++i)
        c.piranha_frame_ids[0][i] = 71 + i;
    return c;
}
static int piranha_motion(void)
{
    PortableMeshCourseActorsConfig c = piranha_config();
    PortableMeshCourseActors w;
    char error[160];
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_count(&w) == 1 &&
          portable_mesh_course_actors_config_count(&c) == 1 && w.piranha_victim[0] == 255);
    const uint64_t fresh = portable_mesh_course_actors_checksum(&w);
    CHECK(fresh == UINT64_C(2586974600549296886)); /* Pre-seagull PIR1 bytes. */
    PortableMeshCourseActorBinding binding;
    PortableMeshCourseActorView view;
    CHECK(portable_mesh_course_actors_binding(&c, 0, &binding));
    CHECK(binding.frame_count == 5 && binding.frame_ids == c.piranha_frame_ids[0]);
    CHECK(portable_mesh_course_actors_view(&c, &w, 0, &view));
    CHECK(!view.visible && view.affine_x_q8 == 224 && view.affine_y_q8 == 224);
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.piranhas[0].state == 1 && w.piranhas[0].flags == 2);
    CHECK(portable_piranha_contact(&w.piranhas[0], 0x2000, 0, 0, 1));
    w.piranha_victim[0] = 1;
    PortableMeshRacerState racers[2] = {{.phase = PORTABLE_MESH_RACER_RACING},
                                        {.phase = PORTABLE_MESH_RACER_RACING}};
    PortableMeshCourseActorEvents e = {0};
    for (unsigned tick = 1; tick <= 24; ++tick)
    {
        PortableMeshCourseActors before = w;
        CHECK(
            portable_mesh_course_actors_advance_events(&c, &w, racers, 2, &e, error, sizeof error));
        CHECK(e.piranha_bite_mask == (tick == 9 ? 2 : 0));
        CHECK(w.piranhas[0].art == (tick < 5    ? 0
                                    : tick < 9  ? 1
                                    : tick < 16 ? 2
                                    : tick < 21 ? 4
                                                : 3));
        CHECK(w.piranhas[0].state == (tick == 24 ? 0 : 17) && w.piranhas[0].flags == 1);
        if (tick == 9)
        {
            PortableMeshCourseActors finished = before;
            PortableMeshCourseActorEvents suppressed = {0};
            racers[1].phase = PORTABLE_MESH_RACER_FINISHED;
            CHECK(portable_mesh_course_actors_advance_events(&c, &finished, racers, 2, &suppressed,
                                                             error, sizeof error));
            CHECK(!suppressed.piranha_bite_mask && !memcmp(&finished, &w, sizeof w));
            racers[1].phase = PORTABLE_MESH_RACER_RACING;
        }
    }
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.piranhas[0].state == 1 && w.piranhas[0].flags == 2);
    /* Protected removal from a hidden collidable plant stays invisible. */
    CHECK(portable_piranha_contact(&w.piranhas[0], 0x2000, 0, 1, 1));
    for (unsigned tick = 1; tick <= 26; ++tick)
    {
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
        CHECK(w.piranhas[0].height_q16 ==
              (int32_t)((int64_t)tick * 3 * Q - ((int64_t)tick * (tick - 1) / 2) * (Q / 4)));
        CHECK(!(w.piranhas[0].flags & 3) && w.piranhas[0].flip_y);
    }
    CHECK(w.piranhas[0].state == 243);
    CHECK(portable_mesh_course_actors_view(&c, &w, 0, &view));
    CHECK(!view.visible && view.flip_y && view.affine_x_q8 == 224 && view.affine_y_q8 == 256);
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_checksum(&w) == fresh);
    const PortableMeshCourseActors saved = w;
    w.piranhas[0].state = 18;
    PortableMeshCourseActors bad = w;
    CHECK(!portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(!memcmp(&bad, &w, sizeof w));
    w = saved;
    w.piranha_victim[0] = 8;
    bad = w;
    CHECK(!portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(!memcmp(&bad, &w, sizeof w));
    return 0;
}
static int piranha_contacts(void)
{
    PortableMeshCourseActorsConfig c = piranha_config();
    PortableMeshCourseActors w;
    char error[160];
    PortableMeshItemRules mir = rules();
    PortableMeshKartRules kart = test_mesh_rules();
    PortableCourse3DView course = {.id = 7};
    PortableMeshItemsConfig ic = {.course = &course,
                                  .rules = &mir,
                                  .kart_rules = &kart,
                                  .mode = 8,
                                  .count = 2,
                                  .human_mask = 3};
    PortableMeshItems items = {0};
    CHECK(portable_mesh_item_status_init(&items.status, 2, 3) == PORTABLE_MESH_ITEM_STATUS_OK);
    PortableMeshRacerState r[2] = {{.phase = PORTABLE_MESH_RACER_RACING},
                                   {.phase = PORTABLE_MESH_RACER_RACING}};
    for (unsigned i = 0; i < 2; ++i)
    {
        memcpy(r[i].kart.position_q16, c.piranha_anchors_q16[0], sizeof r[i].kart.position_q16);
        r[i].kart.position_q16[2] += kart.kart_radius_q16 + kart.ride_height_q16;
        r[i].kart.up_q30[2] = 1073741824;
        r[i].kart.forward_q30[0] = 1073741824;
        r[i].kart.velocity_q16[0] = 5 * Q;
        r[i].kart.speed_q16 = 5 * Q;
        r[i].kart.boost_ticks[0] = 11;
        r[i].kart.drift_charge = 19;
    }
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    PortableMeshCourseActorEvents e = {0};
    PortableMeshRacerState before[2];
    memcpy(before, r, sizeof r);
    const PortableMeshItemStatusOwner before_status = items.status;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, r, 2, &e, error, sizeof error));
    CHECK(e.piranha_contacts == 1 && e.piranha_touch_mask == 3 && !e.hit_mask && !e.bump_mask &&
          !e.protected_mask);
    CHECK(w.piranhas[0].state == 16 && w.piranha_victim[0] == 1 && w.actor_grace[0] == 8 &&
          w.actor_grace[1] == 8);
    CHECK(!memcmp(before, r, sizeof r) &&
          !memcmp(&before_status, &items.status, sizeof items.status));
    for (unsigned tick = 1; tick <= 9; ++tick)
        CHECK(portable_mesh_course_actors_advance_events(&c, &w, r, 2, &e, error, sizeof error));
    CHECK(e.piranha_bite_mask == 2);
    e.capture_mask = 1;
    e.launch_mask = 2;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, r, 2, &e, error, sizeof error));
    CHECK(e.piranha_bite_mask == 2 && e.capture_mask == 1 && e.launch_mask == 2 &&
          !e.piranha_contacts);
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    items.status.racers[0].star_ticks = 5;
    r[1].respawn_grace = 1;
    e = (PortableMeshCourseActorEvents){0};
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, r, 2, &e, error, sizeof error));
    CHECK(e.piranha_protected == 1 && e.piranha_touch_mask == 1 && !e.protected_mask &&
          !e.hit_mask && !w.actor_grace[0]);
    CHECK(w.piranhas[0].state == 240 && !memcmp(before, r, sizeof r[0]));
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.piranhas[0].state == 241 && !w.piranhas[0].flags);
    /* A swallowed racer still triggers the passive callback after rearm even
     * though its item controls are marked recovering and actor grace is live. */
    c.capture_surface_count = 1;
    c.capture_surfaces[0] = 0;
    c.capture_forward_speed_q16 = 31 * Q;
    c.capture_up_speed_q16 = 8 * Q;
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    items.status.racers[0].star_ticks = 0;
    w.capture[0] = (PortableMeshCaptureState){.remaining = 92, .actor_index_plus1 = 1};
    w.capture[0].forward_q30[1] = -1073741824;
    w.capture[0].up_q30[2] = 1073741824;
    w.actor_grace[0] = 4;
    items.controls[0].recovering = 1;
    e = (PortableMeshCourseActorEvents){0};
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, r, 2, &e, error, sizeof error));
    CHECK(e.piranha_contacts == 1 && e.piranha_touch_mask == 1 && w.piranhas[0].state == 16);
    /* Late-invalid second racer keeps all staged contact results unpublished. */
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    items.controls[0].recovering = 0;
    r[1].respawn_grace = 0;
    r[1].kart.up_q30[2] = INT32_MAX;
    const PortableMeshCourseActors saved = w;
    const PortableMeshItems saved_items = items;
    memcpy(before, r, sizeof r);
    const PortableMeshCourseActorEvents saved_e = e;
    CHECK(!portable_mesh_course_actors_resolve(&c, &w, &items, &ic, r, 2, &e, error, sizeof error));
    CHECK(!memcmp(&w, &saved, sizeof w) && !memcmp(&items, &saved_items, sizeof items) &&
          !memcmp(r, before, sizeof r) && !memcmp(&e, &saved_e, sizeof e));
    return 0;
}

static PortableMeshCourseActorsConfig seagull_config(void)
{
    PortableMeshCourseActorsConfig c = piranha_config();
    c.piranha_count = 0;
    c.seagull_count = 1;
    portable_seagull_init(&c.initial_seagulls[0], 100 * Q, 100 * Q, 0, 0);
    c.initial_seagulls[0].placement_order = 18;
    memcpy(c.seagull_anchors_q16[0], c.piranha_anchors_q16[0], sizeof c.seagull_anchors_q16[0]);
    c.seagull_object_indices[0] = 11;
    for (unsigned i = 0; i < 3; ++i)
        c.seagull_frame_ids[0][i] = 81 + i;
    return c;
}
static int seagull_motion(void)
{
    PortableMeshCourseActorsConfig c = seagull_config();
    PortableMeshCourseActors w;
    char error[160];
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    const uint64_t fresh = portable_mesh_course_actors_checksum(&w);
    PortableMeshCourseActorBinding binding;
    PortableMeshCourseActorView view;
    CHECK(portable_mesh_course_actors_binding(&c, 0, &binding));
    CHECK(binding.frame_count == 3 && binding.frame_ids == c.seagull_frame_ids[0]);
    CHECK(portable_mesh_course_actors_view(&c, &w, 0, &view));
    CHECK(view.visible && !view.has_facing && view.affine_x_q8 == 224 && view.affine_y_q8 == 224);
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.seagulls[0].state == 1 && w.seagulls[0].affine_x_q8 == 256);
    CHECK(portable_mesh_course_actors_view(&c, &w, 0, &view));
    CHECK(view.has_facing && view.facing_x_q16 == Q && !view.facing_y_q16 && view.turn_flip &&
          view.facing_half_turn);
    portable_seagull_contact(&w.seagulls[0], 0x2000, 0, 0);
    for (unsigned tick = 1; tick <= 40; ++tick)
    {
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
        CHECK(w.seagulls[0].timer == (int32_t)(tick % 5) &&
              w.seagulls[0].phase == (int32_t)(tick / 5));
        CHECK(w.seagulls[0].height_q16 == c.sine[((tick / 5) * 4 + tick % 5) * 16] * 20);
        CHECK(w.seagulls[0].x_q16 == 100 * Q && w.seagulls[0].y_q16 == 100 * Q &&
              w.seagulls[0].flags == 3);
        if (tick < 40)
        {
            CHECK(w.seagulls[0].state == 17 && w.seagulls[0].art == 1 + (tick / 5) % 2);
            PortableSeagull before = w.seagulls[0];
            portable_seagull_contact(&w.seagulls[0], 0x2000, 0, 0);
            CHECK(!memcmp(&before, &w.seagulls[0], sizeof before));
        }
    }
    CHECK(w.seagulls[0].state == 0);
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.seagulls[0].state == 1 && w.seagulls[0].art == 0 && !w.seagulls[0].height_q16);
    portable_seagull_contact(&w.seagulls[0], 0x2000, 0, 0);
    CHECK(w.seagulls[0].state == 16);
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_checksum(&w) == fresh);
    /* Protect at every flutter phase, including positive/negative authored
     * heights. A below-ground start can terminate on its first update. */
    for (unsigned i = 0; i < 1024; ++i)
        c.sine[i] = (i & 16) ? -32768 : 32768;
    for (unsigned phase = 0; phase <= 40; ++phase)
    {
        CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
        portable_seagull_contact(&w.seagulls[0], 0x2000, 0, 0);
        for (unsigned i = 0; i < phase; ++i)
            CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
        const int32_t start = w.seagulls[0].height_q16;
        portable_seagull_contact(&w.seagulls[0], 0x2000, 0, 1);
        unsigned n = 0;
        while (w.seagulls[0].state != 243)
        {
            CHECK(++n <= 28);
            CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
            CHECK(w.seagulls[0].height_q16 ==
                  start + (int32_t)(196608 * (int64_t)n - 8192 * (int64_t)n * (n - 1)));
        }
        CHECK(!w.seagulls[0].flags && w.seagulls[0].flip_y && w.seagulls[0].affine_y_q8 == 256);
        if (!start)
            CHECK(n == 26);
        if (start == -10 * Q)
            CHECK(n == 1);
        CHECK(portable_mesh_course_actors_view(&c, &w, 0, &view));
        CHECK(!view.visible && view.flip_y);
    }
    for (unsigned bad = 0; bad < 7; ++bad)
    {
        CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
        if (bad == 0)
            w.seagulls[0].state = 18;
        if (bad == 1)
            w.seagulls[0].timer = 5;
        if (bad == 2)
            w.seagulls[0].phase = 9;
        if (bad == 3)
            w.seagulls[0].flags = 2;
        if (bad == 4)
            w.seagulls[0].affine_x_q8 = 0;
        if (bad == 5)
            w.seagulls[0].height_q16 = 1;
        if (bad == 6)
            w.seagulls[0].art = 3;
        const PortableMeshCourseActors saved = w;
        CHECK(!portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
        CHECK(!memcmp(&saved, &w, sizeof w));
    }
    return 0;
}
static int seagull_contacts(void)
{
    PortableMeshCourseActorsConfig c = seagull_config();
    PortableMeshCourseActors w;
    char error[160];
    PortableMeshItemRules mir = rules();
    mir.lightning_scale_ratio_q16 = Q / 2;
    PortableMeshKartRules kart = test_mesh_rules();
    PortableCourse3DView course = {.id = 7};
    PortableMeshItemsConfig ic = {.course = &course,
                                  .rules = &mir,
                                  .kart_rules = &kart,
                                  .mode = 2,
                                  .count = 1,
                                  .human_mask = 1};
    PortableMeshItems items = {0};
    CHECK(portable_mesh_item_status_init(&items.status, 1, 1) == PORTABLE_MESH_ITEM_STATUS_OK);
    PortableMeshRacerState r = {.phase = PORTABLE_MESH_RACER_RACING};
    r.kart.up_q30[2] = 1073741824;
    r.kart.forward_q30[0] = 1073741824;
    memcpy(r.kart.position_q16, c.seagull_anchors_q16[0], sizeof r.kart.position_q16);
    r.kart.position_q16[2] += kart.kart_radius_q16 + kart.ride_height_q16;
    r.kart.speed_q16 = 4 * Q;
    r.kart.velocity_q16[0] = 4 * Q;
    r.kart.panel_ticks = 7;
    r.kart.drift_charge = 11;
    const PortableMeshRacerState before = r;
    const PortableMeshItems before_items = items;
    PortableMeshCourseActorEvents e = {0};
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(e.seagull_contacts == 1 && e.seagull_changed == 1 && e.seagull_touch_mask == 1 &&
          !e.hit_mask && !e.bump_mask && !e.protected_mask && !w.actor_grace[0]);
    CHECK(!memcmp(&r, &before, sizeof r) && !memcmp(&items, &before_items, sizeof items));
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    const int32_t timer = w.seagulls[0].timer;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!e.seagull_changed && e.seagull_touch_mask == 1 && w.seagulls[0].timer == timer);
    /* Source actor activation is independent of normal contact grace, while
     * its human sound cue is suppressed until the final grace turn. */
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    w.actor_grace[0] = 8;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(w.seagulls[0].state == 16 && !e.seagull_touch_mask && w.actor_grace[0] == 8);
    w.actor_grace[0] = 1;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(e.seagull_touch_mask == 1);
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    items.status.racers[0].star_ticks = 5;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(e.protected_mask == 1 && e.seagull_defeated == 1 && !e.seagull_touch_mask &&
          !e.hit_mask && !e.bump_mask);
    CHECK(!memcmp(&r, &before, sizeof r));
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    items.status.racers[0].star_ticks = 0;
    r.kart.position_q16[2] += 4 * Q;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!e.seagull_contacts);
    r = before;
    items.status.racers[0].boo_invisible_ticks = 3;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!e.seagull_contacts);
    items.status.racers[0].boo_invisible_ticks = 0;
    r.respawn_grace = 1;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!e.seagull_contacts);
    r = before;
    r.kart.position_q16[0] += 29491;
    r.kart.position_q16[2] =
        c.seagull_anchors_q16[0][2] + (kart.kart_radius_q16 + kart.ride_height_q16) / 2;
    items.status.racers[0].lightning_ticks = 1;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!e.seagull_contacts);
    return 0;
}
static PortableMeshCourseActorsConfig lava_config(unsigned variant)
{
    PortableMeshCourseActorsConfig c = config();
    c.count = 0;
    c.lava_count = 1;
    portable_lava_bubble_init(&c.initial_lava[0], 100 * Q, 100 * Q, 0, variant);
    c.initial_lava[0].placement_order = 19;
    c.lava_anchors_q16[0][0] = 3 * Q;
    c.lava_anchors_q16[0][1] = 4 * Q;
    c.lava_frame_ids[0][0] = 31;
    c.lava_frame_ids[0][1] = 32;
    return c;
}
static int lava_motion(void)
{
    static const unsigned delay[13] = {0, 100, 180, 140, 160, 200, 120, 30, 146, 122, 98, 74, 50};
    static const unsigned period[13] = {90, 100, 90,  100, 140, 100, 90,
                                        80, 180, 180, 180, 180, 180};
    char error[160];
    for (unsigned variant = 0; variant < 13; ++variant)
    {
        PortableMeshCourseActorsConfig c = lava_config(variant);
        PortableMeshCourseActors w, initial;
        CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
        initial = w;
        PortableMeshCourseActorBinding b;
        CHECK(portable_mesh_course_actors_binding(&c, 0, &b) && b.frame_count == 2 &&
              b.frame_ids[1] == 32);
        unsigned waiting = delay[variant] ? delay[variant] : 1;
        for (unsigned t = 0; t < waiting; ++t)
        {
            CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
            CHECK(!w.lava[0].flags);
        }
        CHECK(w.lava[0].phase == 2 && w.lava[0].timer == 64);
        for (int t = 63; t >= 0; --t)
        {
            CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
            CHECK(w.lava[0].height_q16 == c.sine[t * 8] * 48);
            CHECK(w.lava[0].art == (t <= 31) && w.lava[0].flags == 3);
            PortableMeshCourseActorView v;
            CHECK(portable_mesh_course_actors_view(&c, &w, 0, &v));
            CHECK(v.visible && v.model_id == (t <= 31 ? 32u : 31u) && !v.has_facing);
        }
        CHECK(w.lava[0].phase == 0 && w.lava[0].timer == (int32_t)period[variant]);
        for (unsigned t = 0; t < period[variant]; ++t)
            CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
        CHECK(w.lava[0].phase == 2 && w.lava[0].timer == 64 && !w.lava[0].flags);
        CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error) &&
              !memcmp(&w, &initial, sizeof w));
        PortableMeshCourseActors saved = w;
        w.lava[0].period = 0;
        saved = w;
        CHECK(!portable_mesh_course_actors_advance(&c, &w, error, sizeof error) &&
              !memcmp(&w, &saved, sizeof w));
    }
    PortableMeshCourseActorsConfig mixed = lava_config(0);
    mixed.count = 1;
    portable_crab_init(&mixed.initial[0], 100 * Q, 100 * Q, 0, 0);
    mixed.initial[0].placement_order = 19;
    PortableMeshCourseActors rejected;
    memset(&rejected, 0x31, sizeof rejected);
    PortableMeshCourseActors preserved = rejected;
    CHECK(!portable_mesh_course_actors_init(&mixed, &rejected, error, sizeof error));
    CHECK(!memcmp(&rejected, &preserved, sizeof rejected));
    mixed.initial[0].placement_order = 18;
    CHECK(portable_mesh_course_actors_init(&mixed, &rejected, error, sizeof error));
    CHECK(portable_mesh_course_actors_count(&rejected) == 2);
    return 0;
}
static int lava_contacts(void)
{
    PortableMeshCourseActorsConfig c = lava_config(0);
    memset(c.sine, 0, sizeof c.sine);
    PortableMeshCourseActors w;
    char error[160];
    PortableMeshItemRules mir = rules();
    mir.lightning_scale_ratio_q16 = Q / 2;
    PortableMeshKartRules kart = test_mesh_rules();
    PortableCourse3DView course = {.id = 7};
    PortableMeshItemsConfig ic = {.course = &course,
                                  .rules = &mir,
                                  .kart_rules = &kart,
                                  .mode = 2,
                                  .count = 1,
                                  .human_mask = 1};
    PortableMeshItems items = {0};
    CHECK(portable_mesh_item_status_init(&items.status, 1, 1) == PORTABLE_MESH_ITEM_STATUS_OK);
    PortableMeshRacerState r = {.phase = PORTABLE_MESH_RACER_RACING};
    r.kart.up_q30[2] = 1073741824;
    r.kart.forward_q30[0] = 1073741824;
    memcpy(r.kart.position_q16, c.lava_anchors_q16[0], sizeof r.kart.position_q16);
    r.kart.position_q16[2] += kart.kart_radius_q16 + kart.ride_height_q16;
    r.kart.speed_q16 = 4 * Q;
    r.kart.velocity_q16[0] = 4 * Q;
    r.kart.panel_ticks = 7;
    r.kart.drift_charge = 11;
    PortableMeshCourseActorEvents e = {0};
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!e.lava_contacts && !w.lava_burn_ticks[0]); /* Initial hidden wait. */
    for (unsigned t = 0; t < 2; ++t)
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(e.hit_mask == 1 && e.lava_contacts == 1 && e.lava_burn_mask == 1 && !e.lava_defeated);
    CHECK(w.lava_burn_ticks[0] == 250 && r.kart.speed_q16 == 2 * Q && !r.kart.panel_ticks &&
          !r.kart.drift_charge);
    uint64_t hash = portable_mesh_course_actors_checksum(&w);
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error) &&
          w.lava_burn_ticks[0] == 249);
    CHECK(hash != portable_mesh_course_actors_checksum(&w));
    PortableMeshCourseActors burn_only = w;
    for (unsigned t = 0; t < 239; ++t)
        CHECK(portable_mesh_course_actors_advance(&c, &burn_only, error, sizeof error));
    CHECK(burn_only.lava_burn_ticks[0] == 10);
    for (unsigned t = 0; t < 10; ++t)
        CHECK(portable_mesh_course_actors_advance(&c, &burn_only, error, sizeof error));
    CHECK(!burn_only.lava_burn_ticks[0]);
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!e.hit_mask && !e.lava_burn_mask && w.lava_burn_ticks[0] == 249);
    items.controls[0].recovering = 1;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(w.lava_burn_ticks[0] == 249); /* Held/capture marker is not a rescue. */
    r.kart.respawn_ticks = 1;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!w.lava_burn_ticks[0]);
    r.kart.respawn_ticks = 0;
    items.controls[0].recovering = 0;
    items.status.racers[0].hit_grace_ticks = 0;
    items.status.racers[0].spin_ticks = 0;
    items.status.racers[0].star_ticks = 5;
    r.kart.velocity_q16[0] = 4 * Q;
    r.kart.velocity_q16[1] = -40 * Q;
    const PortableMeshRacerState before = r;
    for (int sign = -1; sign <= 1; sign += 2)
    {
        for (unsigned t = 0; t < 1024; ++t)
            c.sine[t] = sign * 32768;
        CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
        for (unsigned t = 0; t < 2; ++t)
            CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
        r = before;
        int32_t p[3];
        CHECK(portable_mesh_course_actors_pose(&c, &w, 0, p));
        memcpy(r.kart.position_q16, p, sizeof p);
        r.kart.position_q16[2] += kart.kart_radius_q16 + kart.ride_height_q16;
        CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error,
                                                  sizeof error));
        CHECK(e.protected_mask == 1 && e.lava_defeated == 1 && !e.hit_mask && !e.lava_burn_mask);
        CHECK(w.lava[0].phase == 4 && w.lava[0].velocity_x_q16 == 34 &&
              w.lava[0].velocity_y_q16 == -342);
        unsigned steps = 0;
        while (w.lava[0].phase != 6 && steps < 33)
        {
            CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
            ++steps;
        }
        CHECK(steps == (sign < 0 ? 1u : 32u) && !w.lava[0].flags);
        CHECK(w.lava[0].x_q16 == 100 * Q + (int32_t)steps * 34);
        CHECK(w.lava[0].y_q16 == 100 * Q - (int32_t)steps * 342);
        CHECK(!w.lava_burn_ticks[0]);
    }
    /* Source integration adds component*256, so 4m/s at .5m/source-unit
     * copies trunc(8*4389/1024)=34, not a Q16 displacement of8778.
     * At the minimum authored scale a valid256m/s yields component54850;
     * the original signed halfword copies its low bits as -10686. */
    memset(c.sine, 0, sizeof c.sine);
    c.scale_q16 = 1311;
    for (int sign = -1; sign <= 1; sign += 2)
    {
        CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
        for (unsigned t = 0; t < 2; ++t)
            CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
        r = before;
        r.kart.velocity_q16[0] = sign * 256 * Q;
        r.kart.velocity_q16[1] = 0;
        CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error,
                                                  sizeof error));
        CHECK(w.lava[0].velocity_x_q16 == -sign * 10686 && !w.lava[0].velocity_y_q16);
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
        CHECK(w.lava[0].x_q16 == 100 * Q - sign * 10686);
    }
    c.scale_q16 = Q / 2;
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    for (unsigned t = 0; t < 2; ++t)
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    items.status.racers[0].star_ticks = 0;
    items.status.racers[0].boo_invisible_ticks = 3;
    r = before;
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        !e.lava_contacts);
    items.status.racers[0].boo_invisible_ticks = 0;
    r.kart.position_q16[2] += 4 * Q;
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        !e.lava_contacts);
    r = before;
    /* Between the normal and half-size fullXYZ contact spheres. Source
     * radius remains2*scale; only the modern body/suspension shrink. */
    const int32_t offset = 2 * c.scale_q16 + 3 * kart.kart_radius_q16 / 4;
    r.kart.position_q16[0] += offset;
    r.kart.position_q16[2] = (kart.kart_radius_q16 + kart.ride_height_q16) / 2;
    const int64_t small_z = kart.kart_radius_q16 / 2;
    const int64_t normal_z = (kart.kart_radius_q16 - kart.ride_height_q16) / 2;
    const int64_t small_radius = 2 * c.scale_q16 + kart.kart_radius_q16 / 2;
    const int64_t normal_radius = 2 * c.scale_q16 + kart.kart_radius_q16;
    CHECK((int64_t)offset * offset + small_z * small_z > small_radius * small_radius);
    CHECK((int64_t)offset * offset + normal_z * normal_z < normal_radius * normal_radius);
    items.status.racers[0].lightning_ticks = 1;
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        !e.lava_contacts);
    PortableMeshCourseActors normal_world = w;
    PortableMeshItems normal_items = items;
    normal_items.status.racers[0].lightning_ticks = 0;
    PortableMeshRacerState normal_racer = r;
    PortableMeshCourseActorEvents normal_events = {0};
    CHECK(portable_mesh_course_actors_resolve(&c, &normal_world, &normal_items, &ic, &normal_racer,
                                              1, &normal_events, error, sizeof error));
    CHECK(normal_events.lava_contacts == 1 && normal_events.lava_burn_mask == 1);
    PortableMeshCourseActors saved = w;
    PortableMeshItems saved_items = items;
    PortableMeshRacerState saved_racer = r;
    PortableMeshCourseActorEvents saved_events = e;
    w.lava[0].velocity_x_q16 = 40000;
    saved = w;
    CHECK(
        !portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!memcmp(&w, &saved, sizeof w) && !memcmp(&items, &saved_items, sizeof items) &&
          !memcmp(&r, &saved_racer, sizeof r) && !memcmp(&e, &saved_events, sizeof e));
    return 0;
}
static PortableMeshCourseActorsConfig thwomp_config(unsigned variant)
{
    PortableMeshCourseActorsConfig c = config();
    c.count = 0;
    c.thwomp_count = 1;
    portable_thwomp_init(&c.initial_thwomps[0], 1000 * Q, 1000 * Q, 0, variant);
    c.initial_thwomps[0].placement_order = 23;
    c.thwomp_anchors_q16[0][0] = 3 * Q;
    c.thwomp_anchors_q16[0][1] = 4 * Q;
    c.thwomp_frame_ids[0][0] = 37;
    for (unsigned i = 0; i < 5; ++i)
    {
        c.crush_rules.counts[i] = 1;
        c.crush_rules.rows[i][0] = (PortableDriverAffineRow){256, 256, 2};
    }
    c.crush_rules.rows[2][0].inverse_y_q8 = 512;
    c.crush_rules.rows[3][0].inverse_y_q8 = 512;
    return c;
}
static int thwomp_motion(void)
{
    char error[160];
    for (unsigned variant = 0; variant < 12; ++variant)
    {
        PortableMeshCourseActorsConfig c = thwomp_config(variant);
        PortableMeshCourseActors w, initial;
        CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
        initial = w;
        PortableMeshCourseActorBinding b;
        CHECK(portable_mesh_course_actors_binding(&c, 0, &b) && b.frame_count == 1 &&
              b.frame_ids[0] == 37);
        unsigned landings = 0;
        for (unsigned t = 0; t < 3500; ++t)
        {
            PortableMeshCourseActorEvents e = {0};
            CHECK(portable_mesh_course_actors_advance_events(&c, &w, NULL, 0, &e, error,
                                                             sizeof error));
            if (e.thwomp_landed)
            {
                CHECK(e.thwomp_landed == 1 && !w.thwomps[0].height_q16 &&
                      !w.thwomps[0].velocity_z_q16);
                ++landings;
            }
            CHECK(w.thwomps[0].flags == 3 && w.thwomps[0].height_q16 >= 0 &&
                  w.thwomps[0].height_q16 <= 0x208000);
        }
        CHECK(landings >= 2);
        CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error) &&
              !memcmp(&initial, &w, sizeof w));
        if (variant < 4)
        {
            for (unsigned t = 0; t < 1 + variant * 32; ++t)
                CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
            CHECK(w.thwomps[0].state == 1 && w.thwomps[0].timer == 36);
            for (unsigned t = 0; t < 36; ++t)
                CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
            CHECK(w.thwomps[0].state == 2 && w.thwomps[0].timer == 64 &&
                  w.thwomps[0].velocity_z_q16 == Q / 2);
            for (unsigned t = 0; t < 64; ++t)
                CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
            CHECK(w.thwomps[0].height_q16 == 32 * Q && w.thwomps[0].state == 3);
        }
        w = initial;
        w.thwomps[0].state = 255;
        PortableMeshCourseActors saved = w;
        CHECK(!portable_mesh_course_actors_advance(&c, &w, error, sizeof error) &&
              !memcmp(&w, &saved, sizeof w));
    }
    return 0;
}
static int thwomp_contacts(void)
{
    char error[160];
    PortableMeshCourseActorsConfig c = thwomp_config(0);
    PortableMeshCourseActors w;
    PortableMeshItemRules mir = rules();
    mir.lightning_scale_ratio_q16 = Q / 2;
    PortableMeshKartRules kart = test_mesh_rules();
    PortableCourse3DView course = {.id = 7};
    PortableMeshItemsConfig ic = {.course = &course,
                                  .rules = &mir,
                                  .kart_rules = &kart,
                                  .mode = 16,
                                  .count = 1,
                                  .human_mask = 1};
    PortableMeshItems items = {0};
    CHECK(portable_mesh_item_status_init(&items.status, 1, 1) == PORTABLE_MESH_ITEM_STATUS_OK);
    PortableMeshRacerState r = {.phase = PORTABLE_MESH_RACER_RACING};
    r.kart.up_q30[2] = 1073741824;
    r.kart.forward_q30[0] = 1073741824;
    memcpy(r.kart.position_q16, c.thwomp_anchors_q16[0], sizeof r.kart.position_q16);
    r.kart.position_q16[2] += kart.kart_radius_q16 + kart.ride_height_q16;
    r.kart.velocity_q16[0] = 4 * Q;
    r.kart.speed_q16 = 4 * Q;
    r.kart.panel_ticks = 9;
    const PortableMeshRacerState before = r;
    PortableMeshCourseActorEvents e = {0};
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(e.thwomp_contacts == 1 && e.thwomp_bump_mask == 1 && e.bump_mask == 1 && !e.hit_mask &&
          !e.thwomp_crush_mask);
    CHECK(r.kart.velocity_q16[0] < 0 && !items.status.racers[0].spin_ticks &&
          !w.crush[0].contact_timer && w.actor_grace[0] == 8);
    /* Reach an actual descending source face rather than forging a crush. */
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    for (unsigned t = 0; t < 200 && (w.thwomps[0].state != 5 || w.thwomps[0].height_q16 > 4 * Q);
         ++t)
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.thwomps[0].state == 5 && w.thwomps[0].height_q16 == 4 * Q);
    int32_t p[3];
    CHECK(portable_mesh_course_actors_pose(&c, &w, 0, p));
    r = before; /* Ordinary ground height: source size20 reaches a falling face. */
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(e.thwomp_crush_mask == 1 && e.hit_mask == 1 && !e.bump_mask &&
          !items.status.racers[0].spin_ticks);
    CHECK(w.crush[0].contact_timer == 0x7000 && !r.kart.velocity_q16[0] && !r.kart.velocity_q16[1]);
    uint64_t hash = portable_mesh_course_actors_checksum(&w);
    --w.crush[0].contact_timer;
    CHECK(hash != portable_mesh_course_actors_checksum(&w));
    items.controls[0].recovering = 1;
    PortableMeshCourseActors released = w;
    int held = 0, cue = 0;
    for (unsigned t = 0; t < 127; ++t)
        CHECK(portable_mesh_course_crush_tick(&c.crush_rules, &released.crush[0], 0, &held, &cue,
                                              error, sizeof error));
    CHECK(held && !released.crush[0].contact_timer && released.crush[0].affine.mode == 7);
    const PortableMeshCrushState recovery_affine = released.crush[0];
    CHECK(portable_mesh_course_actors_resolve(&c, &released, &items, &ic, &r, 1, &e, error,
                                              sizeof error));
    CHECK(!memcmp(&released.crush[0], &recovery_affine, sizeof recovery_affine) &&
          !e.thwomp_contacts);
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(w.crush[0].contact_timer == 0x6fff && !e.thwomp_contacts);
    r.kart.respawn_ticks = 1;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!w.crush[0].contact_timer && !e.thwomp_contacts);
    items.controls[0].recovering = 0;
    for (unsigned variant = 0; variant < 12; ++variant)
    {
        c = thwomp_config(variant);
        CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
        items.status.racers[0].star_ticks = 5;
        r = before;
        r.kart.velocity_q16[1] = -40 * Q;
        PortableMeshRacerState protected_before = r;
        CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error,
                                                  sizeof error));
        CHECK(e.thwomp_defeated == 1 && e.protected_mask == 1 && !e.hit_mask &&
              !e.thwomp_bump_mask);
        CHECK(w.thwomps[0].velocity_x_q16 == 34 * 384 &&
              w.thwomps[0].velocity_y_q16 == -342 * 384 && !w.crush[0].contact_timer &&
              !memcmp(&r, &protected_before, sizeof r));
        unsigned ticks = 0;
        while (w.thwomps[0].flags & 1)
        {
            CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
            CHECK(++ticks < 25);
        }
        CHECK(w.thwomps[0].height_q16 <= 0);
    }
    c = thwomp_config(0);
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    for (unsigned t = 0; t < 200 && w.thwomps[0].height_q16 != 0x208000; ++t)
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.thwomps[0].height_q16 == 0x208000);
    CHECK(portable_mesh_course_actors_pose(&c, &w, 0, p));
    r = before;
    memcpy(r.kart.position_q16, p, sizeof p);
    r.kart.position_q16[2] += kart.kart_radius_q16 + kart.ride_height_q16;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    unsigned flight_ticks = 0;
    while ((w.thwomps[0].flags & 1) && flight_ticks < 30)
    {
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
        ++flight_ticks;
    }
    CHECK(flight_ticks == 29 && !w.thwomps[0].flags);
    c = thwomp_config(0);
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    r = before;
    r.kart.position_q16[2] += 8 * Q;
    items.status.racers[0].star_ticks = 0;
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        !e.thwomp_contacts);
    r = before;
    items.status.racers[0].boo_invisible_ticks = 3;
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        !e.thwomp_contacts);
    items.status.racers[0].boo_invisible_ticks = 0;
    r.respawn_grace = 1;
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        !e.thwomp_contacts);
    PortableMeshCourseActors saved = w;
    PortableMeshItems saved_items = items;
    PortableMeshRacerState saved_racer = r;
    PortableMeshCourseActorEvents saved_events = e;
    w.crush[0].contact_timer = 0x6f80;
    saved = w; /* Invalid released boundary. */
    CHECK(
        !portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!memcmp(&w, &saved, sizeof w) && !memcmp(&items, &saved_items, sizeof items) &&
          !memcmp(&r, &saved_racer, sizeof r) && !memcmp(&e, &saved_events, sizeof e));
    return 0;
}
static PortableMeshCourseActorsConfig cannonball_config(unsigned variant)
{
    PortableMeshCourseActorsConfig c = config();
    c.count = 0;
    c.cannonball_count = 1;
    portable_cannonball_init(&c.initial_cannonballs[0], 100 * Q, 100 * Q, 0, (uint16_t)variant);
    c.cannonball_variants[0] = (uint8_t)variant;
    c.initial_cannonballs[0].placement_order = 29;
    c.cannonball_anchors_q16[0][0] = 3 * Q;
    c.cannonball_anchors_q16[0][1] = 4 * Q;
    for (unsigned f = 0; f < 7; ++f)
        c.cannonball_frame_ids[0][f] = 41 + f;
    return c;
}
static int cannonball_motion(void)
{
    char error[160];
    for (unsigned variant = 0; variant < 4; ++variant)
    {
        PortableMeshCourseActorsConfig c = cannonball_config(variant);
        PortableMeshCourseActors w, initial;
        CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
        initial = w;
        PortableMeshCourseActorBinding b;
        CHECK(portable_mesh_course_actors_binding(&c, 0, &b) && b.frame_count == 7 &&
              b.frame_ids[6] == 47);
        PortableMeshCourseActorEvents e = {0};
        for (unsigned t = 0; t < variant * 32; ++t)
        {
            CHECK(portable_mesh_course_actors_advance_events(&c, &w, NULL, 0, &e, error,
                                                             sizeof error));
            CHECK(w.cannonballs[0].state == 1 && !w.cannonballs[0].flags &&
                  !e.cannonball_launched && !e.cannonball_exploded);
        }
        for (unsigned n = 1; n <= 51; ++n)
        {
            CHECK(portable_mesh_course_actors_advance_events(&c, &w, NULL, 0, &e, error,
                                                             sizeof error));
            CHECK(e.cannonball_launched == (n == 1) && !e.cannonball_exploded);
            const PortableCannonball *a = &w.cannonballs[0];
            CHECK(a->velocity_z_q16 == -(int32_t)n * 2048 && a->flags == 3 && a->art == 0 &&
                  a->size == 4);
            int32_t height = 40 * Q - (int32_t)(n * (n + 1)) * 1024;
            CHECK(a->height_q16 == (height > 0 ? height : 0));
            const unsigned scale = n < 32 ? 1024 - 24 * n : 256;
            CHECK(a->affine_x_q8 == scale && a->affine_y_q8 == scale);
            PortableMeshCourseActorView v;
            CHECK(portable_mesh_course_actors_view(&c, &w, 0, &v));
            CHECK(v.visible && v.model_id == 41 && v.affine_x_q8 == scale && !v.flip_y &&
                  !v.has_facing);
        }
        CHECK(w.cannonballs[0].state == 4 &&
              w.cannonballs[0].timer == 1); /* One collidable ground turn. */
        CHECK(portable_mesh_course_actors_advance_events(&c, &w, NULL, 0, &e, error, sizeof error));
        CHECK(e.cannonball_exploded == 1 && !e.cannonball_launched);
        CHECK(w.cannonballs[0].state == 5 && w.cannonballs[0].timer == 31 &&
              w.cannonballs[0].art == 6 && w.cannonballs[0].size == 4 &&
              w.cannonballs[0].flags == 1);
        for (int timer = 30; timer >= 0; --timer)
        {
            CHECK(portable_mesh_course_actors_advance_events(&c, &w, NULL, 0, &e, error,
                                                             sizeof error));
            unsigned frame = timer >= 28   ? 1
                             : timer >= 24 ? 2
                             : timer >= 20 ? 3
                             : timer >= 14 ? 4
                             : timer >= 8  ? 5
                                           : 6;
            CHECK(w.cannonballs[0].art == frame && w.cannonballs[0].size == 20 &&
                  w.cannonballs[0].flags == 1 && !e.cannonball_exploded);
            PortableMeshCourseActorView v;
            CHECK(portable_mesh_course_actors_view(&c, &w, 0, &v));
            CHECK(v.model_id == 41 + frame && v.visible);
        }
        CHECK(portable_mesh_course_actors_advance_events(&c, &w, NULL, 0, &e, error, sizeof error));
        CHECK(w.cannonballs[0].state == 1 && w.cannonballs[0].timer == 64 &&
              !w.cannonballs[0].flags && w.cannonballs[0].size == 4);
        for (unsigned t = 0; t < 64; ++t)
        {
            CHECK(portable_mesh_course_actors_advance_events(&c, &w, NULL, 0, &e, error,
                                                             sizeof error));
            CHECK(!e.cannonball_launched && !w.cannonballs[0].flags);
        }
        CHECK(
            portable_mesh_course_actors_advance_events(&c, &w, NULL, 0, &e, error, sizeof error) &&
            e.cannonball_launched == 1);
        CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error) &&
              !memcmp(&w, &initial, sizeof w));
        for (unsigned field = 0; field < 6; ++field)
        {
            PortableMeshCourseActors bad = w;
            switch (field)
            {
            case 0:
                bad.cannonballs[0].state = 2;
                break;
            case 1:
                bad.cannonballs[0].timer = 97;
                break;
            case 2:
                bad.cannonballs[0].affine_x_q8 = 255;
                break;
            case 3:
                bad.cannonballs[0].height_q16 = 1;
                break;
            case 4:
                bad.cannonballs[0].flags = 3;
                break;
            default:
                bad.cannonballs[0].size = 20;
                break;
            }
            PortableMeshCourseActors saved = bad;
            PortableMeshCourseActorEvents before = e;
            CHECK(!portable_mesh_course_actors_advance_events(&c, &bad, NULL, 0, &e, error,
                                                              sizeof error));
            CHECK(!memcmp(&bad, &saved, sizeof bad) && !memcmp(&e, &before, sizeof e));
        }
    }
    return 0;
}
static int cannonball_contacts(void)
{
    char error[160];
    PortableMeshCourseActorsConfig c = cannonball_config(0);
    PortableMeshCourseActors w;
    PortableMeshItemRules mir = rules();
    mir.lightning_scale_ratio_q16 = Q / 2;
    mir.hit_impulse_q16 = Q;
    PortableMeshKartRules kart = test_mesh_rules();
    PortableCourse3DView course = {.id = 7};
    PortableMeshItemsConfig ic = {.course = &course,
                                  .rules = &mir,
                                  .kart_rules = &kart,
                                  .mode = 2,
                                  .count = 1,
                                  .human_mask = 1};
    PortableMeshItems items = {0};
    CHECK(portable_mesh_item_status_init(&items.status, 1, 1) == PORTABLE_MESH_ITEM_STATUS_OK);
    items.standings[0] = 0;
    PortableMeshRacerState r = {.phase = PORTABLE_MESH_RACER_RACING};
    r.kart.up_q30[2] = 1073741824;
    r.kart.forward_q30[0] = 1073741824;
    r.kart.grounded = 1;
    memcpy(r.kart.position_q16, c.cannonball_anchors_q16[0], sizeof r.kart.position_q16);
    r.kart.position_q16[0] += Q / 4;
    r.kart.position_q16[2] += kart.kart_radius_q16 + kart.ride_height_q16;
    r.kart.speed_q16 = 4 * Q;
    r.kart.velocity_q16[0] = 4 * Q;
    r.kart.velocity_q16[2] = Q / 2;
    r.kart.panel_ticks = 7;
    r.kart.drift_charge = 11;
    const PortableMeshRacerState before = r;
    PortableMeshCourseActorEvents e = {0};
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        !e.cannonball_contacts);
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        !e.cannonball_contacts); /* High overhead. */
    for (unsigned t = 1; t < 51; ++t)
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.cannonballs[0].state == 4 && (w.cannonballs[0].flags & 2));
    const PortableMeshCourseActors on_floor = w;
    const PortableCannonball actor = w.cannonballs[0];
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(e.cannonball_contacts == 1 && e.cannonball_hit_mask == 1 && e.hit_mask == 1 &&
          !e.cannonball_protected_mask);
    CHECK(!memcmp(&actor, &w.cannonballs[0], sizeof actor));
    CHECK(w.strong_hit[0].pending && w.strong_hit[0].rate == 0x1000 && w.actor_grace[0] == 8 &&
          !items.status.racers[0].spin_ticks);
    CHECK(items.status.racers[0].hit_grace_ticks == mir.hit_invulnerability_ticks &&
          !r.kart.panel_ticks && !r.kart.drift_charge);
    CHECK(r.kart.velocity_q16[0] == 3 * Q && r.kart.velocity_q16[2] == Q / 2);
    uint64_t hash = portable_mesh_course_actors_checksum(&w);
    w.strong_hit[0].direction ^= 1;
    CHECK(hash != portable_mesh_course_actors_checksum(&w));
    w = on_floor;
    r = before;
    memset(&items.status.racers[0], 0, sizeof items.status.racers[0]);
    items.status.racers[0].star_ticks = 5;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(e.cannonball_contacts == 1 && e.cannonball_protected_mask == 1 && e.protected_mask == 1 &&
          !e.hit_mask && !w.strong_hit[0].pending);
    CHECK(!memcmp(&w, &on_floor, sizeof w) && !memcmp(&r, &before, sizeof r));
    e.cannonball_launched = 1;
    e.cannonball_exploded = 1;
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(w.cannonballs[0].state == 5 && !(w.cannonballs[0].flags & 2));
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!e.cannonball_contacts && e.cannonball_launched == 1 && e.cannonball_exploded == 1);
    for (unsigned protection = 0; protection < 4; ++protection)
    {
        w = on_floor;
        r = before;
        memset(&items.status.racers[0], 0, sizeof items.status.racers[0]);
        if (protection == 0)
            items.status.racers[0].boo_invisible_ticks = 3;
        if (protection == 1)
            items.status.racers[0].hit_grace_ticks = 1;
        if (protection == 2)
            r.respawn_grace = 1;
        if (protection == 3)
            items.controls[0].recovering = 1;
        CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error,
                                                  sizeof error));
        CHECK(!e.cannonball_contacts && !w.strong_hit[0].pending);
        items.controls[0].recovering = 0;
    }
    w = on_floor;
    r = before;
    memset(&items.status.racers[0], 0, sizeof items.status.racers[0]);
    r.kart.position_q16[0] =
        c.cannonball_anchors_q16[0][0] + c.scale_q16 + 3 * kart.kart_radius_q16 / 4;
    r.kart.position_q16[2] = (kart.kart_radius_q16 + kart.ride_height_q16) / 2;
    items.status.racers[0].lightning_ticks = 1;
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        !e.cannonball_contacts);
    /* Maximum legal MIR impulse must remain admissible near the SI limit.
     * Equal X/Y radial deltas must scale together; independently clamping X
     * would leave a much larger Y response. Vertical velocity is unchanged. */
    for (int sign = -1; sign <= 1; sign += 2)
    {
        PortableMeshItemRules maximum_rules = mir;
        maximum_rules.hit_impulse_q16 = 256 * Q;
        maximum_rules.hit_speed_ratio_q16 = Q;
        PortableMeshItems maximum_items = {0};
        CHECK(portable_mesh_item_status_init(&maximum_items.status, 1, 1) ==
              PORTABLE_MESH_ITEM_STATUS_OK);
        maximum_items.standings[0] = 0;
        maximum_items.status.racers[0].mushroom_ticks = 1;
        PortableMeshItemsConfig maximum_config = ic;
        maximum_config.rules = &maximum_rules;
        PortableMeshRacerState maximum_racer = before;
        maximum_racer.kart.position_q16[0] = c.cannonball_anchors_q16[0][0] + sign * Q / 4;
        maximum_racer.kart.position_q16[1] += sign * Q / 4;
        maximum_racer.kart.velocity_q16[0] = sign * 255 * Q;
        maximum_racer.kart.velocity_q16[1] = sign * 10 * Q;
        maximum_racer.kart.speed_q16 = sign * 255 * Q;
        const PortableMeshRacerState maximum_before = maximum_racer;
        PortableMeshCourseActors maximum_world = on_floor;
        CHECK(portable_mesh_course_actors_resolve(&c, &maximum_world, &maximum_items,
                                                  &maximum_config, &maximum_racer, 1, &e, error,
                                                  sizeof error));
        CHECK(e.cannonball_hit_mask == 1 && maximum_world.actor_grace[0] == 8 &&
              maximum_world.strong_hit[0].pending);
        CHECK(!maximum_items.status.racers[0].spin_ticks &&
              !maximum_items.status.racers[0].mushroom_ticks &&
              maximum_items.status.racers[0].hit_grace_ticks ==
                  maximum_rules.hit_invulnerability_ticks);
        CHECK(sign * maximum_racer.kart.velocity_q16[0] <= 256 * Q &&
              sign * maximum_racer.kart.velocity_q16[0] >= 256 * Q - 1);
        CHECK(maximum_racer.kart.velocity_q16[0] - maximum_before.kart.velocity_q16[0] ==
              maximum_racer.kart.velocity_q16[1] - maximum_before.kart.velocity_q16[1]);
        CHECK(maximum_racer.kart.velocity_q16[2] == maximum_before.kart.velocity_q16[2] &&
              maximum_racer.kart.speed_q16 == maximum_racer.kart.velocity_q16[0]);
        CHECK(portable_mesh_kart_state_valid(&maximum_racer.kart));
        CHECK(!memcmp(&maximum_world.cannonballs[0], &actor, sizeof actor));
    }
    /* Actual standing order, not racer index, selects CPU long/short source
     * spin. The human is finished and cannot contribute a second contact. */
    for (unsigned human_ahead = 0; human_ahead < 2; ++human_ahead)
    {
        PortableMeshItems cpu_items = {0};
        CHECK(portable_mesh_item_status_init(&cpu_items.status, 2, 1) ==
              PORTABLE_MESH_ITEM_STATUS_OK);
        cpu_items.standings[0] = (uint8_t)(human_ahead ? 0 : 1);
        cpu_items.standings[1] = (uint8_t)(human_ahead ? 1 : 0);
        PortableMeshItemsConfig cpu_config = ic;
        cpu_config.mode = 8;
        cpu_config.count = 2;
        PortableMeshRacerState cpu_racers[2] = {{.phase = PORTABLE_MESH_RACER_FINISHED}, before};
        w = on_floor;
        CHECK(portable_mesh_course_actors_resolve(&c, &w, &cpu_items, &cpu_config, cpu_racers, 2,
                                                  &e, error, sizeof error));
        CHECK(e.cannonball_hit_mask == 2 &&
              w.strong_hit[1].rate == (human_ahead ? 0x1000 : 0x2000) && !w.strong_hit[0].rate);
    }
    w = on_floor;
    /* Invalid actual rank order is a late error after genuine contact geometry;
     * neither strong state nor damage/events may leak out. */
    r = before;
    items.status.racers[0].lightning_ticks = 0;
    items.standings[0] = 1;
    PortableMeshCourseActors saved = w;
    PortableMeshItems saved_items = items;
    PortableMeshRacerState saved_racer = r;
    PortableMeshCourseActorEvents saved_events = e;
    CHECK(
        !portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!memcmp(&w, &saved, sizeof w) && !memcmp(&items, &saved_items, sizeof items) &&
          !memcmp(&r, &saved_racer, sizeof r) && !memcmp(&e, &saved_events, sizeof e));
    return 0;
}
static PortableMeshCourseActorsConfig boo_config(unsigned variant)
{
    PortableMeshCourseActorsConfig c = config();
    c.count = 0;
    c.boo_count = 1;
    portable_boo_init(&c.initial_boos[0], 100 * Q, 100 * Q, 0, (uint16_t)variant);
    c.initial_boos[0].placement_order = 31;
    c.boo_anchors_q16[0][0] = 3 * Q;
    c.boo_anchors_q16[0][1] = 4 * Q;
    c.boo_frame_ids[0][0] = 61;
    c.boo_frame_ids[0][1] = 62;
    return c;
}
static int boo_motion(void)
{
    char error[160];
    for (unsigned variant = 0; variant < 2; ++variant)
    {
        PortableMeshCourseActorsConfig c = boo_config(variant);
        for (unsigned n = 0; n < 1024; ++n)
            c.sine[n] = (int32_t)n - 512;
        PortableMeshCourseActors w, initial;
        CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
        initial = w;
        PortableMeshCourseActorBinding b;
        CHECK(portable_mesh_course_actors_binding(&c, 0, &b) && b.frame_count == 2 &&
              b.frame_ids[1] == 62);
        for (unsigned tick = 0; tick < 128; ++tick)
        {
            CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
            CHECK(w.boos[0].state == 17 && w.boos[0].phase == (tick + 1) * 1024 &&
                  w.boos[0].flags == 3);
            CHECK(w.boos[0].height_q16 == 8 * Q + ((int32_t)(tick % 64) * 16 - 512) * 3);
            PortableMeshCourseActorView v;
            CHECK(portable_mesh_course_actors_view(&c, &w, 0, &v));
            CHECK(v.visible && v.model_id == 61 && v.alternate_model_id == 62 &&
                  v.view_heading == 0x4000 && v.has_view_art && !v.has_facing && !v.flip_y);
            CHECK(v.position_q16[0] == 3 * Q && v.position_q16[1] == 4 * Q);
        }
        w.boos[0].phase = 0xfffffc00u;
        w.boos[0].height_q16 = 8 * Q + c.sine[992] * 3;
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
        CHECK(!w.boos[0].phase && w.boos[0].height_q16 == 8 * Q + c.sine[1008] * 3);
        CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
        CHECK(w.boos[0].phase == 1024 && w.boos[0].height_q16 == 8 * Q + c.sine[0] * 3);
        uint64_t hash = portable_mesh_course_actors_checksum(&w);
        w.boo_pending_mask = 1;
        CHECK(hash != portable_mesh_course_actors_checksum(&w));
        CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error) &&
              !memcmp(&w, &initial, sizeof w));
        for (unsigned field = 0; field < 5; ++field)
        {
            PortableMeshCourseActors bad = w;
            switch (field)
            {
            case 0:
                bad.boos[0].state = 240;
                break;
            case 1:
                bad.boos[0].phase = 1;
                break;
            case 2:
                bad.boos[0].height_q16 = 1;
                break;
            case 3:
                bad.boos[0].heading = 0xc000;
                break;
            default:
                bad.boos[0].flags = 1;
                break;
            }
            PortableMeshCourseActors saved = bad;
            CHECK(!portable_mesh_course_actors_advance(&c, &bad, error, sizeof error) &&
                  !memcmp(&bad, &saved, sizeof bad));
        }
    }
    return 0;
}
static int boo_contacts(void)
{
    char error[160];
    PortableMeshCourseActorsConfig c = boo_config(0);
    PortableMeshCourseActors w;
    PortableMeshItemRules mir = rules();
    PortableMeshKartRules kart = test_mesh_rules();
    PortableCourse3DView course = {.id = 7};
    PortableMeshItemsConfig ic = {.course = &course,
                                  .rules = &mir,
                                  .kart_rules = &kart,
                                  .mode = 16,
                                  .count = 1,
                                  .human_mask = 1};
    PortableMeshItems items = {0};
    CHECK(portable_mesh_item_status_init(&items.status, 1, 1) == PORTABLE_MESH_ITEM_STATUS_OK);
    PortableMeshRacerState r = {.phase = PORTABLE_MESH_RACER_RACING};
    r.kart.up_q30[2] = 1073741824;
    r.kart.forward_q30[0] = 1073741824;
    memcpy(r.kart.position_q16, c.boo_anchors_q16[0], sizeof r.kart.position_q16);
    r.kart.position_q16[2] += kart.kart_radius_q16 + kart.ride_height_q16;
    r.kart.velocity_q16[0] = 4 * Q;
    r.kart.speed_q16 = 4 * Q;
    r.kart.panel_ticks = 7;
    const PortableMeshRacerState before = r;
    const PortableMeshItems before_items = items;
    PortableMeshCourseActorEvents e = {0};
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    const PortableBoo actor = w.boos[0];
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(e.boo_contacts == 1 && e.boo_touch_mask == 1 && w.boo_flash_mask == 1 &&
          !w.boo_pending_mask);
    CHECK(!e.hit_mask && !e.protected_mask && !e.bump_mask && !w.actor_grace[0]);
    CHECK(!memcmp(&r, &before, sizeof r) && !memcmp(&items, &before_items, sizeof items) &&
          !memcmp(&actor, &w.boos[0], sizeof actor));
    r.kart.position_q16[0] += 20 * Q;
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error) && !w.boo_flash_mask);
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!e.boo_contacts && !w.boo_pending_mask && !w.boo_flash_mask);
    /* Star retains the request after leaving the overlap, then consumes it
     * for exactly one accepted turn. It never defeats or flips the Boo. */
    r = before;
    items.status.racers[0].star_ticks = 3;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(e.boo_contacts == 1 && !e.boo_touch_mask && !e.protected_mask &&
          w.boo_pending_mask == 1 && !w.boo_flash_mask);
    CHECK(w.boos[0].state == 17 && w.boos[0].flags == 3 && w.boos[0].affine_y_q8 == 256);
    r.kart.position_q16[0] += 20 * Q;
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        w.boo_pending_mask == 1 && !w.boo_flash_mask);
    items.status.racers[0].star_ticks = 0;
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        !w.boo_pending_mask && w.boo_flash_mask == 1 && !e.boo_contacts);
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error) && !w.boo_flash_mask);
    /* Actor +38 analogue filters >1; ordinary hit grace and a normal hop
     * do not filter the independent palette callback. */
    r = before;
    r.kart.air_ticks = 5;
    items.status.racers[0].hit_grace_ticks = 3;
    w.actor_grace[0] = 2;
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        !e.boo_contacts);
    w.actor_grace[0] = 1;
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        e.boo_contacts == 1 && e.boo_touch_mask == 1 && w.actor_grace[0] == 1);
    items.status.racers[0].boo_invisible_ticks = 1;
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        e.boo_contacts == 1 && !e.boo_touch_mask);
    items.status.racers[0].boo_invisible_ticks = 0;
    r.kart.respawn_ticks = 1;
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        !e.boo_contacts && !w.boo_pending_mask && !w.boo_flash_mask);
    r = before;
    w.actor_grace[0] = 0;
    int32_t p[3];
    CHECK(portable_mesh_course_actors_pose(&c, &w, 0, p));
    r.kart.position_q16[0] = p[0] + 9 * c.scale_q16 + kart.kart_radius_q16 + 1;
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        !e.boo_contacts);
    r = before;
    r.kart.position_q16[2] =
        p[2] + 16 * c.scale_q16 + kart.kart_radius_q16 + kart.ride_height_q16 + 1;
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        !e.boo_contacts);
    /* Tall source Z shape reaches beyond a9-source-unit spherical radius. */
    r.kart.position_q16[2] = p[2] + 15 * c.scale_q16 + kart.ride_height_q16;
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        e.boo_contacts == 1);
    PortableMeshCourseActors saved = w;
    PortableMeshItems saved_items = items;
    PortableMeshRacerState saved_racer = r;
    PortableMeshCourseActorEvents saved_events = e;
    w.boos[0].phase |= 1;
    saved = w;
    CHECK(
        !portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(!memcmp(&w, &saved, sizeof w) && !memcmp(&items, &saved_items, sizeof items) &&
          !memcmp(&r, &saved_racer, sizeof r) && !memcmp(&e, &saved_events, sizeof e));
    /* Burn is independent and higher priority; its final turn releases a
     * request even when the kart has already left the Boo. */
    c.lava_count = 1;
    portable_lava_bubble_init(&c.initial_lava[0], 100 * Q, 100 * Q, 0, 1);
    c.initial_lava[0].placement_order = 32;
    c.lava_anchors_q16[0][0] = 100 * Q;
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    w.lava_burn_ticks[0] = 2;
    r = before;
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        w.boo_pending_mask == 1 && !w.boo_flash_mask);
    r.kart.position_q16[0] += 20 * Q;
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        w.lava_burn_ticks[0] == 1 && w.boo_pending_mask == 1 && !w.boo_flash_mask);
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        !w.lava_burn_ticks[0] && !w.boo_pending_mask && w.boo_flash_mask == 1);
    /* Ordinary held/recovery controls refuse contact. Own active strong spin
     * is source hop mode10: it suppresses380, but not the palette callback. */
    r = before;
    items.controls[0].recovering = 1;
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        !e.boo_contacts);
    c.cannonball_count = 1;
    c.cannonball_variants[0] = 1;
    portable_cannonball_init(&c.initial_cannonballs[0], 100 * Q, 100 * Q, 0, 1);
    c.initial_cannonballs[0].placement_order = 33;
    c.cannonball_anchors_q16[0][0] = 100 * Q;
    CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
    CHECK(portable_mesh_course_strong_hit_admit(&w.strong_hit[0], 0x1000, 0));
    int held = 0;
    CHECK(portable_mesh_course_strong_hit_tick(&w.strong_hit[0], 1, 0, &held) && held);
    CHECK(
        portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        e.boo_contacts == 1 && !e.boo_touch_mask && w.boo_flash_mask == 1 && !w.boo_pending_mask &&
        !e.hit_mask && !e.protected_mask);
    return 0;
}
static PortableMeshCourseActorsConfig rock_config(unsigned variant)
{
    PortableMeshCourseActorsConfig c = config();
    c.count = 0;
    c.rock_count = 1;
    c.scale_q16 = 6554;
    portable_course_prop_init(&c.initial_rocks[0], 100 * Q, 100 * Q, 0xa20000u | variant, 224, 0);
    c.initial_rocks[0].placement_order = 40;
    c.rock_object_indices[0] = 12;
    c.rock_anchors_q16[0][0] = 3 * Q;
    c.rock_anchors_q16[0][1] = 4 * Q;
    for (unsigned j = 0; j < 5; ++j)
        c.rock_frame_ids[0][j] = 70 + j;
    return c;
}
static int rock_motion(void)
{
    char error[160];
    for (unsigned variant = 0; variant < 4; ++variant)
    {
        PortableMeshCourseActorsConfig c = rock_config(variant);
        PortableMeshCourseActors w, initial;
        CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error));
        initial = w;
        PortableMeshCourseActorBinding binding;
        CHECK(portable_mesh_course_actors_binding(&c, 0, &binding) && binding.frame_count == 5 &&
              binding.frame_ids[4] == 74);
        const unsigned delay = 112 + 16 * variant;
        for (unsigned tick = 0; tick < delay; ++tick)
        {
            PortableMeshCourseActorEvents e = {0};
            CHECK(portable_mesh_course_actors_advance_events(&c, &w, NULL, 0, &e, error,
                                                             sizeof error));
            CHECK(!w.rocks[0].flags && !e.rock_cue_masks[0]);
        }
        CHECK(w.rocks[0].state == 2 && !w.rocks[0].lake_rock.timer);
        for (unsigned n = 1; n <= 45; ++n)
        {
            PortableMeshCourseActorEvents e = {0};
            CHECK(portable_mesh_course_actors_advance_events(&c, &w, NULL, 0, &e, error,
                                                             sizeof error));
            CHECK(e.rock_cue_masks[0] == (n == 1 ? 1u : 0u));
            CHECK(w.rocks[0].lake_rock.velocity_z_q16 == -(int32_t)(3072 * n));
            CHECK(w.rocks[0].height_q16 == (n == 45 ? 0 : 48 * Q - (int32_t)(1536 * n * (n + 1))));
            PortableMeshCourseActorView v;
            CHECK(portable_mesh_course_actors_view(&c, &w, 0, &v) && v.visible &&
                  v.model_id == 70 && v.affine_x_q8 == (n < 35 ? 1024 - 24 * n : 192) &&
                  v.affine_y_q8 == v.affine_x_q8);
        }
        CHECK(w.rocks[0].state == 4);
        for (unsigned n = 0; n < 10; ++n)
        {
            PortableMeshCourseActorEvents e = {0};
            CHECK(portable_mesh_course_actors_advance_events(&c, &w, NULL, 0, &e, error,
                                                             sizeof error));
            CHECK(e.rock_cue_masks[1] == (n == 0 ? 1u : 0u));
            const unsigned frames[10] = {1, 1, 2, 2, 2, 3, 3, 3, 4, 4};
            CHECK(w.rocks[0].lake_rock.frame == frames[n]);
        }
        CHECK(w.rocks[0].state == 6);
        for (unsigned n = 1; n <= 23; ++n)
        {
            PortableMeshCourseActorEvents e = {0};
            CHECK(portable_mesh_course_actors_advance_events(&c, &w, NULL, 0, &e, error,
                                                             sizeof error));
            CHECK(e.rock_cue_masks[2] == (n == 1 ? 1u : 0u));
            CHECK(w.rocks[0].x_q16 == 100 * Q + (int32_t)n * 16384 &&
                  w.rocks[0].y_q16 == w.rocks[0].x_q16);
            CHECK(w.rocks[0].height_q16 ==
                  (n == 23 ? 0 : (int32_t)n * Q - (int32_t)(3072 * n * (n - 1))));
        }
        CHECK(w.rocks[0].state == 8 && w.rocks[0].lake_rock.velocity_z_q16 == -69632);
        for (unsigned n = 1; n <= 13; ++n)
        {
            PortableMeshCourseActorEvents e = {0};
            CHECK(portable_mesh_course_actors_advance_events(&c, &w, NULL, 0, &e, error,
                                                             sizeof error));
            CHECK(e.rock_cue_masks[3] == (n == 1 ? 1u : 0u));
            CHECK(w.rocks[0].height_q16 ==
                  (n == 13 ? 0 : (int32_t)n * 34816 - (int32_t)(3072 * n * (n - 1))));
        }
        CHECK(w.rocks[0].state == 10 && w.rocks[0].x_q16 == 109 * Q &&
              w.rocks[0].lake_rock.velocity_z_q16 == -38912);
        int32_t p[3];
        CHECK(portable_mesh_course_actors_pose(&c, &w, 0, p) && p[0] == 3 * Q + 9 * c.scale_q16 &&
              p[1] == 4 * Q + 9 * c.scale_q16);
        for (unsigned n = 0; n < 128; ++n)
            CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
        CHECK(w.rocks[0].state == 0);
        for (unsigned n = 0; n < delay + 45; ++n)
            CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
        CHECK(w.rocks[0].state == 4 && w.rocks[0].x_q16 == 100 * Q);
        /* A complete second cycle validates retained source fields on reset. */
        for (unsigned n = 0; n < 10 + 23 + 13 + 128; ++n)
            CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error));
        CHECK(w.rocks[0].state == 0);
        CHECK(portable_mesh_course_actors_init(&c, &w, error, sizeof error) &&
              !memcmp(&w, &initial, sizeof w));
        for (unsigned field = 0; field < 7; ++field)
        {
            PortableMeshCourseActors bad = w;
            switch (field)
            {
            case 0:
                bad.rocks[0].state = 12;
                break;
            case 1:
                bad.rocks[0].lake_rock.velocity_z_q16 = INT32_MIN;
                break;
            case 2:
                bad.rocks[0].lake_rock.timer = -1;
                break;
            case 3:
                bad.rocks[0].lake_rock.frame = 5;
                break;
            case 4:
                bad.rocks[0].affine_y_q8 = 0;
                break;
            case 5:
                bad.rocks[0].x_q16++;
                break;
            default:
                bad.rocks[0].height_q16 = 1;
                break;
            }
            PortableMeshCourseActors saved = bad;
            PortableMeshCourseActorEvents e;
            memset(&e, 0x5a, sizeof e);
            PortableMeshCourseActorEvents before = e;
            CHECK(!portable_mesh_course_actors_advance_events(&c, &bad, NULL, 0, &e, error,
                                                              sizeof error) &&
                  !memcmp(&bad, &saved, sizeof bad) && !memcmp(&e, &before, sizeof e));
        }
    }
    return 0;
}
static int rock_contacts(void)
{
    char error[160];
    PortableMeshCourseActorsConfig c = rock_config(0);
    PortableMeshCourseActors floor;
    CHECK(portable_mesh_course_actors_init(&c, &floor, error, sizeof error));
    for (unsigned j = 0; j < 112 + 45; ++j)
        CHECK(portable_mesh_course_actors_advance(&c, &floor, error, sizeof error));
    PortableMeshItemRules mir = rules();
    PortableMeshKartRules kart = test_mesh_rules();
    PortableCourse3DView course = {.id = 7};
    PortableMeshItemsConfig ic = {.course = &course,
                                  .rules = &mir,
                                  .kart_rules = &kart,
                                  .mode = 16,
                                  .count = 1,
                                  .human_mask = 1};
    PortableMeshItems items = {0};
    CHECK(portable_mesh_item_status_init(&items.status, 1, 1) == PORTABLE_MESH_ITEM_STATUS_OK);
    PortableMeshRacerState r = {.phase = PORTABLE_MESH_RACER_RACING};
    r.kart.up_q30[2] = 1073741824;
    r.kart.forward_q30[0] = 1073741824;
    memcpy(r.kart.position_q16, c.rock_anchors_q16[0], sizeof r.kart.position_q16);
    r.kart.position_q16[2] = kart.kart_radius_q16 + kart.ride_height_q16;
    r.kart.velocity_q16[0] = r.kart.speed_q16 = 4 * Q;
    r.kart.panel_ticks = 10;
    const PortableMeshRacerState initial_racer = r;
    PortableMeshCourseActors w = floor;
    PortableMeshCourseActorEvents e = {0};
    e.rock_cue_masks[1] = 1;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(e.rock_contacts == 1 && e.rock_hit_mask == 1 && e.hit_mask == 1 &&
          !e.rock_protected_mask && !e.bump_mask && e.lava_burn_mask == 1 &&
          e.rock_cue_masks[1] == 1 && w.lava_burn_ticks[0] == 250 && w.actor_grace[0] == 8);
    CHECK(items.status.racers[0].spin_ticks == mir.hit_spin_ticks && !r.kart.panel_ticks &&
          r.kart.speed_q16 == portable_fixed_3d_mul(4 * Q, mir.hit_speed_ratio_q16));
    CHECK(!memcmp(&w.rocks[0], &floor.rocks[0], sizeof w.rocks[0]));
    CHECK(portable_mesh_course_actors_advance(&c, &w, error, sizeof error) &&
          w.lava_burn_ticks[0] == 249);
    uint64_t hash = portable_mesh_course_actors_checksum(&w);
    --w.lava_burn_ticks[0];
    CHECK(hash != portable_mesh_course_actors_checksum(&w));
    /* Protection cues without removing/changing the source rock. */
    w = floor;
    r = initial_racer;
    CHECK(portable_mesh_item_status_init(&items.status, 1, 1) == PORTABLE_MESH_ITEM_STATUS_OK);
    CHECK(mir.star_player_ticks > 0 && mir.star_player_ticks <= UINT16_MAX);
    items.status.racers[0].star_ticks = (uint16_t)mir.star_player_ticks;
    CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error));
    CHECK(e.rock_contacts == 1 && e.rock_protected_mask == 1 && e.protected_mask == 1 &&
          !e.hit_mask && !w.lava_burn_ticks[0] && !memcmp(&r, &initial_racer, sizeof r) &&
          !memcmp(&w.rocks[0], &floor.rocks[0], sizeof w.rocks[0]));
    for (unsigned guard = 0; guard < 5; ++guard)
    {
        w = floor;
        r = initial_racer;
        CHECK(portable_mesh_item_status_init(&items.status, 1, 1) == PORTABLE_MESH_ITEM_STATUS_OK);
        if (guard == 0)
            items.status.racers[0].boo_invisible_ticks = 1;
        if (guard == 1)
            items.status.racers[0].hit_grace_ticks = 1;
        if (guard == 2)
            r.respawn_grace = 1;
        if (guard == 3)
            r.kart.position_q16[2] += 10 * Q;
        if (guard == 4)
        {
            items.status.racers[0].lightning_ticks = 1;
            const int32_t small =
                portable_fixed_3d_mul(kart.kart_radius_q16, mir.lightning_scale_ratio_q16);
            r.kart.position_q16[0] += c.scale_q16 + (small + kart.kart_radius_q16) / 2;
            r.kart.position_q16[2] =
                portable_fixed_3d_mul(kart.ride_height_q16, mir.lightning_scale_ratio_q16);
        }
        CHECK(portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error,
                                                  sizeof error) &&
              !e.rock_contacts && !e.hit_mask && !w.lava_burn_ticks[0]);
    }
    w = floor;
    r = initial_racer;
    CHECK(portable_mesh_item_status_init(&items.status, 1, 1) == PORTABLE_MESH_ITEM_STATUS_OK);
    w.rocks[0].lake_rock.frame = 4;
    PortableMeshCourseActors saved = w;
    PortableMeshItems saved_items = items;
    PortableMeshRacerState saved_r = r;
    PortableMeshCourseActorEvents saved_e = e;
    CHECK(
        !portable_mesh_course_actors_resolve(&c, &w, &items, &ic, &r, 1, &e, error, sizeof error) &&
        !memcmp(&w, &saved, sizeof w) && !memcmp(&items, &saved_items, sizeof items) &&
        !memcmp(&r, &saved_r, sizeof r) && !memcmp(&e, &saved_e, sizeof e));
    return 0;
}
typedef struct
{
    PortableMeshTriangle triangles[6];
    uint32_t fail_row, type;
    int duplicate_rows;
} GroundGeometry;
static int ground_triangle(const void *context, uint32_t i, PortableMeshTriangle *out)
{
    const GroundGeometry *g = context;
    if (i == g->fail_row || (!g->duplicate_rows && i >= 6))
        return 0;
    *out = g->triangles[g->duplicate_rows ? 0 : i];
    return 1;
}
static int ground_surface(const void *context, uint32_t i, PortableMeshSurface *out)
{
    if (i)
        return 0;
    *out = (PortableMeshSurface){.type = ((const GroundGeometry *)context)->type};
    return 1;
}
static PortableMeshCourse ground_geometry(GroundGeometry *g)
{
    memset(g, 0, sizeof *g);
    g->fail_row = UINT32_MAX;
    const int32_t points[3][4][3] = {
        {{0,-4*Q,0},{2*Q,-4*Q,0},{2*Q,4*Q,0},{0,4*Q,0}},
        {{2*Q,-4*Q,0},{8*Q,-4*Q,3*Q/2},{8*Q,4*Q,3*Q/2},{2*Q,4*Q,0}},
        {{0,-4*Q,3*Q},{8*Q,-4*Q,3*Q},{8*Q,4*Q,3*Q},{0,4*Q,3*Q}}};
    const unsigned corners[2][3] = {{0,1,2},{0,2,3}};
    for (unsigned row = 0; row < 6; ++row)
        for (unsigned v = 0; v < 3; ++v)
            memcpy(g->triangles[row].vertices_q16[v], points[row/2][corners[row%2][v]], 12);
    return (PortableMeshCourse){.context = g, .triangle_count = 6,
        .triangle = ground_triangle, .surface = ground_surface};
}
static int ground_motion(void)
{
    char error[192];
    GroundGeometry g;
    PortableMeshCourse geometry = ground_geometry(&g);
    static PortableMeshCourseActorsConfig c, only;
    static PortableMeshCourseActors a, before, copy;
    c = config();
    c.ground_follow_version = 1;
    c.initial[0].placement_order = 0;
    c.anchors_q16[0][0] = 2*Q;
    c.anchors_q16[0][1] = 0;
    c.anchors_q16[0][2] = 2*Q/5; /* Authored .4m art clearance is retained. */
    c.penguin_count = 1;
    CHECK(portable_penguin_init(&c.initial_penguins[0],100*Q,100*Q,0,0));
    c.initial_penguins[0].placement_order = 1;
    c.penguin_anchors_q16[0][0] = 3*Q;
    c.penguin_anchors_q16[0][2] = Q/4+2*Q/5;
    c.mouse_count = 2;
    CHECK(portable_mouse_init(&c.initial_mice[0],100*Q,100*Q,0,0));
    CHECK(portable_mouse_init(&c.initial_mice[1],100*Q,100*Q,0,0x8001));
    c.initial_mice[0].placement_order = 2;
    c.initial_mice[1].placement_order = 3;
    memcpy(c.mouse_anchors_q16[0],c.anchors_q16[0],12);
    memcpy(c.mouse_anchors_q16[1],c.penguin_anchors_q16[0],12);
    c.mechakoopa_count = 1;
    CHECK(portable_course_prop_init(&c.initial_mechakoopas[0],100*Q,100*Q,0xa10000,224,0));
    c.initial_mechakoopas[0].placement_order = 4;
    memcpy(c.mechakoopa_anchors_q16[0],c.anchors_q16[0],12);
    memset(&a,0x5b,sizeof a);
    before = a;
    CHECK(!portable_mesh_course_actors_init(&c,&a,error,sizeof error));
    CHECK(!memcmp(&a,&before,sizeof a));
    CHECK(portable_mesh_course_actors_init_on_course(&c,&geometry,&a,error,sizeof error));
    before = a;
    CHECK(portable_mesh_course_actors_init_on_course(&c,&geometry,&copy,error,sizeof error));
    CHECK(!memcmp(&a,&copy,sizeof a)); /* Retry/reset is deterministic. */
    for (unsigned i = 0; i < 4; ++i)
    {
        CHECK(portable_mesh_course_actors_advance_events_on_course(&c,&geometry,&a,NULL,0,NULL,error,sizeof error));
        CHECK(portable_mesh_course_actors_advance_events_on_course(&c,&geometry,&copy,NULL,0,NULL,error,sizeof error));
    }
    CHECK(!memcmp(&a,&copy,sizeof a)); /* Presentation/pause cannot advance it. */
    CHECK(a.crab_ground_offset_q16[0] >= Q/4-2 && a.crab_ground_offset_q16[0] <= Q/4+2);
    CHECK(a.mouse_ground_offset_q16[0] == a.crab_ground_offset_q16[0]);
    CHECK(a.mechakoopa_ground_offset_q16[0] == a.crab_ground_offset_q16[0]);
    CHECK(!a.penguin_ground_offset_q16[0] && !a.mouse_ground_offset_q16[1]);
    int32_t position[3];
    CHECK(portable_mesh_course_actors_pose(&c,&a,0,position));
    CHECK(position[2] == c.anchors_q16[0][2]+a.crab_ground_offset_q16[0]);
    PortableMeshCourseActorView view;
    CHECK(portable_mesh_course_actors_view(&c,&a,0,&view));
    CHECK(!memcmp(view.position_q16,position,sizeof position));
    copy = a;
    CHECK(portable_mesh_course_actors_pose(&c,&a,0,position));
    CHECK(!memcmp(&a,&copy,sizeof a));
    const uint64_t hash = portable_mesh_course_actors_checksum(&a);
    int32_t *offsets[4] = {a.crab_ground_offset_q16,a.penguin_ground_offset_q16,
                          a.mouse_ground_offset_q16,a.mechakoopa_ground_offset_q16};
    for (unsigned i = 0; i < 4; ++i)
    {
        ++offsets[i][0];
        CHECK(portable_mesh_course_actors_checksum(&a) != hash);
        --offsets[i][0];
    }
    a.crab_ground_offset_q16[63] = 1;
    CHECK(!portable_mesh_course_actors_checksum(&a));
    a = copy;
    PortableMeshCourseActorEvents events, saved_events;
    memset(&events,0x39,sizeof events);
    saved_events = events;
    g.fail_row = 5; /* Late malformed callback after several usable floors. */
    CHECK(!portable_mesh_course_actors_advance_events_on_course(&c,&geometry,&a,NULL,0,&events,error,sizeof error));
    CHECK(strstr(error,"crab[0]") && !memcmp(&a,&copy,sizeof a));
    CHECK(!memcmp(&events,&saved_events,sizeof events));
    g.fail_row = UINT32_MAX;
    CHECK(!portable_mesh_course_actors_advance_events(&c,&a,NULL,0,&events,error,sizeof error));
    CHECK(!memcmp(&a,&copy,sizeof a) && !memcmp(&events,&saved_events,sizeof events));
    /* The same deck geometry also supports ordinary source hazard floors;
     * this feature supplies geometry, not invented NPC death rules. */
    g.type = 6;
    CHECK(portable_mesh_course_actors_init_on_course(&c,&geometry,&a,error,sizeof error));
    g.type = 0;
    /* No downward deck switch: the upper floor is selected at initialization. */
    only = c;
    for (unsigned i = 0; i < 3; ++i)
        only.anchors_q16[0][i] = c.anchors_q16[0][i];
    only.anchors_q16[0][2] += 3*Q;
    only.count = 1;
    only.penguin_count = only.mouse_count = only.mechakoopa_count = 0;
    CHECK(portable_mesh_course_actors_init_on_course(&only,&geometry,&a,error,sizeof error));
    for (unsigned i = 0; i < 4; ++i)
        CHECK(portable_mesh_course_actors_advance_events_on_course(&only,&geometry,&a,NULL,0,NULL,error,sizeof error));
    CHECK(!a.crab_ground_offset_q16[0]);
    /* Descending patrol uses the same initial deck, not an increasing-height cache. */
    only = c;
    only.count = only.penguin_count = only.mouse_count = 0;
    CHECK(portable_course_prop_init(&only.initial_mechakoopas[0],100*Q,100*Q,0xa10001,224,0));
    only.initial_mechakoopas[0].placement_order = 4;
    only.mechakoopa_anchors_q16[0][0] = 6*Q;
    only.mechakoopa_anchors_q16[0][2] = Q+2*Q/5;
    CHECK(portable_mesh_course_actors_init_on_course(&only,&geometry,&a,error,sizeof error));
    for (unsigned i = 0; i < 4; ++i)
        CHECK(portable_mesh_course_actors_advance_events_on_course(&only,&geometry,&a,NULL,0,NULL,error,sizeof error));
    CHECK(a.mechakoopa_ground_offset_q16[0] >= -Q/4-2 && a.mechakoopa_ground_offset_q16[0] <= -Q/4+2);
    /* All protected source states freeze their accepted floor delta even if
     * terrain callbacks subsequently fail; source flight remains authoritative. */
    a = copy;
    portable_crab_contact(&a.crabs[0],0x2000,0,1);
    CHECK(portable_penguin_contact(&a.penguins[0],0x2000,0,1));
    for (unsigned i = 0; i < 2; ++i)
        portable_mouse_contact(&a.mice[i],0x2000,0,1);
    portable_course_prop_damage(&a.mechakoopas[0]);
    const int32_t frozen = a.crab_ground_offset_q16[0];
    g.fail_row = 0;
    CHECK(portable_mesh_course_actors_advance_events_on_course(&c,&geometry,&a,NULL,0,NULL,error,sizeof error));
    CHECK(a.crab_ground_offset_q16[0] == frozen && a.mouse_ground_offset_q16[0] == frozen &&
          a.mechakoopa_ground_offset_q16[0] == frozen);
    CHECK(a.crabs[0].height_q16 > 0 && a.penguins[0].height_q16 > 0);
    g.fail_row = UINT32_MAX;
    /* An ordinary hopper freezes its floor base throughout the source hop,
     * then reacquires the same fixed XY floor when its own controller lands. */
    only = c;
    only.count = only.penguin_count = only.mechakoopa_count = 0;
    only.mouse_count = 1;
    only.initial_mice[0] = c.initial_mice[1];
    memcpy(only.mouse_anchors_q16[0],c.mouse_anchors_q16[1],12);
    CHECK(portable_mesh_course_actors_init_on_course(&only,&geometry,&a,error,sizeof error));
    for (unsigned i = 0; i < 60; ++i)
        CHECK(portable_mesh_course_actors_advance_events_on_course(&only,&geometry,&a,NULL,0,NULL,error,sizeof error));
    CHECK(a.mice[0].hop_phase == 1 && !a.mice[0].height_q16);
    g.fail_row = 0;
    CHECK(portable_mesh_course_actors_advance_events_on_course(&only,&geometry,&a,NULL,0,NULL,error,sizeof error));
    CHECK(a.mice[0].height_q16 > 0 && !a.mouse_ground_offset_q16[0]);
    g.fail_row = UINT32_MAX;
    for (unsigned i = 0; i < 20 && a.mice[0].hop_phase; ++i)
        CHECK(portable_mesh_course_actors_advance_events_on_course(&only,&geometry,&a,NULL,0,NULL,error,sizeof error));
    CHECK(!a.mice[0].hop_phase && !a.mice[0].height_q16 && !a.mouse_ground_offset_q16[0]);
    /* Two-LSB up encoding reserve also works on a slope: the radius1
     * sphere needs more than one LSB of vertical clearance there. */
    only = c;
    only.penguin_count = only.mouse_count = only.mechakoopa_count = 0;
    only.anchors_q16[0][0] = 3*Q;
    int32_t normal[3];
    CHECK(portable_mesh_query_normal(&g.triangles[2],normal));
    int64_t numerator = -(int64_t)Q*normal[0];
    const int32_t floor_height = (int32_t)(numerator/normal[2]+(numerator%normal[2]>0));
    only.anchors_q16[0][2] = floor_height-2;
    CHECK(portable_mesh_course_actors_init_on_course(&only,&geometry,&a,error,sizeof error));
    only.anchors_q16[0][2] = floor_height-3;
    before = a;
    CHECK(!portable_mesh_course_actors_init_on_course(&only,&geometry,&a,error,sizeof error));
    CHECK(!memcmp(&a,&before,sizeof a));
    only.anchors_q16[0][0] = 2*Q;
    only.anchors_q16[0][2] = Q;
    CHECK(portable_mesh_course_actors_init_on_course(&only,&geometry,&a,error,sizeof error));
    before = a;
    ++only.anchors_q16[0][2];
    CHECK(!portable_mesh_course_actors_init_on_course(&only,&geometry,&a,error,sizeof error));
    CHECK(!memcmp(&a,&before,sizeof a));
    /* Initial clearance is bounded; enabled init failure preserves destination. */
    only = c;
    only.anchors_q16[0][2] = 2*Q;
    before = a;
    CHECK(!portable_mesh_course_actors_init_on_course(&only,&geometry,&a,error,sizeof error));
    CHECK(!memcmp(&a,&before,sizeof a));
    g.duplicate_rows = 1;
    geometry.triangle_count = 513;
    CHECK(!portable_mesh_course_actors_init_on_course(&c,&geometry,&a,error,sizeof error));
    CHECK(strstr(error,"status=3") && !memcmp(&a,&before,sizeof a));
    return 0;
}
int main(void)
{
    CHECK(!motion());
    CHECK(!ground_motion());
    CHECK(!contact());
    CHECK(!decode());
    CHECK(!snowman_motion());
    CHECK(!snowman_contacts());
    CHECK(!penguin_motion());
    CHECK(!penguin_contacts());
    CHECK(!mouse_motion());
    CHECK(!mouse_contact());
    CHECK(!mouse_hop_contacts_all_phases());
    CHECK(!mouse_invalid());
    CHECK(!piranha_motion());
    CHECK(!piranha_contacts());
    CHECK(!seagull_motion());
    CHECK(!seagull_contacts());
    CHECK(!lava_motion());
    CHECK(!lava_contacts());
    CHECK(!thwomp_motion());
    CHECK(!thwomp_contacts());
    CHECK(!cannonball_motion());
    CHECK(!cannonball_contacts());
    CHECK(!boo_motion());
    CHECK(!boo_contacts());
    CHECK(!rock_motion());
    CHECK(!rock_contacts());
    return 0;
}
