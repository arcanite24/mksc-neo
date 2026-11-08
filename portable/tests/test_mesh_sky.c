#include "mesh_sky.h"
#include "mesh_course_actors.h"
#include "mesh_course_fixture.h"
#include "mesh_item_fixture.h"
#include "mesh_item_rules_asset.h"
#include "mesh_race.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#define CHECK(v)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(v))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "mesh sky:%d: %s\n", __LINE__, #v);                                    \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static void put(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(value >> (8 * i));
}
static void fixture(uint8_t bytes[PORTABLE_MESH_SKY_PROFILE_SIZE], unsigned controller)
{
    memset(bytes, 0, PORTABLE_MESH_SKY_PROFILE_SIZE);
    const uint32_t header[8] = {
        UINT32_C(0x314b534d), 1, PORTABLE_MESH_SKY_PROFILE_SIZE, 41, 7, 7, 1, 1024};
    for (unsigned i = 0; i < 8; ++i)
        put(bytes + 4 * i, header[i]);
    const int32_t rules[23] = {0,  7,  7,  8, 7, 0, 0, 0, 0, -96, 0, 38,
                               32, 38, -1, 0, 0, 0, 2, 0, 0, 1,   2};
    for (unsigned i = 0; i < 23; ++i)
        put(bytes + 32 + 4 * i, (uint32_t)rules[i]);
    put(bytes + 32, controller);
    if (controller == 3)
        put(bytes + 32 + 11 * 4, 352);
    for (unsigned i = 0; i < 3; ++i)
    {
        put(bytes + 124 + 8 * i, 256u + i * 256u);
        put(bytes + 128 + 8 * i, (1024u + i * 8u) * 65536u);
    }
    put(bytes + 148 + 256 * 4, 32768);
    put(bytes + 148 + 768 * 4, (uint32_t)-32768);
}
static int malformed_profiles(void)
{
    uint8_t original[PORTABLE_MESH_SKY_PROFILE_SIZE], bytes[PORTABLE_MESH_SKY_PROFILE_SIZE];
    fixture(original, 6);
    PortableMeshSkyConfig config;
    char error[192];
    CHECK(portable_mesh_sky_decode(original, sizeof original, 41, &config, error, sizeof error));
    CHECK(config.rules.x_bias[1] == -96 && config.rules.split_y == -1 &&
          config.sine[768] == -32768);
    CHECK(config.logical_width[2] == 768 && config.radius_q16[2] == 1040 * 65536);
    const PortableMeshSkyConfig before = config;
    const uint32_t mutations[][2] = {
        {0, 0},   {4, 2},   {8, 4240},    {12, 42},
        {16, 0},  {20, 8},  {24, 2},      {28, 1023},
        {32, 8},  {36, 16}, {64, 32768},  {88, 160},
        {124, 0}, {128, 0}, {148, 32769}, {148 + 1023 * 4, UINT32_C(0xffff7fff)}};
    for (unsigned i = 0; i < sizeof mutations / sizeof mutations[0]; ++i)
    {
        memcpy(bytes, original, sizeof bytes);
        put(bytes + mutations[i][0], mutations[i][1]);
        CHECK(!portable_mesh_sky_decode(bytes, sizeof bytes, 41, &config, error, sizeof error));
        CHECK(strstr(error, "profile=41 MSK1") && memcmp(&config, &before, sizeof config) == 0);
    }
    CHECK(
        !portable_mesh_sky_decode(original, sizeof original - 1, 41, &config, error, sizeof error));
    CHECK(!portable_mesh_sky_decode(NULL, sizeof original, 41, &config, error, sizeof error));
    CHECK(!portable_mesh_sky_decode(original, sizeof original, 41, NULL, error, sizeof error));
    CHECK(memcmp(&config, &before, sizeof config) == 0);
    return 0;
}
static int clock_and_views(void)
{
    const unsigned controllers[] = {0, 1, 3, 6, 7};
    for (unsigned choice = 0; choice < 5; ++choice)
    {
        uint8_t bytes[PORTABLE_MESH_SKY_PROFILE_SIZE];
        fixture(bytes, controllers[choice]);
        PortableMeshSkyConfig config;
        PortableMeshSky world, ghost;
        char error[192];
        CHECK(portable_mesh_sky_decode(bytes, sizeof bytes, 41, &config, error, sizeof error));
        CHECK(portable_mesh_sky_init(&config, &world));
        ghost = world;
        const uint64_t initial = portable_mesh_sky_checksum(&config, &world);
        CHECK(initial);
        for (unsigned tick = 0; tick < 64; ++tick)
        {
            CHECK(portable_mesh_sky_advance(&config, &world, &world));
            CHECK(portable_mesh_sky_advance(&config, &ghost, &ghost));
            CHECK(portable_mesh_sky_checksum(&config, &world) ==
                  portable_mesh_sky_checksum(&config, &ghost));
        }
        CHECK(world.clock == 64 && initial != portable_mesh_sky_checksum(&config, &world));
        const PortableMeshSky before = world;
        CHECK(portable_mesh_sky_config_valid(&config));
        CHECK(portable_mesh_sky_state_valid(&config, &world) ==
              portable_mesh_sky_valid(&config, &world));
        PortableSkyMotionSnapshot zero, right, left;
        CHECK(portable_mesh_sky_snapshot(&config, &world, 0, &zero));
        CHECK(portable_mesh_sky_snapshot(&config, &world, 16384, &right));
        CHECK(portable_mesh_sky_snapshot(&config, &world, 49152, &left));
        CHECK(memcmp(&world, &before, sizeof world) == 0);
        CHECK(right.scroll_x_q16[0] - zero.scroll_x_q16[0] == 128 * 65536);
        CHECK(left.scroll_x_q16[0] - zero.scroll_x_q16[0] == -128 * 65536);
        CHECK(right.scroll_y_q16[0] == zero.scroll_y_q16[0] &&
              left.scroll_y_q16[0] == zero.scroll_y_q16[0]);
        if (controllers[choice] != 3)
        {
            const uint16_t headings[] = {0, 1, 32767, 32768, 65535};
            for (unsigned heading = 0; heading < 5; ++heading)
            {
                PortableSkyMotion source = world.motion;
                const PortableSkyMotionInputs in = {.clock = world.clock,
                                                    .yaw = headings[heading],
                                                    .scene_flags_7fe = 256,
                                                    .precise_vertical_offset = 1};
                CHECK(portable_sky_motion_resolve(&source, &config.rules, &in, config.sine, 1024,
                                                  0) == 1);
                PortableSkyMotionSnapshot resolved;
                CHECK(portable_mesh_sky_snapshot(&config, &world, headings[heading], &resolved));
                for (unsigned layer = 0; layer < 3; ++layer)
                {
                    CHECK(resolved.scroll_x[layer] == source.snapshot.scroll_x[layer]);
                    CHECK(resolved.scroll_x_q16[layer] == source.snapshot.scroll_x_q16[layer]);
                    CHECK(resolved.scroll_y_q16[layer] == source.snapshot.scroll_y_q16[layer]);
                }
                CHECK(resolved.split_scroll_x_q16 == source.snapshot.split_scroll_x_q16);
            }
        }
        if (controllers[choice] == 1)
            CHECK(zero.scroll_x[0] == 0); /* cos(quarter turn). */
        if (controllers[choice] == 6)
            CHECK(zero.scroll_x_q16[1] == (-96 + 4) * 65536);
        if (controllers[choice] == 7)
            CHECK(zero.scroll_x_q16[1] == (-96 + 16) * 65536);
        const uint32_t logical_width = config.logical_width[0];
        config.logical_width[0] = 0;
        right = zero;
        CHECK(!portable_mesh_sky_valid(&config, &world));
        CHECK(!portable_mesh_sky_advance(&config, &world, &world));
        CHECK(!portable_mesh_sky_snapshot(&config, &world, 0, &right));
        CHECK(!memcmp(&world, &before, sizeof world) && !memcmp(&right, &zero, sizeof right));
        config.logical_width[0] = logical_width;
        CHECK(portable_mesh_sky_valid(&config, &world));
        PortableMeshSky invalid = world;
        invalid.revision++;
        CHECK(!portable_mesh_sky_state_valid(&config, &invalid));
        CHECK(!portable_mesh_sky_advance(&config, &world, &invalid));
        CHECK(memcmp(&world, &before, sizeof world) == 0);
        right = zero;
        CHECK(!portable_mesh_sky_snapshot(&config, &invalid, 0, &right));
        CHECK(memcmp(&right, &zero, sizeof right) == 0);
    }
    return 0;
}
static int rising_epoch(void)
{
    uint8_t bytes[PORTABLE_MESH_SKY_PROFILE_SIZE];
    fixture(bytes, 3);
    PortableMeshSkyConfig config;
    PortableMeshSky world;
    char error[192];
    CHECK(portable_mesh_sky_decode(bytes, sizeof bytes, 41, &config, error, sizeof error));
    CHECK(portable_mesh_sky_init(&config, &world));
    for (unsigned i = 0; i < 300; ++i)
        CHECK(portable_mesh_sky_advance(&config, &world, &world));
    CHECK(world.motion.phase == 0 && world.motion.elapsed == 300);
    CHECK(portable_mesh_sky_advance(&config, &world, &world));
    CHECK(world.motion.phase == 1 && world.motion.elapsed == 0);
    CHECK(portable_mesh_sky_advance(&config, &world, &world));
    CHECK(world.motion.elapsed == 1 && world.motion.snapshot.scroll_y_q16[0] == 352 * 65536);
    PortableSkyMotionSnapshot view;
    CHECK(portable_mesh_sky_snapshot(&config, &world, 16384, &view));
    CHECK(view.scroll_y_q16[0] == 352 * 65536 && world.motion.elapsed == 1);
    CHECK(portable_mesh_sky_advance(&config, &world, &world));
    CHECK(world.motion.snapshot.scroll_y_q16[0] < 352 * 65536);
    for (unsigned i = 0; i < 7300; ++i)
        CHECK(portable_mesh_sky_advance(&config, &world, &world));
    CHECK(world.motion.elapsed == 7201 && world.motion.snapshot.scroll_y_q16[0] == 32 * 65536);
    config.entry_delay = 0;
    CHECK(portable_mesh_sky_init(&config, &world));
    CHECK(portable_mesh_sky_advance(&config, &world, &world));
    CHECK(world.motion.phase == 1);
    return 0;
}
static int course_bindings(void)
{
    uint8_t meta[128] = {0}, objects[192] = {0}, props[8 * 32] = {0}, strings[128] = {0};
    uint8_t bytes[PORTABLE_MESH_SKY_PROFILE_SIZE];
    fixture(bytes, 6);
    const char *keys[] = {"mksc:sky-motion", "mksc:sky-layer", "mksc:controller", "mksc:billboard"};
    uint32_t offsets[4], used = 1;
    for (unsigned i = 0; i < 4; ++i)
    {
        offsets[i] = used;
        size_t n = strlen(keys[i]) + 1;
        memcpy(strings + used, keys[i], n);
        used += (uint32_t)n;
    }
    put(props, offsets[0]);
    put(props + 4, PORTABLE_COURSE_3D_PROPERTY_ASSET);
    put(props + 8, 106);
    put(props + 12, 41);
    put(meta + 100, 1);
    for (unsigned i = 0; i < 3; ++i)
    {
        put(props + 32 * (i + 1), offsets[1]);
        put(props + 32 * (i + 1) + 8, 2 - i);
        put(objects + 64 * i + 4, 96);
        put(objects + 64 * i + 8, 90 + i);
        put(objects + 64 * i + 56, i + 1);
        put(objects + 64 * i + 60, 1);
    }
    put(props + 128, offsets[2]);
    put(props + 132, PORTABLE_COURSE_3D_PROPERTY_STRING);
    put(props + 136, 0);
    put(props + 160, offsets[3]);
    put(props + 164, PORTABLE_COURSE_3D_PROPERTY_BOOLEAN);
    put(props + 168, 1);
    /* An optional property after layer2 permits independent controller/billboard failures. */
    memcpy(props + 192, props + 32, 32);
    put(props + 200, 0);
    uint8_t mesh[88] = {0}, material[16] = {0};
    memcpy(mesh, "MSH1", 4);
    put(mesh + 4, 3);
    put(mesh + 8, 1);
    put(mesh + 12, 1);
    put(mesh + 16, 100);
    put(mesh + 40, 65536);
    put(mesh + 64, 65536);
    mesh[82] = 1;
    mesh[84] = 2;
    memcpy(material, "MTL1", 4);
    put(material + 4, UINT32_C(0xff112233));
    put(material + 8, 1);
    uint8_t msa[4128] = {0};
    memcpy(msa, "MSA1", 4);
    put(msa + 4, 1);
    put(msa + 8, sizeof msa);
    put(msa + 12, 42);
    put(msa + 16, 1);
    put(msa + 20, 1024);
    PortableAssetSectionView sections[6] = {
        {.kind = 106, .content_id = 41, .version = 1, .bytes = bytes, .size = sizeof bytes},
        {.kind = 96, .content_id = 90, .version = 1, .bytes = mesh, .size = sizeof mesh},
        {.kind = 96, .content_id = 91, .version = 1, .bytes = mesh, .size = sizeof mesh},
        {.kind = 96, .content_id = 92, .version = 1, .bytes = mesh, .size = sizeof mesh},
        {.kind = 97, .content_id = 100, .version = 1, .bytes = material, .size = sizeof material},
        {.kind = 104, .content_id = 42, .version = 1, .bytes = msa, .size = sizeof msa}};
    PortableAssetPackView pack = {.resolved_sections = sections, .section_count = 6};
    PortableCourse3DView course = {.id = 700, .pack = &pack, .table_count = 4};
    course.tables[0] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('M', 'E', 'T', 'A'), 1, 128, meta};
    course.tables[1] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('O', 'B', 'J', 'S'), 3, 64, objects};
    course.tables[2] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('P', 'R', 'O', 'P'), 7, 32, props};
    course.tables[3] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('S', 'T', 'R', 'S'), used, 1, strings};
    PortableMeshSkyConfig config;
    PortableMeshSkyBindings bindings;
    char error[256];
    CHECK(portable_mesh_sky_config_open(&course, &config, &bindings, error, sizeof error));
    CHECK(config.enabled && config.profile_id == 41 && bindings.object_indices[0] == 2 &&
          bindings.object_indices[1] == 1 && bindings.object_indices[2] == 0);
    PortableMeshCourseActorsConfig owner;
    CHECK(portable_mesh_course_actors_config_open(&course, &owner, error, sizeof error));
    CHECK(owner.sky.enabled && owner.profile_id == 41 && owner.revision == 7 &&
          !portable_mesh_course_actors_config_count(&owner) &&
          !portable_mesh_course_actors_needs_items(&owner));
    /* A sky must not legitimize an unrelated MSA1 with no actual controllers. */
    const unsigned actor_key = used;
    memcpy(strings + used, "mksc:actor-rules", 17);
    used += 17;
    course.tables[3].count = used;
    uint8_t old_property[32];
    memcpy(old_property, props + 128, 32);
    put(props + 128, actor_key);
    put(props + 132, PORTABLE_COURSE_3D_PROPERTY_ASSET);
    put(props + 136, 104);
    put(props + 140, 42);
    put(meta + 100, 5);
    const PortableMeshCourseActorsConfig old_owner = owner;
    CHECK(!portable_mesh_course_actors_config_open(&course, &owner, error, sizeof error) &&
          strstr(error, "rules declared without controllers") &&
          !memcmp(&owner, &old_owner, sizeof owner));
    memcpy(props + 128, old_property, 32);
    put(meta + 100, 1);
    PortableMeshSkyConfig saved = config;
    PortableMeshSkyBindings old = bindings;
    for (unsigned change = 0; change < 23; ++change)
    {
        uint8_t old_meta[128], old_objects[192], old_props[sizeof props];
        memcpy(old_meta, meta, sizeof meta);
        memcpy(old_objects, objects, sizeof objects);
        memcpy(old_props, props, sizeof props);
        if (change == 0)
            put(meta + 100, 0); /* Layer tags without a profile. */
        if (change == 1)
            put(props + 8, 105);
        if (change == 2)
            put(props + 12, 42);
        if (change == 3)
            sections[0].version = 2;
        if (change == 4)
            put(bytes + 32, 4);
        if (change == 5)
            course.tables[1].count = 2;
        if (change == 6)
            put(props + 40, 1); /* Duplicate logical1, missing2. */
        if (change == 7)
            put(props + 36, PORTABLE_COURSE_3D_PROPERTY_BOOLEAN);
        if (change == 8)
            put(props + 40, 3);
        if (change == 9)
            put(objects + 4, 97);
        if (change == 10)
            put(objects + 8, 99);
        if (change == 11)
            sections[1].version = 2;
        if (change == 12)
        {
            put(objects + 128 + 56, 3);
            put(objects + 128 + 60, 2);
        }
        if (change == 13)
        {
            put(objects + 128 + 56, 5);
            put(objects + 128 + 60, 2);
        }
        if (change == 14)
            sections[0].size = 4243;
        if (change == 15)
            sections[0].size = SIZE_MAX;
        if (change == 16)
            sections[1].bytes = NULL;
        if (change == 17)
            mesh[0] = 0;
        if (change == 18)
            put(mesh + 16, 101); /* Referenced material absent. */
        if (change == 19)
            sections[4].bytes = NULL;
        if (change == 20)
            mesh[82] = 3; /* Triangle index beyond three vertices. */
        if (change == 22)
            put(bytes + 32, 2); /* A progress profile requires course provenance. */
        if (change == 21)
        {
            sections[1].content_id = 0;
            put(objects + 8, 0);
        }
        CHECK(!portable_mesh_sky_config_open(&course, &config, &bindings, error, sizeof error));
        CHECK(strstr(error, "course=700 sky profile=") && !memcmp(&config, &saved, sizeof saved) &&
              !memcmp(&bindings, &old, sizeof old));
        if (change == 4)
            CHECK(strstr(error, "sunset sky requires palette-motion"));
        if (change == 22)
            CHECK(strstr(error, "missing source progress provenance"));
        memcpy(meta, old_meta, sizeof meta);
        memcpy(objects, old_objects, sizeof objects);
        memcpy(props, old_props, sizeof props);
        sections[0].version = 1;
        sections[1].version = 1;
        course.tables[1].count = 3;
        sections[0].size = sizeof bytes;
        put(bytes + 32, 6);
        sections[1].bytes = mesh;
        sections[1].content_id = 90;
        sections[4].bytes = material;
        mesh[0] = 'M';
        put(mesh + 16, 100);
        mesh[82] = 1;
    }
    static const char palette_key[] = "mksc:palette-motion";
    memcpy(strings + used, palette_key, sizeof palette_key);
    put(props + 224, used);
    used += (unsigned)sizeof palette_key;
    put(props + 228, PORTABLE_COURSE_3D_PROPERTY_BOOLEAN);
    put(props + 232, 1);
    course.tables[2].count = 8;
    course.tables[3].count = used;
    put(meta + 100, 8);
    CHECK(!portable_mesh_sky_config_open(&course, &config, &bindings, error, sizeof error));
    CHECK(strstr(error, "orphan palette-motion") && !memcmp(&config, &saved, sizeof saved) &&
          !memcmp(&bindings, &old, sizeof old));
    put(bytes + 32, 4);
    CHECK(!portable_mesh_sky_config_open(&course, &config, &bindings, error, sizeof error));
    CHECK(strstr(error, "expected MPA1 asset") && !memcmp(&config, &saved, sizeof saved) &&
          !memcmp(&bindings, &old, sizeof old));
    put(bytes + 32, 6);
    put(meta + 100, 1);
    /* False billboard property is harmless. No profile and no placements is neutral. */
    put(props + 168, 0);
    put(objects + 128 + 56, 5);
    put(objects + 128 + 60, 2);
    CHECK(portable_mesh_sky_config_open(&course, &config, &bindings, error, sizeof error));
    put(meta + 100, 0);
    course.tables[1].count = 0;
    CHECK(portable_mesh_sky_config_open(&course, &config, &bindings, error, sizeof error));
    CHECK(!config.enabled && bindings.object_indices[0] == UINT32_MAX &&
          bindings.object_indices[1] == UINT32_MAX && bindings.object_indices[2] == UINT32_MAX);
    return 0;
}
/* Authored KPH2 words keep the shared-race declaration path fully admitted. */
static void kart_payload(uint8_t bytes[PORTABLE_MESH_KART_RULES_SIZE], const PortableMeshKartRules *r)
{
    PortableMeshKartProfile p = test_mesh_profile();
    p.profile_id = r->profile_id;
    p.revision = r->revision;
    test_mesh_profile_payload(&p, bytes);
}
static int accepted_lifecycle(unsigned controller)
{
    PortableTestMeshCourse floor;
    test_mesh_course_init(&floor);
    uint8_t property[32] = {0};
    static const uint8_t strings[] = "mksc:sky-motion";
    put(property + 4, PORTABLE_COURSE_3D_PROPERTY_ASSET);
    put(property + 8, 106);
    put(property + 12, 41);
    put(floor.meta + 100, 1);
    floor.course.tables[floor.course.table_count++] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('P', 'R', 'O', 'P'), 1, 32, property};
    floor.course.tables[floor.course.table_count++] = (PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('S', 'T', 'R', 'S'), sizeof strings, 1, strings};
    uint8_t bytes[PORTABLE_MESH_SKY_PROFILE_SIZE];
    fixture(bytes, controller);
    char error[256];
    PortableMeshCourseActorsConfig config = {
        .course_id = floor.course.id, .profile_id = 41, .revision = 7};
    CHECK(portable_mesh_sky_decode(bytes, sizeof bytes, 41, &config.sky, error, sizeof error));
    if (controller == 2 || controller == 5)
        config.sky.progress =
            (PortableMeshSkyProgressConfig){.enabled = 1, .stage_count = 2, .source_raw = {0, 90}};
    PortableAssetSectionView sky_section = {
        .kind = 106, .content_id = 41, .version = 1, .bytes = bytes, .size = sizeof bytes};
    PortableAssetPackView pack = {.resolved_sections = &sky_section, .section_count = 1};
    floor.course.pack = &pack;
    PortableMeshCourseActors actors;
    CHECK(portable_mesh_course_actors_init(&config, &actors, error, sizeof error));
    CHECK(!portable_mesh_course_actors_config_count(&config) &&
          portable_mesh_course_actors_required(&config) &&
          !portable_mesh_course_actors_needs_items(&config));
    PortableMeshTimeTrial trial;
    PortableMeshKartEvents events;
    PortableMeshItemEvents item_events;
    PortableMeshCourseActorEvents actor_events;
    CHECK(portable_mesh_time_trial_init(&trial, &floor.course, &floor.rules, 0, error,
                                        sizeof error) == PORTABLE_MESH_OK);
    PortableMeshTimeTrial saved = trial;
    memset(&events, 0x5a, sizeof events);
    PortableMeshKartEvents old_events = events;
    CHECK(portable_mesh_time_trial_tick(&trial, (PortableMeshKartInput){0}, &events, error,
                                        sizeof error) == PORTABLE_MESH_UNSUPPORTED);
    CHECK(!memcmp(&trial, &saved, sizeof trial) && !memcmp(&events, &old_events, sizeof events));
    CHECK(portable_mesh_time_trial_tick_items(&trial, NULL, NULL, (PortableMeshKartInput){0},
                                              (PortableMeshItemControls){0}, &events, &item_events,
                                              error, sizeof error) == PORTABLE_MESH_UNSUPPORTED);
    for (unsigned tick = 0; tick < 181; ++tick)
    {
        CHECK(portable_mesh_time_trial_tick_actors(
                  &trial, NULL, NULL, &actors, &config, (PortableMeshKartInput){0},
                  (PortableMeshItemControls){0}, &events, &item_events, &actor_events, error,
                  sizeof error) == PORTABLE_MESH_OK);
        CHECK(actors.sky.clock == tick + 1 && !actors.tick && !item_events.count);
    }
    CHECK(trial.state.phase == PORTABLE_MESH_TT_RACING);
    /* Invalid input and late gate admission both roll back every owner/output. */
    for (unsigned failure = 0; failure < 2; ++failure)
    {
        if (failure)
            put(floor.edges, UINT32_MAX);
        saved = trial;
        PortableMeshCourseActors before = actors;
        memset(&events, 0x5a, sizeof events);
        old_events = events;
        memset(&item_events, 0x6b, sizeof item_events);
        PortableMeshItemEvents old_item = item_events;
        memset(&actor_events, 0x7c, sizeof actor_events);
        PortableMeshCourseActorEvents old_actor = actor_events;
        CHECK(portable_mesh_time_trial_tick_actors(
                  &trial, NULL, NULL, &actors, &config,
                  (PortableMeshKartInput){.buttons = failure ? PORTABLE_MESH_ACCELERATE : 16},
                  (PortableMeshItemControls){0}, &events, &item_events, &actor_events, error,
                  sizeof error) != PORTABLE_MESH_OK);
        CHECK(!memcmp(&trial, &saved, sizeof trial) && !memcmp(&actors, &before, sizeof actors) &&
              !memcmp(&events, &old_events, sizeof events) &&
              !memcmp(&item_events, &old_item, sizeof item_events) &&
              !memcmp(&actor_events, &old_actor, sizeof actor_events));
        put(floor.edges, 1);
    }
    /* Start/Retry init restores clock0. With MIR, countdown and racing still advance once. */
    CHECK(portable_mesh_course_actors_init(&config, &actors, error, sizeof error) &&
          !actors.sky.clock);
    CHECK(portable_mesh_time_trial_init(&trial, &floor.course, &floor.rules, 0, error,
                                        sizeof error) == PORTABLE_MESH_OK);
    uint8_t mir_bytes[PORTABLE_MESH_ITEM_RULES_BYTES];
    test_mir_payload(mir_bytes);
    PortableAssetSectionView section = {
        .kind = 103, .content_id = 41, .version = 1, .bytes = mir_bytes, .size = sizeof mir_bytes};
    PortableMeshItemRules rules;
    CHECK(portable_mesh_item_rules_asset_open(&section, &rules));
    PortableMeshItemsConfig items_config = {.course = &floor.course,
                                            .rules = &rules,
                                            .kart_rules = &floor.rules,
                                            .mode = 2,
                                            .count = 1,
                                            .human_mask = 1};
    PortableMeshItems items;
    CHECK(portable_mesh_items_init(&items, &items_config, 123, error, sizeof error) ==
          PORTABLE_MESH_ITEMS_OK);
    for (unsigned tick = 0; tick < 181; ++tick)
    {
        CHECK(portable_mesh_time_trial_tick_actors(
                  &trial, &items, &items_config, &actors, &config, (PortableMeshKartInput){0},
                  (PortableMeshItemControls){0}, &events, &item_events, &actor_events, error,
                  sizeof error) == PORTABLE_MESH_OK);
        CHECK(actors.sky.clock == tick + 1 && actors.tick == (tick >= 180 ? 1u : 0u));
    }
    return 0;
}
static int shared_lifecycle(unsigned controller)
{
    PortableTestMeshCourse floor;
    test_mesh_course_init(&floor);
    put(floor.meta + 12, 8);
    uint8_t grids[64];
    memcpy(grids, floor.grid, 32);
    memcpy(grids + 32, floor.grid, 32);
    put(grids + 32, 4 * 65536);
    floor.course.tables[1].bytes = grids;
    floor.course.tables[1].count = 2;
    uint8_t property[32] = {0};
    static const uint8_t strings[] = "mksc:sky-motion";
    put(property + 4, PORTABLE_COURSE_3D_PROPERTY_ASSET);
    put(property + 8, 106);
    put(property + 12, 41);
    put(floor.meta + 100, 1);
    floor.course.tables[floor.course.table_count++] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('P', 'R', 'O', 'P'), 1, 32, property};
    floor.course.tables[floor.course.table_count++] = (PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('S', 'T', 'R', 'S'), sizeof strings, 1, strings};
    PortableMeshCourseActorsConfig config = {
        .course_id = floor.course.id, .profile_id = 41, .revision = 7};
    uint8_t bytes[PORTABLE_MESH_SKY_PROFILE_SIZE];
    fixture(bytes, controller);
    char error[256];
    CHECK(portable_mesh_sky_decode(bytes, sizeof bytes, 41, &config.sky, error, sizeof error));
    if (controller == 2 || controller == 5)
        config.sky.progress =
            (PortableMeshSkyProgressConfig){.enabled = 1, .stage_count = 2, .source_raw = {0, 90}};
    uint8_t kph[PORTABLE_MESH_KART_RULES_SIZE];
    kart_payload(kph, &floor.rules);
    PortableAssetSectionView sections[2] = {
        {.kind = 106, .content_id = 41, .version = 1, .bytes = bytes, .size = sizeof bytes},
        {.kind = 102,
         .content_id = floor.rules.profile_id,
         .version = PORTABLE_MESH_KART_RULES_VERSION,
         .bytes = kph,
         .size = sizeof kph}};
    PortableAssetPackView pack = {.resolved_sections = sections, .section_count = 2};
    floor.course.pack = &pack;
    PortableMeshRaceSession race;
    CHECK(portable_mesh_race_init(&race, &floor.course, &floor.rules, 2, NULL, error,
                                  sizeof error) == PORTABLE_MESH_OK);
    PortableMeshKartInput inputs[2] = {{0}, {0}};
    PortableMeshKartEvents events[2];
    CHECK(portable_mesh_race_tick_items(&race, inputs, NULL, events, error, sizeof error) ==
          PORTABLE_MESH_UNSUPPORTED);
    CHECK(!race.state.tick);
    CHECK(portable_mesh_race_enable_actors(&race, &config, error, sizeof error) ==
          PORTABLE_MESH_OK);
    CHECK(!race.items_enabled && !portable_mesh_course_actors_count(&race.actors));
    for (unsigned tick = 0; tick < 181; ++tick)
    {
        CHECK(portable_mesh_race_tick_items(&race, inputs, NULL, events, error, sizeof error) ==
              PORTABLE_MESH_OK);
        CHECK(race.actors.sky.clock == tick + 1 && !race.actors.tick);
    }
    PortableMeshRaceSession before = race;
    inputs[0].buttons = 16;
    memset(events, 0x5a, sizeof events);
    PortableMeshKartEvents old[2];
    memcpy(old, events, sizeof old);
    CHECK(portable_mesh_race_tick_items(&race, inputs, NULL, events, error, sizeof error) ==
          PORTABLE_MESH_INVALID);
    CHECK(!memcmp(&race, &before, sizeof race) && !memcmp(events, old, sizeof old));
    inputs[0].buttons = 0;
    /* A rejected post-movement checkpoint turn cannot publish a clock step. */
    put(floor.edges, UINT32_MAX);
    before = race;
    CHECK(portable_mesh_race_tick_items(&race, inputs, NULL, events, error, sizeof error) ==
          PORTABLE_MESH_INVALID);
    CHECK(!memcmp(&race, &before, sizeof race) && !memcmp(events, old, sizeof old));
    race = before;
    put(floor.edges, 1);
    race.state.results_ready = 1;
    const uint64_t frozen = portable_mesh_course_actors_checksum(&race.actors);
    CHECK(portable_mesh_race_tick_items(&race, inputs, NULL, events, error, sizeof error) ==
          PORTABLE_MESH_OK);
    CHECK(frozen == portable_mesh_course_actors_checksum(&race.actors));
    return 0;
}
static int absent_and_wrap(void)
{
    PortableMeshSkyConfig config = {0};
    PortableMeshSky world;
    CHECK(portable_mesh_sky_init(&config, &world));
    CHECK(portable_mesh_sky_advance(&config, &world, &world));
    CHECK(!world.enabled && !world.clock && portable_mesh_sky_checksum(&config, &world) == 0);
    CHECK(portable_mesh_sky_hash(&world, UINT64_C(0x123456789abcdef0)) ==
          UINT64_C(0x123456789abcdef0));
    uint8_t bytes[PORTABLE_MESH_SKY_PROFILE_SIZE];
    fixture(bytes, 6);
    char error[192];
    CHECK(portable_mesh_sky_decode(bytes, sizeof bytes, 41, &config, error, sizeof error));
    CHECK(portable_mesh_sky_init(&config, &world));
    world.clock = UINT32_MAX;
    CHECK(portable_mesh_sky_advance(&config, &world, &world));
    CHECK(world.clock == 0);
    CHECK(world.motion.snapshot.scroll_x_q16[1] == -96 * 65536);
    return 0;
}
int main(void)
{
    const unsigned lifecycle_controllers[] = {2, 5, 6};
    for (unsigned i = 0; i < 3; ++i)
    {
        CHECK(!accepted_lifecycle(lifecycle_controllers[i]));
        CHECK(!shared_lifecycle(lifecycle_controllers[i]));
    }
    CHECK(!course_bindings());
    CHECK(!malformed_profiles());
    CHECK(!clock_and_views());
    CHECK(!rising_epoch());
    CHECK(!absent_and_wrap());
    puts("mesh sky: PASS MSK1 bounds source clocks immutable views RISE epoch rollback wrap");
    return 0;
}
