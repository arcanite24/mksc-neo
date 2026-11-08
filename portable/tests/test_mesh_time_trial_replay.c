#include "mesh_time_trial_replay.h"
#include "mesh_time_trial_store.h"
#include "mesh_course_fixture.h"
#include "test_temp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "mesh replay line %d: %s\n", __LINE__, #c);                            \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
#define STATE_BYTES (PORTABLE_MESH_TIME_TRIAL_STATE_WORDS * 4u)
#define SAMPLE_CAPACITY 20000u

typedef struct
{
    PortableTestMeshCourse fixture;
    PortableMeshTimeTrialCompatibility identity;
    PortableMeshTimeTrialSession initial, finished;
    uint8_t *bytes, *states;
    size_t size;
    uint32_t ticks;
} Recording;
static void record_course(Recording *recording)
{
    memset(recording, 0, sizeof *recording);
    test_mesh_course_init(&recording->fixture);
    recording->identity =
        (PortableMeshTimeTrialCompatibility){.course_id = recording->fixture.course.id,
                                             .driver_id = 90001u,
                                             .profile_id = recording->fixture.rules.profile_id,
                                             .profile_revision = recording->fixture.rules.revision,
                                             .tick_numerator = 4389u,
                                             .tick_denominator = 262144u};
    for (unsigned i = 0u; i < 32u; ++i)
        recording->identity.digest[i] = (uint8_t)(i * 7u + 3u);
    char error[256];
    CHECK(portable_mesh_time_trial_init(&recording->initial, &recording->fixture.course,
                                        &recording->fixture.rules, 0u, error,
                                        sizeof error) == PORTABLE_MESH_OK);
    recording->finished = recording->initial;
    recording->bytes = malloc(SAMPLE_CAPACITY);
    recording->states = malloc(1000u * STATE_BYTES);
    CHECK(recording->bytes != NULL && recording->states != NULL);
    PortableReplayMemoryOutput output = {recording->bytes, SAMPLE_CAPACITY, 0u};
    PortableReplayWriter writer = portable_replay_memory_writer(&output);
    CHECK(portable_mesh_replay_write_header(&writer, &recording->identity, &recording->initial) ==
          PORTABLE_MESH_REPLAY_OK);
    int saw_air = 0, saw_steering = 0;
    while (recording->finished.state.phase != PORTABLE_MESH_TT_FINISHED && recording->ticks < 1000u)
    {
        const uint32_t tick = recording->ticks;
        const PortableMeshKartInput input = {
            tick >= 185u && tick < 195u ? (int16_t)(tick & 1u ? 1234 : -1234) : 0,
            /* Throttle from GO: holding it through MK8's countdown burns out. */
            (uint16_t)((tick >= 180u ? PORTABLE_MESH_ACCELERATE : 0u) | (tick == 210u ? PORTABLE_MESH_HOP : 0u))};
        PortableMeshKartEvents events;
        const PortableMeshStatus status = portable_mesh_time_trial_tick(
            &recording->finished, input, &events, error, sizeof error);
        if (status != PORTABLE_MESH_OK)
            fprintf(stderr, "mesh recording tick=%u phase=%u status=%s detail=%s\n",
                    recording->ticks, recording->finished.state.phase,
                    portable_mesh_status_name(status), error);
        CHECK(status == PORTABLE_MESH_OK);
        CHECK(portable_mesh_replay_write_tick(&writer, input, &recording->finished) ==
              PORTABLE_MESH_REPLAY_OK);
        CHECK(portable_mesh_time_trial_state_write(
            &recording->finished.state, recording->states + (size_t)recording->ticks * STATE_BYTES,
            STATE_BYTES));
        saw_air |= events.hopped;
        saw_steering |= recording->finished.state.kart.steer_filtered_q16 != 0;
        ++recording->ticks;
    }
    CHECK(recording->finished.state.phase == PORTABLE_MESH_TT_FINISHED && recording->ticks > 180u &&
          saw_air && saw_steering);
    CHECK(recording->finished.state.kart.position_q16[1] <
          recording->initial.state.kart.position_q16[1]);
    CHECK(portable_mesh_replay_write_end(&writer, &recording->finished) == PORTABLE_MESH_REPLAY_OK);
    recording->size = output.size;
    CHECK(recording->size == PORTABLE_MESH_REPLAY_HEADER_BYTES + 2u * STATE_BYTES +
                                 16u * ((size_t)recording->ticks + 1u));
}
static void check_playback(const Recording *recording)
{
    PortableReplayMemoryInput memory = {recording->bytes, recording->size, 0u};
    PortableReplayReader reader = portable_replay_memory_reader(&memory);
    PortableMeshReplayHeader header;
    CHECK(portable_mesh_replay_inspect_header(&reader, &header) == PORTABLE_MESH_REPLAY_OK);
    CHECK(header.compatibility.course_id == 65536u && header.compatibility.driver_id == 90001u &&
          header.version == 1u && !header.ghost);
    CHECK(memory.position == PORTABLE_MESH_REPLAY_HEADER_BYTES);
    CHECK(recording->bytes[16u] == 0u &&
          recording->bytes[18u] == 1u); /* Full course ID, explicit LE. */
    memory.position = 0u;
    PortableMeshTimeTrialSession playback = recording->initial;
    PortableMeshReplayPlayback cursor;
    CHECK(portable_mesh_replay_playback_init(&cursor, &reader, &recording->identity, &playback) ==
          PORTABLE_MESH_REPLAY_OK);
    for (uint32_t tick = 0u; tick < recording->ticks; ++tick)
    {
        CHECK(portable_mesh_replay_playback_tick(&cursor, NULL) == PORTABLE_MESH_REPLAY_OK);
        uint8_t actual[STATE_BYTES];
        CHECK(portable_mesh_time_trial_state_write(&playback.state, actual, sizeof actual));
        CHECK(memcmp(actual, recording->states + (size_t)tick * STATE_BYTES, sizeof actual) == 0);
    }
    CHECK(portable_mesh_replay_playback_tick(&cursor, NULL) == PORTABLE_MESH_REPLAY_COMPLETE);
    CHECK(cursor.ticks == recording->ticks &&
          playback.state.finish_ticks == recording->finished.state.finish_ticks);
    CHECK(portable_mesh_replay_playback_tick(&cursor, NULL) == PORTABLE_MESH_REPLAY_COMPLETE);
    memory.position = 0u;
    playback = recording->initial;
    size_t ticks = 0u;
    CHECK(portable_mesh_replay_run(&reader, &recording->identity, &playback, NULL, &ticks) ==
              PORTABLE_MESH_REPLAY_COMPLETE &&
          ticks == recording->ticks);
}
static void malformed_checks(const Recording *recording)
{
    uint32_t ticks = 0u;
    CHECK(portable_mesh_replay_validate(recording->bytes, recording->size, &recording->identity,
                                        &ticks) == PORTABLE_MESH_REPLAY_COMPLETE &&
          ticks == recording->ticks);
    const size_t cuts[] = {0u,
                           7u,
                           PORTABLE_MESH_REPLAY_HEADER_BYTES - 1u,
                           PORTABLE_MESH_REPLAY_HEADER_BYTES + STATE_BYTES,
                           recording->size - 1u,
                           recording->size - STATE_BYTES - 16u};
    for (unsigned i = 0u; i < sizeof cuts / sizeof cuts[0]; ++i)
        CHECK(portable_mesh_replay_validate(recording->bytes, cuts[i], &recording->identity,
                                            NULL) == PORTABLE_MESH_REPLAY_TRUNCATED);
    uint8_t *changed = malloc(recording->size + 1u);
    CHECK(changed != NULL);
    memcpy(changed, recording->bytes, recording->size);
    changed[recording->size] = 0u;
    CHECK(portable_mesh_replay_validate(changed, recording->size + 1u, &recording->identity,
                                        NULL) == PORTABLE_MESH_REPLAY_INVALID);
    PortableMeshTimeTrialCompatibility wrong = recording->identity;
    wrong.digest[31] ^= 1u;
    PortableReplayMemoryInput memory = {recording->bytes, recording->size, 0u};
    PortableReplayReader reader = portable_replay_memory_reader(&memory);
    PortableMeshTimeTrialSession playback = recording->initial;
    const PortableMeshTimeTrialState before = playback.state;
    CHECK(portable_mesh_replay_run(&reader, &wrong, &playback, NULL, NULL) ==
          PORTABLE_MESH_REPLAY_INCOMPATIBLE);
    CHECK(memcmp(&before, &playback.state, sizeof before) == 0);
    wrong = recording->identity;
    wrong.course_id += 65536u;
    CHECK(portable_mesh_replay_validate(recording->bytes, recording->size, &wrong, NULL) ==
          PORTABLE_MESH_REPLAY_INCOMPATIBLE);
    memcpy(changed, recording->bytes, recording->size);
    changed[8u] = 2u;
    CHECK(portable_mesh_replay_validate(changed, recording->size, &recording->identity, NULL) ==
          PORTABLE_MESH_REPLAY_INVALID);
    memcpy(changed, recording->bytes, recording->size);
    changed[PORTABLE_MESH_REPLAY_HEADER_BYTES] ^= 1u;
    CHECK(portable_mesh_replay_validate(changed, recording->size, &recording->identity, NULL) ==
          PORTABLE_MESH_REPLAY_INVALID);
    memcpy(changed, recording->bytes, recording->size);
    changed[PORTABLE_MESH_REPLAY_HEADER_BYTES + STATE_BYTES + 8u] ^=
        1u; /* First accepted checksum. */
    memory = (PortableReplayMemoryInput){changed, recording->size, 0u};
    reader = portable_replay_memory_reader(&memory);
    playback = recording->initial;
    CHECK(portable_mesh_replay_run(&reader, &recording->identity, &playback, NULL, NULL) ==
          PORTABLE_MESH_REPLAY_DIVERGED);
    CHECK(memcmp(&before, &playback.state, sizeof before) == 0);
    memcpy(changed, recording->bytes, recording->size);
    changed[PORTABLE_MESH_REPLAY_HEADER_BYTES + STATE_BYTES + 4u] = 128u;
    CHECK(portable_mesh_replay_validate(changed, recording->size, &recording->identity, NULL) ==
          PORTABLE_MESH_REPLAY_INVALID);
    CHECK(portable_mesh_replay_validate(recording->bytes, PORTABLE_MESH_REPLAY_MAX_BYTES + 1u,
                                        &recording->identity, NULL) == PORTABLE_MESH_REPLAY_LIMIT);
    uint8_t short_bytes[8];
    PortableReplayMemoryOutput output = {short_bytes, sizeof short_bytes, 0u};
    PortableReplayWriter writer = portable_replay_memory_writer(&output);
    CHECK(portable_mesh_replay_write_header(&writer, &recording->identity, &recording->initial) ==
              PORTABLE_MESH_REPLAY_IO_ERROR &&
          writer.failed);
    free(changed);
}
static void expect_file(const char *path, const uint8_t *expected, size_t size)
{
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    uint8_t *actual = malloc(size);
    CHECK(actual != NULL);
    CHECK(fread(actual, 1u, size, file) == size && fgetc(file) == EOF && fclose(file) == 0);
    CHECK(memcmp(actual, expected, size) == 0);
    free(actual);
}
static void store_checks(const Recording *recording)
{
    char directory[256], ghost[4096], other[4096], pb[4096], subdirectory[512];
    CHECK(test_temp_path(directory, sizeof directory, "mesh-store", 1) == 0);
    CHECK(portable_mesh_store_path(directory, &recording->identity, 1, ghost, sizeof ghost));
    CHECK(strstr(ghost, "/mesh3d/course-65536-driver-90001-profile-1-r1-") != NULL);
    {
        /* A chosen kart keeps its own ghost beside the stock one. */
        PortableMeshTimeTrialCompatibility karted = recording->identity;
        karted.kart_id = 7u;
        CHECK(portable_mesh_store_path(directory, &karted, 1, other, sizeof other) &&
              strstr(other, "/mesh3d/course-65536-driver-90001-kart-7-profile-1-r1-") != NULL);
    }
    CHECK(portable_mesh_store_path(directory, &recording->identity, 0, pb, sizeof pb));
    CHECK(portable_mesh_ghost_save(ghost, &recording->identity, recording->bytes,
                                   recording->size) == PORTABLE_MESH_STORE_OK);
    uint8_t *saved = NULL;
    size_t saved_size = 0u;
    CHECK(portable_mesh_ghost_load(ghost, &recording->identity, &saved, &saved_size) ==
              PORTABLE_MESH_STORE_OK &&
          saved_size == recording->size);
    CHECK(memcmp(saved, "MKSC3GHO", 8u) == 0 &&
          memcmp(saved + 8u, recording->bytes + 8u, saved_size - 8u) == 0);
    PortableReplayMemoryInput input = {saved, saved_size, 0u};
    PortableReplayReader reader = portable_replay_memory_reader(&input);
    PortableMeshTimeTrialSession replay = recording->initial;
    CHECK(portable_mesh_replay_run(&reader, &recording->identity, &replay, NULL, NULL) ==
          PORTABLE_MESH_REPLAY_COMPLETE);
    PortableMeshTimeTrialCompatibility wrong = recording->identity;
    wrong.digest[0] ^= 128u;
    uint8_t *untouched = (uint8_t *)recording->bytes;
    size_t untouched_size = 123u;
    CHECK(portable_mesh_ghost_load(ghost, &wrong, &untouched, &untouched_size) ==
              PORTABLE_MESH_STORE_INCOMPATIBLE &&
          untouched == recording->bytes && untouched_size == 123u);
    CHECK(portable_mesh_ghost_save(ghost, &wrong, recording->bytes, recording->size) ==
          PORTABLE_MESH_STORE_INCOMPATIBLE);
    CHECK(portable_mesh_ghost_save(ghost, &recording->identity, recording->bytes,
                                   recording->size - 1u) == PORTABLE_MESH_STORE_INVALID);
    expect_file(ghost, saved, saved_size);
    /* Exhausting exclusive staging names must not truncate an existing save. */
    char temporary[4160];
    for (unsigned i = 0u; i < 32u; ++i)
    {
        snprintf(temporary, sizeof temporary, "%s.tmp-%lu-%u", ghost,
                 (unsigned long)test_process_id(), i);
        FILE *file = fopen(temporary, "wb");
        CHECK(file != NULL && fputc(71, file) != EOF && fclose(file) == 0);
    }
    CHECK(portable_mesh_ghost_save(ghost, &recording->identity, recording->bytes,
                                   recording->size) == PORTABLE_MESH_STORE_IO_ERROR);
    expect_file(ghost, saved, saved_size);
    for (unsigned i = 0u; i < 32u; ++i)
    {
        snprintf(temporary, sizeof temporary, "%s.tmp-%lu-%u", ghost,
                 (unsigned long)test_process_id(), i);
        const uint8_t marker = 71u;
        expect_file(temporary, &marker, 1u);
        CHECK(remove(temporary) == 0);
    }
    CHECK(portable_mesh_store_path(directory, &wrong, 1, other, sizeof other) &&
          strcmp(ghost, other) != 0);
    CHECK(portable_mesh_ghost_load(other, &wrong, &untouched, &untouched_size) ==
          PORTABLE_MESH_STORE_NOT_FOUND);
    PortableMeshPersonalBest best = {recording->finished.state.finish_ticks,
                                     recording->finished.state.finish_ticks},
                             loaded;
    int updated = -1;
    CHECK(portable_mesh_personal_best_update(pb, &recording->identity, &best, &loaded, &updated) ==
              PORTABLE_MESH_STORE_OK &&
          updated);
    const PortableMeshPersonalBest original = loaded;
    best.race_ticks += 20u;
    best.best_lap_ticks += 20u;
    CHECK(portable_mesh_personal_best_update(pb, &recording->identity, &best, &loaded, &updated) ==
              PORTABLE_MESH_STORE_OK &&
          !updated);
    CHECK(loaded.race_ticks == original.race_ticks &&
          loaded.best_lap_ticks == original.best_lap_ticks);
    CHECK(portable_mesh_personal_best_load(pb, &wrong, &loaded) ==
          PORTABLE_MESH_STORE_INCOMPATIBLE);
    CHECK(portable_mesh_personal_best_update(pb, &wrong, &best, &loaded, &updated) ==
          PORTABLE_MESH_STORE_INCOMPATIBLE);
    CHECK(portable_mesh_personal_best_load(pb, &recording->identity, &loaded) ==
              PORTABLE_MESH_STORE_OK &&
          loaded.race_ticks == original.race_ticks);
    best = original;
    --best.best_lap_ticks;
    CHECK(portable_mesh_personal_best_update(pb, &recording->identity, &best, &loaded, &updated) ==
              PORTABLE_MESH_STORE_OK &&
          updated && loaded.best_lap_ticks == best.best_lap_ticks);
    CHECK(remove(ghost) == 0 && remove(pb) == 0);
    snprintf(subdirectory, sizeof subdirectory, "%s/mesh3d", directory);
    CHECK(test_rmdir(subdirectory) == 0 && test_rmdir(directory) == 0);
    free(saved);
}
static void sky_profile(PortableMeshSkyConfig *out)
{
    uint8_t bytes[PORTABLE_MESH_SKY_PROFILE_SIZE] = {0};
    const uint32_t header[8] = {
        UINT32_C(0x314b534d), 1, PORTABLE_MESH_SKY_PROFILE_SIZE, 41, 7, 7, 1, 1024};
    const int32_t rules[23] = {6,  7,  7,  8, 7, 0, 0, 0, 0, -96, 0, 38,
                               32, 38, -1, 0, 0, 0, 2, 0, 0, 1,   2};
    for (unsigned n = 0; n < 8; ++n)
        for (unsigned b = 0; b < 4; ++b)
            bytes[n * 4 + b] = (uint8_t)(header[n] >> (b * 8));
    for (unsigned n = 0; n < 23; ++n)
        for (unsigned b = 0; b < 4; ++b)
            bytes[32 + n * 4 + b] = (uint8_t)((uint32_t)rules[n] >> (b * 8));
    for (unsigned n = 0; n < 3; ++n)
    {
        const uint32_t width = 256u + n * 256u, radius = (1024u + n * 8u) * 65536u;
        for (unsigned b = 0; b < 4; ++b)
        {
            bytes[124 + n * 8 + b] = (uint8_t)(width >> (b * 8));
            bytes[128 + n * 8 + b] = (uint8_t)(radius >> (b * 8));
        }
    }
    bytes[148 + 256 * 4 + 1] = 128;
    bytes[148 + 768 * 4 + 1] = 128;
    bytes[148 + 768 * 4 + 2] = 255;
    bytes[148 + 768 * 4 + 3] = 255;
    char error[192];
    CHECK(portable_mesh_sky_decode(bytes, sizeof bytes, 41, out, error, sizeof error));
}
static void sky_only_replay(const Recording *legacy)
{
    enum
    {
        CAPACITY = 64000,
        HEADER = 128,
        RECORD = 40
    };
    char error[256];
    PortableMeshCourseActorsConfig *config = calloc(1, sizeof *config);
    PortableMeshCourseActors *actors = calloc(1, sizeof *actors), *fresh = calloc(1, sizeof *fresh);
    PortableMeshCourseActors *ghost = calloc(1, sizeof *ghost);
    uint8_t *bytes = malloc(CAPACITY), *broken = malloc(CAPACITY);
    CHECK(config && actors && fresh && ghost && bytes && broken);
    config->course_id = legacy->fixture.course.id;
    config->profile_id = 41;
    config->revision = 7;
    sky_profile(&config->sky);
    CHECK(portable_mesh_course_actors_init(config, actors, error, sizeof error));
    *fresh = *actors;
    PortableMeshTimeTrial trial = legacy->initial;
    PortableReplayMemoryOutput output = {bytes, CAPACITY, 0};
    PortableReplayWriter writer = portable_replay_memory_writer(&output);
    CHECK(portable_mesh_replay_write_header_actors(&writer, &legacy->identity, &trial, NULL, NULL,
                                                   0, actors, config) == PORTABLE_MESH_REPLAY_OK);
    for (unsigned n = 88; n < 112; ++n)
        CHECK(bytes[n] == 0);
    const size_t header_size = output.size;
    /* A declared MIR owner cannot be omitted even when the actor config itself
     * contains only a sky. Presence is authoritative, not the property type. */
    PortableTestMeshCourse declared = legacy->fixture;
    for (unsigned n = 0; n < declared.course.table_count; ++n)
        if (declared.course.tables[n].tag == PORTABLE_COURSE_3D_TAG('M', 'E', 'T', 'A'))
            declared.course.tables[n].bytes = declared.meta;
    test_mesh_word(declared.meta + 100, 1);
    uint8_t property[32] = {0};
    static const uint8_t item_key[] = "mksc:item-rules";
    declared.course.tables[declared.course.table_count++] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('P', 'R', 'O', 'P'), 1, 32, property};
    declared.course.tables[declared.course.table_count++] = (PortableCourse3DTable){
        PORTABLE_COURSE_3D_TAG('S', 'T', 'R', 'S'), sizeof item_key, 1, item_key};
    uint8_t payload = 0;
    const PortableAssetSectionView section = {
        .kind = 103, .content_id = 41, .version = 1, .bytes = &payload, .size = 1};
    const PortableAssetPackView pack = {.section_count = 1, .resolved_sections = &section};
    declared.course.pack = &pack;
    for (unsigned marker = 0; marker < 2; ++marker)
    {
        memset(property, 0, sizeof property);
        test_mesh_word(property + 4, marker ? PORTABLE_COURSE_3D_PROPERTY_BOOLEAN
                                            : PORTABLE_COURSE_3D_PROPERTY_ASSET);
        test_mesh_word(property + 8, marker ? 1 : 103);
        if (!marker)
            test_mesh_word(property + 12, 41);
        PortableCourse3DProperty parsed;
        CHECK(portable_course_3d_property_at(&declared.course, 0, &parsed));
        PortableMeshTimeTrial declared_trial = legacy->initial;
        declared_trial.course = &declared.course;
        const PortableMeshTimeTrial before_declared = declared_trial;
        CHECK(portable_mesh_replay_write_header_actors(&writer, &legacy->identity, &declared_trial,
                                                       NULL, NULL, 0, fresh,
                                                       config) == PORTABLE_MESH_REPLAY_INVALID);
        CHECK(output.size == header_size);
        PortableReplayMemoryInput declared_input = {bytes, header_size, 0};
        PortableReplayReader declared_reader = portable_replay_memory_reader(&declared_input);
        PortableMeshReplayPlayback rejected;
        memset(&rejected, 0xa5, sizeof rejected);
        const PortableMeshReplayPlayback before_rejected = rejected;
        *ghost = *fresh;
        CHECK(portable_mesh_replay_playback_init_actors(
                  &rejected, &declared_reader, &legacy->identity, &declared_trial, NULL, NULL,
                  ghost, config) == PORTABLE_MESH_REPLAY_INCOMPATIBLE);
        CHECK(memcmp(&rejected, &before_rejected, sizeof rejected) == 0 &&
              memcmp(&declared_trial, &before_declared, sizeof declared_trial) == 0 &&
              memcmp(ghost, fresh, sizeof *fresh) == 0);
        PortableMeshKartEvents declared_kart;
        PortableMeshItemEvents declared_items;
        PortableMeshCourseActorEvents declared_actors;
        memset(&declared_kart, 0xa5, sizeof declared_kart);
        memset(&declared_items, 0xa5, sizeof declared_items);
        memset(&declared_actors, 0xa5, sizeof declared_actors);
        const PortableMeshKartEvents before_kart = declared_kart;
        const PortableMeshItemEvents before_items = declared_items;
        const PortableMeshCourseActorEvents before_actors = declared_actors;
        CHECK(portable_mesh_time_trial_tick_actors(
                  &declared_trial, NULL, NULL, ghost, config, (PortableMeshKartInput){0},
                  (PortableMeshItemControls){0}, &declared_kart, &declared_items, &declared_actors,
                  error, sizeof error) == PORTABLE_MESH_UNSUPPORTED);
        CHECK(memcmp(&declared_trial, &before_declared, sizeof declared_trial) == 0 &&
              memcmp(ghost, fresh, sizeof *fresh) == 0 &&
              memcmp(&declared_kart, &before_kart, sizeof declared_kart) == 0 &&
              memcmp(&declared_items, &before_items, sizeof declared_items) == 0 &&
              memcmp(&declared_actors, &before_actors, sizeof declared_actors) == 0);
    }
    CHECK(portable_mesh_replay_write_header_actors(&writer, &legacy->identity, &trial, NULL, NULL,
                                                   1, actors,
                                                   config) == PORTABLE_MESH_REPLAY_INVALID);
    CHECK(output.size == header_size);
    unsigned ticks = 0;
    while (trial.state.phase != PORTABLE_MESH_TT_FINISHED && ticks < 1000)
    {
        PortableMeshKartEvents kart_events;
        PortableMeshItemEvents item_events;
        PortableMeshCourseActorEvents actor_events;
        const PortableMeshKartInput input = {0, PORTABLE_MESH_ACCELERATE};
        CHECK(portable_mesh_time_trial_tick_actors(&trial, NULL, NULL, actors, config, input,
                                                   (PortableMeshItemControls){0}, &kart_events,
                                                   &item_events, &actor_events, error,
                                                   sizeof error) == PORTABLE_MESH_OK);
        ++ticks;
        CHECK(actors->sky.clock == ticks);
        CHECK(portable_mesh_replay_write_tick_actors(&writer, input, (PortableMeshItemControls){0},
                                                     &trial, NULL,
                                                     actors) == PORTABLE_MESH_REPLAY_OK);
    }
    CHECK(trial.state.phase == PORTABLE_MESH_TT_FINISHED);
    CHECK(portable_mesh_replay_write_end_actors(&writer, &trial, NULL, actors) ==
          PORTABLE_MESH_REPLAY_OK);
    const size_t size = output.size;
    CHECK(portable_mesh_replay_write_tick_actors(
              &writer, (PortableMeshKartInput){0}, (PortableMeshItemControls){.item_pressed = 1},
              &trial, NULL, actors) == PORTABLE_MESH_REPLAY_INVALID);
    CHECK(output.size == size);
    CHECK(size == HEADER + STATE_BYTES + (ticks + 1u) * RECORD + STATE_BYTES);
    CHECK(portable_mesh_replay_validate(bytes, size, &legacy->identity, NULL) ==
          PORTABLE_MESH_REPLAY_COMPLETE);
    PortableReplayMemoryInput memory = {bytes, size, 0};
    PortableReplayReader reader = portable_replay_memory_reader(&memory);
    PortableMeshReplayHeader header;
    CHECK(portable_mesh_replay_inspect_header(&reader, &header) == PORTABLE_MESH_REPLAY_OK);
    CHECK(header.version == 3 && !header.item_profile_id && !header.initial_item_checksum &&
          header.actor_profile_id == 41 && header.actor_profile_revision == 7);
    PortableMeshTimeTrial replay = legacy->initial, other = legacy->initial;
    PortableMeshReplayPlayback playback, independent;
    *ghost = *fresh;
    memory.position = 0;
    CHECK(portable_mesh_replay_playback_init_actors(&playback, &reader, &legacy->identity, &replay,
                                                    NULL, NULL, ghost,
                                                    config) == PORTABLE_MESH_REPLAY_OK);
    PortableReplayMemoryInput other_memory = {bytes, size, 0};
    PortableReplayReader other_reader = portable_replay_memory_reader(&other_memory);
    PortableMeshCourseActors *other_world = malloc(sizeof *other_world);
    CHECK(other_world);
    *other_world = *fresh;
    CHECK(portable_mesh_replay_playback_init_actors(&independent, &other_reader, &legacy->identity,
                                                    &other, NULL, NULL, other_world,
                                                    config) == PORTABLE_MESH_REPLAY_OK);
    CHECK(portable_mesh_replay_playback_tick(&playback, NULL) == PORTABLE_MESH_REPLAY_OK);
    CHECK(ghost->sky.clock == 1 && other_world->sky.clock == 0 && other.state.tick == 0);
    while (playback.status == PORTABLE_MESH_REPLAY_OK)
        (void)portable_mesh_replay_playback_tick(&playback, NULL);
    CHECK(playback.status == PORTABLE_MESH_REPLAY_COMPLETE && replay.state.tick == ticks);
    CHECK(portable_mesh_course_actors_checksum(ghost) ==
          portable_mesh_course_actors_checksum(actors));
    CHECK(portable_mesh_time_trial_checksum(&replay.state) ==
          portable_mesh_time_trial_checksum(&trial.state));
    PortableMeshCourseActorEvents complete_events;
    memset(&complete_events, 0xa5, sizeof complete_events);
    const uint64_t final_hash = portable_mesh_course_actors_checksum(ghost);
    CHECK(portable_mesh_replay_playback_tick_actor_events(
              &playback, NULL, NULL, NULL, &complete_events) == PORTABLE_MESH_REPLAY_COMPLETE);
    const PortableMeshCourseActorEvents no_events = {0};
    CHECK(memcmp(&complete_events, &no_events, sizeof no_events) == 0);
    CHECK(portable_mesh_course_actors_checksum(ghost) == final_hash);
    memcpy(broken, bytes, size);
    broken[120] ^= 1;
    PortableReplayMemoryInput wrong_initial = {broken, size, 0};
    PortableReplayReader wrong_reader = portable_replay_memory_reader(&wrong_initial);
    PortableMeshReplayPlayback sentinel;
    memset(&sentinel, 0xa5, sizeof sentinel);
    const PortableMeshReplayPlayback sentinel_before = sentinel;
    PortableMeshTimeTrial untouched = legacy->initial;
    *other_world = *fresh;
    CHECK(portable_mesh_replay_playback_init_actors(&sentinel, &wrong_reader, &legacy->identity,
                                                    &untouched, NULL, NULL, other_world,
                                                    config) == PORTABLE_MESH_REPLAY_INCOMPATIBLE);
    CHECK(memcmp(&sentinel, &sentinel_before, sizeof sentinel) == 0 && untouched.state.tick == 0 &&
          memcmp(other_world, fresh, sizeof *fresh) == 0);
    /* Every canonical-zero item extension field is independently enforced. */
    const unsigned offsets[] = {88,
                                92,
                                96,
                                100,
                                104,
                                HEADER + STATE_BYTES + 16,
                                HEADER + STATE_BYTES + 20,
                                HEADER + STATE_BYTES + 24};
    for (unsigned n = 0; n < sizeof offsets / sizeof offsets[0]; ++n)
    {
        memcpy(broken, bytes, size);
        broken[offsets[n]] = 1;
        CHECK(portable_mesh_replay_validate(broken, size, &legacy->identity, NULL) ==
              PORTABLE_MESH_REPLAY_INVALID);
    }
    memcpy(broken, bytes, size);
    broken[HEADER + STATE_BYTES + 32] ^= 1;
    memory = (PortableReplayMemoryInput){broken, size, 0};
    reader = portable_replay_memory_reader(&memory);
    replay = legacy->initial;
    *ghost = *fresh;
    CHECK(portable_mesh_replay_playback_init_actors(&playback, &reader, &legacy->identity, &replay,
                                                    NULL, NULL, ghost,
                                                    config) == PORTABLE_MESH_REPLAY_OK);
    PortableMeshKartEvents ke, ke_before;
    PortableMeshItemEvents ie, ie_before;
    PortableMeshCourseActorEvents ae, ae_before;
    memset(&ke, 0xa5, sizeof ke);
    ke_before = ke;
    memset(&ie, 0xa5, sizeof ie);
    ie_before = ie;
    memset(&ae, 0xa5, sizeof ae);
    ae_before = ae;
    CHECK(portable_mesh_replay_playback_tick_actor_events(&playback, NULL, &ke, &ie, &ae) ==
          PORTABLE_MESH_REPLAY_DIVERGED);
    CHECK(replay.state.tick == 0 && memcmp(ghost, fresh, sizeof *ghost) == 0);
    CHECK(memcmp(&ke, &ke_before, sizeof ke) == 0 && memcmp(&ie, &ie_before, sizeof ie) == 0 &&
          memcmp(&ae, &ae_before, sizeof ae) == 0);
    CHECK(portable_mesh_replay_validate(bytes, size - 1, &legacy->identity, NULL) ==
          PORTABLE_MESH_REPLAY_TRUNCATED);
    memcpy(broken, bytes, size);
    broken[size] = 0;
    CHECK(portable_mesh_replay_validate(broken, size + 1, &legacy->identity, NULL) ==
          PORTABLE_MESH_REPLAY_INVALID);
    free(other_world);
    free(broken);
    free(bytes);
    free(ghost);
    free(fresh);
    free(actors);
    free(config);
}
int main(void)
{
    Recording recording;
    record_course(&recording);
    check_playback(&recording);
    malformed_checks(&recording);
    store_checks(&recording);
    sky_only_replay(&recording);
    free(recording.states);
    free(recording.bytes);
    puts(
        "Mesh TT replay/store: PASS full-state deterministic finish, analog input, strict framing and separate atomic saves");
    return 0;
}
