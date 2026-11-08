#include "mesh_time_trial_replay.h"
#include "mesh_items_state.h"
#include "mesh_course_fixture.h"
#include "mesh_item_fixture.h"
#include "mesh_item_rules_asset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "item replay line %d: %s\n", __LINE__, #x);                            \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
#define HEADER 112u
#define STATE 176u
#define RECORD 32u
#define CAPACITY 100000u
typedef struct
{
    PortableTestMeshCourse course;
    PortableMeshItemRules rules;
    PortableMeshItemsConfig config;
    PortableMeshTimeTrialCompatibility identity;
    PortableMeshTimeTrial trial, initial;
    PortableMeshItems world;
    uint8_t bytes[CAPACITY];
    size_t size;
    uint32_t ticks;
} Sample;
static void reset(Sample *s)
{
    char error[256];
    CHECK(portable_mesh_time_trial_init(&s->trial, &s->course.course, &s->course.rules, 0, error,
                                        sizeof error) == PORTABLE_MESH_OK);
    CHECK(portable_mesh_items_init(&s->world, &s->config, 123, error, sizeof error) ==
          PORTABLE_MESH_ITEMS_OK);
}
static void setup(Sample *s)
{
    memset(s, 0, sizeof *s);
    test_mesh_course_init(&s->course);
    uint8_t payload[PORTABLE_MESH_ITEM_RULES_BYTES];
    test_mir_payload(payload);
    const PortableAssetSectionView section = {
        .kind = 103, .content_id = 41, .version = 1, .bytes = payload, .size = sizeof payload};
    CHECK(portable_mesh_item_rules_asset_open(&section, &s->rules));
    s->rules.mushroom_boost_ticks = 4;
    s->rules.mushroom_acceleration_q16 = 24 * 65536;
    s->rules.mushroom_cap_ratio_q16 = 2 * 65536;
    s->config = (PortableMeshItemsConfig){
        &s->course.course, &s->rules, &s->course.rules, 2, 1, 1, 0, 0, NULL};
    s->identity = (PortableMeshTimeTrialCompatibility){.course_id = s->course.course.id,
                                                       .driver_id = 90001,
                                                       .profile_id = s->course.rules.profile_id,
                                                       .profile_revision = s->course.rules.revision,
                                                       .tick_numerator = 4389,
                                                       .tick_denominator = 262144};
    for (unsigned i = 0; i < 32; ++i)
        s->identity.digest[i] = (uint8_t)(3 + i * 7);
    reset(s);
    s->initial = s->trial;
}

