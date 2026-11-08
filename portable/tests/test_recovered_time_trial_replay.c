#include "../platform/recovered_replay_file.h"
#include "driver_item_fixture.h"
#include "driver_response_fixture.h"
#include "recovered_time_trial_replay.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "recovered replay test failed line %d: %s\n", \
                __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

typedef struct
{
    PortableRacePoint route[3];
    uint8_t cells[64];
    uint8_t terrain_cells[256];
    uint8_t material_flags[256];
    int16_t auto_acceleration[64];
    int32_t sine[1024];
    PortableTrackRaster raster;
    PortableTerrainView terrain;
    PortableTrackMaterialView material;
    PortableRecoveredRaceView race;
} Fixture;

static void initialize_fixture(Fixture *fixture)
{
    memset(fixture, 0, sizeof *fixture);
    fixture->route[0] = (PortableRacePoint){100, 100};
    fixture->route[1] = (PortableRacePoint){100, 50};
    fixture->route[2] = (PortableRacePoint){50, 50};
    fixture->raster = (PortableTrackRaster){
        .cells = fixture->cells,
        .capacity = sizeof fixture->cells,
        .width = 8u,
        .height = 8u,
    };
    fixture->terrain = (PortableTerrainView){
        .cells = fixture->terrain_cells,
        .row_stride = 16u,
        .width = 16u,
        .height = 16u,
    };
    fixture->material = (PortableTrackMaterialView){
        .flags = fixture->material_flags,
        .count = sizeof fixture->material_flags,
    };
    fixture->race.rules = (PortableRaceRulesView){
        .route = fixture->route,
        .route_count = 3u,
        .start_x_q16 = 100 * 65536,
        .start_y_q16 = 100 * 65536,
        .start_heading = 0u,
        .start_route_index = 2u,
        .progress_kind = PORTABLE_RACE_PROGRESS_DEMO_V1,
        .finish_kind = PORTABLE_FINISH_NORTHBOUND_Y,
        .finish_y_q16 = 100 * 65536,
        .lap_count = 3u,
        .countdown_ticks = 2u,
        .rescue_penalty_ticks = 180u,
        .compatibility = {
            .course_id = 24u, .course_revision = 3u,
            .rules_id = 7u, .rules_revision = 2u,
            .handling_profile_id = PORTABLE_HANDLING_RECOVERED_V1,
            .handling_revision = 2u, .character_class = 2u,
            .state_schema_version = 1u,
        },
    };
    for (size_t index = 0;
         index < sizeof fixture->race.rules.compatibility.deterministic_digest;
         ++index)
        fixture->race.rules.compatibility.deterministic_digest[index] =
            (uint8_t)(index * 7u + 3u);
    test_item_dependencies(&fixture->race.dependencies);
    fixture->race.dependencies.response_rules=portable_test_response_profile();
    fixture->race.dependencies.raster = &fixture->raster;
    fixture->race.dependencies.auto_acceleration_table =
        fixture->auto_acceleration;
    fixture->race.dependencies.auto_acceleration_count = 64u;
    fixture->race.dependencies.sine_table = fixture->sine;
    fixture->race.dependencies.sine_count = 1024u;
    fixture->race.dependencies.track_material = &fixture->material;
    fixture->race.dependencies.terrain = &fixture->terrain;
    fixture->race.initial_driver.pose.position_x =
        fixture->race.rules.start_x_q16;
    fixture->race.initial_driver.pose.position_y =
        fixture->race.rules.start_y_q16;
}

