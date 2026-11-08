#include "gp_results_session.h"

#include <stdio.h>

/* Authored record-art order, independent of the source donor permutation. */
static const uint8_t record_identities[8]={7,6,5,4,3,2,1,0};
static const uint8_t menu_option_codes[5]={0,3,1,2,4};
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition); \
    return __LINE__; \
} } while (0)

static PortableGpResultsSessionConfig make_config(void)
{
    PortableGpResultsSessionConfig config;
    memset(&config, 0, sizeof config);
    config.record_identities=record_identities;config.menu_option_codes=menu_option_codes;
    config.race_flags = UINT16_C(0x2202);
    config.course_in_cup = 3u;
    config.cup_record_changed = 1u;
    config.course_record_changed = 1u;
    for (uint8_t index = 0u; index < PORTABLE_GP_RESULTS_MAX_ROWS; ++index)
        config.times_centiseconds[index] = (uint16_t)(6000u + index * 200u);
    for (uint8_t index = 0u; index < PORTABLE_GP_RESULTS_RECORD_ROW_COUNT;
         ++index)
    {
        config.records[index].flags = index < 5u ? index : UINT32_C(0x10);
        config.records[index].time_centiseconds = (uint16_t)(6000u + index * 200u);
        config.records[index].emphasized = (uint8_t)(index == 0u);
    }
    return config;
}

int main(void)
{
    PortableGpResultsSession session;
    PortableGpResultsSessionConfig config = make_config();
    PortableGpResultsSessionStatus status = PORTABLE_GP_RESULTS_SESSION_OK;
    unsigned guard = 0u;

    CHECK(portable_gp_results_session_init(&session, &config) ==
          PORTABLE_GP_RESULTS_SESSION_OK);
    CHECK(session.presentation == PORTABLE_GP_RESULTS_PRESENT_RACE);
    while (session.flow.state < 11u && guard++ < 800u)
    {
        status = portable_gp_results_session_tick(&session, 0u, 0u);
        CHECK(status == PORTABLE_GP_RESULTS_SESSION_OK);
        if (session.song_count != 0u)
            CHECK(session.song_count == 1u && session.song_ids[0] == 14u);
    }
    CHECK(guard == 655u && session.flow.state == 11u);
    CHECK(session.presentation == PORTABLE_GP_RESULTS_PRESENT_MENU_RECORDS);
    CHECK(session.flow.record_row_count == PORTABLE_GP_RESULTS_RECORD_ROW_COUNT);

    CHECK(portable_gp_results_session_tick(&session, UINT16_C(0x80), 0u) ==
          PORTABLE_GP_RESULTS_SESSION_OK);
    CHECK(session.flow.menu_selection == 1u && session.song_count == 1u &&
          session.song_ids[0] == 141u);
    CHECK(portable_gp_results_session_tick(&session, 0u, UINT16_C(1)) ==
          PORTABLE_GP_RESULTS_SESSION_OK);
    CHECK(session.flow.state == 11u && session.song_count == 1u &&
          session.song_ids[0] == 145u);
    CHECK(portable_gp_results_session_tick(&session, UINT16_C(0x80), 0u) ==
          PORTABLE_GP_RESULTS_SESSION_OK);
    CHECK(session.flow.menu_selection == 2u && session.song_ids[0] == 141u);
    CHECK(portable_gp_results_session_tick(&session, 0u, UINT16_C(1)) ==
          PORTABLE_GP_RESULTS_SESSION_OK);
    CHECK(session.flow.state == 12u && session.song_ids[0] == 143u);
    CHECK(portable_gp_results_session_tick(&session, 0u, 0u) ==
          PORTABLE_GP_RESULTS_SESSION_COMPLETE);
    CHECK(session.complete && session.flow.state == 13u &&
          session.presentation == PORTABLE_GP_RESULTS_PRESENT_TERMINAL &&
          session.next_mode == 2u && session.scene_request_pending &&
          session.scene_request_value == UINT32_C(1));
    CHECK(portable_gp_results_session_tick(&session, 0u, 0u) ==
          PORTABLE_GP_RESULTS_SESSION_COMPLETE);

    CHECK(portable_gp_results_session_init(NULL, &config) ==
          PORTABLE_GP_RESULTS_SESSION_INVALID_ARGUMENT);
    config.course_in_cup = 4u;
    CHECK(portable_gp_results_session_init(&session, &config) ==
          PORTABLE_GP_RESULTS_SESSION_INVALID_ARGUMENT);
    puts("GP results session: PASS state0-13 songs selection and handoff");
    return 0;
}