typedef struct
{
    PortableMeshTriangle triangles[4];
    int fail_last;
} GroundReplayGeometry;
static int ground_replay_triangle(const void *context, uint32_t index, PortableMeshTriangle *out)
{
    const GroundReplayGeometry *g = context;
    if (index >= 4u || (g->fail_last && index == 3u))
        return 0;
    *out = g->triangles[index];
    return 1;
}
static int ground_replay_surface(const void *context, uint32_t index, PortableMeshSurface *out)
{
    (void)context;
    if (index)
        return 0;
    *out = (PortableMeshSurface){
        .friction_q16 = 65536, .speed_ratio_q16 = 65536, .gravity_ratio_q16 = 65536};
    return 1;
}
static void ground_follow_replay(Sample *s)
{
    setup(s);
    /* The ordinary TT strip stays flat. A separate, finite ramp supports the
     * real source crab patrol, far from kart/item contacts. */
    GroundReplayGeometry geometry = {
        .triangles = {{.vertices_q16 = {{-20 * 65536, -20 * 65536, 0},
                                        {20 * 65536, -20 * 65536, 0},
                                        {20 * 65536, 20 * 65536, 0}},
                       .respawn = UINT32_MAX},
                      {.vertices_q16 = {{-20 * 65536, -20 * 65536, 0},
                                        {20 * 65536, 20 * 65536, 0},
                                        {-20 * 65536, 20 * 65536, 0}},
                       .respawn = UINT32_MAX},
                      {.vertices_q16 = {{20 * 65536, 90 * 65536, 0},
                                        {40 * 65536, 90 * 65536, 10 * 65536},
                                        {40 * 65536, 110 * 65536, 10 * 65536}},
                       .respawn = UINT32_MAX},
                      {.vertices_q16 = {{20 * 65536, 90 * 65536, 0},
                                        {40 * 65536, 110 * 65536, 10 * 65536},
                                        {20 * 65536, 110 * 65536, 0}},
                       .respawn = UINT32_MAX}}};
    const PortableMeshCourse mesh = {
        &geometry, 4, 10 * 65536, ground_replay_triangle, ground_replay_surface, NULL, NULL};
    PortableMeshCourseActorsConfig *cfg = calloc(1, sizeof *cfg);
    PortableMeshCourseActors *actors = malloc(sizeof *actors), *recorded = malloc(sizeof *recorded),
                             *before_actors = malloc(sizeof *before_actors);
    PortableMeshItems *before_items = malloc(sizeof *before_items);
    CHECK(cfg && actors && recorded && before_actors && before_items);
    cfg->course_id = s->course.course.id;
    cfg->profile_id = 7;
    cfg->revision = 1;
    cfg->scale_q16 = 6554;
    cfg->count = 1;
    cfg->ground_follow_version = 1;
    CHECK(portable_crab_init(&cfg->initial[0], 100 * 65536, 100 * 65536, 0, 0));
    cfg->anchors_q16[0][0] = 25 * 65536;
    cfg->anchors_q16[0][1] = 100 * 65536;
    cfg->anchors_q16[0][2] = 5 * 65536 / 2;
    for (unsigned i = 0; i < 4; ++i)
        cfg->frame_ids[0][i] = i + 1;
    s->trial.geometry = &mesh;
    s->config.geometry = &mesh;
    CHECK(portable_mesh_course_actors_init_on_course(cfg, &mesh, actors, NULL, 0));
    CHECK(actors->crab_ground_offset_q16[0] == 0);
    PortableReplayMemoryOutput output = {s->bytes, sizeof s->bytes, 0};
    PortableReplayWriter writer = portable_replay_memory_writer(&output);
    CHECK(portable_mesh_replay_write_header_actors(&writer, &s->identity, &s->trial, &s->world,
                                                   &s->config, 123, actors,
                                                   cfg) == PORTABLE_MESH_REPLAY_OK);
    unsigned climbing = 0;
    const PortableMeshKartInput input = {0, PORTABLE_MESH_ACCELERATE};
    const PortableMeshItemControls controls = {0};
    PortableMeshKartEvents kart;
    PortableMeshItemEvents item;
    PortableMeshCourseActorEvents actor;
    char error[256];
    for (unsigned tick = 0; tick < 1000 && s->trial.state.phase != PORTABLE_MESH_TT_FINISHED;
         ++tick)
    {
        CHECK(portable_mesh_time_trial_tick_actors(&s->trial, &s->world, &s->config, actors, cfg,
                                                   input, controls, &kart, &item, &actor, error,
                                                   sizeof error) == PORTABLE_MESH_OK);
        climbing |= actors->crab_ground_offset_q16[0] > 65536;
        CHECK(portable_mesh_replay_write_tick_actors(&writer, input, controls, &s->trial, &s->world,
                                                     actors) == PORTABLE_MESH_REPLAY_OK);
    }
    CHECK(climbing && s->trial.state.phase == PORTABLE_MESH_TT_FINISHED);
    CHECK(portable_mesh_replay_write_end_actors(&writer, &s->trial, &s->world, actors) ==
          PORTABLE_MESH_REPLAY_OK);
    const uint64_t kart_hash = portable_mesh_time_trial_checksum(&s->trial.state),
                   item_hash = portable_mesh_items_checksum(&s->world),
                   actor_hash = portable_mesh_course_actors_checksum(actors);
    *recorded = *actors;
    s->size = output.size;
    reset(s);
    s->trial.geometry = &mesh;
    CHECK(portable_mesh_course_actors_init_on_course(cfg, &mesh, actors, NULL, 0));
    PortableReplayMemoryInput memory = {s->bytes, s->size, 0};
    PortableReplayReader reader = portable_replay_memory_reader(&memory);
    PortableMeshReplayPlayback playback, sentinel;
    memset(&playback, 0xa5, sizeof playback);
    sentinel = playback;
    const PortableMeshTimeTrial initial = s->trial;
    *before_items = s->world;
    *before_actors = *actors;
    s->trial.geometry = NULL;
    s->config.geometry = NULL;
    CHECK(portable_mesh_replay_playback_init_actors(&playback, &reader, &s->identity, &s->trial,
                                                    &s->world, &s->config, actors,
                                                    cfg) == PORTABLE_MESH_REPLAY_INCOMPATIBLE);
    CHECK(!memcmp(&playback, &sentinel, sizeof playback) &&
          !memcmp(&s->trial.state, &initial.state, sizeof initial.state) &&
          !memcmp(&s->world, before_items, sizeof s->world) &&
          !memcmp(actors, before_actors, sizeof *actors));
    /* First playback injects a late immutable-row failure after real ramp
     * progress. The second starts fresh, independently reproducing all owners. */
    for (unsigned attempt = 0; attempt < 2; ++attempt)
    {
        geometry.fail_last = 0;
        reset(s);
        s->trial.geometry = &mesh;
        s->config.geometry = &mesh;
        CHECK(portable_mesh_course_actors_init_on_course(cfg, &mesh, actors, NULL, 0));
        memory.position = 0;
        reader = portable_replay_memory_reader(&memory);
        CHECK(portable_mesh_replay_playback_init_actors(&playback, &reader, &s->identity, &s->trial,
                                                        &s->world, &s->config, actors,
                                                        cfg) == PORTABLE_MESH_REPLAY_OK);
        int injected = 0;
        for (;;)
        {
            if (!attempt && actors->tick > 20)
            {
                const PortableMeshTimeTrial before_trial = s->trial;
                *before_items = s->world;
                *before_actors = *actors;
                memset(&kart, 0xa5, sizeof kart);
                memset(&item, 0xa5, sizeof item);
                memset(&actor, 0xa5, sizeof actor);
                const PortableMeshKartEvents before_kart = kart;
                const PortableMeshItemEvents before_item = item;
                PortableMeshCourseActorEvents *before_event = malloc(sizeof *before_event);
                CHECK(before_event);
                *before_event = actor;
                const size_t position = memory.position;
                const uint32_t ticks = playback.ticks;
                geometry.fail_last = 1;
                CHECK(portable_mesh_replay_playback_tick_actor_events(&playback, NULL, &kart, &item,
                                                                      &actor) ==
                      PORTABLE_MESH_REPLAY_SIMULATION_ERROR);
                CHECK(!memcmp(&s->trial, &before_trial, sizeof s->trial) &&
                      !memcmp(&s->world, before_items, sizeof s->world) &&
                      !memcmp(actors, before_actors, sizeof *actors) &&
                      !memcmp(&kart, &before_kart, sizeof kart) &&
                      !memcmp(&item, &before_item, sizeof item) &&
                      !memcmp(&actor, before_event, sizeof actor) && playback.ticks == ticks &&
                      memory.position == position + 40u);
                const size_t consumed = memory.position;
                CHECK(portable_mesh_replay_playback_tick_actor_events(&playback, NULL, &kart, &item,
                                                                      &actor) ==
                          PORTABLE_MESH_REPLAY_SIMULATION_ERROR &&
                      memory.position == consumed);
                free(before_event);
                injected = 1;
                break;
            }
            const PortableMeshReplayStatus status = portable_mesh_replay_playback_tick_actor_events(
                &playback, NULL, &kart, &item, &actor);
            CHECK(status == PORTABLE_MESH_REPLAY_OK || status == PORTABLE_MESH_REPLAY_COMPLETE);
            CHECK(portable_mesh_course_actors_checksum(recorded) == actor_hash);
            if (status == PORTABLE_MESH_REPLAY_COMPLETE)
                break;
        }
        if (!attempt)
            CHECK(injected);
        else
            CHECK(portable_mesh_time_trial_checksum(&s->trial.state) == kart_hash &&
                  portable_mesh_items_checksum(&s->world) == item_hash &&
                  portable_mesh_course_actors_checksum(actors) == actor_hash);
    }
    free(before_items);
    free(before_actors);
    free(recorded);
    free(actors);
    free(cfg);
}
static void record(Sample *s)
{
    setup(s);
    PortableReplayMemoryOutput output = {s->bytes, sizeof s->bytes, 0};
    PortableReplayWriter writer = portable_replay_memory_writer(&output);
    CHECK(portable_mesh_replay_write_header_items(&writer, &s->identity, &s->trial, &s->world,
                                                  &s->config, 123) == PORTABLE_MESH_REPLAY_OK);
    int saw_mushroom = 0, saw_rescue = 0;
    for (unsigned i = 0; i < 1000 && s->trial.state.phase != PORTABLE_MESH_TT_FINISHED; ++i)
    {
        const PortableMeshKartInput input = {
            0, (uint16_t)(PORTABLE_MESH_ACCELERATE | (i == 210 ? PORTABLE_MESH_RESCUE : 0))};
        const PortableMeshItemControls held = {.item_pressed = (uint8_t)(i == 190 || i == 230),
                                               .aim_backward = (uint8_t)(i == 240)};
        PortableMeshKartEvents kart_events;
        PortableMeshItemEvents item_events;
        char error[256];
        CHECK(portable_mesh_time_trial_tick_items(&s->trial, &s->world, &s->config, input, held,
                                                  &kart_events, &item_events, error,
                                                  sizeof error) == PORTABLE_MESH_OK);
        saw_mushroom |= item_events.count != 0;
        saw_rescue |= s->trial.state.kart.respawn_ticks != 0;
        CHECK(portable_mesh_replay_write_tick_items(&writer, input, held, &s->trial, &s->world) ==
              PORTABLE_MESH_REPLAY_OK);
        ++s->ticks;
    }
    CHECK(saw_mushroom && saw_rescue && s->trial.state.phase == PORTABLE_MESH_TT_FINISHED);
    CHECK(portable_mesh_replay_write_end_items(&writer, &s->trial, &s->world) ==
          PORTABLE_MESH_REPLAY_OK);
    s->size = output.size;
    CHECK(s->size == HEADER + 2 * STATE + RECORD * ((size_t)s->ticks + 1));
}
static PortableMeshReplayStatus play(Sample *s, const uint8_t *bytes, size_t size)
{
    reset(s);
    PortableReplayMemoryInput memory = {bytes, size, 0};
    PortableReplayReader reader = portable_replay_memory_reader(&memory);
    PortableMeshReplayPlayback playback;
    PortableMeshReplayStatus result = portable_mesh_replay_playback_init_items(
        &playback, &reader, &s->identity, &s->trial, &s->world, &s->config);
    while (result == PORTABLE_MESH_REPLAY_OK)
        result = portable_mesh_replay_playback_tick(&playback, NULL);
    return result;
}
static void headers_and_roundtrip(Sample *s)
{
    const uint64_t kart = portable_mesh_time_trial_checksum(&s->trial.state),
                   items = portable_mesh_items_checksum(&s->world);
    PortableReplayMemoryInput memory = {s->bytes, s->size, 0};
    PortableReplayReader reader = portable_replay_memory_reader(&memory);
    PortableMeshReplayHeader header;
    CHECK(portable_mesh_replay_inspect_header(&reader, &header) == PORTABLE_MESH_REPLAY_OK &&
          memory.position == HEADER);
    CHECK(header.version == 2 && header.item_profile_id == s->rules.profile_id &&
          header.item_profile_revision == s->rules.revision && header.item_seed == 123);
    uint32_t ticks = 0;
    CHECK(portable_mesh_replay_validate(s->bytes, s->size, &s->identity, &ticks) ==
              PORTABLE_MESH_REPLAY_COMPLETE &&
          ticks == s->ticks);
    CHECK(play(s, s->bytes, s->size) == PORTABLE_MESH_REPLAY_COMPLETE);
    CHECK(portable_mesh_time_trial_checksum(&s->trial.state) == kart &&
          portable_mesh_items_checksum(&s->world) == items);
    memcpy(s->bytes, "MKSC3GHO", 8);
    CHECK(play(s, s->bytes, s->size) == PORTABLE_MESH_REPLAY_COMPLETE);
    memcpy(s->bytes, "MKSC3RPL", 8);
    reset(s);
    memory.position = 0;
    reader = portable_replay_memory_reader(&memory);
    PortableMeshReplayPlayback playback;
    CHECK(portable_mesh_replay_playback_init(&playback, &reader, &s->identity, &s->trial) ==
          PORTABLE_MESH_REPLAY_INCOMPATIBLE);
    s->rules.revision++;
    memory.position = 0;
    reader = portable_replay_memory_reader(&memory);
    CHECK(portable_mesh_replay_playback_init_items(&playback, &reader, &s->identity, &s->trial,
                                                   &s->world, &s->config) ==
          PORTABLE_MESH_REPLAY_INCOMPATIBLE);
    s->rules.revision--;
    s->config.human_mask = 0;
    memory.position = 0;
    reader = portable_replay_memory_reader(&memory);
    CHECK(portable_mesh_replay_playback_init_items(&playback, &reader, &s->identity, &s->trial,
                                                   &s->world, &s->config) ==
          PORTABLE_MESH_REPLAY_INCOMPATIBLE);
    s->config.human_mask = 1;
}
static void initial_transaction_and_v1(Sample *s)
{
    reset(s);
    const uint64_t initial_hash = portable_mesh_items_checksum(&s->world);
    PortableReplayMemoryInput memory = {s->bytes, s->size, 0};
    PortableReplayReader reader = portable_replay_memory_reader(&memory);
    PortableMeshReplayPlayback playback, before;
    memset(&playback, 0xa5, sizeof playback);
    memcpy(&before, &playback, sizeof before);
    s->world.inventory.racers[0].held_item = 0;
    CHECK(portable_mesh_replay_playback_init_items(&playback, &reader, &s->identity, &s->trial,
                                                   &s->world, &s->config) ==
          PORTABLE_MESH_REPLAY_INCOMPATIBLE);
    CHECK(!memcmp(&playback, &before, sizeof playback) &&
          s->world.inventory.racers[0].held_item == 0);
    reset(s);
    CHECK(portable_mesh_items_checksum(&s->world) == initial_hash);
    uint8_t original[88 + STATE];
    PortableReplayMemoryOutput out = {original, sizeof original, 0};
    PortableReplayWriter writer = portable_replay_memory_writer(&out);
    CHECK(portable_mesh_replay_write_header(&writer, &s->identity, &s->trial) ==
          PORTABLE_MESH_REPLAY_OK);
    CHECK(out.size == sizeof original && original[8] == 1 && original[12] == 88);
    memory = (PortableReplayMemoryInput){original, sizeof original, 0};
    reader = portable_replay_memory_reader(&memory);
    PortableMeshReplayHeader header;
    CHECK(portable_mesh_replay_inspect_header(&reader, &header) == PORTABLE_MESH_REPLAY_OK);
    CHECK(header.version == 1 && !header.item_profile_id && !header.item_profile_revision &&
          !header.item_seed && !header.initial_item_checksum && memory.position == 88);
    memory.position = 0;
    reader = portable_replay_memory_reader(&memory);
    CHECK(portable_mesh_replay_playback_init_items(&playback, &reader, &s->identity, &s->trial,
                                                   &s->world, &s->config) ==
          PORTABLE_MESH_REPLAY_INCOMPATIBLE);
    out.size = 0;
    writer = portable_replay_memory_writer(&out);
    CHECK(portable_mesh_replay_write_header_items(&writer, &s->identity, &s->trial, &s->world,
                                                  &s->config,
                                                  124) == PORTABLE_MESH_REPLAY_INVALID &&
          !out.size);
}
static void corrupted_turn(Sample *s, unsigned offset, uint8_t xor_mask,
                           PortableMeshReplayStatus expected)
{
    /* Tick191 actually requests a mushroom: corrupt that turn after all earlier
       turns verify, then compare raw owner bytes from the same initialized object. */
    const size_t at = HEADER + STATE + 190 * RECORD + offset;
    s->bytes[at] ^= xor_mask;
    reset(s);
    PortableReplayMemoryInput memory = {s->bytes, s->size, 0};
    PortableReplayReader reader = portable_replay_memory_reader(&memory);
    PortableMeshReplayPlayback playback;
    CHECK(portable_mesh_replay_playback_init_items(&playback, &reader, &s->identity, &s->trial,
                                                   &s->world,
                                                   &s->config) == PORTABLE_MESH_REPLAY_OK);
    for (unsigned i = 0; i < 190; ++i)
        CHECK(portable_mesh_replay_playback_tick(&playback, NULL) == PORTABLE_MESH_REPLAY_OK);
    PortableMeshItems *before = malloc(sizeof *before);
    CHECK(before);
    memcpy(before, &s->world, sizeof *before);
    uint8_t trial[sizeof s->trial];
    memcpy(trial, &s->trial, sizeof trial);
    PortableMeshKartEvents kart_events, old_kart;
    PortableMeshItemEvents *item_events = malloc(sizeof *item_events),
                           *old_items = malloc(sizeof *old_items);
    CHECK(item_events && old_items);
    memset(&kart_events, 0xa5, sizeof kart_events);
    memcpy(&old_kart, &kart_events, sizeof old_kart);
    memset(item_events, 0x5a, sizeof *item_events);
    memcpy(old_items, item_events, sizeof *item_events);
    CHECK(portable_mesh_replay_playback_tick_events(&playback, NULL, &kart_events, item_events) ==
          expected);
    CHECK(!memcmp(&kart_events, &old_kart, sizeof kart_events) &&
          !memcmp(item_events, old_items, sizeof *item_events));
    free(item_events);
    free(old_items);
    CHECK(!memcmp(trial, &s->trial, sizeof trial) && !memcmp(before, &s->world, sizeof *before));
    CHECK(playback.ticks == 190);
    free(before);
    s->bytes[at] ^= xor_mask;
}
static void verified_events(Sample *s)
{
    reset(s);
    PortableReplayMemoryInput memory = {s->bytes, s->size, 0};
    PortableReplayReader reader = portable_replay_memory_reader(&memory);
    PortableMeshReplayPlayback playback;
    CHECK(portable_mesh_replay_playback_init_items(&playback, &reader, &s->identity, &s->trial,
                                                   &s->world,
                                                   &s->config) == PORTABLE_MESH_REPLAY_OK);
    PortableMeshItemEvents *events = malloc(sizeof *events);
    CHECK(events);
    PortableMeshKartEvents kart;
    unsigned boosts = 0, uses = 0;
    PortableMeshReplayStatus result;
    do
    {
        memset(events, 0xa5, sizeof *events);
        result = portable_mesh_replay_playback_tick_events(&playback, NULL, &kart, events);
        CHECK(result == PORTABLE_MESH_REPLAY_OK || result == PORTABLE_MESH_REPLAY_COMPLETE);
        for (unsigned i = 0; i < events->count; ++i)
        {
            boosts += events->values[i].kind == PORTABLE_MESH_EVENT_BOOST;
            uses += events->values[i].kind == PORTABLE_MESH_EVENT_USE;
        }
    } while (result == PORTABLE_MESH_REPLAY_OK);
    CHECK(boosts == 2 && uses == 2 && !events->count);
    const PortableMeshKartEvents zero = {0};
    CHECK(!memcmp(&kart, &zero, sizeof kart));
    const uint64_t sum = portable_mesh_time_trial_checksum(&s->trial.state),
                   item_sum = portable_mesh_items_checksum(&s->world);
    memset(events, 0xa5, sizeof *events);
    memset(&kart, 0xa5, sizeof kart);
    CHECK(portable_mesh_replay_playback_tick_events(&playback, NULL, &kart, events) ==
          PORTABLE_MESH_REPLAY_COMPLETE);
    CHECK(!events->count && !memcmp(&kart, &zero, sizeof kart));
    CHECK(sum == portable_mesh_time_trial_checksum(&s->trial.state) &&
          item_sum == portable_mesh_items_checksum(&s->world));
    free(events);
}
static void legacy_events(Sample *s)
{
    reset(s);
    uint8_t *bytes = malloc(CAPACITY);
    CHECK(bytes);
    PortableReplayMemoryOutput output = {bytes, CAPACITY, 0};
    PortableReplayWriter writer = portable_replay_memory_writer(&output);
    CHECK(portable_mesh_replay_write_header(&writer, &s->identity, &s->trial) ==
          PORTABLE_MESH_REPLAY_OK);
    char error[256];
    PortableMeshKartEvents kart;
    for (unsigned tick = 0; tick < 1000 && s->trial.state.phase != PORTABLE_MESH_TT_FINISHED;
         ++tick)
    {
        const PortableMeshKartInput input = {
            0, (uint16_t)(PORTABLE_MESH_ACCELERATE | (tick == 260 ? PORTABLE_MESH_HOP : 0))}; /* after the burnout */
        CHECK(portable_mesh_time_trial_tick(&s->trial, input, &kart, error, sizeof error) ==
              PORTABLE_MESH_OK);
        CHECK(portable_mesh_replay_write_tick(&writer, input, &s->trial) ==
              PORTABLE_MESH_REPLAY_OK);
    }
    CHECK(s->trial.state.phase == PORTABLE_MESH_TT_FINISHED);
    CHECK(portable_mesh_replay_write_end(&writer, &s->trial) == PORTABLE_MESH_REPLAY_OK);
    reset(s);
    PortableReplayMemoryInput memory = {bytes, output.size, 0};
    PortableReplayReader reader = portable_replay_memory_reader(&memory);
    PortableMeshReplayPlayback playback;
    CHECK(portable_mesh_replay_playback_init(&playback, &reader, &s->identity, &s->trial) ==
          PORTABLE_MESH_REPLAY_OK);
    PortableMeshItemEvents *items = malloc(sizeof *items);
    CHECK(items);
    unsigned hops = 0;
    PortableMeshReplayStatus result;
    do
    {
        memset(items, 0xa5, sizeof *items);
        result = portable_mesh_replay_playback_tick_events(&playback, NULL, &kart, items);
        CHECK(result == PORTABLE_MESH_REPLAY_OK || result == PORTABLE_MESH_REPLAY_COMPLETE);
        CHECK(!items->count);
        hops += kart.hopped;
    } while (result == PORTABLE_MESH_REPLAY_OK);
    CHECK(hops == 1);
    free(items);
    free(bytes);
}
static void malformed_and_tail(Sample *s)
{
    corrupted_turn(s, 16, 1, PORTABLE_MESH_REPLAY_DIVERGED);
    corrupted_turn(s, 17, 1, PORTABLE_MESH_REPLAY_INVALID);
    corrupted_turn(s, 20, 1, PORTABLE_MESH_REPLAY_INVALID);
    corrupted_turn(s, 24, 1, PORTABLE_MESH_REPLAY_DIVERGED);
    const size_t last = HEADER + STATE + (size_t)s->ticks * RECORD;
    s->bytes[last + 24] ^= 1;
    CHECK(play(s, s->bytes, s->size) == PORTABLE_MESH_REPLAY_DIVERGED);
    CHECK(portable_mesh_replay_validate(s->bytes, s->size, &s->identity, NULL) ==
          PORTABLE_MESH_REPLAY_INVALID);
    s->bytes[last + 24] ^= 1;
    CHECK(play(s, s->bytes, s->size - 1) == PORTABLE_MESH_REPLAY_TRUNCATED);
    CHECK(portable_mesh_replay_validate(s->bytes, s->size - 1, &s->identity, NULL) ==
          PORTABLE_MESH_REPLAY_TRUNCATED);
    s->bytes[s->size] = 1;
    CHECK(play(s, s->bytes, s->size + 1) == PORTABLE_MESH_REPLAY_INVALID);
    s->bytes[96] ^= 1;
    CHECK(play(s, s->bytes, s->size) == PORTABLE_MESH_REPLAY_INCOMPATIBLE);
    s->bytes[96] ^= 1;
    s->bytes[8] = 3;
    CHECK(play(s, s->bytes, s->size) == PORTABLE_MESH_REPLAY_INVALID);
    s->bytes[8] = 2;
    s->bytes[76] = 1; /* another kart */
    CHECK(play(s, s->bytes, s->size) == PORTABLE_MESH_REPLAY_INCOMPATIBLE);
    s->bytes[76] = 0;
    s->bytes[100] = 1;
    CHECK(play(s, s->bytes, s->size) == PORTABLE_MESH_REPLAY_INVALID);
    s->bytes[100] = 0;
    s->bytes[104] ^= 1;
    CHECK(play(s, s->bytes, s->size) == PORTABLE_MESH_REPLAY_INCOMPATIBLE);
    s->bytes[104] ^= 1;
    s->bytes[12] = 88;
    CHECK(play(s, s->bytes, s->size) == PORTABLE_MESH_REPLAY_INVALID);
    s->bytes[12] = 112;
}
static void actors_roundtrip(Sample *s, unsigned family)
{
    setup(s);
    PortableMeshCourseActorsConfig *config = calloc(1, sizeof *config);
    PortableMeshCourseActors *actors = malloc(sizeof *actors),
                             *actor_before = malloc(sizeof *actor_before);
    PortableMeshItems *items_before = malloc(sizeof *items_before);
    CHECK(config && actors && actor_before && items_before);
    config->course_id = s->course.course.id;
    config->count = 1;
    config->profile_id = 7;
    config->revision = 1;
    config->scale_q16 = 6554;
    CHECK(portable_crab_init(&config->initial[0], 100 * 65536, 100 * 65536, 0, 0));
    config->anchors_q16[0][0] = 100 * 65536;
    config->anchors_q16[0][1] = 100 * 65536;
    for (unsigned frame = 0; frame < 4; ++frame)
        config->frame_ids[0][frame] = frame + 1;
    if(family==1 || family==2 || family==4 || family==6 || family==8 || family==10 || family==12 || family==14 || family==16 || family==18 || family==20 || family==22 || family==24 || family==26 || family==28 || family==30 || family==32 || family==34 || family==36)
    {
        config->snowman_count=1;
        if(family==1)config->count=0;
        CHECK(portable_snowman_init(&config->initial_snowmen[0],100*65536,100*65536,0,0));
        config->initial_snowmen[0].placement_order=1;
        config->snowman_anchors_q16[0][0]=100*65536;
        config->snowman_anchors_q16[0][1]=100*65536;
        config->snowman_frame_ids[0][0]=5;config->snowman_frame_ids[0][1]=6;
    }
    if(family==3 || family==4 || family==6 || family==8 || family==10 || family==12 || family==14 || family==16 || family==18 || family==20 || family==22 || family==24 || family==26 || family==28 || family==30 || family==32 || family==34 || family==36)
    {
        config->penguin_count=1;
        if(family==3)config->count=0;
        CHECK(portable_penguin_init(&config->initial_penguins[0],100*65536,100*65536,0,0));
        config->initial_penguins[0].placement_order=2;
        config->penguin_anchors_q16[0][0]=config->penguin_anchors_q16[0][1]=100*65536;
        for(unsigned frame=0;frame<6;++frame)config->penguin_frame_ids[0][frame]=7+frame;
    }
    if(family==5 || family==6 || family==8 || family==10 || family==12 || family==14 || family==16 || family==18 || family==20 || family==22 || family==24 || family==26 || family==28 || family==30 || family==32 || family==34 || family==36)
    {
        config->mouse_count=2;
        if(family==5)config->count=0;
        for(unsigned i=0;i<2;++i)
        {
            CHECK(portable_mouse_init(&config->initial_mice[i],100*65536,100*65536,0,
                i?UINT16_C(0x8001):0));
            config->initial_mice[i].placement_order=(uint8_t)(3+i);
            config->mouse_anchors_q16[i][0]=config->mouse_anchors_q16[i][1]=100*65536;
            config->mouse_frame_ids[i][0]=13;config->mouse_frame_ids[i][1]=14;
        }
    }
    if(family==7 || family==8 || family==10 || family==12 || family==14 || family==16 || family==18 || family==20 || family==22 || family==24 || family==26 || family==28 || family==30 || family==32 || family==34 || family==36)
    {
        config->piranha_count=1;
        if(family==7)config->count=0;
        CHECK(portable_piranha_init(&config->initial_piranhas[0],100*65536,100*65536,0,0));
        config->initial_piranhas[0].placement_order=5;
        config->piranha_anchors_q16[0][0]=config->piranha_anchors_q16[0][1]=100*65536;
        for(unsigned frame=0;frame<5;++frame)config->piranha_frame_ids[0][frame]=15+frame;
        config->capture_forward_speed_q16=12*65536;
        config->capture_up_speed_q16=3*65536;
    }
    if(family==9 || family==10 || family==12 || family==14 || family==16 || family==18 || family==20 || family==22 || family==24 || family==26 || family==28 || family==30 || family==32 || family==34 || family==36)
    {
        config->seagull_count=1;
        if(family==9)config->count=0;
        CHECK(portable_seagull_init(&config->initial_seagulls[0],100*65536,100*65536,0,0));
        config->initial_seagulls[0].placement_order=6;
        config->seagull_anchors_q16[0][0]=config->seagull_anchors_q16[0][1]=100*65536;
        for(unsigned frame=0;frame<3;++frame)config->seagull_frame_ids[0][frame]=20+frame;
    }
    if(family==11 || family==12 || family==14 || family==16 || family==18 || family==20 || family==22 || family==24 || family==26 || family==28 || family==30 || family==32 || family==34 || family==36)
    {
        config->lava_count=1;
        if(family==11)config->count=0;
        CHECK(portable_lava_bubble_init(&config->initial_lava[0],100*65536,100*65536,0,0));
        config->initial_lava[0].placement_order=7;
        config->lava_anchors_q16[0][0]=config->lava_anchors_q16[0][1]=100*65536;
        config->lava_frame_ids[0][0]=23;config->lava_frame_ids[0][1]=24;
    }
    if(family==13 || family==14 || family==16 || family==18 || family==20 || family==22 || family==24 || family==26 || family==28 || family==30 || family==32 || family==34 || family==36)
    {
        config->thwomp_count=1;
        if(family==13)config->count=0;
        CHECK(portable_thwomp_init(&config->initial_thwomps[0],100*65536,100*65536,0,0));
        config->initial_thwomps[0].placement_order=8;
        config->thwomp_anchors_q16[0][0]=config->thwomp_anchors_q16[0][1]=100*65536;
        config->thwomp_frame_ids[0][0]=25;
        for(unsigned table=0;table<5;++table)
        {
            config->crush_rules.counts[table]=1;
            config->crush_rules.rows[table][0]=(PortableDriverAffineRow){256,256,1};
        }
        config->crush_rules.rows[2][0]=(PortableDriverAffineRow){224,384,110};
        config->crush_rules.rows[3][0]=(PortableDriverAffineRow){224,384,18};
        config->crush_rules.rows[4][0]=(PortableDriverAffineRow){224,384,112};
    }
    if(family==15 || family==16 || family==18 || family==20 || family==22 || family==24 || family==26 || family==28 || family==30 || family==32 || family==34 || family==36)
    {
        config->cannonball_count=1;
        if(family==15)config->count=0;
        CHECK(portable_cannonball_init(&config->initial_cannonballs[0],100*65536,100*65536,0,3));
        config->cannonball_variants[0]=3;
        config->initial_cannonballs[0].placement_order=9;
        config->cannonball_anchors_q16[0][0]=config->cannonball_anchors_q16[0][1]=100*65536;
        for(unsigned frame=0;frame<7;++frame)config->cannonball_frame_ids[0][frame]=26+frame;
    }
    if(family==17 || family==18 || family==20 || family==22 || family==24 || family==26 || family==28 || family==30 || family==32 || family==34 || family==36)
    {
        config->boo_count=1;
        if(family==17)config->count=0;
        CHECK(portable_boo_init(&config->initial_boos[0],100*65536,100*65536,0,1));
        config->initial_boos[0].placement_order=10;
        config->boo_anchors_q16[0][0]=config->boo_anchors_q16[0][1]=100*65536;
        config->boo_frame_ids[0][0]=33;config->boo_frame_ids[0][1]=34;
    }
    if(family==19 || family==20 || family==22 || family==24 || family==26 || family==28 || family==30 || family==32 || family==34 || family==36)
    {
        config->rock_count=1;
        if(family==19)config->count=0;
        CHECK(portable_course_prop_init(&config->initial_rocks[0],100*65536,100*65536,
                                       UINT32_C(0xa20003),224,0));
        config->initial_rocks[0].placement_order=11;
        config->rock_anchors_q16[0][0]=config->rock_anchors_q16[0][1]=100*65536;
        for(unsigned frame=0;frame<5;++frame)config->rock_frame_ids[0][frame]=35+frame;
    }
    if (family == 21 || family == 22 || family == 24 || family == 26 || family == 28 || family == 30 || family == 32 || family == 34 || family == 36)
    {
        if (family == 21) config->count = 0;
        config->coins = (PortableMeshCourseCoinsConfig){
            .course_id = config->course_id, .enabled = 1, .profile_id = 7,
            .revision = 1, .modes = 31, .cap = 99, .initial = 5, .placement_count = 1,
            .loss = {4, 2, 1, 8, 1}};
        for (unsigned count = 0; count < 100; ++count)
            config->coins.ratios_q16[count] =
                (int32_t)(((90u + (count < 10 ? count : 10u)) * 65536u + 50u) / 100u);
        config->coins.placements[0] = (PortableMeshCourseCoin){
            .id = 1, .object_index = 1, .model_id = 40,
            .position_q16 = {0, 2 * 65536, 0}, .half_q16 = {32768, 32768, 32768}};
    }
    if(family==23 || family==24 || family==26 || family==28 || family==30 || family==32 || family==34 || family==36)
    {
        if(family==23)config->count=0;
        config->teepee_count=1;
        CHECK(portable_course_prop_init(&config->initial_teepees[0],100*65536,100*65536,
                                        0x030000u,256,0));
        config->initial_teepees[0].placement_order=12;
        config->teepee_anchors_q16[0][1]=4*65536;
        for(unsigned frame=0;frame<11;++frame)config->teepee_frame_ids[0][frame]=50+frame;
    }
    if(family==25 || family==26 || family==28 || family==30 || family==32 || family==34 || family==36)
    {
        if(family==25)config->count=0;
        config->falling_star_count=1;
        CHECK(portable_falling_star_init(&config->initial_falling_stars[0],100*65536,100*65536,0,0));
        config->initial_falling_stars[0].placement_order=13;
        config->falling_star_anchors_q16[0][0]=100*65536;
        config->falling_star_anchors_q16[0][1]=100*65536;
        for(unsigned frame=0;frame<5;++frame)config->falling_star_frame_ids[0][frame]=70+frame;
    }
    if(family==35 || family==36)
    {
        if(family==35)config->count=0;
        config->pier_boo_count=1;config->actor_visual_seed=123;
        config->pier_boo_rules=(PortableBooRules){1,360,64,1024,1024,3*65536,16384};
        CHECK(portable_course_prop_init(&config->initial_pier_boos[0],100*65536,100*65536,0xa30000u,256,0));
        config->initial_pier_boos[0].placement_order=20;
        config->pier_boo_anchors_q16[0][0]=config->pier_boo_anchors_q16[0][1]=100*65536;
        for(unsigned n=0;n<3;++n)config->pier_boo_frame_ids[0][n]=190+n;
    }
    if(family==33 || family==34 || family==36)
    {
        if(family==33)config->count=0;
        config->kamek_count=1;
        CHECK(portable_course_prop_init(&config->initial_kameks[0],100*65536,100*65536,0xa00000u,224,0));
        config->initial_kameks[0].placement_order=19;
        config->kamek_anchors_q16[0][0]=config->kamek_anchors_q16[0][1]=100*65536;
        config->kamek_frame_ids[0][0]=180;config->kamek_frame_ids[0][1]=181;
    }
    if(family==31 || family==32 || family==34 || family==36)
    {
        if(family==31)config->count=0;
        config->mechakoopa_count=1;
        CHECK(portable_course_prop_init(&config->initial_mechakoopas[0],100*65536,100*65536,0xa10000u,224,0));
        config->initial_mechakoopas[0].placement_order=18;
        config->mechakoopa_anchors_q16[0][0]=config->mechakoopa_anchors_q16[0][1]=100*65536;
        for(unsigned n=0;n<4;++n)config->mechakoopa_frame_ids[0][n]=170+n;
    }
    if(family==29 || family==30 || family==32 || family==34 || family==36)
    {
        if(family==29)config->count=0;
        config->common_prop_count=2;
        for(unsigned n=0;n<2;++n)
        {
            CHECK(portable_course_prop_init(&config->initial_common_props[n],100*65536,100*65536,
                n?UINT32_C(0x860000):UINT32_C(0x060000),256,0));
            config->initial_common_props[n].placement_order=(uint8_t)(16+n);
            config->common_prop_anchors_q16[n][0]=config->common_prop_anchors_q16[n][1]=100*65536;
            config->common_prop_frame_ids[n][0]=160+n*2;
            config->common_prop_frame_ids[n][1]=161+n*2;
        }
    }
    if(family==27 || family==28 || family==30 || family==32 || family==34 || family==36)
    {
        if(family==27)config->count=0;
        config->thundercloud_count=2;
        for(unsigned n=0;n<2;++n)
        {
            CHECK(portable_thundercloud_init(&config->initial_thunderclouds[n],100*65536,100*65536,0,(uint16_t)n));
            config->initial_thunderclouds[n].placement_order=(uint8_t)(14+n);
            config->thundercloud_anchors_q16[n][0]=config->thundercloud_anchors_q16[n][1]=100*65536;
            for(unsigned frame=0;frame<72;++frame)config->thundercloud_frame_ids[n][frame]=80+frame;
        }
    }
    CHECK(portable_mesh_course_actors_init(config, actors, NULL, 0));
    if(family==17 || family==18 || family==20 || family==22 || family==24 || family==26 || family==28 || family==30 || family==32 || family==34 || family==36)
    {
        const uint64_t pristine=portable_mesh_course_actors_checksum(actors);
        actors->boo_pending_mask=1;
        const uint64_t pending=portable_mesh_course_actors_checksum(actors);
        actors->boo_pending_mask=0;actors->boo_flash_mask=1;
        const uint64_t flash=portable_mesh_course_actors_checksum(actors);
        CHECK(pristine!=pending && pristine!=flash && pending!=flash);
        actors->boo_flash_mask=0;
    }
    if(family==1)
        CHECK(portable_mesh_course_actors_checksum(actors)==UINT64_C(0x2a667fbf3592d994));
    if(!family)
    {
        /* Frozen pre-snowman MAS1 baseline from explicit LE named fields. */
        CHECK(portable_mesh_course_actors_checksum(actors)==UINT64_C(0x1a8b695c82be177b));
        actors->actor_grace[0]=7;
        CHECK(portable_mesh_course_actors_checksum(actors)==UINT64_C(0x1a8b695c82be177b));
        actors->actor_grace[0]=0;
    }
    PortableReplayMemoryOutput output = {s->bytes, sizeof s->bytes, 0};
    PortableReplayWriter writer = portable_replay_memory_writer(&output);
    CHECK(portable_mesh_replay_write_header_actors(&writer, &s->identity, &s->trial, &s->world,
                                                   &s->config, 123, actors,
                                                   config) == PORTABLE_MESH_REPLAY_OK);
    unsigned coin_pickups = 0, teepee_attached = 0;
    s->ticks = 0;
    PortableMeshKartEvents kart;
    PortableMeshItemEvents item;
    PortableMeshCourseActorEvents actor;
    char error[256];
    for (unsigned tick = 0; tick < 1000 && s->trial.state.phase != PORTABLE_MESH_TT_FINISHED;
         ++tick)
    {
        const PortableMeshKartInput input = {0, PORTABLE_MESH_ACCELERATE};
        const PortableMeshItemControls controls = {.item_pressed = (uint8_t)(tick == 190)};
        CHECK(portable_mesh_time_trial_tick_actors(&s->trial, &s->world, &s->config, actors, config,
                                                   input, controls, &kart, &item, &actor, error,
                                                   sizeof error) == PORTABLE_MESH_OK);
        CHECK(portable_mesh_replay_write_tick_actors(&writer, input, controls, &s->trial, &s->world,
                                                     actors) == PORTABLE_MESH_REPLAY_OK);
        coin_pickups += actor.coins.picked[0] != 0;
        if((family==23 || family==24 || family==26 || family==28 || family==30 || family==32 || family==34 || family==36) && actors->teepees[0].teepee.rider_state==1)
        {
            teepee_attached=1;
            CHECK(s->world.status.racers[0].boo_lock_ticks>0 &&
                  s->world.status.racers[0].boo_lock_ticks<=360 && !actor.hit_mask);
        }
        ++s->ticks;
    }
    if (family == 21 || family == 22)
        CHECK(coin_pickups == 1 && actors->coins.count[0] == 6 && !actors->coins.available);
    if(family==24 || family==26 || family==28 || family==30 || family==32 || family==34 || family==36)
        CHECK(coin_pickups==1 && actors->coins.count[0]<=6 && !actors->coins.available);
    if(family==23 || family==24 || family==26 || family==28 || family==30 || family==32 || family==34 || family==36)CHECK(teepee_attached);
    CHECK(s->trial.state.phase == PORTABLE_MESH_TT_FINISHED && actors->tick > 0);
    if(family==27 || family==28 || family==30 || family==32 || family==34 || family==36)CHECK(actors->thundercloud_palette_phase[0]>0 &&
                       !actors->thundercloud_palette_phase[1]);
    CHECK(portable_mesh_replay_write_end_actors(&writer, &s->trial, &s->world, actors) ==
          PORTABLE_MESH_REPLAY_OK);
    s->size = output.size;
    CHECK(s->size == 128u + 2 * STATE + 40u * ((size_t)s->ticks + 1));
    const uint64_t kart_hash = portable_mesh_time_trial_checksum(&s->trial.state),
                   item_hash = portable_mesh_items_checksum(&s->world),
                   actor_hash = portable_mesh_course_actors_checksum(actors);
    PortableReplayMemoryInput memory = {s->bytes, s->size, 0};
    PortableReplayReader reader = portable_replay_memory_reader(&memory);
    PortableMeshReplayHeader header;
    CHECK(portable_mesh_replay_inspect_header(&reader, &header) == PORTABLE_MESH_REPLAY_OK &&
          memory.position == 128 && header.version == 3 && header.actor_profile_id == 7 &&
          header.actor_profile_revision == 1);
    uint32_t ticks = 0;
    CHECK(portable_mesh_replay_validate(s->bytes, s->size, &s->identity, &ticks) ==
              PORTABLE_MESH_REPLAY_COMPLETE &&
          ticks == s->ticks);
    /* A ghost uses its own freshly initialized actor owner and the same wire. */
    memcpy(s->bytes, "MKSC3GHO", 8);
    reset(s);
    CHECK(portable_mesh_course_actors_init(config, actors, NULL, 0));
    memory.position = 0;
    reader = portable_replay_memory_reader(&memory);
    PortableMeshReplayPlayback playback;
    CHECK(portable_mesh_replay_playback_init_actors(&playback, &reader, &s->identity, &s->trial,
                                                    &s->world, &s->config, actors,
                                                    config) == PORTABLE_MESH_REPLAY_OK);
    PortableMeshReplayStatus status;
    do
    {
        status =
            portable_mesh_replay_playback_tick_actor_events(&playback, NULL, &kart, &item, &actor);
        CHECK(status == PORTABLE_MESH_REPLAY_OK || status == PORTABLE_MESH_REPLAY_COMPLETE);
    } while (status == PORTABLE_MESH_REPLAY_OK);
    CHECK(portable_mesh_time_trial_checksum(&s->trial.state) == kart_hash &&
          portable_mesh_items_checksum(&s->world) == item_hash &&
          portable_mesh_course_actors_checksum(actors) == actor_hash);
    const PortableMeshCourseActorEvents empty = {0};
    CHECK(!memcmp(&actor, &empty, sizeof actor));
    memset(&actor, 0xa5, sizeof actor);
    CHECK(portable_mesh_replay_playback_tick_actor_events(&playback, NULL, NULL, NULL, &actor) ==
              PORTABLE_MESH_REPLAY_COMPLETE &&
          !memcmp(&actor, &empty, sizeof actor));
    /* Header/config mismatches never publish even the playback cursor. */
    for (unsigned mismatch = 0; mismatch < 5; ++mismatch)
    {
        reset(s);
        CHECK(portable_mesh_course_actors_init(config, actors, NULL, 0));
        if (mismatch == 0)
            s->bytes[120] ^= 1;
        if (mismatch == 1)
            ++config->revision;
        if (mismatch == 2)
            ++config->course_id;
        if (mismatch == 3)
        {
            if(family>=35)actors->pier_boo_visual_rng^=1;
            else if(family>=33)actors->kameks[0].kamek.phase=1024;
            else if(family>=31)actors->mechakoopas[0].mechakoopa.animation_phase^=1;
            else if(family>=29)actors->common_props[1].contact_phase=1;
            else if(family>=27)actors->thundercloud_palette_phase[0]=1;
            else if(family>=25)actors->falling_stars[0].angle=1;
            else if(family>=23)actors->teepee_plane_q16[0]=65536;
            else if(family>=21)--actors->coins.count[0];
            else if(family>=19)actors->lava_burn_ticks[0]=250;
            else if(family>=17)actors->boo_flash_mask=1;
            else if(family>=15)actors->strong_hit[0]=(PortableMeshStrongHitState){.rate=0x1000,.pending=1,.direction=1};
            else if(family>=13)actors->crush[0]=(PortableMeshCrushState){.contact_timer=0x7000,
                .affine={.inverse_x_q8=224,.inverse_y_q8=384,.mode=5,.remaining=109}};
            else if(family>=11)actors->lava_burn_ticks[0]=250;
            else if(family>=9)actors->seagulls[0].state=16;
            else if(family>=7)actors->piranhas[0].struck_player ^= 1u;
            else if(family>=5)++actors->mice[0].timer;
            else if(family>=3)++actors->penguins[0].timer;
            else if(family)++actors->snowmen[0].timer;else ++actors->crabs[0].timer;
        }
        if(mismatch==4)
        {
            actors->count=family==1?1u:family==2?2u:0u;
            actors->snowman_count=family==0?1u:0u;
            if(family==3){actors->count=1;actors->penguin_count=0;}
            if(family==5 || family==6){actors->count=config->count+1;actors->mouse_count=1;}
            if(family==7 || family==8){actors->count=config->count+1;actors->piranha_count=0;}
            if(family==9 || family==10){actors->count=config->count+1;actors->seagull_count=0;}
            if(family==11 || family==12){actors->count=config->count+1;actors->lava_count=0;}
            if(family>=35)actors->pier_boo_count=0;
            else if(family>=33)actors->kamek_count=0;
            else if(family>=31)actors->mechakoopa_count=0;
            else if(family>=29)actors->common_prop_count=0;
            else if(family>=27)actors->thundercloud_count=0;
            else if(family>=25)actors->falling_star_count=0;
            else if(family>=23)actors->teepee_count=0;
            else if(family>=21)actors->coins.available|=UINT64_C(2);
            else if(family>=19){actors->count=config->count+1;actors->rock_count=0;}
            else if(family>=17){actors->count=config->count+1;actors->boo_count=0;}
            else if(family>=15){actors->count=config->count+1;actors->cannonball_count=0;}
            else if(family>=13){actors->count=config->count+1;actors->thwomp_count=0;}
        }
        const PortableMeshTimeTrialSession trial = s->trial;
        *actor_before = *actors;
        *items_before = s->world;
        memset(&playback, 0xa5, sizeof playback);
        const PortableMeshReplayPlayback unchanged = playback;
        memory.position = 0;
        reader = portable_replay_memory_reader(&memory);
        CHECK(portable_mesh_replay_playback_init_actors(&playback, &reader, &s->identity, &s->trial,
                                                        &s->world, &s->config, actors, config) ==
              PORTABLE_MESH_REPLAY_INCOMPATIBLE);
        CHECK(!memcmp(&playback, &unchanged, sizeof playback) &&
              !memcmp(&trial, &s->trial, sizeof trial) &&
              !memcmp(actor_before, actors, sizeof *actors) &&
              !memcmp(items_before, &s->world, sizeof s->world));
        if (mismatch == 0)
            s->bytes[120] ^= 1;
        if (mismatch == 1)
            --config->revision;
        if (mismatch == 2)
            --config->course_id;
    }
    /* Tick191 follows genuine active actor turns; late divergence must roll
     * back kart, inventory, actor state and every optional event output. */
    for (unsigned corruption = 0; corruption < (family==7 || family==8 || family>=13?4u:2u); ++corruption)
    {
        const size_t at = 128u + STATE + 190u * 40u + 32u;
        reset(s);
        CHECK(portable_mesh_course_actors_init(config, actors, NULL, 0));
        memory.position = 0;
        reader = portable_replay_memory_reader(&memory);
        CHECK(portable_mesh_replay_playback_init_actors(&playback, &reader, &s->identity, &s->trial,
                                                        &s->world, &s->config, actors,
                                                        config) == PORTABLE_MESH_REPLAY_OK);
        for (unsigned turn = 0; turn < 190; ++turn)
            CHECK(portable_mesh_replay_playback_tick(&playback, NULL) == PORTABLE_MESH_REPLAY_OK);
        if (corruption == 0)
            s->bytes[at] ^= 1;
        else if (corruption == 2)
        {
            if(family>=35)actors->pier_boos[0].source_code=UINT32_C(0xa30001);
            else if(family>=33)actors->kameks[0].source_code=UINT32_C(0xa00001);
            else if(family>=31)actors->mechakoopas[0].source_code=UINT32_C(0xa10002);
            else if(family>=29)actors->common_props[0].source_code=UINT32_C(0x060002);
            else if(family>=27)actors->thunderclouds[0].variant=4;
            else if(family>=25)actors->falling_stars[0].variant=6;
            else if(family>=23)actors->teepees[0].teepee.rider_state=3;
            else if(family>=21)actors->coins.count[0]=100;
            else if(family>=19)actors->rocks[0].lake_rock.frame=5;
            else if(family>=17)actors->boos[0].phase|=1;
            else if(family>=15)actors->strong_hit[0].pending=2;
            else if(family>=13)actors->crush[0].contact_timer=1;
            else actors->piranha_victim[0] = 0;
        }
        else if (corruption == 3)
        {
            if(family>=35)actors->pier_boos[0].flags=4;
            else if(family>=33)actors->kameks[0].flags=4;
            else if(family>=31)actors->mechakoopas[0].flags=4;
            else if(family>=29)actors->common_props[1].contact_phase=2;
            else if(family>=27)actors->thundercloud_palette_phase[0]=9;
            else if(family>=25)actors->falling_stars[0].flags=4;
            else if(family>=23)actors->teepees[0].source_code=0x030001u;
            else if(family>=21)actors->coins.available|=UINT64_C(2);
            else if(family>=19)actors->rocks[0].source_code=UINT32_C(0xa20004);
            else if(family>=17)actors->boos[0].heading=0;
            else if(family>=15)actors->strong_hit[0]=(PortableMeshStrongHitState){.active=1,.rate=1};
            else actors->capture[0].pull_q16[0] = 1;
        }
        else
        {
            if(family>=25)s->world.status.racers[0].boo_lock_ticks=2; /* Survives the next clock decrement. */
            else if(family>=23)++s->world.status.racers[0].boo_lock_ticks;
            else if(family>=21)--actors->coins.count[0];
            else if(family>=19)actors->lava_burn_ticks[0]=250;
            else if(family>=17)actors->boo_pending_mask=1;
            else if(family>=15)actors->strong_hit[0]=(PortableMeshStrongHitState){.rate=0x1000,.pending=1,.direction=1};
            else if(family>=13)actors->crush[0]=(PortableMeshCrushState){.contact_timer=0x7000,
                .affine={.inverse_x_q8=224,.inverse_y_q8=384,.mode=5,.remaining=109}};
            else if(family>=11)actors->lava_burn_ticks[0]=250;
            else if(family>=9)actors->seagulls[0].state=16;
            else if(family>=7)actors->piranhas[0].struck_player ^= 1u;
            else if(family>=5)++actors->mice[0].timer;
            else if(family>=3)++actors->penguins[0].timer;
            else if(family)++actors->snowmen[0].timer;else ++actors->crabs[0].timer;
        }
        *actor_before = *actors;
        *items_before = s->world;
        const PortableMeshTimeTrialSession trial = s->trial;
        memset(&kart, 0xa5, sizeof kart);
        memset(&item, 0xa5, sizeof item);
        memset(&actor, 0xa5, sizeof actor);
        const PortableMeshKartEvents old_kart = kart;
        const PortableMeshItemEvents old_item = item;
        const PortableMeshCourseActorEvents old_actor = actor;
        /* Valid wire records that fail actor-world validation are simulation
         * errors; valid worlds with mismatched recorded hashes diverge. */
        const PortableMeshReplayStatus expected = (corruption == 3 || (family>=13 && corruption==2))
            ? PORTABLE_MESH_REPLAY_SIMULATION_ERROR : PORTABLE_MESH_REPLAY_DIVERGED;
        const PortableMeshReplayStatus actual = portable_mesh_replay_playback_tick_actor_events(
            &playback, NULL, &kart, &item, &actor);
        if (actual != expected)
            fprintf(stderr, "actor replay family=%u corruption=%u actual=%s expected=%s\n",
                family, corruption, portable_mesh_replay_status_name(actual),
                portable_mesh_replay_status_name(expected));
        CHECK(actual == expected);
        CHECK(playback.ticks == 190 && !memcmp(&trial, &s->trial, sizeof trial) &&
              !memcmp(actor_before, actors, sizeof *actors) &&
              !memcmp(items_before, &s->world, sizeof s->world) &&
              !memcmp(&kart, &old_kart, sizeof kart) && !memcmp(&item, &old_item, sizeof item) &&
              !memcmp(&actor, &old_actor, sizeof actor));
        if (corruption == 0)
            s->bytes[at] ^= 1;
    }
    const size_t end = 128u + STATE + (size_t)s->ticks * 40u;
    s->bytes[end + 32] ^= 1;
    CHECK(portable_mesh_replay_validate(s->bytes, s->size, &s->identity, NULL) ==
          PORTABLE_MESH_REPLAY_INVALID);
    s->bytes[end + 32] ^= 1;
    CHECK(portable_mesh_replay_validate(s->bytes, s->size - 1, &s->identity, NULL) ==
          PORTABLE_MESH_REPLAY_TRUNCATED);
    reset(s);
    CHECK(portable_mesh_course_actors_init(config, actors, NULL, 0));
    memory.position = 0;
    reader = portable_replay_memory_reader(&memory);
    CHECK(portable_mesh_replay_playback_init_items(&playback, &reader, &s->identity, &s->trial,
                                                   &s->world, &s->config) ==
          PORTABLE_MESH_REPLAY_INCOMPATIBLE);
    /* No-actor content remains v2; an actor writer must not silently upgrade it. */
    config->count = config->snowman_count = config->penguin_count = config->mouse_count = config->piranha_count = config->seagull_count = config->lava_count = config->thwomp_count = config->cannonball_count = config->boo_count = config->rock_count = config->teepee_count = config->falling_star_count = config->thundercloud_count = config->common_prop_count = config->mechakoopa_count = config->kamek_count = config->pier_boo_count = 0;
    memset(&config->coins, 0, sizeof config->coins);
    CHECK(portable_mesh_course_actors_init(config, actors, NULL, 0));
    output.size = 0;
    writer = portable_replay_memory_writer(&output);
    CHECK(portable_mesh_replay_write_header_actors(&writer, &s->identity, &s->trial, &s->world,
                                                   &s->config, 123, actors,
                                                   config) == PORTABLE_MESH_REPLAY_INVALID &&
          !output.size);
    CHECK(portable_mesh_replay_write_header_items(&writer, &s->identity, &s->trial, &s->world,
                                                  &s->config, 123) == PORTABLE_MESH_REPLAY_OK &&
          s->bytes[8] == 2 && output.size == HEADER + STATE);
    free(items_before);
    free(actor_before);
    free(actors);
    free(config);
}
/* The old v3 record remains structurally valid, but its blocked mushroom
 * turn must diverge under corrected held-item eligibility. Construct only
 * that historical state in the test; no legacy simulation path is retained. */