static void initialize_session(Fixture *fixture,
                               PortableRecoveredTimeTrialSession *session)
{
    CHECK(portable_recovered_time_trial_init(session, &fixture->race) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
}

static int diagnostics_contain(FILE *file, const char *needle)
{
    char text[1024];
    size_t count;
    CHECK(fflush(file) == 0);
    rewind(file);
    count = fread(text, 1, sizeof text - 1u, file);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

static int fail_at_end(void *context, uint8_t *byte)
{
    PortableReplayMemoryInput *input = context;
    if (input->position == input->size) return -1;
    *byte = input->bytes[input->position++];
    return 1;
}

static void memory_transport(void)
{
    Fixture *fixture = malloc(sizeof *fixture);
    PortableRecoveredTimeTrialSession *recorded = malloc(sizeof *recorded);
    PortableRecoveredTimeTrialSession *played = malloc(sizeof *played);
    uint8_t *bytes = malloc(32768u), *file_bytes = malloc(32768u);
    CHECK(fixture && recorded && played && bytes && file_bytes);
    initialize_fixture(fixture);
    initialize_session(fixture, recorded);
    initialize_session(fixture, played);
    PortableReplayMemoryOutput output = {bytes, 32768u, 0u};
    PortableReplayWriter writer = portable_replay_memory_writer(&output);
    FILE *file = tmpfile();
    CHECK(file != NULL);
    CHECK(portable_recovered_replay_write_header(&writer, recorded));
    CHECK(portable_recovered_replay_write_header_file(file, recorded));
    CHECK(output.size > strlen("MKSC-RECOVERED-REPLAY 66 "));
    CHECK(!memcmp(bytes, "MKSC-RECOVERED-REPLAY 66 ", strlen("MKSC-RECOVERED-REPLAY 66 ")));
    for (int pause = 1; pause >= 0; --pause) {
        const size_t before = output.size;
        CHECK(portable_recovered_replay_write_action(&writer, PORTABLE_RECOVERED_REPLAY_PAUSE, pause));
        CHECK(portable_recovered_replay_write_action_file(file, PORTABLE_RECOVERED_REPLAY_PAUSE, pause));
        CHECK(output.size - before == strlen("A pause 0\n"));
        CHECK(!memcmp(bytes + before, pause ? "A pause 1\n" : "A pause 0\n", strlen("A pause 0\n")));
        CHECK(portable_recovered_time_trial_set_paused(recorded, pause) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    }
    for (uint32_t tick = 0u; tick < 5u; ++tick) {
        PortableRaceTickDisposition disposition;
        const uint16_t buttons = tick == 2u ? PORTABLE_INPUT_ITEM : 0u;
        CHECK(portable_recovered_time_trial_tick(recorded, buttons, &disposition) ==
              PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(portable_recovered_replay_write_tick(&writer, tick, buttons, disposition, recorded));
        CHECK(portable_recovered_replay_write_tick_file(file, tick, buttons, disposition, recorded));
    }
    CHECK(portable_recovered_replay_write_action(&writer, PORTABLE_RECOVERED_REPLAY_RESTART, 0));
    CHECK(portable_recovered_replay_write_action_file(file, PORTABLE_RECOVERED_REPLAY_RESTART, 0));
    CHECK(portable_recovered_time_trial_restart(recorded) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(fflush(file) == 0);
    rewind(file);
    CHECK(fread(file_bytes, 1u, 32768u, file) == output.size);
    CHECK(!memcmp(file_bytes, bytes, output.size));
    CHECK(fclose(file) == 0);

    PortableReplayMemoryInput input = {bytes, output.size, 0u};
    PortableReplayReader reader = portable_replay_memory_reader(&input);
    uint8_t diagnostics[256];
    PortableReplayMemoryOutput messages = {diagnostics, sizeof diagnostics - 1u, 0u};
    PortableReplayWriter diagnostic_writer = portable_replay_memory_writer(&messages);
    size_t ticks = 99u;
    CHECK(portable_recovered_replay_run(&reader, played, &diagnostic_writer, &ticks));
    CHECK(ticks == 5u && !reader.failed && !diagnostic_writer.failed);
    CHECK(portable_recovered_replay_state_checksum(played) == portable_recovered_replay_state_checksum(recorded));
    diagnostics[messages.size] = 0u;
    CHECK(strcmp((const char *)diagnostics, "recovered replay: PASS 5 tick(s)\n") == 0);

    initialize_session(fixture, played);
    input.position = 0u;
    reader = (PortableReplayReader){&input, fail_at_end, 0};
    CHECK(!portable_recovered_replay_run(&reader, played, NULL, &ticks));
    CHECK(reader.failed && ticks == 0u);
    output.capacity = 5u; output.size = 0u;
    bytes[5] = 0xA5u;
    writer = portable_replay_memory_writer(&output);
    CHECK(!portable_recovered_replay_write_header(&writer, recorded));
    CHECK(writer.failed && output.size == 5u && bytes[5] == 0xA5u);
    free(file_bytes); free(bytes); free(played); free(recorded); free(fixture);
}

static void replace_token(char *output, size_t capacity, const char *text,
                          unsigned token, const char *replacement)
{
    const char *begin = text;
    for (unsigned i = 0u; i < token; ++i) {
        while (*begin && isspace((unsigned char)*begin)) ++begin;
        while (*begin && !isspace((unsigned char)*begin)) ++begin;
    }
    while (*begin && isspace((unsigned char)*begin)) ++begin;
    const char *end = begin;
    while (*end && !isspace((unsigned char)*end)) ++end;
    CHECK(begin != end);
    const size_t prefix = (size_t)(begin - text), length = strlen(replacement);
    CHECK(prefix + length + strlen(end) < capacity);
    memcpy(output, text, prefix);
    memcpy(output + prefix, replacement, length);
    strcpy(output + prefix + length, end);
}

static void strict_digests(void)
{
    Fixture *fixture = malloc(sizeof *fixture);
    PortableRecoveredTimeTrialSession *session = malloc(sizeof *session);
    CHECK(fixture && session);
    initialize_fixture(fixture);
    /* A permissive numeric decoder would accept +1 as the valid byte 01. */
    fixture->race.rules.compatibility.deterministic_digest[0] = 1u;
    initialize_session(fixture, session);
    uint8_t bytes[8192];
    PortableReplayMemoryOutput output = {bytes, sizeof bytes - 1u, 0u};
    PortableReplayWriter writer = portable_replay_memory_writer(&output);
    CHECK(portable_recovered_replay_write_header(&writer, session));
    CHECK(output.size >= 65u && bytes[output.size - 1u] == '\n');
    const size_t digest_offset = output.size - 65u;
    CHECK(bytes[digest_offset] == '0' && bytes[digest_offset + 1u] == '1');
    PortableRaceTickDisposition disposition;
    CHECK(portable_recovered_time_trial_tick(session, 0u, &disposition) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(portable_recovered_replay_write_tick(&writer, 0u, 0u, disposition, session));
    const uint32_t expected = portable_recovered_replay_state_checksum(session);
    /* Both hex letter cases encode the same identity. */
    for (size_t i = 0u; i < 64u; ++i)
        if (bytes[digest_offset + i] >= 'a' && bytes[digest_offset + i] <= 'f')
            bytes[digest_offset + i] = (uint8_t)(bytes[digest_offset + i] - 'a' + 'A');
    static const char pairs[][2] = {{'+', '1'}, {' ', '1'}, {'g', '1'}, {'1', '+'}};
    for (size_t i = 0u; i <= sizeof pairs / sizeof pairs[0]; ++i) {
        uint8_t text[8192];
        memcpy(text, bytes, output.size);
        if (i != 0u) memcpy(text + digest_offset, pairs[i - 1u], 2u);
        initialize_session(fixture, session);
        const uint32_t before = portable_recovered_replay_state_checksum(session);
        PortableReplayMemoryInput input = {text, output.size, 0u};
        PortableReplayReader reader = portable_replay_memory_reader(&input);
        size_t ticks = 99u;
        CHECK(portable_recovered_replay_run(&reader, session, NULL, &ticks) == (i == 0u));
        CHECK(ticks == (i == 0u ? 1u : 0u));
        CHECK(portable_recovered_replay_state_checksum(session) == (i == 0u ? expected : before));
    }
    free(session);
    free(fixture);
}

static void bounded_numbers(void)
{
    Fixture *fixture = malloc(sizeof *fixture);
    PortableRecoveredTimeTrialSession *session = malloc(sizeof *session);
    CHECK(fixture && session);
    initialize_fixture(fixture);
    initialize_session(fixture, session);
    uint8_t bytes[8192];
    PortableReplayMemoryOutput output = {bytes, sizeof bytes - 1u, 0u};
    PortableReplayWriter writer = portable_replay_memory_writer(&output);
    CHECK(portable_recovered_replay_write_header(&writer, session));
    const size_t header_size = output.size;
    PortableRaceTickDisposition disposition;
    CHECK(portable_recovered_time_trial_tick(session, 0u, &disposition) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(portable_recovered_replay_write_tick(&writer, 0u, 0u, disposition, session));
    bytes[output.size] = 0u;
    const uint32_t expected_checksum = portable_recovered_replay_state_checksum(session);
    char checksum_overflow[32];
    CHECK(snprintf(checksum_overflow, sizeof checksum_overflow, "1%08x",
                   portable_recovered_replay_state_checksum(session)) > 0);
    static const struct { unsigned token; const char *value; } cases[] = {
        {1u, "4294967362"}, {2u, "100000000"}, {3u, "4294967296"},
        {13u, "9223372036854775808"}, /* First tick index. */
        {17u, "9223372036854775808"}, /* First signed state field. */
        {17u, "-9223372036854775809"},
    };
    for (size_t i = 0u; i <= sizeof cases / sizeof cases[0]; ++i) {
        char text[8192];
        if (i == sizeof cases / sizeof cases[0])
            replace_token(text, sizeof text, (const char *)bytes, 16u, checksum_overflow);
        else replace_token(text, sizeof text, (const char *)bytes, cases[i].token, cases[i].value);
        initialize_session(fixture, session);
        const uint32_t before = portable_recovered_replay_state_checksum(session);
        PortableReplayMemoryInput input = {(const uint8_t *)text, strlen(text), 0u};
        PortableReplayReader reader = portable_replay_memory_reader(&input);
        size_t ticks = 99u;
        CHECK(!portable_recovered_replay_run(&reader, session, NULL, &ticks));
        CHECK(ticks == 0u && portable_recovered_replay_state_checksum(session) == before);
    }
    char text[8192];
    memcpy(text, bytes, header_size);
    strcpy(text + header_size, "A pause 4294967296\n");
    initialize_session(fixture, session);
    PortableReplayMemoryInput input = {(const uint8_t *)text, strlen(text), 0u};
    PortableReplayReader reader = portable_replay_memory_reader(&input);
    CHECK(!portable_recovered_replay_run(&reader, session, NULL, NULL));
    CHECK(!session->paused);
    /* Optional plus signs and hexadecimal prefixes remain valid wire input. */
    char variant[8192], checksum_text[32];
    replace_token(variant, sizeof variant, (const char *)bytes, 1u, "+66");
    replace_token(text, sizeof text, variant, 13u, "+0");
    CHECK(snprintf(checksum_text, sizeof checksum_text, "+0x%08x", expected_checksum) > 0);
    replace_token(variant, sizeof variant, text, 16u, checksum_text);
    initialize_session(fixture, session);
    input = (PortableReplayMemoryInput){(const uint8_t *)variant, strlen(variant), 0u};
    reader = portable_replay_memory_reader(&input);
    size_t ticks = 99u;
    CHECK(portable_recovered_replay_run(&reader, session, NULL, &ticks));
    CHECK(ticks == 1u && portable_recovered_replay_state_checksum(session) == expected_checksum);
    free(session); free(fixture);
}

static void test_modern_replay_extension(void)
{
    Fixture *fixture=malloc(sizeof *fixture);
    PortableRecoveredTimeTrialSession *session=malloc(sizeof *session);
    CHECK(fixture!=NULL&&session!=NULL);initialize_fixture(fixture);
    CHECK(portable_recovered_time_trial_init(session,&fixture->race)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    uint8_t bytes[8192],stock[8192];
    PortableReplayMemoryOutput output={bytes,sizeof bytes,0u};
    PortableReplayWriter writer=portable_replay_memory_writer(&output);
    CHECK(portable_recovered_replay_write_header(&writer,session));
    const size_t stock_size=output.size;memcpy(stock,bytes,stock_size);
    const uint32_t stock_hash=portable_recovered_replay_state_checksum(session);
    session->driver.modern_drift=(PortableModernDriftState){3u,5u,1u,2u,1u,2u};
    CHECK(portable_recovered_replay_state_checksum(session)==stock_hash);
    output.size=0u;writer=portable_replay_memory_writer(&output);
    CHECK(portable_recovered_replay_write_header(&writer,session));
    CHECK(output.size==stock_size&&memcmp(bytes,stock,stock_size)==0);

    PortableModernDriftRules rules={.profile_id=9u,.revision=2u,
        .charge_ticks={3u,6u,9u},.boost_ticks={5u,10u,15u},
        .cap_ratio_256={280u,300u,320u},.minimum_speed_ratio_256=128u,
        .acceleration_q16={65536,131072,196608}};
    fixture->race.dependencies.modern_drift_rules=&rules;
    CHECK(portable_recovered_replay_state_checksum(session)!=stock_hash);
    CHECK(portable_recovered_time_trial_set_paused(session,1)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    output.size=0u;writer=portable_replay_memory_writer(&output);
    CHECK(portable_recovered_replay_write_header(&writer,session));
    const size_t header_size=output.size;
    PortableRaceTickDisposition disposition;
    CHECK(portable_recovered_time_trial_tick(session,0u,&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(portable_recovered_replay_write_tick(&writer,0u,0u,disposition,session));
    PortableReplayMemoryInput input={bytes,output.size,0u};
    PortableReplayReader reader=portable_replay_memory_reader(&input);
    PortableRecoveredReplayHeader header;
    CHECK(portable_recovered_replay_inspect_header(&reader,&header));
    CHECK(header.version==67u&&input.position==header_size);
    CHECK(header.compatibility.rules_id==session->runtime.session.compatibility.rules_id);
    input.position=0u;reader=portable_replay_memory_reader(&input);size_t ticks=99u;
    CHECK(portable_recovered_replay_run(&reader,session,NULL,&ticks)&&ticks==1u);
    fixture->race.dependencies.modern_drift_rules=NULL;
    input.position=0u;reader=portable_replay_memory_reader(&input);
    CHECK(!portable_recovered_replay_run(&reader,session,NULL,&ticks));
    fixture->race.dependencies.modern_drift_rules=&rules;
    const PortableModernDriftState saved=session->driver.modern_drift;
    uint32_t hash=portable_recovered_replay_state_checksum(session);
#define MODERN_FIELD(field) \
    ++session->driver.modern_drift.field; \
    CHECK(portable_recovered_replay_state_checksum(session)!=hash); \
    session->driver.modern_drift=saved
    MODERN_FIELD(charge_ticks);MODERN_FIELD(boost_ticks);MODERN_FIELD(charge_tier);
    MODERN_FIELD(boost_tier);MODERN_FIELD(previous_held);MODERN_FIELD(last_applied_tier);
#undef MODERN_FIELD
    /* Dormant air state must not change any E8 v67 columns or checksum. */
    const size_t modern_size=output.size;memcpy(stock,bytes,modern_size);
    session->driver.modern_air=(PortableModernAirState){4u,7u,1u,1u,1u,1u};
    output.size=0u;writer=portable_replay_memory_writer(&output);
    CHECK(portable_recovered_replay_write_header(&writer,session));
    CHECK(portable_recovered_replay_write_tick(&writer,0u,0u,disposition,session));
    CHECK(output.size==modern_size&&memcmp(stock,bytes,modern_size)==0);
    rules.air=(PortableModernAirRules){.minimum_air_ticks=2u,.trick_window_ticks=12u,
        .boost_ticks=9u,.cap_ratio_256=300u,.minimum_speed_ratio_256=128u,
        .acceleration_q16=196608,.launch_mask=1u};
    output.size=0u;writer=portable_replay_memory_writer(&output);
    CHECK(portable_recovered_replay_write_header(&writer,session));
    CHECK(portable_recovered_replay_write_tick(&writer,0u,0u,disposition,session));
    input=(PortableReplayMemoryInput){bytes,output.size,0u};reader=portable_replay_memory_reader(&input);
    CHECK(portable_recovered_replay_inspect_header(&reader,&header)&&header.version==68u);
    input.position=0u;reader=portable_replay_memory_reader(&input);
    CHECK(portable_recovered_replay_run(&reader,session,NULL,&ticks)&&ticks==1u);
    rules.air.launch_mask=0u;
    input.position=0u;reader=portable_replay_memory_reader(&input);
    CHECK(!portable_recovered_replay_run(&reader,session,NULL,&ticks));
    rules.air.launch_mask=1u;
    const PortableModernAirState saved_air=session->driver.modern_air;
    hash=portable_recovered_replay_state_checksum(session);
#define AIR_FIELD(field) \
    ++session->driver.modern_air.field; \
    CHECK(portable_recovered_replay_state_checksum(session)!=hash); \
    session->driver.modern_air=saved_air
    AIR_FIELD(air_ticks);AIR_FIELD(boost_ticks);AIR_FIELD(armed);
    AIR_FIELD(tricked);AIR_FIELD(previous_hop);AIR_FIELD(last_applied);
#undef AIR_FIELD
    PortableRecoveredTimeTrialSnapshot *snapshot=malloc(sizeof *snapshot);CHECK(snapshot!=NULL);
    CHECK(portable_recovered_time_trial_snapshot(session,snapshot)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(snapshot->modern_air_enabled&&memcmp(&snapshot->modern_air,&saved_air,sizeof saved_air)==0);
    CHECK(portable_recovered_replay_state_checksum(session)==hash);free(snapshot);
    const char invalid[]="MKSC-RECOVERED-REPLAY 68 invalid\n";
    input=(PortableReplayMemoryInput){(const uint8_t *)invalid,sizeof invalid-1u,0u};
    reader=portable_replay_memory_reader(&input);
    PortableRecoveredReplayHeader preserved=header;
    CHECK(!portable_recovered_replay_inspect_header(&reader,&header));
    CHECK(memcmp(&header,&preserved,sizeof header)==0);
    free(session);free(fixture);
}

int main(void)
{
    test_modern_replay_extension();
    bounded_numbers();
    strict_digests();
    memory_transport();
    Fixture fixture;
    PortableRecoveredTimeTrialSession recorded;
    PortableRecoveredTimeTrialSession played;
    PortableRaceTickDisposition disposition;
    FILE *replay = tmpfile();
    FILE *diagnostics = tmpfile();
    size_t tick_count = 0;

    initialize_fixture(&fixture);
    initialize_session(&fixture, &recorded);
    CHECK(replay != NULL && diagnostics != NULL);
    CHECK(portable_recovered_replay_write_header_file(replay, &recorded));
    CHECK(portable_recovered_replay_write_action_file(
        replay, PORTABLE_RECOVERED_REPLAY_PAUSE, 1));
    CHECK(portable_recovered_time_trial_set_paused(&recorded, 1) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(portable_recovered_replay_write_action_file(
        replay, PORTABLE_RECOVERED_REPLAY_PAUSE, 0));
    CHECK(portable_recovered_time_trial_set_paused(&recorded, 0) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    for (uint32_t tick = 0; tick < 5u; ++tick)
    {
        uint16_t buttons = tick == 2u ? PORTABLE_INPUT_ITEM : 0u;
        CHECK(portable_recovered_time_trial_tick(
            &recorded, buttons, &disposition) ==
            PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(portable_recovered_replay_write_tick_file(
            replay, tick, buttons, disposition, &recorded));
    }
    CHECK(portable_recovered_replay_write_action_file(
        replay, PORTABLE_RECOVERED_REPLAY_RESTART, 0));
    CHECK(portable_recovered_time_trial_restart(&recorded) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(fflush(replay) == 0);
    rewind(replay);
    initialize_session(&fixture, &played);
    CHECK(portable_recovered_replay_run_file(
        replay, &played, diagnostics, &tick_count));
    CHECK(tick_count == 5u);
    CHECK(portable_recovered_replay_state_checksum(&played) ==
          portable_recovered_replay_state_checksum(&recorded));
    CHECK(diagnostics_contain(diagnostics,
                              "recovered replay: PASS 5 tick(s)"));
    CHECK(fclose(diagnostics) == 0 && fclose(replay) == 0);

    /* Pending and displayed lap digits are replay state even while gameplay
     * progress is unchanged; omitting either would hide a presentation drift. */
    initialize_session(&fixture, &recorded);
    initialize_session(&fixture, &played);
    ++played.lap_hud.pending_lap;
    CHECK(portable_recovered_replay_state_checksum(&played) !=
          portable_recovered_replay_state_checksum(&recorded));
    played.lap_hud = recorded.lap_hud;
    ++played.lap_hud.displayed_lap;
    CHECK(portable_recovered_replay_state_checksum(&played) !=
          portable_recovered_replay_state_checksum(&recorded));
    played.lap_hud = recorded.lap_hud;
    played.lap_hud.initialized ^= 1u;
    CHECK(portable_recovered_replay_state_checksum(&played) !=
          portable_recovered_replay_state_checksum(&recorded));

    /* The header binds both compatibility identity and initial state. */
    replay = tmpfile();
    diagnostics = tmpfile();
    initialize_session(&fixture, &recorded);
    CHECK(replay != NULL && diagnostics != NULL);
    CHECK(portable_recovered_replay_write_header_file(replay, &recorded));
    CHECK(fflush(replay) == 0);
    rewind(replay);
    initialize_session(&fixture, &played);
    ++played.runtime.session.compatibility.handling_revision;
    CHECK(!portable_recovered_replay_run_file(
        replay, &played, diagnostics, &tick_count));
    CHECK(diagnostics_contain(diagnostics, "compatibility mismatch"));
    CHECK(fclose(diagnostics) == 0 && fclose(replay) == 0);

    /* Corrupt a named state field and require stable first-field diagnostics. */
    replay = tmpfile();
    diagnostics = tmpfile();
    initialize_session(&fixture, &recorded);
    CHECK(replay != NULL && diagnostics != NULL);
    CHECK(portable_recovered_replay_write_header_file(replay, &recorded));
    CHECK(portable_recovered_time_trial_tick(
        &recorded, 0u, &disposition) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(portable_recovered_replay_write_tick_file(
        replay, 0u, 0u, disposition, &recorded));
    CHECK(fflush(replay) == 0);
    rewind(replay);
    {
        char contents[8192];
        char *position;
        FILE *corrupt = tmpfile();
        size_t count = fread(contents, 1, sizeof contents - 1u, replay);
        contents[count] = '\0';
        position = strstr(contents, " 6553600 6553600 ");
        CHECK(position != NULL && corrupt != NULL);
        position[2] = '7';
        CHECK(fwrite(contents, 1, count, corrupt) == count);
        rewind(corrupt);
        initialize_session(&fixture, &played);
        CHECK(!portable_recovered_replay_run_file(
            corrupt, &played, diagnostics, &tick_count));
        CHECK(diagnostics_contain(diagnostics, "position_x_q16"));
        CHECK(fclose(corrupt) == 0);
    }
    CHECK(fclose(diagnostics) == 0 && fclose(replay) == 0);

    /* Actor timers and visibility belong to replay state, including countdown. */
    PortableLavaBubble bubble;
    CHECK(portable_lava_bubble_init(&bubble,100*65536,100*65536,0u,0u));
    initialize_session(&fixture,&recorded);
    initialize_session(&fixture,&played);
    CHECK(portable_lava_bubble_course_init(&recorded.lava_bubbles,&bubble,1u));
    CHECK(portable_lava_bubble_course_init(&played.lava_bubbles,&bubble,1u));
    PortableThwomp thwomp;
    CHECK(portable_thwomp_init(&thwomp,100*65536,200*65536,0u,11u));
    CHECK(portable_thwomp_course_init(&recorded.thwomps,&thwomp,1u));
    CHECK(portable_thwomp_course_init(&played.thwomps,&thwomp,1u));
    PortableCrab crab;
    CHECK(portable_crab_init(&crab,100*65536,300*65536,0u,0x8001u));
    CHECK(portable_crab_course_init(&recorded.crabs,&crab,1u));
    CHECK(portable_crab_course_init(&played.crabs,&crab,1u));
    PortableSeagull seagull;
    CHECK(portable_seagull_init(&seagull,100*65536,400*65536,0u,0u));
    CHECK(portable_seagull_course_init(&recorded.seagulls,&seagull,1u));
    CHECK(portable_seagull_course_init(&played.seagulls,&seagull,1u));
    PortablePiranha piranha;
    CHECK(portable_piranha_init(&piranha,100*65536,400*65536,0u,0u));
    CHECK(portable_piranha_course_init(&recorded.piranhas,&piranha,1u));
    CHECK(portable_piranha_course_init(&played.piranhas,&piranha,1u));
    PortableFallingStar star;
    CHECK(portable_falling_star_init(&star,100*65536,400*65536,0u,0u));
    CHECK(portable_falling_star_course_init(&recorded.falling_stars,&star,1u));
    CHECK(portable_falling_star_course_init(&played.falling_stars,&star,1u));
    PortableThundercloud cloud;
    CHECK(portable_thundercloud_init(&cloud,100*65536,400*65536,0u,0u));
    CHECK(portable_thundercloud_course_init(&recorded.thunderclouds,&cloud,1u));
    CHECK(portable_thundercloud_course_init(&played.thunderclouds,&cloud,1u));
    PortableMouse mouse;
    CHECK(portable_mouse_init(&mouse,100*65536,400*65536,0u,0u));
    CHECK(portable_mouse_course_init(&recorded.mice,&mouse,1u));
    CHECK(portable_mouse_course_init(&played.mice,&mouse,1u));
    PortableSnowman snowman;
    CHECK(portable_snowman_init(&snowman,100*65536,400*65536,0u,0u));
    CHECK(portable_snowman_course_init(&recorded.snowmen,&snowman,1u));
    CHECK(portable_snowman_course_init(&played.snowmen,&snowman,1u));
    PortablePenguin penguin;
    CHECK(portable_penguin_init(&penguin,100*65536,400*65536,0u,0u));
    CHECK(portable_penguin_course_init(&recorded.penguins,&penguin,1u));
    CHECK(portable_penguin_course_init(&played.penguins,&penguin,1u));
    PortableBoo boo;
    CHECK(portable_boo_init(&boo,100*65536,400*65536,0u,0u));
    CHECK(portable_boo_course_init(&recorded.boos,&boo,1u));
    CHECK(portable_boo_course_init(&played.boos,&boo,1u));
    PortableCannonball cannonball;
    CHECK(portable_cannonball_init(&cannonball,100*65536,400*65536,0u,0u));
    CHECK(portable_cannonball_course_init(&recorded.cannonballs,&cannonball,1u));
    CHECK(portable_cannonball_course_init(&played.cannonballs,&cannonball,1u));
    PortableCourseProp prop;
    CHECK(portable_course_prop_init(&prop,100*65536,400*65536,0xc0000u,224u,0u));
    CHECK(portable_course_props_init(&recorded.course_props,&prop,1u));
    CHECK(portable_course_props_init(&played.course_props,&prop,1u));
    portable_course_prop_damage(&recorded.course_props.actors[0]);
    portable_course_prop_damage(&played.course_props.actors[0]);
    replay=tmpfile();CHECK(replay!=NULL);
    CHECK(portable_recovered_replay_write_header_file(replay,&recorded));
    for(uint32_t tick=0;tick<70u;++tick){
        CHECK(portable_recovered_time_trial_tick(&recorded,0u,&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(portable_recovered_replay_write_tick_file(replay,tick,0u,disposition,&recorded));
    }
    rewind(replay);
    CHECK(portable_recovered_replay_run_file(replay,&played,NULL,&tick_count));
    CHECK(tick_count==70u&&portable_recovered_replay_state_checksum(&played)==
          portable_recovered_replay_state_checksum(&recorded));
    ++played.course_props.actors[0].timer;
    CHECK(portable_recovered_replay_state_checksum(&played)!=portable_recovered_replay_state_checksum(&recorded));
    --played.course_props.actors[0].timer;
    played.course_props.actors[0].contact_phase^=1u;
    CHECK(portable_recovered_replay_state_checksum(&played)!=portable_recovered_replay_state_checksum(&recorded));
    played.course_props.actors[0].contact_phase^=1u;
    ++played.driver.palette_requests_194;
    CHECK(portable_recovered_replay_state_checksum(&played)!=portable_recovered_replay_state_checksum(&recorded));
    --played.driver.palette_requests_194;
    ++played.runtime.session.max_progress_raw;
    CHECK(portable_recovered_replay_state_checksum(&played) !=
          portable_recovered_replay_state_checksum(&recorded));
    --played.runtime.session.max_progress_raw;
    ++played.runtime.session.checkpoint_raw;
    CHECK(portable_recovered_replay_state_checksum(&played) !=
          portable_recovered_replay_state_checksum(&recorded));
    --played.runtime.session.checkpoint_raw;
    ++played.driver.palette_bank;
    CHECK(portable_recovered_replay_state_checksum(&played)!=portable_recovered_replay_state_checksum(&recorded));
    --played.driver.palette_bank;
    ++played.boos.actors[0].phase;
    CHECK(portable_recovered_replay_state_checksum(&played)!=
          portable_recovered_replay_state_checksum(&recorded));
    --played.boos.actors[0].phase;
    ++played.penguins.actors[0].timer;
    CHECK(portable_recovered_replay_state_checksum(&played)!=
          portable_recovered_replay_state_checksum(&recorded));
    --played.penguins.actors[0].timer;
    played.penguins.actors[0].affine_y_q8=-256;
    CHECK(portable_recovered_replay_state_checksum(&played)!=
          portable_recovered_replay_state_checksum(&recorded));
    played.penguins.actors[0].affine_y_q8=256;
    ++played.falling_stars.actors[0].angle;
    CHECK(portable_recovered_replay_state_checksum(&played)!=portable_recovered_replay_state_checksum(&recorded));
    --played.falling_stars.actors[0].angle;
    ++played.thunderclouds.actors[0].palette_timer;
    CHECK(portable_recovered_replay_state_checksum(&played)!=portable_recovered_replay_state_checksum(&recorded));
    --played.thunderclouds.actors[0].palette_timer;
    ++played.thunderclouds.palette_phase[1];
    CHECK(portable_recovered_replay_state_checksum(&played)!=portable_recovered_replay_state_checksum(&recorded));
    --played.thunderclouds.palette_phase[1];
    ++played.driver.lightning_timer_68;
    CHECK(portable_recovered_replay_state_checksum(&played)!=portable_recovered_replay_state_checksum(&recorded));
    --played.driver.lightning_timer_68;
    ++played.mice.actors[0].timer;
    CHECK(portable_recovered_replay_state_checksum(&played)!=portable_recovered_replay_state_checksum(&recorded));
    --played.mice.actors[0].timer;
    ++played.snowmen.actors[0].state;
    CHECK(portable_recovered_replay_state_checksum(&played)!=
          portable_recovered_replay_state_checksum(&recorded));
    --played.snowmen.actors[0].state;
    ++played.driver.contact_anchor_x_ee;
    CHECK(portable_recovered_replay_state_checksum(&played)!=
          portable_recovered_replay_state_checksum(&recorded));
    --played.driver.contact_anchor_x_ee;
    played.piranhas.actors[0].struck_player=1u;
    CHECK(portable_recovered_replay_state_checksum(&played)!=
          portable_recovered_replay_state_checksum(&recorded));
    played.piranhas.actors[0].struck_player=0u;
    ++played.piranha_sounds.serial;
    ++played.impact_sounds.serial;
    played.impact_sounds.count=1u;
    ++played.thwomp_sounds.serial;
    played.thwomp_sounds.count=1u;
    played.thwomp_sounds.volumes[0]=256u;
    CHECK(portable_recovered_replay_state_checksum(&played)==
          portable_recovered_replay_state_checksum(&recorded));
    ++played.seagulls.actors[0].timer;
    CHECK(portable_recovered_replay_state_checksum(&played)!=
          portable_recovered_replay_state_checksum(&recorded));
    --played.seagulls.actors[0].timer;
    ++played.driver.strong_spin_rate_b6;
    CHECK(portable_recovered_replay_state_checksum(&played)!=
          portable_recovered_replay_state_checksum(&recorded));
    --played.driver.strong_spin_rate_b6;
    ++played.scene_clock;
    CHECK(portable_recovered_replay_state_checksum(&played)!=
          portable_recovered_replay_state_checksum(&recorded));
    --played.scene_clock;
    ++played.cannonball_sounds.serial;
    played.cannonball_sounds.count=1u;
    played.cannonball_sounds.events[0]=(PortableCannonballSound){382u,128u,1u};
    CHECK(portable_recovered_replay_state_checksum(&played)==
          portable_recovered_replay_state_checksum(&recorded));
    --played.cannonball_sounds.serial;
    played.cannonball_sounds.count=0u;
    ++played.cannonballs.actors[0].timer;
    CHECK(portable_recovered_replay_state_checksum(&played)!=
          portable_recovered_replay_state_checksum(&recorded));
    --played.cannonballs.actors[0].timer;
    ++played.crabs.actors[0].animation_timer;
    CHECK(portable_recovered_replay_state_checksum(&played)!=
          portable_recovered_replay_state_checksum(&recorded));
    --played.crabs.actors[0].animation_timer;
    played.crabs.actors[0].flip_y^=1u;
    CHECK(portable_recovered_replay_state_checksum(&played)!=
          portable_recovered_replay_state_checksum(&recorded));
    played.crabs.actors[0].flip_y^=1u;
    ++played.driver.flattened_64;
    CHECK(portable_recovered_replay_state_checksum(&played)!=
          portable_recovered_replay_state_checksum(&recorded));
    --played.driver.flattened_64;
    ++played.driver.contact_guard_66;
    CHECK(portable_recovered_replay_state_checksum(&played)!=
          portable_recovered_replay_state_checksum(&recorded));
    --played.driver.contact_guard_66;
    ++played.thwomps.actors[0].timer;
    CHECK(portable_recovered_replay_state_checksum(&played)!=
          portable_recovered_replay_state_checksum(&recorded));
    --played.thwomps.actors[0].timer;
    ++played.lava_bubbles.actors[0].timer;
    CHECK(portable_recovered_replay_state_checksum(&played)!=
          portable_recovered_replay_state_checksum(&recorded));
    CHECK(fclose(replay)==0);
    puts("recovered Time Trial replay: PASS");
    return 0;
}