static void teepee_item_replay(Sample *s, unsigned legacy)
{
    setup(s);
    PortableMeshCourseActorsConfig *cfg = calloc(1, sizeof *cfg);
    PortableMeshCourseActors *actors = malloc(sizeof *actors),
                             *before_actors = malloc(sizeof *actors);
    PortableMeshItems *before_items = malloc(sizeof *before_items);
    CHECK(cfg && actors && before_actors && before_items);
    cfg->course_id = s->course.course.id;
    cfg->profile_id = 7;
    cfg->revision = 1;
    cfg->scale_q16 = 6554;
    cfg->teepee_count = 1;
    CHECK(portable_course_prop_init(&cfg->initial_teepees[0], 100 * 65536, 100 * 65536, 0x030000u,
                                    256, 0));
    cfg->initial_teepees[0].placement_order = 12;
    cfg->teepee_anchors_q16[0][1] = 4 * 65536;
    for (unsigned f = 0; f < 11; ++f)
        cfg->teepee_frame_ids[0][f] = 50 + f;
    CHECK(portable_mesh_course_actors_init(cfg, actors, NULL, 0));
    PortableReplayMemoryOutput output = {s->bytes, sizeof s->bytes, 0};
    PortableReplayWriter writer = portable_replay_memory_writer(&output);
    CHECK(portable_mesh_replay_write_header_actors(&writer, &s->identity, &s->trial, &s->world,
                                                   &s->config, 123, actors,
                                                   cfg) == PORTABLE_MESH_REPLAY_OK);
    PortableMeshKartEvents kart;
    PortableMeshItemEvents item;
    PortableMeshCourseActorEvents actor;
    const PortableMeshKartInput input = {0, PORTABLE_MESH_ACCELERATE};
    char error[256];
    unsigned press_tick = 0;
    for (unsigned tick = 0; tick < 1000 && s->trial.state.phase != PORTABLE_MESH_TT_FINISHED;
         ++tick)
    {
        const unsigned press = !press_tick && s->trial.state.phase == PORTABLE_MESH_TT_RACING &&
                               actors->teepees[0].teepee.rider_state == 1 &&
                               s->world.status.racers[0].boo_lock_ticks > 1 &&
                               s->trial.state.kart.grounded &&
                               !s->world.inventory.racers[0].previous_item_pressed;
        if (press)
        {
            CHECK(s->world.inventory.racers[0].held_item == 0x1008 &&
                  !s->world.inventory.racers[0].active && !s->world.status.racers[0].spin_ticks &&
                  !s->world.status.racers[0].golden_ticks && !s->world.controls[0].recovering &&
                  !s->world.previous_item[0]);
            for (unsigned i = 0; i < 64; ++i)
                CHECK(s->world.actors[i].phase == PORTABLE_MESH_ACTOR_FREE);
        }
        const PortableMeshItemControls controls = {.item_pressed = (uint8_t)press};
        const PortableMeshItemControls admitted = {.item_pressed = (uint8_t)(press && !legacy)};
        CHECK(portable_mesh_time_trial_tick_actors(&s->trial, &s->world, &s->config, actors, cfg,
                                                   input, admitted, &kart, &item, &actor, error,
                                                   sizeof error) == PORTABLE_MESH_OK);
        if (press)
        {
            press_tick = s->trial.state.tick;
            CHECK(press_tick == 182 && s->world.status.racers[0].boo_lock_ticks == 359);
            if (legacy)
            {
                /* Actual old-code capture: blocked press changed precisely
                 * these three histories relative to a neutral accepted turn. */
                CHECK(!item.count && s->world.inventory.racers[0].held_item == 0x1008);
                s->world.controls[0].item_pressed = 1;
                s->world.inventory.racers[0].previous_item_pressed = 1;
                s->world.previous_item[0] = 1;
                CHECK(portable_mesh_time_trial_checksum(&s->trial.state) ==
                      UINT64_C(0x0c35454d72e01eb1));
                CHECK(portable_mesh_items_checksum(&s->world) == UINT64_C(0x98ed67025a18b66b));
                CHECK(portable_mesh_course_actors_checksum(actors) == UINT64_C(0x75916799c9fe115f));
            }
            else
            {
                unsigned uses = 0, boosts = 0;
                for (unsigned i = 0; i < item.count; ++i)
                {
                    uses += item.values[i].kind == PORTABLE_MESH_EVENT_USE;
                    boosts += item.values[i].kind == PORTABLE_MESH_EVENT_BOOST;
                }
                CHECK(uses == 1 && boosts == 1 && s->world.inventory.racers[0].held_item != 0x1008);
                CHECK(portable_mesh_items_checksum(&s->world) != UINT64_C(0x98ed67025a18b66b));
            }
        }
        CHECK(portable_mesh_replay_write_tick_actors(&writer, input, controls, &s->trial, &s->world,
                                                     actors) == PORTABLE_MESH_REPLAY_OK);
    }
    CHECK(press_tick && s->trial.state.phase == PORTABLE_MESH_TT_FINISHED);
    CHECK(portable_mesh_replay_write_end_actors(&writer, &s->trial, &s->world, actors) ==
          PORTABLE_MESH_REPLAY_OK);
    s->size = output.size;
    const uint64_t final_kart = portable_mesh_time_trial_checksum(&s->trial.state),
                   final_items = portable_mesh_items_checksum(&s->world),
                   final_actors = portable_mesh_course_actors_checksum(actors);
    CHECK(s->bytes[8] == 3 && portable_mesh_replay_validate(s->bytes, s->size, &s->identity,
                                                            NULL) == PORTABLE_MESH_REPLAY_COMPLETE);
    reset(s);
    CHECK(portable_mesh_course_actors_init(cfg, actors, NULL, 0));
    PortableReplayMemoryInput memory = {s->bytes, s->size, 0};
    PortableReplayReader reader = portable_replay_memory_reader(&memory);
    PortableMeshReplayPlayback playback;
    CHECK(portable_mesh_replay_playback_init_actors(&playback, &reader, &s->identity, &s->trial,
                                                    &s->world, &s->config, actors,
                                                    cfg) == PORTABLE_MESH_REPLAY_OK);
    for (unsigned tick = 1; tick < press_tick; ++tick)
        CHECK(portable_mesh_replay_playback_tick_actor_events(&playback, NULL, &kart, &item,
                                                              &actor) == PORTABLE_MESH_REPLAY_OK);
    if (legacy)
    {
        const PortableMeshTimeTrial before_trial = s->trial;
        *before_items = s->world;
        *before_actors = *actors;
        memset(&kart, 0xa5, sizeof kart);
        const PortableMeshKartEvents old_kart = kart;
        memset(&item, 0x5a, sizeof item);
        const PortableMeshItemEvents old_item = item;
        memset(&actor, 0x3c, sizeof actor);
        const PortableMeshCourseActorEvents old_actor = actor;
        CHECK(portable_mesh_replay_playback_tick_actor_events(
                  &playback, NULL, &kart, &item, &actor) == PORTABLE_MESH_REPLAY_DIVERGED);
        CHECK(playback.ticks == press_tick - 1 &&
              !memcmp(&s->trial, &before_trial, sizeof before_trial) &&
              !memcmp(&s->world, before_items, sizeof *before_items) &&
              !memcmp(actors, before_actors, sizeof *actors));
        CHECK(!memcmp(&kart, &old_kart, sizeof kart) && !memcmp(&item, &old_item, sizeof item) &&
              !memcmp(&actor, &old_actor, sizeof actor));
    }
    else
    {
        unsigned uses = 0, boosts = 0;
        PortableMeshReplayStatus result;
        do
        {
            result = portable_mesh_replay_playback_tick_actor_events(&playback, NULL, &kart, &item,
                                                                     &actor);
            CHECK(result == PORTABLE_MESH_REPLAY_OK || result == PORTABLE_MESH_REPLAY_COMPLETE);
            for (unsigned i = 0; i < item.count; ++i)
            {
                uses += item.values[i].kind == PORTABLE_MESH_EVENT_USE;
                boosts += item.values[i].kind == PORTABLE_MESH_EVENT_BOOST;
            }
        } while (result == PORTABLE_MESH_REPLAY_OK);
        CHECK(uses == 1 && boosts == 1 &&
              portable_mesh_time_trial_checksum(&s->trial.state) == final_kart &&
              portable_mesh_items_checksum(&s->world) == final_items &&
              portable_mesh_course_actors_checksum(actors) == final_actors);
    }
    free(before_items);
    free(before_actors);
    free(actors);
    free(cfg);
}

int main(int argc, char **argv)
{
    CHECK(argc >= 1 && argc <= 2);
    const char *group = argc == 1 ? "all" : argv[1];
    CHECK((!strcmp(group, "all") || !strcmp(group, "base") || !strcmp(group, "early") ||
           !strcmp(group, "middle") || !strcmp(group, "late")));
    Sample *s = malloc(sizeof *s);
    CHECK(s);
    if (!strcmp(group, "all") || !strcmp(group, "base"))
    {
        ground_follow_replay(s);
        teepee_item_replay(s, 1);
        teepee_item_replay(s, 0);
        record(s);
        headers_and_roundtrip(s);
        initial_transaction_and_v1(s);
        verified_events(s);
        legacy_events(s);
        malformed_and_tail(s);
    }
    /* Every family starts from setup(s), so emulator groups retain the same
     * independent roundtrip/corruption assertions without enlarging timeouts. */
    for (unsigned family = 0; family <= 36; ++family)
        if (!strcmp(group, "all") || (!strcmp(group, "early") && family <= 12) ||
            (!strcmp(group, "middle") && family >= 13 && family <= 24) ||
            (!strcmp(group, "late") && family >= 25))
            actors_roundtrip(s, family);
    free(s);
    return 0;
}
